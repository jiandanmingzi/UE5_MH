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

	/** 只在显式来源→目标接缝稳住持械挂点；其他片段和蒙太奇默认不参与。 */
	UPROPERTY(EditAnywhere, Category="持械约束", meta=(DisplayName="启用接缝持械约束"))
	bool bEnableWeaponGripConstraint = false;

	/** 启用持械接缝约束时使用的来源姿势序列。 */
	UPROPERTY(EditAnywhere, Category="持械约束", meta=(EditCondition="bEnableWeaponGripConstraint", DisplayName="来源序列"))
	TObjectPtr<UAnimSequenceBase> WeaponGripFromSequence;

	/** 启用持械接缝约束时使用的目标姿势序列。 */
	UPROPERTY(EditAnywhere, Category="持械约束", meta=(EditCondition="bEnableWeaponGripConstraint", DisplayName="目标序列"))
	TObjectPtr<UAnimSequenceBase> WeaponGripToSequence;

	/** 保持Duration后平滑交回原轨道；仅控制辅助骨，不延长动作或输入。 */
	UPROPERTY(EditAnywhere, Category="持械约束", meta=(EditCondition="bEnableWeaponGripConstraint", DisplayName="约束退出时间（秒）", ClampMin="0.001", UIMax="0.1"))
	float WeaponGripReleaseDuration = 1.f / 30.f;
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
	float DesiredWeight = 1.0f;
};

/** Instance-owned, game-thread envelope; only its scalar Alpha is read by the graph. */
struct MHGZ_API FMHGZWeaponGripConstraint
{
	float Alpha = 0.f;
	int64 Serial = 0;
	void Clear();
	void Update(TConstArrayView<FMHGZMontageSegmentBlendSample> Before,
		TConstArrayView<FMHGZMontageSegmentBlendSample> After, float DeltaSeconds);
private:
	TWeakObjectPtr<UAnimMontage> OwnerMontage;
	TWeakObjectPtr<UAnimSequenceBase> TargetSequence;
	int32 InstanceID = INDEX_NONE;
	float Elapsed = 0.f;
	float HoldDuration = 0.f;
	float ReleaseDuration = 0.f;
};

/** Pure transition policy shared by the update hook and directed tests. */
struct MHGZ_API FMHGZMontageSegmentBlend
{
	static void Capture(const TArray<FAnimMontageInstance*>& Instances,
		TArray<FMHGZMontageSegmentBlendSample>& OutSamples);
	static void BuildRequests(TConstArrayView<FMHGZMontageSegmentBlendSample> Before,
		TConstArrayView<FMHGZMontageSegmentBlendSample> After, TMap<FName, float>& OutRequests);
};
