# 第三人称游戏系统架构设计

> UE5.6 GAS 驱动。物品/属性/存储/使用/词条五大系统解耦。不含实现代码。

> **⚠️ 适用范围：** 当前版本仅针对单机/本地游戏设计，不涉及网络复制（Multiplayer/Replication）。所有 GameplayAbility、GameplayEffect、Attribute、装备状态同步方案将在后续版本补充。

---

## 一、物品系统

**设计原则：** 定义与实例分离——`UPrimaryDataAsset` 定义物品模板（策划可编辑），`UObject` 表示运行时实例（数量/镶嵌状态）。`bIsUsable` 用成员变量 bool（决定代码路径），物品类型用 GameplayTag（纯分类扩展）。装备槽位用类继承+Tag 双重标识（子类决定字段，Tag 决定运行时槽位匹配）。

### 1.1 FEquipmentSocket — 镶嵌孔位

```
USTRUCT(BlueprintType)
```

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| SocketName | FName | 必填 | 孔位名（如 "Gem_01"） |
| SocketLevel | int32 | 1 | 孔位等级（1-4），饰品等级 ≤ 此值方可镶入 |
| bIsLocked | bool | false | 是否锁定（需解锁） |

### 1.2 FEntryReference — 词条引用

```
USTRUCT(BlueprintType)
```

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| EntryID | FName | 必填 | 词条编号，匹配 DT_EntryCatalog 中的行 |
| EntryLevel | int32 | 1 | 当前词条等级，须 ≤ 词条定义的 MaxLevel |

> **纯数据结构——不持有查询方法。** 词条查询统一走 `UMHGZDataManager::FindEntryDefinition(EntryID)`（§6.3）。DataManager 是 GameInstanceSubsystem 全局单例，持有 DT_EntryCatalog 和 CT_EntryMagnitudes 引用，ExecCalc 和 EquipmentComponent 均可通过 `GetWorld()->GetGameInstance()->GetSubsystem<UMHGZDataManager>()` 获取。

### 1.3 FEntryModifier — 词条属性修饰器

```
USTRUCT(BlueprintType)
```

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| AttributeTag | FGameplayTag | 必填 | 目标属性标签 |
| ModifierOp | EGameplayModOp | Add | 操作类型（Add/Multiply/Override） |
| MagnitudeCurve | FCurveTableRowHandle | 必填 | 等级→数值曲线（X=等级, Y=数值）。支持非线性、跨级突变 |

### 1.4 FEntryDefinition — 词条目录行

```
USTRUCT(BlueprintType)
struct FEntryDefinition : public FTableRowBase
```

> **DataTable RowName 即 EntryID。** 不再在 struct 内部冗余存储 EntryID 字段。`FEntryReference::EntryID` 直接对应 DT_EntryCatalog 的行名。运行时查询通过 `UMHGZDataManager::FindEntryDefinition(EntryID)`（§6.3），内部调用 `DT_EntryCatalog→FindRow<FEntryDefinition>(EntryID)`。

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| DisplayName | FText | 空 | 词条名称 |
| Description | FText | 空 | 词条描述（UI 二级详情显示） |
| MaxLevel | int32 | 1 | 词条等级上限 |
| EntryTags | FGameplayTagContainer | 空 | 词条分类标签 |
| EffectType | EEntryEffectType | SimpleStat | SimpleStat=数值类 / Complex=行为类 |
| Modifiers | TArray\<FEntryModifier\> | 空 | 属性修饰列表（SimpleStat 时生效） |
| EffectClass | TSubclassOf\<UGameplayEffect\> | nullptr | 自定义 GE 蓝图（仅 Complex 时生效） |

### 1.5 UMHGZItemDefinition — 物品定义基类

```
UCLASS(BlueprintType, Abstract)
class UMHGZItemDefinition : public UPrimaryDataAsset
```

| 成员 | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| ItemID | FName | "Item\|Base" | NAME_None | 全局唯一标识 |
| DisplayName | FText | "Item\|Base" | 空 | 显示名称 |
| Description | FText | "Item\|Base" | 空 | 描述文本 |
| Icon | TSoftObjectPtr\<UTexture2D\> | "Item\|Base" | nullptr | 图标 |
| RarityLevel | int32 | "Item\|Base" | 1 | 稀有度 r1-r12，自动生成 `Item.Rarity.r{N}` Tag |
| ItemTags | FGameplayTagContainer | "Item\|Base" | 空 | 多维度分类标签 |
| bIsUsable | bool | "Item\|Base" | false | 是否可使用（决定能否进快捷栏） |
| BackpackMaxStack | int32 | "Item\|Base" | 1 | 背包堆叠上限 |
| Price | int32 | "Item\|Base" | 0 | 售价 |
| bCanDiscard | bool | "Item\|Base" | true | 是否可丢弃 |

方法：

- `bool IsEquipment() const`
  - 作用：虚函数，装备子类覆写返回 true。用于运行时分清装备/非装备。
- `FPrimaryAssetId GetPrimaryAssetId() const override`
  - 输出：FPrimaryAssetId。
  - 作用：返回 `FPrimaryAssetId("MHGZItemDefinition", ItemID)`，供 AssetManager 异步加载。

> **堆叠上限的职责归属：** `BackpackMaxStack` 是物品定义层的固有属性（同种物品在背包中的自然堆叠上限）。仓库 99999 是存储层的策略，不属于物品定义——`UMHGWarehouseComponent` 内部使用常量 `WAREHOUSE_MAX_STACK = 99999`，不依赖 Definition 的方法。

### 1.6 UMHGZEquipmentDefinition — 装备定义基类

```
UCLASS(BlueprintType, Abstract)
class UMHGZEquipmentDefinition : public UMHGZItemDefinition
```

构造函数设置 `bIsUsable=false`，`BackpackMaxStack=1`。

| 成员（追加） | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| EquipmentSlotTag | FGameplayTag | "Equipment" | 空 | 装备槽位标签（protected，子类构造函数设置，只读） |
| SocketSlots | TArray\<FEquipmentSocket\> | "Equipment\|Socket" | 空 | 镶嵌孔位（0-3 个） |
| Entries | TArray\<FEntryReference\> | "Equipment\|Entries" | 空 | 词条引用列表 |

方法（追加）：

- `bool IsEquipment() const override`
  - 输出：true。
- `bool HasSocketSlots() const`
  - 输出：SocketSlots 数组非空。
- `bool GetSocketByName(FName Name, FEquipmentSocket& Out) const`
  - 输入：孔位名。
  - 输出：通过 Out 返回孔位信息。返回值为是否找到。
- `bool CanSocketAccessory(int32 AccessoryLevel, FName SocketName) const`
  - 输入：饰品等级、孔位名。
  - 输出：是否可镶入。
  - 作用：比较 `AccessoryLevel ≤ SocketLevel`。

### 1.7 UMHGZWeaponDefinition — 武器定义

```
UCLASS(BlueprintType)
class UMHGZWeaponDefinition : public UMHGZEquipmentDefinition
```

构造函数设置 `EquipmentSlotTag = Equipment.Slot.Weapon`。

| 成员（追加） | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| WeaponTypeTag | FGameplayTag | "Weapon" | 空 | 武器种类标签 |
| AttackPower | float | "Weapon\|Combat" | 0 | 攻击力（装备时转为 GE 加成角色攻击力） |
| CriticalRate | float | "Weapon\|Combat" | 0 | 会心率（装备时转为 GE 加成角色会心率） |
| Mesh | TSoftObjectPtr\<USkeletalMesh\> | "Weapon\|Visual" | nullptr | 武器模型 |

### 1.8 UMHGZArmorDefinition — 衣服定义

```
UCLASS(BlueprintType)
class UMHGZArmorDefinition : public UMHGZEquipmentDefinition
```

构造函数设置 `EquipmentSlotTag = Equipment.Slot.Armor`。

| 成员（追加） | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| ArmorTypeTag | FGameplayTag | "Armor" | 空 | 护甲类型标签 |
| Defense | float | "Armor\|Combat" | 0 | 防御力（装备时转为 GE 加成角色防御力） |
| Mesh | TSoftObjectPtr\<USkeletalMesh\> | "Armor\|Visual" | nullptr | 衣服模型 |

### 1.9 UMHGZAccessoryDefinition — 饰品定义

```
UCLASS(BlueprintType)
class UMHGZAccessoryDefinition : public UMHGZEquipmentDefinition
```

构造函数设置 `EquipmentSlotTag = Equipment.Slot.Accessory`，`SocketSlots` 强制为空（饰品不可再镶嵌）。饰品不提供属性，仅通过词条 GE 生效。

| 成员（追加） | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| AccessoryLevel | int32 | "Accessory" | 1 | 饰品等级（1-4），须 ≤ 孔位等级方可镶入 |

### 1.10 UMHGZConsumableDefinition — 可使用物品定义

```
UCLASS(BlueprintType)
class UMHGZConsumableDefinition : public UMHGZItemDefinition
```

构造函数设置 `bIsUsable = true`。

| 成员（追加） | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| UseActionClass | TSubclassOf\<UMHGZUseAction\> | "Consumable\|Use" | nullptr | 使用行为类 |
| UseCooldown | float | "Consumable\|Use" | 0 | 使用冷却（秒） |
| UseAnimation | TSoftObjectPtr\<UAnimMontage\> | "Consumable\|Animation" | nullptr | 使用动画 |
| bConsumeOnUse | bool | "Consumable\|Use" | true | 使用后是否消耗 |

### 1.11 FItemCustomization — 物品客制化

```
USTRUCT(BlueprintType)
struct FItemCustomization
```

每件物品实例独立持有的客制化数据。对 Definition 的数值和词条做增量修改。

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| StatOverrides | TMap\<FGameplayTag, float\> | 空 | 属性覆写（Key=`Attribute.AttackPower`/`Attribute.CriticalRate`/`Attribute.Defense`，有则覆盖 Definition 值，无则用 Definition 原值）。Key 用 GameplayTag 匹配 Attribute 体系——与词条修饰器 `FEntryModifier::AttributeTag` 一致，避免魔法字符串拼写错误 |
| AddedEntries | TArray\<FEntryReference\> | 空 | 客制化新增的词条 |
| RemovedEntryIDs | TArray\<FName\> | 空 | 从 Definition.Entries 中移除的词条 ID |
| ModifiedEntries | TMap\<FName, int32\> | 空 | 覆盖 Definition.Entries 中同名 EntryID 的等级（Key=EntryID → Value=新 EntryLevel）。O(1) 查找，语义明确——"修改"而非"新增"。用于"词条升级"场景（如 AttackUp Lv2→Lv3），无需 Remove+Add 两步操作 |

> StatOverrides 支持增减：`Attribute.AttackPower`=20 覆盖原 15（+5），`Attribute.AttackPower`=10 覆盖原 15（-5）。AddedEntries 和 RemovedEntryIDs 让客制化可以增删词条。ModifiedEntries 用于覆盖已有词条的等级——`ApplyItemEffects` 收集有效词条时优先级：`ModifiedEntries` > `RemovedEntryIDs`（若同一 EntryID 同时出现在 ModifiedEntries 和 RemovedEntryIDs 中，以 ModifiedEntries 为准，即"升级后不删除"）。

### 1.12 UMHGZItemInstance — 物品实例（消耗品/材料）

```
UCLASS(BlueprintType)
class UMHGZItemInstance : public UObject
```

运行时对象，用于消耗品、材料、任务物品等在背包/仓库中管理的物品。

| 成员 | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| Definition | TObjectPtr\<UMHGZItemDefinition\> | "Instance" | nullptr | 指向物品定义（只读） |
| Quantity | int32 | "Instance" | 1 | 当前数量 |
| InstanceID | FGuid | "Instance" | NewGuid() | 存档唯一标识。构造函数中 `NewGuid()`；`PostLoad()` 中若 Guid 有效（从存档恢复）则保留，若 Guid 无效（旧版存档或新创建）则生成新 Guid。存/读档系统待后续实现 |

> **Guid 生命周期：** 构造函数始终 `NewGuid()`（运行时创建新物品）。`PostLoad()` 检查 `UPROPERTY()` 标记的 Guid 是否已从存档反序列化恢复——若已有效（非默认值）则保留，否则生成新 Guid。避免构造函数无条件覆盖已恢复的存档数据。

方法：

- `bool IsEquipment() const`
  - 输出：Definition→IsEquipment()。
- `bool IsUsable() const`
  - 输出：Definition→bIsUsable。
- `bool CanStackWith(const UMHGZItemInstance* Other) const`
  - 输入：另一个物品实例。
  - 输出：是否可堆叠。
  - 作用：同 Definition 且 Quantity 未达 `GetMaxStack()` 时返回 true。
- `int32 GetMaxStack() const`
  - 输出：最大堆叠数。
  - 作用：返回 `Definition→BackpackMaxStack`。仓库场景不使用此方法——仓库组件内部用常量 99999 自行管理堆叠逻辑。
- `bool IsStackFull() const`
  - 输出：Quantity 是否已达到 `GetMaxStack()`。
- `int32 AddQuantity(int32 Count)`
  - 输入：添加数量。
  - 输出：实际增加量（受 `GetMaxStack()` 约束，超出部分被截断）。
  - 注意：此方法仅用于背包场景（上限 = BackpackMaxStack）。仓库的存入操作由 `UMHGWarehouseComponent::DepositItem` 自行处理，不走此方法。
- `int32 RemoveQuantity(int32 Count)`
  - 输入：移除数量。
  - 输出：实际减少量（不低于 0，不会让 Quantity 变为负数）。
- `bool IsEmpty() const`
  - 输出：Quantity <= 0。

### 1.13 UMHGZEquipmentInstance — 装备实例（武器/衣服/饰品）

```
UCLASS(BlueprintType)
class UMHGZEquipmentInstance : public UMHGZItemInstance
```

继承 ItemInstance，用于武器/衣服/饰品。装备不占背包，Quantity 恒为 1，不可堆叠。客制化和镶嵌信息直接存储在实例上。

**EInstanceStatus 枚举：**
| 值 | 说明 |
|----|------|
| InStorage | 在仓库中，空闲 |
| Equipped | 装备在角色身上 |
| Socketed | 镶嵌在某件装备中 |

| 成员（追加） | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| **Status** | **EInstanceStatus** | **"Equipment\|State"** | **InStorage** | **⭐ 当前状态。由 EquipmentComponent 维护，UI 直接读取 O(1)** |
| Customization | FItemCustomization | "Equipment\|State" | 空 | 客制化覆写（饰品不可客制化，始终为空） |
| SocketedAccessories | TMap\<FName, TObjectPtr\<UMHGZEquipmentInstance\>\> | "Equipment\|State" | 空 | 已镶嵌饰品（Key=孔位名，作用域限本实例） |

方法（覆写/追加）：

- `bool CanStackWith(const UMHGZItemInstance* Other) const override`
  - 输出：恒为 false。
  - 作用：装备不可堆叠。
- `int32 GetMaxStack() const override`
  - 输出：恒为 1。
- `bool CanSocketAccessory(const UMHGZEquipmentInstance* Accessory, FName SocketName) const`
  - 输入：待镶嵌饰品、目标孔位名。
  - 输出：是否可镶入。
  - 作用：查 `Definition→GetSocketByName` 获取孔位等级，比较 `Accessory→Definition→AccessoryLevel ≤ SocketLevel`。
- `void SocketAccessory(UMHGZEquipmentInstance* Accessory, FName SocketName)`
  - 输入：饰品实例、孔位名。
  - 作用：将饰品写入 `SocketedAccessories` Map。
- `UMHGZEquipmentInstance* RemoveAccessory(FName SocketName)`
  - 输入：孔位名。
  - 输出：拆除的饰品实例指针。
  - 作用：从 `SocketedAccessories` 移除并返回。
- `void SetStatus(EInstanceStatus NewStatus)`
  - 输入：新状态。
  - 作用：修改 `Status` 字段。**这是修改 Status 的唯一入口**——`EquipmentComponent`、`WarehouseComponent` 等所有组件均通过此方法修改状态，确保单一真相源。方法内部广播 `FOnEquipmentInstanceStatusChanged` 委托供 UI 刷新。

Delegate：`FOnEquipmentInstanceStatusChanged` — Status 变更后广播（UI 订阅刷新装备状态标记）。

---

## 二、角色属性与装备系统

**设计原则：** 装备系统与角色属性完全解耦。装备不直接修改属性——`UMHGZEquipmentComponent` 读取装备的 AttackPower/Defense/CriticalRate 和词条引用，创建 GameplayEffect 授予 ASC。所有装备 GE 统一打 `Effect.Source.Equipment` 标签，装备变更时 `RemoveActiveEffectsWithTags` 一行清空，然后遍历已装备物品重新 Apply。不存 GE Handle，不追踪中间状态。属性约束：Health/Stamina 基础 100、上限 200；AttackPower/Defense 基础 0、无上限；CriticalRate 基础 0、范围 [-100, 100]；StaminaRegenRate/DeductionRate/ConsumptionRate 基础 1.0、下限 0。武器专属资源由 WeaponTypeTag 查 DT_WeaponResourceConfig 决定，同种类共享。

### 2.1 UMHGZAttributeSet — 角色属性集

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
| StaggerMultiplier | FGameplayAttributeData | 1.0 | ∞ | 0 | ★ 破坏值倍率。参与硬直计算：`Stagger = BaseStaggerValue(招式) × StaggerMultiplier × HitzoneStaggerRate(怪物部位)`。基础 1.0，通过装备词条 GE 加成（如"破坏王"技能珠 +0.3） |
| MoveSpeedMultiplier | FGameplayAttributeData | 1.0 | 3.0 | 0.1 | 移速倍率（驱动 CMC.MaxWalkSpeed。CMC 每 Tick 读取 `MoveSpeedMultiplier` 写回 `MaxWalkSpeed = BaseSpeed × Multiplier`，见下方同步说明）。GE 修改此值即可实现加速/减速 |

> **武器专属资源不在 AttributeSet 中：** 怪猎武器资源系统极其多样——太刀气刃槽（色阶）、盾斧瓶计数+盾充能、大剑蓄力等级、操虫棍萃取、双刀鬼人槽等——无法用简单的 float Current/Max/Regen 统一概括。每种武器的资源由各自的 Ability/Component 管理，UI 按 `WeaponTypeTag` 查表选择对应的资源显示组件。`DT_WeaponResourceConfig` 保留作为武器种类→资源类型的查找桥接（具体字段待各武器资源方案确定后补充）。`UMHGZGameplayAbility` 中的 `bRequiresWeaponResource` / `WeaponResourceCost` 保留为通用钩子——各武器 Ability 子类覆写实现具体资源消耗逻辑。

Clamp 约束（PreAttributeChange / PostGameplayEffectExecute）：

- Health → [0, MaxHealth]；Stamina → [0, MaxStamina]
- MaxHealth/MaxStamina → [1, 200]
- CriticalRate → [-100, 100]
- AttackPower/Defense → [0, ∞)
- StaggerMultiplier → [0, ∞)
- StaminaRegenRate/DeductionRate/ConsumptionRate → [0, ∞)
- MoveSpeedMultiplier → [0.1, 3.0]

#### MoveSpeedMultiplier → CMC 同步机制

`MoveSpeedMultiplier` 是 GAS Attribute，`CMC.MaxWalkSpeed` 是 CharacterMovementComponent 属性——两者不在同一系统。同步方式：**角色 Tick 中每帧读 Attribute 写回 CMC**。

```cpp
void AMHGZCharacter::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
    {
        const float Multiplier = ASC->GetNumericAttribute(
            UMHGZAttributeSet::GetMoveSpeedMultiplierAttribute());
        GetCharacterMovement()->MaxWalkSpeed = BaseMaxWalkSpeed * Multiplier;
    }
}
```

| 移速来源 | 实现 | 说明 |
|----------|------|------|
| 收刀态基础速度 | `BaseMaxWalkSpeed`（角色构造函数中从 CMC 初始值缓存） | 所有武器收刀时移速相同 |
| 武器持刀移速差异 | 装备武器时 Apply GE 修改 `MoveSpeedMultiplier`（太刀 0.85、大剑 0.6） | 与装备其他属性（AttackPower/Defense）走同一 GE 路径，卸下时随 `Effect.Source.Equipment` 批量清空 |
| Buff 移速变化 | 加速药水/减速 debuff 通过 GE 修改 `MoveSpeedMultiplier` | 标准 GAS 属性叠加 |
| 奔跑加速 | GA_Sprint 激活期间 Apply GE 提升 `MoveSpeedMultiplier` | 持续扣耐力，松开/耐力耗尽 EndAbility |
| 持刀不可奔跑 | GA_Sprint::CanActivateAbility 检查 `Combat.State.Unsheathed` Tag → 阻塞 | 收刀态（Sheathed）可奔跑，拔刀态（Unsheathed）不可 |
| 重型武器笨重感 | 武器 GE 同时降低 `MaxAcceleration`（CMC 属性，通过 GE 修改或直接设置） | 起步/转向更慢；AnimBP 按 WeaponTypeTag 切换 BlendSpace 资产实现不同移动动画 |

> **为何用 Tick 而非 PostGameplayEffectExecute：** Tick 读 Attribute 写 CMC 是一行代码，每帧开销 < 0.001ms。PostGameplayEffectExecute 回调在 GE Apply/Remove 时触发，但多个 GE 叠加修改 MoveSpeedMultiplier 时的中间状态变化可能漏掉。Tick 保证永远同步。

### 2.2 UMHGZEquipmentComponent — 装备 GE 管理组件

```
UCLASS(ClassGroup=(Equipment), BlueprintType)
class UMHGZEquipmentComponent : public UActorComponent
```

挂载到 Character。管理装备槽位、GE 的创建/Apply/移除。直接与 `UMHGZEquipmentInstance` 对接，不需要中间状态结构体。

| 成员 | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| EquippedItems | TMap\<FGameplayTag, TObjectPtr\<UMHGZEquipmentInstance\>\> | "Equipment\|State" | 空 | 已装备物品（Key=槽位Tag，Value=EquipmentInstance*。一个 TMap 存所有槽位） |

> 物品是否被装备/镶嵌的判定：直接读 `EquipmentInstance→Status`，O(1)。

方法：

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
- `bool CanSocketAccessory(UMHGZEquipmentInstance* HostItem, UMHGZEquipmentInstance* Accessory, FName SocketName) const`
  - 输入：宿主装备、饰品、孔位名。
  - 输出：是否可镶入。
  - 作用：转发 `HostItem→CanSocketAccessory`，比较饰品等级与孔位等级。
- `void OnEquipmentChanged()`
  - 作用：装备变更的统一入口。任何 Equip/Unequip/Socket 操作最终都调用此方法。
  - 设计思路：
    1. `ASC→RemoveActiveEffectsWithTags(FGameplayTagContainer(Effect.Source.Equipment))` — 一行清空全部装备 GE，属性回归基础值。
    2. 遍历 `EquippedItems` 中所有物品 → `ApplyItemEffects(Item)` — 重新 Apply 全部。
  - **性能说明（全量重算而非增量更新）：** 一次 `OnEquipmentChanged` 涉及 ~20-30 个 GE 的销毁与重建。装备变更仅在换装/镶嵌/拆除时触发——这些操作全部发生在非战斗期（工坊、准备阶段、菜单），频率极低（每秒 < 0.1 次）。全量重算的核心优势是**零中间状态**——不存在"忘记移除某个 GE"或"旧 GE 残留"的 bug，正确性由设计保证。20-30 个 GE 的 Remove+Apply 在 UE5 GAS 中耗时 < 0.5ms，对玩家完全无感知。
- `void ApplyItemEffects(UMHGZEquipmentInstance* Item)`
  - 输入：装备实例。
  - 作用：面向 `Item→Definition` + `Item→Customization` 创建 GE 并 Apply 到 ASC。
  - 设计思路：
    1. `ApplyIntrinsicGE(ASC, Item→Definition, Item→GetCustomization())` — 读取覆盖后的有效数值 Apply GE。
    2. 收集有效词条：`Def→Entries − Customization.RemovedEntryIDs + Customization.AddedEntries` → `ApplyEntryGEs(ASC, EffectiveEntries)`。
    3. **仅当 `Item→Definition` 为 `UMHGZWeaponDefinition` 时：** 通过 `UMHGZDataManager::FindWeaponResourceConfig(WeaponTypeTag)`（§6.3）查表 → 若匹配则 Apply 资源 GE。
    4. 遍历 `Item→SocketedAccessories` → `ApplyEntryGEs(ASC, AccDef)`。
    5. **仅当 `Item→Definition` 为 `UMHGZWeaponDefinition` 时：** 通过 `UMHGZDataManager::FindWeaponComboData(WeaponTypeTag)`（§6.3）查表 → 若匹配则授予连招协调器 Ability。
    6. 所有 GE 创建时统一添加 `GrantedTags: Effect.Source.Equipment`。
- `void ApplyIntrinsicGE(UAbilitySystemComponent* ASC, UMHGZEquipmentDefinition* Def, const FItemCustomization& Custom)`
  - 输入：ASC、装备定义、客制化覆写。
  - 作用：读取有效数值（`Custom.StatOverrides` 覆盖 `Def` 原值；Key 为 `FGameplayTag`，与 `Attribute.*` 体系匹配）→ 创建对应 GE → Apply 到 ASC。
- `void ApplyEntryGEs(UAbilitySystemComponent* ASC, UMHGZEquipmentDefinition* Def)`
  - 输入：ASC、装备定义。
  - 作用：遍历 `Def→Entries`，每个 `FEntryReference`：
    1. 调用 `UMHGZDataManager::FindEntryDefinition(EntryRef.EntryID)` 查 `DT_EntryCatalog` 获取 `FEntryDefinition`（§6.3）。
    2. 构造 GE Spec：`ASC→MakeOutgoingSpec(GE_EntryStat)` → `Spec→SetSetByCallerMagnitude("EntryID", EntryRef.EntryID)` → `Spec→SetSetByCallerMagnitude("EntryLevel", EntryRef.EntryLevel)`。
    3. SimpleStat 词条 Apply `GE_EntryStat`（`UExecCalc_EntryStat` 在 Execute 中从 Spec 读取 EntryID+EntryLevel 后查曲线计算数值）。
    4. Complex 词条实例化 `EffectClass` → Apply。
    5. 所有 GE 统一添加 `GrantedTags: Effect.Source.Equipment`。

Delegate：`FOnEquipmentChanged` — Equip/Unequip/Socket 后广播。

### 2.3 DT_WeaponResourceConfig — 武器种类资源映射

DataTable，RowStruct 如下：

| 列名 | 类型 | 说明 |
|------|------|------|
| WeaponTypeTag | FGameplayTag | 武器种类（主键） |
| ResourceMax | float | 资源上限 |
| ResourceRegen | float | 每秒回复量（0=不自动回复） |
| RegenCondition | FGameplayTagQuery | 回复条件（空=始终回复） |

运行时 `ApplyItemEffects` 中按 `Def→WeaponTypeTag` 查表，命中则创建资源 GE→Apply。

### 2.4 DT_WeaponComboConfig — 武器连招表映射

DataTable，按武器种类映射连招数据。

| 列名 | 类型 | 说明 |
|------|------|------|
| WeaponTypeTag | FGameplayTag | 武器种类（主键） |
| ComboData | TSoftObjectPtr\<UMHGZWeaponComboData\> | 连招表 DataAsset 引用（完整定义见 §3.6） |

> 运行时 `ApplyItemEffects` 中按 `Def→WeaponTypeTag` 查表，命中则通知 ASC 加载对应 `WeaponComboData` 并授予连招协调器（详见 §3.7）。§2 仅保留此桥接——连招数据的完整定义和协调器运行时逻辑归属动作系统（§3）。

### 2.5 GE_EntryStat — 通用词条 GameplayEffect

所有 SimpleStat 词条共用此 GE 蓝图，不预设属性修饰符。

| 设置项 | 值 | 说明 |
|--------|-----|------|
| Duration Policy | Infinite | 装备在即持续 |
| Stacking | AggregateBySource | 不同词条可叠加 |
| Modifiers | 空 | 修饰符由 ExecCalc 运行时注入 |
| Calculation Class | UExecCalc_EntryStat | 自定义执行计算 |
| GrantedTags | Effect.Source.Equipment | 用于批量移除 |

### 2.6 UExecCalc_EntryStat — 词条执行计算

```
UCLASS()
class UExecCalc_EntryStat : public UGameplayEffectExecutionCalculation
```

纯 C++ 类，每次 GE_EntryStat 被 Apply 时 GAS 自动调用。

