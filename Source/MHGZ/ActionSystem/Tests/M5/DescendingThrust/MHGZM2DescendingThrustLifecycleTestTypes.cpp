// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZM2DescendingThrustLifecycleTestTypes.h"

#include "ActionSystem/AbilityTask_MHGZPlayMontageAndWait.h"
#include "Animation/AnimMontage.h"
#include "GameplayEffect.h"
#include "GameplayTask.h"
#include "UObject/UnrealType.h"

FActiveGameplayEffectHandle UMHGZM2RejectingGameplayEffectASC::ApplyGameplayEffectSpecToSelf(
	const FGameplayEffectSpec& GameplayEffect, const FPredictionKey PredictionKey)
{
	if (bRejectIncomingGameplayEffects)
	{
		++RejectedIncomingGameplayEffectCount;
		return FActiveGameplayEffectHandle();
	}
	return Super::ApplyGameplayEffectSpecToSelf(GameplayEffect, PredictionKey);
}

UAnimMontage* UMHGZM2DescendingThrustLifecycleTestAbility::TestMainMontage = nullptr;
UAnimMontage* UMHGZM2DescendingThrustLifecycleTestAbility::TestLandingMontage = nullptr;
TArray<FAttackSegmentConfig>	UMHGZM2DescendingThrustLifecycleTestAbility::TestSegments;

void UMHGZM2DescendingThrustLifecycleTestAbility::ConfigureForTest(
	UAnimMontage* MainMontage, UAnimMontage* InLandingMontage,
	const TArray<FAttackSegmentConfig>& InSegments)
{
	TestMainMontage = MainMontage;
	TestLandingMontage = InLandingMontage;
	TestSegments = InSegments;
}

void UMHGZM2DescendingThrustLifecycleTestAbility::ResetTestConfiguration()
{
	TestMainMontage = nullptr;
	TestLandingMontage = nullptr;
	TestSegments.Reset();
}

void UMHGZM2DescendingThrustLifecycleTestAbility::ApplyTestConfiguration()
{
	AttackMontage = TestMainMontage;
	DefaultEntrySection = TEXT("Start");
	InputTag = FGameplayTag::RequestGameplayTag(TEXT("Input.Weapon.RT"));
	AttackSegments = TestSegments;

	const FObjectPropertyBase* LandingProperty = FindFProperty<FObjectPropertyBase>(
		UMHGZDescendingThrustAbility::StaticClass(), TEXT("LandingMontage"));
	if (LandingProperty)
	{
		LandingProperty->SetObjectPropertyValue_InContainer(this, TestLandingMontage);
	}
}

void UMHGZM2DescendingThrustLifecycleTestAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	ApplyTestConfiguration();
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}

bool UMHGZM2DescendingThrustLifecycleTestAbility::DriveMontageTaskToEndForTest(
	const FName TaskInstanceName)
{
	for (UGameplayTask* Task : ActiveTasks)
	{
		if (Task && Task->GetInstanceName() == TaskInstanceName)
		{
			if (UAbilityTask_MHGZPlayMontageAndWait* MontageCompletionTask =
				Cast<UAbilityTask_MHGZPlayMontageAndWait>(Task))
			{
				MontageCompletionTask->DriveTickForTest(0.0f);
				return true;
			}
		}
	}
	return false;
}
