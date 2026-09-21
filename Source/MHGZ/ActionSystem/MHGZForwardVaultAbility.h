// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActionSystem/MHGZPoleVaultAbility.h"
#include "MHGZForwardVaultAbility.generated.h"

/**
 * RT+A 前撑杆跳：撑杆跳在「前」这个方向上的数据预设。
 *
 * 机制全在 `UMHGZPoleVaultAbility` 里，这里**只负责说自己是谁** —— 构造里
 * 调一次 `BuildDirectionProfiles`，把本方向那两条（无白灯 / 白灯）曲线与 clip
 * 灌进 `VaultProfiles`。
 *
 * 向前跳。空摇杆的 RT+A 落到这个方向 —— 靠 `DA_IG_Combo` 里一条 `Direction=None` 的兜底边指向它，C++ 不写特例。
 */
UCLASS(BlueprintType, Blueprintable)
class MHGZ_API UMHGZForwardVaultAbility : public UMHGZPoleVaultAbility
{
	GENERATED_BODY()

public:
	UMHGZForwardVaultAbility();
};
