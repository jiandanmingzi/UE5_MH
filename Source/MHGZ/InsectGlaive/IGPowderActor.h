// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InsectGlaive/IGPowderTypes.h"
#include "IGPowderActor.generated.h"

class APlayerController;
class URes_InsectGlaive;
class USceneComponent;

/**
 * Non-colliding, runtime-owned generic powder. The Resource component owns all
 * authoritative lifetime and reservation decisions; this actor only stores its
 * immutable origin data and presents Available/Reserved/Consumed state.
 */
UCLASS(BlueprintType)
class MHGZ_API AIGPowderActor : public AActor
{
	GENERATED_BODY()

public:
	AIGPowderActor();

	bool InitializePowder(
		URes_InsectGlaive* InOwnerResource,
		APlayerController* InOwningPlayer,
		const FWeaponRuntimeToken& InRuntimeToken,
		const FGuid& InPowderID,
		const FGuid& InSourceAttackInstanceID,
		const FGuid& InSourceHitInstanceID,
		const FHitResult& InSourceHit,
		EIGPowderSpawnPolicy InSpawnPolicy,
		double InSpawnWorldTime,
		float InLifetime,
		float InKinsectAttackPowerSnapshot,
		float InDamageMultiplierSnapshot,
		float InDamageContribution,
		uint64 InSpawnSequenceID);

	bool IsAvailableFor(const URes_InsectGlaive* Resource,
		const FWeaponRuntimeToken& RuntimeToken, double WorldTime) const;
	bool TryReserve(const FIGPowderReservationToken& Token);
	bool IsReservedBy(const FIGPowderReservationToken& Token) const;
	bool CanCommitReservation(const FIGPowderReservationToken& Token,
		double WorldTime) const;
	void MarkConsumed(const FIGPowderReservationToken& Token,
		EIGPowderTerminalReason Reason = EIGPowderTerminalReason::GatherConsumed);
	bool ReleaseReservation(const FIGPowderReservationToken& Token,
		const FTransform& RestoreTransform, double WorldTime);
	/** Release without teleporting; used when a user command cancels visible gathering. */
	bool ReleaseReservationAtCurrentLocation(const FIGPowderReservationToken& Token,
		double WorldTime);
	bool SetGatherPresentationLocation(const FIGPowderReservationToken& Token,
		const FVector& Location);
	bool RefreshLifetime(const FIGPowderReservationToken& Token,
		double WorldTime, float LifetimeSeconds);

	FIGPowderRuntimeSnapshot MakeSnapshot(double WorldTime) const;
	double GetSpawnWorldTime() const { return SpawnWorldTime; }
	double GetExpireWorldTime() const { return ExpireWorldTime; }
	float GetLifetime() const { return Lifetime; }
	int32 GetLifetimeRevision() const { return LifetimeRevision; }
	float GetKinsectAttackPowerSnapshot() const { return KinsectAttackPowerSnapshot; }
	float GetDamageMultiplierSnapshot() const { return DamageMultiplierSnapshot; }
	uint64 GetSpawnSequenceID() const { return SpawnSequenceID; }
	const FWeaponRuntimeToken& GetRuntimeToken() const { return RuntimeToken; }
	const FGuid& GetPowderID() const { return PowderID; }
	const FGuid& GetSourceHitInstanceID() const { return SourceHitInstanceID; }
	float GetDamageContribution() const { return DamageContribution; }
	EIGPowderState GetPowderState() const { return State; }
	EIGPowderTerminalReason GetTerminalReason() const { return TerminalReason; }
	EIGPowderSpawnPolicy GetSpawnPolicy() const { return SpawnPolicy; }
	URes_InsectGlaive* GetOwnerResource() const { return OwnerResource.Get(); }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	friend class URes_InsectGlaive;
#if WITH_DEV_AUTOMATION_TESTS
	friend class FMHGZM633PowderNaturalExpiryTest;
#endif

	bool IsLifetimeValidAt(double WorldTime) const;
	void ScheduleLifetimeTimer();
	void HandleLifetimeExpired(int32 ExpectedRevision);
	void MarkTerminal(EIGPowderTerminalReason Reason);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Powder", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(Transient)
	TWeakObjectPtr<URes_InsectGlaive> OwnerResource;

	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerController> OwningPlayer;

	UPROPERTY(Transient)
	FWeaponRuntimeToken RuntimeToken;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Powder", meta = (AllowPrivateAccess = "true"))
	FGuid PowderID;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Powder", meta = (AllowPrivateAccess = "true"))
	FGuid SourceAttackInstanceID;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Powder", meta = (AllowPrivateAccess = "true"))
	FGuid SourceHitInstanceID;

	UPROPERTY(Transient)
	FHitResult SourceHit;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Powder", meta = (AllowPrivateAccess = "true"))
	double SpawnWorldTime = 0.0;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Powder", meta = (AllowPrivateAccess = "true"))
	double ExpireWorldTime = 0.0;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Powder", meta = (AllowPrivateAccess = "true"))
	float Lifetime = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Powder", meta = (AllowPrivateAccess = "true"))
	float KinsectAttackPowerSnapshot = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Powder", meta = (AllowPrivateAccess = "true"))
	float DamageMultiplierSnapshot = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Powder", meta = (AllowPrivateAccess = "true"))
	float DamageContribution = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Powder", meta = (AllowPrivateAccess = "true"))
	EIGPowderState State = EIGPowderState::Available;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Powder", meta = (AllowPrivateAccess = "true"))
	EIGPowderTerminalReason TerminalReason = EIGPowderTerminalReason::None;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Powder", meta = (AllowPrivateAccess = "true"))
	EIGPowderSpawnPolicy SpawnPolicy = EIGPowderSpawnPolicy::None;

	FIGPowderReservationToken ReservationToken;
	uint64 SpawnSequenceID = 0;
	int32 LifetimeRevision = 0;
	FTimerHandle LifetimeTimer;
	bool bInitialized = false;
};
