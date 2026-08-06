// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZDamageGameplayEffect.h"
#include "MHGZDamageExecCalc.h"

UMHGZDamageGameplayEffect::UMHGZDamageGameplayEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;

	FGameplayEffectExecutionDefinition DamageExecution;
	DamageExecution.CalculationClass = UMHGZDamageExecCalc::StaticClass();
	Executions.Add(DamageExecution);
}
