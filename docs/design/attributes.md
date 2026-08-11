# 角色属性与装备系统

> **实施状态说明（以源码为准）：** M2 已实现 AttributeSet 四个 Incoming Meta 的原子结算、装备 Snapshot 差分、Pawn RuntimeHost 动态授予/撤销武器 Ability 与 Resource，以及玩家 IncomingHitResolver/反击 Token 基底。词条执行计算、正式受击/霸体 Ability、虫棍资源状态机及最终数据资产仍是后续方案。

## 当前实现

| 模块 | 当前状态 |
|------|----------|
| AttributeSet | 常规属性与 Clamp 已实现；`IncomingDamage/IncomingStagger/IncomingCriticalFlag/IncomingHitSignal` 为非复制 Meta，HitSignal 到达时一次读取清零、扣血、发硬直事件并组装反馈。Health 首次归零会关停 Pawn RuntimeHost。 |
| 装备基础属性 | `UMHGZEquipmentComponent` 当前通过 `SetNumericAttributeBase` 汇总 Attack/Defense/Crit，不是通过装备 GE 写入基础三围。 |
| 装备效果重建 | `OnEquipmentStatsChanged` 与 `OnEquippedWeaponChanged(FEquippedWeaponSnapshot)` 已分离。相同武器实例/RuntimeDefinition 不重建；真正换武器由 Character RuntimeHost 统一撤销 Coordinator/Ability、关闭 Resource、使旧 Token 失效并重建。 |
| 词条 | `FEntryReference/FEntryDefinition/FEntryModifier` 已定义，但 `ApplyEntryGEs` 当前为空；`GE_EntryStat` 与 `UExecCalc_EntryStat` 未实现。 |
| 武器资源映射 | 运行时只走 `UMHGZWeaponDefinition::RuntimeDefinition`，再取得 ResourceClass/InputProfile/CombatConfig；旧 WeaponComboConfig/DataManager Getter 与 Equipment DataTable 查询已删除。 |
| MoveSpeedMultiplier | 属性与 Clamp 已实现，但当前 Motion Matching 速度计算尚未读取该值。 |
| 受击/霸体 | `IncomingHitResolver` 已支持目标校验、AttackInstance 去重和按优先级/TTL 的当前 ActionToken 拦截；Damage ExecCalc 输出硬直 Meta，AttributeSet 发送硬直事件。正式受击 Ability、霸体比较和动画仍待后续。 |

**目标设计：** 装备基础数值与词条最终统一通过 GAS 表达。当前实现会直接用 `SetNumericAttributeBase` 重算 AttackPower/Defense/CriticalRate；只有后续词条/效果方案计划使用 GameplayEffect。装备变化时，已存在的装备 GE 通过 `RemoveActiveEffectsWithAppliedTags(Effect.Source.Equipment)` 清理。

属性约束：Health/Stamina 基础 100、上限 200；AttackPower/Defense 基础 0、无上限；CriticalRate 基础 0、范围 [-100, 100]；StaminaRegenRate/DeductionRate/ConsumptionRate 基础 1.0、下限 0。目标武器专属资源由 WeaponDefinition 引用的 WeaponRuntimeDefinition 决定，同种类武器共享运行时定义。

## UMHGZAttributeSet — 角色属性集

```
UCLASS()
class UMHGZAttributeSet : public UAttributeSet
```