`void Execute_Implementation(const FGameplayEffectCustomExecutionParameters& Params, FGameplayEffectCustomExecutionOutput& Out)`

- 输入：`ExecutionParams`（含 EffectSpec 中 SetByCaller 注入的 EntryID 和 EntryLevel）。
- 输出：`OutExecutionOutput`（写入各属性的修改量和操作类型）。
- 作用：从 Spec 读 EntryID → 通过 `UMHGZDataManager`（§6.3）查 DT_EntryCatalog 获取 `FEntryDefinition` → 遍历 Modifiers → 每条 `Curve.Eval(EntryLevel)`（CT_EntryMagnitudes 由 DataManager 提供）得数值 → 写入 `Out.Add(Attribute, Op, Value)`。

> **DataManager 访问方式：** ExecCalc 通过 `Params.GetSourceAbilitySystemComponent()->GetWorld()->GetGameInstance()->GetSubsystem<UMHGZDataManager>()` 获取 DataManager。所有全局 DataTable/CurveTable 引用集中在 DataManager，ExecCalc 不需要硬编码资产路径。


### 2.7 受击与霸体判定（玩家侧）

**原则：GE Spec 即信息载体——攻击方在 `MakeDamageSpec` 中已将伤害值（SetByCaller）、硬直等级（DynamicTag）、命中位置/冲击方向（GameplayEffectContext→HitResult）打包进 Spec。目标侧 ExecCalc 从 Spec 读取全部信息，不需要独立的"广播"通道。**

**硬直触发采用 GameplayEvent（非 Tag Trigger）：** 使用 `ASC→HandleGameplayEvent(Combat.Event.HitStagger, &EventData)` 触发 GA_HitReaction，而非依赖 `Combat.State.Hitstun` Tag 的 Add/Remove 检测。理由：Tag Trigger 仅在 Tag 从无到有时触发——若目标已处于 Hitstun（正在播放受击动画），第二次命中添加同一 Tag 不会重新触发 GA_HitReaction，导致连打吞受击动画。GameplayEvent 每次调用独立触发，无此问题。`Combat.State.Hitstun` 仍保留——用于 `CanActivateAbility` 阻塞输入（GA_HitReaction 的 Activate 时添加，EndAbility 时移除）。

**完整受击流程（三步，同步+异步）：**

```
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
步骤 1：攻击方 Apply GE Spec（攻击侧 §3.3b）
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
MakeDamageSpec → 构造 GE Spec：
  - SetByCaller: 伤害值, Hitzone.DefenseMultiplier
  - DynamicTag: HitStaggerTag (Combat.Stagger.Light/Medium/Heavy), HitzoneTag
  - GameplayEffectContext: HitResult（含命中点、攻击者位置、冲击方向）
→ SourceASC→ApplyGameplayEffectSpecToTarget(Spec, TargetASC)

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
步骤 2：目标 ExecCalc 执行（同步，纯数据）
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
UExecCalc_PlayerDamage::Execute：
  1. 读目标 Tag：Invincible → 伤害=0, return（GAS 层保底——主要拦截由碰撞层完成：DodgeWindow 已将玩家 Weapon 通道设为 Ignore，攻击 Sweep 物理上穿不过）；Dead → return
  2. 从 Spec 读伤害值 → 套用 Defense 属性 → 修改 Health
  3. 从 Spec 读 HitStaggerTag → 与目标霸体 Tag 比较等级：
     无霸体 + 任意 Stagger → 需硬直
     有 Poise.Light + Stagger=Light → 无硬直（霸体足够）
     有 Poise.Light + Stagger=Medium → 需硬直（霸体不足）
     ... 类推
  4. 若需硬直 → 构造 FGameplayEventData（含 StaggerLevel=HitStaggerTag, HitResult）→
     TargetASC→HandleGameplayEvent(Combat.Event.HitStagger, &EventData)
  （ExecCalc 不播放动画——它是 const 纯计算。动画由步骤 3 触发）

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

**霸体由谁提供：**
- **攻击自带霸体**：攻击 Ability 的 Montage 上挂 `AnimNotifyState_PoiseWindow`，NotifyBegin 时 `ASC→AddLooseGameplayTag(Combat.Poise.X)`，NotifyEnd 时移除。策划拖拽区间 = 霸体持续帧
- **装备/Buff 提供的被动霸体**：通过 GE 持续性持有 `Combat.Poise.*`（如铁壁技能珠），属于角色属性层而非动作层


---

## 三、动作系统

**设计原则：** GAS + EnhancedInput 驱动，通过 GameplayTag 桥接输入与 Ability，避免硬编码。核心能力（移动/闪避）始终可用，武器能力（连招/资源技能）由装备系统动态授予/移除。Ability 基类统一处理耐力消耗、冷却检查、输入绑定。**无独立跳跃键——角色通过奔跑/翻滚到达平台边缘时触发前跃（Edge Vault），由 CMC 边缘检测 + GAS 触发动画驱动（见下文边缘跳越说明）。**

**移动实现——UE5 原生 CharacterMovementComponent，GAS 只管权限：**
- 移动物理：`UCharacterMovementComponent`（MaxWalkSpeed、重力、爬坡等），**不用 GAS 实现**
- 移动输入：角色类 `AddMovementInput`（走 EnhancedInput → 非 GAS 路径，与攻击/闪避路径分开）
- 移动动画：AnimBP 的 BlendSpace1D 基于 `Speed = Velocity.Size()` 驱动 Walk/Jog/Sprint 融合，**UE5 原生支持，蓝图连线即用。CMC 的 MaxAcceleration/BrakingDeceleration 控制加速/减速手感——重型武器降低加速度即可实现"笨重感"，无需额外起步/停步动画。不同武器拔刀态走不同 BlendSpace（AnimBP 按 WeaponTypeTag 切换资产）；瞄准时上半身覆盖 AimOffset（Layered Blend Per Bone + AimYaw/AimPitch 驱动），下半身继续正常移动——都是标准 AnimBP 节点，不新建动画蓝图**
- 奔跑（GA_Sprint）：按下 LS/L3 → `TryActivateAbilityByTag(Input.Sprint)` → GE 提升 `MoveSpeedMultiplier` + 持续扣耐力；松开/耐力耗尽 → EndAbility 恢复原速。`CanActivateAbility` 检查 `Combat.State.Unsheathed` Tag → 持刀时阻塞，仅收刀态可奔跑
- 移动阻塞：`CanActivateAbility` / 角色 Tick 中检查 `Combat.State.Hitstun` / `Combat.State.Unsheathed` 等 Tag → 禁止输入时 `AddMovementInput` 传入零向量。**持刀（Unsheathed）时移动输入不被阻塞（可以走路），仅奔跑被阻塞**
- GAS 只管两件事：**能不能动**（Tag 阻塞，如持刀不可奔跑）、**有多快**（GE 修改 MoveSpeedMultiplier，CMC 每帧读取同步）

**边缘跳越（Vault）——替代独立跳跃键：**
- 怪猎式"跳跃"本质是**边缘触发的前跃动作**：角色奔跑/翻滚到平台边缘时，自动播放向前腾跃的动画，实现跨台阶下跳。不是"按跳键原地起跳"，是"水平位移到达边缘时触发的动画跳跃"。
- **CMC 能做什么：** CMC 的 `FindFloor` / `IsWalkingOffLedge` 能检测角色是否即将离开地面边缘——当胶囊体前方超出平台、下方无地面时返回 true。这是边缘检测的基础。
- **CMC 不能做什么：** CMC 本身不会在检测到边缘时自动播放前跃动画或施加额外速度——它只会让角色进入 `MOVE_Falling` 自然坠落。**主动前跃需要自定义逻辑。**
- **实现方案（待细化）：** 可选项包括——
  - **方案 A（组件轮询）**：`UMHGZEdgeVaultComponent` 在 Tick 中检查 CMC 的边缘状态 + 当前输入（Sprint/Dodge 激活中）→ 满足条件时触发 `GA_EdgeVault` → 播前跃 Montage + RootMotion 位移。
  - **方案 B（GAS 被动 Ability）**：`GA_Passive_EdgeVault` 为持续型被动 Ability，在 `CanActivate` / Tick 中读 CMC 边缘状态 + 角色 Tag（`Combat.State.Sprinting` / Dodge 激活中）→ 触发子 Ability 播动画。
  - 共同前提：需要一个可靠的**边缘检测函数**——在 CMC 的 `IsWalkingOffLedge()` 基础上加前向 Trace 验证前方是否有可落脚的平台（区分"跳下去"vs"纯坠落"），以及目标平台高度是否在可接受范围内。

  **方案对比与推荐：**

  | 维度 | 方案 A（组件轮询） | 方案 B（GAS 被动 Ability） |
  |------|:--:|:--:|
  | 实现复杂度 | ⭐ 低。标准 UActorComponent + Tick，~50 行 C++ | ⭐⭐ 中。需管理 Infinite Ability 生命周期、子 Ability 激活，~80-100 行 |
  | 每帧开销 | 极低。直接函数调用，非 Sprint/Dodge 时立即 return（1-2 次 Tag 检查 ≈ 0.01ms） | 略高。走 ASC→Tick→遍历 ActiveAbilities→虚函数调用路径（~0.02-0.05ms） |
  | 触发时开销 | 一次 `TryActivateAbilityByTag`，标准 GAS 激活 | 已在 GAS 上下文内，直接 `ActivateAbility`，略省一次 Tag 查找 |
  | 状态阻塞 | 需手动检查 ASC Tag（`HasMatchingGameplayTag`）排除 Hitstun 等状态 | GAS 原生 Tag 阻塞——`ActivationBlockedTags` 自动拦截 |
  | 调试便利性 | ⭐⭐⭐ 组件独立，断点直观，可单独禁用测试 | ⭐⭐ 嵌在 GAS 框架内，需理解 Ability 激活链 |
  | 与现有架构一致性 | 新增组件类型 | 与连招协调器（Infinite Ability）模式一致 |

  **推荐方案 A。** 边缘跳越的条件判断（Sprint？Dodge？At edge？前方有平台？）是简单的布尔组合，不需要 GAS 的 Tag 状态机来管理。组件内一次 Tick 完成全部判断 + 触发，比 GAS Ability 的激活链路更短、更直观。唯一需要 GAS 的地方是触发 `GA_EdgeVault` 播动画——那就是一个 `TryActivateAbilityByTag` 调用。

- **翻滚边缘跳**：`GA_Dodge` 的 Montage 若将角色带出边缘，Dodge 中途接入边缘跳越 Montage（Blend Out → Vault Montage → 着地恢复）。
- **总结：无独立跳跃键。边缘跳越 = CMC 边缘检测 + GAS 触发 + 动画驱动的水平→抛物线位移。** 策划只需在关卡中设计有高低差的平台，角色在奔跑/翻滚到达边缘时自动触发前跃。
- **详细组件定义见 [§3.3d](#33d-umhgzedgevaultcomponent--边缘跳越组件)。**

**攀爬（待后续设计）：**
- CMC 支持 `MOVE_Custom` 自定义移动模式，可实现攀爬物理（沿墙面移动、重力方向改变、体力消耗）。
- 怪猎风格攀爬为**场景触发式**（特定藤蔓/岩壁标记 `Climbable` Tag），非自由攀爬——实现难度低于 BOTW 式全地形攀爬。
- 核心组件：墙面检测（射线/碰撞）+ `PhysCustom()` 覆写攀爬重力 + 攀爬动画 + 耐力消耗 + 翻越（Mantle）检测。
- 当前版本不实现攀爬系统，CMC 预留 `MOVE_Custom` 接口。

**输入流：**
```
EnhancedInput (按键/摇杆)
  → InputAction (IA_Y, IA_B, IA_Dodge, IA_Sprint...)
    → FAbilityInputBinding 查表 → GameplayTag (Input.Weapon.Y / Input.Weapon.B...)
      → ASC→OnInputActionTriggered(Tag)
        → 武器Tag? → Coordinator→HandleWeaponInput
        → 非武器Tag? → TryActivateAbilityByTag
```

### 3.1 FAbilityInputBinding — 输入-技能绑定

```
USTRUCT(BlueprintType)
struct FAbilityInputBinding
```

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| InputAction | TObjectPtr\<UInputAction\> | nullptr | EnhancedInput 的 InputAction 资产 |
| AbilityTag | FGameplayTag | 空 | 触发时激活的 Ability Tag |
| bConsumeInput | bool | true | 触发后是否消耗此次输入（防止一个按键触发多个 Ability） |

### 3.2 UMHGZAbilitySystemComponent — 扩展 ASC

```
UCLASS()
class UMHGZAbilitySystemComponent : public UAbilitySystemComponent
```

扩展 UE 原生 ASC，增加输入绑定和批量授予能力。

| 成员（追加） | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| InputBindings | TArray\<FAbilityInputBinding\> | "Input" | 空 | 输入绑定列表（策划在蓝图中配置） |
| CoreAbilities | TArray\<TSubclassOf\<UGameplayAbility\>\> | "Ability\|Core" | 空 | 核心能力列表（BeginPlay 时自动授予） |
| CoreAttributeEffects | TArray\<TSubclassOf\<UGameplayEffect\>\> | "Ability\|Core" | 空 | 核心 GE 列表（BeginPlay 时自动 Apply） |

方法：

- `void InitializeAbilitySystem()`
  - 作用：BeginPlay 时调用。授予 CoreAbilities → Apply CoreAttributeEffects → 遍历 `InputBindings` 用 lambda 绑定 EnhancedInput。
  - 设计思路：绑定时不传 `UInputAction*` 到回调——而是用 **lambda 捕获 `FGameplayTag`**。每个 IA 的回调直接拿到对应的 `AbilityTag`，无需在回调内再次查 `InputBindings` 数组：
    ```cpp
    for (auto& Binding : InputBindings)
    {
        FGameplayTag Tag = Binding.AbilityTag;
        // Triggered: 点按 → 连招匹配/普通 Ability 激活
        EnhancedInput->BindAction(Binding.InputAction, ETriggerEvent::Triggered,
            [this, Tag](const FInputActionValue&) { OnInputActionTriggered(Tag); });
        // Completed: 松开 → 蓄力 GA 内部接收释放通知（若当前 Active 的 GA 是蓄力型）
        EnhancedInput->BindAction(Binding.InputAction, ETriggerEvent::Completed,
            [this, Tag](const FInputActionValue&) { OnInputActionCompleted(Tag); });
    }
    ```
    `OnInputActionTriggered` 按 Tag 类别分叉——武器 Tag → `Coordinator→HandleWeaponInput(Tag)`；非武器 Tag → `TryActivateAbilityByTag(Tag)`。
    `OnInputActionCompleted` 仅分发给当前 Active 的 Abilities（通过 `NotifyAbilitiesOfInputCompleted` 或逐个调用），供蓄力类 GA 接收释放信号。
- `void OnInputActionTriggered(FGameplayTag AbilityTag)`
  - 输入：直接传入 `AbilityTag`（已在绑定时由 lambda 捕获，无需查表）。
  - 作用：
    - 若 `AbilityTag.MatchesTag("Input.Weapon")` → 查找当前 Active 的 `GA_WeaponComboCoordinator` → `Coordinator→HandleWeaponInput(AbilityTag)`
    - 否则 → `TryActivateAbilityByTag(AbilityTag)`  // Dodge 也走此路径——GA_Dodge 的 CanActivateAbility 通过 ASC Tag（Attacking/DodgeAcceptOpen）自行判断
  - 设计思路：回调签名只需一个 `FGameplayTag` 参数——Tag 已在绑定时捕获，派发函数内 O(1) 做 `MatchesTag` 父标签判别，无任何查表遍历。Dodge 与普通 Ability 走同一路径，可用性由 GAS 原生 Tag 检查决定。
- `void OnInputActionCompleted(FGameplayTag AbilityTag)`
  - 输入：直接传入 `AbilityTag`。
  - 作用：通知当前持有该 `InputTag` 的 Active Ability 输入已释放（Completed 事件）。主要用于蓄力类 GA——蓄力 GA 在 Activate 时缓存自己的 `InputTag`，在内部处理 Completed 回调以释放蓄力等级对应的攻击。
- `void GrantWeaponAbilities(TArray<TSubclassOf<UGameplayAbility>> Abilities)`
  - 输入：武器授予的能力类列表。
  - 作用：授予并存储 Handle → `EquipmentComponent→OnEquipmentChanged` 时调用。
- `void RemoveWeaponAbilities()`
  - 作用：移除所有武器授予的能力（切换武器时调用）。
- `void BindInputAction(UInputAction* Action, FGameplayTag AbilityTag)`
  - 输入：InputAction 资产、Ability Tag。
  - 作用：运行时动态绑定/替换单个 IA→Tag 映射。常见场景：进入载具后换一套按键映射、特殊状态（攀爬/游泳）覆盖默认绑定。
  - 注意：**限制攻击/不可操作**场景不通过解绑实现——GAS 的 `CanActivateAbility` 在 Ability 端通过 GameplayTag 阻塞（如 `Combat.State.Stunned`）拦截激活，输入绑定保持不变。避免"解绑-恢复"的时序风险。

### 3.3 UMHGZGameplayAbility — Ability 基类

```
UCLASS(BlueprintType, Blueprintable, Abstract)
class UMHGZGameplayAbility : public UGameplayAbility
```

所有 Ability 的基类，统一处理耐力消耗、冷却、输入标签。

| 成员 | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| InputTag | FGameplayTag | "Ability\|Input" | 空 | 绑定的输入标签（`Input.Weapon.Y`/`Input.Weapon.B`/`Input.Dodge`…） |
| StaminaCost | FScalableFloat | "Ability\|Cost" | 0 | 单次耐力扣除量（闪避/单次攻击）。`ActivateAbility` 时一次扣除：`Cost × StaminaDeductionRate` |
| StaminaCostRate | FScalableFloat | "Ability\|Cost" | 0 | 持续耐力消耗速率（每秒）。用于奔跑/蓄力/瞄准。仅 `bIsContinuous==true` 的 Ability 使用：每 Tick 扣除 `Rate × StaminaConsumptionRate × Δt` |
| bIsContinuous | bool | "Ability\|Cost" | false | 是否持续型 Ability（true=GA_Sprint/GA_Aim，false=单次型如 GA_Dodge） |
| CooldownDuration | FScalableFloat | "Ability\|Cooldown" | 0 | 冷却时长 |
| CooldownTag | FGameplayTag | "Ability\|Cooldown" | 空 | 冷却标签（用于 UI 显示冷却） |
| bRequiresWeaponResource | bool | "Ability\|Cost" | false | 是否需要武器专属资源 |
| WeaponResourceCost | FScalableFloat | "Ability\|Cost" | 0 | 消耗的资源量 |

> **StaminaCost 与 FComboNode::StaminaRequired 的职责区分：** `StaminaCost` 是 GA 的**实际耐力扣除量**（`ActivateAbility` 中执行 `Cost × StaminaDeductionRate`）。`FComboNode::StaminaRequired`（§3.6）是连招协调器的**匹配门槛**——检查当前耐力是否够放这招，不负责扣耐。两者独立配置，通常数值相同但语义不同（策划可能设 Required > Cost 保留耐力余量）。

> **FScalableFloat 与全局 CurveTable：** 所有 `FScalableFloat` 字段（StaminaCost、StaminaCostRate、CooldownDuration、WeaponResourceCost 等）统一关联全局 CurveTable `DT_AbilityScalars`。`FScalableFloat` 在 UE 中需要一个 CurveTable 才能求值——全局表意味着所有 Ability 只需指定行名（RowName），无需各自配置 CurveTable 引用。主要用途是支持未来的等级缩放（如技能升级后冷却缩短）；若当前无缩放需求，曲线设为平直常量（所有等级返回相同值）即可。

方法（覆写）：

- `bool CanActivateAbility(const FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo*, FGameplayTagContainer*, FGameplayTagContainer*) const override`
  - 输出：是否可激活。
  - 作用：检查耐力是否够 → 检查武器资源是否够 → 检查冷却是否结束 → 任一不满足返回 false（GAS 自动处理 UI 提示）。
- `void ActivateAbility(const FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo*, const FGameplayAbilityActivationInfo, const FGameplayEventData*) override`
  - 作用：
    - **单次型（bIsContinuous=false）**：基类在 Activate 时扣除耐力 `StaminaCost × StaminaDeductionRate`、扣除武器资源、启动冷却。非攻击类 Ability（闪避/喝药）子类覆写实现具体逻辑；攻击类 Ability 由 `UMHGZAttackAbility`（3.3b）接管
    - **持续型（bIsContinuous=true）**：基类不一次性扣耐力——改为在 `OnTick` 中每帧扣除。**用 Tick × DeltaTime 保证帧率无关**，不同帧率下 1 秒总扣除量一致（30/60/120 FPS 均扣除 `CostRate × ConsumptionRate`）。API：`ASC→ApplyModToAttribute(StaminaAttribute, Add, -CostRate × ConsumptionRate × DeltaTime)`——走 ApplyMod 触发 AttributeSet 的 `PreAttributeChange` Clamp，防止扣到负数。耐力归零后 `EndAbility`
- `void EndAbility(const FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo*, const FGameplayAbilityActivationInfo, bool, bool) override`
  - 作用：清理动画状态、结束冷却计时器。


### 3.3b UMHGZAttackAbility — 攻击 Ability 中间层

```
UCLASS(BlueprintType, Blueprintable, Abstract)
class UMHGZAttackAbility : public UMHGZGameplayAbility
```

继承 `UMHGZGameplayAbility`，统一封装所有攻击类 Ability 的**碰撞检测**、**命中过滤**、**伤害 GE 构造与 Apply**。蓝图子类只需配置参数，不写逻辑代码。

**FAttackCollisionConfig — 单段碰撞配置：**

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| AttachSocketName | FName | 必填 | 碰撞体挂载的骨骼 Socket（如 "weapon_tip"、"hand_r"） |
| Shape | ECollisionShape | Sphere | 碰撞形状（Sphere / Capsule / Box） |
| ShapeExtent | FVector | (20,20,20) | 形状参数：Sphere→X=Radius；Capsule→X=Radius+Z=HalfHeight；Box→HalfExtent |
| CollisionChannel | TEnumAsByte\<ECollisionChannel\> | GameTraceChannel1 | 碰撞通道（默认 Weapon 通道） |
| HitzoneQueryTag | FGameplayTag | 空 | 限定碰撞仅检测带此 Tag 的组件。空=不限制（检测所有碰撞） |

**FAttackDamageConfig — 单段伤害配置：**

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| DamageEffectClass | TSubclassOf\<UGameplayEffect\> | nullptr | 伤害 GE 蓝图（策划在编辑器中配置） |
| MotionValue | FScalableFloat | 1.0 | ★ 动作值（倍率），参与伤害计算：`Damage = AttackPower × MotionValue × HitzoneDefense`。轻击 0.8 / 重击 1.5 / 终结技 3.0。原 `AttackPowerMultiplier` 重命名为 `MotionValue`——更准确的动作游戏术语 |
| BaseStaggerValue | float | 0 | ★ 基础破坏值。参与硬直计算：`Stagger = BaseStaggerValue × StaggerMultiplier(Attribute) × MonsterHitzoneStaggerRate`。为 0 则该段不造成硬直 |
| KnockbackAngle | float | 0 | 击退方向（相对攻击者朝向，0=前方，180=击飞） |
| KnockbackForce | float | 0 | 击退力度 |
| HitStaggerTag | FGameplayTag | 空 | 硬直等级（`Combat.Stagger.Light` / `Medium` / `Heavy`） |
| DamageSetByCallerTag | FGameplayTag | 空 | SetByCaller 伤害值 Tag（GE 中用此 Tag 读取动态伤害值） |
| bUseHitzoneDefense | bool | true | 是否按命中部位的 `DefenseMultiplier` 修正伤害。怪物侧每个 hitzone 碰撞体持有 `DefenseMultiplier`（肉质）和 `StaggerRate`（硬直肉质） |
| bRequiresHitToContinue | bool | false | 招式内空挥截断：为 true 时，本段碰撞窗口结束后检查 `HitTargets`——若该段空挥，提前 `EndAbility` |
| OnHitSelfEffect | TSubclassOf\<UGameplayEffect\> | nullptr | 命中时对自身施加的 GE（如"命中后攻击力+10"）。仅首次命中时 Apply 一次 |

**FAttackSegmentConfig — 单段攻击配置（碰撞 + 伤害 + 多跳）：**

```
USTRUCT(BlueprintType)
struct FAttackSegmentConfig
```

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| Collision | FAttackCollisionConfig | — | 本段碰撞参数（形状/通道/过滤） |
| Damage | FAttackDamageConfig | — | 本段伤害参数（动作值/破坏值/击退） |
| MultiHitCount | int32 | 1 | ★ 单次碰撞产生的伤害跳数。默认 1=命中即造成 1 次伤害。>1=命中后每隔 `MultiHitInterval` 秒造成一次伤害，共 `MultiHitCount` 次（如登龙剑下批：1 次碰撞判定 × 7 跳伤害） |
| MultiHitInterval | float | 0.1 | 多次伤害之间的间隔（秒）。仅 `MultiHitCount>1` 时有效 |
| MaxWarpAngle | float | 30.0 | ★ 本段 MotionWarping 允许的最大旋转修正角度（度）。与 GA 的 `MaxCorrectionAngle` 区分：GA 的管控"激活时第一段扭头"，段的 `MaxWarpAngle` 管控"段内 Montage 播放期间 MotionWarping 的旋转上限"。多段招式每段可不同（见切段0=180°后撤、段1=120°回砍）。0=该段不做旋转 Warp |

**成员：**

| 成员 | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| AttackSegments | TArray\<FAttackSegmentConfig\> | "Attack" | 空 | ★ 多段攻击配置——每段独立配置碰撞 + 伤害 + 多跳。替代原来分离的 `CollisionConfigs` + `DamageConfig`，解决非数组成员（Damage/MotionValue/Stagger 等）无法随段变化的问题。横扫+下劈两段的 MotionValue 和 BaseStaggerValue 通常不同 |
| MaxCorrectionAngle | float | "Attack\|Correction" | 30.0 | ★ 攻击激活瞬间（第一段）的最大方向修正角度（度）。读摇杆方向，若偏离 ≤ 此值则设 MotionWarping RotationTarget 扭向目标。段内 MotionWarping 的修正上限由 `FAttackSegmentConfig::MaxWarpAngle` 控制，两者独立。0=禁止修正 |
| HitTargets | TMap\<AActor*, FName\> | — | 空 | 已命中的怪物→首个接触的 hitzone 骨骼名（Key=Actor, Value=BoneName）。**每段 `EnableCollision` 清空，各段独立记录——段0命中头部、段1命中尾部互不干扰。** 同一段内同怪物只记录首个接触部位 |
| CurrentSegmentIndex | int32 | — | 0 | 当前正在执行的段索引（运行时状态） |
| bHasHitThisActivation | bool | — | false | 本次 GA 激活后是否已有命中。用于首次命中时触发一次性逻辑（通知协调器 + Apply OnHitSelfEffect），避免多段/多怪重复触发 |

**方法（覆写/新增）：**

- `void ActivateAbility(...) override`
  - 作用：基类扣耐力/资源 → `ASC→AddLooseGameplayTag(Combat.State.Attacking)` → **方向修正（读摇杆 → 若偏离 ≤ `MaxCorrectionAngle` 则设 MotionWarping RotationTarget → 播放 Montage）** → 等待 `AnimNotifyState_AttackCollision` 控制碰撞窗口开关。蓝图子类通常**不需要覆写**此方法。
  - 方向修正流程：`GetLastMovementInputVector` → 若长度 ≥ 0.1 → 计算与角色朝向的夹角 → 若夹角 ≤ `MaxCorrectionAngle` → `MotionWarpingComponent→AddOrUpdateWarpTarget(FRotationTarget(...))` → 播放 Montage（Montage 中挂 `AnimNotifyState_MotionWarping` 自动插值旋转）。无输入或超出角度 → 不设 Target → Montage 用角色当前朝向。**修正发生在 Montage 播放前，ComboWindow 在 Montage 内若干帧后才打开——修正不破坏连招窗口。**
- `void EndAbility(...) override`
  - 作用：`ASC→RemoveLooseGameplayTag(Combat.State.Attacking)` → 通过 `ASC→GetActiveAbilities()` 查找 `GA_WeaponComboCoordinator` → 调用 `Coordinator→OnAttackFinished()`（★ 主要回 Idle 路径——Montage 自然播完即触发，若期间无新 GA 激活则 `CurrentState="Idle"`）→ 清理动画状态、结束冷却、清除 `MultiHitTimer` 若存在。

- `void EnableCollision(int32 SegmentIndex = 0)`
  - 输入：段索引。
  - 作用：`CurrentSegmentIndex = SegmentIndex` → 在 `AttackSegments[SegmentIndex].Collision.AttachSocketName` 处按配置形状创建碰撞体 → 清空 `HitTargets`。
  - 首帧判定：创建碰撞体后下一 Tick 执行一次 `SweepMultiByChannel`——从武器上一帧位置扫到当前帧位置，按 `FHitResult.Time` 升序取首个带 `HitzoneQueryTag` 的命中（若配置了该 Tag），记录到 `HitTargets` 后调用 `ApplyDamage(HitActor, BoneName, SegmentIndex)`。若 Sweep 无命中则注册 `OnComponentBeginOverlap` 持续检测后续新进入的怪物。
  - **多跳伤害（MultiHitCount>1）：** 首帧 Sweep 命中后启动 `MultiHitTimer`，每隔 `MultiHitInterval` 秒对 `HitTargets` 中所有怪物调用 `ApplyDamage(HitActor, BoneName, SegmentIndex)`，共 `MultiHitCount` 次。`DisableCollision` 或 GA 结束 → 清除 Timer。
  - 调用方：`UAnimNotifyState_AttackCollision→NotifyBegin`。

- `void DisableCollision()`
  - 作用：清除 `MultiHitTimer`（若存在）→ 销毁碰撞体 → 停止检测。若 `AttackSegments[CurrentSegmentIndex].Damage.bRequiresHitToContinue && HitTargets.IsEmpty()` → 调用 `ShouldContinueAfterHit()` → 若返回 false → `EndAbility` 提前。
  - 调用方：`UAnimNotifyState_AttackCollision→NotifyEnd`。

- `void OnAttackOverlap(AActor* HitActor, FName HitzoneBoneName)`
  - 输入：被命中的 Actor、接触的 hitzone 骨骼名。
  - 作用：过滤链 — 自身 → 队友 → 已在 `HitTargets` 中（同怪物已命中）→ 无敌 → 已死亡 → 命中组件不含 `HitzoneQueryTag`（若配置）→ 任一命中则 return。通过后 `HitTargets.Add(HitActor, HitzoneBoneName)` → 调用 `ApplyDamage(HitActor, HitzoneBoneName)`。
  - 设计思路：每怪物只记录首次接触的 hitzone。若首帧 Sweep 已命中怪物，后续 Overlap 事件中同怪物直接跳过。多怪物场景下各自独立记录。

- `void ApplyDamage(AActor* Target, FName HitzoneBoneName, int32 SegmentIndex)`
  - 输入：目标 Actor、命中部位骨骼名、段索引。
  - 作用：
    1. 调用 `MakeDamageSpec(Target, HitzoneBoneName, SegmentIndex)` 构造 GE Spec → `SourceASC→ApplyGameplayEffectSpecToTarget(Spec, TargetASC)`。
    2. **首次命中时（`bHasHitThisActivation==false`）：** 设 `bHasHitThisActivation=true` → 通知协调器 `GA_WeaponComboCoordinator→OnAttackHit()`（触发 `PendingGrantedTags` 授予）→ 若段 `Damage.OnHitSelfEffect` 非空则 Apply 到自身 ASC。
    3. 多段碰撞/多怪物场景下，后续命中跳过步骤 2。**多跳伤害（MultiHitCount>1）每次 Tick 都执行步骤 1（Apply 伤害 GE），但不重复触发首次命中逻辑。**
  - 设计思路：不直接 `ApplyGameplayEffectToTarget`，而用 Spec 方式——可在 Apply 前动态注入 SetByCaller 数值（伤害量）和 Dynamic Tag（硬直等级）。

- `FGameplayEffectSpecHandle MakeDamageSpec(AActor* Target, FName HitzoneBoneName, int32 SegmentIndex)`
  - 输入：目标 Actor、命中部位骨骼名、段索引。
  - 输出：构造好的 GE Spec。
  - 作用：
    1. `ASC→MakeOutgoingSpec(AttackSegments[SegmentIndex].Damage.DamageEffectClass)`。
    2. **伤害计算：** `Damage = AttackPower(ASC Attribute) × Seg.Damage.MotionValue × HitzoneDefenseMultiplier` → `Spec→SetSetByCallerMagnitude(DamageSetByCallerTag, Damage)`。
    3. **硬直值计算（若 Seg.Damage.BaseStaggerValue > 0）：** `Stagger = Seg.Damage.BaseStaggerValue × StaggerMultiplier(ASC Attribute) × HitzoneStaggerRate` → 写入 Spec 供目标 ExecCalc 处理。
    4. `Spec→AddDynamicAssetTag(Seg.Damage.HitStaggerTag)`。
    5. 若 `bUseHitzoneDefense`：`Spec→SetSetByCallerMagnitude("Hitzone.DefenseMultiplier", MonsterHitzoneComp→DefenseMultiplier)` + `Spec→SetSetByCallerMagnitude("Hitzone.StaggerRate", MonsterHitzoneComp→StaggerRate)`。
    6. `Spec→AddDynamicAssetTag(HitzoneTag)` — 命中部位标签（如 `Hitzone.Head`）写入 Spec。
    7. `Spec→GetContext()→AddHitResult(Hit)` — 碰撞检测的 `FHitResult` 写入 GameplayEffectContext。
  - 设计思路：伤害构造逻辑独立，子类可覆写实现暴击翻倍、属性克制修正、背刺加成等。部位信息通过 SetByCaller 和 DynamicTag 两条通道传递给怪物侧。**GE Spec 是攻击→受击的唯一信息载体——不需要独立的"广播"通道。**

**AnimNotifyState 类：**

| 类 | 作用 |
|----|------|
| `UAnimNotifyState_AttackCollision` | `NotifyBegin` → `EnableCollision(ConfigIndex)`；`NotifyEnd` → `DisableCollision()`。通过 `MeshComp→GetOwner()→GetAbilitySystemComponent()` 获取当前 Active Ability → Cast 到 `UMHGZAttackAbility` 调用对应方法。判定窗口可短至 1-2 帧；多段攻击（如双刀乱舞）在 Montage 中放多个独立 NotifyState 各自负责一段判定 |
| `UAnimNotifyState_MonsterAttackCollision` | 怪物攻击碰撞通知（见下方"怪物攻击碰撞"小节）。`NotifyBegin` → 遍历指定部位 Tag → MonsterAttack 通道 = Block；`NotifyTick` → SweepMultiByChannel 检测玩家；`NotifyEnd` → 恢复 MonsterAttack 通道 = Ignore |

> **MotionWarping（UE5 内置）：** 方向修正使用 UE5 内置的 `UAnimNotifyState_MotionWarping`（非自定义类）。GA ActivateAbility 中设 `FMotionWarpingTarget(RotationTarget)`，Montage 中挂多个 MotionWarping NotifyState 各自负责一段旋转 Warp。多段招式（见切）在两段之间更新 Warp Target 即可实现不同段不同方向修正。`RotationTarget` 自带角度限制参数，GA 设 Target 时指定该段允许的最大角度。


#### 3.3b-monster 怪物攻击碰撞——部位胶囊体复用 + 通道切换

怪物与玩家不同：**不需要临时创建碰撞体**。怪物身体各部位（头/尾/爪/翼）的骨骼上始终挂着形状匹配的胶囊体，动画驱动骨骼移动时胶囊体自然跟随。

**部位胶囊体通道配置：**

```
每个部位 (Head/Tail/LeftClaw/RightWing...) 挂 UPrimitiveComponent：
┌──────────────────────────┬──────────────┬──────────────┐
│  通道                     │  常态(Hitzone)│  攻击窗口     │
├──────────────────────────┼──────────────┼──────────────┤
│  Weapon (玩家攻击检测)     │  Block ← 始终 │  Block       │
│  MonsterAttack (怪物攻击)  │  Ignore      │  Block ★     │
│  Pawn (物理阻挡)          │  Block ← 始终 │  Block       │
│  Visibility (射线/UI)      │  Ignore      │  Ignore      │
└──────────────────────────┴──────────────┴──────────────┘
★ 唯一在 NotifyBegin/NotifyEnd 之间切换的通道
```

- **Weapon 始终 Block**：玩家 `SweepMultiByChannel(Weapon)` 任何时候都能检测到部位——伤害计算不受影响
- **MonsterAttack 窗口内 Block**：怪物攻击 Sweep 仅在攻击帧能命中玩家。收招动作（如尾巴扫完后缓慢归位）自动 Ignore——不会误伤
- **Pawn 始终 Block**：物理推挤不变，玩家和怪物不会重叠

**UAnimNotifyState_MonsterAttackCollision：**

```
NotifyBegin：
  → 遍历本怪物所有参与此攻击的部位 Tag（策划在 NotifyState 上配置 TArray<FGameplayTag>）
  → 每个部位设 MonsterAttack 通道 = Block
  → 可选：根据攻击类型临时叠加一个更大的胶囊体（如龙车全身判定）

