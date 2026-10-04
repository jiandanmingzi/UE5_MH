// Copyright MHGZ Project. All Rights Reserved.

#include "ActionSystem/AbilityTask_MHGZActionDirectionCorrection.h"

#include "ActionSystem/MHGZGameplayAbility.h"
#include "GameFramework/Character.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

UAbilityTask_MHGZActionDirectionCorrection::UAbilityTask_MHGZActionDirectionCorrection(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bTickingTask = true;
}

UAbilityTask_MHGZActionDirectionCorrection*
UAbilityTask_MHGZActionDirectionCorrection::StartCorrection(
	UGameplayAbility* OwningAbility,
	const FWeaponActionToken& InActionToken,
	ACharacter* InCharacter,
	const float InStartYaw,
	const float InDeltaYaw,
	const float InRequestedDuration,
	const float InMaxYawSpeedDegreesPerSecond,
	const EActionDirectionCorrectionEasing InEasing)
{
	if (!OwningAbility || !InCharacter || !InActionToken.IsValid()
		|| !FMath::IsFinite(InStartYaw) || !FMath::IsFinite(InDeltaYaw)
		|| !FMath::IsFinite(InRequestedDuration) || InRequestedDuration <= 0.0f
		|| !FMath::IsFinite(InMaxYawSpeedDegreesPerSecond)
		|| InMaxYawSpeedDegreesPerSecond < 0.0f)
	{
		return nullptr;
	}

	UAbilityTask_MHGZActionDirectionCorrection* Task =
		NewAbilityTask<UAbilityTask_MHGZActionDirectionCorrection>(OwningAbility);
	Task->ActionToken = InActionToken;
	Task->Character = InCharacter;
	Task->StartYaw = FRotator::NormalizeAxis(InStartYaw);
	Task->DeltaYaw = InDeltaYaw;
	Task->MaxYawSpeedDegreesPerSecond = InMaxYawSpeedDegreesPerSecond;
	Task->Easing = InEasing;
	Task->EffectiveDuration = InRequestedDuration;

	if (InMaxYawSpeedDegreesPerSecond > 0.0f)
	{
		const float PeakSlope = Task->GetMaximumEasingSlope();
		const float DurationRequiredBySpeed =
			FMath::Abs(InDeltaYaw) * PeakSlope / InMaxYawSpeedDegreesPerSecond;
		if (!FMath::IsFinite(DurationRequiredBySpeed))
		{
			return nullptr;
		}
		Task->EffectiveDuration = FMath::Max(InRequestedDuration, DurationRequiredBySpeed);
	}

	Task->ReadyForActivation();
	return Task;
}

void UAbilityTask_MHGZActionDirectionCorrection::Activate()
{
	Super::Activate();

	UMHGZGameplayAbility* OwnerAbility = Cast<UMHGZGameplayAbility>(Ability.Get());
	RuntimeHost = OwnerAbility ? OwnerAbility->GetRuntimeHost() : nullptr;
	UMHGZWeaponRuntimeHostComponent* Host = RuntimeHost.Get();
	if (!OwnerAbility || !OwnerAbility->IsActive()
		|| !OwnerAbility->IsActionActivationCommitted()
		|| OwnerAbility->GetActionToken() != ActionToken
		|| !Host || !Host->AcquireActionRotationOverride(ActionToken))
	{
		EndTask();
		return;
	}

	bDidAcquireRotationOverride = true;
	SetWaitingOnAvatar();
}

bool UAbilityTask_MHGZActionDirectionCorrection::HasValidOwner() const
{
	const UMHGZGameplayAbility* OwnerAbility = Cast<UMHGZGameplayAbility>(Ability.Get());
	const UMHGZWeaponRuntimeHostComponent* Host = RuntimeHost.Get();
	return OwnerAbility && OwnerAbility->IsActive() && OwnerAbility->IsActionActivationCommitted()
		&& OwnerAbility->GetActionToken() == ActionToken
		&& Host && Host->IsTokenCurrent(ActionToken.RuntimeToken)
		&& Host->IsActionRotationOverrideOwnedBy(ActionToken)
		&& Character.IsValid();
}

float UAbilityTask_MHGZActionDirectionCorrection::EvaluateEasing(float Alpha) const
{
	const float T = FMath::Clamp(Alpha, 0.0f, 1.0f);
	switch (Easing)
	{
	case EActionDirectionCorrectionEasing::EaseIn:
		return T * T;
	case EActionDirectionCorrectionEasing::EaseOut:
		return 1.0f - FMath::Square(1.0f - T);
	case EActionDirectionCorrectionEasing::EaseInOut:
		return T * T * (3.0f - 2.0f * T);
	case EActionDirectionCorrectionEasing::Linear:
	default:
		return T;
	}
}

float UAbilityTask_MHGZActionDirectionCorrection::GetMaximumEasingSlope() const
{
	switch (Easing)
	{
	case EActionDirectionCorrectionEasing::EaseIn:
	case EActionDirectionCorrectionEasing::EaseOut:
		return 2.0f;
	case EActionDirectionCorrectionEasing::EaseInOut:
		return 1.5f;
	case EActionDirectionCorrectionEasing::Linear:
	default:
		return 1.0f;
	}
}

void UAbilityTask_MHGZActionDirectionCorrection::TickTask(const float DeltaTime)
{
	Super::TickTask(DeltaTime);
	if (!HasValidOwner())
	{
		EndTask();
		return;
	}

	ACharacter* Avatar = Character.Get();
	if (!Avatar)
	{
		EndTask();
		return;
	}

	const float SafeDeltaTime = FMath::IsFinite(DeltaTime)
		? FMath::Max(0.0f, DeltaTime) : 0.0f;
	ElapsedTime += SafeDeltaTime;
	const float Alpha = EffectiveDuration > 0.0f
		? FMath::Clamp(ElapsedTime / EffectiveDuration, 0.0f, 1.0f)
		: 1.0f;
	const float DesiredYaw = FRotator::NormalizeAxis(
		StartYaw + DeltaYaw * EvaluateEasing(Alpha));

	const FRotator CurrentRotation = Avatar->GetActorRotation();
	const float CurrentYaw = CurrentRotation.Yaw;
	const float RemainingYaw = FMath::FindDeltaAngleDegrees(CurrentYaw, DesiredYaw);
	const float MaxStep = MaxYawSpeedDegreesPerSecond > 0.0f
		? MaxYawSpeedDegreesPerSecond * SafeDeltaTime
		: FMath::Abs(RemainingYaw);
	const float AppliedStep = FMath::Clamp(RemainingYaw, -MaxStep, MaxStep);
	FRotator NextRotation = CurrentRotation;
	NextRotation.Yaw = FRotator::NormalizeAxis(CurrentYaw + AppliedStep);
	Avatar->SetActorRotation(NextRotation);

	if (Alpha >= 1.0f && FMath::Abs(FMath::FindDeltaAngleDegrees(
		Avatar->GetActorRotation().Yaw, DesiredYaw)) <= 0.05f)
	{
		EndTask();
	}
}

void UAbilityTask_MHGZActionDirectionCorrection::OnDestroy(const bool AbilityIsEnding)
{
	if (bDidAcquireRotationOverride)
	{
		if (UMHGZWeaponRuntimeHostComponent* Host = RuntimeHost.Get())
		{
			Host->ReleaseActionRotationOverride(ActionToken);
		}
		bDidAcquireRotationOverride = false;
	}

	Super::OnDestroy(AbilityIsEnding);
}
