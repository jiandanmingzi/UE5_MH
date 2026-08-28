// Copyright MHGZ Project. All Rights Reserved.

#include "Animation/MHGZPoseSearchFeatureChannel_StopGait.h"

#include "Animation/MHGZMotionMatchingAnimInstance.h"
#include "PoseSearch/PoseSearchContext.h"

void UMHGZPoseSearchFeatureChannel_StopGait::BuildQuery(
	UE::PoseSearch::FSearchContext& SearchContext) const
{
	float StopGait = 0.0f;
	if (const FChooserEvaluationContext* Context = SearchContext.GetContext(SampleRole))
	{
		if (const UMHGZMotionMatchingAnimInstance* AnimInstance =
			Cast<UMHGZMotionMatchingAnimInstance>(Context->GetFirstObjectParam()))
		{
			StopGait = AnimInstance->MMStopGaitQuery;
		}
	}

	UE::PoseSearch::FFeatureVectorHelper::EncodeFloat(SearchContext.EditFeatureVector(),
		GetChannelDataOffset(), StopGait);
}