NotifyTick：
  → SweepMultiByChannel(MonsterAttack) 从各部位上一帧位置扫到当前帧
  → 命中玩家 → 构造怪物伤害 GE Spec → Apply 到玩家 ASC

NotifyEnd：
  → 恢复所有部位 MonsterAttack 通道 = Ignore
  → 移除临时叠加的碰撞体（若有）
```

> **与玩家 AttackCollision 的对称性：** 策划在怪物攻击 Montage 上拖拽此 NotifyState 区间即可，和玩家攻击配置方式完全一致——都是 AnimNotifyState 驱动 + 通道切换 + Sweep 判定。

**边界情况：**

| 边界 | 处理 |
|------|------|
| 高速攻击穿透（如龙车） | 首帧 Sweep 从上一帧位置扫到当前帧位置，`FHitResult.Time` 升序取首个命中——高速也不会穿透 |
| 攻击范围大于部位胶囊体 | 部分招式（如翻滚碾压）临时叠加一个更大的 Box/Sphere 碰撞体，NotifyEnd 时移除 |
| 多部位同时攻击 | NotifyState 的 `AttackPartTags` 数组可配多个部位 Tag——如龙扫尾同时涉及 Tail1 + Tail2 + TailTip |

**额外方法（资源门控 + 条件衔接）：**

- `bool ShouldContinueAfterHit() const` (BlueprintNativeEvent)
  - 输出：当前碰撞窗口命中后，是否继续下一段碰撞窗口。
  - 默认实现：若 `AttackSegments[CurrentSegmentIndex].Damage.bRequiresHitToContinue && HitTargets.IsEmpty()` → return false；否则 return true。
  - **蓝图覆写场景——登龙剑：** 覆写此函数 → 读 ASC 的武器资源（气刃槽色阶）→ 若色阶 < 白 → return false → `EndAbility` 提前，第二段不播放。
  - 调用时机：`DisableCollision` 内，下一段 `EnableCollision` 之前。

- `bool CheckWeaponResourceForAbility() const` (BlueprintNativeEvent)
  - 输出：当前武器资源是否满足此 Ability 的消耗要求。
  - 默认返回 true。**各武器 GA 子类自行覆写**——查询各自武器特有的资源系统（气刃槽/瓶计数/蓄力等级等）。具体实现留空，仅保留接口。

**Ability 继承层级总览：**

```
UGameplayAbility                              ← UE 原生
  └── UMHGZGameplayAbility                    ← 耐力/冷却/资源（3.3）
        ├── UMHGZAttackAbility                ← ★ 碰撞+伤害+部位判定（3.3b）
        │     ├── GA_Sword_Slash_01           ← 蓝图：配碰撞+伤害参数+Montage
        │     ├── GA_Sword_Slash_02
        │     └── GA_GreatSword_Slash_01
        ├── UMHGZDodgeAbility                 ← ★ 翻滚/闪避（3.3c）— 不进连招表
        │     └── GA_Dodge                    ← 蓝图：配 DT_WeaponDodgeConfig
        ├── UMHGZEdgeVaultAbility             ← ★ 边缘跳越（3.3d）— 由组件自动触发
        │     └── GA_EdgeVault                ← 蓝图：配 Sprint/Dodge Vault Montage
        ├── GA_Sprint                         ← 蓝图：GE 改 MoveSpeedMultiplier + 持续扣耐力（CanActivateAbility 检查 Unsheathed Tag 阻塞）
        └── GA_Heal                           ← 蓝图：覆写 ActivateAbility
```

#### 3.3b-weapon 武器 Ability 基类分化——按武器种类

怪猎武器资源系统极其多样，无法用统一基类概括。每种武器从 `UMHGZAttackAbility` 派生一个**武器专属中间类**，持有该武器的资源组件引用并覆写关键钩子：

```
UMHGZAttackAbility                         ← 碰撞+伤害+方向修正（通用）
  ├── UMHGZLongSwordAbility                ← ★ 太刀: 气刃槽Level+Amount+衰减
  │     ├── GA_LS_Slash_01                 ← 蓝图: 配AttackSegments
  │     ├── GA_LS_ForesightSlash           ← 见切(两段MotionWarping+判定)
  │     └── GA_LS_HelmBreaker              ← 登龙(三段: 突刺+起跳+下劈, 多跳)
  │
  ├── UMHGZInsectGlaiveAbility             ← ★ 虫棍: 三灯+猎虫耐力
  ├── UMHGZChargeBladeAbility              ← ★ 盾斧: 瓶计数+盾充能
  └── UMHGZSwitchAxeAbility                ← ★ 斩斧: 充能槽
```

每个武器基类（~50 行 C++）持有：
- `UMHGZWeaponResourceComponent*` 子类引用（装备切换时注入）
- 覆写 `CheckWeaponResourceForAbility()` → 资源门控（气刃槽≥白？瓶计数≥N？）
- 覆写 `ShouldContinueAfterHit()` → 招内击中派生（登龙突刺命中+资源够→播下劈；否则→特殊后摇）
- 便捷方法：`GetSpiritLevel()`, `ConsumeSpiritGauge(float)`, `GetActiveBuffs()` 等

> **武器基类 vs FComboNode 的职责边界：** 武器基类管 GA **激活后**的内部逻辑（资源是否够？招内能否继续？），FComboNode 管**激活前**的匹配条件（按键+状态Tag是否满足？）。`CheckWeaponResourceForAbility` 在 `CanActivateAbility` 中被调用——连招表匹配成功后、GA 真正激活前做最后一轮资源检查。`FComboNode` 不该知道"气刃槽"是什么。

> **见切为何必须继承 UMHGZAttackAbility：** 见切是连招表中的一行——协调器通过 `ActivateAbility(Entry.AbilityClass)` 激活它，`Entry.AbilityClass` 必须是 `UMHGZAttackAbility` 子类（FComboNode 的类型约束）。见切有两段：段0 后撤闪避 + 段1 回砍伤害。段1 走标准 `AttackCollision → OnAttackOverlap → ApplyDamage` 管线——这正是 `UMHGZAttackAbility` 的核心价值，不能为了段0 的特殊性而放弃段1 的复用。
>
> **段0（后撤闪避）如何在 AttackAbility 框架中实现：** `AttackSegments[0]` 的 `Damage` 字段全部为 0（`MotionValue=0, BaseStaggerValue=0`，不产生伤害和硬直），碰撞形状可以设为一个大的 Sphere 覆盖回避路径（或直接设为极小/不创建——因为段0 判定不走 `EnableCollision` 的 Sweep 管线）。真正的闪避判定由两个并行的 AnimNotifyState 在 Montage 段0 区间内完成：
> - `AnimNotifyState_DodgeWindow`：设 `Invincible` Tag + Weapon 通道 Ignore（标准无敌帧）
> - `AnimNotifyState_ForesightJudge`：窗口内注册 `HandleGameplayEvent(HitStagger)` 回调 → 若怪物攻击打中玩家（伤害被 Invincible 截为 0 但事件仍触发）→ GA 记录 `bDodgeSuccessful=true`
>
> `AttackSegments[0]` 的存在只是为了满足 `UMHGZAttackAbility` 的多段框架——它让段0→段1 的切换走统一的 `EnableCollision(0)→DisableCollision()→EnableCollision(1)` 生命周期。策划在 Montage 上拖拽两个 AnimNotifyState（DodgeWindow + ForesightJudge）覆盖段0 区间，再拖一个 `AnimNotifyState_AttackCollision(ConfigIndex=1)` 覆盖段1 区间。零额外 C++ 代码。

#### 3.3b-resource 武器资源子系统——不统一但提供杠杆

**原则：不做统一资源系统。** 气刃槽色阶、虫棍三灯、盾斧瓶计数差异太大。但提供基础设施让各武器各自管理。

**UMHGZWeaponResourceComponent（基类，挂载到 Character）：**

| 成员/接口 | 类型 | 说明 |
|------|------|------|
| WeaponTypeTag | FGameplayTag | 关联武器种类 |
| GetCurrentValue() | virtual float | 当前资源量（各子类覆写） |
| GetMaxValue() | virtual float | 资源上限 |
| Consume(float Amount) | virtual bool | 消耗资源，返回是否足够 |
| Restore(float Amount) | virtual void | 回复资源 |
| GetNormalizedValue() | float | 0~1 归一化值（UI 绑定用） |

**各武器子类自行管理特有字段：**

| 子类 | 特有字段 | 特殊逻辑 |
|------|----------|----------|
| `URes_LongSword` | `ESpiritLevel Level`（无/白/黄/红）、`float Amount`、`FTimerHandle DecayTimer` | 击中回复量不同、等级随时间和命中升降、衰减 Timer |
| `URes_InsectGlaive` | `TMap<ELightColor, float> RemainingTimes`（三灯各自倒计时）、`float KinsectStamina` | 同时持两灯=组合Buff、三灯=全Buff且刷新时间、猎虫离身扣耐/归身回耐 |
| `URes_ChargeBlade` | `int32 PhialCount`(0~6)、`bool ShieldCharged`、`FTimerHandle RedShieldTimer` | 瓶被动不消耗、部分招式主动消耗、红盾有时限 |
| `URes_SwitchAxe` | `float ChargeGauge`(0~1) | 连续值充能 |

> **关于"槽"的可量化设计：** 大多数武器资源视觉上是"槽"（进度条/图标组），但逻辑上差异大——色阶是离散枚举、瓶是整数计数、三灯是独立 Timer。**共性在 UI 层**（`DT_WeaponResourceConfig` 按 `WeaponTypeTag` 查表加载对应 Widget），不在逻辑层。不需要统一的"槽"类——各子类管自己的字段，基类只提供 UI 绑定骨架。

**资源可视化：** `DT_WeaponResourceConfig`（DataTable）桥接 `WeaponTypeTag → TSoftClassPtr<UUserWidget>`。UI 系统按当前装备武器的 Tag 查表加载对应的资源条组件。资源组件 Tick 中广播 `OnValueChanged` 委托，UI 订阅刷新。

**词条/装备对资源的加成：** 走现有 GAS 体系——如"气刃槽回复速度+20%"通过 GE 修改 `URes_LongSword` 的 `RegenMultiplier` 参数（非 AttributeSet 属性，是资源组件自有的 float），不走 AttributeSet。资源系统独立于 GAS Attribute，通过 GE 修改倍率参数间接影响。

> **设计思路：** 攻击 Ability 的共性是"播 Montage → 等动画通知 → 碰撞检测 → 命中 → Apply 伤害 GE"。这部分逻辑 100% 可复用，抽到 `UMHGZAttackAbility` 后，新增一把武器只需：创建蓝图子类 → 指定 Montage → 配 `AttackSegments`（每段独立碰撞+动作值+破坏值+多跳配置）→ 完成。0 行代码。
>
> **多段攻击：** 横扫+下劈 = `AttackSegments[0]`（横扫碰撞+MotionValue 0.6）+ `AttackSegments[1]`（下劈碰撞+MotionValue 1.2）。AnimNotifyState_AttackCollision 的 `ConfigIndex` 参数指定激活哪一段。
>
> **单碰撞多跳：** 如登龙剑下批——`AttackSegments[0].Collision` 配一次碰撞框，`MultiHitCount=7, MultiHitInterval=0.1`。Sweep 命中后启动 Timer，每 0.1s Apply 一次伤害 GE，共 7 次。DisableCollision 清除 Timer。
>
> **部位命中与多段判定：**
> - 首帧 Sweep（`SweepMultiByChannel`）确保按空间先后取首个接触的 hitzone，解决 UE Overlap 事件不保证顺序的问题。Sweep 从武器上一帧位置扫到当前帧，按 `FHitResult.Time` 升序取第一个带 `HitzoneQueryTag` 的命中
> - `HitTargets` 以（Actor, 骨骼名）组合去重：同一怪物记录首个 hitzone，后续该怪物的任何 hitzone 被 Overlap 都跳过；不同怪物独立记录
> - 部位信息通过 SetByCaller（`Hitzone.DefenseMultiplier`）和 DynamicTag（`Hitzone.Head` 等）传入 GE Spec，怪物侧 `UExecCalc_Damage` 按部位查表修正伤害
> - AnimNotifyState 窗口可短至 1 帧（瞬间判定，如单发斩击）或多帧（持续判定，如龙击炮）。多段攻击在 Montage 中放多个独立 NotifyState，每个负责一段判定


#### 3.3b-extra 蓄力式攻击——GA 内部多阶段状态机

蓄力（大剑蓄力斩、弓蓄力射、盾斧蓄力等）不进连招表路由——全程在一个 GA 内部闭环管理。

**模式：**

```
按下 Y → GA_ChargeSlash 激活（bIsContinuous=true）
  ├── 阶段1 蓄力: 播放蓄力动画，计时器累积 ChargeLevel
  │      - 可选：每帧耗耐（用 Tick × Δt，同奔跑）
  │      - ASC 添加 Input.Modifier.Charging Tag
  │      - ChargeLevel 随时间增长 (Lv1 → Lv2 → Lv3)
  │
  └── 松开 Y（EnhancedInput Completed 事件）→
         → GA 内部读当前 ChargeLevel
         → 根据等级触发对应 Montage（一段/二段/真蓄力斩）
         → Montage 播完 → EndAbility
```

**设计要点：**

| 要点 | 说明 |
|------|------|
| 不进连招表 | 蓄力的释放（Lv1/Lv2/Lv3）不走 `FComboNode` 匹配——它们在同一个 GA 内根据 `ChargeLevel` 分支选择 Montage 和 `DamageConfig`，不经过协调器路由 |
| 输入区分 | EnhancedInput 绑定需同时监听 **`Triggered`**（普通按下→连招匹配）和 **`Completed`**（松开→蓄力 GA 收到释放通知）。§3.2 的 `InitializeAbilitySystem` 绑定时增加 `Completed` 事件 |
| `bIsContinuous` | 蓄力 GA 设为 true——覆盖"按住不放"的整个阶段。若蓄力期间需要耗耐，在 `OnTick` 中用同奔跑的 `ApplyModToAttribute` 方式扣除 |
| `Input.Modifier.Charging` | 蓄力期间 ASC 持有此 Tag。其他系统（移动、翻滚）可借此判断角色正在蓄力并做对应限制 |
| 多段蓄力等级 | 通过成员变量 `int32 ChargeLevel` 累积，递增规则（Lv1→Lv2 需要 1.2s）写在 GA 蓝图的曲线或参数中 |
| 伤害/碰撞差异 | 同一个 GA 内，不同 ChargeLevel 使用不同的 `AttackSegments` 配置——不创建多个 GA 蓝图子类 |
| **释放方向修正** | Completed 事件处理中，读当前摇杆方向 → 若偏离 ≤ `MaxCorrectionAngle`（蓄力类通常设 60°）→ 设 MotionWarping RotationTarget → 再播放释放 Montage。修正角度比普通连招大——蓄力期间玩家有充足时间调整方向 |

#### 3.3b-multiwarp 多段招式二次修正——见切（太刀）完整示例

见切是一个 GA，两段 MotionWarping + 闪避判定 + 击中派生：

```
GA_LS_ForesightSlash::ActivateAbility（MaxCorrectionAngle=180°）：
  1. 读摇杆方向 → 反方向 = 后撤方向
  2. 段0 Warp Target = 反方向 × 500 → MaxWarpAngle=180°（后撤任意方向）
  3. 播放 Montage

  Montage 内：
    ├── AnimNotifyState_DodgeWindow（段0: 后撤无敌帧）
    │     + 同时: AnimNotifyState_ForesightJudge（见切判定窗口）
    │       窗口内若有怪物攻击命中玩家 → ExecCalc返伤害=0（Invincible Tag）
    │       → GA 收到 GameplayEvent(HitStagger) → 记录 bDodgeSuccessful=true
    │
    ├── [段0 NotifyEnd]
    │     GA 读摇杆方向 → 回砍方向
    │     → 段1 Warp Target = 角色位置 + 摇杆方向 × 500 → MaxWarpAngle=120°
    │
    ├── AnimNotifyState_AttackCollision(ConfigIndex=1: 回砍碰撞)
    │     Sweep 命中 → OnAttackOverlap → ApplyDamage
    │     → 首次命中时（bHasHitThisActivation==false）：
    │       若 bDodgeSuccessful:
    │         → RestoreSpiritGaugeToMax()         ← 回满气刃槽
    │         → 协调器→OnAttackHit() → GrantedTags={Combo.Branch.ForesightSuccess}
    │           （大回旋 RequiredTags 含此 Tag → 可匹配派生）
    │       若 !bDodgeSuccessful:
    │         → OnAttackHit() 不触发 → GrantedTags 不应用 → 大回旋不可派生
    │
    └── Montage 播完 → EndAbility
```

> **关键点：** 闪避判定（`bDodgeSuccessful`）是 GA 内部状态——后撤段有无敌帧，但不主动检测。`AnimNotifyState_ForesightJudge` 窗口内注册一个回调监听 `HandleGameplayEvent(HitStagger)`→即使伤害为 0（Invincible），ExecCalc 仍会触发事件→GA 收到事件→设 `bDodgeSuccessful=true`。**不拆成两个 GA**——后撤+回砍是一个完整动作序列，拆开反而丢失上下文。

> **与单段修正的区别：** 两段之间多了一次"读输入 → 更新 Warp Target"。GA 在段 0 NotifyEnd 回调中更新 Target，段 1 的 MotionWarping 读取时已是新值。每段 `MaxWarpAngle` 可不同（段 0=180°，段 1=120°），不需要创建多个 GA。


#### 3.3b-helmbreaker 登龙（太刀）——招内击中派生

登龙是武器基类 `UMHGZLongSwordAbility` 中 `ShouldContinueAfterHit()` 的典型应用场景：

```
GA_LS_HelmBreaker::ActivateAbility：
  1. 段0: 突刺（AnimNotifyState_AttackCollision, ConfigIndex=0）
     OnAttackOverlap → 首次命中时：
       → 调用 ShouldContinueAfterHit(bFirstHit=true)
         → 检查: 气刃槽 ≥ 白（CheckWeaponResourceForAbility）
         → ✅ → 返回 true → 播段1 起跳 Montage
         → ❌ → 返回 false → 播段0-fail 特殊后摇 → EndAbility
  
  2. 段1: 起跳（MotionWarping 上跳，无碰撞）
     播完自动进段2
  
  3. 段2: 下劈（AnimNotifyState_AttackCollision, ConfigIndex=1，MultiHit）
     MultiHitCount=7, MultiHitInterval=0.1s
     AnimNotifyState 每 0.1s Sweep→ApplyDamage（共7次）
     DisableCollision 清除 Timer
     → Montage 播完 → EndAbility
