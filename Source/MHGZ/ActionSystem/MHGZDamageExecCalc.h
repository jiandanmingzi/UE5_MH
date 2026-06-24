// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectExecutionCalculation.h"
#include "MHGZDamageExecCalc.generated.h"

/**
 * UExecCalc_Damage — 统一伤害执行计算
 * 单一 ExecCalc 处理全部伤害来源（武器攻击 + 猎虫伤害）
 * 通过 SetByCaller 区分来源参数
 *
 * 伤害公式：AttackPower × MotionValue × HitzoneDefense × CritMultiplier
 * 硬直公式：BaseStagger × StaggerMultiplier × HitzoneStaggerRate
 */
UCLASS()
class UMHGZDamageExecCalc : public UGameplayEffectExecutionCalculation
{
	GENERATED_BODY()

public:
	UMHGZDamageExecCalc();

	virtual void Execute_Implementation(
		const FGameplayEffectCustomExecutionParameters& ExecutionParams,
		FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const override;
};
