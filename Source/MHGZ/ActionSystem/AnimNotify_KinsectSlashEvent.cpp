// Copyright MHGZ Project. All Rights Reserved.

#include "ActionSystem/AnimNotify_KinsectSlashEvent.h"

#include "ActionSystem/MHGZAnimNotifyActionResolver.h"

void UAnimNotify_KinsectSlashEvent::Notify(
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

	if (UMHGZKinsectSlashAbility* Ability =
		Cast<UMHGZKinsectSlashAbility>(ActionToken.AbilityInstance.Get()))
	{
		Ability->HandleMontageEvent(ActionToken, MontageInstanceID, Event);
	}
}

#if WITH_EDITOR
FString UAnimNotify_KinsectSlashEvent::GetNotifyName_Implementation() const
{
	const UEnum* Enum = StaticEnum<EKinsectSlashMontageEvent>();
	return Enum ? Enum->GetDisplayNameTextByValue(static_cast<int64>(Event)).ToString()
		: TEXT("Kinsect Slash Event");
}
#endif
