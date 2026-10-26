// Copyright MHGZ Project. All Rights Reserved.

#include "AnimNotify_KinsectRecallCommit.h"

#include "MHGZ.h"
#include "MHGZAnimNotifyActionResolver.h"
#include "MHGZInsectGlaiveKinsectAbilities.h"
#include "MHGZSheatheAbility.h"
#include "MHGZInsectGlaiveAbility.h"
#include "AttributeSystem/Res_InsectGlaive.h"

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
		else if (UMHGZSheatheAbility* Sheathe =
			Cast<UMHGZSheatheAbility>(Action.AbilityInstance.Get()))
		{
			Sheathe->CommitKinsectRecall(Action);
		}
		else if (const UMHGZInsectGlaiveAbility* Attack =
			Cast<UMHGZInsectGlaiveAbility>(Action.AbilityInstance.Get());
			Attack && Attack->CoAttack.bEnabled && Attack->IsActionActivationCommitted())
		{
			// 仅结束该通知精确所属动作的协同；不抢占自由猎虫或后继动作。
			if (URes_InsectGlaive* Resource =
				Cast<URes_InsectGlaive>(Action.RuntimeToken.Host->GetResourceProvider()))
			{
				Resource->EndKinsectCoAttack(Action);
			}
		}
	}
	else
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[KinsectRecallCommit] Notify could not resolve its active montage action (Mesh=%s, Animation=%s)."),
			*GetNameSafe(MeshComp), *GetNameSafe(Animation));
	}
}
