// Copyright MHGZ Project. All Rights Reserved.

#include "AnimNotify_IG_AerialHandoff.h"

#include "MHGZAnimNotifyActionResolver.h"
#include "MHGZInsectGlaiveAbility.h"

void UAnimNotify_IG_AerialHandoff::Notify(
	USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	FWeaponActionToken ActionToken;
	if (MHGZ::AnimNotify::ResolveAction(MeshComp, EventReference, ActionToken, Animation))
	{
		if (UMHGZInsectGlaiveAbility* Ability =
			Cast<UMHGZInsectGlaiveAbility>(ActionToken.AbilityInstance.Get()))
		{
			Ability->NotifyAerialHandoff();
		}
	}
}

#if WITH_EDITOR
FString UAnimNotify_IG_AerialHandoff::GetNotifyName_Implementation() const
{
	return TEXT("IG Aerial Handoff");
}
#endif