| 属性 | 类型 | 基础值 | 上限 | 下限 | 说明 |
|------|------|:--:|:--:|:--:|------|
| Health | FGameplayAttributeData | 100 | 200 | 0 | 生命值 |
| MaxHealth | FGameplayAttributeData | 100 | 200 | 1 | 生命上限 |
| Stamina | FGameplayAttributeData | 100 | 200 | 0 | 耐力值 |
| MaxStamina | FGameplayAttributeData | 100 | 200 | 1 | 耐力上限 |
| StaminaRegenRate | FGameplayAttributeData | 1.0 | ∞ | 0 | 耐力恢复倍率 |
| StaminaDeductionRate | FGameplayAttributeData | 1.0 | ∞ | 0 | 单次动作扣耐倍率 |
| StaminaConsumptionRate | FGameplayAttributeData | 1.0 | ∞ | 0 | 持续耗耐倍率 |
| AttackPower | FGameplayAttributeData | 0 | ∞ | 0 | 攻击力 |
| Defense | FGameplayAttributeData | 0 | ∞ | 0 | 防御力 |
| CriticalRate | FGameplayAttributeData | 0 | 100 | -100 | 会心率（%） |
| StaggerMultiplier | FGameplayAttributeData | 1.0 | ∞ | 0 | ★ 破坏值倍率（**攻击方属性**——代表玩家造成怪物硬直的能力）。参与硬直计算：`Stagger = BaseStaggerValue(招式) × StaggerMultiplier(攻击方) × HitzoneStaggerRate(怪物部位)`。基础 1.0，通过装备词条 GE 加成（如"破坏王"技能珠 +0.3）。怪物侧无此属性——怪物硬直阈值由自身系统内部累积管理 |
| MoveSpeedMultiplier | FGameplayAttributeData | 1.0 | 3.0 | 0.1 | 移速倍率 Attribute 已实现；当前移动和 CMC 尚未消费该值 |

Demo 伤害迁移时新增四个不复制、不持久化的 Meta Attribute，基础值均为 0：

| Meta Attribute | 用途 |
|---|---|
| IncomingDamage | ExecCalc 计算出的本次待结算伤害 |
| IncomingStagger | 本次待提交硬直值 |
| IncomingCriticalFlag | 0/1 会心结果，保证反馈与本次随机结果一致 |
| IncomingHitSignal | 必须最后输出；PostGameplayEffectExecute 只在此信号到达时原子读取并清空整组 Meta |

> **武器专属资源不在 AttributeSet 中：** 怪猎武器资源系统极其多样——太刀气刃槽（色阶）、盾斧瓶计数+盾充能、大剑蓄力等级、操虫棍萃取、双刀鬼人槽等——无法用简单的 float Current/Max/Regen 统一概括。每种武器的资源由各自的 Ability/Component 管理，UI 按 `WeaponTypeTag` 查表选择对应的资源显示组件。旧 `bRequiresWeaponResource/WeaponResourceCost(float)` 在 M1 删除，改为通用 `FWeaponResourceCostSpec{CostType, Amount}` 数组，由当前 Resource 原子检查/提交；通用 GA 不解释 CostType。

### Clamp 约束

在 `PreAttributeChange` / `PostGameplayEffectExecute` 中执行：

- Health → [0, MaxHealth]
- Stamina → [0, MaxStamina]
- MaxHealth / MaxStamina → [1, 200]
- CriticalRate → [-100, 100]
- AttackPower / Defense → [0, ∞)
- StaggerMultiplier → [0, ∞)
- StaminaRegenRate / DeductionRate / ConsumptionRate → [0, ∞)
- MoveSpeedMultiplier → [0.1, 3.0]

### MoveSpeedMultiplier 消费方案（当前未接入，保留规划）

当前 `MoveSpeedMultiplier` 只完成 Attribute 和 Clamp；`CMC.MaxWalkSpeed` 固定为 1200，用于避免钳制 Root Motion，`CalcCruiseSpeed` 也尚未读取该倍率。以下事件驱动写回 CMC 的代码是旧的详细方案，若继续使用 Motion Matching，应改为缩放目标巡航速度，而不是降低 CMC 的 1200 上限。

```cpp
// UMHGZAttributeSet::PostGameplayEffectExecute
void UMHGZAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
    if (Data.EvaluatedData.Attribute == GetMoveSpeedMultiplierAttribute())
    {
        // GE 修改了移速倍率 → 立即同步到 CMC
        if (AMHGZCharacter* Character = GetTypedOuter<AMHGZCharacter>())
        {
            Character->UpdateMaxWalkSpeed();
        }
    }
}

// AMHGZCharacter::UpdateMaxWalkSpeed
void AMHGZCharacter::UpdateMaxWalkSpeed()
{
    if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
    {
        const float Multiplier = ASC->GetNumericAttribute(
            UMHGZAttributeSet::GetMoveSpeedMultiplierAttribute());
        GetCharacterMovement()->MaxWalkSpeed = BaseMaxWalkSpeed * Multiplier;
    }
}
```

