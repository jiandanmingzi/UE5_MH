// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PoseSearch/PoseSearchFeatureChannel_Curve.h"
#include "MHGZPoseSearchFeatureChannel_StopGait.generated.h"

/**
 * Pose Search curve channel whose query value is produced by the project's
 * Motion Matching AnimInstance instead of by the currently evaluated pose.
 *
 * Indexed animation data still comes from CurveName (MM_StopGait).  This is
 * deliberately native rather than a BPSC so PMM-7 can install and validate
 * the channel without a manual Blueprint graph step.
 */
UCLASS(EditInlineNew, meta=(DisplayName="MHGZ Stop Gait Curve Channel"))
class MHGZ_API UMHGZPoseSearchFeatureChannel_StopGait
	: public UPoseSearchFeatureChannel_Curve
{
	GENERATED_BODY()

public:
	virtual void BuildQuery(UE::PoseSearch::FSearchContext& SearchContext) const override;
};
