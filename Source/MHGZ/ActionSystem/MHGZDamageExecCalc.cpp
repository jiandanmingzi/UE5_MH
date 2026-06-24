// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZDamageExecCalc.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"
#include "AbilitySystemComponent.h"

// 硬编码硬直/霸体等级映射
static int32 GetStaggerLevel(FGameplayTag Tag)
{
	static TMap<FGameplayTag, int32> Map;
	if (Map.IsEmpty())
	{
		Map.Add(FGameplayTag::RequestGameplayTag(TEXT("Combat.Stagger.Light")), 1);
		Map.Add(FGameplayTag::RequestGameplayTag(TEXT("Combat.Stagger.Medium")), 2);
		Map.Add(FGameplayTag::RequestGameplayTag(TEXT("Combat.Stagger.Heavy")), 3);
	}
	const int32* Found = Map.Find(Tag);
	return Found ? *Found : 0;
}

static int32 GetPoiseLevel(FGameplayTag Tag)
{
	static TMap<FGameplayTag, int32> Map;
	if (Map.IsEmpty())
	{
		Map.Add(FGameplayTag::RequestGameplayTag(TEXT("Combat.Poise.Light")), 1);
		Map.Add(FGameplayTag::RequestGameplayTag(TEXT("Combat.Poise.Medium")), 2);
		Map.Add(FGameplayTag::RequestGameplayTag(TEXT("Combat.Poise.Heavy")), 3);
		Map.Add(FGameplayTag::RequestGameplayTag(TEXT("Combat.Poise.Super")), 4);
	}
	const int32* Found = Map.Find(Tag);
	return Found ? *Found : 0;
}

UMHGZDamageExecCalc::UMHGZDamageExecCalc()
{
	// 捕获攻击方属性
	FGameplayEffectAttributeCaptureDefinition AttackPowerDef;
	AttackPowerDef.AttributeToCapture = UMHGZAttributeSet::GetAttackPowerAttribute();
	AttackPowerDef.AttributeSource = EGameplayEffectAttributeCaptureSource::Source;
	AttackPowerDef.bSnapshot = true;
	RelevantAttributesToCapture.Add(AttackPowerDef);

	FGameplayEffectAttributeCaptureDefinition CriticalRateDef;
	CriticalRateDef.AttributeToCapture = UMHGZAttributeSet::GetCriticalRateAttribute();
	CriticalRateDef.AttributeSource = EGameplayEffectAttributeCaptureSource::Source;
	CriticalRateDef.bSnapshot = true;
	RelevantAttributesToCapture.Add(CriticalRateDef);

	FGameplayEffectAttributeCaptureDefinition StaggerMultDef;
	StaggerMultDef.AttributeToCapture = UMHGZAttributeSet::GetStaggerMultiplierAttribute();
	StaggerMultDef.AttributeSource = EGameplayEffectAttributeCaptureSource::Source;
	StaggerMultDef.bSnapshot = true;
	RelevantAttributesToCapture.Add(StaggerMultDef);

	// 目标属性（用于扣血）
	FGameplayEffectAttributeCaptureDefinition HealthDef;
	HealthDef.AttributeToCapture = UMHGZAttributeSet::GetHealthAttribute();
	HealthDef.AttributeSource = EGameplayEffectAttributeCaptureSource::Target;
	HealthDef.bSnapshot = false;
	RelevantAttributesToCapture.Add(HealthDef);
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

	AActor* TargetActor = TargetASC->GetOwnerActor();

	// ── 2. 读取 Source ASC 的属性 ──
	FAggregatorEvaluateParameters EvalParams;
	EvalParams.SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
	EvalParams.TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();

	float AttackPower = 0.f;
	float CriticalRate = 0.f;
	float StaggerMultiplier = 1.0f;

	// 使用 AttemptCalculateCapturedAttributeMagnitude
	const TArray<FGameplayEffectAttributeCaptureDefinition>& AttrDefs = RelevantAttributesToCapture;

	for (const FGameplayEffectAttributeCaptureDefinition& CaptureDef : AttrDefs)
	{
		if (CaptureDef.AttributeToCapture == UMHGZAttributeSet::GetAttackPowerAttribute() &&
			CaptureDef.AttributeSource == EGameplayEffectAttributeCaptureSource::Source)
		{
			ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(CaptureDef, EvalParams, AttackPower);
		}
		else if (CaptureDef.AttributeToCapture == UMHGZAttributeSet::GetCriticalRateAttribute() &&
			CaptureDef.AttributeSource == EGameplayEffectAttributeCaptureSource::Source)
		{
			ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(CaptureDef, EvalParams, CriticalRate);
		}
		else if (CaptureDef.AttributeToCapture == UMHGZAttributeSet::GetStaggerMultiplierAttribute() &&
			CaptureDef.AttributeSource == EGameplayEffectAttributeCaptureSource::Source)
		{
			ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(CaptureDef, EvalParams, StaggerMultiplier);
		}
	}

	// ── 3. 从 Spec 读取 SetByCaller ──
	const float MotionValue = Spec.GetSetByCallerMagnitude(
		FGameplayTag::RequestGameplayTag(TEXT("Damage.MotionValue")), true, 1.0f);
	const float BaseStagger = Spec.GetSetByCallerMagnitude(
		FGameplayTag::RequestGameplayTag(TEXT("Damage.BaseStagger")), true, 0.f);
	const float AttackPowerOverride = Spec.GetSetByCallerMagnitude(
		FGameplayTag::RequestGameplayTag(TEXT("Damage.AttackPower")), true, -1.f);
	const float CritOverride = Spec.GetSetByCallerMagnitude(
		FGameplayTag::RequestGameplayTag(TEXT("Damage.CritOverride")), true, -1.f);

	// 猎虫伤害 → 使用覆写的攻击力
	if (AttackPowerOverride > 0.f)
	{
		AttackPower = AttackPowerOverride;
	}

	// ── 4. 从 DynamicAssetTags 读取 Hitzone 信息 ──
	float HitzoneDefense = 1.0f;
	float HitzoneStaggerRate = 1.0f;
	FGameplayTag HitzoneTag;

	for (const FGameplayTag& Tag : Spec.GetDynamicAssetTags())
	{
		if (Tag.MatchesTag(FGameplayTag::RequestGameplayTag(TEXT("Hitzone"))))
		{
			HitzoneTag = Tag;
			break;
		}
	}

	if (HitzoneTag.IsValid() && TargetActor)
	{
		TArray<UMHGZMonsterHitzoneComponent*> Hitzones;
		TargetActor->GetComponents<UMHGZMonsterHitzoneComponent>(Hitzones);
		for (UMHGZMonsterHitzoneComponent* HZ : Hitzones)
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
	const float EffectiveCritRate = (CritOverride >= 0.f) ? CritOverride : CriticalRate;
	if (EffectiveCritRate > 0.f && FMath::FRandRange(0.f, 100.f) < EffectiveCritRate)
	{
		bCrit = true;
		RawDamage *= 1.25f;
	}

	// 至少 1 点伤害
	const float FinalDamage = FMath::Max(1.0f, RawDamage);

	// ── 6. 写入扣血 ──
	OutExecutionOutput.AddOutputModifier(
		FGameplayModifierEvaluatedData(
			UMHGZAttributeSet::GetHealthAttribute(),
			EGameplayModOp::Additive,
			-FinalDamage));

	// ── 7. 暴击时标记（GC 标签由 MakeDamageSpec 的 AddDynamicAssetTag 处理） ──
	// ── 8. 硬直计算（硬直事件由 PostGameplayEffectExecute 广播） ──
}
