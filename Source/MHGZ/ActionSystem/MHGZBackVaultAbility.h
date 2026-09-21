// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActionSystem/MHGZPoleVaultAbility.h"
#include "MHGZBackVaultAbility.generated.h"

/**
 * RT+A 后撑杆跳：撑杆跳在「后」这个方向上的数据预设。
 *
 * 机制全在 `UMHGZPoleVaultAbility` 里，这里**只负责说自己是谁** —— 构造里
 * 调一次 `BuildDirectionProfiles(EDirectionalInput::Back)`，把本方向那两条
 * （无白灯 / 白灯）曲线与 clip 灌进 `VaultProfiles`。
 *
 * ⚠ **类名刻意保留不改**：蓝图 `GA_IG_HouChengGanTiao` 的 `NativeParentClass`
 * 与资产里序列化的 `/Script/MHGZ.MHGZBackVaultAbility` 都指向它，改名要配
 * redirector，而改名本身不带来任何好处。
 *
 * ⚠ 这是四个方向里**唯一**数据不在生成表里的一个：它的实录不在 `reframework`
 * 采集目录内，`build_vault_curves.py` 扫不到。所以那两条 22 / 24 键的轨迹表是
 * `MHGZPoleVaultAbility.cpp` 里的手抄常量（迁移前叫 `BackVaultTrajectory` /
 * `WhiteBackVaultTrajectory`），值逐字未变。
 */
UCLASS(BlueprintType, Blueprintable)
class MHGZ_API UMHGZBackVaultAbility : public UMHGZPoleVaultAbility
{
	GENERATED_BODY()

public:
	UMHGZBackVaultAbility();
};
