# 伤害执行计算（ExecCalc）

> **实施状态说明（以源码与 Content 为准）：** M2 已把武器/木桩 Demo 伤害接入单一 `UMHGZDamageExecCalc`、自定义 EffectContext、四个 Incoming Meta、AttributeSet 原子结算和 FeedbackRouter。硬直事件与伤害数字 Cue 路由已存在，但 GameplayCue Content 资产尚未创建；猎虫伤害仍沿用旧硬编码资产路径，必须由 M3 改接统一 Context/GE 后才可用。

> **Demo 范围：** 当前只需要稳定验证动作值、命中部位、会心、舞踏倍率和木桩扣血。斩味、斩/打肉质、属性、异常、眩晕、耐力伤害、部位破坏和任务倍率属于后续扩展；它们通过 `FCombatDamageContext` 和 ExecCalc 阶段增加，不要求在本次 Demo 先完整设计。

## 当前实现

| 项目 | 当前状态 |
|------|----------|
| 伤害公式 | `AttackPower × MotionValue × HitzoneDefense × Crit × DanceMultiplier`；零/负动作值为 0，正动作值最低 1。ExecCalc 只写 Meta，不直接扣 Health。 |
| 输入 | SetByCaller 读取 MotionValue/BaseStagger/可选 AttackPower/Crit/Dance；Hitzone Defense/StaggerRate 从 EffectContext 的真实 HitComponent 读取，DynamicAssetTags 仅作调试镜像。 |
| 硬直 | `BaseStagger × SourceStaggerMultiplier × HitzoneStaggerRate` 输出 `IncomingStagger`；AttributeSet 仅在有效 StaggerTag 且值大于 0 时发送硬直事件。 |
| GameplayCue | AttributeSet 结算后构造唯一 `FMHGZHitFeedbackResult`；目标侧 Router 显式执行 Hit/Element/DamageNumber Cue，并提交 HitStop/CameraShake。Content 资产仍待 E6。 |
| GameplayEffect | 通用伤害是 C++ 类 `UMHGZDamageGameplayEffect`（Instant + `UMHGZDamageExecCalc`），不是 `GE_Damage.uasset`。 |
| 猎虫伤害 | 代码硬编码加载 `/Game/GameplayEffects/Core/GE_KinsectDamage`，但资产不存在，因此该路径当前无法结算。 |

> **设计原则：** 全部 Demo 伤害来源复用 `UMHGZDamageExecCalc`，通过明确的 DamageContext/SetByCaller 区分武器、猎虫、粉尘与舞踏参数。简化公式是可替换的计算阶段，不把攻击 Ability 与未来完整伤害规则绑定。

---

## 零、目标完整架构（简化伤害基线保留；上下文/硬直/GC 待迁移）

```
攻击链路
  MakeDamageSpec (GA/猎虫/粉尘侧)
    → FMHGZGameplayEffectContext: 真实 HitResult、AttackInstanceID、来源和 CueTag
    → 注入 SetByCaller: Damage.MotionValue, Damage.BaseStagger
    → ASC::ApplyGameplayEffectSpecToTarget
      → UMHGZDamageExecCalc::Execute ← 本文档
        → 计算最终伤害/硬直/暴击
        → Meta 输出: IncomingDamage + IncomingStagger + IncomingCriticalFlag
        → 最后输出 IncomingHitSignal
      → UMHGZAttributeSet::PostGameplayEffectExecute
        → HitSignal 到达时原子读取/清空 Meta、扣 Health、发送硬直事件、组装 HitFeedbackResult
      → Target HitFeedbackRouter 显式执行 Cue/数字/卡肉请求
```

---

## 一、SetByCaller 键名体系（统一）

