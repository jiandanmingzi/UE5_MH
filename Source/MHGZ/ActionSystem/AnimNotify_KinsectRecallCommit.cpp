// Copyright MHGZ Project. All Rights Reserved.

#include "AnimNotify_KinsectRecallCommit.h"

#include "MHGZ.h"
#include "MHGZAnimNotifyActionResolver.h"
#include "MHGZInsectGlaiveKinsectAbilities.h"

void UAnimNotify_KinsectRecallCommit::Notify(USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	FWeaponActionToken Action;
	if (MHGZ::AnimNotify::ResolveAction(MeshComp, EventReference, Action, Animation))
	{
		if (UMHGZRecallKinsectAbility* Recall =
			Cast<UMHGZRecallKinsectAbility>(Action.AbilityInstance.Get()))
		{
			Recall->CommitRecallKinsect(Action);
		}
	}
	else
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[KinsectRecallCommit] Notify could not resolve its active montage action (Mesh=%s, Animation=%s)."),
			*GetNameSafe(MeshComp), *GetNameSafe(Animation));
	}
}
