// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "ActionSystem/MHGZDescendingThrustAbility.h"
#include "MHGZM2DescendingThrustLifecycleTestTypes.generated.h"

/** Test target ASC that can explicitly reject an otherwise valid incoming GE. */
UCLASS()
class UMHGZM2RejectingGameplayEffectASC : public UAbilitySystemComponent
{
	GENERATED_BODY()

public:
	bool bRejectIncomingGameplayEffects = false;
	int32 RejectedIncomingGameplayEffectCount = 0;

	virtual FActiveGameplayEffectHandle ApplyGameplayEffectSpecToSelf(
		const FGameplayEffectSpec& GameplayEffect,
		FPredictionKey PredictionKey = FPredictionKey()) override;
};

/**
 * Runtime-lifecycle fixture only. It injects transient Montage/segment fixtures
 * into the actual InstancedPerExecution object immediately before production
 * ActivateAbility runs; it does not replace production validation or behavior.
 * Resident asset wiring remains an editor/M2 asset-audit responsibility.
 */
UCLASS()
class UMHGZM2DescendingThrustLifecycleTestAbility
	: public UMHGZDescendingThrustAbility
{
	GENERATED_BODY()

public:
	static void ConfigureForTest(UAnimMontage* MainMontage,
		UAnimMontage* InLandingMontage,
		const TArray<FAttackSegmentConfig>& InSegments);
	static void ResetTestConfiguration();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;
	bool DriveMontageTaskToEndForTest(FName TaskInstanceName);

private:
	void ApplyTestConfiguration();

	static UAnimMontage* TestMainMontage;
	static UAnimMontage* TestLandingMontage;
	static TArray<FAttackSegmentConfig> TestSegments;
};