| 键名 (FGameplayTag) | 类型 | 来源 | 说明 |
|------|:--:|------|------|
| `Damage.MotionValue` | float | GA 的 `AttackSegments[i].Damage.MotionValue` | 招式动作值倍率。需要伤害的段必须显式设置；缺失按 0 处理，不用隐式 1.0 制造意外伤害 |
| `Damage.BaseStagger` | float | GA 的 `AttackSegments[i].Damage.BaseStaggerValue` | 招式基础破坏值 |
| `Damage.AttackPower` | float | 猎虫专用——`URes_InsectGlaive::GetModifiedKinsectAttackPower()` | **可选**：仅猎虫伤害时设置，武器攻击时 ExecCalc 从 Source ASC 的 `AttackPower` Attribute 读取 |
| `Damage.CritOverride` | float | 可选——GA 内部暴击覆写（见切成功等） | **可选**：-1 表示不覆写，使用角色 CriticalRate 正常判定 |
| `Damage.DanceMultiplier` | float | 虫棍空中 AttackSegment 创建 Spec 时快照 | **可选**：默认 1；猎虫/粉尘固定 1 |

> **命名约定：** 全部位于 `Damage.*` 命名空间下。`Damage.Kinsect.*` 旧名已废弃，统一为 `Damage.MotionValue`（猎虫伤害时由 `ApplyKinsectDamage` 设置相同的 Tag）。

---

## 二、输入参数汇总

### 从 GE Spec 读取（SetByCaller）

| 参数 | Tag | 必须？ | 默认值 |
|------|------|:--:|:--:|
| 动作值 | `Damage.MotionValue` | 伤害段必须 | 0 |
| 基础破坏值 | `Damage.BaseStagger` | ❌ | 0 |
| 攻击力覆写 | `Damage.AttackPower` | ❌ | -1（-1=从 Source ASC 读取） |
| 暴击覆写 | `Damage.CritOverride` | ❌ | -1（-1=正常暴击判定） |
| 舞踏倍率 | `Damage.DanceMultiplier` | ❌ | 1.0 |
| 伤害显示值 | `Damage.DisplayValue` | ❌ | 0（0=由 GC 从 RawMagnitude 读取） |

### 从自定义 FMHGZGameplayEffectContext 读取

| 参数 | 说明 |
|------|------|
| OriginalHitResult | 攻击判定产生的真实 HitResult；命中组件必须是 Hitzone |
| AttackInstanceID | 去重、反击与反馈关联使用的稳定攻击身份 |
| SourceActionTag / DamageSourceType | 区分武器、猎虫、粉尘和木桩攻击，不改变通用公式 |
| HitzoneTag | 从 HitResult.Component 的 Hitzone 元数据快照；用于日志和回退校验 |
| HitCueTag / ElementCueTag | 只作为反馈数据，由 Router 显式执行，不靠 DynamicAssetTags 自动触发 |

### 从 Source ASC 读取（攻击方属性）

| 属性 | Attribute | 说明 |
|------|------|------|
| 攻击力 | `UMHGZAttributeSet::GetAttackPowerAttribute()` | 仅当 `Damage.AttackPower` 为 -1 时读取 |
| 会心率 | `UMHGZAttributeSet::GetCriticalRateAttribute()` | 仅当 `Damage.CritOverride` 为 -1 时读取 |
| 破坏值倍率 | `UMHGZAttributeSet::GetStaggerMultiplierAttribute()` | 装备词条加成后的破坏值倍率 |

### 从 Target 读取（受击方数据）

| 信息 | 来源 | 说明 |
|------|------|------|
| 肉质 | OriginalHitResult.Component 对应 `UMHGZMonsterHitzoneComponent::DefenseMultiplier` | 优先使用本次真实命中组件 |
| 硬直肉质 | 同一 Hitzone 的 `StaggerRate` | 同上 |
| 部位 Tag | Context.HitzoneTag | 用于日志/调试和组件失效时的回退数据 |

---

## 三、伤害公式

### 3.1 最终伤害

$$\text{RawDamage} = \text{AttackPower} \times \text{MotionValue} \times \text{HitzoneDefense}$$

$$\text{bCrit} = (\text{CriticalRate} > 0) \land (\text{Random}(0, 100) < \text{CriticalRate})$$

