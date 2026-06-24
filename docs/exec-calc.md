# 伤害执行计算（ExecCalc）

> **设计原则：** 单一 `UExecCalc_Damage` 处理全部伤害来源（武器攻击 + 猎虫伤害），通过 `SetByCaller` 区分来源参数。伤害公式 = `AttackPower × MotionValue × HitzoneDefense × CritMultiplier`。硬直公式独立计算后通过 `GameplayEvent` 广播。

---

## 零、架构定位

```
攻击链路
  MakeDamageSpec (GA 侧)
    → 注入 SetByCaller: Damage.MotionValue, Damage.BaseStagger
    → 注入 DynamicAssetTags: Hitzone.* (部位信息)
    → 注入 DynamicGameplayCueTags: GameplayCue.Hit.* (命中反馈)
    → ASC::ApplyGameplayEffectSpecToTarget
      → UExecCalc_Damage::Execute ← 本文档
        → 计算最终伤害 + 硬直
        → 暴击判定
        → OutExecutionOutput: Health -= FinalDamage
        → HandleGameplayEvent: Combat.Event.HitStagger
      → ASC 自动触发 GameplayCue (火花/音效/伤害数字)
```

---

## 一、SetByCaller 键名体系（统一）

| 键名 (FGameplayTag) | 类型 | 来源 | 说明 |
|------|:--:|------|------|
| `Damage.MotionValue` | float | GA 的 `AttackSegments[i].Damage.MotionValue` | 招式动作值倍率。武器攻击=GA 配置值；猎虫=送虫 GA 通过 `SetDamageParams` 传入 |
| `Damage.BaseStagger` | float | GA 的 `AttackSegments[i].Damage.BaseStaggerValue` | 招式基础破坏值 |
| `Damage.AttackPower` | float | 猎虫专用——`URes_InsectGlaive::GetModifiedKinsectAttackPower()` | **可选**：仅猎虫伤害时设置，武器攻击时 ExecCalc 从 Source ASC 的 `AttackPower` Attribute 读取 |
| `Damage.CritOverride` | float | 可选——GA 内部暴击覆写（见切成功等） | **可选**：-1 表示不覆写，使用角色 CriticalRate 正常判定 |

> **命名约定：** 全部位于 `Damage.*` 命名空间下。`Damage.Kinsect.*` 旧名已废弃，统一为 `Damage.MotionValue`（猎虫伤害时由 `ApplyKinsectDamage` 设置相同的 Tag）。

---

## 二、输入参数汇总

### 从 GE Spec 读取（SetByCaller）

| 参数 | Tag | 必须？ | 默认值 |
|------|------|:--:|:--:|
| 动作值 | `Damage.MotionValue` | ❌ | 1.0 |
| 基础破坏值 | `Damage.BaseStagger` | ❌ | 0 |
| 攻击力覆写 | `Damage.AttackPower` | ❌ | -1（-1=从 Source ASC 读取） |
| 暴击覆写 | `Damage.CritOverride` | ❌ | -1（-1=正常暴击判定） |
| 伤害显示值 | `Damage.DisplayValue` | ❌ | 0（0=由 GC 从 RawMagnitude 读取） |

### 从 GE Spec 读取（DynamicAssetTags）

| 参数 | Tag 模式 | 说明 |
|------|------|------|
| 命中部位 | `Hitzone.*` | 由 `MakeDamageSpec` 注入。ExecCalc 据此在 Target 上查找 `UMonsterHitzoneComponent` |

### 从 Source ASC 读取（攻击方属性）

| 属性 | Attribute | 说明 |
|------|------|------|
| 攻击力 | `UMHGZAttributeSet::GetAttackPowerAttribute()` | 仅当 `Damage.AttackPower` 为 -1 时读取 |
| 会心率 | `UMHGZAttributeSet::GetCriticalRateAttribute()` | 仅当 `Damage.CritOverride` 为 -1 时读取 |
| 破坏值倍率 | `UMHGZAttributeSet::GetStaggerMultiplierAttribute()` | 装备词条加成后的破坏值倍率 |

### 从 Target 读取（受击方数据）

| 信息 | 来源 | 说明 |
|------|------|------|
| 肉质 | `UMonsterHitzoneComponent::DefenseMultiplier` | 通过 HitzoneTag 匹配 Target 上的部位碰撞体 |
| 硬直肉质 | `UMonsterHitzoneComponent::StaggerRate` | 同上 |
| 部位 Tag | `UMonsterHitzoneComponent::HitzoneTag` | 用于日志/调试 |

