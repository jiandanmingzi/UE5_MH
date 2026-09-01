// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "WeaponRuntime/MHGZWeaponRuntimeTypes.h"
#include "AnimNotify_MotionMatchingHandoff.generated.h"

/**
 * One authored safe-frame boundary from a functional Montage to an Exit PSD.
 * It resolves only the exact `(Mesh, MontageInstanceID)` ActionToken supplied
 * by UE, and delegates all validation/ownership work to that Action.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Motion Matching Handoff"))
class MHGZ_API UAnimNotify_MotionMatchingHandoff : public UAnimNotify
{
	GENERATED_BODY()

public:
	/** Candidate family to pass to the E4.2 Exit PSD/Chooser; never an animation name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion Matching")
	EMHGZMotionMatchingHandoffType HandoffType = EMHGZMotionMatchingHandoffType::None;

	virtual void Notify(USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
};