$$\text{FinalDamage} = \begin{cases} 0 & \text{if AttackPower} \le 0 \text{ or MotionValue} \le 0 \\ \max(1, \lfloor\text{RawDamage} \times \text{DanceMultiplier} \times \text{CritMultiplier}\rfloor) & \text{otherwise} \end{cases}$$

| 参数 | 来源 | 范围 |
|------|------|:--:|
| AttackPower | `Damage.AttackPower` > 0 ? 用覆写值 : 读 Source ASC 的 `AttackPower` Attribute | [0, ∞) |
| MotionValue | Spec 的 `Damage.MotionValue` SetByCaller | (0, ∞) |
| HitzoneDefense | Target 的 `UMonsterHitzoneComponent::DefenseMultiplier` | (0, ∞)，典型值 0.2~1.5 |
| CriticalRate | `Damage.CritOverride` >= 0 ? 覆写 : 读 Source ASC 的 `CriticalRate` | [-100, 100] |
| DanceMultiplier | 虫棍空中攻击在创建 Spec 时快照；非舞踏攻击默认为 1 | [1, ConfigMax] |

零动作值必须保持零伤害，供反击移动段、纯位移段和非伤害 Notify 使用；“命中反馈”不能靠强制扣 1 点生命实现。

### 3.2 后续完整模型的扩展边界

`FCombatDamageContext` 预留来源类型、伤害类型、物理/属性动作值、斩味、异常、部位伤害与固定伤害字段。Demo 未使用字段保持默认值，由后续独立计算阶段消费。AttackAbility 只负责提供动作固有参数和真实 HitResult，不直接实现斩味或属性公式。

### 3.3 硬直值（规划，当前未输出）

$$\text{Stagger} = \text{BaseStaggerValue} \times \text{StaggerMultiplier} \times \text{HitzoneStaggerRate}$$

| 参数 | 来源 | 范围 |
|------|------|:--:|
| BaseStaggerValue | Spec 的 `Damage.BaseStagger` SetByCaller | [0, ∞) |
| StaggerMultiplier | Source ASC 的 `StaggerMultiplier` Attribute（装备词条加成后） | [0, ∞) |
| HitzoneStaggerRate | Target 的 `UMonsterHitzoneComponent::StaggerRate` | [0, ∞) |

---

## 四、目标执行步骤（伪代码；硬直/GC 部分未实现）

```cpp
void UMHGZDamageExecCalc::Execute_Implementation(
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
        FGameplayTag::RequestGameplayTag("Damage.MotionValue"), true, 0.0f);
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

    // ── 4. 从自定义 EffectContext 的真实 HitResult 读取 Hitzone ──
    float HitzoneDefense = 1.0f;
    float HitzoneStaggerRate = 1.0f;
    const FMHGZGameplayEffectContext* CombatContext =
        FMHGZGameplayEffectContext::Extract(Spec.GetContext());
    const FHitResult* Hit = CombatContext ? CombatContext->GetHitResult() : nullptr;
    const UMHGZMonsterHitzoneComponent* Hitzone = Hit
        ? Cast<UMHGZMonsterHitzoneComponent>(Hit->GetComponent())
        : nullptr;

    if (Hitzone)
    {
        HitzoneDefense = Hitzone->DefenseMultiplier;
        HitzoneStaggerRate = Hitzone->StaggerRate;
    }

    // ── 5. 伤害计算 ──
    float DanceMultiplier = Spec.GetSetByCallerMagnitude(
        FGameplayTag::RequestGameplayTag("Damage.DanceMultiplier"), false, 1.0f);
    float RawDamage = AttackPower * MotionValue * HitzoneDefense * FMath::Max(1.0f, DanceMultiplier);

    // 暴击判定
    bool bCrit = false;
    float EffectiveCritRate = (CritOverride >= 0.f) ? CritOverride : CriticalRate;
    if (EffectiveCritRate > 0.f && FMath::FRandRange(0.f, 100.f) < EffectiveCritRate)
    {
        bCrit = true;
        RawDamage *= 1.25f;
    }

    // 正动作值最低 1；零/负动作值保持 0
    float FinalDamage = (AttackPower > 0.0f && MotionValue > 0.0f)
        ? FMath::Max(1.0f, FMath::FloorToFloat(RawDamage))
        : 0.0f;

    // ── 6. 只写 Meta Attribute，不直接改 Health/播放 Cue/发 Event ──
    const float StaggerValue = FMath::Max(0.f,
        BaseStagger * StaggerMultiplier * HitzoneStaggerRate);
    AddMetaOutput(OutExecutionOutput,
        UMHGZAttributeSet::GetIncomingDamageAttribute(), FinalDamage);
    AddMetaOutput(OutExecutionOutput,
        UMHGZAttributeSet::GetIncomingStaggerAttribute(), StaggerValue);
    AddMetaOutput(OutExecutionOutput,
        UMHGZAttributeSet::GetIncomingCriticalFlagAttribute(), bCrit ? 1.f : 0.f);

    // 必须最后添加。AttributeSet 只在处理 HitSignal 时原子读取并清零以上 Meta。
    AddMetaOutput(OutExecutionOutput,
        UMHGZAttributeSet::GetIncomingHitSignalAttribute(), 1.f);
}
```

