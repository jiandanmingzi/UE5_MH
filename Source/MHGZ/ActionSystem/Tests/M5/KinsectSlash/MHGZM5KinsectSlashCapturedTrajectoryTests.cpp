// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "ActionSystem/MHGZKinsectSlashAbility.h"
#include <limits>

namespace
{
FVector EvaluateCapturedDance(const FWeaponMovementRequest& Request, const float Time)
{
	const float T = FMath::Clamp(Time, 0.0f, Request.Duration);
	const FVector Acceleration = Request.PlanarAcceleration
		+ FVector(0.0f, 0.0f, -Request.GravityMagnitude);
	return Request.LaunchVelocity * T + 0.5f * Acceleration * T * T;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5KinsectSlashCapturedDefaultsTest,
	"MHGZ.M5.KinsectSlash.Captured189DefaultsMatchBothHitEntries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5KinsectSlashCapturedDefaultsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const UMHGZKinsectSlashAbility* Ability = GetDefault<UMHGZKinsectSlashAbility>();
	if (!TestNotNull(TEXT("production C++ ability defaults exist"), Ability))
	{
		return false;
	}
	const FKinsectSlashDanceEntryTuning* Entries[] = {
		&Ability->DanceFromDashTuning, &Ability->DanceFromMissTuning
	};
	for (int32 EntryIndex = 0; EntryIndex < UE_ARRAY_COUNT(Entries); ++EntryIndex)
	{
		const FKinsectSlashDanceEntryTuning& Tuning = *Entries[EntryIndex];
		const FString Label = EntryIndex == 0 ? TEXT("187") : TEXT("188");
		TestTrue(Label + TEXT(" enables the captured trajectory by default"),
			Tuning.bUseCapturedTrajectory && Tuning.IsValid());
		TestTrue(Label + TEXT(" has the measured preparation and flight durations"),
			FMath::IsNearlyEqual(Tuning.PrepDuration, 0.35f, 0.000001f)
				&& FMath::IsNearlyEqual(Tuning.FlightDuration, 1.1833333f, 0.000001f));
		TestTrue(Label + TEXT(" has the shared measured launch and acceleration parameters"),
			Tuning.LaunchUpSpeed == 1080.0f && Tuning.LaunchBackwardSpeed == 300.0f
				&& Tuning.GravityMagnitude == 1800.0f && Tuning.BackwardAcceleration == 360.0f);
		const FWeaponMovementRequest Request = Tuning.MakeRequest(
			FWeaponActionToken(), FVector::ForwardVector);
		TestEqual(Label + TEXT(" uses CMC-authored falling physics by default"),
			Request.Mode, EWeaponMovementMode::AuthoredAerialPhysics);
		TestTrue(Label + TEXT(" produces a valid captured request"),
			Request.HasValidAuthoredAerialPhysicsParameters());
		TestTrue(Label + TEXT(" starts with the measured upward/backward velocity"),
			Request.LaunchVelocity.Equals(FVector(-300.0f, 0.0f, 1080.0f), 0.001f));
		TestTrue(Label + TEXT(" carries the measured planar backward acceleration"),
			Request.PlanarAcceleration.Equals(FVector(-360.0f, 0.0f, 0.0f), 0.001f));
		TestEqual(Label + TEXT(" carries the measured gravity magnitude"),
			Request.GravityMagnitude, 1800.0f);
	}
	TestEqual(TEXT("Miss188 keeps its own measured gravity"), Ability->MissGravityMagnitude, 3096.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5KinsectSlashCapturedTrajectoryShapeTest,
	"MHGZ.M5.KinsectSlash.Captured189BackwardParabolaAnd324cmApex",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5KinsectSlashCapturedTrajectoryShapeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FKinsectSlashDanceEntryTuning Tuning;
	const FWeaponMovementRequest Request = Tuning.MakeRequest(
		FWeaponActionToken(), FVector::ForwardVector);
	TestEqual(TEXT("captured movement uses CMC-authored physics"),
		Request.Mode, EWeaponMovementMode::AuthoredAerialPhysics);
	TestTrue(TEXT("captured request passes its finite-value validation"),
		Request.HasValidAuthoredAerialPhysicsParameters());
	TestTrue(TEXT("the movement axis points backwards from the locked planar launch"),
		Request.DirectionSnapshot.Equals(-FVector::ForwardVector, 0.000001f));
	TestTrue(TEXT("the request contains the exact captured launch tangent"),
		Request.LaunchVelocity.Equals(FVector(-300.0f, 0.0f, 1080.0f), 0.001f));
	TestTrue(TEXT("the request contains the exact captured planar acceleration"),
		Request.PlanarAcceleration.Equals(FVector(-360.0f, 0.0f, 0.0f), 0.001f));
	TestEqual(TEXT("captured movement preserves the actor's yaw through the locked rotation policy"),
		Request.RotationPolicy, EActionRotationPolicy::Locked);
	TestEqual(TEXT("captured movement preserves cancellation velocity"),
		Request.CancelVelocityPolicy, EMovementCancelVelocityPolicy::PreserveVelocity);
	bool bMonotonicBackward = true;
	double MaxModelError = 0.0;
	FVector Previous = FVector::ZeroVector;
	for (int32 SampleIndex = 0; SampleIndex <= 240; ++SampleIndex)
	{
		const float Time = Request.Duration * (static_cast<float>(SampleIndex) / 240.0f);
		const FVector Local = EvaluateCapturedDance(Request, Time);
		const double CapturedTime = Time;
		// Oracle from the 2026-09-28 measurement, independent of the request builder.
		const FVector Measured(-300.0 * CapturedTime - 180.0 * CapturedTime * CapturedTime,
			0.0, 1080.0 * CapturedTime - 900.0 * CapturedTime * CapturedTime);
		MaxModelError = FMath::Max(MaxModelError, (Local - Measured).Size());
		if (SampleIndex > 0)
		{
			bMonotonicBackward &= Local.X < Previous.X;
		}
		Previous = Local;
	}
	TestTrue(TEXT("reconstructed motion goes monotonically backwards throughout flight"), bMonotonicBackward);
	TestTrue(TEXT("CMC request equations match the measured parabola exactly"),
		MaxModelError < 0.001);
	const FVector Apex = EvaluateCapturedDance(Request, 0.6f);
	TestTrue(TEXT("the flight apex at 0.6s is 324cm above lift-off"),
		FMath::IsNearlyEqual(Apex.Z, 324.0, 0.001));
	TestTrue(TEXT("the path rises before the apex and descends after it"),
		EvaluateCapturedDance(Request, 0.59f).Z < Apex.Z
			&& EvaluateCapturedDance(Request, 0.61f).Z < Apex.Z);
	TestTrue(TEXT("flight begins at the preparation hover's position"),
		EvaluateCapturedDance(Request, 0.0f).IsZero());
	TestTrue(TEXT("flight remains descending at its timed handoff"),
		Request.LaunchVelocity.Z - Request.GravityMagnitude * Request.Duration < 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5KinsectSlashCapturedTrajectoryTangentTest,
	"MHGZ.M5.KinsectSlash.Captured189ExactTangentAndPlanarYaw",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5KinsectSlashCapturedTrajectoryTangentTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FKinsectSlashDanceEntryTuning Tuning;
	FWeaponActionToken Token;
	Token.ActivationSequenceID = 17;
	const FWeaponMovementRequest Request = Tuning.MakeRequest(Token, FVector(3.0f, 4.0f, 12.0f));
	TestTrue(TEXT("request keeps its exact owning action token"), Request.OwnerAction == Token);
	TestTrue(TEXT("aim pitch is removed before reversing the flight axis"),
		Request.DirectionSnapshot.Equals(FVector(-0.6f, -0.8f, 0.0f), 0.000001f));
	TestTrue(TEXT("launch velocity remains in the locked world yaw frame"),
		Request.LaunchVelocity.Equals(FVector(-180.0f, -240.0f, 1080.0f), 0.001f));
	TestTrue(TEXT("planar acceleration remains in the locked world yaw frame"),
		Request.PlanarAcceleration.Equals(FVector(-216.0f, -288.0f, 0.0f), 0.001f));
	const FVector HandoffVelocity = Request.LaunchVelocity
		+ (Request.PlanarAcceleration + FVector(0.0f, 0.0f, -Request.GravityMagnitude))
			* Request.Duration;
	TestTrue(TEXT("timed CMC continuation retains the analytic terminal tangent"),
		HandoffVelocity.Equals(FVector(-435.6f, -580.8f, -1050.0f), 0.01f));

	FKinsectSlashDanceEntryTuning BelowLiftOff = Tuning;
	BelowLiftOff.FlightDuration = 1.25f;
	const FWeaponMovementRequest LongerRequest = BelowLiftOff.MakeRequest(Token, FVector::ForwardVector);
	TestTrue(TEXT("an editable descending flight may finish below lift-off"),
		BelowLiftOff.IsValid() && LongerRequest.HasValidAuthoredAerialPhysicsParameters()
			&& FMath::IsNearlyEqual(EvaluateCapturedDance(LongerRequest,
				LongerRequest.Duration).Z, -56.25f, 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5KinsectSlashCapturedTrajectoryValidationTest,
	"MHGZ.M5.KinsectSlash.Captured189RejectsInvalidAndAscendingTuning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5KinsectSlashCapturedTrajectoryValidationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FKinsectSlashDanceEntryTuning Defaults;
	struct FCapturedField
	{
		const TCHAR* Name;
		float FKinsectSlashDanceEntryTuning::* Member;
	};
	const FCapturedField Fields[] = {
		{ TEXT("PrepDuration"), &FKinsectSlashDanceEntryTuning::PrepDuration },
		{ TEXT("FlightDuration"), &FKinsectSlashDanceEntryTuning::FlightDuration },
		{ TEXT("LaunchUpSpeed"), &FKinsectSlashDanceEntryTuning::LaunchUpSpeed },
		{ TEXT("LaunchBackwardSpeed"), &FKinsectSlashDanceEntryTuning::LaunchBackwardSpeed },
		{ TEXT("GravityMagnitude"), &FKinsectSlashDanceEntryTuning::GravityMagnitude },
		{ TEXT("BackwardAcceleration"), &FKinsectSlashDanceEntryTuning::BackwardAcceleration }
	};
	const float InvalidValues[] = { 0.0f, -1.0f,
		std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() };
	for (const FCapturedField& Field : Fields)
	{
		for (int32 ValueIndex = 0; ValueIndex < UE_ARRAY_COUNT(InvalidValues); ++ValueIndex)
		{
			FKinsectSlashDanceEntryTuning Invalid = Defaults;
			Invalid.*(Field.Member) = InvalidValues[ValueIndex];
			TestFalse(FString::Printf(TEXT("%s rejects invalid value %d"), Field.Name, ValueIndex),
				Invalid.IsValid());
		}
	}
	FKinsectSlashDanceEntryTuning Invalid = Defaults;
	Invalid.FlightDuration = 0.5f;
	TestFalse(TEXT("a flight ending while still rising is invalid"), Invalid.IsValid());
	Invalid.FlightDuration = 0.6f;
	TestFalse(TEXT("a flight ending exactly at the apex is invalid"), Invalid.IsValid());
	Invalid.FlightDuration = std::numeric_limits<float>::max();
	TestFalse(TEXT("finite tuning whose endpoint overflows is invalid"), Invalid.IsValid());
	const FWeaponMovementRequest InvalidRequest = Invalid.MakeRequest(
		FWeaponActionToken(), FVector::ForwardVector);
	TestTrue(TEXT("invalid tuning returns an invalid captured request"),
		!InvalidRequest.HasValidAuthoredAerialPhysicsParameters());
	Invalid = Defaults;
	Invalid.GravityMagnitude = std::numeric_limits<float>::max();
	TestFalse(TEXT("an overflowing end height/tangent is invalid"), Invalid.IsValid());
	Invalid = Defaults;
	Invalid.BackwardAcceleration = std::numeric_limits<float>::max();
	TestFalse(TEXT("an overflowing planar tangent is invalid"), Invalid.IsValid());

	FKinsectSlashDanceEntryTuning LegacyFields = Defaults;
	LegacyFields.ApexHeight = std::numeric_limits<float>::quiet_NaN();
	LegacyFields.Duration = -1.0f;
	LegacyFields.HorizontalDistance = 307.6f;
	LegacyFields.ExplicitLaunchVelocity = FVector(0.0f, 0.0f, 10.0f);
	TestTrue(TEXT("serialized legacy values cannot override the enabled captured model"), LegacyFields.IsValid());
	const FWeaponMovementRequest CapturedRequest = LegacyFields.MakeRequest(
		FWeaponActionToken(), FVector::ForwardVector);
	TestTrue(TEXT("the captured request ignores legacy branch fields"),
		CapturedRequest.Mode == EWeaponMovementMode::AuthoredAerialPhysics
			&& FMath::IsNearlyEqual(CapturedRequest.LaunchVelocity.Z, 1080.0f, 0.001f));
	LegacyFields.bUseCapturedTrajectory = false;
	TestFalse(TEXT("legacy opt-out still validates its legacy parameters"), LegacyFields.IsValid());
	return true;
}

#endif