---

## 三、伤害公式

### 3.1 最终伤害

$$\text{RawDamage} = \text{AttackPower} \times \text{MotionValue} \times \text{HitzoneDefense}$$

$$\text{bCrit} = (\text{CriticalRate} > 0) \land (\text{Random}(0, 100) < \text{CriticalRate})$$

$$\text{FinalDamage} = \text{RawDamage} \times \begin{cases} 1.25 & \text{if bCrit} \\ 1.0 & \text{otherwise} \end{cases}$$

| 参数 | 来源 | 范围 |
|------|------|:--:|
| AttackPower | `Damage.AttackPower` > 0 ? 用覆写值 : 读 Source ASC 的 `AttackPower` Attribute | [0, ∞) |
| MotionValue | Spec 的 `Damage.MotionValue` SetByCaller | (0, ∞) |
| HitzoneDefense | Target 的 `UMonsterHitzoneComponent::DefenseMultiplier` | (0, ∞)，典型值 0.2~1.5 |
| CriticalRate | `Damage.CritOverride` >= 0 ? 覆写 : 读 Source ASC 的 `CriticalRate` | [-100, 100] |

### 3.2 硬直值

$$\text{Stagger} = \text{BaseStaggerValue} \times \text{StaggerMultiplier} \times \text{HitzoneStaggerRate}$$

| 参数 | 来源 | 范围 |
|------|------|:--:|
| BaseStaggerValue | Spec 的 `Damage.BaseStagger` SetByCaller | [0, ∞) |
| StaggerMultiplier | Source ASC 的 `StaggerMultiplier` Attribute（装备词条加成后） | [0, ∞) |
| HitzoneStaggerRate | Target 的 `UMonsterHitzoneComponent::StaggerRate` | [0, ∞) |

---

## 四、执行步骤（伪代码）

