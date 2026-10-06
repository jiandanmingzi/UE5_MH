// Copyright MHGZ Project. All Rights Reserved.

#include "ActionSystem/AnimNotify_IG_EarlyDescendingThrust.h"

#include "ActionSystem/MHGZAirDodgeAbility.h"
#include "ActionSystem/MHGZAnimNotifyActionResolver.h"

void UAnimNotify_IG_EarlyDescendingThrust::Notify(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	FWeaponActionToken ActionToken;
	int32 MontageInstanceID = INDEX_NONE;
	if (!MHGZ::AnimNotify::ResolveAction(MeshComp, EventReference,
		ActionToken, Animation, &MontageInstanceID,
		/* bRequireMontageInstanceContext = */ true))
	{
		return;
	}

	if (UMHGZAirDodgeAbility* AirDodge =
		Cast<UMHGZAirDodgeAbility>(ActionToken.AbilityInstance.Get()))
	{
		AirDodge->NotifyEarlyDescendingThrustWindow(
			ActionToken, MontageInstanceID, Animation);
	}
}

#if WITH_EDITOR
FString UAnimNotify_IG_EarlyDescendingThrust::GetNotifyName_Implementation() const
{
	return TEXT("IG Early Descending Thrust");
}
#endif
