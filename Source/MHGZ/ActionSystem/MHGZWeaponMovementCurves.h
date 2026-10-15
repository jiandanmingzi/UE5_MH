// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UCurveFloat;
struct FRootMotionSource_MoveToDynamicForce;

/** Shared validation and construction for normalized, curve-driven weapon movement. */
namespace MHGZWeaponMovementCurves
{
	/** Accept finite, monotonic, linear progress keys from (0,0) to (1,1). */
	MHGZ_API bool ValidateNormalizedProgressCurve(const UCurveFloat* Curve, float& OutTerminalSlope);

	/** Build the production 3D curve-mapped RMS and resolve its authored end tangent. */
	TSharedPtr<FRootMotionSource_MoveToDynamicForce> MakeCurvedDirectionalSource(
		const FVector& StartLocation,
		const FVector& Direction,
		float MaxDistance,
		float Duration,
		UCurveFloat* DistanceCurve,
		FName InstanceName,
		FVector& OutTerminalVelocity);
}
