// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AnimNotifyState_AttackCollision.generated.h"

/**
 * UAnimNotifyState_AttackCollision — 攻击碰撞窗口
 * NotifyBegin → 调用 GA 的 EnableCollision(SegmentIndex)
 * NotifyEnd   → 调用 GA 的 DisableCollision()
 */
UCLASS(BlueprintType, meta = (DisplayName = "Attack Collision"))
class UAnimNotifyState_AttackCollision : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	/** 段索引——对应 AttackSegments 数组 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attack")
	int32 ConfigIndex = 0;

	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		float TotalDuration, const FAnimNotifyEventReference& EventReference) override;

	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
};
