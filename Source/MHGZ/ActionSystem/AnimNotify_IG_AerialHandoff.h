// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_IG_AerialHandoff.generated.h"

/**
 * 空中最早可操作帧 —— **点**通知。
 *
 * 这一帧之前，弧独占胶囊：MovementTask 把 CMC 置于 MOVE_Flying，
 * PhysFlying 不跑重力、不 FindFloor、不 ProcessLanded，弧不可能被一次误判的
 * 触地打断。这一帧之后，CMC 接管，空中招式与落地动画可触发。
 *
 * 这是一个点，不是一个窗口，所以它没有任何时长/区间语义：
 *  - 不需要 begin/end 配对，因此不需要 `MakeNotifyEventID` 那套按事件记账的
 *    token 表（旧 UAnimNotifyState_IG_AerialWindow 需要，因为窗口有开有关）；
 *  - 不需要载荷 —— ability 自己持有 CMC、avatar 与 action token，加参数只会
 *    造出第二个真相源。
 *
 * 实测位置（MHR 实录，取被玩家取消的段的最短时长）：
 *  - AM_IG_WuTa                0.316 s（舞踏，弧长 1.6167）
 *  - AM_IG_HouChengGanTiao     0.783 s（后撑杆跳，弧长 1.7333）
 *  - AM_IG_HouChengGanTiao_W   0.775 s（白灯后撑杆跳，弧长 2.137）
 * 由 UMHGZAerialHandoffSetupCommandlet 写入。
 */
UCLASS(meta = (DisplayName = "IG Aerial Handoff"))
class MHGZ_API UAnimNotify_IG_AerialHandoff : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

#if WITH_EDITOR
	virtual FString GetNotifyName_Implementation() const override;
#endif
};
