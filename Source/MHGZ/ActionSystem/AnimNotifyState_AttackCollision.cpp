// Copyright MHGZ Project. All Rights Reserved.

#include "AnimNotifyState_AttackCollision.h"
#include "MHGZAnimNotifyActionResolver.h"
#include "MHGZAttackAbility.h"

void UAnimNotifyState_AttackCollision::NotifyBegin(USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation, float TotalDuration,
	const FAnimNotifyEventReference& EventReference)
{
	FWeaponActionToken Action;
	if (MHGZ::AnimNotify::ResolveAction(MeshComp, EventReference, Action))
	{
		if (UMHGZAttackAbility* Attack = Cast<UMHGZAttackAbility>(Action.AbilityInstance.Get()))
		{
			Attack->EnableCollision(ConfigIndex);
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
	if (MHGZ::AnimNotify::ResolveAction(MeshComp, EventReference, Action))
	{
		if (UMHGZAttackAbility* Attack = Cast<UMHGZAttackAbility>(Action.AbilityInstance.Get()))
		{
			Attack->TickCollision(ConfigIndex, FrameDeltaTime);
		}
	}
	(void)Animation;
}

void UAnimNotifyState_AttackCollision::NotifyEnd(USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	FWeaponActionToken Action;
	if (MHGZ::AnimNotify::ResolveAction(MeshComp, EventReference, Action))
	{
		if (UMHGZAttackAbility* Attack = Cast<UMHGZAttackAbility>(Action.AbilityInstance.Get()))
		{
			// NotifyTick is not guaranteed on the exit frame. Sweep the last authored
			// movement interval before retiring this still-open window.
			Attack->TickCollision(ConfigIndex, 0.0f);
			// Damage callbacks can end/re-enter an action. Never retire a new action's window.
			if (Attack->GetActionToken() == Action) { Attack->DisableCollision(ConfigIndex); }
		}
	}
	(void)Animation;
}
