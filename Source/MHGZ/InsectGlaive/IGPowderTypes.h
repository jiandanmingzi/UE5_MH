// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WeaponRuntime/MHGZWeaponRuntimeTypes.h"
#include "IGPowderTypes.generated.h"

class AIGPowderActor;

UENUM(BlueprintType)
enum class EIGPowderState : uint8
{
	Available UMETA(DisplayName = "Available"),
	Reserved UMETA(DisplayName = "Reserved"),
	Consumed UMETA(DisplayName = "Consumed")
};

/** A powder's one and only terminal outcome. */
UENUM(BlueprintType)
enum class EIGPowderTerminalReason : uint8
{
	None UMETA(DisplayName = "None"),
	NaturalExplosion UMETA(DisplayName = "Natural Explosion"),
	GatherConsumed UMETA(DisplayName = "Gather Consumed"),
	DistanceDespawn UMETA(DisplayName = "Distance Despawn"),
	RuntimeShutdown UMETA(DisplayName = "Runtime Shutdown")
};

/** Successful real Kinsect damage route that is allowed to create powder. */
UENUM(BlueprintType)
enum class EIGPowderSpawnPolicy : uint8
{
	None UMETA(DisplayName = "None"),
	MarkedContact UMETA(DisplayName = "Marked Contact"),
	/** Legacy serialized policy: keep the original numeric value and strict piercing contract. */
	AwakenedPiercing UMETA(DisplayName = "Awakened Piercing"),
	/** 黄灯觉虫击：单击/贯通均在每次成功伤害后产粉，无需虫印且绕过普通冷却。 */
	AwakenedEachHit UMETA(DisplayName = "觉虫击黄灯逐击产粉")
};

/** Exact identity for one fixed set of powders reserved by one action activation. */
USTRUCT()
struct FIGPowderReservationToken
{
	GENERATED_BODY()

	FWeaponRuntimeToken RuntimeToken;

	uint32 ActivationSequenceID = 0;

	uint64 ReservationID = 0;

	/** Resource command generation for a post-Montage vortex transaction. */
	uint64 CommandSerial = 0;

	bool IsValid() const
	{
		return RuntimeToken.IsValid() && ActivationSequenceID != 0 && ReservationID != 0;
	}

	bool operator==(const FIGPowderReservationToken& Other) const
	{
		return RuntimeToken == Other.RuntimeToken
			&& ActivationSequenceID == Other.ActivationSequenceID
			&& ReservationID == Other.ReservationID
			&& CommandSerial == Other.CommandSerial;
	}

	bool operator!=(const FIGPowderReservationToken& Other) const
	{
		return !(*this == Other);
	}
};

/** Read-only per-powder data for later UI, telemetry, and transaction audits. */
USTRUCT(BlueprintType)
struct FIGPowderRuntimeSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FGuid PowderID;

	UPROPERTY(BlueprintReadOnly)
	FGuid SourceAttackInstanceID;

	UPROPERTY(BlueprintReadOnly)
	FGuid SourceHitInstanceID;

	UPROPERTY(BlueprintReadOnly)
	FVector Location = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	float RemainingLifetime = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float DamageContribution = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float KinsectAttackPowerSnapshot = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float MotionValueSnapshot = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	EIGPowderState State = EIGPowderState::Available;

	UPROPERTY(BlueprintReadOnly)
	EIGPowderTerminalReason TerminalReason = EIGPowderTerminalReason::None;

	UPROPERTY(BlueprintReadOnly)
	int32 LifetimeRevision = 0;

	UPROPERTY(BlueprintReadOnly)
	EIGPowderSpawnPolicy SpawnPolicy = EIGPowderSpawnPolicy::None;
};

/** Immutable result of committing a powder reservation. */
USTRUCT(BlueprintType)
struct FIGPowderExplosionSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FGuid ExplosionID;

	UPROPERTY(BlueprintReadOnly)
	FGuid Stage1AttackID;

	UPROPERTY(BlueprintReadOnly)
	FGuid Stage2AttackID;

	UPROPERTY(BlueprintReadOnly)
	FVector Center = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	int32 ConsumedCount = 0;

	UPROPERTY(BlueprintReadOnly)
	float TotalDamage = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float Stage1Damage = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float Stage2Damage = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float Radius = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float BaseStagger = 0.0f;
};