> **为何用事件驱动而非 Tick：** MoveSpeedMultiplier 变化频率极低（换武器/喝药/奔跑——每秒最多几次），`PostGameplayEffectExecute` 在 GE Apply/Remove 时自动触发，零额外轮询开销。多个 GE 叠加修改时每次变化都触发回调，不存在"中间状态漏掉"的问题。`UpdateMaxWalkSpeed` 内部仅一行 Attribute 读取——调用频率低、开销可忽略。

| 移速来源 | 实现 | 说明 |
|----------|------|------|
| 收刀态基础速度 | `BaseMaxWalkSpeed`（角色构造函数中从 CMC 初始值缓存） | 所有武器收刀时移速相同 |
| 武器持刀移速差异 | 装备武器时 Apply GE 修改 `MoveSpeedMultiplier`（太刀 0.85、大剑 0.6） | 与装备其他属性（AttackPower/Defense）走同一 GE 路径，卸下时随 `Effect.Source.Equipment` 批量清空 |
| Buff 移速变化 | 加速药水/减速 debuff 通过 GE 修改 `MoveSpeedMultiplier` | 标准 GAS 属性叠加 |
| 奔跑加速 | 当前由 Character 的 `bSprintHeld` 选择 SprintCruise | GA_Sprint + GE + 持续扣耐是后续方案 |
| 持刀不可奔跑 | `SprintPressed` 检查 `Combat.State.Unsheathed` 后直接 return | 当前不是 GA 阻塞 |
| 重型武器笨重感 | 武器 GE 同时降低 `MaxAcceleration`（CMC 属性，通过 GE 修改或直接设置） | 起步/转向更慢；AnimBP 按 WeaponTypeTag 切换 BlendSpace 资产实现不同移动动画 |

## UMHGZEquipmentComponent — 装备 GE 管理组件

```
UCLASS(ClassGroup=(Equipment), BlueprintType)
class UMHGZEquipmentComponent : public UActorComponent
```

挂载到 PlayerState。管理装备槽位、GE 的创建/Apply/移除。直接与 `UMHGZEquipmentInstance` 对接，不需要中间状态结构体。

### 重要成员

| 成员 | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| EquippedItems | TMap\<FGameplayTag, TObjectPtr\<UMHGZEquipmentInstance\>\> | "Equipment\|State" | 空 | 已装备物品（Key=槽位Tag，Value=EquipmentInstance*。一个 TMap 存所有槽位） |

> 物品是否被装备/镶嵌的判定：直接读 `EquipmentInstance→Status`，O(1)。

### 重要方法

- `void EquipItem(FGameplayTag SlotTag, UMHGZEquipmentInstance* Item)`
  - 输入：槽位标签、装备实例。
  - 作用：`Item→SetStatus(Equipped)`，将装备装入 `EquippedItems` Map，触发 `OnEquipmentChanged()`。
  - 注意：若槽位已有装备，先自动 Unequip 旧装备。

- `void UnequipItem(FGameplayTag SlotTag)`
  - 输入：槽位标签。
  - 作用：槽位中原装备 `SetStatus(InStorage)`，从 `EquippedItems` 移除，触发 `OnEquipmentChanged()`。

- `void SocketAccessory(UMHGZEquipmentInstance* HostItem, UMHGZEquipmentInstance* Accessory, FName SocketName)`
  - 输入：宿主装备、饰品实例、孔位名。
  - 作用：`Accessory→SetStatus(Socketed)`，调用 `HostItem→SocketAccessory`，触发 `OnEquipmentChanged()`。
  - 注意：需先调用 `CanSocketAccessory` 验证。

