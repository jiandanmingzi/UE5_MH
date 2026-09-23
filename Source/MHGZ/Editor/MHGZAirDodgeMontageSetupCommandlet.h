// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"
#include "MHGZAirDodgeMontageSetupCommandlet.generated.h"

/**
 * 由 `AS_UnSh_Dash_Air` 生成常驻的 `AM_IG_AirDodge` 单段蒙太奇（M5 阶段 D · 空中回避 id 137）。
 *
 * 为什么不并进 `MHGZWuTaMontageSetup`：那个是**单资产、报告 schema 写死**的，
 * 拧成多路由会毁掉既有报告与产物的对应关系（同 `MHGZPoleVaultMontageSetupCommandlet.h`
 * 里已经写明的理由）。本命令列只烘这一条，报告独立写在 `Saved/_air_dodge_montage.json`。
 *
 * **目标时长刻意取剪辑自身的有效长度**（`play_length / |rate_scale| = 2.3667 / 2.0
 * = 1.18333 s`），不取真值的 1.185 s：
 *  - 剪辑按 120 fps 网格烘成 284 源帧 `rate_scale = 2.0` ⇒ **有效 142 帧**，与 MHR 实测的
 *    142 帧（119.8 Hz）是同一条帧栅格；
 *  - 于是段速率正好 = 1.0（`RequiredSegmentPlayRate` 的定义会把它算成 1）；
 *  - 取 1.185 会让速率变成 0.9995，并把 0.650 s 那条点通知从帧栅格上推走 0.14 帧，
 *    换不到任何东西。
 *
 * 运行（先于 `MHGZAerialHandoffSetup`，顺序不能换 —— 它负责清起手段的孤儿通知）：
 *     UnrealEditor-Cmd.exe <abs>/MHGZ.uproject -run=MHGZAirDodgeMontageSetup \
 *         -unattended -nop4 -nosplash -stdout
 *
 * 与脚本型探针同样把结果写文件 —— commandlet 会吞掉全部 print/UE_LOG 输出。
 */
UCLASS()
class MHGZ_API UMHGZAirDodgeMontageSetupCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMHGZAirDodgeMontageSetupCommandlet();
	virtual int32 Main(const FString& Params) override;
};
