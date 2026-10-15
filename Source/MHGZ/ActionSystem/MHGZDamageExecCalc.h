// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectAttributeCaptureDefinition.h"
#include "GameplayEffectExecutionCalculation.h"
#include "MHGZDamageExecCalc.generated.h"

/**
 * UExecCalc_Damage — 统一伤害执行计算
 * 单一 ExecCalc 处理全部伤害来源（武器攻击 + 猎虫伤害）
 * 通过 SetByCaller 区分来源参数
 *
 * Weapon: 分别计算物理/属性（独立斩味、肉质、动作倍率和会心），分别取整后相加。
 * Weapon / Kinsect 使用百分数单位：MotionValue=10 表示10，物理肉质=45表示45；两者各自除100。
 * Powder 使用 KinsectAttackPower × MotionValue/100，不乘肉质；DummyAttack保留独立输入合同。
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

private:
	FGameplayEffectAttributeCaptureDefinition SourceAttackPowerDef;
	FGameplayEffectAttributeCaptureDefinition SourceElementPowerDef;
	FGameplayEffectAttributeCaptureDefinition SourceCriticalRateDef;
	FGameplayEffectAttributeCaptureDefinition SourceStaggerMultiplierDef;
};