- `void RemoveAccessory(UMHGZEquipmentInstance* HostItem, FName SocketName)`
  - 输入：宿主装备、孔位名。
  - 作用：拆除的饰品 `SetStatus(InStorage)`，调用 `HostItem→RemoveAccessory`，触发 `OnEquipmentChanged()`。

- `bool CanSocketAccessory(...)`（当前由 `UMHGZEquipmentInstance` 提供，`UMHGZEquipmentComponent` 没有该转发方法）
  - 输入：宿主装备、饰品、孔位名。
  - 输出：是否可镶入。
  - 作用：转发 `HostItem→CanSocketAccessory`，比较饰品等级与孔位等级。

### OnEquipmentChanged 全量重算（当前源码，M2 拆分）

`void OnEquipmentChanged()`
- 作用：装备变更的统一入口。任何 Equip/Unequip/Socket 操作最终都调用此方法。
- 设计思路：
  1. `ASC→RemoveWeaponAbilities()`，取消旧协调器并清除旧武器 Ability。
  2. `ASC→RemoveActiveEffectsWithAppliedTags(FGameplayTagContainer(Effect.Source.Equipment))`，清空装备 GE。
  3. 清理并销毁旧 WeaponResourceComponent。
  4. 遍历 `EquippedItems` → `ApplyItemEffects(Item)`，重建资源组件、连招与武器 Ability。
  5. `RecalculateEquipmentBaseAttributes(ASC)`，用 `SetNumericAttributeBase` 汇总基础三围。
- **性能说明（全量重算而非增量更新）：** 一次 `OnEquipmentChanged` 涉及 ~20-30 个 GE 的销毁与重建。装备变更仅在换装/镶嵌/拆除时触发——这些操作全部发生在非战斗期（工坊、准备阶段、菜单），频率极低（每秒 < 0.1 次）。全量重算的核心优势是**零中间状态**——不存在"忘记移除某个 GE"或"旧 GE 残留"的 bug，正确性由设计保证。性能实测后若不达预期，可优化为增量更新（仅 Remove/Apply 变更的 GE），但当前全量方案优先保证正确性。

### Demo 目标：统计与武器身份分流

- `OnEquipmentStatsChanged`：装备/护甲/饰品/镶嵌变化均可触发，只负责属性和可用词条重算。
- `OnEquippedWeaponChanged(FEquippedWeaponSnapshot)`：仅武器槽身份变化触发。Snapshot 包含 EquipmentInstance、WeaponDefinition、RuntimeDefinition 和 WeaponRevision。
- RuntimeHost 比较 EquipmentInstance/RuntimeDefinition 身份；相同武器的重复广播、护甲和饰品变化为 no-op，不销毁 Resource、不移除武器 GA、不清空精华/舞踏。
- 只有真正换武器/卸武器才由 RuntimeHost 执行完整 Shutdown/Initialize。EquipmentComponent 不再直接创建或销毁 Resource。

### ApplyItemEffects

`void ApplyItemEffects(UMHGZEquipmentInstance* Item)`
- 输入：装备实例。
- 作用：面向 `Item→Definition` + `Item→Customization` 创建 GE 并 Apply 到 ASC。
- 设计思路：
  1. `ApplyIntrinsicGE(ASC, Item→Definition, Item→GetCustomization())` — 读取覆盖后的有效数值 Apply GE。
  2. 收集有效词条（优先级：`ModifiedEntries` > `RemovedEntryIDs`）：
     a) 遍历 `Def→Entries` → 对每条 `FEntryReference`：
        - 若 `EntryID` 在 `Customization.ModifiedEntries` 中 → 使用修改后的等级
        - 若 `EntryID` 在 `Customization.RemovedEntryIDs` 中 → 跳过（删除）
        - 否则 → 使用原等级
     b) 追加 `Customization.AddedEntries`
     c) 将收集结果传入 `ApplyEntryGEs(ASC, EffectiveEntries)`。
  3. **仅当 `Item→Definition` 为 `UMHGZWeaponDefinition` 时：** 通过 `UMHGZDataManager::FindWeaponResourceConfig(WeaponTypeTag)` 查表 → 若匹配则 Apply 资源 GE。
  4. 遍历 `Item→SocketedAccessories` → `ApplyEntryGEs(ASC, AccDef)`。
  5. **仅当 `Item→Definition` 为 `UMHGZWeaponDefinition` 时：** 通过 `UMHGZDataManager::FindWeaponComboData(WeaponTypeTag)` 查表 → 若匹配则授予连招协调器 Ability。
  6. 所有 GE 创建时统一添加 `GrantedTags: Effect.Source.Equipment`。