```

> **与连招表命中分支的区别：** 连招表的 `GrantedTags`/`DeniedTags` 是**跨 GA 派生**（GA_A 命中→连招表匹配 GA_B）。`ShouldContinueAfterHit()` 是**招内派生**——同一个 GA 内，段 0 命中后决定段 1 是起跳还是后摇，不经过连招表路由。

> **资源检查顺序：** `CanActivateAbility` 检查资源 ≥ 白 → 门控登龙激活。`ShouldContinueAfterHit` 再次检查资源 ≥ 白 → 门控招内派生。中间无 Tick 间损耗（GAS 单帧原子操作），两轮检查值一致。若将来有"招内耗耐/耗资源"机制，两轮值可能不同——这是设计意图，不是冗余检查。



### 3.3c GA_Dodge — 翻滚/闪避 Ability（不进连招表）

```
UCLASS(BlueprintType, Blueprintable)
class UMHGZDodgeAbility : public UMHGZGameplayAbility
```

继承 `UMHGZGameplayAbility`（非 `UMHGZAttackAbility`——翻滚不涉及攻击碰撞和伤害）。通过 `TryActivateAbilityByTag(Input.Dodge)` 激活，**不进连招协调器，不占 FComboNode 行**。

**设计原则：翻滚是取消/中断动作，不是连招的一环。用单独 Ability + DataAsset 参数化，而非在每武器每个 State 下写重复行。**

**GA_Dodge 自身配置（蓝图可编辑）：**

| 成员 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| SheathedDodgeMontages | TMap\<EComboDirection, TSoftObjectPtr\<UAnimMontage\>\> | 空 | 收刀态各方向翻滚 Montage（所有武器共用，Key=None=无方向翻滚，Forward/Back/Left/Right=方向翻滚）。无敌帧区间由动画师在 Montage 上拖拽 `AnimNotifyState_DodgeWindow` 控制 |

**FWeaponDodgeConfig — 武器翻滚配置（存于 DT_WeaponDodgeConfig DataTable）：**

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| WeaponTypeTag | FGameplayTag | 必填 | 武器种类（主键） |
| UnsheathedMontages | TMap\<EComboDirection, TSoftObjectPtr\<UAnimMontage\>\> | 空 | 拔刀态各方向翻滚 Montage（Key=None=无方向翻滚，Forward/Back/Left/Right=方向翻滚）。无敌帧区间由动画师在 Montage 上拖拽 `AnimNotifyState_DodgeWindow` 控制，不在此处配数字 |

> **耐力消耗：** 翻滚的耐力消耗使用基类 `UMHGZGameplayAbility::StaminaCost`（§3.3），不在此结构体中重复定义。若未来不同武器需要不同翻滚耐力消耗，通过基类的 `FScalableFloat` 关联 CurveTable 按 WeaponTypeTag 查表实现。

**执行流程：**

```
GA_Dodge::CanActivateAbility：
  → 检查 ASC 不含 Combat.State.Hitstun / Knockdown
  → 若 ASC 有 Combat.State.Attacking 但无 Combat.State.DodgeAcceptOpen → 阻塞（攻击中但不在翻滚窗口）
  → 检查 Stamina ≥ StaminaCost（基类字段，§3.3）
  → ✅ 通过 → 激活
  // 纯 Tag 检查：无武器时 Attacking 不存在 → 翻滚始终可用

GA_Dodge::ActivateAbility：
  → 基类扣耐力（StaminaCost × StaminaDeductionRate）
  → 读 ASC 的 Sheathed/Unsheathed 标签：
      Sheathed → 使用 `SheathedDodgeMontages`（GA_Dodge 自身配置，所有武器共用）
      Unsheathed → 通过 `UMHGZDataManager::FindWeaponDodgeConfig(WeaponTypeTag)`（§6.3）获取 FWeaponDodgeConfig → 使用 UnsheathedMontages
  → 读摇杆方向 → 从 UnsheathedMontages/DodgeDirections 中选对应 Montage
     （无方向 → None 键；有方向 → 按象限匹配 Forward/Back/Left/Right；无匹配 → 回退 None）
  → 播放 Montage（含 AnimNotifyState_DodgeWindow 控制无敌帧）
  → Montage 播完 → EndAbility
```

**翻滚与连招的关系：**
- 翻滚激活时 GAS 自动取消当前攻击 GA（若存在）→ 攻击 Montage 中止 → 协调器的 ComboTimeout 兜底回 Idle
- 翻滚的 `CanActivateAbility` 通过 GameplayTag 阻塞受击/击倒状态——与连招窗口无关
- 翻滚无敌帧由独立的 `AnimNotifyState_DodgeWindow` 控制（非 ComboWindow），策划在翻滚 Montage 中拖拽区间

**武器翻滚配置表 DT_WeaponDodgeConfig：**

| 列名 | 类型 | 说明 |
|------|------|------|
| WeaponTypeTag | FGameplayTag | 武器种类（主键） |
| DodgeConfig | FWeaponDodgeConfig | 翻滚参数（各方向 Montage + 无敌帧区间）。注意：耐力消耗由基类 `UMHGZGameplayAbility::StaminaCost` 管理，不在此结构中 |

> **为何翻滚不进连招表：**
> - 每武器 N 个 State × 4 方向 = 连招表大量重复行。收刀态所有武器相同——更不该每武器复制
> - 翻滚的可用帧区间（Animation Notify）和连招窗口（ComboWindow）可能不同——两个独立 AnimNotifyState 各自管理
> - 翻滚取消攻击 → GAS 自动中止攻击 GA → 协调器兜底回 Idle——翻滚不需要"知道"当前连招状态


### 3.3d UMHGZEdgeVaultComponent — 边缘跳越组件

```
UCLASS(ClassGroup=(Movement), BlueprintType, meta=(BlueprintSpawnableComponent))
class UMHGZEdgeVaultComponent : public UActorComponent
```

挂载到 Character。Tick 中轮询 CMC 边缘状态 + 角色移动状态，满足条件时触发 `GA_EdgeVault`。推荐方案 A——比 GAS 被动 Ability 实现更简单、开销更低。

**配置参数（BlueprintReadWrite，策划可在蓝图中调整）：**

| 成员 | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| ForwardTraceDistance | float | "EdgeVault\|Trace" | 100 | 前向 Trace 距离（cm），从角色胯部高度向前探测边缘 |
| ForwardTraceHeight | float | "EdgeVault\|Trace" | 60 | 前向 Trace 起始高度（cm），约为胶囊体半高 |
| DownwardTraceDepth | float | "EdgeVault\|Trace" | 500 | 向下 Trace 深度（cm），验证下方有可落脚平台 |
| MinLandingDepth | float | "EdgeVault\|Trace" | 50 | 最小下落深度（cm）。低于此值视为平地/台阶，CMC 自动步下，不触发跳越 |
| MaxLandingDepth | float | "EdgeVault\|Trace" | 500 | 最大下落深度（cm）。超出此值视为悬崖，不触发跳越（防止摔死） |
| VaultCooldown | float | "EdgeVault\|Cooldown" | 0.5 | 触发冷却（秒），防止同一边缘反复触发 |
| BlockingTags | FGameplayTagContainer | "EdgeVault\|State" | `Combat.State.Hitstun, Combat.State.Knockdown, Combat.State.Dead` | ASC 中存在任一 Tag 时禁止触发 |

**核心方法：**

- `virtual void TickComponent(float DeltaTime, ELevelTick, FActorComponentTickFunction*) override`
  - 作用：每帧执行检测链（全部通过才触发 `GA_EdgeVault`）：
    1. **冷却检查**：`VaultCooldown` 计时器是否到期。未到期 → return。
    2. **状态阻塞**：`ASC→HasAnyMatchingGameplayTags(BlockingTags)` → return。
    3. **移动状态**：角色是否正在奔跑（`ASC→HasMatchingGameplayTag(Combat.State.Sprinting)`）**或**正在闪避（`ASC→GetActiveAbilities` 中包含 `UMHGGZDodgeAbility`）。两者都不满足 → return。
    4. **边缘检测**：调用 `DetectLedge()`。未检测到边缘 → return。
    5. **触发**：`ASC→TryActivateAbilityByTag(Input.EdgeVault)` → 启动 `VaultCooldown` 计时器。

- `bool DetectLedge(FVector& OutLandingPoint) const`
  - 输出：通过 `OutLandingPoint` 返回预估着陆点。返回值为是否检测到有效跳越边缘。
  - 作用：
    1. 从角色位置 + `ForwardTraceHeight` 高度，沿 **角色当前水平速度方向**（`CMC→Velocity` 归一化后的水平分量）做前向 LineTrace（距离=`ForwardTraceDistance`，通道=`ECC_Visibility`，忽略自身）。
    2. 若前向 Trace 命中（前方有墙/地面）→ 未到边缘，return false。
    3. 若前向 Trace 未命中（前方无阻挡，已在边缘外）→ 从前向 Trace 终点向下做 LineTrace（距离=`DownwardTraceDepth`）。
    4. 若向下 Trace 命中，且命中点 Z 坐标与当前地面 Z 的差值在 `[MinLandingDepth, MaxLandingDepth]` 范围内 → 有效跳越边缘，`OutLandingPoint = HitResult.Location`，return true。
    5. 若向下 Trace 未命中（深渊）或深度超出范围 → return false。

- `bool IsSprintingOrDodging() const`
  - 输出：当前是否处于奔跑或闪避状态。
  - 作用：读 ASC 的 `Combat.State.Sprinting` Tag + 检查 ActiveAbilities 中是否有 Dodge Ability。

**边缘检测可视化示意：**

```
角色 → 奔跑方向 →
  ┌──────┐
  │ 角色 │──前向Trace(100cm)──→ ✗ (无命中，已出边缘)
  └──┬───┘                        │
     │ 地面                        │ 向下Trace(500cm)
─────┘                             ↓
═════════════════════════════    ──── 平台
                               ✓ 命中！深度在 [50,500]cm → 触发跳越
                                    │
                                    │ 若深度 > 500cm → 悬崖，不触发
                                    ↓
                                ═══════════════════
```

**GA_EdgeVault — 边缘跳越 Ability：**

```
UCLASS(BlueprintType, Blueprintable)
class UMHGZEdgeVaultAbility : public UMHGZGameplayAbility
```

继承 `UMHGZGameplayAbility`。由 `UMHGZEdgeVaultComponent` 通过 `TryActivateAbilityByTag(Input.EdgeVault)` 触发。

| 成员 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| SprintVaultMontage | TSoftObjectPtr\<UAnimMontage\> | nullptr | 奔跑边缘跳 Montage（RootMotion 驱动前跃位移） |
| DodgeVaultMontage | TSoftObjectPtr\<UAnimMontage\> | nullptr | 翻滚边缘跳 Montage（RootMotion 驱动前跃位移） |

- `ActivateAbility`：判断触发来源（Sprint 或 Dodge）→ 选择对应 Montage → 播放 → Montage 播完 → EndAbility。
- 注意：`InputTag` 设为 `Input.EdgeVault`，供 `TryActivateAbilityByTag` 匹配。

**翻滚边缘跳的特殊处理：**
`GA_Dodge` 执行中若 `UMHGZEdgeVaultComponent` 检测到边缘，Dodge 尚未结束时 `GA_EdgeVault` 被激活。由于 GAS 默认行为是激活新 Ability 时取消当前 Ability（取决于 `InstancingPolicy` 和 `ActivationGroup`），需配置 `GA_EdgeVault` 与 `GA_Dodge` 在同一 `ActivationGroup` 或使用 `bAutoCancelAbilities=false`，使 Dodge Montage 自然 Blend Out 到 Vault Montage。


### 3.4 UMHGZInputComponent — 输入组件

```
UCLASS(ClassGroup=(Input), BlueprintType)
class UMHGZInputComponent : public UActorComponent
```

挂载到 PlayerController 或 Character。**仅负责 IMC（InputMappingContext）生命周期管理**——添加/移除/切换映射上下文。EnhancedInput 与 Ability 的绑定由 ASC 的 `InputBindings`（§3.2）统一管理，InputComponent 不持有绑定数据。

| 成员 | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| InputMappingContext | TArray\<UInputMappingContext\> | "Input" | 空 | 默认 IMC 列表（BeginPlay 时添加到 EnhancedInputSubsystem） |

方法：

- `void InitializeInput(APlayerController* PC)`
  - 输入：PlayerController。
  - 作用：将 `InputMappingContext` 添加到 EnhancedInputSubsystem → 通知 ASC 调用 `InitializeAbilitySystem()`（ASC 内部遍历自己的 `InputBindings` 完成 EnhancedInput 绑定）。仅调用一次。
- `void PushInputMappingContext(UInputMappingContext* IMC, int32 Priority = 0)`
  - 输入：映射上下文、优先级。
  - 作用：运行时叠加额外的 IMC（如进入载具后切换一套按键映射）。不影响 ASC 已有绑定。
- `void PopInputMappingContext(UInputMappingContext* IMC)`
  - 输入：映射上下文。
  - 作用：移除之前 Push 的 IMC，恢复默认映射。

> **职责分离：** InputComponent 只管 IMC 的添加/移除/切换（"哪些按键可用"），ASC 的 `InputBindings` 管 IA→Tag→Ability 的映射（"按键触发什么技能"）。两者不重叠——InputComponent 不持有任何 `FAbilityInputBinding`。进入载具等场景需要换按键映射时，InputComponent Push/Pop IMC 即可；限制攻击/不可操作场景不通过解绑实现——GAS 的 `CanActivateAbility` 在 Ability 端通过 GameplayTag 阻塞拦截激活，输入绑定保持不变。

**统一派发流程（绑定在 ASC 侧，路由在 ASC 侧）：**

```
EnhancedInput (所有按键/摇杆)
  → Lambda 捕获的 FGameplayTag → ASC→OnInputActionTriggered(AbilityTag)
    → Tag.MatchesTag("Input.Weapon") ?
        ├── 是 → 查找 Active 的 GA_WeaponComboCoordinator
        │        → Coordinator→HandleWeaponInput(AbilityTag)
        │        → 协调器内部：帧级批处理 → StateIndex 匹配 → ActivateAbility(Class)
        │
        └── 否 → ASC→TryActivateAbilityByTag(AbilityTag)
                 → GAS 标准路径
```

> **设计思路：** EnhancedInput 绑定时用 lambda 捕获 `FGameplayTag`，回调直接传入——无需在 `OnInputActionTriggered` 内再次查 `InputBindings` 数组。分叉仅在 `MatchesTag("Input.Weapon")` 判断处。协调器不绑定 EnhancedInput，只暴露 `HandleWeaponInput(FGameplayTag)`。武器未装备时按 Y/B/RT → 判别为武器 Tag → 查协调器 → 不存在 → 静默跳过。

**Ability 分类与输入归属：**

| 类别 | 示例 | 授予方 | 输入绑定 |
|------|------|--------|----------|
| 核心能力 | 移动、闪避、边缘跳越、交互 | ASC→CoreAbilities（BeginPlay） | ASC→InputBindings（EdgeVault 例外——由 UMHGZEdgeVaultComponent 代码触发，不绑定按键） |
| 武器连招 | Y/B/RT/组合键 | 装备时 ASC→GrantWeaponAbilities | ASC→InputBindings（IA_Y→Input.Weapon.Y），由 `OnInputActionTriggered` 判别为武器 Tag 后转发给协调器 |
| 特殊动作 | 钩爪、探测、拍照 | 快捷栏手动分配 | 经快捷栏→UseAction→Ability |
| 消耗品 | 喝药、投掷 | 快捷栏自动登记 | 经快捷栏→UseAction→Ability |

### 3.5 与装备系统的对接

`EquipmentComponent→ApplyItemEffects` 中新增第 6 步后的逻辑：

```
6. 通知 ASC:
   ASC→RemoveWeaponAbilities()             // 移除旧武器能力
   ASC→GrantWeaponAbilities(ComboAbilities) // 授予新武器连招能力
   ASC→GiveAbility + TryActivateAbility(GA_WeaponComboCoordinator)  // 先激活协调器（Infinite，空状态）
   // 然后 ApplyItemEffects → 步骤5 → 通过 UMHGZDataManager::FindWeaponComboData 获取 ComboData
   // → Coordinator→SetComboData(ComboData) → 构建 StateIndex → 开始接收输入
   // 输入绑定不变——所有武器共用 Input.Weapon.Y/Input.Weapon.B/Input.Weapon.RT
```

> **设计思路：** 所有武器的同类型攻击共用同一组 `InputTag`。`IA_Y` 始终映射到 `Input.Weapon.Y`。切换武器时只换 ComboData——太刀的 `GA_Sword_Slash_01` 被移除，大剑的 `GA_GreatSword_Slash_01` 被授予。但输入路由不变：`IA_Y` → lambda 捕获 `Input.Weapon.Y` → `OnInputActionTriggered(Input.Weapon.Y)` → 判别为武器 Tag → 转发协调器 → `HandleWeaponInput(Input.Weapon.Y)` → StateIndex 匹配 → 激活大剑的 GA。


### 3.6 UMHGZWeaponComboData — 连招表 DataAsset

```
UCLASS(BlueprintType)
class UMHGZWeaponComboData : public UPrimaryDataAsset
```

每武器种类一个，策划在编辑器中配置完整连招图（有向图，允许环）。

| 成员 | 类型 | 说明 |
|------|------|------|
| WeaponTypeTag | FGameplayTag | 关联武器种类 |
| ComboEntries | TArray\<FComboNode\> | 连招节点列表 |

**FComboNode 结构体：**

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| StateName | FName | 必填 | 当前所处的具体招式名（"Idle" / "RisingSlash" / "DoubleSlash" / "TornadoSlash"…），非抽象段位编号。`"Idle"` 为起手待机态 |
| bMatchAnyState | bool | false | 为 true 时忽略 StateName 匹配——匹配任意招式状态（含 Idle）。用于纳刀、起跳等通用招式。**不自动排除任何状态**——若需排除受击/击倒/Idle，使用 `RequiredTags`+`BlockedTags` 显式声明。示例：特殊纳刀 `bMatchAnyState=true` + `RequiredTags={Unsheathed}` → Idle 默认持 `Sheathed` → 不可起手 |
| InputAction | FGameplayTag | 必填 | 触发条件（`Input.Weapon.Y` / `Input.Weapon.B` / `Input.Weapon.RT` 为基础按键；`Input.Weapon.YB` / `Input.Weapon.RTA` 等为 Chord Trigger 同时按键）。修饰态（如按住 LT 瞄准）不走单独 InputAction——通过 `RequiredTags={Input.Modifier.Aiming}` 区分 |
| DirectionalInput | EComboDirection | None | 方向条件：None=不判方向 / Forward / Back / Left / Right。不匹配则跳过该节点 |
| NextState | FName | 必填 | 命中后跳转到的招式名（可指向自身或前序招式，有向图允许环） |
| AbilityClass | TSubclassOf\<UMHGZAttackAbility\> | nullptr | 触发的攻击 GA 蓝图（Montage 由 GA 蓝图内部指定，连招表不持有动画引用） |
| StaminaRequired | float | 0 | 耐力门槛——连招匹配时协调器检查 `CurrentStamina ≥ Required`。**不负责扣耐**——实际扣除由 GA 的 `UMHGZGameplayAbility::StaminaCost`（§3.3）在 ActivateAbility 中执行。与 GA 的 StaminaCost 区分：一个管"能不能放"（门控），一个管"放完扣多少"（消耗） |
| RequiredTags | FGameplayTagContainer | 空 | 激活前提——ASC **必须持有全部**这些 Tag（AND）。含状态标签：`Grounded/Unsheathed`（地面招式）、`Aerial/Unsheathed`（空中招式）、`Sheathed`（拔刀攻击）。Buff/PowerUp 也在此列 |
| BlockedTags | FGameplayTagContainer | 空 | 激活阻止——ASC **必须不持有任一**这些 Tag（NOR）。用于排除特定状态：登龙剑设 `BlockedTags={Combo.Branch.PostRoundslash}`，大回旋 `GrantedTags` 含此 Tag → 登龙无法从大回旋后派生 |
| GrantedTags | FGameplayTagContainer | 空 | **GA 首次命中后**由协调器授予的临时 Tag，供后续节点 RequiredTags/BlockedTags 判断（非激活时立即授予）。空挥则 GrantedTags 不生效 → 依赖此 Tag 的后续节点匹配失败 → 空挥断连。示例：`Combo.Branch.SpiritBlade` 锁气刃分支；`Combo.Branch.PostRoundslash` 标记大回旋后 |
| bRequiresHitToContinue | bool | false | 为 true 时本节点必须命中才能接下一段（协调器仅收到 GA 命中通知后才应用 GrantedTags）。false=激活即授予，允许空挥接下一段 |
| bRequiresWindowOpen | bool | true | 为 false 时本节点匹配**不受 `Combat.State.ComboWindowOpen` Tag 限制**——即使连招窗口关闭也能触发。实际可用性仍受 `RequiredTags` 约束（收虫/纳刀需 `DodgeAcceptOpen`）。默认 true |
| bResetsComboLevel | bool | false | 命中后是否重置连段计数（不影响状态机，仅供伤害修正等系统读取） |
| Priority | int32 | 0 | 显式匹配优先级。同层（精确招式/通用招式 + DirectionalInput）内有多个候选行满足 InputAction 条件时，Priority 高的优先匹配。取代原来的"RequiredTags 数量降序"规则——Tag 数量多不等于匹配更精确。策划显式指定数值，越大越优先 |
| ComboTimeout | float | 10.0 | 绝对安全兜底超时（秒）。自 GA 激活起算，到期时若仍未回 Idle→强切 Idle+清临时 Tag。★ 正常流程 Montage 播完→EndAbility→OnAttackFinished() 已回 Idle，此计时器不触发。仅 Montage 卡死/GA 异常未结束/超长运镜等极端情况介入。10s 足够覆盖任何合法动画 |

**EComboDirection 枚举：**

| 值 | 说明 |
|----|------|
| None | 不检测方向 |
| Forward | 摇杆前推（相对角色朝向） |
| Back | 摇杆后拉 |
| Left | 摇杆左推 |
| Right | 摇杆右推 |

> **设计决策——为何 StateName 用具体招式名而非抽象段位编号：**
> - 多路径收敛：虫棍的 `上捞斩→Y→二连斩` 和 `突刺→Y→二连斩` 收敛到同一招，两行出招表自然表达——抽象编号（"Light2"）无法区分两条路径
> - 派生差异透明：`上捞斩→B→飞圆斩`（直达）vs `突刺→B→横扫→B→飞圆斩`（需中间招）——招式名让完整路径一目了然
> - `bMatchAnyState` 处理通用招式：太刀纳刀、虫棍起跳等"任意地面招式后均可接"的动作只需一行 `* + RT → Vault`，无需在每个 StateName 下写重复行
>
> **设计决策——为何采用出招表（Move List）而非纯 Tag 连招：**
> - 怪猎武器连招是"作者编排的固定曲谱"而非"玩家自由组合的乐高"：大剑蓄力斩→强蓄力斩→真蓄力斩是严格三段递进，`StateName→NextState` 天然表达这种有向图结构
> - **蓄力类攻击不在连招表中路由：** 蓄力（大剑蓄力、弓蓄力射等）是 GA 内部的多阶段状态机（§3.3b-extra），蓄力→释放全流程在一个 GA 内闭环。释放后的连招（如真蓄力斩后接翻滚/纳刀）才回到协调器路由——此时 FComboNode 的 `StateName` 设为蓄力斩完成后的招式名即可
> - 策划可视化：一把太刀 15+ 招式、30+ 条分支在一张 DataAsset 中全部可见；纯 Tag 方案下连招拓扑分散在多个 GA 蓝图的 Tag 条件中，改一个分支需交叉检查多处
> - 连招窗口由动画师在 Montage 中拖拽 AnimNotifyState 区间决定（§3.8），精确到帧——代码计时器无法做到这种精度
> - Tag 在擅长的位置发挥作用：输入桥接（`Input.Weapon.Y`→ASC）、武器分类（`WeaponTypeTag` 查表）、战斗状态标记（`Stagger`/`Dead`）、动态分支条件（`RequiredTags`/`GrantedTags`）
> - 虫棍类多派生、无收尾招武器：有向图允许环（`DoubleSlash→Y→DoubleSlash` 自循环），窗口超时自然回 Idle；方向+按键组合用 `DirectionalInput` 区分派生
> - 通用招式用 `bMatchAnyState`：一行覆盖所有地面招式，RequiredTags 排除受击/击倒状态防止异常衔接
>
> **地面/空中/拔刀态管理：**
> - `Combat.State.Grounded` / `Aerial` 由角色 `MovementComponent` 管理——`OnLanded` 加 Grounded 并移除 Aerial、`OnJump`/`OnFell` 加 Aerial 并移除 Grounded（两操作在同一函数调用内完成）。协调器不主动判断地面/空中——只读 ASC 的 Tag 状态
> - `Combat.State.Sheathed` / `Unsheathed` 由 `GA_Sheathe` / `GA_Unsheathe` 管理。非战斗默认持有 `Sheathed`；按攻击键或拔刀键 → 激活拔刀 GA → 移除 `Sheathed`、添加 `Unsheathed`。纳刀同理反向操作
> - 每个 `FComboNode` 通过 `RequiredTags` 声明自己所属的状态条件（地面+持刀 / 空中+持刀 / 地面+收刀…）。被击飞时 ASC 自动获得 `Aerial` + `Hitstun` → 地面招式的 RequiredTags 匹配失败 → 输入被忽略。落地后 `Aerial` 移除 → 地面招式恢复
>
> **复合输入与修饰态——三种模式的处理：**
>
> | 模式 | 示例 | 实现 | FComboNode 如何区分 |
> |------|------|------|---------------------|
> | 长按修饰+点按 | 按住 LT 瞄准时按 B | LT→Hold trigger→设 `Input.Modifier.Aiming` Tag；B 照常触发 | 同一 InputAction（B）+ 不同 RequiredTags（空 vs `Aiming`）匹配不同行 |
> | 同时按 | Y+B | `IA_YB` + `UInputTriggerChordAction` 引用 IA_Y 和 IA_B | 独立 InputTag `Input.Weapon.YB`，与 Y、B 不冲突 |
> | 嵌套长按 | 按住 RT 蓄力 + 按住 LT 瞄准 | 两个独立 Hold trigger，各管各的 Tag | `RequiredTags={Charging, Aiming}` 匹配蓄力瞄准态招式 |
>
> **核心原则：** 不创建组合爆炸的 InputAction——修饰态走 GameplayTag（`Input.Modifier.*`），真正的同时按键走 Chord Trigger。`FComboNode.RequiredTags` 无需新增字段，修饰态 = 额外的 Tag 匹配条件，出招表的多行自然区分。
>
> **Tag 辅助示例——太刀气刃斩：**
>
> `Slash_Spirit1` 设 `GrantedTags={Combo.Branch.SpiritBlade}`，`Slash_Spirit2` 设 `RequiredTags={Combo.Branch.SpiritBlade}`。只有成功命中 Spirit1 打上 Tag 才能接 Spirit2，空挥则断连。分支逻辑在出招表中显式声明，不隐藏在各 GA 内部。
>
> **虫棍完整出招表演示：**
>
> ```
> 起手（StateName="Idle"，所有行 RequiredTags={Grounded}）：
>   Idle + Y + None           → RisingSlash     (上捞斩，ReqTag+Unsheathed)
>   Idle + Y + Forward        → Thrust          (突刺，前+Y起手，ReqTag+Unsheathed)
>   Idle + B + None           → WideSweep       (横扫，ReqTag+Unsheathed)
>   Idle + Y + None           → DrawSlash       (拔刀斩，ReqTag+Sheathed→Granted=Unsheathed)
>
> 上捞斩后（StateName="RisingSlash"，ReqTags={Grounded, Unsheathed}）：
>   RisingSlash + Y           → DoubleSlash
>   RisingSlash + B           → TornadoSlash
>
> 突刺后（StateName="Thrust"，ReqTags={Grounded, Unsheathed}）：
>   Thrust + Y                → DoubleSlash
>   Thrust + B                → WideSweep
>
> 空中招式（StateName="Aerial.Idle"，bMatchAnyState=true，ReqTags={Aerial, Unsheathed}）：
>   * + Y                     → AerialSlash1    (跳跃突进斩)
>
> 通用招式——任意地面招式后（StateName="*"，bMatchAnyState=true，ReqTags={Grounded, Unsheathed}）：
>   * + RT                    → Vault           (起跳，Granted={Aerial} 进入空中态)
>   * + RT                    → IaiSheath       (纳刀，Granted={Sheathed}→移除Unsheathed)
>
> 修饰态区分示例（拔刀态下 LT 瞄准 + B vs 纯 B）：
>   Idle + B + ReqTags={Unsheathed}                     → WideSweep      (普通横扫)
>   Idle + B + ReqTags={Unsheathed, Input.Modifier.Aiming} → WyvernFire     (LT+B 龙击炮)
>
> 同时按键示例（Y+B Chord Trigger）：
>   Idle + YB + ReqTags={Grounded, Unsheathed}          → SuperSlash     (超必/收尾技)
> ```
>
> **太刀完整出招表演示（BlockedTags + bRequiresHitToContinue + 资源门控）：**
>
> ```
> 起手（StateName="Idle"，ReqTags={Unsheathed}）：
>   Idle + Y + None           → RisingSlash     (纵斩)
>   Idle + Y + Forward        → Thrust          (突刺)
>   Idle + YB + None          → HelmBreaker     (登龙剑，可起手 ✓)
>
> 特殊纳刀——任意持刀态派生，不可起手（StateName="*"，bMatchAnyState=true，bRequiresWindowOpen=false）：
>   *, bMatchAnyState=true, bRequiresWindowOpen=false
>     Input=RT, ReqTags={Unsheathed}, BlockedTags={Hitstun, Knockdown}
>                             → IaiSheath       (窗口外可用；Req=Unsheathed→Idle不可起手 ✓)
>
> 大回旋收刀（StateName="SpiritRoundslash"）：
>   SpiritRoundslash + Y      → IaiSheath       (纳刀，Granted={Sheathed})
>   SpiritRoundslash + None   → Idle            (Montage 结束自动回 Idle，Granted={Combo.Branch.PostRoundslash})
>
> 纳刀后派生（StateName="IaiSheath"，ReqTags={Sheathed}）：
>   IaiSheath + Y             → IaiSlash        (小居合)
>   IaiSheath + B             → IaiSpiritSlash  (大居合)
>   （翻滚走独立路径 GA_Dodge，不在出招表中）
>   （移动被 Sheathed Tag 阻塞——见 §三 移动阻塞说明）
>
> 登龙剑——除大回旋/纳刀外任意派生（StateName="*"，bMatchAnyState=true）：
>   * + YB + ReqTags={Unsheathed}, BlockedTags={Combo.Branch.PostRoundslash}
>                             → HelmBreaker     (大回旋后PostRoundslash在Blocked→排除 ✓)
>   // GA_HelmBreaker 内部：
>   //   第一段突刺: bRequiresHitToContinue=true
>   //   ShouldContinueAfterHit() 覆写→检查气刃槽≥白→true才播第二段
>   //   第二段: 7段CollisionConfig + AnimNotifyState
>   //   第二段后半: 监听Y输入→加速下批；不按→自动完成
> ```
>
> 协调器匹配时：`bMatchAnyState=true` 的节点不自动排除任何状态——是否排除受击/击倒/特定招式由 `RequiredTags` + `BlockedTags` 显式声明。
>
> **出招表数据模型——平面搜索而非树状嵌套：**
>
> `ComboEntries` 是一个**平面数组**，包含该武器所有合法连招分支的全部行。出招表本身不是树——它是一组条件→结果的行集合。`NextState` 是一个**字符串键**（不是指针），标识"这招打完后你处于什么招式名"。协调器构建 `TMap<FName, TArray<int32>> StateIndex`，按 `StateName` 分组存储行号索引，运行时按 `CurrentState` 做 O(1) 查找候选行，再遍历候选行匹配 Input/Direction/Tag 条件。
>
> ```
> 数据结构示意：
> StateIndex["Idle"]        → [0, 1, 2]    // 3 条起手分支
> StateIndex["RisingSlash"] → [3, 4]       // 2 条后续分支
> StateIndex["Thrust"]      → [5, 6]
> StateIndex["WideSweep"]   → [7]
> StateIndex["DoubleSlash"] → [8, 9]
> StateIndex["*"]           → [10, 11]     // bMatchAnyState=true 的行放入 "*" 桶
> ```
>
> 匹配时查询 `StateIndex[CurrentState]` 和 `StateIndex["*"]` 两个桶，分别代表精确匹配和通用招式。


### 3.7 GA_WeaponComboCoordinator — 连招协调器

```
UCLASS(BlueprintType, Blueprintable)
class UMHGZWeaponComboCoordinatorAbility : public UGameplayAbility
```

**Infinite 持续型 Ability**——装备武器时授予并激活，卸下时结束。整个装备期间协调器保持 Active，不结束时不影响其他 Ability 同时运行。在 Activate 中构建 `StateIndex`，在 EndAbility 中清理解绑。**协调器不绑定 EnhancedInput**——只暴露公共方法 `HandleWeaponInput(FGameplayTag)`，由 ASC 的 `OnInputActionTriggered` 在判别为武器 Tag 时调用。

**运行时状态（非 UPERTY，协调器内部维护）：**

| 成员 | 类型 | 说明 |
|------|------|------|
| CurrentState | FName | 当前所处的招式名（初始 "Idle"） |
| ComboData | TObjectPtr\<UMHGZWeaponComboData\> | 当前武器的连招表 |
| StateIndex | TMap\<FName, TArray\<int32\>\> | 按 StateName 分组的行号索引。含 `"*"` 桶存放所有 bMatchAnyState=true 的行。O(1) 查找候选行 |

| ComboTimeoutTimer | FTimerHandle | 绝对安全兜底计时器。每次新 GA 激活时重置，时长=`ComboTimeout`(10s)。到期时若仍未回 Idle→强切+清临时 Tag。正常流程 Montage 完成先触发，此计时器不介入 |
| PendingInputs | TArray\<FGameplayTag\> | 当前帧累积的待处理武器输入（帧级批处理——收集后统一排序匹配） |
| InputBatchTimer | FTimerHandle | 批处理延迟 Timer（0 秒延迟，下一帧触发排序匹配） |
| PendingGrantedTags | FGameplayTagContainer | 当前激活的 GA 待授予的 GrantedTags。GA 激活时存入（非立即应用），GA 首次命中时由 `OnAttackHit()` 写入 ASC；若 GA 结束仍未命中则丢弃 |
| PreInputTag | FGameplayTag | 预输入缓冲区——ComboWindow 打开前景早按键的 Tag（后覆盖前，仅存一个）。在 `HandleWeaponInput` 中窗口关闭时写入，`ComboWindow→NotifyBegin` 中刷新消费 |
| PreInputTimestamp | float | 预输入捕获时刻（`GetWorld()->GetTimeSeconds()`）。用于判定是否在 `PreInputLifetime` 内 |
| PreInputLifetime | float | 预输入有效窗口（秒）。默认 0.15（~9帧@60fps），策划可在蓝图中调整。超时未等到窗口打开则清空缓冲 |

**公共方法：**

- `void SetComboData(UMHGZWeaponComboData* InData)`
  - 输入：武器连招表 DataAsset。
  - 作用：由 EquipmentComponent 在 ApplyItemEffects 中调用。构建 StateIndex（遍历 ComboEntries 按 StateName 分组），完成后协调器开始接收输入。仅在 Activate 后、EndAbility 前有效。
- `void HandleWeaponInput(FGameplayTag AbilityTag)`
  - 输入：武器输入 Tag（`Input.Weapon.Y` / `Input.Weapon.B` 等）。
  - 作用：由 ASC 的 `OnInputActionTriggered` 在判别为武器 Tag 时调用。若 ComboData 未注入（StateIndex 为空）→ 忽略。
- `void OnAttackHit()`
  - 作用：由攻击 GA 的 `ApplyDamage` 在首次命中时调用。将 `PendingGrantedTags` 写入 ASC。
- `void OnAttackFinished()`
  - 作用：由攻击 GA 的 `EndAbility` 调用。若 `CurrentState` 在此期间未被新 GA 激活变更 → `CurrentState = "Idle"` + 清除 `Combo.Branch.*` 临时 Tag。


---

**完整运行时工作流（6 阶段）：**

```
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
阶段 A：装备武器 → 协调器激活
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
1. EquipmentComponent→OnEquipmentChanged()
2. ASC→GrantWeaponAbilities(新武器 GA 列表)
3. ASC→GiveAbility(GA_WeaponComboCoordinator)
4. ASC→TryActivateAbility(GA_WeaponComboCoordinator)  // 先激活（Infinite Ability），此时 ComboData 为空
5. 协调器 Activate：
   - CurrentState = "Idle"
   - StateIndex 为空——等待 ComboData 注入
   - 进入等待循环（Infinite，不结束）
