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
			Attack && (bRecallFromAnyState || Attack->CoAttack.bEnabled) && Attack->IsActionActivationCommitted())
		{
			// 仍须属于当前精确动作；任意状态召回只在该点显式启用。
			if (URes_InsectGlaive* Resource =
				Cast<URes_InsectGlaive>(Action.RuntimeToken.Host->GetResourceProvider()))
			{
				if (bRecallFromAnyState)
					// 复用玩家收虫规则；Action清理会让操虫斩回槽/原地待机态留在原处。
					Resource->RecallKinsect(EKinsectCommandSource::Player);
				else
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
