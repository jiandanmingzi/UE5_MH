// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZDamageExecCalc.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "MHGZGameplayEffectContext.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"
#include "AbilitySystemComponent.h"

namespace
{
float NonnegativeFinite(float Value)
{
	return FMath::IsFinite(Value) ? FMath::Max(0.0f, Value) : 0.0f;
}

// MHWI ordinary-hit channels round separately; a truly positive sub-1 value
// keeps 1, but a zero hitzone/multiplier does not manufacture damage.
float RoundDamageChannel(float Value)
{
	return FMath::IsFinite(Value) && Value > 0.0f
		? FMath::Max(1.0f, FMath::RoundToFloat(Value)) : 0.0f;
}
}

UMHGZDamageExecCalc::UMHGZDamageExecCalc()
{
	// 捕获攻击方属性
	SourceAttackPowerDef.AttributeToCapture = UMHGZAttributeSet::GetAttackPowerAttribute();
	SourceAttackPowerDef.AttributeSource = EGameplayEffectAttributeCaptureSource::Source;
	SourceAttackPowerDef.bSnapshot = true;
	RelevantAttributesToCapture.Add(SourceAttackPowerDef);
	SourceElementPowerDef.AttributeToCapture = UMHGZAttributeSet::GetElementPowerAttribute();
	SourceElementPowerDef.AttributeSource = EGameplayEffectAttributeCaptureSource::Source;
	SourceElementPowerDef.bSnapshot = true;
	RelevantAttributesToCapture.Add(SourceElementPowerDef);

	SourceCriticalRateDef.AttributeToCapture = UMHGZAttributeSet::GetCriticalRateAttribute();
	SourceCriticalRateDef.AttributeSource = EGameplayEffectAttributeCaptureSource::Source;
	SourceCriticalRateDef.bSnapshot = true;
	RelevantAttributesToCapture.Add(SourceCriticalRateDef);

	SourceStaggerMultiplierDef.AttributeToCapture = UMHGZAttributeSet::GetStaggerMultiplierAttribute();
	SourceStaggerMultiplierDef.AttributeSource = EGameplayEffectAttributeCaptureSource::Source;
	SourceStaggerMultiplierDef.bSnapshot = true;
	RelevantAttributesToCapture.Add(SourceStaggerMultiplierDef);
}

