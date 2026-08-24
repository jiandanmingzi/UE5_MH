// Copyright MHGZ Project. All Rights Reserved.

#include "AnimNotify_DodgeExitDecision.h"

void UAnimNotify_DodgeExitDecision::Notify(USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	// Compatibility only. UMHGZDodgeAbility listens to its own active Montage
	// instance's SectionChanged delegate, so selecting an exit never depends on
	// a Notify rediscovering the Ability through the global action registry.
	// Existing assets may retain this Notify safely, but newly authored Dodges
	// must not add it.
	(void)MeshComp;
	(void)Animation;
	(void)EventReference;
}
