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
	/** 循环相位允许的最大误差；用于限制脚相搜索偏离目标周期的范围。 */
	UPROPERTY(EditAnywhere, Category="Settings", meta=(ClampMin="0", ClampMax="0.5"))
	float MaxCycleError = 0.001f;
	/** 是否读取持刀节点独立的一次性循环入口上下文。 */
	UPROPERTY(EditAnywhere, Category="Settings")
	bool bUnsheathed = false;
	/** 一次性入口评分采样双脚相对速度的时间跨度，单位为秒。 */
	UPROPERTY(EditAnywhere, Category="Settings", meta=(ClampMin="0", ClampMax="0.1"))
	float EntryVelocityHorizon = 0.02f;
	/** 入口评分中根空间移动速度的独立采样跨度，单位为秒；不形成持续挡位延迟。 */
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
	/** 特征布局缓存版本；布局变化时参与DDC键，避免默认值或临时基数未进入缓存键导致旧索引复用。 */
	UPROPERTY()
	int32 FeatureLayoutVersion = 0;
	/** 索引布局中左脚骨骼的缓存编号。 */
	UPROPERTY()
	int8 LeftFootIdx = -1;
	/** 索引布局中右脚骨骼的缓存编号。 */
	UPROPERTY()
	int8 RightFootIdx = -1;
};
