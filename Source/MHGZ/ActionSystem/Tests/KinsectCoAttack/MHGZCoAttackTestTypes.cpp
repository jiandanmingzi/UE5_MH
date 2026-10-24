// Copyright MHGZ Project. All Rights Reserved.
#include "MHGZCoAttackTestTypes.h"
#include "ActionSystem/MHGZDamageGameplayEffect.h"
#include "Animation/AnimMontage.h"

UMHGZCoAttackTestAbility::UMHGZCoAttackTestAbility()
{
	StaminaCostPolicy = EAbilityStaminaCostPolicy::None;
	MaxCorrectionAngle = 0.f;
	CoAttack.bEnabled = true;
	for (int32 I = 0; I < 3; ++I)
	{
		auto& S = AttackSegments.AddDefaulted_GetRef();
		S.Damage.DamageEffectClass = UMHGZDamageGameplayEffect::StaticClass();
		S.Damage.MotionValue = 10.f;
		auto& R = S.Collision.TraceRegions.AddDefaulted_GetRef();
		R.EndSocketName = TEXT("IG_FrontTip"); R.Radius = 5.f;
		CoAttack.SegmentMotionValues.Add(I, 10.f);
	}
}

void UMHGZCoAttackTestAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo Info,
	const FGameplayEventData* Event)
{
	AttackMontage = NewObject<UAnimMontage>(this);
	Super::ActivateAbility(Handle, ActorInfo, Info, Event);
}

void UMHGZCoAttackTestAbility::OnAttackCollisionWindowOpened(const int32 Segment, const uint64 Occurrence)
{
	++OpenCount;
	Super::OnAttackCollisionWindowOpened(Segment, Occurrence);
	if (bCloseOnOpen) DisableCollision(Segment);
}

void UMHGZCoAttackTestAbility::OnAttackCollisionWindowRetired(const int32 Segment, const uint64 Occurrence)
{
	++CloseCount;
	Super::OnAttackCollisionWindowRetired(Segment, Occurrence);
}
