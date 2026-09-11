// Copyright MHGZ Project. All Rights Reserved.

#include "AnimNotify_IG_AdvancingChargeStarted.h"

#include "MHGZAdvancingChargeAbility.h"
#include "MHGZAnimNotifyActionResolver.h"

void UAnimNotify_IG_AdvancingChargeStarted::Notify(
	USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	FWeaponActionToken ActionToken;
	if (MHGZ::AnimNotify::ResolveAction(MeshComp, EventReference, ActionToken, Animation))
	{
		if (UMHGZAdvancingChargeAbility* Ability =
			Cast<UMHGZAdvancingChargeAbility>(ActionToken.AbilityInstance.Get()))
		{
			Ability->BeginAdvancingCharge();
		}
	}
}