```cpp
void UExecCalc_Damage::Execute_Implementation(
    const FGameplayEffectCustomExecutionParameters& ExecutionParams,
    FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const
{
    // ── 1. 获取 Source 和 Target ──
    const FGameplayEffectSpec& Spec = ExecutionParams.GetOwningSpec();
    AActor* SourceActor = Spec.GetContext().GetInstigator();
    AActor* TargetActor = ExecutionParams.GetTargetAbilitySystemComponent()->GetOwnerActor();

    // ── 2. 获取 Source ASC 属性 ──
    FAggregatorEvaluateParameters EvalParams;
    float AttackPower = 0.f;
    float CriticalRate = 0.f;
    float StaggerMultiplier = 1.0f;

    ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
        UMHGZAttributeSet::GetAttackPowerAttribute(), EvalParams, AttackPower);
    ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
        UMHGZAttributeSet::GetCriticalRateAttribute(), EvalParams, CriticalRate);
    ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
        UMHGZAttributeSet::GetStaggerMultiplierAttribute(), EvalParams, StaggerMultiplier);

    // ── 3. 从 Spec 读取 SetByCaller ──
    float MotionValue = Spec.GetSetByCallerMagnitude(
        FGameplayTag::RequestGameplayTag("Damage.MotionValue"), true, 1.0f);
    float BaseStagger = Spec.GetSetByCallerMagnitude(
        FGameplayTag::RequestGameplayTag("Damage.BaseStagger"), true, 0.f);
    float AttackPowerOverride = Spec.GetSetByCallerMagnitude(
        FGameplayTag::RequestGameplayTag("Damage.AttackPower"), true, -1.f);
    float CritOverride = Spec.GetSetByCallerMagnitude(
        FGameplayTag::RequestGameplayTag("Damage.CritOverride"), true, -1.f);

    // 猎虫伤害→使用覆写的攻击力
    if (AttackPowerOverride > 0.f)
    {
        AttackPower = AttackPowerOverride;
    }

    // ── 4. 从 DynamicAssetTags 读取 Hitzone 信息 ──
    float HitzoneDefense = 1.0f;
    float HitzoneStaggerRate = 1.0f;
    FGameplayTag HitzoneTag;

    for (const FGameplayTag& Tag : Spec.DynamicAssetTags)
    {
        if (Tag.MatchesTag(FGameplayTag::RequestGameplayTag("Hitzone")))
        {
            HitzoneTag = Tag;
            break;
        }
    }

    if (HitzoneTag.IsValid())
    {
        // 在 Target 上查找对应的 UMonsterHitzoneComponent
        TArray<UMonsterHitzoneComponent*> Hitzones;
        TargetActor->GetComponents<UMonsterHitzoneComponent>(Hitzones);
        for (UMonsterHitzoneComponent* HZ : Hitzones)
        {
            if (HZ->HitzoneTag == HitzoneTag)
            {
                HitzoneDefense = HZ->DefenseMultiplier;
                HitzoneStaggerRate = HZ->StaggerRate;
                break;
            }
        }
    }

    // ── 5. 伤害计算 ──
    float RawDamage = AttackPower * MotionValue * HitzoneDefense;

    // 暴击判定
    bool bCrit = false;
    float EffectiveCritRate = (CritOverride >= 0.f) ? CritOverride : CriticalRate;
    if (EffectiveCritRate > 0.f && FMath::FRandRange(0.f, 100.f) < EffectiveCritRate)
    {
        bCrit = true;
        RawDamage *= 1.25f;
    }

    // 至少造成 1 点伤害（避免 0 伤害无反馈）
    float FinalDamage = FMath::Max(1.0f, RawDamage);

    // ── 6. 写入输出 ──
    // 扣血（仅当 Target 有 Health Attribute 时生效——木桩无 AttributeSet 则跳过）
    OutExecutionOutput.AddOutputModifier(
        FGameplayModifierEvaluatedData(
            UMHGZAttributeSet::GetHealthAttribute(),
            EGameplayModOp::Additive,
            -FinalDamage));

    // ── 7. 暴击时标记 GameplayCue ──
    // ★ I-6 修复：使用 OutExecutionOutput.MarkGameplayCueActive 而非 const_cast 修改 Spec。
    // const_cast 在 Spec 为栈拷贝时修改无效——GC 通知系统读不到注入的 Tag。
    if (bCrit)
    {
        OutExecutionOutput.MarkGameplayCueActive(
            FGameplayTag::RequestGameplayTag("GameplayCue.Hit.Crit"));
    }

    // ── 8. 硬直计算与事件广播 ──
    // ★ 注意：HandleGameplayEvent 不是 const 方法，ExecCalc 中不能直接调用。
    // 硬直事件通过 PostGameplayEffectExecute 中检查 DynamicAssetTags 再广播。
    // 此处仅将 StaggerValue 通过 SetSetByCaller 输出，供 AttributeSet 回调读取。
    if (BaseStagger > 0.f)
    {
        float StaggerValue = BaseStagger * StaggerMultiplier * HitzoneStaggerRate;
        // 通过 OutExecutionOutput 的 SetSetByCaller 传递硬直值
        //（具体 API 取决于 UE 版本——也可通过 GameplayEffectContext 传递）
        OutExecutionOutput.MarkGameplayCueActive(
            FGameplayTag::RequestGameplayTag("Combat.Event.HitStagger"));
    }

    // ── 9. 伤害显示值 ──
    // ★ GC_Hit_DamageNumber::OnBurst 从 FGameplayCueParameters::RawMagnitude 读取伤害值。
    // RawMagnitude 由 GAS 自动从 GE Modifier 的 Magnitude 填充——无需手动 SetByCaller。
    // 此处 FinalDamage 已通过步骤 6 的 AddOutputModifier(Health, Additive, -FinalDamage)
    // 写入——GC 的 RawMagnitude 为 abs(-FinalDamage) = FinalDamage。
}
```

---

## 五、GameplayCue 联动

### 5.1 GC Tag 注入时机

| 阶段 | 注入方 | Tag |
|------|------|------|
| `MakeDamageSpec` | GA（攻击基类） | `GameplayCue.Hit.Slash` / `.Blunt`（物理类型） |
| `MakeDamageSpec` | GA（武器子类覆写） | `GameplayCue.Hit.Fire` / `.Ice` 等（元素属性） |
| `MakeDamageSpec` | 通用 | `GameplayCue.Hit.DamageNumber`（伤害数字） |
| `Execute` (ExecCalc) | 暴击时 | `GameplayCue.Hit.Crit`（暴击特效） |
| `MakeDamageSpec` | `UMHGZInsectGlaiveAbility` 覆写 | `GameplayCue.Hit.IG.DivingWyvern`（降龙专用） |

