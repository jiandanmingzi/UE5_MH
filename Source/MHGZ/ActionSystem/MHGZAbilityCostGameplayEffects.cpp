// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZAbilityCostGameplayEffects.h"

#include "AttributeSystem/MHGZAttributeSet.h"
#include "GameplayTagContainer.h"

UMHGZStaminaCostGameplayEffect::UMHGZStaminaCostGameplayEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;

	FGameplayModifierInfo& Modifier = Modifiers.AddDefaulted_GetRef();
	Modifier.Attribute = UMHGZAttributeSet::GetStaminaAttribute();
	Modifier.ModifierOp = EGameplayModOp::Additive;

	FSetByCallerFloat SetByCaller;
	SetByCaller.DataTag = GetStaminaCostSetByCallerTag();
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(SetByCaller);
}

FGameplayTag UMHGZStaminaCostGameplayEffect::GetStaminaCostSetByCallerTag()
{
	static const FGameplayTag DataTag =
		FGameplayTag::RequestGameplayTag(TEXT("Data.Cost.Stamina"));
	return DataTag;
}

UMHGZCooldownGameplayEffect::UMHGZCooldownGameplayEffect()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	// 占位时长；实际时长由 GA 在 Spec 上 SetDuration 覆盖。
	DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(1.0f));
}
