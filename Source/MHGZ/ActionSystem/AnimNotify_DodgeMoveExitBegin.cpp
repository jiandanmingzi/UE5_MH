// Copyright MHGZ Project. All Rights Reserved.

#include "AnimNotify_DodgeMoveExitBegin.h"

void UAnimNotify_DodgeMoveExitBegin::Notify(USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	// Compatibility only. The owning UMHGZDodgeAbility releases its movement
	// lock when its active Montage enters MoveExit. This avoids any dependency
	// on a Notify resolving the active Ability from global registry state.
	(void)MeshComp;
	(void)Animation;
	(void)EventReference;
}