6. EquipmentComponent→ApplyItemEffects → 步骤5 → 通过 UMHGZDataManager::FindWeaponComboData(WeaponTypeTag) 获取 ComboData
7. 协调器→SetComboData(WeaponComboData)：
   - 构建 StateIndex：遍历 ComboEntries，按 StateName 分组存行号
   - bMatchAnyState=true 的行额外放入 "*" 桶
   - 开始接收输入（输入由 ASC 的 `OnInputActionTriggered` 统一派发，判别为武器 Tag 后调用 `Coordinator→HandleWeaponInput(Tag)`）

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
阶段 B：玩家按键 → 起手攻击（CurrentState="Idle"）
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
7. ASC→OnInputActionTriggered(Input.Weapon.Y) → 判别为武器Tag → 协调器→HandleWeaponInput(Input.Weapon.Y)：
   - 候选行 = StateIndex["Idle"] ∪ StateIndex["*"]     (O(1)+O(1))
   - 候选行按优先级排序：
       1) bMatchAnyState=false + DirectionalInput 具体 → 精确招式+方向
       2) bMatchAnyState=false + DirectionalInput=None → 精确招式无条件
       3) bMatchAnyState=true + DirectionalInput 具体 → 通用招式+方向
       4) bMatchAnyState=true + DirectionalInput=None → 通用招式无条件
     同优先级内按 `Priority` 降序（Priority 相同的行任意顺序均可——策划显式指定数值消除歧义）
   - 遍历候选行，对每行依次检查全部条件：
       条件A: InputAction == 触发的 InputTag                         （必须）
       条件B: DirectionalInput 匹配 或 DirectionalInput==None        （None=不判方向，任何摇杆输入都通过）
       条件C: ASC→HasMatchingGameplayTag(RequiredTags) 全部满足     （必须，空=通过）
       条件D: CurrentState=="Idle" 或 ASC→HasMatchingGameplayTag(Combat.State.ComboWindowOpen) 或 Entry.bRequiresWindowOpen==false
       条件E: ASC→HasAnyMatchingGameplayTag(BlockedTags) == false    （必须，空=通过）
       条件F: ASC→GetNumericAttribute(Stamina) >= Entry.StaminaRequired （必须，耐力不足则跳过。注意：此门槛不扣耐——实际扣除由 GA ActivateAbility 执行）
     → 全部通过 = **匹配判定成功**。立即停止遍历，执行下方步骤 7a-7e。不继续检查后续行。
   - CurrentState=="Idle" → 不检查 ComboWindowOpen Tag（起手无需窗口）
   - ✅ 匹配成功：
     a. ActivateAbility(Entry.AbilityClass)          // 攻击 GA 开始
     b. CurrentState = Entry.NextState                // 立即更新状态
     c. `PendingGrantedTags = Entry.GrantedTags`      // 存入待授予列表（非立即应用——等待 GA 命中通知）
        → 若 `Entry.bRequiresHitToContinue==false`，也可在此步直接 Apply（允许空挥接下一段）
     d. 重置 ComboTimeoutTimer（时长 = Entry.ComboTimeout，默认 10s）
   - ❌ 无匹配 → 忽略输入

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
阶段 C：攻击 GA 执行中
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
8. GA_RisingSlash→ActivateAbility：
   - 扣耐力（StaminaCost × StaminaDeductionRate）
   - 播放 GA 自身持有的 Montage

9. Montage 到达 AnimNotifyState_AttackCollision→NotifyBegin：
   - MeshComp→GetOwner()→ASC
   - 查找 Active 的 GA_RisingSlash（或任何当前攻击 GA）
   - Cast 到 UMHGZAttackAbility → EnableCollision(ConfigIndex)
   - 创建碰撞体 → 下一 Tick Sweep → 命中怪物 → ApplyDamage
   - NotifyEnd → DisableCollision() → 销毁碰撞体

10. Montage 到达 AnimNotifyState_ComboWindow→NotifyBegin：
    - MeshComp→GetOwner()→ASC
    - 查找 Active 的 GA_WeaponComboCoordinator
    - ASC→AddLooseGameplayTag(Combat.State.ComboWindowOpen)          ✅ 窗口打开

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
阶段 D：窗口内按键 → 连招下一段
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
11. ASC→OnInputActionTriggered → 判别为武器Tag → 协调器→HandleWeaponInput(Tag)：
    - 候选行 = StateIndex[CurrentState] ∪ StateIndex["*"]
    - 遍历匹配条件（同上）
    - ASC→HasMatchingGameplayTag(Combat.State.ComboWindowOpen)==true ✅ → 继续
    - ✅ 匹配成功：
      a. 取消旧 ComboTimeoutTimer
      b. ActivateAbility(Entry.AbilityClass)
      c. CurrentState = Entry.NextState
      d. `PendingGrantedTags = Entry.GrantedTags`（同上，存入等待命中通知；`bRequiresHitToContinue==false` 时可直接 Apply）
      e. 重启 ComboTimeoutTimer
    - ❌ 无匹配 → 忽略

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
阶段 D-2：GA 命中回调 → GrantedTags 实际生效
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
10b. GA 首次命中（`UMHGZAttackAbility::ApplyDamage` 内，`bHasHitThisActivation==false` 时触发）：
    - GA 通过 `ASC→GetActiveAbilities()` 查找 `GA_WeaponComboCoordinator`
    - 调用 `Coordinator→OnAttackHit()`
    - 协调器：`ASC→AddLooseGameplayTags(PendingGrantedTags)` → 清空 `PendingGrantedTags`
    - （此时后续节点的 `RequiredTags` 中包含这些 Tag 的才能被匹配——空挥则 Tag 永远不出现，断连）

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
阶段 E：窗口关闭 + 自然回 Idle
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
12. AnimNotifyState_ComboWindow→NotifyEnd：
    - ASC→RemoveLooseGameplayTag(Combat.State.ComboWindowOpen)
    - 攻击 Montage 继续播放剩余帧
    - 播放完毕 → BlendOut → 动画回到 Idle 待机

12b. Montage 完成 → GA EndAbility（★ 主要回 Idle 路径）：
    - 攻击 GA 的 Montage 自然播完 → EndAbility 被调用
    - GA 通过 `ASC→GetActiveAbilities()` 查找 `GA_WeaponComboCoordinator`
    - 调用 `Coordinator→OnAttackFinished()`
    - 协调器检查：若 `CurrentState` 在此期间未被新 GA 激活变更（窗口内无连招输入）→ `CurrentState = "Idle"` + 清除 `Combo.Branch.*` 临时 Tag
    - 若 CurrentState 已变更（玩家在窗口内接了下一招）→ 不做任何操作
    - // 优势：Montage 自身长度即精确计时，无需设计师猜测填写每招动画秒数

13. 窗口关闭后按键：
    - ComboWindowOpen Tag 不存在 且 CurrentState!="Idle"
    → ❌ 输入被忽略

14. （ComboIdleTimer 已移除——回 Idle 由步骤 12b Montage 完成驱动，无需额外无输入计时器）

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
阶段 F：异常兜底 / 武器卸下
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
15. ComboTimeoutTimer 到期（仅极端异常触发）：
    - 攻击 Montage 被打断/卡地形/动画蓝图 bug/超长运镜 → GA 永不完结或异常长
    - → 强制 CurrentState = "Idle"
    - → ASC→RemoveLooseGameplayTag(Combat.State.ComboWindowOpen)
    - → 清除 Combo.Branch.* Tag

16. EndAbility（武器卸下）：
    - 清除所有 Combo.Branch.* Tag
    - CurrentState 重置
    - StateIndex 清空
    - （输入派发路径不变——ASC 的 `OnInputActionTriggered` 判别为武器 Tag 后找不到协调器，自然跳过）
```

> **设计要点：**
> - 协调器是 **Infinite 持续型 Ability**：装备期间永不结束，GAS 允许多个 Ability 同时 Active——协调器和攻击 GA 并行运行，互不阻塞
> - **StateIndex** 是关键优化：`ComboEntries` 是策划编辑的平面数组，协调器在 Activate 时构建 `TMap<FName, TArray<int32>>`，后续匹配 O(1) 查候选行 → 遍历候选行（通常 2-5 行），无需全量遍历
> - **Montage 完成 = 主要回 Idle 路径**：攻击 GA 的 Montage 自然播完 → EndAbility → `Coordinator→OnAttackFinished()` → 若期间无新 GA 激活，`CurrentState="Idle"`。动画时长即精确计时，无需设计师猜测填写秒数
> - **ComboTimeout = 唯一安全兜底**：自 GA 激活起 10s 后若仍未回 Idle（Montage 卡死/GA 异常未结束/超长运镜），强制 `CurrentState="Idle"` + 清临时 Tag。正常流程下 Montage 完成先触发，此计时器不介入。合并原 ComboExpiry 和 ComboTimeout 的职责——两者在 Montage 驱动模型下完全重叠
> - **状态标签管理**：`Combat.State.Grounded/Aerial` 由 MovementComponent 管理，`Combat.State.Sheathed/Unsheathed` 由 `GA_Sheathe/GA_Unsheathe` 管理。`Input.Modifier.Aiming/Charging/Sheathed` 由对应输入 Action 的 Hold/Trigger 驱动。协调器不主动设置这些标签——只通过 `RequiredTags` 读取 ASC 的 Tag 状态做匹配过滤
> - **复合输入**：长按修饰（LT 瞄准）+ 点按（B）通过 `RequiredTags` 的附加条件区分——LT Hold 设 `Input.Modifier.Aiming`，B 照常触发，匹配时多一个 Tag 条件。同时按键（Y+B）走 `UInputTriggerChordAction` 触发独立 `Input.Weapon.YB`。嵌套长按（RT 蓄力+LT 瞄准）两 Tag 共存，`RequiredTags={Charging, Aiming}` 同时满足才匹配。**FComboNode 零字段新增**
> - **帧级输入批处理（Chord Trigger 优先级）**：UE5 EnhancedInput 中 `UInputTriggerChordAction` 触发 `IA_YB` 时，`IA_Y` 和 `IA_B` 也会被各自独立触发——同一帧内协调器的 `HandleWeaponInput` 会依次收到 `Input.Weapon.Y`、`Input.Weapon.B`、`Input.Weapon.YB` 三次调用。为避免单键匹配抢先消耗输入导致 Chord 招式永远无法触发，协调器采用**帧级批处理**：
>   1. `HandleWeaponInput(Tag)` 不立即匹配——将 Tag 追加到 `PendingInputs` 数组，启动一个 0 秒延迟 Timer（下一帧执行）
>   2. Timer 回调中对 `PendingInputs` 排序：**多键 Chord Tag（如 `Input.Weapon.YB`、`Input.Weapon.RTA`）优先级高于单键 Tag（如 `Input.Weapon.Y`）**。同优先级内按 `Priority` 降序（Priority 相同的行任意顺序均可）
>   3. 按排序后顺序遍历匹配——命中即激活 GA 并清空 `PendingInputs`，该帧内不再处理后续输入
>   4. 若本帧无任何匹配，清空 `PendingInputs`（输入被丢弃）
>   > 为何不配置 Chord Trigger "消费"单键：EnhancedInput 的 Chord Trigger 不阻止被引用的 InputAction 独立触发——这是引擎行为，无法通过配置改变。帧级批处理是唯一可靠的解决方案
>
>   协调器运行时状态新增：
>   | PendingInputs | TArray\<FGameplayTag\> | 当前帧累积的待处理武器输入 |
>   | InputBatchTimer | FTimerHandle | 批处理延迟 Timer（0 秒，下一帧触发） |
> - **UI 状态驱动**：Ability 不直接操作 UI——Ability 修改 ASC 的 GameplayTag（如 `Combat.State.Aiming`）和 Attribute，UI 组件（`WBP_HUD` / `WBP_Crosshair` 等）订阅 `ASC→RegisterGameplayTagEvent(Tag, EGameplayTagEventType::NewOrRemoved)` 响应变化。GA_Aim 只需管理自己的 Tag 生命周期，不关心哪些 UI 在监听
> - **输入路径分工**：EnhancedInput 绑定时用 lambda 捕获 `FGameplayTag` → `OnInputActionTriggered(Tag)` → `MatchesTag("Input.Weapon")` 分叉。协调器不绑定 EnhancedInput，只暴露 `HandleWeaponInput(FGameplayTag)`
> - **开销分析**：武器按键 ≈ lambda 捕获（零开销） + MatchesTag O(1) + 查协调器 O(1) + StateIndex hash O(1) + 候选行常数遍历 + GAS 直接激活 hash O(1) ≈ **4 次 O(1)**
> - **武器未装备时**：ASC 派发判别为武器 Tag → 查找协调器 → 不存在 → 静默跳过。无需额外绑定/解绑逻辑
> - 节点匹配优先级（四级排序）：1) 精确招式（bMatchAnyState=false）> 通用招式（true）；2) DirectionalInput 具体 > None；3) `Priority` 降序（策划显式指定）；4) 以上均相同的行任意顺序均可。**优先级排序在协调器运行时自动执行**——收集候选行后按规则排序再遍历，不依赖 DataAsset 中的数组顺序。策划通过 `Priority` 字段精确控制同条件多行的匹配顺序
> - **匹配即停**：候选行按优先级排序后遍历，命中第一个所有条件都满足的行即停止——调用 `ActivateAbility(Entry.AbilityClass)` 并 return。不会出现"一行匹配后继续检查下一行导致重复激活"
> - **状态区分靠需求标签（RequiredTags），不靠禁止标签**：地面 Y 需 `{Grounded}`、空中 Y 需 `{Aerial}`——Grounded 和 Aerial 互斥，永远只有一个被满足，无需显式写"禁止 Aerial"。两行仅一个 Tag 不同时（如普通 B vs 瞄准 B），瞄准 B 多一个 `Aiming` Tag → RequiredTags 更多 → 优先级更高 → 自然胜出。出招表不设禁止条件
> - **攻击 GA 与协调器的解耦（通过两个回调）**：协调器激活攻击 GA 后不等待其结束——立即更新 `CurrentState`、启动 `ComboTimeoutTimer`。攻击 GA 自己播 Montage、做碰撞、自然结束。**耦合仅限两个回调**：(1) `OnAttackHit()`——GA 首次命中时通过 `ASC→GetActiveAbilities()` 查找协调器 → 协调器将 `PendingGrantedTags` 写入 ASC。若 GA 全程空挥，`OnAttackHit()` 永不触发，`PendingGrantedTags` 在 `EndAbility` 中清理。(2) `OnAttackFinished()`——GA 的 `EndAbility` 中调用，协调器检查 `CurrentState` 是否因新 GA 激活已变更，若未变更则回 `"Idle"`（★ 主要回 Idle 路径）。
> - **方向匹配与 None 回退**：`DirectionalInput=None` = 不判方向——任何摇杆输入（含无输入/中性）均通过此条件，因此 None 行是**兜底回退**。`DirectionalInput=Forward` = 仅摇杆前推时通过。例：`Idle+Y, Dir=Forward → Thrust`（前+Y=突刺）+ `Idle+Y, Dir=None → RisingSlash`（Y=上捞斩，回退）。玩家前+Y 时两行条件均满足，但 Forward 优先级 > None → 匹配 Thrust。玩家中性 Y 时 Forward 不满足 → 跳过 → None 行满足 → 匹配 RisingSlash。**策划须确保同一 StateName+InputAction 下存在一行 Dir=None 作为兜底**，否则特定方向以外的输入会无响应
> - 方向判定：`GetLastMovementInputVector` 与 `GetActorForwardVector` 夹角分 4 象限（±45° 为 Forward/Back/Left/Right），无输入或向量长度 < 0.1 视为 None
> - **预输入缓冲（Pre-Input Buffer）**：玩家在 ComboWindow 打开前若干帧按键，应被"记住"并在窗口打开瞬间生效。
>   1. **存储对象——`FGameplayTag`**：EnhancedInput 绑定时已将按键转为 Tag（`Input.Weapon.Y`），这是系统通用语言。不存 `UInputAction*`（需额外查表还原 Tag），不存 `FInputActionValue`（连招不关心摇杆量级）。
>   2. **存储位置——协调器**：已管理 `PendingInputs`、`PendingGrantedTags`，所有"暂存待条件满足再消费"的逻辑都在此。预输入不应例外。
>   3. **工作流——"后覆盖前"单槽缓冲**：
>      - `HandleWeaponInput(Tag)`：若 ASC 无 `Combat.State.ComboWindowOpen` Tag → `PreInputTag = Tag`，`PreInputTimestamp = Now()`。若已有旧 Tag 则直接覆盖（最后按的键优先）。
>      - `AnimNotifyState_ComboWindow→NotifyBegin`：ASC 加 `ComboWindowOpen` Tag → 若 `PreInputTag` 有效且 `(Now − PreInputTimestamp) ≤ PreInputLifetime` → 以该 Tag 调用 `HandleWeaponInput(PreInputTag)`（此时窗口已开，正常匹配）→ 清空缓冲。方向判定使用调用时刻的**当前摇杆方向**（非缓冲时刻方向）。
>      - 超时：若 `PreInputLifetime` 内未等到窗口打开 → 清空缓冲并丢弃。
>   4. **翻滚预输入同理**：DodgeAcceptWindow 开前按 A → ASC 的 `OnInputActionTriggered` 调用 `TryActivateAbilityByTag(Input.Dodge)` → GA_Dodge 的 CanActivateAbility 因 `DodgeAcceptOpen` Tag 不存在而阻塞 → 但预输入缓冲（协调器 PreInputTag 机制）可同样用于 Dodge——窗口关闭时写入，窗口打开时 NotifyBegin 设 Tag 后重新尝试激活。具体预输入方案复用连招预输入，不新增字段。
>   5. **不进缓冲**：长按修饰（`Input.Modifier.*`）是持续 Tag 状态，不存在"捕获瞬间"；Chord Trigger 已被帧批处理合并为独立 Tag（`Input.Weapon.YB`），等同点按。
> - **翻滚窗口与取消（纯 Tag 方案）**：翻滚可用性由攻击 Montage 中的 `AnimNotifyState_DodgeAcceptWindow`（§3.9b）控制——窗口内 ASC 持有 `Combat.State.DodgeAcceptOpen` Tag。`GA_Dodge::CanActivateAbility` 用纯 Tag 检查：`Attacking` 有 + `DodgeAcceptOpen` 无 → 阻塞。窗口通常与 ComboWindow 同时开始，可延伸到收招帧。攻击 GA 的 ActivateAbility/EndAbility 管理 `Attacking` Tag——无武器时此 Tag 不存在，翻滚始终可用 |
> - **招式特有取消（虫棍收虫等）——bRequiresWindowOpen=false + DodgeAcceptOpen Tag**：取消动作经协调器路由（InputAction 仍是 `Input.Weapon.*`）。设 `bRequiresWindowOpen=false` 绕过 ComboWindowOpen Tag 检查，但 `RequiredTags` 须含 `Combat.State.DodgeAcceptOpen`——与翻滚共用取消窗口。不新增 Tag 层 |
> - `bMatchAnyState=true` 的节点本身不做任何状态排除——它匹配任意招式状态。若需排除受击/击倒等不可操作状态，策划在 `RequiredTags` 中显式添加排除条件（如 `RequiredTags` 不含 `Combat.State.Hitstun` 和 `Combat.State.Knockdown` 时自然不匹配这些状态）。不硬编码排除逻辑，保持灵活性（未来可能有"受击中反击"类招式需要在 Hitstun 中激活）


### 3.8 UAnimNotifyState_ComboWindow — 连招窗口通知

```
UCLASS(BlueprintType, Blueprintable)
class UAnimNotifyState_ComboWindow : public UAnimNotifyState
```

挂载在攻击 Montage 中，标记"接受下一招输入"的精确帧区间。替代纯代码计时器。

| 成员 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| bHapticFeedback | bool | false | 窗口结束前是否手柄震动提示 |
| HapticLeadTime | float | 0.05 | 震动提前量（秒） |

**生命周期：**

```
NotifyBegin（窗口打开）：
  → MeshComp→GetOwner()→GetAbilitySystemComponent()
  → ASC→AddLooseGameplayTag(Combat.State.ComboWindowOpen)
  → （可选）按 HapticLeadTime 启动震动计时器

NotifyEnd（窗口关闭）：
  → ASC→RemoveLooseGameplayTag(Combat.State.ComboWindowOpen)
  → 输入不再触发连招
  → 攻击 Montage 自然播放至 BlendOut → 动画回到 Idle 待机
