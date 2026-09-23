// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "MHGZStripPresentationRootMotionCommandlet.generated.h"

/**
 * 把空中表现蒙太奇（下坠 143/157、落地 148、空回本体）的**根运动抽取面**关死，
 * 同时用 bForceRootLock 保住根骨钉在首帧的观感（A9 事项）。
 *
 * 调用：
 *   UnrealEditor-Cmd.exe <Project>.uproject -run=MHGZStripPresentationRootMotion
 * 报告：Saved/_strip_root_motion_report.json（幂等，重复跑结果相同）
 *
 * ## 为什么（PIE 第 5 轮「空回下坠途中速度骤变」根因）
 *
 * 引擎的抽取闸门是 `UAnimSequence::bEnableRootMotion`，两路都只看它：
 * `UAnimMontage::HasRootMotion()`（决定 FAnimMontageInstance::Advance 抽不抽）与
 * 资产玩家 `HandleAssetPlayerTickedInternal` 的 `Accumulate` 点。**恒定
 * (0,0,-111) 的根轨道照样会被 `Accumulate(identity)` 把 `bHasRootMotion` 置真**
 * （FRootMotionMovementParams::Set 对单位变换不设零检查），于是 CMC 的
 * `ConstrainAnimRootMotionVelocity`（Falling 语义：XY 换成动画根速度、Z 保留）
 * 在 137→下坠缝那一帧把水平 900 抹成 ~0 —— 遥测（20260923-001906）逐帧可见：
 * 缝上 vX 899→-0.14、vZ 残差 0.3 匀加速照走，RootBone/胶囊水平同步冻住。
 * 旧录制（20260922-224312）每条自由落体缝同样抹零，不是当轮回归；此前的
 * 空回比对窗口截在 1.185 s（137 蒙太奇全长），缝在窗外没照到。
 *
 * ## 为什么三个字段一起写
 *
 * 1. `bEnableRootMotion = false` —— 抽取关死（根因）。
 * 2. `bForceRootLock = true` —— 抽取关死后**独立**维持根锁定（ResetRootBoneForRootMotion
 *    的 `|| bForceRootLock` 分支），根骨仍钉首帧，观感与现在一致。
 * 3. `bRootMotionSettingsCopiedFromMontage = true` —— 钉死回灌通道：蒙太奇 PostLoad
 *    会按其 DEPRECATED 的 bEnableRootMotionTranslation/Rotation 把序列设置
 *    **强制写回**（EnableRootMotionSettingFromMontage 仅在该标记为 false 时写入，
 *    标记一旦置 true 永不再写）。顺带把 4 条蒙太奇的那两个废弃字段清零，
 *    PostLoad 的 `if (bRootMotionEnabled)` 直接不进 —— 双保险，A9 式静默还原
 *    的两条路都断掉。
 */
UCLASS()
class UMHGZStripPresentationRootMotionCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& Params) override;
};