`PostGameplayEffectExecute` 收到 `IncomingHitSignal` 后读取本次 Meta，按当前 Health Clamp 得到 `ActualDamage`，随后把四个 Meta 全部清零。硬直事件和 `FMHGZHitFeedbackResult` 都只在这里生成一次。若 MotionValue 为 0，ActualDamage 为 0，但真实命中的反击/纯位移段仍可得到一次零伤害反馈；完全没有 HitResult 的效果不得提交 HitSignal。

UE5.6 的 `GameplayEffect.cpp` 当前按 OutputModifiers 数组插入顺序逐项调用 `InternalExecuteMod`，并在每项后调用 `PostGameplayEffectExecute`；所以 HitSignal 的“最后输出”是实现合同，不只是注释。M2 必须有自动化测试验证一次 ExecCalc 只产生一次反馈，升级引擎后也先跑该测试。

---

## 五、GameplayCue 联动（规划，当前未接通）

### 5.1 HitFeedbackResult 组装时机

| 阶段 | 提供方 | 数据 |
|------|------|------|
| `MakeDamageSpec` | GA/GameplayEffectContext | 真实 HitResult、物理/元素 CueTag、AttackInstanceID |
| `Execute` | ExecCalc | IncomingDamage、IncomingStagger、IncomingCriticalFlag，最后 IncomingHitSignal |
| `PostGameplayEffectExecute` | AttributeSet/结算层 | HitSignal 时原子读取/清零 Meta，合并为一次 `FMHGZHitFeedbackResult` |
| 结算完成 | `UMHGZHitFeedbackRouter` | 显式 Execute 物理、元素、Crit、DamageNumber Cue |

### 5.2 伤害数字读取方式

`GC_Hit_DamageNumber::OnBurst` 从 `FGameplayCueParameters::RawMagnitude` 读取数值，但该字段由 `UMHGZHitFeedbackRouter` 使用最终结算伤害显式填写，不依赖 GAS 从某个 Modifier 自动推断。

```cpp
// GC_Hit_DamageNumber::OnBurst
float DamageValue = Parameters.RawMagnitude;
```

> 木桩有 Health Attribute，因此 Router 能取得最终实际扣血。若未来目标没有 Health Attribute，不显示伪造伤害数字，只保留无伤害命中反馈。

---

## 六、GameplayEffect 配置状态

### UMHGZDamageGameplayEffect（当前原生通用伤害 GE）

| 属性 | 值 | 说明 |
|------|------|------|
| DurationPolicy | Instant | 即时生效 |
| Modifiers | — | **留空**——所有数值由 ExecCalc 通过 `OutExecutionOutput` 写入 |
| ExecCalc | `UMHGZDamageExecCalc` | 构造函数中加入 Execution |
| GameplayCueTags | — | 留空；目标由 HitFeedbackRouter 显式执行，当前 MakeDamageSpec 仍只添加 DynamicAssetTags |