```

> **设计思路：**
> - 连招窗口的起止由动画师在 Montage 时间轴中直接拖拽 AnimNotifyState 区间决定，精确到帧，与动画节奏天然同步
> - 与 DodgeAcceptWindow（§3.9b）一致，直接操作 ASC Tag——不查找协调器，零耦合
> - 窗口关闭后角色自然播完剩余动画帧、BlendOut、回到 Idle——不存在"超时硬切"的突兀感
>
> **与 ComboTimeout 的职责对比：**
>
> | 机制 | 驱动方 | 时长量级 | 作用 |
> |------|--------|:--:|------|
> | ComboWindow（AnimNotifyState） | 动画师在 Montage 中拖拽区间 | 0.1–0.5s | 精确控制哪几帧可接下一招 |
> | ComboTimeout（FComboNode 字段） | 协调器 ComboTimeoutTimer | ~10.0s | 绝对安全兜底——Montage 卡死/GA 异常/超长动画时强制重置。正常流程 Montage 完成先触发（步骤 12b），此计时器不介入 |
>
> ★ 正常流程：Montage 播完 → GA EndAbility → Coordinator→OnAttackFinished() → CurrentState="Idle"（主要路径）。ComboTimeout 仅在被击飞打断、卡地形、动画蓝图 bug、超长运镜等极端异常情况下介入。原 ComboExpiry 已合并入 ComboTimeout——两者在 Montage 驱动模型下职责完全重叠。


### 3.9 UAnimNotifyState_DodgeWindow — 翻滚无敌帧通知

```
UCLASS(BlueprintType, Blueprintable)
class UAnimNotifyState_DodgeWindow : public UAnimNotifyState
```

挂载在翻滚 Montage 中，标记无敌帧的精确区间。与 `ComboWindow` 完全独立——翻滚不在连招系统内，无需访问协调器。

**无伤穿透攻击但不可穿过怪物——碰撞通道分离：**

攻击检测和物理阻挡使用**不同的碰撞通道**。无敌帧期间只关闭 Weapon 通道响应，Pawn 通道保持不变：

```
┌──────────────────────────────────────────┐
│  Weapon 通道（玩家攻击检测用）            │  ← NotifyBegin: 玩家胶囊体设为 Ignore
│  作用: 怪物武器 Sweep 检测玩家             │     NotifyEnd:   恢复原始响应
│                                           │     效果: 攻击 Sweep 物理上穿过玩家
├──────────────────────────────────────────┤
│  Pawn 通道（物理阻挡用）                  │  ← 始终 Block，不变!
│  作用: 角色胶囊体 ↔ 怪物胶囊体             │     效果: 玩家仍被怪物身体阻挡
│         CMC 推挤/重叠修正                  │     不会出现"翻滚穿过整个怪物"
└──────────────────────────────────────────┘
```

> **两层保障：** (1) 碰撞层——Weapon 通道 Ignore，攻击 Sweep 物理上扫不到玩家。(2) GAS 层——`Combat.State.Invincible` Tag，万一有其他路径的攻击命中，ExecCalc 读到 Tag 则伤害=0。

**卡模/穿模风险：**

| 风险 | 对策 |
|------|------|
| 翻滚结束时卡在怪物体内 | CMC 自带 `ResolvePenetration`——两个 Pawn 胶囊体重叠时自动推挤分离。极端情况：`NotifyEnd` 中做一次重叠检测，若仍重叠则强制推离 |
| 大型怪物 vs 小型怪物 | 策划在怪物蓝图上配置 Pawn 通道响应：大型（Block，不可穿过） vs 小型（Ignore，可穿过）——翻滚手感由策划按怪物体型决定 |
| 两段无敌帧之间频繁切换 | `SetCollisionResponseToChannel` 是修改一个枚举值，每帧开销可忽略。每个翻滚只执行两次（Begin+End） |

**生命周期：**

```
NotifyBegin：
  → MeshComp→GetOwner()→GetAbilitySystemComponent()
  → ASC→AddLooseGameplayTag(Combat.State.Invincible)
  → ★ 将角色胶囊体对 Weapon 通道设为 Ignore（保存原始响应，NotifyEnd 恢复）
  → 可选：同时设 MonsterAttack 通道 = Ignore（若怪物攻击用独立通道）

NotifyEnd：
  → ASC→RemoveLooseGameplayTag(Combat.State.Invincible)
  → ★ 恢复胶囊体 Weapon 通道的原始碰撞响应
```

> **与 ComboWindow 的区别：**
>
> | | ComboWindow（§3.8） | DodgeWindow（§3.9） |
> |------|------|------|
> | 挂载位置 | 攻击 Montage | 翻滚 Montage |
> | 作用 | 允许接下一招连招输入 | 无敌帧——怪物攻击穿身无效 |
> | 操作 | ASC 增删 Combat.State.ComboWindowOpen Tag | ASC 增删 Combat.State.Invincible Tag |
> | 策划调整 | 拖拽区间即可 | 拖拽区间即可 |


### 3.9b UAnimNotifyState_DodgeAcceptWindow — 翻滚接受窗口通知

```
UCLASS(BlueprintType, Blueprintable)
class UAnimNotifyState_DodgeAcceptWindow : public UAnimNotifyState
```

挂载在**攻击 Montage** 中，标记"允许翻滚"的精确帧区间。与 `DodgeWindow`（§3.9，翻滚动画自身的无敌帧）不同——这是攻击侧的接受窗口。**不操作协调器，直接操作 ASC Tag**——与 GAS 的 `ActivationRequiredTags` 机制天然契合。

**生命周期：**

```
NotifyBegin：
  → MeshComp→GetOwner()→GetAbilitySystemComponent()
  → ASC→AddLooseGameplayTag(Combat.State.DodgeAcceptOpen)

NotifyEnd：
  → ASC→RemoveLooseGameplayTag(Combat.State.DodgeAcceptOpen)
```

> **与 ComboWindow 的区间对比：**
>
> | | ComboWindow（§3.8） | DodgeAcceptWindow（§3.9b） |
> |------|------|------|
> | 挂载位置 | 攻击 Montage | 攻击 Montage |
> | 作用 | 接受连招输入 | 接受翻滚输入 |
> | 典型区间 | 0.1–0.5s | 0.1–0.7s（延伸到收招帧） |
> | 策划调整 | 拖拽区间即可 | 拖拽区间即可 |
> | 不进窗口时 | 输入被忽略 | 输入由 GAS ActivationBlockedTags 自动阻塞 |
> | 暴力禁止 | 不出现在 Montage 中 | 不出现在 Montage 中 |

> **与 DodgeWindow 的命名区分：**
>
> | | DodgeAcceptWindow（§3.9b） | DodgeWindow（§3.9） |
> |------|------|------|
> | 所在 Montage | **攻击** Montage | **翻滚** Montage |
> | 控制什么 | 攻击中何时允许翻滚 | 翻滚中何时无敌 |
> | 通知对象 | ASC（增删 `Combat.State.DodgeAcceptOpen` Tag） | ASC（增删 `Combat.State.Invincible` Tag） |


### 3.10 特效 / 音效 / 镜头——三层分工，无独立系统

**原则：不设独立的"特效系统"或"音效系统"类。VFX/SFX/镜头由 Montage AnimNotify、GAS GameplayCue、Ability CameraModifier 三层覆盖。**

| 层 | 机制 | 适用场景 | 特点 |
|----|------|----------|------|
| 帧级同步 | Montage AnimNotify / AnimNotifyState | 武器拖尾开关、脚步声、武器挥空音效（咻~） | 动画师在 Montage 时间轴拖拽，与动画帧精确同步。无论是否命中都会触发 |
| 状态驱动 | GAS GameplayCue（`ASC→ExecuteGameplayCue`） | 命中火花 VFX + **命中碰撞音效**（按武器类型/物理材质/元素/暴击选不同音效）、伤害数字、怪物咆哮、Buff 光环 | 与 GameplayTag 绑定，目标侧异步执行。可读 HitResult 物理材质 + 攻击方 GameplayTag 选择对应音效，可跨 Ability 复用 |
| 镜头 | Ability 内 `UCameraModifier` 或 `UGameplayCamerasSubsystem` | 震屏、FOV 变化、瞄准拉近、锁定追踪、受击震屏 | 需要 GAS 状态上下文——是否蓄力中、是否瞄准、伤害等级决定震屏幅度 |

**调用链示例（太刀纵斩命中）：**

```
GA_Slash_01::Montage
  ├── AnimNotify_SlashWhoosh     → 挥空音效（帧同步，无论命中）
  ├── AnimNotifyState_WeaponTrail → 刀光拖尾（帧同步）
  └── AttackCollision::Sweep → 命中 → ApplyDamage
        └── GE Spec Apply 到目标
              ├── 目标 ExecCalc 计算伤害
              └── GameplayCue.Hit.Slash 触发
                    ├── 命中火花 VFX（GameplayCue, 目标位置）
                    ├── **命中音效**（GameplayCue, 按物理材质选: 金属/肉/鳞）
                    └── 伤害数字 UI（GameplayCue, 世界空间 Widget）
```

> **CameraModifier 管理：** 镜头效果不放在 AnimNotify 中——震屏幅度依赖伤害等级、FOV 依赖蓄力阶段、瞄准偏移依赖锁定目标。这些全部在 Ability 中通过 `UCameraModifier` 或 `PlayerCameraManager→StartCameraShake` 直接调用，Ability 结束后自动清理。


## 四、存储系统

**设计原则：** 背包有限格（默认 30）、不可分类、堆叠上限跟随物品定义。仓库无限格、统一 99999 堆叠、可分类浏览（GameplayTag 标签页）。一键整理 = 堆叠合并 + 三级排序（品类→稀有度降序→名称升序）。

### 4.1 FStorageSlot — 存储槽位

```
USTRUCT(BlueprintType)
```

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| ItemInstance | TObjectPtr\<UMHGZItemInstance\> | nullptr | 槽位中的物品实例 |
| SlotIndex | int32 | -1 | 槽位序号 |

方法：`bool IsEmpty() const`、`void Clear()`。

### 4.2 UMHGZBackpackComponent — 背包组件

```
UCLASS(ClassGroup=(Inventory), BlueprintType)
class UMHGZBackpackComponent : public UActorComponent
```

挂载到 Character。

| 成员 | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| MaxSlotCount | int32 | "Backpack\|Config" | 30 | 最大槽位数 |
| Slots | TArray\<FStorageSlot\> | "Backpack\|State" | 预分配 30 空槽 | 槽位数组 |

方法：

- `int32 AddItem(UMHGZItemDefinition* Def, int32 Count)`
  - 输入：物品定义、数量。
  - 输出：**实际放入的数量**（支持部分成功：若背包只能容纳 3 个但请求 5 个，返回 3，剩余的 2 个由调用方处理——如留在地上或提示背包已满）。
  - 作用：优先堆叠已有同类物品，满堆叠后占用新空槽，直到 Count 全部放入或无空位。
- `int32 AddItemInstance(UMHGZItemInstance* Instance, int32 Count)`
  - 输入：已有物品实例、数量。
  - 输出：**实际放入的数量**（同上，支持部分成功）。
  - 作用：同上，使用已存在的 ItemInstance 而非新建。
- `int32 RemoveItem(UMHGZItemInstance* Instance, int32 Count)`
  - 输入：实例、数量。
  - 输出：实际移除量（可能小于 Count，若实例数量不足）。
- `int32 RemoveItemAtSlot(int32 SlotIndex, int32 Count)`
  - 输入：槽位索引、数量。
  - 输出：实际移除量。
- `UMHGZItemInstance* GetItemAtSlot(int32 SlotIndex)`
  - 输入：槽位索引。
  - 输出：物品实例指针，空槽返回 nullptr。
- `int32 FindFirstEmptySlot()`
  - 输出：首个空槽索引，-1 表示无空槽。
- `int32 FindSlotForItem(UMHGZItemInstance* Instance)`
  - 输入：物品实例。
  - 输出：所在槽位索引，-1 表示未找到。
- `int32 GetAvailableSpaceFor(UMHGZItemDefinition* Def)`
  - 输入：物品定义。
  - 输出：背包中还能放入该物品的总数量。
- `bool CanAddItem(UMHGZItemDefinition* Def, int32 Count)`
  - 输入：物品定义、数量。
  - 输出：是否有足够空间。
- `bool MoveItem(int32 From, int32 To)`
  - 输入：源槽位、目标槽位。
  - 输出：成功与否。
  - 作用：移动或交换两槽位的物品。
- `void SwapSlots(int32 A, int32 B)`
  - 输入：槽位 A、槽位 B。
  - 作用：直接交换两槽位内容。
- `void AutoSort()`
  - 作用：一键整理。先合并同类堆叠（上限 `BackpackMaxStack`），再按三级排序键排列（品类→RarityLevel 降序→名称升序），空格置尾。
- `int32 GetItemCount()`
  - 输出：物品种类数（非空槽数），不含 Quantity 聚合。
- `int32 GetUsedSlotCount()`
  - 输出：已占用的槽位数。
- `TArray<UMHGZItemInstance*> GetAllUsableItems()`
  - 输出：所有 `bIsUsable==true` 的物品实例列表。
  - 作用：供快捷栏 `RefreshFromBackpack` 自动登记。

Delegate：`FOnBackpackChanged` — 增/删/移/整理后广播。

### 4.3 UMHGWarehouseComponent — 仓库组件

```
UCLASS(ClassGroup=(Inventory), BlueprintType)
class UMHGWarehouseComponent : public UActorComponent
```

挂载到 PlayerState，跨关卡保留。常量 `WAREHOUSE_MAX_STACK = 99999`。可混合存储 `ItemInstance`（消耗品/材料）和 `EquipmentInstance`（武器/衣服/饰品），通过多态统一管理。

**仓库 UI 双视图：**

| | 物品视图 | 装备视图 |
|:--|:--|:--|
| 显示对象 | 仅 Status==InStorage 的物品（ItemInstance + EquipmentInstance） | 所有 EquipmentInstance（不管 Status） |
| 显示方式 | 按 Definition 合并堆叠（两个同款空闲饰品显示为 ×2） | 逐个独立展示（每个装备一行） |
| 状态标记 | 无 | 读 `Status`：`Equipped`→"已装备"，`Socketed`→"已镶嵌"，`InStorage`→无标记 |
| 用途 | 快速查看可支配库存 | 管理装备个体的镶嵌/客制化/装备状态 |

| 成员 | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| Slots | TArray\<FStorageSlot\> | "Warehouse\|State" | 空 | 槽位数组（动态增长） |

方法：

- `bool DepositItem(UMHGZItemInstance* Instance, int32 Count)`
  - 输入：物品实例、数量。
  - 输出：成功与否。
  - 作用：从背包存入仓库，按 99999 合并堆叠。若 `Instance→IsEmpty()` 则从背包移除该实例。
- `bool WithdrawItem(UMHGZItemInstance* Instance, int32 Count)`
  - 输入：物品实例、数量。
  - 输出：成功与否。
  - 作用：从仓库取出到背包，检查背包容量和堆叠限制。不满足则拒绝。
- `int32 DepositAll(TArray<UMHGZItemInstance*> Instances)`
  - 输入：物品实例列表。
  - 输出：成功存入的实例数量。
- `UMHGZItemInstance* GetItemAtSlot(int32 SlotIndex)`
  - 输入：槽位索引。
  - 输出：物品实例指针。
- `TArray<UMHGZItemInstance*> GetItemsByTag(FGameplayTag Tag)`
  - 输入：GameplayTag。
  - 输出：匹配标签的物品实例列表。
  - 作用：仓库分类页的核心查询方法。利用 UE 的 Tag 层级匹配（查 `Item.Type.Weapon` 自动包含 `.Sword/.Bow` 等子标签）。
- `TArray<UMHGZItemInstance*> GetItemsByTagQuery(FGameplayTagQuery Query)`
  - 输入：标签查询表达式。
  - 输出：匹配的物品实例列表。
  - 作用：支持 AND/OR 组合条件的高级筛选。
- `TArray<UMHGZItemInstance*> SearchByName(FString Keyword)`
  - 输入：关键词。
  - 输出：名称模糊匹配的物品实例列表。
- `void AutoSort(bool bGlobal = true)`
  - 输入：bGlobal=true 全仓库排序，false 仅当前分类页内排序。
  - 作用：堆叠合并（上限 99999）→ 三级排序（品类→RarityLevel 降序→名称升序）→ 空格置尾。

Delegate：`FOnWarehouseChanged`。

---

## 五、使用系统

**设计原则：** 与物品系统完全解耦。快捷栏自动登记背包中所有 `bIsUsable` 物品 + 少量手动分配的特殊动作。交互模式：切换键（滚轮/Q/E）循环选中 → 触发键（鼠标左键/F）执行当前选中项。UseAction 和 SpecialAction 均通过 GAS Ability 触发。常规动作（攻击/闪避）由 GAS 直接绑定输入，不经过快捷栏。

### 5.1 EQuickBarSlotType — 快捷栏槽位类型

| 值 | 说明 |
|----|------|
| Empty | 空槽 |
| Item | 可使用物品 |
| SpecialAction | 非常规动作 |

### 5.2 FQuickBarSlot — 快捷栏槽位

```
USTRUCT(BlueprintType)
```

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| SlotType | EQuickBarSlotType | Empty | 槽位类型 |
| ItemInstance | TObjectPtr\<UMHGZItemInstance\> | nullptr | 物品引用（SlotType==Item 时有效） |
| SpecialActionDef | TObjectPtr\<UMHGZSpecialAction\> | nullptr | 动作引用（SlotType==SpecialAction 时有效） |
| CachedIcon | TSoftObjectPtr\<UTexture2D\> | nullptr | 缓存图标 |
| CachedName | FText | 空 | 缓存名称 |

方法：`bool IsEmpty() const`、`void Clear()`、`UTexture2D* GetDisplayIcon()`、`FText GetDisplayName()`、`int32 GetQuantity()`（仅 Item 类型有效，其他返回 -1）。

### 5.3 UMHGZUseAction — 使用行为

```
UCLASS(BlueprintType, Blueprintable, Abstract, EditInlineNew)
class UMHGZUseAction : public UPrimaryDataAsset
```

| 成员 | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| ActionName | FText | "Action" | 空 | 行为名称 |
| ActionIcon | TSoftObjectPtr\<UTexture2D\> | "Action" | nullptr | 图标 |
| CooldownOverride | float | "Action" | -1 | 冷却覆盖（-1=使用物品定义值） |
| AbilityClass | TSubclassOf\<UGameplayAbility\> | "Action\|GAS" | nullptr | GAS Ability 类（留空待实现） |

方法：

- `bool Execute(AActor* User, UMHGZItemInstance* SourceItem)`
  - 输入：使用者 Actor、来源物品实例。
  - 输出：成功与否。
  - 作用：通过 `User` 的 `ASC→TryActivateAbility(AbilityClass)` 触发 GAS Ability。
- `bool CanExecute(AActor* User, UMHGZItemInstance* SourceItem) const`
  - 输入：使用者、来源物品实例。
  - 输出：是否满足执行条件。
  - 作用：检查冷却时间等前置条件。

预置子类：`UUseAction_Heal`（HealAmount, bPercentHeal）、`UUseAction_ThrowProjectile`（ProjectileClass, ThrowSpeed）、`UUseAction_ApplyBuff`（BuffGE, Duration）、`UUseAction_PlaceTrap`（TrapClass）。

### 5.4 UMHGZSpecialAction — 非常规动作

```
UCLASS(BlueprintType, Blueprintable, Abstract, EditInlineNew)
class UMHGZSpecialAction : public UPrimaryDataAsset
```

不属于常规战斗/移动系统、需玩家主动触发的动作（探测、钩爪、拍照、演奏等）。

| 成员 | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| ActionName | FText | "Action" | 空 | 动作名称 |
| ActionIcon | TSoftObjectPtr\<UTexture2D\> | "Action" | nullptr | 图标 |
| AbilityClass | TSubclassOf\<UGameplayAbility\> | "Action\|GAS" | nullptr | GAS Ability 类 |

方法：

- `bool Execute(AActor* User)`
  - 输入：使用者 Actor。
  - 输出：成功与否。
  - 作用：通过 `ASC→TryActivateAbility(AbilityClass)` 触发 GAS Ability。
- `bool CanExecute(AActor* User) const`
  - 输入：使用者 Actor。
  - 输出：是否满足执行条件。

预置子类：`USpecialAction_Scan`、`USpecialAction_GrapplingHook`、`USpecialAction_PhotoMode`、`USpecialAction_PlayInstrument`。

### 5.5 UMHGZQuickBarComponent — 快捷栏组件

```
UCLASS(ClassGroup=(UseSystem), BlueprintType)
class UMHGZQuickBarComponent : public UActorComponent
```

挂载到 PlayerController。

| 成员 | 类型 | Category | 默认值 | 说明 |
|------|------|----------|--------|------|
| MaxSlotCount | int32 | "QuickBar\|Config" | 8 | 最大槽位数 |
| Slots | TArray\<FQuickBarSlot\> | "QuickBar\|State" | 预分配 8 空槽 | 槽位数组 |
| SelectedIndex | int32 | "QuickBar\|State" | 0 | 当前选中槽位索引 |

方法：

- `void RefreshFromBackpack(UMHGZBackpackComponent* Backpack)`
  - 输入：背包组件。
  - 作用：清空快捷栏物品槽位，遍历背包 `GetAllUsableItems()` 重新填入。特殊动作槽位不受影响。
- `void SelectNext()`
  - 作用：选中下一个非空槽位，到末尾循环回开头。
- `void SelectPrevious()`
  - 作用：选中上一个非空槽位，到开头循环回末尾。
- `void SelectSlot(int32 Index)`
  - 输入：槽位索引。
  - 作用：直接选中指定槽位。
- `bool UseSelected()`
  - 输出：成功与否。
  - 作用：触发当前选中槽位。
  - 设计思路：
    - SlotType==Item → 检查 `Quantity>0` → 实例化 `UseAction` → `Execute` → 若 `bConsumeOnUse` 则 `ItemInstance→RemoveQuantity(1)` → 若 `ItemInstance→IsEmpty()` 则清空该槽位并从背包移除该实例。**不调用 `RefreshFromBackpack`**——单槽增量更新，避免因一次使用重建整个快捷栏。仅当背包物品增删（拾取/丢弃）时才调用 `RefreshFromBackpack` 全量刷新。
    - SlotType==SpecialAction → 实例化 `SpecialAction` → `Execute`。
- `bool AssignSpecialAction(int32 SlotIndex, UMHGZSpecialAction* Action)`
  - 输入：槽位索引、特殊动作定义。
  - 输出：成功与否。
  - 作用：手动将特殊动作分配到指定槽位。
- `void RemoveSpecialAction(int32 SlotIndex)`
  - 输入：槽位索引。
  - 作用：移除指定槽位的特殊动作。
- `FQuickBarSlot GetSelectedSlot()`
  - 输出：当前选中槽位的完整信息。

输入绑定：`IA_QuickBar_Next`→SelectNext、`IA_QuickBar_Prev`→SelectPrevious、`IA_QuickBar_Use`→UseSelected。

Delegate：`FOnQuickBarChanged(int32 SelectedIndex)`、`FOnSlotUsed(int32 SlotIndex, bool bSuccess)`。

---

## 六、词条系统

**设计原则：** 目录集中定义 + 装备仅存 ID 引用。DT_EntryCatalog（DataTable）登记所有词条的完整信息（名称、描述、最大等级、效果类型、修饰器或 GE 类）。装备/饰品只存 `{EntryID, EntryLevel}`。80% 词条为 SimpleStat——通过 CurverTable 曲线 + 通用 GE_EntryStat + UExecCalc_EntryStat 参数化处理，无需各自创建 GE 蓝图。20% 为 Complex——各自创建自定义 GE 蓝图。所有 SimpleStat 词条的等级→数值曲线集中在 CT_EntryMagnitudes 一个 CurveTable。

### 6.1 DT_EntryCatalog — 词条目录

DataTable，RowStruct = `FEntryDefinition`（见 1.4 节）。

示例行：

| EntryID | MaxLevel | EffectType | Modifiers | EffectClass |
|------|:--:|:--:|------|------|
| AttackUp | 5 | SimpleStat | [{ Attr=AttackPower, Op=Add, Curve=Curve_AttackUp }] | — |
| CritBoost | 3 | SimpleStat | [{ Attr=CriticalRate, Op=Add, Curve=Curve_CritBoost }] | — |
| AttackMaster | 5 | SimpleStat | [{ Attr=AttackPower, Op=Add, Curve=Curve_AtkMas_Atk }, { Attr=CriticalRate, Op=Add, Curve=Curve_AtkMas_Crit }] | — |
| LifeSteal | 3 | Complex | — | GE_LifeSteal |

### 6.2 CT_EntryMagnitudes — 词条数值曲线表

CurveTable，每行一条 FRichCurve，X=词条等级，Y=该等级数值。策划在编辑器中拖拽曲线节点，支持任意非线性形状。

示例行：

| 曲线名 | Lv1 | Lv2 | Lv3 | Lv4 | Lv5 |
|------|:--:|:--:|:--:|:--:|:--:|
| Curve_AttackUp | 3 | 6 | 9 | 12 | 15 |
| Curve_CritBoost | 20 | 50 | 100 | — | — |
| Curve_AtkMas_Atk | 3 | 6 | 9 | 12 | 15 |
| Curve_AtkMas_Crit | 0 | 0 | 0 | 5 | 10 |

> Curve_CritBoost：非均匀——Lv1=20%, Lv2=50%, Lv3=100%。Curve_AtkMas_Crit：Lv4 才出现会心加成——多属性+突变。

### 6.3 UMHGZDataManager — 全局数据管理器

```
UCLASS()
class UMHGZDataManager : public UGameInstanceSubsystem
```

GameInstanceSubsystem，全局单例。统一持有所有全局 DataTable/CurveTable 的引用，解决 FEntryReference（纯数据结构）和 UExecCalc_EntryStat（无实例状态）无法自行访问资产的问题。

| 成员 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| DT_EntryCatalog | TSoftObjectPtr\<UDataTable\> | nullptr | 词条目录（RowStruct=FEntryDefinition，RowName=EntryID） |
| CT_EntryMagnitudes | TSoftObjectPtr\<UCurveTable\> | nullptr | 词条数值曲线（X=等级, Y=数值） |
| DT_AbilityScalars | TSoftObjectPtr\<UCurveTable\> | nullptr | Ability FScalableFloat 全局曲线表 |
| DT_WeaponResourceConfig | TSoftObjectPtr\<UDataTable\> | nullptr | 武器种类→资源参数 |
| DT_WeaponComboConfig | TSoftObjectPtr\<UDataTable\> | nullptr | 武器种类→连招表桥接 |
| DT_WeaponDodgeConfig | TSoftObjectPtr\<UDataTable\> | nullptr | 武器种类→翻滚参数 |

方法：

- `virtual void Initialize(FSubsystemCollectionBase& Collection) override`
  - 作用：异步加载所有 SoftObjectPtr 资产。
- `bool FindEntryDefinition(FName EntryID, FEntryDefinition& OutDef) const`
  - 输入：词条 ID（对应 DT_EntryCatalog RowName）。
  - 输出：通过 OutDef 返回词条完整定义。返回值为是否查到。
  - 作用：`DT_EntryCatalog→FindRow<FEntryDefinition>(EntryID)`。
- `float EvaluateEntryMagnitude(FName CurveName, int32 Level) const`
  - 输入：曲线名、词条等级。
  - 输出：该等级对应的数值。
  - 作用：`CT_EntryMagnitudes→Eval(CurveName, Level)`。
- `FWeaponResourceConfig* FindWeaponResourceConfig(FGameplayTag WeaponTypeTag) const`
- `UMHGZWeaponComboData* FindWeaponComboData(FGameplayTag WeaponTypeTag) const`
- `FWeaponDodgeConfig* FindWeaponDodgeConfig(FGameplayTag WeaponTypeTag) const`

> **访问方式：** 任意需要这些数据的代码（ExecCalc、EquipmentComponent、GA_Dodge 等）通过 `GetWorld()->GetGameInstance()->GetSubsystem<UMHGZDataManager>()` 获取。策划只需在 DataManager 蓝图或 Ini 中配置一次资产引用，无需在多处重复设置。

---

## 七、GameplayTags 完整层级

### 7.1 物品类型
```
Item.Type.Weapon(.Sword/.Bow/.Staff/.Dagger)
Item.Type.Armor(.Helmet/.Chest/.Legs/.Gloves/.Boots)
Item.Type.Accessory
Item.Type.Consumable(.Potion/.Food/.Throwable/.Scroll)
Item.Type.Material(.Ore/.Herb/.MonsterPart)
Item.Type.Quest
```

### 7.2 稀有度（RarityLevel 自动生成）
```
Item.Rarity.r1 ~ Item.Rarity.r12
```

### 7.3 装备槽位
```
Equipment.Slot.Weapon / Armor / Accessory
```

### 7.4 角色属性
```
Attribute.Health / MaxHealth
Attribute.Stamina / MaxStamina
Attribute.StaminaRegenRate / StaminaDeductionRate / StaminaConsumptionRate
Attribute.AttackPower / Defense / CriticalRate
Attribute.StaggerMultiplier                  ← 破坏值倍率
Attribute.WeaponResource / MaxWeaponResource
Attribute.MoveSpeedMultiplier                   ← 移速倍率
```

### 7.5 GE 来源（批量移除用）
```
Effect.Source.Equipment    ← 所有装备 GE 打此标签
```

### 7.5b GameplayCue 标签（特效/音效触发）
```
GameplayCue.Hit.Slash      ← 斩击命中火花
GameplayCue.Hit.Blunt      ← 打击命中火花
GameplayCue.Hit.Fire       ← 火属性命中特效
GameplayCue.Hit.Crit       ← 暴击命中特效
GameplayCue.Monster.Roar   ← 怪物咆哮
GameplayCue.Buff.Applied   ← Buff 叠加特效
GameplayCue.Weapon.Trail   ← 武器拖尾（不走 GameplayCue 亦可——AnimNotifyState 更精确）
```

### 7.6 战斗
```
Combat.Event.HitStagger    ← 受击硬直事件（ExecCalc→HandleGameplayEvent 触发。替代 Tag Trigger，每次命中独立触发 GA_HitReaction）
Combat.Stagger.Light       ← 小硬直（攻击方的 HitStaggerTag，打在无霸体目标上触发小硬直）
Combat.Stagger.Medium      ← 中硬直
Combat.Stagger.Heavy       ← 大硬直
Combat.State.Dead          ← 死亡状态
Combat.State.Invincible    ← 无敌状态（翻滚/部分攻击的无敌帧。受击 GE Execute 时检查：存在则伤害=0）
Combat.State.Hitstun       ← 受击硬直中（无法输入/移动。CanActivateAbility 检查此 Tag 阻塞新动作）
Combat.State.Knockdown     ← 击倒/击飞中
Combat.State.Grounded      ← 地面态（Character MovementComponent 管理：OnLanded → Add Grounded + Remove Aerial；OnJump/Fell → Add Aerial + Remove Grounded。两操作在同一函数调用内完成，先后顺序不跨帧。Grounded/Aerial 切换伴随跳跃/落地动作，动作持续帧内不可随意取消——受击中断时 GA_HitReaction 读取当前状态自行处理）
Combat.State.Aerial        ← 空中态（与 Grounded 互斥）
Combat.State.Sheathed      ← 拔刀前/纳刀态（GA_Sheathe/GA_Unsheathe 管理。非战斗默认持有）
Combat.State.Unsheathed    ← 拔刀后/持刀态（拔刀动作后持有，纳刀后移除。与 Sheathed 互斥）
Combat.State.Aiming        ← 瞄准态（LT Hold → GA_Aim 激活时持有，松开/被打断时移除。UI 监听此 Tag 切换准星/HUD）
Combat.State.ComboWindowOpen ← 连招输入窗口打开中（ComboWindow→NotifyBegin 添加，NotifyEnd 移除。协调器条件 D 检查此 Tag）
Combat.State.Attacking     ← 攻击中（攻击 GA 的 ActivateAbility 添加，EndAbility 移除。GA_Dodge 的 CanActivateAbility 检查此 Tag）
Combat.State.DodgeAcceptOpen ← 翻滚接受窗口打开中（DodgeAcceptWindow→NotifyBegin 添加，NotifyEnd 移除。GA_Dodge 的 CanActivateAbility 检查此 Tag）
Combat.State.Charging      ← 蓄力中（GA_ChargeSlash 激活时持有，松开/EndAbility 移除。翻滚/移动可据此限制操作）
Combat.Poise.Light         ← 轻霸体（如片手剑部分招式：无视 Combat.Stagger.Light 硬直）
Combat.Poise.Medium        ← 中霸体（如大剑蓄力：无视 Light+Medium 硬直）
Combat.Poise.Heavy         ← 重霸体（如太刀气刃斩：无视 Light+Medium+Heavy 硬直）
Combat.Poise.Super         ← 超霸体（如龙击炮/铁山靠：无视全部硬直+击飞）
Combat.Branch.SpiritBlade  ← 太刀气刃分支标记
Combat.Branch.PostRoundslash ← 太刀大回旋后标记（BlockedTags 排除登龙派生）
Combat.Branch.Heavy        ← 重击分支标记
Combat.Branch.Special      ← 特殊攻击分支标记
Combat.Branch.Aerial       ← 空中连段分支标记

