// Copyright MHGZ Project. All Rights Reserved.

#include "ActionSystem/MHGZSegmentedDirectionalMovement.h"
#include "MHGZMath.h"

namespace
{
	bool IsRepresentableNonNegativeFloat(const double Value)
	{
		const double FloatMax = static_cast<double>(TNumericLimits<float>::Max());
		return Value >= 0.0 && Value <= FloatMax;
	}

	bool IsRepresentablePositiveFloat(const double Value)
	{
		const double FloatMax = static_cast<double>(TNumericLimits<float>::Max());
		return Value > 0.0 && Value <= FloatMax;
	}

	bool TryGetNormalizedDirection(const FVector& Input, FVector& OutDirection)
	{
		OutDirection = FVector::ZeroVector;
		if (!MHGZMath::IsFiniteVector(Input))
		{
			return false;
		}

		OutDirection = Input.GetSafeNormal();
		return MHGZMath::IsFiniteVector(OutDirection) && !OutDirection.IsNearlyZero();
	}

	float ClampElapsedSeconds(const FSegmentedDirectionalMovementProfile& Profile, float Seconds)
	{
		if (!FMath::IsFinite(Seconds))
		{
			return 0.0f;
		}

		return FMath::Clamp(Seconds, 0.0f, Profile.GetDuration());
	}

	FVector GetGlideAcceleration(
		const FSegmentedDirectionalMovementProfile& Profile,
		const FVector& Direction)
	{
		// Gravity is measured in world Z and intentionally is not pitch-rotated
		// with the launch direction.
		return Direction.GetSafeNormal2D() * Profile.GlideForwardAcceleration
			+ FVector(0.0f, 0.0f, -Profile.GlideGravityMagnitude);
	}

	double GetGlideAccelerationBound(const FSegmentedDirectionalMovementProfile& Profile)
	{
		return FMath::Max(
			static_cast<double>(Profile.GlideForwardAcceleration),
			static_cast<double>(Profile.GlideGravityMagnitude));
	}
}

bool FSegmentedDirectionalMovementProfile::IsValid() const
{
	if (!(FMath::IsFinite(InitialHoldSeconds)
		&& FMath::IsFinite(StraightSeconds)
		&& FMath::IsFinite(GlideSeconds)
		&& FMath::IsFinite(StraightSpeed)
		&& FMath::IsFinite(GlideInitialSpeed)
		&& FMath::IsFinite(GlideGravityMagnitude)
		&& FMath::IsFinite(GlideForwardAcceleration)
		&& InitialHoldSeconds >= 0.0f
		&& StraightSeconds > 0.0f
		&& GlideSeconds > 0.0f
		&& StraightSpeed > 0.0f
		&& GlideInitialSpeed > 0.0f
		&& GlideGravityMagnitude >= 0.0f
		&& GlideForwardAcceleration >= 0.0f))
	{
		return false;
	}

	const double HoldSeconds = static_cast<double>(InitialHoldSeconds);
	const double StraightDuration = static_cast<double>(StraightSeconds);
	const double GlideDuration = static_cast<double>(GlideSeconds);
	const double StraightEndSeconds = HoldSeconds + StraightDuration;
	const double DurationSeconds = StraightEndSeconds + GlideDuration;
	const double StraightDisplacement =
		static_cast<double>(StraightSpeed) * StraightDuration;
	const double GlideAccelerationBound = GetGlideAccelerationBound(*this);
	const double GlideDisplacement =
		static_cast<double>(GlideInitialSpeed) * GlideDuration
		+ 0.5 * GlideAccelerationBound * GlideDuration * GlideDuration;
	const double TotalDisplacement = StraightDisplacement + GlideDisplacement;
	const double TerminalVelocityBound =
		static_cast<double>(GlideInitialSpeed)
		+ GlideAccelerationBound * GlideDuration;
	const float FloatStraightEndSeconds = InitialHoldSeconds + StraightSeconds;
	const float FloatDurationSeconds = static_cast<float>(DurationSeconds);

	return FMath::IsFinite(FloatStraightEndSeconds)
		&& FloatStraightEndSeconds > InitialHoldSeconds
		&& FMath::IsFinite(FloatDurationSeconds)
		&& FloatDurationSeconds > FloatStraightEndSeconds
		&& IsRepresentablePositiveFloat(StraightEndSeconds)
		&& IsRepresentablePositiveFloat(DurationSeconds)
		&& IsRepresentableNonNegativeFloat(StraightDisplacement)
		&& IsRepresentableNonNegativeFloat(GlideDisplacement)
		&& IsRepresentableNonNegativeFloat(TotalDisplacement)
		&& IsRepresentableNonNegativeFloat(TerminalVelocityBound);
}

