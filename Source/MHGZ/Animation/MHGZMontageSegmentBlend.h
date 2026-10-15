// Copyright MHGZ Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimMetaData.h"
#include "MHGZMontageSegmentBlend.generated.h"

class UAnimMontage;
class UAnimSequenceBase;
struct FAnimMontageInstance;

/** Opt-in, cooked Montage metadata. No metadata means existing playback is unchanged. */
UCLASS(EditInlineNew, meta=(DisplayName="MHGZ 蒙太奇内部片段混合"))
class MHGZ_API UMHGZMontageSegmentBlendSettings : public UAnimMetaData
{
	GENERATED_BODY()
public:
	/** 仅对同一蒙太奇实例中不同动画序列的切换请求惯性混合。 */
	UPROPERTY(EditAnywhere, Category="内部片段混合", meta=(DisplayName="启用内部片段混合"))
	bool bEnabled = true;

	/** 实际秒数，不随动画倍率缩放；0 表示关闭。不会增加片段时长或延迟通知。 */
	UPROPERTY(EditAnywhere, Category="内部片段混合", meta=(DisplayName="混合时间（秒）", ClampMin="0.0", UIMax="0.2"))
	float Duration = 0.05f;
};

/** Game-thread snapshots; never retain an FAnimMontageInstance across Montage_Advance. */
struct FMHGZMontageSegmentBlendSample
{
	TWeakObjectPtr<UAnimMontage> Montage;
	TWeakObjectPtr<UAnimSequenceBase> Sequence;
	int32 InstanceID = INDEX_NONE;
	int32 SegmentIndex = INDEX_NONE;
	FName SlotName;
	FName GroupName;
	float Duration = 0.0f;
	float Weight = 0.0f;
};

/** Pure transition policy shared by the update hook and directed tests. */
struct MHGZ_API FMHGZMontageSegmentBlend
{
	static void Capture(const TArray<FAnimMontageInstance*>& Instances,
		TArray<FMHGZMontageSegmentBlendSample>& OutSamples);
	static void BuildRequests(TConstArrayView<FMHGZMontageSegmentBlendSample> Before,
		TConstArrayView<FMHGZMontageSegmentBlendSample> After, TMap<FName, float>& OutRequests);
};
