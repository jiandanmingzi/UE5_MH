// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/Tasks/AbilityTask.h"
#include "InsectGlaive/IGPowderTypes.h"
#include "AbilityTask_MHGZGatherPowder.generated.h"

class URes_InsectGlaive;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMHGZGatherPowderDelegate);

/** Tick-driven presentation for one reservation already advanced to Gathering. */
UCLASS()
class MHGZ_API UAbilityTask_MHGZGatherPowder : public UAbilityTask
{
	GENERATED_BODY()

public:
	UAbilityTask_MHGZGatherPowder();

	static UAbilityTask_MHGZGatherPowder* StartGather(
		UGameplayAbility* OwningAbility, URes_InsectGlaive* Resource,
		const FIGPowderReservationToken& ReservationToken, float DurationSeconds);

	UPROPERTY(BlueprintAssignable)
	FMHGZGatherPowderDelegate OnCompleted;

	UPROPERTY(BlueprintAssignable)
	FMHGZGatherPowderDelegate OnFailed;

	void DriveTickForTest(float DeltaTime) { TickTask(DeltaTime); }

protected:
	virtual void Activate() override;
	virtual void TickTask(float DeltaTime) override;
	virtual void OnDestroy(bool bInOwnerFinished) override;

private:
	void FinishFailure();

	TWeakObjectPtr<URes_InsectGlaive> ResourceComponent;
	FIGPowderReservationToken Reservation;
	float Duration = 0.0f;
	float Elapsed = 0.0f;
	bool bFinished = false;
};