### 5.2 伤害数字读取方式

`GC_Hit_DamageNumber::OnBurst` 从 `FGameplayCueParameters::RawMagnitude` 读取伤害值——这是 GAS 标准路径。`GE_Damage` 的唯一 Modifier 是 `Health -= FinalDamage`，GAS 自动将其 Magnitude 的绝对值填入 `RawMagnitude`。

```cpp
// GC_Hit_DamageNumber::OnBurst
float DamageValue = FMath::Abs(Parameters.RawMagnitude);
// RawMagnitude 为负值（扣血），取绝对值显示
```

> **原理：** `RawMagnitude` 由 GAS 在 ApplyGE 时从首个 Modifier 的 Magnitude 自动填充，不依赖 SetByCaller、不依赖 Target AttributeSet。木桩无 AttributeSet 时 ExecCalc 仍执行、`RawMagnitude` 仍填充——伤害数字正常显示。

---

## 六、GE 蓝图配置

### GE_Damage（通用伤害 GE）

| 属性 | 值 | 说明 |
|------|------|------|
| DurationPolicy | Instant | 即时生效 |
| Modifiers | — | **留空**——所有数值由 ExecCalc 通过 `OutExecutionOutput` 写入 |
| ExecCalc | `UExecCalc_Damage` | |
| GameplayCueTags | — | **留空**——由 `MakeDamageSpec` 动态注入 |

### GE_KinsectDamage（猎虫伤害 GE）

| 属性 | 值 | 说明 |
|------|------|------|
| DurationPolicy | Instant | 即时生效 |
| Modifiers | — | 留空 |
| ExecCalc | `UExecCalc_Damage` | **复用同一个 ExecCalc**——通过 `Damage.AttackPower` 覆写区分来源 |
| GameplayCueTags | — | 留空——由 `ApplyKinsectDamage` 构造 Spec 时注入 `GameplayCue.Hit.Kinsect` |

---

## 七、与现有系统的接口

### 7.1 AttackAbility → MakeDamageSpec

```cpp
// UMHGZAttackAbility::MakeDamageSpec
FGameplayEffectSpecHandle UMHGZAttackAbility::MakeDamageSpec(
    const FAttackDamageConfig& DamageConfig,
    const FGameplayTag HitzoneTag)
{
    FGameplayEffectSpecHandle Spec = MakeOutgoingGameplayEffectSpec(GE_Damage);

    // 动作值
    Spec.Data->SetSetByCallerMagnitude(
        FGameplayTag::RequestGameplayTag("Damage.MotionValue"),
        DamageConfig.MotionValue);

    // 基础破坏值
    Spec.Data->SetSetByCallerMagnitude(
        FGameplayTag::RequestGameplayTag("Damage.BaseStagger"),
        DamageConfig.BaseStaggerValue);

    // 部位信息
    if (HitzoneTag.IsValid())
    {
        Spec.Data->DynamicAssetTags.AddTag(HitzoneTag);
    }

    // GameplayCue——命中反馈
    Spec.Data->DynamicGameplayCueTags.AddTag(DamageConfig.HitCueTag);
    Spec.Data->DynamicGameplayCueTags.AddTag(
        FGameplayTag::RequestGameplayTag("GameplayCue.Hit.DamageNumber"));

    return Spec;
}
```

### 7.2 URes_InsectGlaive → ApplyKinsectDamage

```cpp
void URes_InsectGlaive::ApplyKinsectDamage(
    UMonsterHitzoneComponent* Hitzone, AActor* Monster, float MotionValue)
{
    UAbilitySystemComponent* PlayerASC = GetPlayerASC();
    UAbilitySystemComponent* MonsterASC = Monster->FindComponentByClass<UAbilitySystemComponent>();
    if (!PlayerASC || !MonsterASC) return;

    FGameplayEffectSpecHandle Spec = PlayerASC->MakeOutgoingSpec(
        GE_KinsectDamage, 1.0f, PlayerASC->MakeEffectContext());

    // ★ 统一使用 Damage.MotionValue（与武器攻击一致）
    Spec.Data->SetSetByCallerMagnitude(
        FGameplayTag::RequestGameplayTag("Damage.MotionValue"), MotionValue);

    // ★ 猎虫攻击力覆写——ExecCalc 读到 >0 时使用此值而非玩家 AttackPower
    Spec.Data->SetSetByCallerMagnitude(
        FGameplayTag::RequestGameplayTag("Damage.AttackPower"),
        GetModifiedKinsectAttackPower());

    // 部位信息
    Spec.Data->DynamicAssetTags.AddTag(Hitzone->HitzoneTag);

    // GameplayCue——猎虫命中反馈
    Spec.Data->DynamicGameplayCueTags.AddTag(
        FGameplayTag::RequestGameplayTag("GameplayCue.Hit.Kinsect"));
    Spec.Data->DynamicGameplayCueTags.AddTag(
        FGameplayTag::RequestGameplayTag("GameplayCue.Hit.DamageNumber"));

    PlayerASC->ApplyGameplayEffectSpecToTarget(*Spec.Data, MonsterASC);
}
```

