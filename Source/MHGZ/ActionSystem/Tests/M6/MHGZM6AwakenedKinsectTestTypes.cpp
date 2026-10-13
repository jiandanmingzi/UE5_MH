// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZM6AwakenedKinsectTestTypes.h"
#include "ActionSystem/MHGZDamageGameplayEffect.h"

#include "ActionSystem/MHGZGameplayAbility.h"

void UMHGZM6AwakenedKinsectAbilityTestDouble::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// This test isolates the production resource-branch hook. The production attack
	// path additionally requires the authored Montage asset and is tested separately.
	UMHGZGameplayAbility::ActivateAbility(
		Handle, ActorInfo, ActivationInfo, TriggerEventData);
}
void UMHGZM6FourHitMarkTestAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// Per-execution configuration; never mutate a CDO or the resident four-hit GA.
	MarkSegmentIndex = 1;
	AttackSegments.SetNum(4);
	for (FAttackSegmentConfig& Segment : AttackSegments)
	{
		Segment.Damage.DamageEffectClass = UMHGZDamageGameplayEffect::StaticClass();
		Segment.Damage.MotionValue = 10.0f;
		Segment.Damage.bRequiresHitToContinue = false;
	}
	UMHGZGameplayAbility::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}
