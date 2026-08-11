// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AttributeSystem/MHGZWeaponResourceComponent.h"
#include "MHGZGameplayAbility.h"
#include "MHGZM1PlaceholderAbilities.generated.h"

/** Resource-free M1 harness action used to validate FSM transitions and re-entry. */
UCLASS(NotBlueprintable)
class UMHGZM1PlaceholderActionA : public UMHGZGameplayAbility
{
	GENERATED_BODY()
public:
	UMHGZM1PlaceholderActionA();
};

/** Second resource-free M1 harness action used by Idle -> A -> B tests. */
UCLASS(NotBlueprintable)
class UMHGZM1PlaceholderActionB : public UMHGZGameplayAbility
{
	GENERATED_BODY()
public:
	UMHGZM1PlaceholderActionB();
};

/** Deliberately unaffordable action used to verify Commit rollback. */
UCLASS(NotBlueprintable)
class UMHGZM1CommitFailureAbility : public UMHGZGameplayAbility
{
	GENERATED_BODY()
public:
	UMHGZM1CommitFailureAbility();

	virtual bool CommitAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) override;
};

/** Deterministic reservation provider used by the M1 transaction harness. */
UCLASS(NotBlueprintable)
class UMHGZM1ReservationProbeResource : public UMHGZWeaponResourceComponent
{
	GENERATED_BODY()

public:
	virtual bool CanReserveCosts(
		const TArray<FWeaponResourceCostSpec>& Specs) const override;
	virtual bool TryReserveCosts(
		const FWeaponActionToken& ActionToken,
		const TArray<FWeaponResourceCostSpec>& Specs,
		FWeaponResourceCostReservation& OutReservation) override;
	virtual void ReleaseReservation(
		const FWeaponResourceCostReservation& Reservation) override;
	virtual void ConsumeReservedCosts(
		const FWeaponResourceCostReservation& Reservation) override;

	int32 GetReleaseCount() const { return ReleaseCount; }
	int32 GetConsumeCount() const { return ConsumeCount; }
	int32 GetOutstandingReservationCount() const { return ActiveReservationIDs.Num(); }

private:
	uint64 NextReservationID = 1;
	TSet<uint64> ActiveReservationIDs;
	int32 ReleaseCount = 0;
	int32 ConsumeCount = 0;
};