### ApplyIntrinsicGE（规划，当前不存在该方法）

`void ApplyIntrinsicGE(UAbilitySystemComponent* ASC, UMHGZEquipmentDefinition* Def, const FItemCustomization& Custom)`
- 输入：ASC、装备定义、客制化覆写。
- 作用：读取有效数值（`Custom.StatOverrides` 覆盖 `Def` 原值；Key 为 `FGameplayTag`，与 `Attribute.*` 体系匹配）→ 创建对应 GE → Apply 到 ASC。

### ApplyEntryGEs — 词条分支处理（规划，当前函数体为空）

`void ApplyEntryGEs(UAbilitySystemComponent* ASC, UMHGZEquipmentDefinition* Def)`
- 输入：ASC、装备定义。
- 作用：遍历 `Def→Entries`，每个 `FEntryReference`：
  1. 调用 `UMHGZDataManager::FindEntryDefinition(EntryRef.EntryID)` 查 `DT_EntryCatalog` 获取 `FEntryDefinition`。
  2. 构造 GE Spec：`ASC→MakeOutgoingSpec(GE_EntryStat)` → `Spec→SetSetByCallerMagnitude("EntryID", EntryRef.EntryID)` → `Spec→SetSetByCallerMagnitude("EntryLevel", EntryRef.EntryLevel)`。
  3. 按 `EffectType` 分三种路径：
     - **SimpleStat：** Apply `GE_EntryStat`（`UExecCalc_EntryStat` 在 Execute 中从 Spec 读取 EntryID+EntryLevel 后查曲线计算数值）。
     - **Complex：** 实例化 `EffectClass` → Apply。
     - **WeaponResource：** Demo 明确禁用。当前多来源合并和目标参数过滤不可靠；Data Validation/内容审计拒绝 Demo 装备引用该路径，完整词条阶段重新设计来源句柄后再接入。
  4. 所有 GE 统一添加 `GrantedTags: Effect.Source.Equipment`。

> **多武器词条路由机制：** `FEntryModifier::AttributeTag` 作为路由键。`WeaponResource.LongSword.*` 仅长刀识别；`WeaponResource.Shared.*` 所有武器识别。不匹配→静默跳过。**前缀校验：** `ApplyEntryModifier` 检查 `MatchesTag("WeaponResource")`，不匹配则 Warning 日志+跳过。同理 UExecCalc_EntryStat 检查 `MatchesTag("Attribute")`。

> **复合词条处理：** `EffectType` 为单一枚举值——SimpleStat/Complex/WeaponResource 三种路径互斥。若一个词条需同时影响 Attribute 和 WeaponResource（如"攻击力+10 且气刃槽回复+20%"），采用以下策略：
> - **推荐——拆分为两个独立词条引用**：装备定义中挂两个 `FEntryReference`，一个 SimpleStat（走 `GE_EntryStat` → ExecCalc 改 Attribute），一个 WeaponResource（走 `ApplyEntryModifier` 改资源组件）。95% 的复合词条可用此方案解决。
> - **备选——Complex 自定义 GE**：创建 `GE_MasterSwordsman` 蓝图，在 GE 内同时配置 Attribute Modifiers 和自定义逻辑操作 ResourceComponent。适用于需要复杂条件判断的复合效果。

### Delegate