float FSegmentedDirectionalMovementProfile::GetDuration() const
{
	if (!IsValid())
	{
		return 0.0f;
	}

	const double Duration = static_cast<double>(InitialHoldSeconds)
		+ static_cast<double>(StraightSeconds)
		+ static_cast<double>(GlideSeconds);
	return static_cast<float>(Duration);
}

FVector FSegmentedDirectionalMovementProfile::GetOffset(
	const FVector& NormalizedLaunchDirection,
	const float Seconds) const
{
	if (!IsValid() || !FMath::IsFinite(Seconds))
	{
		return FVector::ZeroVector;
	}

	FVector Direction;
	if (!TryGetNormalizedDirection(NormalizedLaunchDirection, Direction))
	{
		return FVector::ZeroVector;
	}

	const float ElapsedSeconds = ClampElapsedSeconds(*this, Seconds);
	const float StraightEndSeconds = InitialHoldSeconds + StraightSeconds;
	// Exact boundaries enter the following phase; glide owns the terminal sample.
	if (ElapsedSeconds < InitialHoldSeconds)
	{
		return FVector::ZeroVector;
	}

	if (ElapsedSeconds < StraightEndSeconds)
	{
		const FVector Offset = Direction * (StraightSpeed * (ElapsedSeconds - InitialHoldSeconds));
		return MHGZMath::IsFiniteVector(Offset) ? Offset : FVector::ZeroVector;
	}

	const float GlideElapsedSeconds = FMath::Clamp(
		ElapsedSeconds - StraightEndSeconds, 0.0f, GlideSeconds);
	const FVector Acceleration = GetGlideAcceleration(*this, Direction);
	const FVector Offset = Direction * (StraightSpeed * StraightSeconds)
		+ Direction * (GlideInitialSpeed * GlideElapsedSeconds)
		+ (Acceleration * GlideElapsedSeconds) * (0.5f * GlideElapsedSeconds);
	return MHGZMath::IsFiniteVector(Offset) ? Offset : FVector::ZeroVector;
}

FVector FSegmentedDirectionalMovementProfile::GetVelocity(
	const FVector& NormalizedLaunchDirection,
	const float Seconds) const
{
	if (!IsValid() || !FMath::IsFinite(Seconds))
	{
		return FVector::ZeroVector;
	}

	FVector Direction;
	if (!TryGetNormalizedDirection(NormalizedLaunchDirection, Direction))
	{
		return FVector::ZeroVector;
	}

	const float ElapsedSeconds = ClampElapsedSeconds(*this, Seconds);
	const float StraightEndSeconds = InitialHoldSeconds + StraightSeconds;
	// Exact hold and straight ends select straight and glide velocity respectively.
	if (ElapsedSeconds < InitialHoldSeconds)
	{
		return FVector::ZeroVector;
	}

	if (ElapsedSeconds < StraightEndSeconds)
	{
		const FVector Velocity = Direction * StraightSpeed;
		return MHGZMath::IsFiniteVector(Velocity) ? Velocity : FVector::ZeroVector;
	}

	const float GlideElapsedSeconds = FMath::Clamp(
		ElapsedSeconds - StraightEndSeconds, 0.0f, GlideSeconds);
	const FVector Velocity = Direction * GlideInitialSpeed
		+ GetGlideAcceleration(*this, Direction) * GlideElapsedSeconds;
	return MHGZMath::IsFiniteVector(Velocity) ? Velocity : FVector::ZeroVector;
}

float FSegmentedDirectionalMovementProfile::GetNextBoundarySeconds(const float Seconds) const
{
	if (!IsValid() || !FMath::IsFinite(Seconds))
	{
		return 0.0f;
	}

	const float ElapsedSeconds = ClampElapsedSeconds(*this, Seconds);
	const float StraightEndSeconds = InitialHoldSeconds + StraightSeconds;
	if (ElapsedSeconds < InitialHoldSeconds)
	{
		return InitialHoldSeconds;
	}

	if (ElapsedSeconds < StraightEndSeconds)
	{
		return StraightEndSeconds;
	}

	return GetDuration();
}
