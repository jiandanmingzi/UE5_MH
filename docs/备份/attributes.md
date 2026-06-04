# 角色属性与装备系统

**设计原则：** 装备系统与角色属性完全解耦。装备不直接修改属性——`UMHGZEquipmentComponent` 读取装备的 AttackPower/Defense/CriticalRate 和词条引用，创建 GameplayEffect 授予 ASC。所有装备 GE 统一打 `Effect.Source.Equipment` 标签，装备变更时 `RemoveActiveEffectsWithTags` 一行清空，然后遍历已装备物品重新 Apply。不存 GE Handle，不追踪中间状态。

属性约束：Health/Stamina 基础 100、上限 200；AttackPower/Defense 基础 0、无上限；CriticalRate 基础 0、范围 [-100, 100]；StaminaRegenRate/DeductionRate/ConsumptionRate 基础 1.0、下限 0。

## UMHGZAttributeSet — 角色属性集

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
| StaggerMultiplier | FGameplayAttributeData | 1.0 | ∞ | 0 | 破坏值倍率 |
| MoveSpeedMultiplier | FGameplayAttributeData | 1.0 | 3.0 | 0.1 | 移速倍率（驱动 CMC.MaxWalkSpeed） |

> **武器专属资源不在 AttributeSet 中。** 每种武器的资源由各自的 Ability/Component 管理。

### MoveSpeedMultiplier → CMC 同步机制

角色 Tick 中每帧读 Attribute 写回 CMC：`CMC.MaxWalkSpeed = BaseMaxWalkSpeed × MoveSpeedMultiplier`

| 移速来源 | 实现 |
|----------|------|
| 收刀态基础速度 | BaseMaxWalkSpeed（CMC 初始值缓存） |
| 武器持刀移速差异 | 装备时 Apply GE 修改 MoveSpeedMultiplier（太刀 0.85、大剑 0.6） |
| Buff 移速变化 | 通过 GE 修改 MoveSpeedMultiplier |
| 奔跑加速 | GA_Sprint 激活期间 Apply GE |
| 持刀不可奔跑 | GA_Sprint::CanActivateAbility 检查 Unsheathed Tag |
| 重型武器笨重感 | 武器 GE 同时降低 MaxAcceleration |

## UMHGZEquipmentComponent — 装备 GE 管理组件

挂载到 PlayerState。管理装备槽位、GE 的创建/Apply/移除。

### OnEquipmentChanged 全量重算

```
1. ASC→RemoveActiveEffectsWithTags(Effect.Source.Equipment) — 清空全部装备 GE
2. 遍历 EquippedItems → ApplyItemEffects(Item) — 重新 Apply 全部
```

装备变更仅在非战斗期触发，全量重算零中间状态。

### ApplyEntryGEs — 词条分支处理

按 `EffectType` 分三种路径：
- **SimpleStat：** 构造 GE_EntryStat → SetByCaller 注入 EntryID+EntryLevel → UExecCalc_EntryStat 执行计算
- **Complex：** 实例化 EffectClass → Apply
- **WeaponResource：** 不创建 GE。查找当前武器的 ResourceComponent → `ApplyEntryModifier` → 存入 ActiveModifiers Map

> **多武器词条路由机制：** `FEntryModifier::AttributeTag` 作为路由键。`WeaponResource.LongSword.*` 仅长刀识别；`WeaponResource.Shared.*` 所有武器识别。不匹配→静默跳过。**前缀校验：** `ApplyEntryModifier` 检查 `MatchesTag("WeaponResource")`，不匹配则 Warning 日志+跳过。同理 UExecCalc_EntryStat 检查 `MatchesTag("Attribute")`。

### WeaponResourceComponent 销毁时序

OnEquipmentChanged 内执行顺序：
1. RemoveActiveEffectsWithTags — 清空旧 GE
2. ClearAllEntryModifiers — 清空旧 ResourceComponent 修饰器（旧组件仍存活）
3. DestroyComponent 销毁旧 ResourceComponent
4. NewObject + RegisterComponent 创建新 ResourceComponent
5. ApplyItemEffects → ApplyEntryGEs → 新 ResourceComponent 的 ApplyEntryModifier

## DT_WeaponResourceConfig

DataTable，仅做 WeaponTypeTag → 资源 UI Widget 查找桥接。不含资源数值。

## DT_WeaponComboConfig

DataTable，按武器种类映射连招数据：WeaponTypeTag → TSoftObjectPtr\<UMHGZWeaponComboData\>

## GE_EntryStat

所有 SimpleStat 词条共用此 GE 蓝图。Duration Policy=Infinite，Stacking=AggregateBySource，Calculation Class=UExecCalc_EntryStat。

## UExecCalc_EntryStat

纯 C++ 类。从 Spec 读 EntryID→查 DT_EntryCatalog→遍历 Modifiers→Curve.Eval(EntryLevel)→写入 Out.Add(Attribute, Op, Value)。

## 受击与霸体判定（玩家侧）

**原则：** GE Spec 即信息载体。攻击方在 MakeDamageSpec 中打包伤害值/硬直等级/命中位置进 Spec。硬直触发采用 **GameplayEvent**（非 Tag Trigger）——每次命中独立触发，支持连打。

### 完整受击流程（三步）

```
步骤 1：攻击方 MakeDamageSpec → SetByCaller(伤害) + DynamicTag(HitStaggerTag) + HitResult
步骤 2：目标 ExecCalc::Execute
  → 读 Tag（Invincible/Dead）
  → 读伤害值 → 套用 Defense → 修改 Health
  → 读 HitStaggerTag → 与目标霸体 Tag 比较等级
  → 需硬直 → HandleGameplayEvent(Combat.Event.HitStagger)
步骤 3：GA_HitReaction 激活（InstancedPerExecution）
  → Add Hitstun Tag
  → 选方向/等级 Montage → 播放
  → Montage 播完 → EndAbility → 移除 Hitstun
```

### 霸体等级比较算法——推荐方案 A（硬编码整数映射）

```cpp
static int32 GetStaggerLevel(FGameplayTag Tag) {
    static TMap<FGameplayTag, int32> Map;
    if (Map.IsEmpty()) {
        Map.Add(FGameplayTag::RequestGameplayTag("Combat.Stagger.Light"),  1);
        Map.Add(FGameplayTag::RequestGameplayTag("Combat.Stagger.Medium"), 2);
        Map.Add(FGameplayTag::RequestGameplayTag("Combat.Stagger.Heavy"),  3);
    }
    const int32* Found = Map.Find(Tag);
    return Found ? *Found : 0;
}
static int32 GetPoiseLevel(FGameplayTag Tag) { /* 同理，Super=4 */ }
// 霸体足够 = GetPoiseLevel(PoiseTag) >= GetStaggerLevel(StaggerTag)
```

### 霸体来源

- **攻击自带霸体：** AnimNotifyState_PoiseWindow（NotifyBegin Add Poise Tag，NotifyEnd 移除）
- **装备/被动霸体：** 通过 GE 持续性持有 Poise Tag