目标 Delegate：

- `FOnEquipmentStatsChanged` — 任意会改变装备属性/词条的操作后广播。
- `FOnEquippedWeaponChanged(const FEquippedWeaponSnapshot&)` — 仅当前武器身份变化后广播。

当前 `FOnEquipmentChanged` 只记录旧源码，M2 迁移后不继续作为武器运行时重建入口。

### WeaponResourceComponent 销毁时序（当前源码）

OnEquipmentChanged 内执行顺序：
1. `RemoveWeaponAbilities` — 取消旧协调器并清除旧武器 Ability
2. `RemoveActiveEffectsWithAppliedTags` — 清空旧装备 GE
3. `ClearAllEntryModifiers` → `DestroyComponent` — 清空并销毁旧 ResourceComponent
4. `ApplyItemEffects` 中按 WeaponResourceConfig `NewObject + RegisterComponent` 创建新组件
5. `ApplyEntryGEs` 当前为空；词条修饰器 Apply 是后续方案
6. `RecalculateEquipmentBaseAttributes` 重算基础三围

目标时序不再由 EquipmentComponent 直接销毁/创建 Resource；Equipment 重算持久装备/GE，并仅在武器身份变化时广播 Snapshot。Character RuntimeHost 按 [冻结实施计划 §3.4](demo-implementation-plan.md#34-武器资源宿主与清理顺序) 编排 Ability、Resource 和 UI。

## UWeaponRuntimeDefinition — 武器运行时定义（目标）

每种武器类型一个 PrimaryDataAsset，具体武器物品定义引用它。它只负责运行时接线，不保存单件武器攻击力/外观/词条。

| 列名 | 类型 | 说明 |
|------|------|------|
| WeaponTypeTag | FGameplayTag | 武器种类（主键） |
| ResourceComponentClass | TSubclassOf\<UMHGZWeaponResourceComponent\> | RuntimeHost 创建的资源组件类 |
| InputProfile | TSoftObjectPtr\<UWeaponInputProfile\> | 原始键、Chord 和方向阈值 |
| CombatConfig | TSoftObjectPtr\<UWeaponCombatConfigBase\> | 武器专属调参；虫棍为 UInsectGlaiveCombatConfig |
| ResourceWidgetClass | TSoftClassPtr\<UUserWidget\> | 资源 UI Widget 类 |

Demo 创建 `DA_WeaponRuntime_IG`，指向 `URes_InsectGlaive`、`DA_IG_InputProfile`、`DA_IG_Combat` 和 `WBP_IG_ResourcePanel`。虫棍 ComboData 由 CombatConfig 引用。`DT_WeaponResourceConfig` 资产不存在；M2 删除 `DT_WeaponComboConfig` 的运行时读取，E3 删除该旧资产，不保留第二入口。

## GE_EntryStat — 通用词条 GameplayEffect（规划，资产未创建）

所有 SimpleStat 词条共用此 GE 蓝图，不预设属性修饰符。

| 设置项 | 值 | 说明 |
|--------|-----|------|
| Duration Policy | Infinite | 装备在即持续 |
| Stacking | AggregateBySource | 不同词条可叠加 |
| Modifiers | 空 | 修饰符由 ExecCalc 运行时注入 |
| Calculation Class | UExecCalc_EntryStat | 自定义执行计算 |
| GrantedTags | Effect.Source.Equipment | 用于批量移除 |

## UExecCalc_EntryStat — 词条执行计算（规划，类未创建）

```
UCLASS()
class UExecCalc_EntryStat : public UGameplayEffectExecutionCalculation
```

纯 C++ 类，每次 GE_EntryStat 被 Apply 时 GAS 自动调用。

`void Execute_Implementation(const FGameplayEffectCustomExecutionParameters& Params, FGameplayEffectCustomExecutionOutput& Out)`
- 输入：`ExecutionParams`（含 EffectSpec 中 SetByCaller 注入的 EntryID 和 EntryLevel）。
- 输出：`OutExecutionOutput`（写入各属性的修改量和操作类型）。
- 作用：从 Spec 读 EntryID → 通过 `UMHGZDataManager` 查 DT_EntryCatalog 获取 `FEntryDefinition` → 遍历 Modifiers → 每条 `Curve.Eval(EntryLevel)`（CT_EntryMagnitudes 由 DataManager 提供）得数值 → 写入 `Out.Add(Attribute, Op, Value)`。

> **DataManager 访问方式：** ExecCalc 通过 `Params.GetSourceAbilitySystemComponent()->GetWorld()->GetGameInstance()->GetSubsystem<UMHGZDataManager>()` 获取 DataManager。所有全局 DataTable/CurveTable 引用集中在 DataManager，ExecCalc 不需要硬编码资产路径。

## 受击与霸体判定（玩家侧，规划；当前仅有未接通骨架）

**原则：自定义 GameplayEffectContext 是命中载荷，ExecCalc 只输出合法 Modifier。** 攻击方把真实 HitResult、攻击实例 ID、硬直等级和命中类型写入 Context；数值用 SetByCaller/捕获属性。ExecCalc 不在执行过程中修改 Spec、添加 DynamicTag 或发送事件。

**硬直触发采用 GameplayEvent（非 Tag Trigger）：** 使用 `ASC→HandleGameplayEvent(Combat.Event.HitStagger, &EventData)` 触发 GA_HitReaction，而非依赖 `Combat.State.Hitstun` Tag 的 Add/Remove 检测。理由：Tag Trigger 仅在 Tag 从无到有时触发——若目标已处于 Hitstun（正在播放受击动画），第二次命中添加同一 Tag 不会重新触发 GA_HitReaction，导致连打吞受击动画。GameplayEvent 每次调用独立触发，无此问题。`Combat.State.Hitstun` 仍保留——用于 `CanActivateAbility` 阻塞输入（GA_HitReaction 的 Activate 时添加，EndAbility 时移除）。

### 完整受击流程（三步，同步+异步）

```
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
步骤 1：攻击方 Apply GE Spec（攻击侧）
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
MakeDamageSpec → 构造 GE Spec：
  - SetByCaller: MotionValue, BaseStagger, 可选 AttackPower/DanceMultiplier
  - GameplayEffectContext: HitResult、HitStaggerTag、HitzoneTag、AttackInstanceID
→ SourceASC→ApplyGameplayEffectSpecToTarget(Spec, TargetASC)

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
步骤 2：目标 ExecCalc 执行（同步，const 纯计算）
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
UExecCalc_PlayerDamage::Execute（const 方法，不调用 HandleGameplayEvent）：
  1. 读目标 Tag：Invincible/Dead → return，不提交 HitSignal
  2. 从 Spec 读伤害值 → 套用 Defense 属性 → 得到 FinalDamage，但不直接修改 Health
  3. 从 Spec 读 HitStaggerTag → 与目标霸体 Tag 比较等级：
     无霸体 + 任意 Stagger → 需硬直
     有 Poise.Light + Stagger=Light → 无硬直（霸体足够）
     有 Poise.Light + Stagger=Medium → 需硬直（霸体不足）
     ... 类推
  4. 依次输出 `IncomingDamage`、`IncomingStagger`、`IncomingCriticalFlag` Meta
  5. 最后输出 `IncomingHitSignal=1`
     // 不修改 Spec、不直接修改 Health、不调用 HandleGameplayEvent
  （ExecCalc 不播放动画——它是 const 纯计算。动画由步骤 2.5+3 触发）

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
步骤 2.5：PostGameplayEffectExecute 触发硬直事件（同步，非 const）
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
UMHGZAttributeSet::PostGameplayEffectExecute（GE Apply 完成后 GAS 自动调用）：
  1. 只有 EvaluatedAttribute 为最后的 IncomingHitSignal 时开始结算；读取自定义 Context 的 HitStaggerTag/HitResult
  2. 原子读取并清零 IncomingDamage/IncomingStagger/IncomingCriticalFlag/IncomingHitSignal
  3. Clamp 并扣 Health，记录 ActualDamage；若 Stagger > 0，构造 FGameplayEventData → ASC→HandleGameplayEvent(Combat.Event.HitStagger)
  4. 用同一组 ActualDamage/会心/HitResult 生成一次 HitFeedbackResult；从 Target ASC ActorInfo 的 AvatarActor 查 HitFeedbackRouter，木桩无独立 Avatar 时才回退 OwnerActor

> 突进回旋斩的反击发生在 Apply GE 之前：Character 的 IncomingHitResolver 让当前反击 Token 检查并消费 AttackInstanceID；成功后不创建伤害 Spec，也不会进入本流程。普通受击才由 ExecCalc/Meta Attribute 触发硬直。

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
步骤 3：GA_HitReaction 自动激活（异步，播 Montage）
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
GA_HitReaction（继承 UMHGZGameplayAbility）：
  配置 AbilityTrigger → 监听 GameplayEvent(Combat.Event.HitStagger)
  InstancingPolicy = InstancedPerExecution  ← 每次事件创建新实例，支持连打
  Activate：
    - ASC→AddLooseGameplayTag(Combat.State.Hitstun)  // 阻塞移动/攻击输入
    - 从 EventData 读 HitResult → 冲击方向 → 选对应方向 Montage
    - 从 EventData 读 StaggerLevel → 选轻/中/重 Montage
    - 播放 Montage + 施加击退 Impulse（方向=HitResult normal）
    - Montage 播完 → EndAbility → 移除 Combat.State.Hitstun
```

> **玩家与怪物受击分离：** 本节仅描述玩家侧受击逻辑。怪物侧另设 `UExecCalc_MonsterDamage`（按 HitzoneBoneName 查部位防御 + 硬直阈值积累 + 部位破坏判定），与玩家 ExecCalc 共享 GE Spec 传递的数据，但计算逻辑完全不同。怪物硬直由怪物 AI 系统处理，不在本文档范围。

### 霸体等级比较算法——推荐方案 A（硬编码整数映射）

```cpp
static int32 GetStaggerLevel(FGameplayTag Tag)
{
    static TMap<FGameplayTag, int32> Map;
    if (Map.IsEmpty())
    {
        Map.Add(FGameplayTag::RequestGameplayTag("Combat.Stagger.Light"),  1);
        Map.Add(FGameplayTag::RequestGameplayTag("Combat.Stagger.Medium"), 2);
        Map.Add(FGameplayTag::RequestGameplayTag("Combat.Stagger.Heavy"),  3);
    }
    const int32* Found = Map.Find(Tag);
    return Found ? *Found : 0;
}

static int32 GetPoiseLevel(FGameplayTag Tag)
{
    static TMap<FGameplayTag, int32> Map;
    if (Map.IsEmpty())
    {
        Map.Add(FGameplayTag::RequestGameplayTag("Combat.Poise.Light"),  1);
        Map.Add(FGameplayTag::RequestGameplayTag("Combat.Poise.Medium"), 2);
        Map.Add(FGameplayTag::RequestGameplayTag("Combat.Poise.Heavy"),  3);
        Map.Add(FGameplayTag::RequestGameplayTag("Combat.Poise.Super"),  4);
    }
    const int32* Found = Map.Find(Tag);
    return Found ? *Found : 0;
}

// 霸体足够 = GetPoiseLevel(PoiseTag) >= GetStaggerLevel(StaggerTag)
```

### 霸体来源

- **攻击自带霸体**：攻击 Ability 的 Montage 上挂 `AnimNotifyState_PoiseWindow`，NotifyBegin 时 `ASC→AddLooseGameplayTag(Combat.Poise.X)`，NotifyEnd 时移除。策划拖拽区间 = 霸体持续帧
- **装备/Buff 提供的被动霸体**：通过 GE 持续性持有 `Combat.Poise.*`（如铁壁技能珠），属于角色属性层而非动作层
