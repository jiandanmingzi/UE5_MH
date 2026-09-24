// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"

#include "MHGZPMMNotifyTrackRepairCommandlet.generated.h"

/**
 * Re-homes the Pose Search control notifies of any AnimSequence whose track name
 * drifted away from the project's `PoseSearchControl` convention.
 *
 * 背景：`MHGZ.PMM.Assets.PoseSearchControlNotifies` 要求 MM Start/Stop 序列的
 * `PoseSearchBlockTransition` / `PoseSearchOverrideContinuingPoseCostBias` 挂在一条名为
 * `PoseSearchControl` 的 notify 轨上（`UMHGZPMMAssetFixupCommandlet` 就是建这条轨的工具，
 * `UMHGZE4ExitAssetSetupCommandlet` 用同名常量）。实测有 4 条序列把通知挂在按类名自动命名的
 * 轨上（`PoseSearchBlock` / `PoseSearchCostBias`），于是审计红。轨名对引擎没有语义
 * （PoseSearch 读通知**类**），但项目的两处工具都把它当约定标记用 ⇒ 该修的是资产。
 *
 * ⚠ **只改轨名 / 只改通知的 `TrackIndex`**，不碰通知类别、起止时间与曲线。**不要**改成
 * 重跑 `UMHGZPMMAssetFixupCommandlet`：它的 Stop 块起点是固定 `0.12`，而测试按 PMM-7.1
 * 从 `MM_DistanceToStop` 的提交键推导生成停步的起点，重跑会把那批时序改坏（该工具已相对
 * 测试过期，是一条独立待办）。
 *
 * 默认只报告；加 `-Apply` 才写回资产。
 *
 * ⚠ Python（`-run=pythonscript`）路线**不通**：`UAnimSequenceBase::Notifies` /
 * `AnimNotifyTracks` 在 Python 反射里是 protected，`get_editor_property("notifies")` 会直接
 * 报错 —— 而 pythonscript 又吞掉 print，所以那种脚本会**静默地「零发现」**，看着像「没问题」。
 * 这是本工具用 C++ commandlet 的原因（2026-09-24 实测）。
 *
 *     UnrealEditor-Cmd.exe <abs>/MHGZ.uproject -run=MHGZPMMNotifyTrackRepair \
 *         -unattended -nop4 -nosplash -NullRHI -stdout -log [-Apply]
 */
UCLASS()
class MHGZ_API UMHGZPMMNotifyTrackRepairCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMHGZPMMNotifyTrackRepairCommandlet();

	virtual int32 Main(const FString& Params) override;
};