### 猎虫/粉尘伤害

不创建第二套 `GE_KinsectDamage` 资产。武器、猎虫和粉尘统一使用原生 `UMHGZDamageGameplayEffect`；通过 Context.DamageSourceType、CueTag 和 `Damage.AttackPower` 覆写区分来源。M3 已删除猎虫对 `/Game/GameplayEffects/Core/GE_KinsectDamage` 的硬编码加载。

---

## 七、目标接口（M2/M3 迁移）

### 7.1 AttackAbility → MakeDamageSpec

```cpp
// UMHGZAttackAbility::MakeDamageSpec
FGameplayEffectSpecHandle UMHGZAttackAbility::MakeDamageSpec(
    const FHitResult& Hit, int32 SegmentIndex, const FGuid& AttackInstanceID)
{
    const FAttackSegmentConfig& Segment = AttackSegments[SegmentIndex];

    FGameplayEffectContextHandle ContextHandle = ASC->MakeEffectContext();
    FMHGZGameplayEffectContext* Context =
        FMHGZGameplayEffectContext::ExtractMutable(ContextHandle);
    Context->AddHitResult(Hit, true);
    Context->AttackInstanceID = AttackInstanceID;
    Context->SourceActionTag = AbilityIdentityTag;
    Context->DamageSourceType = EDamageSourceType::Weapon;
    Context->HitCueTag = Segment.Damage.HitCueTag;
    if (const auto* Hitzone = Cast<UMHGZMonsterHitzoneComponent>(Hit.GetComponent()))
    {
        Context->HitzoneTag = Hitzone->HitzoneTag;
    }

    FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(
        UMHGZDamageGameplayEffect::StaticClass(), 1.f, ContextHandle);

    // 动作值
    Spec.Data->SetSetByCallerMagnitude(
        FGameplayTag::RequestGameplayTag("Damage.MotionValue"),
        Segment.Damage.MotionValue.GetValueAtLevel(GetAbilityLevel()));

    // 基础破坏值
    Spec.Data->SetSetByCallerMagnitude(
        FGameplayTag::RequestGameplayTag("Damage.BaseStagger"),
        Segment.Damage.BaseStaggerValue.GetValueAtLevel(GetAbilityLevel()));

    return Spec;
}
```

`ApplyDamage` 同样接收 `const FHitResult&`；从碰撞筛选到 GE Apply 全程不降级为 Actor+BoneName。CueTag/HitzoneTag 可以保留 DynamicAssetTag 镜像用于调试，但 Context 才是结算真相源，Router 不扫描 DynamicAssetTags 决定触发。

### 7.2 URes_InsectGlaive → ApplyKinsectDamage

```cpp
void URes_InsectGlaive::ApplyKinsectDamage(
    const FHitResult& Hit, float MotionValue, const FGuid& AttackInstanceID)
{
    UMonsterHitzoneComponent* Hitzone =
        Cast<UMHGZMonsterHitzoneComponent>(Hit.GetComponent());
    AActor* Monster = Hit.GetActor();
    if (!Hitzone || !Monster) return;

    UAbilitySystemComponent* PlayerASC = GetPlayerASC();
    UAbilitySystemComponent* MonsterASC = Monster->FindComponentByClass<UAbilitySystemComponent>();
    if (!PlayerASC || !MonsterASC) return;

    FGameplayEffectContextHandle ContextHandle = PlayerASC->MakeEffectContext();
    FMHGZGameplayEffectContext* Context =
        FMHGZGameplayEffectContext::ExtractMutable(ContextHandle);
    Context->AddHitResult(Hit, true);
    Context->AttackInstanceID = AttackInstanceID;
    Context->DamageSourceType = EDamageSourceType::Kinsect;
    Context->HitzoneTag = Hitzone->HitzoneTag;
    Context->HitCueTag = FGameplayTag::RequestGameplayTag("GameplayCue.Hit.Kinsect");

    FGameplayEffectSpecHandle Spec = PlayerASC->MakeOutgoingSpec(
        UMHGZDamageGameplayEffect::StaticClass(), 1.0f, ContextHandle);

    // ★ 统一使用 Damage.MotionValue（与武器攻击一致）
    Spec.Data->SetSetByCallerMagnitude(
        FGameplayTag::RequestGameplayTag("Damage.MotionValue"), MotionValue);

    // ★ 猎虫攻击力覆写——ExecCalc 读到 >0 时使用此值而非玩家 AttackPower
    Spec.Data->SetSetByCallerMagnitude(
        FGameplayTag::RequestGameplayTag("Damage.AttackPower"),
        GetModifiedKinsectAttackPower());

    PlayerASC->ApplyGameplayEffectSpecToTarget(*Spec.Data, MonsterASC);
}
```

