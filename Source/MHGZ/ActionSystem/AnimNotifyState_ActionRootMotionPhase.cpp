// Copyright MHGZ Project. All Rights Reserved.

#include "AnimNotifyState_ActionRootMotionPhase.h"

#include "MHGZAnimNotifyActionResolver.h"
#include "MHGZGameplayAbility.h"

void UAnimNotifyState_ActionRootMotionPhase::NotifyBegin(
	USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	FWeaponActionToken Action;
	if (MHGZ::AnimNotify::ResolveAction(MeshComp, EventReference, Action, Animation))
	{
		if (UMHGZGameplayAbility* Ability =
			Cast<UMHGZGameplayAbility>(Action.AbilityInstance.Get()))
		{
			Ability->BeginActionRootMotionPhase(Action, bOwnsMontageRootMotion,
				bObserveRawMovementInput);
		}
	}
	(void)TotalDuration;
}

void UAnimNotifyState_ActionRootMotionPhase::NotifyTick(
	USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	float FrameDeltaTime, const FAnimNotifyEventReference& EventReference)
{
	FWeaponActionToken Action;
	if (MHGZ::AnimNotify::ResolveAction(MeshComp, EventReference, Action, Animation))
	{
		if (UMHGZGameplayAbility* Ability =
			Cast<UMHGZGameplayAbility>(Action.AbilityInstance.Get()))
		{
			Ability->ObserveActionRootMotionPhase(Action);
		}
	}
	(void)FrameDeltaTime;
}

void UAnimNotifyState_ActionRootMotionPhase::NotifyEnd(
	USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	FWeaponActionToken Action;
	if (MHGZ::AnimNotify::ResolveAction(MeshComp, EventReference, Action, Animation))
	{
		if (UMHGZGameplayAbility* Ability =
			Cast<UMHGZGameplayAbility>(Action.AbilityInstance.Get()))
		{
			Ability->EndActionRootMotionPhase(Action, bOwnsMontageRootMotion,
				bObserveRawMovementInput);
		}
	}
}
