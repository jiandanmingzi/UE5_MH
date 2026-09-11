// Copyright MHGZ Project. All Rights Reserved.

#include "AnimNotifyState_IG_AdvancingCounter.h"

#include "MHGZAdvancingCounterAbility.h"
#include "MHGZAnimNotifyActionResolver.h"

void UAnimNotifyState_IG_AdvancingCounter::NotifyBegin(
	USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	FWeaponActionToken ActionToken;
	if (MHGZ::AnimNotify::ResolveAction(MeshComp, EventReference, ActionToken, Animation))
	{
		if (UMHGZAdvancingCounterAbility* Ability =
			Cast<UMHGZAdvancingCounterAbility>(ActionToken.AbilityInstance.Get()))
		{
			Ability->BeginAdvancingCounterWindow(
				MHGZ::AnimNotify::MakeNotifyEventID(EventReference), TotalDuration);
		}
	}
}

void UAnimNotifyState_IG_AdvancingCounter::NotifyEnd(
	USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	FWeaponActionToken ActionToken;
	if (MHGZ::AnimNotify::ResolveAction(MeshComp, EventReference, ActionToken, Animation))
	{
		if (UMHGZAdvancingCounterAbility* Ability =
			Cast<UMHGZAdvancingCounterAbility>(ActionToken.AbilityInstance.Get()))
		{
			Ability->EndAdvancingCounterWindow(
				MHGZ::AnimNotify::MakeNotifyEventID(EventReference));
		}
	}
}