void UMHGZDamageExecCalc::Execute_Implementation(
	const FGameplayEffectCustomExecutionParameters& ExecutionParams,
	FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const
{
	// ── 1. 获取 Source 和 Target ──
	const FGameplayEffectSpec& Spec = ExecutionParams.GetOwningSpec();
	UAbilitySystemComponent* SourceASC = ExecutionParams.GetSourceAbilitySystemComponent();
	UAbilitySystemComponent* TargetASC = ExecutionParams.GetTargetAbilitySystemComponent();

	if (!SourceASC || !TargetASC) return;

	// ── 2. 真实 HitResult 是结算前提：没有命中就没有反馈信号 ──
	const FMHGZGameplayEffectContext* CombatContext =
		FMHGZGameplayEffectContext::ExtractEffectContext(Spec.GetContext());
	const FHitResult* Hit = CombatContext ? CombatContext->GetHitResult() : nullptr;
	if (!Hit || !Hit->GetComponent())
	{
		return;
	}

	// ── 3. 从 Context 的真实命中组件读取肉质；不再扫描 DynamicAssetTags ──
	float PhysicalHitzone = 100.0f;
	float ElementHitzone = 0.0f;
	float HitzoneStaggerRate = 1.0f;
	const bool bWeaponDamage = CombatContext->DamageSourceType == EMHGZDamageSourceType::Weapon;
	const bool bKinsectDamage = CombatContext->DamageSourceType == EMHGZDamageSourceType::Kinsect;
	if (const UMHGZMonsterHitzoneComponent* Hitzone =
		UMHGZMonsterHitzoneComponent::Resolve(Hit->GetComponent()))
	{
		if (!CombatContext || CombatContext->bUseHitzoneDefense)
		{
			const float* TypedPhysical = (bWeaponDamage || bKinsectDamage)
				? Hitzone->PhysicalHitzoneMultipliers.Find(CombatContext->PhysicalDamageType) : nullptr;
			PhysicalHitzone = NonnegativeFinite(TypedPhysical ? *TypedPhysical : Hitzone->DefenseMultiplier);
		}
		if (bWeaponDamage)
		{
			if (const float* Element = Hitzone->ElementHitzoneMultipliers.Find(
				CombatContext->WeaponDamageProfile.ElementType))
			{
				ElementHitzone = NonnegativeFinite(*Element);
			}
		}
		HitzoneStaggerRate = NonnegativeFinite(Hitzone->StaggerRate);
	}

	// ── 4. 读取 Source ASC 的属性 ──
	FAggregatorEvaluateParameters EvalParams;
	EvalParams.SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
	EvalParams.TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();

	float AttackPower = 0.f;
	float ElementPower = 0.f;
	float CriticalRate = 0.f;
	float StaggerMultiplier = 1.0f;

	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
		SourceAttackPowerDef, EvalParams, AttackPower);
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
		SourceElementPowerDef, EvalParams, ElementPower);
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
		SourceCriticalRateDef, EvalParams, CriticalRate);
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
		SourceStaggerMultiplierDef, EvalParams, StaggerMultiplier);

	// ── 5. 从 Spec 读取 SetByCaller（缺失按默认值：MotionValue 缺失=0） ──
	const float MotionValue = Spec.GetSetByCallerMagnitude(
		FGameplayTag::RequestGameplayTag(TEXT("Damage.MotionValue")), true, 0.0f);
	const float BaseStagger = Spec.GetSetByCallerMagnitude(
		FGameplayTag::RequestGameplayTag(TEXT("Damage.BaseStagger")), true, 0.f);
	const float AttackPowerOverride = Spec.GetSetByCallerMagnitude(
		FGameplayTag::RequestGameplayTag(TEXT("Damage.AttackPower")), false, -1.f);
	const float CritOverride = Spec.GetSetByCallerMagnitude(
		FGameplayTag::RequestGameplayTag(TEXT("Damage.CritOverride")), false, -1.f);
	const float DanceMultiplier = Spec.GetSetByCallerMagnitude(
		FGameplayTag::RequestGameplayTag(TEXT("Damage.DanceMultiplier")), false, 1.0f);

	// 猎虫伤害 → 使用覆写的攻击力
	if (AttackPowerOverride >= 0.f)
	{
		AttackPower = AttackPowerOverride;
	}

	// ── 6. Weapon 物理/属性独立计算；其他来源不继承玩家武器参数 ──
	AttackPower = NonnegativeFinite(AttackPower);
	ElementPower = NonnegativeFinite(ElementPower);
	const float SafeMotionValue = NonnegativeFinite(MotionValue);
	const float SafeDanceMultiplier = FMath::IsFinite(DanceMultiplier)
		? FMath::Max(1.0f, DanceMultiplier) : 1.0f;
	float PhysicalDamage = 0.0f;
	if (bWeaponDamage || bKinsectDamage)
	{
		PhysicalDamage = AttackPower * (SafeMotionValue / 100.0f)
			* (PhysicalHitzone / 100.0f);
	}
	else if (CombatContext->DamageSourceType == EMHGZDamageSourceType::Powder)
	{
		PhysicalDamage = AttackPower * (SafeMotionValue / 100.0f);
	}
	else
	{
		// DummyAttack is a separate incoming-damage protocol: its MotionValue is a
		// legacy direct multiplier, while physical hitzones now use the 0..100 unit.
		PhysicalDamage = AttackPower * SafeMotionValue * (PhysicalHitzone / 100.0f);
	}
	float ElementDamage = 0.0f;
	float FinalDamage = 0.0f;
	bool bCrit = false;

	const float RequestedCritRate = (CritOverride >= 0.f) ? CritOverride : CriticalRate;
	const float EffectiveCritRate = FMath::IsFinite(RequestedCritRate)
		? FMath::Clamp(RequestedCritRate, -100.0f, 100.0f) : 0.0f;
	const FMHGZWeaponDamageProfile& Profile = CombatContext->WeaponDamageProfile;
	if (bWeaponDamage)
	{
		if (Profile.IsValid() && SafeMotionValue > 0.0f)
		{
			const float PhysicalMultiplier = NonnegativeFinite(Spec.GetSetByCallerMagnitude(
				FGameplayTag::RequestGameplayTag(TEXT("Damage.PhysicalMultiplier")), false, 1.0f));
			const float ElementMultiplier = NonnegativeFinite(Spec.GetSetByCallerMagnitude(
				FGameplayTag::RequestGameplayTag(TEXT("Damage.ElementMultiplier")), false, 1.0f));
			const float ElementMotionValue = NonnegativeFinite(Spec.GetSetByCallerMagnitude(
				FGameplayTag::RequestGameplayTag(TEXT("Damage.ElementMotionValue")), false, 1.0f));
			PhysicalDamage *= Profile.PhysicalSharpnessMultiplier * PhysicalMultiplier * SafeDanceMultiplier;
			const bool bHasElement = Profile.ElementType != EMHGZElementType::None
				&& ElementPower > 0.0f && ElementMotionValue > 0.0f
				&& ElementHitzone > 0.0f && Profile.ElementSharpnessMultiplier > 0.0f
				&& ElementMultiplier > 0.0f;
			const bool bCanCrit = PhysicalDamage > 0.0f || bHasElement;
			const bool bAffinityProc = bCanCrit && FMath::Abs(EffectiveCritRate) > 0.0f
				&& FMath::FRand() * 100.0f < FMath::Abs(EffectiveCritRate);
			bCrit = bAffinityProc && EffectiveCritRate > 0.0f;
			const bool bNegativeCrit = bAffinityProc && EffectiveCritRate < 0.0f;
			PhysicalDamage *= bCrit ? Profile.CriticalDamageMultiplier : (bNegativeCrit ? 0.75f : 1.0f);
			if (bHasElement)
			{
				// 属性不乘物理动作值或舞踏倍率；普通会心=1，负会心不削减属性。
				const float EffectiveElement = RoundDamageChannel(
					ElementPower * (bCrit ? Profile.CriticalElementMultiplier : 1.0f));
				ElementDamage = EffectiveElement * ElementMotionValue * Profile.ElementSharpnessMultiplier
					* ElementHitzone * ElementMultiplier;
			}
			FinalDamage = RoundDamageChannel(PhysicalDamage) + RoundDamageChannel(ElementDamage);
		}
	}
	else if (bKinsectDamage || CombatContext->DamageSourceType == EMHGZDamageSourceType::Powder)
	{
		// Kinsect and powder use their own attack-power/action-value formulas; they
		// do not inherit hunter affinity, sharpness, element, or Dance modifiers.
		FinalDamage = AttackPower > 0.0f && SafeMotionValue > 0.0f && FMath::IsFinite(PhysicalDamage)
			? FMath::Max(1.0f, FMath::FloorToFloat(PhysicalDamage)) : 0.0f;
	}
	else
	{
		// 保留木桩入站攻击算法与 Floor 取整。
		bCrit = PhysicalDamage > 0.0f && EffectiveCritRate > 0.0f
			&& FMath::FRand() * 100.0f < EffectiveCritRate;
		PhysicalDamage *= (bCrit ? 1.25f : 1.0f) * SafeDanceMultiplier;
		FinalDamage = AttackPower > 0.0f && SafeMotionValue > 0.0f && FMath::IsFinite(PhysicalDamage)
			? FMath::Max(1.0f, FMath::FloorToFloat(PhysicalDamage)) : 0.0f;
	}

	const float StaggerValue = FMath::Max(0.f,
		BaseStagger * StaggerMultiplier * HitzoneStaggerRate);

	UE_LOG(LogTemp, Log,
		TEXT("[Damage] Source=%s Target=%s Attack=%.2f Motion=%.2f PhysicalHitzone=%.2f Physical=%.3f ElementPower=%.2f ElementHitzone=%.3f Element=%.3f Crit=%d Final=%.2f Stagger=%.2f"),
		*GetNameSafe(SourceASC->GetAvatarActor()), *GetNameSafe(TargetASC->GetOwnerActor()),
		AttackPower, MotionValue, PhysicalHitzone, PhysicalDamage, ElementPower, ElementHitzone,
		ElementDamage, bCrit ? 1 : 0, FinalDamage, StaggerValue);

	// ── 7. 只写 Meta Attribute，不直接改 Health/播放 Cue/发 Event ──
	// 顺序合同（UE5.6 按插入顺序逐项执行并在每项后调用 PostGameplayEffectExecute）：
	// Damage → Stagger → CriticalFlag → HitSignal（必须最后）。
	OutExecutionOutput.AddOutputModifier(
		FGameplayModifierEvaluatedData(
			UMHGZAttributeSet::GetIncomingDamageAttribute(),
			EGameplayModOp::Additive,
			FinalDamage));
	OutExecutionOutput.AddOutputModifier(
		FGameplayModifierEvaluatedData(
			UMHGZAttributeSet::GetIncomingStaggerAttribute(),
			EGameplayModOp::Additive,
			StaggerValue));
	OutExecutionOutput.AddOutputModifier(
		FGameplayModifierEvaluatedData(
			UMHGZAttributeSet::GetIncomingCriticalFlagAttribute(),
			EGameplayModOp::Additive,
			bCrit ? 1.f : 0.f));
	OutExecutionOutput.AddOutputModifier(
		FGameplayModifierEvaluatedData(
			UMHGZAttributeSet::GetIncomingHitSignalAttribute(),
			EGameplayModOp::Additive,
			1.f));
}
