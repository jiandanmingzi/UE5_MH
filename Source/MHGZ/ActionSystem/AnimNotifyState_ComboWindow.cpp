// Copyright MHGZ Project. All Rights Reserved.

#include "AnimNotifyState_ComboWindow.h"
#include "MHGZAbilitySystemComponent.h"
#include "MHGZAnimNotifyActionResolver.h"
#include "MHGZCharacter.h"
#include "MHGZComboCoordinatorAbility.h"

namespace
{
	UGA_WeaponComboCoordinator* ResolveCoordinator(USkeletalMeshComponent* MeshComp)
	{
		const AMHGZCharacter* Character =
			MeshComp ? Cast<AMHGZCharacter>(MeshComp->GetOwner()) : nullptr;
		UMHGZAbilitySystemComponent* ASC = Character
			? Cast<UMHGZAbilitySystemComponent>(Character->GetAbilitySystemComponent()) : nullptr;
		return ASC ? ASC->GetActiveComboCoordinator() : nullptr;
	}
}

void UAnimNotifyState_ComboWindow::NotifyBegin(USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation, float TotalDuration,
	const FAnimNotifyEventReference& EventReference)
{
	FWeaponActionToken Action;
	if (MHGZ::AnimNotify::ResolveAction(MeshComp, EventReference, Action))
	{
		if (UGA_WeaponComboCoordinator* Coordinator = ResolveCoordinator(MeshComp))
		{
			Coordinator->OpenComboWindow(Action,
				MHGZ::AnimNotify::MakeNotifyEventID(EventReference));
		}
	}
	(void)Animation;
	(void)TotalDuration;
}

void UAnimNotifyState_ComboWindow::NotifyEnd(USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	FWeaponActionToken Action;
	if (MHGZ::AnimNotify::ResolveAction(MeshComp, EventReference, Action))
	{
		if (UGA_WeaponComboCoordinator* Coordinator = ResolveCoordinator(MeshComp))
		{
			Coordinator->CloseComboWindow(Action,
				MHGZ::AnimNotify::MakeNotifyEventID(EventReference));
		}
	}
	(void)Animation;
}
