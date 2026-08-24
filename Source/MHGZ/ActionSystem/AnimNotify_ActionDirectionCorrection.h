// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_ActionDirectionCorrection.generated.h"

/** Exact one-frame direct-Yaw correction for the active attack Montage instance. */
UCLASS(BlueprintType, meta = (DisplayName = "Action Direction Correction"))
class MHGZ_API UAnimNotify_ActionDirectionCorrection : public UAnimNotify
{
	GENERATED_BODY()

public:
	/** -1 uses the attack GA's MaxCorrectionAngle; 0 intentionally disables this notify. */
	UPROPERTY(EditAnywhere, Category = "Direction Correction",
		meta = (ClampMin = "-1.0", UIMin = "-1.0"))
	float MaxCorrectionAngleOverride = -1.f;

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
};
