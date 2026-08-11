// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZM1PlaceholderAbilities.h"

UMHGZM1PlaceholderActionA::UMHGZM1PlaceholderActionA()
{
	StaminaCostPolicy = EAbilityStaminaCostPolicy::None;
	ActivationBlockedTags.AddTag(
		FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Invincible")));
}

UMHGZM1PlaceholderActionB::UMHGZM1PlaceholderActionB()
{
	StaminaCostPolicy = EAbilityStaminaCostPolicy::None;
}

UMHGZM1CommitFailureAbility::UMHGZM1CommitFailureAbility()
{
	StaminaCostPolicy = EAbilityStaminaCostPolicy::None;
	FWeaponResourceCostSpec Cost;
	Cost.CostType = FGameplayTag::RequestGameplayTag(TEXT("WeaponResource.IG.TripleUp"));
	Cost.Amount = FScalableFloat(1.f);
	WeaponResourceCosts.Add(Cost);
}

bool UMHGZM1CommitFailureAbility::CommitAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	FGameplayTagContainer* OptionalRelevantTags)
{
	return false;
}

bool UMHGZM1ReservationProbeResource::CanReserveCosts(
	const TArray<FWeaponResourceCostSpec>& Specs) const
{
	return true;
}

bool UMHGZM1ReservationProbeResource::TryReserveCosts(
	const FWeaponActionToken& ActionToken,
	const TArray<FWeaponResourceCostSpec>& Specs,
	FWeaponResourceCostReservation& OutReservation)
{
	OutReservation = FWeaponResourceCostReservation();
	if (!ActionToken.IsValid())
	{
		return false;
	}
	OutReservation.RuntimeToken = ActionToken.RuntimeToken;
	OutReservation.ActivationSequenceID = ActionToken.ActivationSequenceID;
	if (Specs.IsEmpty())
	{
		return true;
	}
	OutReservation.ReservationID = NextReservationID++;
	ActiveReservationIDs.Add(OutReservation.ReservationID);
	return true;
}

void UMHGZM1ReservationProbeResource::ReleaseReservation(
	const FWeaponResourceCostReservation& Reservation)
{
	if (ActiveReservationIDs.Remove(Reservation.ReservationID) > 0)
	{
		++ReleaseCount;
	}
}

void UMHGZM1ReservationProbeResource::ConsumeReservedCosts(
	const FWeaponResourceCostReservation& Reservation)
{
	if (ActiveReservationIDs.Remove(Reservation.ReservationID) > 0)
	{
		++ConsumeCount;
	}
}
