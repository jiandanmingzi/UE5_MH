// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "MHGZSegmentedDirectionalMovement.generated.h"

/**
 * Measured baseline for a three-phase directional movement profile: optional
 * hold, straight-speed travel, then gravity/forward-acceleration glide.
 * The default durations sum to exactly 0.25 seconds in float arithmetic.
 * Durations are seconds; speeds are cm/s; accelerations are cm/s^2.
 */
USTRUCT(BlueprintType)
struct MHGZ_API FSegmentedDirectionalMovementProfile
{
	GENERATED_BODY()

public:
	/** Initial stationary hold before straight movement begins. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "0.0"))
	float InitialHoldSeconds = 1.0f / 120.0f;

	/** Duration of the measured straight-speed phase. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "0.000001"))
	float StraightSeconds = 5.0f / 60.0f;

	/** Duration of the accelerating glide phase. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "0.000001"))
	float GlideSeconds = 9.5f / 60.0f;

	/** Straight-phase speed in cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "0.000001"))
	float StraightSpeed = 6600.0f;

	/** Initial glide speed in cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "0.000001"))
	float GlideInitialSpeed = 1800.0f;

	/** World-Z gravity magnitude during glide in cm/s^2. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "0.0"))
	float GlideGravityMagnitude = 1800.0f;

	/** Forward acceleration during glide in cm/s^2. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "0.0"))
	float GlideForwardAcceleration = 360.0f;

	/** Returns whether fields and all derived float-valued phase results are valid. */
	bool IsValid() const;

	/** Returns the total profile duration, or zero for an invalid/non-finite profile. */
	float GetDuration() const;

	/** Returns displacement from the launch point at the clamped elapsed time. */
	FVector GetOffset(const FVector& NormalizedLaunchDirection, float Seconds) const;

	/** Returns velocity at the clamped elapsed time. */
	FVector GetVelocity(const FVector& NormalizedLaunchDirection, float Seconds) const;

	/** Returns the next strictly future phase boundary, or total duration at/after the end. */
	float GetNextBoundarySeconds(float Seconds) const;
};
