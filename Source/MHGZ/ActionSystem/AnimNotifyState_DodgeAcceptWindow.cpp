// Copyright MHGZ Project. All Rights Reserved.

#include "AnimNotifyState_DodgeAcceptWindow.h"

#include "MHGZAnimNotifyActionResolver.h"
#include "MHGZAttackAbility.h"

void UAnimNotifyState_DodgeAcceptWindow::NotifyBegin(
	USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	FWeaponActionToken ActionToken;
	if (MHGZ::AnimNotify::ResolveAction(MeshComp, EventReference, ActionToken))
	{
		if (UMHGZAttackAbility* Attack = Cast<UMHGZAttackAbility>(
			ActionToken.AbilityInstance.Get()))
		{
			Attack->BeginDodgeAcceptWindow(
				MHGZ::AnimNotify::MakeNotifyEventID(EventReference));
		}
	}
	(void)Animation;
	(void)TotalDuration;
}

void UAnimNotifyState_DodgeAcceptWindow::NotifyEnd(
	USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	FWeaponActionToken ActionToken;
	if (MHGZ::AnimNotify::ResolveAction(MeshComp, EventReference, ActionToken))
	{
		if (UMHGZAttackAbility* Attack = Cast<UMHGZAttackAbility>(
			ActionToken.AbilityInstance.Get()))
		{
			Attack->EndDodgeAcceptWindow(
				MHGZ::AnimNotify::MakeNotifyEventID(EventReference));
		}
	}
	(void)Animation;
}
