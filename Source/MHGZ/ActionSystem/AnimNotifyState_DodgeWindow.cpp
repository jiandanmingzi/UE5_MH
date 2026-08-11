// Copyright MHGZ Project. All Rights Reserved.

#include "AnimNotifyState_DodgeWindow.h"
#include "MHGZAnimNotifyActionResolver.h"
#include "MHGZDodgeAbility.h"

void UAnimNotifyState_DodgeWindow::NotifyBegin(USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation, float TotalDuration,
	const FAnimNotifyEventReference& EventReference)
{
	FWeaponActionToken Action;
	if (MHGZ::AnimNotify::ResolveAction(MeshComp, EventReference, Action))
	{
		if (UMHGZDodgeAbility* Dodge = Cast<UMHGZDodgeAbility>(Action.AbilityInstance.Get()))
		{
			Dodge->BeginDodgeWindow(MHGZ::AnimNotify::MakeNotifyEventID(EventReference));
		}
	}
	(void)Animation;
	(void)TotalDuration;
}

void UAnimNotifyState_DodgeWindow::NotifyEnd(USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	FWeaponActionToken Action;
	if (MHGZ::AnimNotify::ResolveAction(MeshComp, EventReference, Action))
	{
		if (UMHGZDodgeAbility* Dodge = Cast<UMHGZDodgeAbility>(Action.AbilityInstance.Get()))
		{
			Dodge->EndDodgeWindow(MHGZ::AnimNotify::MakeNotifyEventID(EventReference));
		}
	}
	(void)Animation;
}
