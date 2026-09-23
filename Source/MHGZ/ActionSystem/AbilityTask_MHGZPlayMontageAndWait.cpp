// Copyright MHGZ Project. All Rights Reserved.

#include "AbilityTask_MHGZPlayMontageAndWait.h"

#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AbilitySystemLog.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "GameFramework/Character.h"

/**
 * 判定「位置已到自身长度」的容差（秒）。引擎在末尾把 `Position` 夹到
 * `SectionEnd − KINDA_SMALL_NUMBER/2`（= 长度 − 5e-5），所以容差只要大于它就够；
 * 1e-3 再宽也只有 1 ms —— 比一帧小两个数量级，不会把「还没播完」判成已播完。
 */
static constexpr float MontageEndTolerance = 1e-3f;

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

bool UAbilityTask_MHGZPlayMontageAndWait::HasMontageReachedEnd(
	const float Position, const float PlayLength)
{
	// 不用 `Montage_IsPlaying`：关掉 `bEnableAutoBlendOut` 的实例到末尾时 `bPlaying` 为 false，
	// 那条判据会把「播完了但还活着」误判成「不在播」。**也不看 `DeltaTime`** —— 这里报的是
	// 「已经到长度」这个事实，不是一个提前量；提前量会随帧率漂，事实不会。
	return Position >= PlayLength - MontageEndTolerance;
}

void UAbilityTask_MHGZPlayMontageAndWait::TickTask(const float DeltaTime)
{
	// 关掉 `bEnableAutoBlendOut` 的蒙太奇不会自己终止（引擎的自动淡出整段被跳过），
	// 所以「播到长度」这件事只能由这里如实上报 —— 详见头文件该入口的注释。
	if (bCompleteAtMontageEnd && !bCompletionReported && MontageToPlay)
	{
		const FGameplayAbilityActorInfo* ActorInfo = Ability ? Ability->GetCurrentActorInfo() : nullptr;
		const UAnimInstance* AnimInstancePtr = ActorInfo ? ActorInfo->GetAnimInstance() : nullptr;
		const FAnimMontageInstance* InstancePtr =
			AnimInstancePtr ? AnimInstancePtr->GetActiveInstanceForMontage(MontageToPlay) : nullptr;
		if (InstancePtr && HasMontageReachedEnd(
			InstancePtr->GetPosition(), MontageToPlay->GetPlayLength()))
		{
			bCompletionReported = true;
			if (ShouldBroadcastAbilityTaskDelegates())
			{
				OnCompleted.Broadcast();
			}
			EndTask();
			return;
		}
	}

	Super::TickTask(DeltaTime);
}
