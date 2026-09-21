// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActionSystem/MHGZPoleVaultAbility.h"
#include "MHGZLeftVaultAbility.generated.h"

/**
 * RT+A 左撑杆跳：撑杆跳在「左」这个方向上的数据预设。
 *
 * 机制全在 `UMHGZPoleVaultAbility` 里，这里**只负责说自己是谁** —— 构造里
 * 调一次 `BuildDirectionProfiles`，把本方向那两条（无白灯 / 白灯）曲线与 clip
 * 灌进 `VaultProfiles`。
 *
 * 向**左**跳：朝向锁死不变，整个身体侧向平移出去，落地时还朝前。
 */
UCLASS(BlueprintType, Blueprintable)
class MHGZ_API UMHGZLeftVaultAbility : public UMHGZPoleVaultAbility
{
	GENERATED_BODY()

public:
	UMHGZLeftVaultAbility();
};