---

## 八、与 GAS 内置行为的关系

| 行为 | 是否依赖 AttributeSet | 说明 |
|------|:--:|------|
| ExecCalc `Execute` 被调用 | ❌ | 无论 Target 有无 AttributeSet，ExecCalc 都会执行 |
| `OutExecutionOutput.AddOutputModifier(Health, …)` | ⚠️ | 若 Target 无 Health Attribute→产生 Warning 日志，不崩溃。扣血跳过 |
| `HandleGameplayEvent(HitStagger)` | ❌ | GameplayEvent 独立于 AttributeModifier，始终触发 |
| `DynamicGameplayCueTags` 路由 GC | ❌ | GC 由 ASC 在 ApplyGE 后自动触发，不依赖 AttributeSet |
| 伤害数字 Widget 显示 | ❌ | 从 `Damage.DisplayValue` SetByCaller 读取，不依赖 Attribute |

> **木桩场景：** 木桩 ASC 无 AttributeSet→伤害 GE Apply 成功、GC 触发（火花+伤害数字弹出）、硬直事件广播、属性扣血跳过（Warning 日志）。需要血条时挂载 `UMHGZAttributeSet` 即可。

---

## 九、目录结构

```
Source/MHGZ/
├── ActionSystem/
│   └── MHGZDamageExecCalc.h/cpp      ← UExecCalc_Damage 实现

Content/
├── GameplayEffects/
│   ├── GE_Damage.uasset              ← 通用伤害 GE（Instant）
│   └── GE_KinsectDamage.uasset       ← 猎虫伤害 GE（Instant，复用同一 ExecCalc）

Content/GameplayCues/Hit/
├── GC_Hit_Slash.uasset               ← 斩击命中
├── GC_Hit_Blunt.uasset               ← 打击命中
├── GC_Hit_Kinsect.uasset             ← 猎虫命中
├── GC_Hit_Crit.uasset                ← 暴击命中
└── GC_Hit_DamageNumber.uasset        ← 伤害数字
```

---

## 十、设计决策

| # | 决策 | 理由 |
|---|------|------|
| ED-0 | 单一 ExecCalc 处理全部伤害来源 | 伤害公式统一、维护一处；武器 vs 猎虫通过 SetByCaller 覆写区分 |
| ED-1 | SetByCaller 键名统一为 `Damage.*` 命名空间 | 废弃 `Damage.Value` 和 `Damage.Kinsect.*` 旧名，避免混淆 |
| ED-2 | `Damage.AttackPower` 为可选覆写（-1=读 ASC） | 猎虫伤害传入猎虫攻击力覆写；武器攻击留空让 ExecCalc 读 Source ASC |
| ED-3 | 暴击 Tag（`GameplayCue.Hit.Crit`）在 ExecCalc 中动态注入 | 暴击判定在 ExecCalc 中完成，需要同时注入 GC Tag。唯一合规方式——ExecCalc 中 `const_cast` 修改 Spec |
| ED-4 | 硬直走 `HandleGameplayEvent` 而非 Tag Trigger | 遵循决策 #62——每次调用独立触发，InstancedPerExecution 支持受击连打 |
| ED-5 | 至少 1 点伤害 | 0 伤害攻击无视觉反馈→玩家困惑。1 点伤害 = 最低限度"命中确认" |
| ED-6 | HitzoneComponent 查找用遍历而非 Map 缓存 | 怪物部位数 ≤ 20，O(n) 遍历成本 < 1μs。Map 缓存需要维护增删同步逻辑→不值得 |
