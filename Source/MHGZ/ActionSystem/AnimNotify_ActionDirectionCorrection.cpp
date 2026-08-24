// Copyright MHGZ Project. All Rights Reserved.

#include "AnimNotify_ActionDirectionCorrection.h"

#include "MHGZAnimNotifyActionResolver.h"
#include "MHGZAttackAbility.h"

void UAnimNotify_ActionDirectionCorrection::Notify(USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	FWeaponActionToken Action;
	if (MHGZ::AnimNotify::ResolveAction(MeshComp, EventReference, Action))
	{
		if (UMHGZAttackAbility* Attack =
			Cast<UMHGZAttackAbility>(Action.AbilityInstance.Get()))
		{
			Attack->ApplyInActionDirectionCorrection(Action, MaxCorrectionAngleOverride);
		}
	}
	(void)Animation;
}
