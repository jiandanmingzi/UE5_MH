// Copyright MHGZ Project. All Rights Reserved.

#include "ActionSystem/AnimNotify_DescendingThrustEvent.h"

#include "ActionSystem/MHGZAnimNotifyActionResolver.h"

void UAnimNotify_DescendingThrustEvent::Notify(
	USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	FWeaponActionToken ActionToken;
	int32 MontageInstanceID = INDEX_NONE;
	if (!MHGZ::AnimNotify::ResolveAction(MeshComp, EventReference,
		ActionToken, Animation, &MontageInstanceID, true))
	{
		return;
	}

	if (UMHGZDescendingThrustAbility* Ability =
		Cast<UMHGZDescendingThrustAbility>(ActionToken.AbilityInstance.Get()))
	{
		Ability->HandleMontageEvent(ActionToken, MontageInstanceID, Event);
	}
}

#if WITH_EDITOR
FString UAnimNotify_DescendingThrustEvent::GetNotifyName_Implementation() const
{
	const UEnum* Enum = StaticEnum<EDescendingThrustMontageEvent>();
	return Enum
		? Enum->GetDisplayNameTextByValue(static_cast<int64>(Event)).ToString()
		: TEXT("Descending Thrust Event");
}
#endif
