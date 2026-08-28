// Copyright MHGZ Project. All Rights Reserved.

#include "Animation/MHGZPoseSearchFeatureChannel_MoveGait.h"

#include "Animation/MHGZMotionMatchingAnimInstance.h"
#include "PoseSearch/PoseSearchContext.h"

void UMHGZPoseSearchFeatureChannel_MoveGait::BuildQuery(
	UE::PoseSearch::FSearchContext& SearchContext) const
{
	float MoveGait = 0.0f;
	if (const FChooserEvaluationContext* Context = SearchContext.GetContext(SampleRole))
	{
		if (const UMHGZMotionMatchingAnimInstance* AnimInstance =
			Cast<UMHGZMotionMatchingAnimInstance>(Context->GetFirstObjectParam()))
		{
			MoveGait = AnimInstance->MMMoveGaitQuery;
		}
	}

	UE::PoseSearch::FFeatureVectorHelper::EncodeFloat(SearchContext.EditFeatureVector(),
		GetChannelDataOffset(), MoveGait);
}
