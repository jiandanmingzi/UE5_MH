// Copyright MHGZ Project. All Rights Reserved.

#include "AnimNotify_IG_AwakenedKinsectEvent.h"

#include "ActionSystem/MHGZAnimNotifyActionResolver.h"
#include "MHGZ.h"

void UAnimNotify_IG_AwakenedKinsectEvent::Notify(
	USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	FWeaponActionToken ActionToken;
	if (!MHGZ::AnimNotify::ResolveAction(MeshComp, EventReference,
		ActionToken, Animation, nullptr, true))
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[AwakenedKinsectEvent] Could not resolve exact active montage action (Mesh=%s, Animation=%s)."),
			*GetNameSafe(MeshComp), *GetNameSafe(Animation));
		return;
	}

	UMHGZAwakenedKinsectAttackAbility* Ability =
		Cast<UMHGZAwakenedKinsectAttackAbility>(ActionToken.AbilityInstance.Get());
	if (!Ability || !Ability->HandleAwakenedKinsectEvent(ActionToken, Event))
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[AwakenedKinsectEvent] Event %d was rejected by the active action (Mesh=%s, Animation=%s)."),
			static_cast<int32>(Event), *GetNameSafe(MeshComp), *GetNameSafe(Animation));
	}
}

#if WITH_EDITOR
FString UAnimNotify_IG_AwakenedKinsectEvent::GetNotifyName_Implementation() const
{
	switch (Event)
	{
	case EAwakenedKinsectAttackEvent::KinsectLaunch:
		return TEXT("IG Awakened Kinsect Launch");
	case EAwakenedKinsectAttackEvent::HunterFlightStart:
		return TEXT("IG Awakened Hunter Flight Start");
	case EAwakenedKinsectAttackEvent::ReleaseFacingCorrection:
		return TEXT("IG Awakened Release Facing Correction");
	default:
		return TEXT("IG Awakened Kinsect Event");
	}
}
#endif
