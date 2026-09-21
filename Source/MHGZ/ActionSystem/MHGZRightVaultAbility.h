// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActionSystem/MHGZPoleVaultAbility.h"
#include "MHGZRightVaultAbility.generated.h"

/**
 * RT+A 右撑杆跳：撑杆跳在「右」这个方向上的数据预设。
 *
 * 机制全在 `UMHGZPoleVaultAbility` 里，这里**只负责说自己是谁** —— 构造里
 * 调一次 `BuildDirectionProfiles`，把本方向那两条（无白灯 / 白灯）曲线与 clip
 * 灌进 `VaultProfiles`。
 *
 * 向**右**跳：与左撑杆跳同构（共用弧段），只是行进方向相反。
 */
UCLASS(BlueprintType, Blueprintable)
class MHGZ_API UMHGZRightVaultAbility : public UMHGZPoleVaultAbility
{
	GENERATED_BODY()

public:
	UMHGZRightVaultAbility();
};
