// Copyright MHGZ Project. All Rights Reserved.

#include "ActionSystem/MHGZWeaponMovementCurves.h"

#include "Curves/CurveFloat.h"
#include "Curves/RichCurve.h"
#include "GameFramework/RootMotionSource.h"

bool MHGZWeaponMovementCurves::ValidateNormalizedProgressCurve(
	const UCurveFloat* Curve, float& OutTerminalSlope)
{
	OutTerminalSlope = 0.0f;
	if (!Curve)
	{
		return false;
	}

	constexpr float ValueTolerance = 0.001f;
	const TArray<FRichCurveKey>& Keys = Curve->FloatCurve.Keys;
	if (Keys.Num() < 2
		|| !FMath::IsFinite(Keys[0].Time) || !FMath::IsFinite(Keys[0].Value)
		|| FMath::Abs(Keys[0].Time) > ValueTolerance
		|| FMath::Abs(Keys[0].Value) > ValueTolerance)
	{
		return false;
	}

	for (int32 KeyIndex = 1; KeyIndex < Keys.Num(); ++KeyIndex)
	{
		const FRichCurveKey& PreviousKey = Keys[KeyIndex - 1];
		const FRichCurveKey& Key = Keys[KeyIndex];
		if (!FMath::IsFinite(Key.Time) || !FMath::IsFinite(Key.Value)
			|| Key.Time <= PreviousKey.Time
			|| Key.Value < PreviousKey.Value - ValueTolerance
			|| Key.Value < -ValueTolerance
			|| Key.Value > 1.0f + ValueTolerance
			|| PreviousKey.InterpMode != RCIM_Linear)
		{
			return false;
		}
	}

	const FRichCurveKey& LastKey = Keys.Last();
	const FRichCurveKey& PreviousKey = Keys[Keys.Num() - 2];
	if (FMath::Abs(LastKey.Time - 1.0f) > ValueTolerance
		|| FMath::Abs(LastKey.Value - 1.0f) > ValueTolerance)
	{
		return false;
	}

	OutTerminalSlope = (LastKey.Value - PreviousKey.Value)
		/ (LastKey.Time - PreviousKey.Time);
	return FMath::IsFinite(OutTerminalSlope) && OutTerminalSlope > KINDA_SMALL_NUMBER;
}

TSharedPtr<FRootMotionSource_MoveToDynamicForce>
MHGZWeaponMovementCurves::MakeCurvedDirectionalSource(
	const FVector& StartLocation,
	const FVector& Direction,
	const float MaxDistance,
	const float Duration,
	UCurveFloat* DistanceCurve,
	const FName InstanceName,
	FVector& OutTerminalVelocity)
{
	OutTerminalVelocity = FVector::ZeroVector;
	float TerminalSlope = 0.0f;
	if (StartLocation.ContainsNaN() || Direction.ContainsNaN()
		|| Direction.IsNearlyZero()
		|| !FMath::IsFinite(MaxDistance) || MaxDistance <= 0.0f
		|| !FMath::IsFinite(Duration) || Duration <= 0.0f
		|| !ValidateNormalizedProgressCurve(DistanceCurve, TerminalSlope))
	{
		return nullptr;
	}

	const FVector NormalizedDirection = Direction.GetSafeNormal();
	OutTerminalVelocity = NormalizedDirection * (MaxDistance * TerminalSlope / Duration);
	if (OutTerminalVelocity.ContainsNaN()
		|| !FMath::IsFinite(OutTerminalVelocity.SizeSquared()))
	{
		OutTerminalVelocity = FVector::ZeroVector;
		return nullptr;
	}

	TSharedPtr<FRootMotionSource_MoveToDynamicForce> Source =
		MakeShared<FRootMotionSource_MoveToDynamicForce>();
	Source->InstanceName = InstanceName;
	Source->Priority = 700;
	Source->AccumulateMode = ERootMotionAccumulateMode::Override;
	Source->Duration = Duration;
	Source->StartLocation = StartLocation;
	Source->InitialTargetLocation = StartLocation + NormalizedDirection * MaxDistance;
	Source->SetTargetLocation(Source->InitialTargetLocation);
	// The authored curve contains faster early travel followed by a glide. The
	// RMS speed limiter uses average distance/duration and would clip that shape.
	Source->bRestrictSpeedToExpected = false;
	Source->TimeMappingCurve = DistanceCurve;
	Source->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::SetVelocity;
	Source->FinishVelocityParams.SetVelocity = OutTerminalVelocity;
	return Source;
}
