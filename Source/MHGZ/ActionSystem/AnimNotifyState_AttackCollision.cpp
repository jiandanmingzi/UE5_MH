// Copyright MHGZ Project. All Rights Reserved.

#include "AnimNotifyState_AttackCollision.h"
#include "MHGZAnimNotifyActionResolver.h"
#include "MHGZAttackAbility.h"

void UAnimNotifyState_AttackCollision::NotifyBegin(USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation, float TotalDuration,
	const FAnimNotifyEventReference& EventReference)
{
	FWeaponActionToken Action;
	int32 MontageID = INDEX_NONE;
	if (MHGZ::AnimNotify::ResolveAction(MeshComp, EventReference, Action, nullptr, &MontageID, true))
	{
		if (UMHGZAttackAbility* Attack = Cast<UMHGZAttackAbility>(Action.AbilityInstance.Get()))
		{
			Attack->BeginCollisionFromNotify(ConfigIndex, MeshComp, MontageID, EventReference);
		}
	}
	(void)Animation;
	(void)TotalDuration;
}

void UAnimNotifyState_AttackCollision::NotifyTick(USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation, float FrameDeltaTime,
	const FAnimNotifyEventReference& EventReference)
{
	FWeaponActionToken Action;
	int32 MontageID = INDEX_NONE;
	if (MHGZ::AnimNotify::ResolveAction(MeshComp, EventReference, Action, nullptr, &MontageID, true))
	{
		if (UMHGZAttackAbility* Attack = Cast<UMHGZAttackAbility>(Action.AbilityInstance.Get()))
		{
			Attack->TickCollisionFromNotify(ConfigIndex, MeshComp, MontageID, EventReference, FrameDeltaTime);
		}
	}
	(void)Animation;
}

void UAnimNotifyState_AttackCollision::NotifyEnd(USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	FWeaponActionToken Action;
	int32 MontageID = INDEX_NONE;
	if (MHGZ::AnimNotify::ResolveAction(MeshComp, EventReference, Action, nullptr, &MontageID, true))
	{
		if (UMHGZAttackAbility* Attack = Cast<UMHGZAttackAbility>(Action.AbilityInstance.Get()))
		{
			Attack->EndCollisionFromNotify(ConfigIndex, MeshComp, MontageID, EventReference);
		}
	}
	(void)Animation;
}
