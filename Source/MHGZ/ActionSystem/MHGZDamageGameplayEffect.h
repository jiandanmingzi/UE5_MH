// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "MHGZDamageGameplayEffect.generated.h"

/**
 * 第一阶段通用瞬时伤害 GE。
 *
 * 直接挂载 UMHGZDamageExecCalc，攻击 GA 蓝图只需将 DamageEffectClass
 * 选择为本类，无需为每个招式重复创建和配置 GE 蓝图资产。
 */
UCLASS(BlueprintType)
class UMHGZDamageGameplayEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UMHGZDamageGameplayEffect();
};
