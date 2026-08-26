// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Animation/MHGZMotionMatchingMath.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZPMMCruiseSpeedQuantization,
	"MHGZ.PMM.Query.CruiseSpeedQuantization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZPMMCruiseSpeedQuantization::RunTest(const FString& Parameters)
{
	const MHGZMotionMatching::FMHGZMotionMatchingCruiseSpeedSettings Settings;
	using namespace MHGZMotionMatching;

	TestEqual(TEXT("zero input stays idle"),
		QuantizeCruiseSpeed(Settings, 0.0f, false, false), 0.0f);
	TestEqual(TEXT("input below deadzone stays idle"),
		QuantizeCruiseSpeed(Settings, 0.099f, false, false), 0.0f);
	TestEqual(TEXT("small sheathed input maps to Walk"),
		QuantizeCruiseSpeed(Settings, 0.30f, false, false), 160.0f);
	TestEqual(TEXT("walk/run boundary maps to Run"),
		QuantizeCruiseSpeed(Settings, 0.50f, false, false), 460.0f);
	TestEqual(TEXT("non-sprinting full input maps to Run"),
		QuantizeCruiseSpeed(Settings, 1.0f, false, false), 460.0f);
	TestEqual(TEXT("sprinting full input maps to Sprint"),
		QuantizeCruiseSpeed(Settings, 1.0f, false, true), 575.0f);
	TestEqual(TEXT("exact sprint threshold remains Run"),
		QuantizeCruiseSpeed(Settings, 0.90f, false, true), 460.0f);
	TestEqual(TEXT("unsheathed always uses its single lane"),
		QuantizeCruiseSpeed(Settings, 0.30f, true, true), 440.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZPMMTrajectoryMath,
	"MHGZ.PMM.Query.TrajectoryMath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZPMMTrajectoryMath::RunTest(const FString& Parameters)
{
	using namespace MHGZMotionMatching;

	const float AccelAt0 = CalculatePredictedDistance(0.0f, 460.0f, 1000.0f, 0.0f);
	const float AccelAt02 = CalculatePredictedDistance(0.0f, 460.0f, 1000.0f, 0.2f);
	const float AccelAt05 = CalculatePredictedDistance(0.0f, 460.0f, 1000.0f, 0.5f);
	TestEqual(TEXT("acceleration starts at the current position"), AccelAt0, 0.0f);
	TestTrue(TEXT("accelerating future distance is monotonic"),
		AccelAt02 > AccelAt0 && AccelAt05 > AccelAt02);

	const float StopAt025 = CalculatePredictedDistance(460.0f, 0.0f, 1900.0f, 0.25f);
	const float StopAt1 = CalculatePredictedDistance(460.0f, 0.0f, 1900.0f, 1.0f);
	TestTrue(TEXT("deceleration reaches a non-negative stop point"), StopAt025 >= 0.0f);
	TestTrue(TEXT("distance remains fixed after reaching zero speed"),
		FMath::IsNearlyEqual(StopAt025, StopAt1, KINDA_SMALL_NUMBER));

	const float StopQuery = CalculateStopDistanceQuery(460.0f, 1900.0f);
	TestTrue(TEXT("stop query uses the required negative convention"), StopQuery < 0.0f);
	TestTrue(TEXT("stop query magnitude matches the predicted stop distance"),
		FMath::IsNearlyEqual(-StopQuery, StopAt1, KINDA_SMALL_NUMBER));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZPMMMotionMeasurementReset,
	"MHGZ.PMM.Query.MotionMeasurementReset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZPMMMotionMeasurementReset::RunTest(const FString& Parameters)
{
	using namespace MHGZMotionMatching;

	TestTrue(TEXT("active action root-motion ownership resets the measurement"),
		ShouldResetMotionMeasurement(true, false, true, true, 1.0f / 60.0f));
	TestTrue(TEXT("first frame after root-motion ownership release also resets"),
		ShouldResetMotionMeasurement(false, true, true, true, 1.0f / 60.0f));
	TestTrue(TEXT("airborne measurement resets"),
		ShouldResetMotionMeasurement(false, false, false, true, 1.0f / 60.0f));
	TestTrue(TEXT("missing previous location resets"),
		ShouldResetMotionMeasurement(false, false, true, false, 1.0f / 60.0f));
	TestFalse(TEXT("normal grounded locomotion keeps the measurement"),
		ShouldResetMotionMeasurement(false, false, true, true, 1.0f / 60.0f));
	return true;
}

#endif
