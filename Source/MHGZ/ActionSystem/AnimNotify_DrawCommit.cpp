// Copyright MHGZ Project. All Rights Reserved.

#include "AnimNotify_DrawCommit.h"

#include "MHGZAnimNotifyActionResolver.h"
#include "MHGZDrawAttackAbility.h"
#include "MHGZInsectGlaiveKinsectAbilities.h"

void UAnimNotify_DrawCommit::Notify(USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	FWeaponActionToken Action;
	if (MHGZ::AnimNotify::ResolveAction(MeshComp, EventReference, Action))
	{
		if (UMHGZDrawAttackAbility* Draw =
			Cast<UMHGZDrawAttackAbility>(Action.AbilityInstance.Get()))
		{
			Draw->CommitDraw(Action);
		}
		else if (UMHGZDrawAndSendKinsectAbility* DrawAndSend =
			Cast<UMHGZDrawAndSendKinsectAbility>(Action.AbilityInstance.Get()))
		{
			DrawAndSend->CommitDraw(Action);
		}
	}
	(void)Animation;
}
