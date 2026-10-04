// Copyright MHGZ Project. All Rights Reserved.

#include "AnimNotify_IG_AerialHandoff.h"

#include "MHGZAnimNotifyActionResolver.h"
#include "MHGZInsectGlaiveAbility.h"
#include "MHGZKinsectSlashAbility.h"

void UAnimNotify_IG_AerialHandoff::Notify(
	USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	FWeaponActionToken ActionToken;
	int32 MontageInstanceID = INDEX_NONE;
	const bool bHasMontageContext =
		MHGZ::AnimNotify::HasMontageInstanceContext(EventReference);
	bool bResolved = MHGZ::AnimNotify::ResolveAction(MeshComp, EventReference,
		ActionToken, Animation, &MontageInstanceID, true);
	if (!bResolved && !bHasMontageContext)
	{
		// Preserve the old generic IG handoff path for legacy abilities whose
		// notify delivery omits UE's montage context. Kinsect Slash itself below
		// remains strict and will never use this asset-only fallback.
		bResolved = MHGZ::AnimNotify::ResolveAction(MeshComp, EventReference,
			ActionToken, Animation, &MontageInstanceID);
	}
	if (bResolved)
	{
		if (UMHGZKinsectSlashAbility* KinsectSlash =
			Cast<UMHGZKinsectSlashAbility>(ActionToken.AbilityInstance.Get()))
		{
			if (!bHasMontageContext)
			{
				return;
			}
			KinsectSlash->NotifyAerialHandoff(ActionToken, MontageInstanceID);
		}
		else if (UMHGZInsectGlaiveAbility* Ability =
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
