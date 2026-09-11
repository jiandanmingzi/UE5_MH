// Copyright MHGZ Project. All Rights Reserved.

#include "AbilityTask_MHGZPlayMontageAndWait.h"

#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AbilitySystemLog.h"
#include "Animation/AnimInstance.h"
#include "GameFramework/Character.h"

UAbilityTask_MHGZPlayMontageAndWait*
UAbilityTask_MHGZPlayMontageAndWait::CreatePlayMontageAndWaitProxy(
	UGameplayAbility* OwningAbility, FName TaskInstanceName,
	UAnimMontage* InMontageToPlay, float InRate, FName InStartSection,
	float InBlendInTime, bool bInStopWhenAbilityEnds,
	float InAnimRootMotionTranslationScale, float InStartTimeSeconds,
	bool bInAllowInterruptAfterBlendOut)
{
	UAbilitySystemGlobals::NonShipping_ApplyGlobalAbilityScaler_Rate(InRate);
	UAbilityTask_MHGZPlayMontageAndWait* Task =
		NewAbilityTask<UAbilityTask_MHGZPlayMontageAndWait>(OwningAbility, TaskInstanceName);
	Task->MontageToPlay = InMontageToPlay;
	Task->Rate = InRate;
	Task->StartSection = InStartSection;
	Task->BlendInTime = FMath::Max(0.0f, InBlendInTime);
	Task->bStopWhenAbilityEnds = bInStopWhenAbilityEnds;
	Task->AnimRootMotionTranslationScale = InAnimRootMotionTranslationScale;
	Task->bAllowInterruptAfterBlendOut = bInAllowInterruptAfterBlendOut;
	Task->StartTimeSeconds = InStartTimeSeconds;
	return Task;
}

void UAbilityTask_MHGZPlayMontageAndWait::Activate()
{
	if (!Ability)
	{
		return;
	}

	bool bPlayedMontage = false;
	if (UMHGZAbilitySystemComponent* ASC =
		Cast<UMHGZAbilitySystemComponent>(AbilitySystemComponent.Get()))
	{
		const FGameplayAbilityActorInfo* ActorInfo = Ability->GetCurrentActorInfo();
		UAnimInstance* AnimInstance = ActorInfo ? ActorInfo->GetAnimInstance() : nullptr;
		if (AnimInstance && ASC->PlayMontageWithBlendIn(Ability,
			Ability->GetCurrentActivationInfo(), MontageToPlay, Rate, StartSection,
			StartTimeSeconds, BlendInTime) > 0.0f)
		{
			// Playback may synchronously interrupt an old ability and run arbitrary
			// game code. Do not bind callbacks to a task that was ended meanwhile.
			if (!ShouldBroadcastAbilityTaskDelegates())
			{
				return;
			}

			InterruptedHandle = Ability->OnGameplayAbilityCancelled.AddUObject(this,
				&UAbilityTask_PlayMontageAndWait::OnGameplayAbilityCancelled);

			BlendedInDelegate.BindUObject(this,
				&UAbilityTask_PlayMontageAndWait::OnMontageBlendedIn);
			AnimInstance->Montage_SetBlendedInDelegate(BlendedInDelegate, MontageToPlay);

			BlendingOutDelegate.BindUObject(this,
				&UAbilityTask_PlayMontageAndWait::OnMontageBlendingOut);
			AnimInstance->Montage_SetBlendingOutDelegate(BlendingOutDelegate, MontageToPlay);

			MontageEndedDelegate.BindUObject(this,
				&UAbilityTask_PlayMontageAndWait::OnMontageEnded);
			AnimInstance->Montage_SetEndDelegate(MontageEndedDelegate, MontageToPlay);

			ACharacter* Character = Cast<ACharacter>(GetAvatarActor());
			if (Character && (Character->GetLocalRole() == ROLE_Authority
				|| (Character->GetLocalRole() == ROLE_AutonomousProxy
					&& Ability->GetNetExecutionPolicy()
						== EGameplayAbilityNetExecutionPolicy::LocalPredicted)))
			{
				Character->SetAnimRootMotionTranslationScale(AnimRootMotionTranslationScale);
			}
			bPlayedMontage = true;
		}
	}

	if (!bPlayedMontage)
	{
		ABILITY_LOG(Warning,
			TEXT("MHGZ PlayMontageWithBlendIn failed in Ability %s for Montage %s; Task %s."),
			*GetNameSafe(Ability), *GetNameSafe(MontageToPlay), *InstanceName.ToString());
		if (ShouldBroadcastAbilityTaskDelegates())
		{
			OnCancelled.Broadcast();
		}
	}

	SetWaitingOnAvatar();
}
