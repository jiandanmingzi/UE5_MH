// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AnimNotifyState_ActionRootMotionPhase.generated.h"

/**
 * Exact ActionToken-scoped Root Motion phase for M4.4.
 *
 * Place it over every authored root-displacement segment.  In-place segments
 * can use the same state with bOwnsMontageRootMotion disabled when they must
 * merely observe raw input for a later handoff.  This Notify never changes
 * gameplay tags, pose, collision, or Motion Matching selection.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Action Root Motion Phase"))
class MHGZ_API UAnimNotifyState_ActionRootMotionPhase : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	/** 该通知窗口是否真正持有蒙太奇根运动；只在实际贡献根运动的片段开启。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Root Motion")
	bool bOwnsMontageRootMotion = true;

	/** 是否记录可移动动作尾段的物理摇杆历史；与禁止移动标签分开，供后续建立待停步交接。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion Matching")
	bool bObserveRawMovementInput = false;

	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation, float TotalDuration,
		const FAnimNotifyEventReference& EventReference) override;

	virtual void NotifyTick(USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation, float FrameDeltaTime,
		const FAnimNotifyEventReference& EventReference) override;

	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
};