---

## 八、与 GAS 内置行为的关系

| 行为 | 是否依赖 AttributeSet | 说明 |
|------|:--:|------|
| ExecCalc `Execute` 被调用 | ❌ | 无论 Target 有无 AttributeSet，ExecCalc 都会执行 |
| `OutExecutionOutput.AddOutputModifier(Health, …)` | ⚠️ | 若 Target 无 Health Attribute→产生 Warning 日志，不崩溃。扣血跳过 |
| `HandleGameplayEvent(HitStagger)` | ✅ | 当前只可能由 Target `UMHGZAttributeSet::PostGameplayEffectExecute` 广播，且 DamageSpec 尚未自动添加 `Combat.Event.HitStagger` |
| GameplayCue 路由 | — | 当前未接通；代码使用 DynamicAssetTags，且 GC 资产不存在 |
| 伤害数字 Widget 显示 | — | 当前未实现 |

> **木桩场景：** 当前木桩拥有 `UMHGZAttributeSet`，默认最大生命与生命均为 1000，通用伤害会实际扣血；GameplayCue、伤害数字和硬直反馈尚未接通。

---

## 九、目录结构（源码项已存在；Content 项为规划）

```
Source/MHGZ/
├── ActionSystem/
│   ├── MHGZDamageExecCalc.h/cpp      ← UMHGZDamageExecCalc 实现
│   └── MHGZDamageGameplayEffect.h/cpp ← 当前原生通用伤害 GE

Content/
├── GameplayEffects/
│   └── （Demo 不需要 GE_KinsectDamage；全部来源复用原生通用 Damage GE）

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
| ED-0 | Demo 单一 ExecCalc 处理全部伤害来源 | 简化公式统一；武器、猎虫、粉尘和舞踏通过 DamageContext/SetByCaller 区分，后续可增加正式计算阶段 |
| ED-1 | SetByCaller 键名统一为 `Damage.*` 命名空间 | 废弃 `Damage.Value` 和 `Damage.Kinsect.*` 旧名，避免混淆 |
| ED-2 | `Damage.AttackPower` 为可选覆写（-1=读 ASC） | 猎虫伤害传入猎虫攻击力覆写；武器攻击留空让 ExecCalc 读 Source ASC |
| ED-3 | 暴击 GameplayCue 尚未实现 | 暴击判定已在 ExecCalc 中完成，后续应通过受支持的 GameplayCue 输出路径接入，不使用 `const_cast` 修改 Spec |
| ED-4 | 硬直走 `HandleGameplayEvent` 而非 Tag Trigger | 遵循决策 #62——每次调用独立触发，InstancedPerExecution 支持受击连打 |
| ED-5 | 正动作值命中最低 1 点；零/负动作值保持 0 | 纯位移、反击判断和非伤害段不能意外扣血；命中反馈由 Cue/事件表达 |
| ED-6 | HitzoneComponent 查找用遍历而非 Map 缓存 | 怪物部位数 ≤ 20，O(n) 遍历成本 < 1μs。Map 缓存需要维护增删同步逻辑→不值得 |
