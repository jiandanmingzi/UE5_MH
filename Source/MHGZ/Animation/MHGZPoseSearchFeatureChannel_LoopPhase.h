// Copyright MHGZ Project. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "PoseSearch/PoseSearchFeatureChannel.h"
#include "MHGZPoseSearchFeatureChannel_LoopPhase.generated.h"

/** Explicit shared-origin cycle phase for Run/Sprint Loop-only banks, independent of continuing-result validity. */
UCLASS(EditInlineNew, meta=(DisplayName="MHGZ Loop Phase"))
class MHGZ_API UMHGZPoseSearchFeatureChannel_LoopPhase : public UPoseSearchFeatureChannel
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category="Settings", meta=(ClampMin="0", ClampMax="0.5"))
	float MaxCycleError = 0.001f;
	/** The separate armed Loop bank reads the armed node's one-shot entry context. */
	UPROPERTY(EditAnywhere, Category="Settings")
	bool bUnsheathed = false;
	/** Seconds of relative foot velocity included in the one-shot entrance score. */
	UPROPERTY(EditAnywhere, Category="Settings", meta=(ClampMin="0", ClampMax="0.1"))
	float EntryVelocityHorizon = 0.02f;
	/** Root-space locomotion velocity is a separate entrance cost, never a persistent gait delay. */
	UPROPERTY(EditAnywhere, Category="Settings", meta=(ClampMin="0", ClampMax="0.1"))
	float EntryRootVelocityHorizon = 0.05f;
	virtual bool Finalize(UPoseSearchSchema* Schema) override;
	virtual void BuildQuery(UE::PoseSearch::FSearchContext& Context) const override;
	virtual bool IsFilterActive() const override { return MaxCycleError > 0.f; }
	virtual bool IsFilterValid(TConstArrayView<float> Pose, TConstArrayView<float> Query,
		int32 PoseIdx, const UE::PoseSearch::FPoseMetadata& Metadata) const override;
#if WITH_EDITOR
	virtual void FillWeights(TArrayView<float> Weights) const override;
	virtual bool IndexAsset(UE::PoseSearch::FAssetIndexer& Indexer) const override;
#endif
	static FVector2D EncodePhase(float Time, float Length);
	/** Raw (unweighted) indexed feet are for entrance selection, not a global PSS cost override. */
	static float EntryMatchCost(TConstArrayView<float> A, TConstArrayView<float> B,
		float FootVelocityHorizon, float RootVelocityHorizon);
private:
	// Deliberately differs from the CDO after Finalize: UE's DDC key omits
	// transient cardinality and default-valued fields, so layout changes need a key.
	UPROPERTY()
	int32 FeatureLayoutVersion = 0;
	UPROPERTY()
	int8 LeftFootIdx = -1;
	UPROPERTY()
	int8 RightFootIdx = -1;
};
