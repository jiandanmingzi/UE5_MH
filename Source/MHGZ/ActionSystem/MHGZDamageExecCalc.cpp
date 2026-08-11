// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZDamageExecCalc.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "MHGZGameplayEffectContext.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"
#include "AbilitySystemComponent.h"

UMHGZDamageExecCalc::UMHGZDamageExecCalc()
{
	// 捕获攻击方属性
	SourceAttackPowerDef.AttributeToCapture = UMHGZAttributeSet::GetAttackPowerAttribute();
	SourceAttackPowerDef.AttributeSource = EGameplayEffectAttributeCaptureSource::Source;
	SourceAttackPowerDef.bSnapshot = true;
	RelevantAttributesToCapture.Add(SourceAttackPowerDef);

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
	float HitzoneDefense = 1.0f;
	float HitzoneStaggerRate = 1.0f;
	if (const UMHGZMonsterHitzoneComponent* Hitzone =
		Cast<UMHGZMonsterHitzoneComponent>(Hit->GetComponent()))
	{
		if (!CombatContext || CombatContext->bUseHitzoneDefense)
		{
			HitzoneDefense = FMath::Max(0.f, Hitzone->DefenseMultiplier);
		}
		HitzoneStaggerRate = FMath::Max(0.f, Hitzone->StaggerRate);
	}

	// ── 4. 读取 Source ASC 的属性 ──
	FAggregatorEvaluateParameters EvalParams;
	EvalParams.SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
	EvalParams.TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();

	float AttackPower = 0.f;
	float CriticalRate = 0.f;
	float StaggerMultiplier = 1.0f;

	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
		SourceAttackPowerDef, EvalParams, AttackPower);
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

	// ── 6. 伤害计算 ──
	float RawDamage = AttackPower * MotionValue * HitzoneDefense;

	// 暴击判定
	bool bCrit = false;
	const float EffectiveCritRate = (CritOverride >= 0.f) ? CritOverride : CriticalRate;
	if (RawDamage > 0.f && EffectiveCritRate > 0.f
		&& FMath::FRandRange(0.f, 100.f) < EffectiveCritRate)
	{
		bCrit = true;
		RawDamage *= 1.25f;
	}
	RawDamage *= FMath::Max(1.0f, DanceMultiplier);

	// 零/负动作值保持 0 伤害；正动作值最低 1 点。
	const float FinalDamage = (AttackPower > 0.0f && MotionValue > 0.0f)
		? FMath::Max(1.0f, FMath::FloorToFloat(RawDamage))
		: 0.0f;

	const float StaggerValue = FMath::Max(0.f,
		BaseStagger * StaggerMultiplier * HitzoneStaggerRate);

	UE_LOG(LogTemp, Log,
		TEXT("[Damage] Source=%s Target=%s Attack=%.2f Motion=%.2f Hitzone=%.2f Final=%.2f Stagger=%.2f"),
		*GetNameSafe(SourceASC->GetAvatarActor()), *GetNameSafe(TargetASC->GetOwnerActor()),
		AttackPower, MotionValue, HitzoneDefense, FinalDamage, StaggerValue);

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
