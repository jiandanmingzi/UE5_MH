// Copyright MHGZ Project. All Rights Reserved.

#include "AnimNotify_KinsectSendCommit.h"

#include "MHGZ.h"
#include "MHGZAnimNotifyActionResolver.h"
#include "MHGZInsectGlaiveKinsectAbilities.h"

void UAnimNotify_KinsectSendCommit::Notify(USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	FWeaponActionToken Action;
	if (MHGZ::AnimNotify::ResolveAction(MeshComp, EventReference, Action, Animation))
	{
		if (UMHGZSendKinsectAbility* Send =
			Cast<UMHGZSendKinsectAbility>(Action.AbilityInstance.Get()))
		{
			Send->CommitSendKinsect(Action);
		}
		else if (UMHGZDrawAndSendKinsectAbility* DrawAndSend =
			Cast<UMHGZDrawAndSendKinsectAbility>(Action.AbilityInstance.Get()))
		{
			DrawAndSend->CommitSendKinsect(Action);
		}
	}
	else
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[KinsectSendCommit] Notify could not resolve its active montage action (Mesh=%s, Animation=%s)."),
			*GetNameSafe(MeshComp), *GetNameSafe(Animation));
	}
}
