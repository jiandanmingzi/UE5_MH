// Copyright MHGZ Project. All Rights Reserved.

#include "AnimNotify_SheatheCommit.h"

#include "MHGZAnimNotifyActionResolver.h"
#include "MHGZSheatheAbility.h"

void UAnimNotify_SheatheCommit::Notify(USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	FWeaponActionToken Action;
	if (MHGZ::AnimNotify::ResolveAction(MeshComp, EventReference, Action))
	{
		if (UMHGZSheatheAbility* Sheathe =
			Cast<UMHGZSheatheAbility>(Action.AbilityInstance.Get()))
		{
			Sheathe->CommitSheathe(Action);
		}
	}
	(void)Animation;
}
