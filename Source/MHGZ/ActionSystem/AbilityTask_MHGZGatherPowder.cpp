// Copyright MHGZ Project. All Rights Reserved.

#include "AbilityTask_MHGZGatherPowder.h"

#include "AbilitySystemComponent.h"
#include "AttributeSystem/Res_InsectGlaive.h"

UAbilityTask_MHGZGatherPowder::UAbilityTask_MHGZGatherPowder()
{
	// AbilityTask tick membership is captured when the task is registered.
	bTickingTask = true;
}

UAbilityTask_MHGZGatherPowder* UAbilityTask_MHGZGatherPowder::StartGather(
	UGameplayAbility* OwningAbility, URes_InsectGlaive* Resource,
	const FIGPowderReservationToken& ReservationToken, const float DurationSeconds)
{
	if (!OwningAbility || !Resource || !ReservationToken.IsValid()
		|| !FMath::IsFinite(DurationSeconds) || DurationSeconds <= 0.0f)
	{
		return nullptr;
	}

	UAbilityTask_MHGZGatherPowder* Task =
		NewAbilityTask<UAbilityTask_MHGZGatherPowder>(OwningAbility);
	Task->ResourceComponent = Resource;
	Task->Reservation = ReservationToken;
	Task->Duration = DurationSeconds;
	return Task;
}

void UAbilityTask_MHGZGatherPowder::Activate()
{
	URes_InsectGlaive* Resource = ResourceComponent.Get();
	if (!Ability || !Ability->IsActive() || !Resource
		|| !Resource->UpdatePowderGathering(Reservation, 0.0f))
	{
		FinishFailure();
		return;
	}

	SetWaitingOnAvatar();
}

void UAbilityTask_MHGZGatherPowder::TickTask(const float DeltaTime)
{
	if (bFinished)
	{
		return;
	}

	if (!Ability || !Ability->IsActive() || !ResourceComponent.IsValid()
		|| !FMath::IsFinite(DeltaTime) || DeltaTime < 0.0f)
	{
		FinishFailure();
		return;
	}

	Elapsed = FMath::Min(Duration, Elapsed + DeltaTime);
	const float Progress = Duration > 0.0f
		? FMath::Clamp(Elapsed / Duration, 0.0f, 1.0f) : 1.0f;
	if (!ResourceComponent->UpdatePowderGathering(Reservation, Progress))
	{
		FinishFailure();
		return;
	}

	if (Elapsed >= Duration)
	{
		bFinished = true;
		if (ShouldBroadcastAbilityTaskDelegates())
		{
			OnCompleted.Broadcast();
		}
		EndTask();
		return;
	}

	Super::TickTask(DeltaTime);
}

void UAbilityTask_MHGZGatherPowder::FinishFailure()
{
	if (bFinished)
	{
		return;
	}
	bFinished = true;
	if (URes_InsectGlaive* Resource = ResourceComponent.Get())
	{
		Resource->ReleasePowderReservation(Reservation);
	}
	if (ShouldBroadcastAbilityTaskDelegates())
	{
		OnFailed.Broadcast();
	}
	EndTask();
}

void UAbilityTask_MHGZGatherPowder::OnDestroy(const bool bInOwnerFinished)
{
	if (!bFinished)
	{
		if (URes_InsectGlaive* Resource = ResourceComponent.Get())
		{
			Resource->ReleasePowderReservation(Reservation);
		}
		bFinished = true;
	}
	Super::OnDestroy(bInOwnerFinished);
}