Hitzone.Head               ← 怪物部位标签（头部）
Hitzone.Neck               ← 颈部
Hitzone.Back               ← 背部
Hitzone.Torso              ← 躯干
Hitzone.Tail               ← 尾部
Hitzone.TailTip            ← 尾尖（弱点）
Hitzone.LeftWing           ← 左翼
Hitzone.RightWing          ← 右翼
Hitzone.LeftClaw           ← 左爪
Hitzone.RightClaw          ← 右爪
Hitzone.LeftLeg            ← 左腿
Hitzone.RightLeg           ← 右腿

Hitzone子属性（每个部位碰撞体持有）：
  DefenseMultiplier (float)  ← 肉质（伤害吸收率，0.2=坚硬/1.0=弱点）
  StaggerRate (float)        ← 硬直肉质（破坏值吸收率）
```
> **碰撞通道说明：** 项目需在 `DefaultEngine.ini` 中定义两个自定义碰撞通道——`Weapon`（玩家攻击检测）和 `MonsterAttack`（怪物攻击检测）。Pawn 通道为 UE 默认。无敌帧期间仅切换 Weapon/MonsterAttack 通道响应，Pawn 始终 Block。部位标签挂载在怪物骨骼的 `UPrimitiveComponent` 上，攻击 Sweep 按 `HitzoneQueryTag` 过滤。

### 7.7 输入
```
Input.Weapon               ← 武器输入父标签（子标签自动匹配此标签）
Input.Weapon.Y             ← △/Y — 轻/基础攻击
Input.Weapon.B             ← ○/B — 重/特殊攻击
Input.Weapon.RT            ← RT/R2 — 武器技能/防御
Input.Weapon.RTA           ← RT+△（Chord Trigger）
Input.Weapon.RTB           ← RT+○（Chord Trigger）
Input.Weapon.RTY           ← RT+Y（Chord Trigger）
Input.Weapon.YB            ← Y+B / △+○（Chord Trigger）— 超必/收尾技
Input.Modifier.Aiming      ← LT/L2 长按进入瞄准态
Input.Modifier.Charging    ← RT 长按蓄力中
Input.Modifier.Sheathed    ← RB/R1 按下进入纳刀态（与 Combat.State.Sheathed 同步）
Input.Sprint               ← LS/L3 — 奔跑（按下=跑，松开=走。通过 GA_Sprint 管理）
Input.Dodge                ← A/× — 闪避
Input.EdgeVault            ← 边缘跳越（由 UMHGZEdgeVaultComponent 自动触发，不绑定玩家按键）
Input.Interact             ← 交互
```

### 7.8 仓库分类标签页

| 标签页 | TagQuery | | 标签页 | TagQuery |
|--------|----------|-|--------|----------|
| 全部 | 无 | | 消耗品 | `Item.Type.Consumable` |
| 武器 | `Item.Type.Weapon` | | 材料 | `Item.Type.Material` |
| 衣服 | `Item.Type.Armor` | | 任务 | `Item.Type.Quest` |
| 饰品 | `Item.Type.Accessory` |

---

## 八、一键整理排序表

| 优先级 | 品类 | 匹配标签 | 二级键 | 三级键 |
|:--:|------|------|:--:|------|
| 0 | 武器 | `Item.Type.Weapon` | RarityLevel 降序 | Name 升序 |
| 1 | 衣服 | `Item.Type.Armor` | RarityLevel 降序 | Name 升序 |
| 2 | 饰品 | `Item.Type.Accessory` | RarityLevel 降序 | Name 升序 |
| 3 | 可使用 | `Item.Type.Consumable` | RarityLevel 降序 | Name 升序 |
| 4 | 不可使用 | `Item.Type.Material` | RarityLevel 降序 | Name 升序 |
| 5 | 任务 | `Item.Type.Quest` | RarityLevel 降序 | Name 升序 |

---

## 九、目录结构

```
Source/MHGZ/
├── Inventory/
│   ├── MHGZItemTypes.h                   (FEquipmentSocket, FEntryReference, FEntryModifier, FEntryDefinition)
│   ├── MHGZItemDefinition.h/cpp
│   ├── MHGZEquipmentDefinition.h/cpp
│   ├── MHGZWeaponDefinition.h/cpp
│   ├── MHGZArmorDefinition.h/cpp
│   ├── MHGZAccessoryDefinition.h/cpp
│   ├── MHGZConsumableDefinition.h/cpp
│   ├── MHGZItemInstance.h/cpp
│   └── MHGZEquipmentInstance.h/cpp
│
├── AttributeSystem/
│   ├── MHGZAttributeSet.h/cpp
│   ├── MHGZEquipmentComponent.h/cpp
│   ├── MHGZDataManager.h/cpp           (GameInstanceSubsystem, 集中持有全局DataTable/CurveTable)
│   ├── MHGZWeaponResourceComponent.h/cpp (武器资源基类: GetCurrentValue/Consume/Restore)
│   ├── Res_LongSword.h/cpp             (太刀: 气刃槽色阶+衰减)
│   ├── Res_InsectGlaive.h/cpp          (虫棍: 三灯Timer+猎虫耐力)
│   ├── Res_ChargeBlade.h/cpp           (盾斧: 瓶计数+红盾Timer)
│   ├── Res_SwitchAxe.h/cpp             (斩斧: 充能槽)
│   ├── DT_WeaponResourceConfig.uasset
│   ├── DT_WeaponComboConfig.uasset       (武器种类→连招表桥接，一行映射)
│   ├── DT_WeaponDodgeConfig.uasset       (武器种类→翻滚参数)
│   ├── CT_EntryMagnitudes.uasset
│   ├── GE_EntryStat.uasset
│   └── UExecCalc_EntryStat.h/cpp
│
├── ActionSystem/
│   ├── MHGZAbilitySystemComponent.h/cpp
│   ├── MHGZGameplayAbility.h/cpp
│   ├── MHGZAttackAbility.h/cpp           (碰撞检测+伤害GE基类)
│   ├── MHGZLongSwordAbility.h/cpp       (太刀基类: 气刃槽判定+招内派生)
│   ├── MHGZInsectGlaiveAbility.h/cpp    (虫棍基类: 三灯+猎虫耐力)
│   ├── MHGZChargeBladeAbility.h/cpp     (盾斧基类: 瓶计数+盾充能)
│   ├── MHGZSwitchAxeAbility.h/cpp       (斩斧基类: 充能槽)
│   ├── MHGZDodgeAbility.h/cpp            (翻滚/闪避 Ability)
│   ├── MHGZWeaponComboData.h/cpp         (连招表 DataAsset + FComboNode)
│   ├── MHGZComboCoordinatorAbility.h/cpp (连招协调器)
│   ├── AnimNotifyState_AttackCollision.h/cpp
│   ├── AnimNotifyState_MonsterAttackCollision.h/cpp (怪物攻击碰撞通知——部位胶囊体复用的通道切换)
│   ├── AnimNotifyState_ComboWindow.h/cpp (连招窗口通知)
│   ├── AnimNotifyState_DodgeWindow.h/cpp (翻滚无敌帧通知)
│   ├── MHGZInputComponent.h/cpp
│   ├── MHGZEdgeVaultComponent.h/cpp   (边缘跳越检测组件)
│   └── MHGZEdgeVaultAbility.h/cpp     (边缘跳越 Ability)
│
├── Storage/
│   ├── MHGZStorageSlot.h
│   ├── MHGZBackpackComponent.h/cpp
│   └── MHGZWarehouseComponent.h/cpp
│
├── UseSystem/
│   ├── MHGZQuickBarSlot.h                (FQuickBarSlot, EQuickBarSlotType)
│   ├── MHGZUseAction.h/cpp
│   ├── MHGZSpecialAction.h/cpp
│   ├── UseAction_Heal.h/cpp
│   ├── UseAction_ThrowProjectile.h/cpp
│   ├── UseAction_ApplyBuff.h/cpp
│   ├── UseAction_PlaceTrap.h/cpp
│   ├── SpecialAction_Scan.h/cpp
│   ├── SpecialAction_GrapplingHook.h/cpp
│   ├── SpecialAction_PhotoMode.h/cpp
│   ├── SpecialAction_PlayInstrument.h/cpp
│   └── MHGZQuickBarComponent.h/cpp
│
Content/
├── Inventory/Definitions/
│   ├── Weapons/          (WeaponDefinition .uasset)
│   ├── Armors/           (ArmorDefinition .uasset)
│   ├── Accessories/      (AccessoryDefinition .uasset)
│   └── Consumables/      (ConsumableDefinition .uasset)
├── Inventory/
│   └── DT_EntryCatalog.uasset
├── Storage/UI/
│   ├── WBP_BackpackPanel.uasset
│   ├── WBP_WarehousePanel.uasset
│   └── WBP_StorageSlot.uasset
└── UseSystem/
    ├── Actions/           (UseAction / SpecialAction .uasset)
    └── UI/
        └── WBP_QuickBar.uasset
