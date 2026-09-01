// Copyright MHGZ Project. All Rights Reserved.

#include "AnimNotify_MotionMatchingHandoff.h"

#include "MHGZAnimNotifyActionResolver.h"
#include "MHGZGameplayAbility.h"

void UAnimNotify_MotionMatchingHandoff::Notify(USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	FWeaponActionToken Action;
	if (MHGZ::AnimNotify::ResolveAction(MeshComp, EventReference, Action, Animation))
	{
		if (UMHGZGameplayAbility* Ability =
			Cast<UMHGZGameplayAbility>(Action.AbilityInstance.Get()))
		{
			Ability->HandleMotionMatchingHandoff(Action, HandoffType);
		}
	}
}
