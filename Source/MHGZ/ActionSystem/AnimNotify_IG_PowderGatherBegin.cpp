// Copyright MHGZ Project. All Rights Reserved.

#include "AnimNotify_IG_PowderGatherBegin.h"

#include "ActionSystem/MHGZAnimNotifyActionResolver.h"
#include "ActionSystem/MHGZInsectGlaiveAbility.h"
#include "MHGZ.h"

void UAnimNotify_IG_PowderGatherBegin::Notify(
	USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	FWeaponActionToken ActionToken;
	if (!MHGZ::AnimNotify::ResolveAction(MeshComp, EventReference,
		ActionToken, Animation, nullptr, true))
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[PowderGatherBegin] Could not resolve exact active montage action (Mesh=%s, Animation=%s)."),
			*GetNameSafe(MeshComp), *GetNameSafe(Animation));
		return;
	}

	UMHGZInsectGlaiveAbility* Ability =
		Cast<UMHGZInsectGlaiveAbility>(ActionToken.AbilityInstance.Get());
	if (!Ability || !Ability->HandlePowderGatherBegin(ActionToken))
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[PowderGatherBegin] Active action rejected the gather event (Mesh=%s, Animation=%s)."),
			*GetNameSafe(MeshComp), *GetNameSafe(Animation));
	}
}

#if WITH_EDITOR
FString UAnimNotify_IG_PowderGatherBegin::GetNotifyName_Implementation() const
{
	return TEXT("IG Powder Gather Begin");
}
#endif