```

---

## 十、设计决策汇总

| # | 决策 | 理由 |
|---|------|------|
| 1 | 五系统解耦 | 物品/属性/存储/使用/词条各自独立迭代 |
| 2 | bIsUsable 用成员 bool | 决定代码路径，高频判断，比 Tag 快 |
| 3 | 物品分类用 GameplayTag | 纯分类扩展，策划随时加子类型 |
| 4 | 装备槽位用类继承+Tag 双重标识 | 子类决定字段（编译器安全），Tag 决定运行时槽位匹配 |
| 5 | RarityLevel 用 int32 | 数值排序，自动生成 Tag 供筛选 |
| 6 | 镶嵌等级制（1-4） | 数值比较，简单可靠 |
| 7 | 背包各自堆叠 vs 仓库 99999 | 策略 vs 便利 |
| 8 | 背包不可分类 vs 仓库可分类 | 快操 vs 检索 |
| 9 | 快捷栏切换选中→触发 | 适配手柄，减少按键占用 |
| 10 | UseAction/SpecialAction 通过 GAS Ability | 与动作系统统一 |
| 11 | 词条目录 + SimpleStat/Complex 分流 | 集中管理 + 80% 词条无需各自 GE 蓝图 |
| 12 | 装备 GE 打 Tag 批量移除 | RemoveActiveEffectsWithTags 一行清空，不存 Handle |
| 13 | **EquipmentInstance::Status 替代 O(n) 查询** | `EInstanceStatus{InStorage,Equipped,Socketed}` 字段由 EquipmentComponent 维护，UI 直接读 O(1)；不再需要遍历 EquippedItems 查状态 |
| 14 | 全量重算非增量 | 不维护中间状态，正确性保证。装备变更仅在非战斗期触发（换装/镶嵌/拆除），频率极低；单次 ~20-30 个 GE 的 Remove+Apply 耗时 < 0.5ms，对玩家无感知 |
| 15 | CurveTable 驱动词条数值 | 支持非线性、多属性、跨级突变 |
| 16 | 武器专属资源由种类决定 | WeaponTypeTag→DataTable，同种类共享 |
| 16b | **武器连招表由种类决定** | WeaponTypeTag→DT_WeaponComboConfig→WeaponComboData（§3.6）；连招协调器（§3.7）在装备时授予、卸下时移除 |
| 17 | 角色属性与装备完全解耦 | 装备字段→GE→ASC，中间无直接引用 |
| 18 | PrimaryDataAsset 做所有定义 | 策划编辑友好，异步加载 |
| 19 | Entries 放在 EquipmentDefinition | 词条是装备专属概念 |
| 20 | **客制化存于 EquipmentInstance** | 客制化跟装备本身；EquipmentInstance 持有 FItemCustomization，卸下再装上不丢失 |
| 21 | **GameplayTag 桥接输入与 Ability** | EnhancedInput→InputAction→Tag→ASC.TryActivateByTag；不硬编码按键，策划可随时改绑定 |
| 22 | **Ability 基类统一处理耐力/冷却/资源** | UMHGZGameplayAbility 覆写 CanActivate/Activate/EndAbility；蓝图子类只需实现具体动作逻辑 |
| 23 | **攻击 Ability 统一继承 UMHGZAttackAbility** | 碰撞检测（AnimNotifyState 驱动）、命中过滤（防重复+防友伤）、伤害 GE 构造（SetByCaller+AttackPowerMultiplier）全部由中间层处理；新增武器只需蓝图配置 CollisionConfigs+DamageConfig，0 行代码 |
| 24 | **连招系统采用出招表（FComboNode 有向图），辅以 RequiredTags/GrantedTags 处理动态分支** | 怪猎武器连招是精心编排的固定有向图（允许环），需策划在一处可视化完整连招树；纯 Tag 方案将连招拓扑分散到各 GA，维护成本高且易出错。Tag 仅用于动态分支条件（Buff 触发终结技、气刃标记锁连段），不承载连招拓扑 |
| 25 | **回 Idle 主驱动 = Montage 完成，ComboTimeout 仅唯一安全兜底** | Montage 自然播完 → GA EndAbility → 协调器 OnAttackFinished() → CurrentState="Idle"（Montage 自身长度即精确计时）。ComboTimeout(10s) 仅兜底卡死/异常长动画等极端情况。原 ComboExpiry 已合并——两者职责重叠，无需两个独立参数 |
| 26 | **FComboNode 支持 StateName 自指/前指（有向图允许环），StateName 用具体招式名而非抽象段位编号** | 适应虫棍等无收尾招的武器——DoubleSlash→△→DoubleSlash 自循环，窗口超时自然回 Idle。招式名让多路径收敛（上捞斩→△ 和 突刺→△ 均到二连斩）和派生差异（上捞斩→○=飞圆斩直达 vs 突刺→○=横扫→○=飞圆斩）在出招表中显式可见；抽象编号（Light1/Light2）无法表达这些关系 |
| 27 | **UMHGZWeaponComboData 归属动作系统（§3）而非装备系统（§2）** | 连招表定义的是"怎么做动作"而非"装备有什么属性"。§2 仅保留 DT_WeaponComboConfig 作为武器种类→连招表的查找桥接 |
| 28 | **bMatchAnyState 处理纳刀/起跳等通用招式** | 一行覆盖所有地面招式，避免在每个 StateName 下写重复行。不硬编码排除受击/击倒——由 RequiredTags 显式声明排除条件 |
| 29 | **Montage 归 GA 蓝图所有，FComboNode 不持有动画引用** | 同一 GA 在各连招段位播同一 Montage，不存在同 GA 配不同 Montage 的场景。连招表只管"哪个状态接哪个 GA"，动画细节由 GA 内部决定 |
| 30 | **首帧 Sweep 判定部位命中顺序** | `EnableCollision` 后下一 Tick 执行 `SweepMultiByChannel`，按 HitResult.Time 取首个 hitzone——解决 UE Overlap 事件不保证空间先后的问题。同怪物仅记录首个接触部位，后续 Overlap 跳过 |
| 31 | **部位信息通过 SetByCaller + DynamicTag 双通道传递给怪物侧** | `Hitzone.DefenseMultiplier` 走 SetByCaller 供伤害计算，`Hitzone.Head` 等 HitzoneTag 走 DynamicTag 供硬直/破部位逻辑。攻击侧不耦合怪物防御力——怪物侧 `UExecCalc_Damage` 按 HitzoneBoneName 自行查表 |
| 32 | **GA_WeaponComboCoordinator 为 Infinite 持续型 Ability，装备期间不结束** | 协调器需在整个装备期间保持 Active，并行于攻击 GA 运行。AnimNotifyState_ComboWindow 通过 `GetActiveAbilities()` 查找协调器——只有 Infinite Ability 才能始终在 Active 列表中 |
| 33 | **出招表为平面数组 + StateIndex 索引，非树状嵌套** | `ComboEntries` 是平面行集合，`NextState` 是字符串键非指针。协调器在 Activate 时构建 `TMap<FName, TArray<int32>>` 索引，匹配时 O(1) 查候选行 → 遍历匹配条件 |
| 34 | **地面/空中/拔刀态由 GameplayTag 显式管理，协调器只读 ASC Tag 状态** | `Grounded/Aerial` 由 MovementComponent 管理，`Sheathed/Unsheathed` 由 GA_Sheathe/GA_Unsheathe 管理。出招表每行通过 RequiredTags 声明状态条件。协调器不主动判断状态——只做 Tag 匹配过滤。被击飞时 ASC 自动换状态 → 不满足条件的行自动不可用 |
| 35 | **所有 IA 统一绑定到 ASC 的 OnInputActionTriggered，按 AbilityTag 类别分叉路由，协调器只暴露 HandleWeaponInput(FGameplayTag)** | 单一 EnhancedInput 绑定点，Tag 做路由中介。协调器生命周期简化（无需 Bind/Unbind）；武器未装备时自然跳过；成本 ≈ 3 次 hash，无冗余搜索 |
| 36 | **UI 由 GameplayTag/Attribute 变化驱动，Ability 不直接操作 UI** | GA_Aim 激活 → ASC 持有 `Combat.State.Aiming`，HUD/准星等 UI 组件订阅 `RegisterGameplayTagEvent` 响应。Ability 只管 Tag 生命周期，不解耦到具体 UI Widget。武器/状态不同时 UI 自然不同——Tag 驱动，无需 if-else 判断 |
| 37 | **翻滚不进连招表——独立 GA_Dodge + DT_WeaponDodgeConfig 参数化** | 翻滚是取消/中断动作，非连招的一环。每武器 N×4 方向写入连招表 = 爆炸。改为一个 GA_Dodge 类，运行时按 WeaponTypeTag + Sheathed/Unsheathed 查 DataTable 选 Montage。收刀态所有武器共用一套 Montage；拔刀态每武器独立配置。无敌帧用独立 AnimNotifyState_DodgeWindow |
| 38 | **受击判断 = GE Execute 时被动检查，不设独立监听系统；霸体通过 GameplayTag + AnimNotifyState_PoiseWindow 提供** | 每次伤害 GE Apply 时在 UExecCalc_Damage 中读目标 Tag（无敌/霸体/硬直等级）做一次判断。不需要 Tick 轮询、不需要"受击监听组件"。攻击自带霸体用 AnimNotifyState 拖拽区间添加临时 Poise Tag；装备/被动霸体用持续性 GE 持有 Poise Tag |
| 39 | **VFX/SFX/镜头三层分工——不设独立系统** | 帧同步（AnimNotify 拖拽武器拖尾/脚步声）、状态驱动（GameplayCue 处理命中火花/伤害数字/咆哮，与 Tag 绑定可复用）、镜头（Ability 内 CameraModifier，震屏幅度/瞄准偏移依赖 GAS 状态上下文） |
| 40 | **InputComponent 只管 IMC 生命周期，ASC 管 IA→Tag 绑定** | 消除双方各自持有 `FAbilityInputBinding` 数组的冗余。InputComponent 不持有任何绑定数据——只负责添加/移除/切换 IMC；ASC 的 `InputBindings` 是绑定数据的唯一真相源 |
| 41 | **协调器帧级输入批处理——Chord Trigger 优先级高于单键** | UE5 EnhancedInput 中 Chord Trigger 不阻止被引用的单键 InputAction 独立触发，同一帧内 Y、B、YB 三个回调依次到达。协调器将当前帧所有武器输入收集到 `PendingInputs`，下一帧统一按 Chord > 单键优先级排序匹配，确保 Y+B 超必杀不被 Y 普通攻击抢先消耗 |
| 42 | **当前版本仅单机，暂不考虑网络复制** | 所有 GAS、Attribute、装备状态同步方案在后续版本补充。单机设计简化了 GE 的 Apply/Remove 流程（无需预测/回滚）、连招协调器状态管理（无需 RPC）、装备实例状态同步 |
| 43 | **武器专属资源不在 AttributeSet 中** | 怪猎武器资源系统极其多样（气刃槽色阶、瓶计数、蓄力等级、萃取等），无法用简单 float 统一概括。各武器自行管理资源，`DT_WeaponResourceConfig` 保留为种类→资源类型查找桥接，具体方案 TBD |
| 44 | **Grounded/Aerial 在同一函数调用内完成切换** | OnJump/Fell 中先后执行 Add Aerial + Remove Grounded（或反之），两行代码顺序执行不跨帧。Grounded/Aerial 切换伴随跳跃/落地动作，动作持续帧内不可随意取消（受击除外）——不存在"切换过程中输入被吞"的窗口。受击中断跳跃时 GA_HitReaction 的 Activate 读取当前状态自行处理 |
| 45 | **bMatchAnyState 不硬编码排除任何状态** | 是否排除受击/击倒由 RequiredTags 显式声明，不写死在协调器代码中。保证灵活性——未来"受击中反击"等招式可在 Hitstun 中激活 |
| 46 | **QuickBar 使用后增量更新，不重建** | 单次使用仅更新对应槽位（扣数量或清空），不调用 RefreshFromBackpack。仅当背包物品增删（拾取/丢弃）时才全量刷新 |
| 47 | **AddItem 返回 int32 支持部分成功** | 拾取 5 瓶药水但背包只能放 3 瓶时返回 3，调用方处理剩余的 2 瓶（留地上/提示已满） |
| 48 | **FItemCustomization 增加 ModifiedEntries** | 词条升级（AttackUp Lv2→Lv3）用 ModifiedEntries 单次操作完成，无需 Remove+Add 两步 |
| 49 | **所有 FScalableFloat 关联全局 DT_AbilityScalars** | 统一 CurveTable，Ability 只指定行名。支持未来等级缩放；当前无缩放需求时曲线设为平直常量 |
| 50 | **FComboNode 增加 Priority 字段替代 RequiredTags 数量排序** | Tag 数量多不等于匹配更精确——1 个 SpiritBlade Tag 比 3 个通用 Tag 更具体。策划显式指定 Priority（int32，越大越优先），消除隐式排序的歧义 |
| 51 | **EquipmentInstance::SetStatus 为唯一状态修改入口** | 所有组件通过 SetStatus 修改状态，保证单一真相源。避免 WarehouseComponent 和 EquipmentComponent 各自修改 Status 导致数据不一致 |
| 52 | **存/读档系统待后续实现** | InstanceID 通过 UPROPERTY 序列化保留；构造函数内仅当 Guid 无效时生成新值。完整存/读档方案后续补充 |
| 53 | **空挥断连三层机制** | (1) 连招间：`FComboNode.bRequiresHitToContinue`+`PendingGrantedTags`——GA 激活时 GrantedTags 不立即生效，由 `OnAttackHit()` 回调触发，空挥则后续节点 RequiredTags 匹配失败。(2) 招式内：`FAttackDamageConfig.bRequiresHitToContinue`——每段碰撞窗口后检查 HitTargets，空挥提前 EndAbility。(3) 命中触发效果：`OnHitSelfEffect`——首次命中时 Apply 自身 GE，`bHasHitThisActivation` 防多段重复 |
| 54 | **FComboNode.BlockedTags 反向排除** | NOR 逻辑——ASC 持有任一 BlockedTags 时节点不匹配。解决 `bMatchAnyState` 无法排除特定状态的问题：登龙剑 `BlockedTags={Combo.Branch.PostRoundslash}` → 大回旋后不可派生登龙。与 RequiredTags（AND）互补——Required 声明"需要什么"，Blocked 声明"不能有什么" |
| 55 | **ShouldContinueAfterHit / CheckWeaponResourceForAbility 虚函数钩子** | 资源门控衔接接口——GA 命中后在 `DisableCollision` 内调用 `ShouldContinueAfterHit()`，默认检查 `bRequiresHitToContinue`，子类覆写追加资源阈值判断（如登龙剑检查气刃槽 ≥ 白）。`CheckWeaponResourceForAbility` 为通用资源查询接口，各武器各自实现，当前留空 |
| 56 | **预输入缓冲——FGameplayTag 存协调器，ComboWindow 打开时刷新** | "后覆盖前"单槽缓冲：窗口关闭时 `HandleWeaponInput` 将 Tag 写入 `PreInputTag`+时间戳；`ComboWindow→NotifyBegin` 中检查是否在 `PreInputLifetime`（默认 0.15s）内 → 是则消费；超时清空。存 Tag 而非 IA——EnhancedInput 已转换为 Tag。存协调器——窗口状态、帧批处理、命中回调均在此 |
| 57 | **翻滚窗口=ASC Tag 方案（Combat.State.Attacking/DodgeAcceptOpen）** | AnimNotifyState_DodgeAcceptWindow 直接操作 ASC Tag，不耦合协调器。GA_Dodge::CanActivateAbility 纯 Tag 检查——`Attacking` 有 + `DodgeAcceptOpen` 无 → 阻塞。无武器时 `Attacking` 不存在，翻滚始终可用 |
| 58 | **FComboNode.bRequiresWindowOpen=false + DodgeAcceptOpen Tag = 统一取消窗口** | 取消动作（收虫/纳刀）设 `bRequiresWindowOpen=false` 绕过 ComboWindow，但 `RequiredTags` 含 `Combat.State.DodgeAcceptOpen`——与翻滚共用 DodgeAcceptWindow 窗口。一个 Tag 统一表达"此帧允许取消" |
| 59 | **UMHGZDataManager (GameInstanceSubsystem) 集中管理全局 DataTable/CurveTable** | 解决 FEntryReference（纯数据结构）和 UExecCalc_EntryStat（无实例状态）无法自行访问资产的问题。策划一处配置，所有系统通过 GetSubsystem 获取 |
| 60 | **MoveSpeedMultiplier → CMC.MaxWalkSpeed 用 Tick 同步** | GAS Attribute 与 CMC 属性不在同一系统。Tick 中一行代码读 Attribute 写回 CMC，每帧开销 < 0.001ms，保证永远同步。不用 PostGameplayEffectExecute——多 GE 叠加时可能漏中间状态 |
| 61 | **持刀不可奔跑用 Unsheathed Tag 阻塞 GA_Sprint** | `GA_Sprint::CanActivateAbility` 检查 `Combat.State.Unsheathed` Tag，不新增专用 Tag。收刀态可奔跑，拔刀态自动禁止——逻辑简单透明 |
| 62 | **受击硬直用 GameplayEvent 替代 Tag Trigger** | Tag Trigger 仅在 Tag 从无到有时触发——连打时第二次命中不会重新激活 GA_HitReaction。`HandleGameplayEvent(Combat.Event.HitStagger)` 每次调用独立触发，`InstancedPerExecution` 支持受击连打。`Combat.State.Hitstun` 仍保留用于 CanActivateAbility 阻塞输入 |
| 63 | **FComboNode::StaminaRequired（门槛）与 GA::StaminaCost（消耗）分离** | 前者是协调器匹配时的检查条件（"能不能放"），后者是 GA ActivateAbility 中的实际扣除（"放完扣多少"）。语义独立，策划可设 Required > Cost 保留耐力余量 |
| 64 | **持续耗耐用 Tick × DeltaTime + ApplyModToAttribute** | 奔跑/蓄力等持续型 Ability 在 OnTick 中每帧扣除 `CostRate × ConsumptionRate × Δt`，乘以 DeltaTime 保证帧率无关（30/60/120 FPS 1 秒总扣除一致）。用 ApplyModToAttribute 走 GAS Clamp 防止扣到负数。不用 GE Periodic——生命周期已绑定 GA，Tick 更简单且无需维护 GE Handle |
| 65 | **蓄力攻击在 GA 内部闭环，不进连招表路由** | 蓄力是 GA 内部多阶段状态机——按住累积 ChargeLevel、松开释放对应等级攻击。全部在一个 GA 内通过 Montage 分支 + DamageConfig 分支实现，不拆成多个 FComboNode 行。释放后的后续连招再回到协调器路由 |
| 66 | **EnhancedInput 同时绑定 Triggered + Completed** | Triggered 用于普通点按→连招匹配；Completed 用于松开→蓄力 GA 接收释放通知。在 ASC::InitializeAbilitySystem 中对每个 InputBinding 同时绑定两个事件 |
| 67 | **无敌帧 = Weapon 通道 Ignore + Invincible Tag 双层保障** | 翻滚期间角色胶囊体对 Weapon 通道设为 Ignore——攻击 Sweep 物理上穿过。Pawn 通道始终 Block——玩家不可穿过怪物身体。GAS 层 Invincible Tag 作为兜底。两层互不依赖，任一失效另一仍生效 |
| 68 | **怪物部位胶囊体复用 + MonsterAttack 通道窗口切换** | 头部/尾部/爪子等部位骨骼上始终挂胶囊体，Weapon（玩家攻击）和 Pawn（物理）通道始终 Block。仅 MonsterAttack 通道在攻击 AnimNotifyState 区间从 Ignore 切到 Block，收招自动 Ignore——不误伤 |
| 69 | **怪物攻击首帧 Sweep 防高速穿透** | 每 Tick 从各部位上一帧位置 Sweep 到当前帧，按 `FHitResult.Time` 升序取首个命中——龙车等高速攻击不会穿透玩家 |
| 70 | **Combat.State.Charging Tag 统一表达蓄力态** | 蓄力 GA 激活期间持有此 Tag。翻滚/移动/交互等系统通过检查此 Tag 自行决定是否允许对应操作——不硬编码蓄力期间的阻塞规则，保持各系统独立决策 |
| 71 | **协调器先激活再注入 ComboData** | `GA_WeaponComboCoordinator` 先 `GiveAbility + TryActivateAbility`（Infinite，空状态），然后 `EquipmentComponent→ApplyItemEffects` 中通过 DataManager 获取 ComboData → `Coordinator→SetComboData()` 构建 StateIndex。解决 Ability 激活时无法传递自定义参数的问题——`TryActivateAbilityByTag` 不支持传参，改为激活后注入 |
| 72 | **方向修正角度存在 GA 成员 `MaxCorrectionAngle`** | 每招的最大修正角度不同（太刀常规 30°/特殊纳刀 180°/见切回砍 120°），存 GA 而非 FComboNode——蓄力 GA 不进连招表也需此参数，同一 GA 被多个连招引用时不重复配置 |
| 73 | **多段招式用多个 MotionWarping AnimNotifyState 实现二次修正** | 如见切：段1后撤（180°）、段2回砍（120°）。GA 在段1 NotifyEnd 回调中读当前摇杆方向更新 Warp Target，段2 读取时已是新值。不同段的 MotionWarping `RotationTarget` 自带角度限制参数，不需要创建多个 GA |
| 74 | **FAttackSegmentConfig 统一管理碰撞+伤害+多跳** | 替换分离的 `CollisionConfigs` + `DamageConfig`——每段独立持有碰撞参数、动作值（MotionValue）、基础破坏值（BaseStaggerValue）、多跳配置。解决原来 DamageConfig 是单数、无法随段变化的问题 |
| 75 | **伤害公式 = AttackPower × MotionValue × HitzoneDefense** | 三因素相乘：角色攻击力（Attribute）、招式动作值（每段不同）、怪物部位肉质（碰撞体持有）。硬直公式 = `BaseStaggerValue × StaggerMultiplier × HitzoneStaggerRate`——新增 `StaggerMultiplier` Attribute 和 `BaseStaggerValue` 字段 |
| 76 | **单碰撞多跳伤害（MultiHitCount>1）** | 登龙剑下批等招式：一次 Sweep 命中 → 启动 MultiHitTimer → 每隔 MultiHitInterval 秒 Apply 伤害 GE（共 MultiHitCount 次）。DisableCollision 或 GA 结束时清除 Timer。每跳走完整 GE Apply 链路、共享首次命中触发的一次性逻辑 |
| 77 | **武器 Ability 基类分化——每种武器一个 C++ 中间类** | 怪猎武器资源系统极其多样（气刃槽色阶/瓶计数/三灯/充能槽），无法用统一基类概括。每种武器从 UMHGZAttackAbility 派生一个约 50 行的中间类，持有该武器的 ResourceComponent 引用、覆写 CheckWeaponResourceForAbility（资源门控）和 ShouldContinueAfterHit（招内击中派生）。FComboNode 只管激活前匹配，武器基类管激活后内部逻辑 |
| 78 | **武器资源子系统——不统一但提供杠杆** | 不做统一资源系统。气刃槽色阶（离散枚举）、瓶计数（整数）、三灯（独立 Timer）、充能槽（连续 float）差异太大。UWeaponResourceComponent 基类提供 UI 绑定骨架（GetNormalizedValue+OnValueChanged 委托），各子类自行管理特有字段。词条对资源的加成走 GE 修改资源组件的倍率参数（非 AttributeSet 属性） |
| 79 | **见切判定——bDodgeSuccessful + GameplayEvent 驱动** | 见切不拆成两个 GA——后撤+回砍是一个完整动作。AnimNotifyState_ForesightJudge 窗口内监听 HandleGameplayEvent(HitStagger)→设 bDodgeSuccessful=true→回砍命中时通过此标志决定是否恢复气刃槽+授予 ForesightSuccess Tag（大回旋派生条件）。闪避失败时回砍照样可命中但不触发奖励 |
| 80 | **登龙招内派生——ShouldContinueAfterHit 钩子** | 登龙三段：突刺→起跳→下劈(多跳)。突刺命中后调用 ShouldContinueAfterHit 检查资源≥白→是则播起跳、否则播后摇。这是招内派生（同一 GA），不同于连招表的跨 GA 派生。CanActivateAbility 和 ShouldContinueAfterHit 两轮资源检查在当前单帧原子模型下值一致——若将来有招内耗耐则需此设计意图 |
| 81 | **见切继承 UMHGZAttackAbility——非攻击段用 Damage=0 + 独立 AnimNotifyState** | 见切是连招表中的一行，必须继承 AttackAbility（协调器的 `Entry.AbilityClass` 类型约束）。段0 后撤闪避：`AttackSegments[0].Damage` 全为 0，真正的闪避判定由并行的 `AnimNotifyState_DodgeWindow`（无敌帧）+ `AnimNotifyState_ForesightJudge`（监听 GameplayEvent）完成，不经过 AttackCollision 的 Sweep 管线。段1 回砍走标准 AttackCollision→ApplyDamage。`AttackSegments[0]` 的碰撞形状可设为极小/不创建——它只为满足多段框架的占位。一个 GA、两段、两套完全不同的判定机制——段0 用 GameplayEvent（怪物→玩家方向），段1 用 Sweep（玩家→怪物方向） |

---

## 十一、验证方案

1. 创建 r5 武器、r3 饰品 DataAsset，验证 RarityLevel 和 Tag 自动生成
2. 孔位 Lv3→镶 Lv2 饰品 ✅，孔位 Lv2→镶 Lv3 饰品 ❌
3. 背包/仓库：堆叠、转移、一键整理
4. 仓库分类：标签页切换正确，稀有度筛选 r1-r12
5. 快捷栏自动登记：放入药水→自动显示，用完→自动移除
6. 快捷栏切换使用：滚轮选中→按键触发→数量-1→归零清槽
7. 特殊动作：分配钩爪→选中→触发（不涉及物品）
8. 装备武器→ASC 获词条 GE；卸下→Tag 批量移除→属性回归基础值
9. ASC 属性初始值验证：Health/Stamina=100，耐力三速率=1.0，AttackPower/Defense=0
10. 装备 AttackPower=15 武器→ASC AttackPower=15；卸下→归零
11. CriticalRate 超出 [-100,100] 截断，StaminaDeductionRate 不低于 0
12. 装备太刀（查 DT_WeaponResourceConfig 有配置）→资源条出现；切换大剑（无配置）→资源条消失
13. DT_EntryCatalog 登记 AttackUp→装备引用→装备时查表→UExecCalc_EntryStat 读曲线算数值→Apply
14. **装备实例独立状态**：两把"铁剑"A 和 B→A 镶火焰宝石、B 镶寒冰宝石→各自 EquipmentInstance→SocketedAccessories 独立，不冲突
15. 存档：ItemInstance 序列化 + EquipmentInstance 序列化 → 反序列化恢复
16. **输入绑定**：按键 A→InputAction→Tag→ASC 触发 GA；改 DataAsset 中的 AbilityTag 后按键 A 触发不同 GA
17. **武器切换**：太刀→大剑→旧连招 Ability 被移除→新连招 Ability 被授予→输入绑定自动切换；协调器同步加载新 ComboData
18. **AttackAbility 中间层**：创建 GA_Sword_Slash_01（继承 UMHGZAttackAbility）→只配 AttackSegments（每段独立碰撞+MotionValue+BaseStaggerValue）+Montage → 不写任何蓝图逻辑 → AnimNotifyState 自动开关碰撞 → 命中敌人自动 Apply 伤害 GE（含多跳）；换大剑 GA_GreatSword_Slash_01 → 同流程，只改参数
19. **连招窗口 AnimNotifyState**：在 Montage 中拖拽 NotifyState 区间 → NotifyBegin 打开窗口、NotifyEnd 关闭窗口 → 窗口内按键触发下一招、窗口外按键被忽略
20. **连招安全兜底**：动画被打断/卡地形超过 ComboTimeout（2s）→ 协调器强切 CurrentState=Idle → 清除 Combo.Branch.* Tag
21. **虫棍类无收尾招环**：RisingSlash→△→DoubleSlash→△→DoubleSlash（自循环）→ 窗口内继续循环、窗口超时自然回 Idle；方向+○ 区分 Thrust（前+△）和 WideSweep（○）派生
22. **Montage 完成回 Idle**：打完 TornadoSlash 后 Montage 自然播完 → GA EndAbility → 协调器 OnAttackFinished() → CurrentState="Idle" → 再按 △ 可从 Idle 重新起手上捞斩。动画自身长度即精确计时，ComboTimeout(10s) 仅在卡死等极端异常时兜底
23. **协调器 Infinite 持续型**：装备太刀期间，ASC→GetActiveAbilities() 始终包含 GA_WeaponComboCoordinator；AnimNotifyState_ComboWindow 从 NotifyBegin 查找不失败
24. **地面/空中态隔离**：被击飞后 ASC 同时持有 Aerial+Hitstun → 地面招式的 RequiredTags（需 Grounded）不再满足 → 按 △ 无响应。落地后 Aerial 移除 + Hitstun 过期 → 地面招式恢复
25. **拔刀/纳刀态**：默认持 Sheathed → 按 △ 触发拔刀斩（RequiredTags 含 Sheathed, GrantedTags 含 Unsheathed）→ 后续地面招式 RequiredTags 含 Unsheathed 可正常连招。按 R1 纳刀 → Sheathed 恢复 → Unsheathed 移除 → 攻击招式不可用
26. **统一派发路由**：按 Y → lambda 捕获 `Input.Weapon.Y` → `OnInputActionTriggered(Input.Weapon.Y)` → `MatchesTag("Input.Weapon")`=true → 协调器→HandleWeaponInput。按 A（Dodge）→ lambda 捕获 `Input.Dodge` → MatchesTag=false → TryActivateAbilityByTag。同一个 `OnInputActionTriggered`、同一个 lambda 绑定模式、Tag 值决定分叉——回调内零查表
27. **部位命中顺序**：武器轨迹先掠过翅膀（防御 0.5）再命中身体（防御 1.0）→ Sweep 按 HitResult.Time 取首个命中=翅膀 → 伤害按翅膀倍率计算
28. **部位去重**：同一斩击命中怪物头部→记录（怪物A, Head），后续同帧 Overlap 到身体→跳过；第二只怪物独立记录
29. **多段判定**：双刀乱舞 Montage 中放 4 个独立 AnimNotifyState_AttackCollision → 每段独立 Sweep → 各自记录 HitTargets → 互不干扰
30. **Montage 归属**：GA_Sword_Slash_01 内部指定 Montage → FComboNode 仅引用 AbilityClass → 连招表和动画零耦合
31. **空挥断连**：(a) 气刃斩1 设 `bRequiresHitToContinue=true` + `GrantedTags=Combo.Branch.SpiritBlade` → 气刃斩1 空挥 → `OnAttackHit()` 不触发 → SpiritBlade Tag 未授予 → 气刃斩2 的 `RequiredTags` 含 SpiritBlade → 匹配失败 → 断连。(b) DoubleSlash 段1 设 `Damage.bRequiresHitToContinue=true` → 段1 碰撞窗口空挥 → `HitTargets` 为空 → `EndAbility` 提前 → 段2 不播放。(c) GA_Slash 段0 设 `Damage.OnHitSelfEffect=GE_AtkUp` → 首次命中 → Apply GE_AtkUp 到自身 → 同一 GA 内后续命中不再重复 Apply
32. **太刀 BlockedTags + 资源门控**：(a) 特殊纳刀 `bMatchAnyState=true, RequiredTags={Unsheathed}` → Idle 持 Sheathed → 不可起手 ✓；任意持刀态 → 可派生 ✓。(b) 登龙剑 `BlockedTags={Combo.Branch.PostRoundslash}` → 大回旋后 PostRoundslash 存在 → 登龙不可派生 ✓；其他招式后 PostRoundslash 不存在 → 可派生 ✓。(c) 登龙第一段命中 → `ShouldContinueAfterHit()` 覆写检查气刃槽 ≥ 白 → 满足 → 第二段播放；不满足 → EndAbility。(d) 大回旋 `GrantedTags={Combo.Branch.PostRoundslash}` → Montage 完成回 Idle → PostRoundslash 随 Combo.Branch.* 一同清除 → 登龙恢复可派生
33. **预输入缓冲**：(a) 纵斩 Montage ComboWindow 开前 0.1s 按 Y → `PreInputTag=Input.Weapon.Y` 写入协调器 → NotifyBegin → `HandleWeaponInput(Input.Weapon.Y)` 被调用 → 正常匹配 → 纵斩发出 ✓。(b) ComboWindow 开前 0.3s 按 Y → 超 PreInputLifetime(0.15s) → NotifyBegin 时缓冲已清空 → 无响应 ✓。(c) 窗口开前连续按 Y→B→Y → 后覆盖前 → 缓冲中仅保留最后的 Y。(d) 翻滚 A 键 ComboWindow 外 DodgeAcceptWindow 内 → 直接触发 ✓
34. **翻滚窗口（纯 Tag）**：(a) 纵斩收招帧（ComboWindow 已关但 DodgeAcceptOpen Tag 仍存在）按 A → `Attacking` 有 + `DodgeAcceptOpen` 有 → CanActivateAbility 通过 → 翻滚发出 ✓。(b) Idle 按 A → `Attacking` 不存在 → 翻滚始终可用 ✓。(c) 虫棍纵斩 DodgeAcceptOpen 内按 LT+B → 经协调器 → 收虫行 `bRequiresWindowOpen=false`+`DodgeAcceptOpen` 满足 → 收虫取消 ✓
35. **bRequiresWindowOpen=false + DodgeAcceptOpen**：(a) 虫棍纵斩 DodgeAcceptOpen 窗口内按 LT+B → `Input.Weapon.B`+`Aiming` → 经协调器 → 匹配收虫行 → `DodgeAcceptOpen` 满足 → 收虫触发 ✓。(b) 同一招式 DodgeAcceptOpen 窗口外按 LT+B → `DodgeAcceptOpen` Tag 不存在 → RequiredTags 不满足 → 不触发 ✓。(c) 普通纵斩 ComboWindow 外 DodgeAcceptOpen 内按 A → 翻滚触发 ✓（与取消动作共用同一窗口）
36. **DataManager 全局查询**：(a) EquipmentComponent 通过 DataManager 查词条目录 → 正常返回 FEntryDefinition ✓。(b) ExecCalc 通过 DataManager 获取曲线表 → EvaluateEntryMagnitude 得正确数值 ✓。(c) DataManager 资产未加载时 → FindEntryDefinition 返回 false → 日志警告 ✓
37. **移速同步**：(a) 装备大剑 → MoveSpeedMultiplier=0.6 → Tick 同步 CMC.MaxWalkSpeed=Base×0.6 ✓。(b) 再喝加速药水 GE MoveSpeedMultiplier+0.2 → 叠加=0.8 → CMC 自动同步 ✓。(c) 卸下大剑 → 装备 GE 移除 → MoveSpeedMultiplier 回归 1.0 ✓
38. **持刀不可奔跑**：(a) 收刀态按 LS → GA_Sprint 激活 ✓。(b) 拔刀后按 LS → Unsheathed Tag 阻塞 CanActivateAbility → 不激活 ✓。(c) 纳刀后按 LS → 恢复可奔跑 ✓
39. **GameplayEvent 受击连打**：(a) 目标 Idle → 受击 → ExecCalc HandleGameplayEvent(HitStagger) → GA_HitReaction 实例1 激活 → Hitstun Tag 添加 ✓。(b) 实例1 Montage 播放中再次受击 → ExecCalc 再次 HandleGameplayEvent → GA_HitReaction 实例2 激活（InstancedPerExecution）→ 实例1 被打断 BlendOut → 实例2 Montage 接管 ✓。(c) 实例2 播完 → EndAbility → Hitstun Tag 移除 ✓
40. **StaminaRequired vs StaminaCost**：(a) 纵斩 Required=20, Cost=20 → 协调器检查 CurrentStamina≥20 ✅ → 激活 GA → 扣 20 ✓。(b) 策划设 Required=25, Cost=20 → CurrentStamina=22 → 协调器门槛不通过 → 连招匹配失败（即使 GA 本身扣得起 20）✓。(c) CurrentStamina=30 → 门槛通过 → GA 扣 20 → 剩余 10 ✓
41. **持续耗耐帧率无关**：(a) 60 FPS 奔跑 1s → OnTick 累计扣除 = StaminaCostRate × 1.0 ✓。(b) 30 FPS 奔跑 1s → OnTick 累计扣除同 60 FPS（每帧 Δt 翻倍、次数减半，总扣除一致）✓。(c) 耐力扣到 0 → ApplyModToAttribute 自动 Clamp 到 0 → EndAbility ✓
42. **蓄力攻击**：(a) 按住 Y → GA_ChargeSlash 激活 → ChargeLevel 递增 → ASC 持有 `Input.Modifier.Charging` ✓。(b) 松开 Y → Completed 事件 → GA 内部读 ChargeLevel=Lv3 → 播放真蓄力斩 Montage → 命中 Apply 伤害 ✓。(c) 蓄力期间按 A → GA_Dodge 的 CanActivateAbility 检查 `Charging` Tag 自行决定是否允许翻滚（策划配置）✓。(d) 真蓄力斩 Montage 播完 → EndAbility → ComboWindow 期间按 Y → 协调器 CurrentState="TrueChargeSlash" → 匹配后续连招 ✓
43. **无敌帧碰撞穿透**：(a) 翻滚 DodgeWindow NotifyBegin → 玩家胶囊体 Weapon 通道=Ignore + ASC Tag=Invincible ✓。(b) 怪物尾巴攻击 Sweep(Weapon) → 玩家胶囊体 Ignore → Sweep 不命中玩家 → 不触发 ApplyDamage ✓。(c) 翻滚期间冲向怪物身体 → Pawn 通道 Block → 无法穿过 ✓。(d) NotifyEnd → Weapon 通道恢复 + Invincible Tag 移除 → 恢复可被命中 ✓
44. **怪物攻击碰撞**：(a) 龙甩尾 Montage NotifyBegin → Tail1/Tail2/TailTip 三个部位 MonsterAttack 通道=Block ✓。(b) NotifyTick Sweep 检测玩家 → 命中 → 构造怪物伤害 GE Apply ✓。(c) 甩尾收招阶段（NotifyEnd 后）→ MonsterAttack 通道恢复 Ignore → 尾巴缓慢归位动画不误伤 ✓。(d) 龙车高速冲撞 → 首帧 Sweep 从上一帧位置扫到当前帧 → 命中玩家 ✓
45. **怪物部位胶囊体常态**：(a) 头部始终 Block Weapon 通道 → 玩家斩击能命中头部 ✓。(b) 头部始终 Block Pawn 通道 → 玩家不能穿过龙头 ✓。(c) 头部 MonsterAttack 常态 Ignore → 怪物待机时不误伤玩家 ✓
46. **方向修正**：(a) 太刀纵斩 MaxCorrectionAngle=30° → 摇杆前推 20° → 角色朝向旋向目标 ✓。(b) 摇杆前推 50°（超出30°）→ 不修正 → 角色保持原朝向 ✓。(c) 无摇杆输入 → 不修正 ✓
47. **蓄力释放方向修正**：(a) 大剑蓄力 MaxCorrectionAngle=60° → 蓄力期间摇杆推左 → Completed 时读方向 → 设 Warp Target=左 × 500 + 最大角 60° → Montage 播放时旋向目标 ✓。(b) 蓄力期间摇杆不动 → 保持蓄力开始时的朝向 ✓
48. **见切多段二次修正**：(a) 激活 GA → 摇杆推前 → 反方向=后 → 段1 Warp Target=后方 180° → 后撤 ✓。(b) 段1 NotifyEnd → 摇杆推左 → 更新 Warp Target=左方 120° → 段2 回砍向左修正 ✓。(c) 段2 修正角度超出 120° → 截断在 120° ✓
49. **多段攻击不同 MotionValue**：(a) 横扫段 MotionValue=0.6、下劈段 MotionValue=1.2 → 两段分别从 AttackSegments[0] 和 [1] 读取 ✓。(b) 同一怪物被两段都命中 → 各段独立调用 ApplyDamage(..., SegmentIndex) → Damage Spec 中 SetByCaller 伤害值分别为 Attack×0.6 和 Attack×1.2 ✓
50. **单碰撞多跳伤害**：(a) 登龙剑 AttackSegments[0].MultiHitCount=7, MultiHitInterval=0.1 → Sweep 命中怪物 → 第 1 跳立即 Apply → MultiHitTimer 启动 → 每隔 0.1s Apply 第 2~7 跳 ✓。(b) DisableCollision → Timer 清除 → 后续跳不再 Apply ✓。(c) 怪物在跳动期间死亡 → 后续跳跳过该怪物 ✓
51. **破坏值计算**：(a) 片手剑纵斩 BaseStaggerValue=8, StaggerMultiplier=1.0, HitzoneStaggerRate=0.8 → 硬直值=6.4 ✓。(b) 装备"破坏王"技能珠 GE → StaggerMultiplier=1.3 → 同招式硬直值=8.32 ✓。(c) BaseStaggerValue=0 → 不造成硬直（如多跳招式的前几跳）✓
52. **武器基类分化**：(a) 太刀所有 GA 继承 UMHGZLongSwordAbility → 持有 URes_LongSword* → 覆写 CheckWeaponResourceForAbility 读气刃槽等级 ✓。(b) 虫棍所有 GA 继承 UMHGZInsectGlaiveAbility → 覆写 ShouldContinueAfterHit 检查三灯状态 ✓。(c) 斩斧添加新武器基类 → 只需建 UMHGZSwitchAxeAbility 子类 + 蓝图子类 GA → 不碰现有武器代码 ✓
53. **武器资源子系统**：(a) 气刃斩命中 → 气刃槽 Amount 增加 → UI 自动刷新（OnValueChanged 委托）→ 达到阈值 → Level 从白升黄 ✓。(b) 一段时间未命中 → Decay Timer 触发 → Level 降级从红到黄 ✓。(c) 虫棍同时持红灯+白灯 → 组合 Buff GE Apply → 体力上限+攻击力；三星全 Buff Timer 内再次吸灯 → 刷新三灯计时器 ✓。(d) 词条"气刃槽回复+20%" → GE 修改 URes_LongSword::RegenMultiplier=1.2 → 回复速度加快 ✓
54. **登龙招内派生**：(a) 登龙激活 → 段0 突刺命中 + 气刃≥白 → ShouldContinueAfterHit=true → 段1 起跳 → 段2 下劈多跳 ✓。(b) 登龙激活 → 段0 突刺命中 + 气刃<白 → ShouldContinueAfterHit=false → 段0-fail 后摇 → EndAbility ✓。(c) CanActivateAbility 时资源<白 → 登龙激活被拒 → 协调器匹配回退 ✓
55. **见切 AttackAbility 双段异质判定**：(a) GA_LS_ForesightSlash 继承 UMHGZLongSwordAbility→UMHGZAttackAbility → 协调器通过 `Entry.AbilityClass` 正常激活 ✓。(b) AttackSegments[0].Damage={MotionValue=0, BaseStaggerValue=0} → 段0 的 EnableCollision 不创建碰撞体或创建极小碰撞体（策划配置）→ 不产生伤害 ✓。(c) Montage 段0 区间同时挂 AnimNotifyState_DodgeWindow + AnimNotifyState_ForesightJudge → 怪物攻击命中玩家 → 伤害被 Invincible 截为 0 → HandleGameplayEvent(HitStagger) 仍触发 → GA 记录 bDodgeSuccessful=true ✓。(d) 段0→段1 切换走标准 DisableCollision→EnableCollision(1) 生命周期 ✓。(e) 段1 AttackSegments[1] 配正常 MotionValue+碰撞 → AnimNotifyState_AttackCollision(ConfigIndex=1) → OnAttackOverlap→ApplyDamage → bDodgeSuccessful ? 回满气刃槽+授予 ForesightSuccess : 仅伤害 ✓
