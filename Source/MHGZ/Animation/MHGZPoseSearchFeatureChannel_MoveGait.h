// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PoseSearch/PoseSearchFeatureChannel_Curve.h"
#include "MHGZPoseSearchFeatureChannel_MoveGait.generated.h"

/**
 * Pose Search curve channel whose runtime query is the current requested
 * locomotion family, rather than a curve evaluated from the blended AnimGraph.
 * Indexed animation data is read from MM_MoveGait on each formal candidate.
 */
UCLASS(EditInlineNew, meta=(DisplayName="MHGZ Move Gait Curve Channel"))
class MHGZ_API UMHGZPoseSearchFeatureChannel_MoveGait
	: public UPoseSearchFeatureChannel_Curve
{
	GENERATED_BODY()

public:
	virtual void BuildQuery(UE::PoseSearch::FSearchContext& SearchContext) const override;
};
