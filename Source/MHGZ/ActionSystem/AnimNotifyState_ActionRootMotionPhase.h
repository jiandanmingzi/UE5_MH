// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AnimNotifyState_ActionRootMotionPhase.generated.h"

/**
 * Exact ActionToken-scoped Root Motion phase for M4.4.
 *
 * Place it over every authored root-displacement segment.  In-place segments
 * can use the same state with bOwnsMontageRootMotion disabled when they must
 * merely observe raw input for a later handoff.  This Notify never changes
 * gameplay tags, pose, collision, or Motion Matching selection.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Action Root Motion Phase"))
class MHGZ_API UAnimNotifyState_ActionRootMotionPhase : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	/** True only for the portion that actually contributes Montage Root Motion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Root Motion")
	bool bOwnsMontageRootMotion = true;

	/**
	 * Record physical-stick history for a mobile action tail. This is separate
	 * from BlockMovement and is what can arm PendingStopAtMMHandoff later.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion Matching")
	bool bObserveRawMovementInput = false;

	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation, float TotalDuration,
		const FAnimNotifyEventReference& EventReference) override;

	virtual void NotifyTick(USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation, float FrameDeltaTime,
		const FAnimNotifyEventReference& EventReference) override;

	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
};
