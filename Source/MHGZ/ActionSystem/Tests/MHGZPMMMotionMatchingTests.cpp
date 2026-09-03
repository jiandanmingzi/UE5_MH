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
	TestEqual(TEXT("input below raw deadzone stays idle"),
		QuantizeCruiseSpeed(Settings, 0.099f, false, false), 0.0f);
	TestEqual(TEXT("sub-Walk sheathed input stays idle"),
		QuantizeCruiseSpeed(Settings, 0.49f, false, false), 0.0f);
	TestEqual(TEXT("Walk activation threshold maps to Walk"),
		QuantizeCruiseSpeed(Settings, 0.50f, false, false), 160.0f);
	TestEqual(TEXT("input below Walk/Run boundary stays Walk"),
		QuantizeCruiseSpeed(Settings, 0.749f, false, false), 160.0f);
	TestEqual(TEXT("walk/run boundary maps to Run"),
		QuantizeCruiseSpeed(Settings, 0.75f, false, false), 460.0f);
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
	FMHGZPMMDownshiftProbe,
	"MHGZ.PMM.Query.DownshiftProbe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZPMMDownshiftProbe::RunTest(const FString& Parameters)
{
	using namespace MHGZMotionMatching;
	constexpr float ConfirmDuration = 0.025f;
	constexpr float FrameSeconds = 1.0f / 60.0f;

	FMHGZDownshiftProbeState State;
	TestEqual(TEXT("initial Run commits immediately"),
		ResolveCruiseSpeedWithDownshiftProbe(State, 460.0f, 1.0f, FrameSeconds,
			ConfirmDuration, true), 460.0f);
	TestEqual(TEXT("first downward Walk sample preserves Run"),
		ResolveCruiseSpeedWithDownshiftProbe(State, 160.0f, 0.60f, FrameSeconds,
			ConfirmDuration, true), 460.0f);
	TestEqual(TEXT("one confirmation frame still preserves Run"),
		ResolveCruiseSpeedWithDownshiftProbe(State, 160.0f, 0.60f, FrameSeconds,
			ConfirmDuration, true), 460.0f);
	TestEqual(TEXT("confirmed lower lane commits Walk"),
		ResolveCruiseSpeedWithDownshiftProbe(State, 160.0f, 0.60f, FrameSeconds,
			ConfirmDuration, true), 160.0f);

	State.Reset();
	ResolveCruiseSpeedWithDownshiftProbe(State, 460.0f, 1.0f, FrameSeconds,
		ConfirmDuration, true);
	ResolveCruiseSpeedWithDownshiftProbe(State, 160.0f, 0.60f, FrameSeconds,
		ConfirmDuration, true);
	TestEqual(TEXT("dropping below the Walk threshold stops immediately"),
		ResolveCruiseSpeedWithDownshiftProbe(State, 0.0f, 0.30f, FrameSeconds,
			ConfirmDuration, true), 0.0f);
	TestFalse(TEXT("immediate release clears the downshift probe"), State.bActive);

	State.Reset();
	ResolveCruiseSpeedWithDownshiftProbe(State, 575.0f, 1.0f, FrameSeconds,
		ConfirmDuration, true);
	TestEqual(TEXT("RB-only Sprint-to-Run change stays immediate"),
		ResolveCruiseSpeedWithDownshiftProbe(State, 460.0f, 1.0f, FrameSeconds,
			ConfirmDuration, true), 460.0f);
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

	const FTransform TurnedActor(FRotator(0.0f, 90.0f, 0.0f), FVector::ZeroVector);
	const FVector ImportedTrajectoryForward = GetImportedLocomotionTrajectoryForward(TurnedActor);
	TestTrue(TEXT("imported locomotion trajectory uses actor local Y"),
		ImportedTrajectoryForward.Equals(TurnedActor.GetUnitAxis(EAxis::Y), KINDA_SMALL_NUMBER));
	TestTrue(TEXT("imported locomotion trajectory does not use actor local X"),
		!ImportedTrajectoryForward.Equals(TurnedActor.GetUnitAxis(EAxis::X), KINDA_SMALL_NUMBER));
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
	TestTrue(TEXT("a held mobile Handoff preserves its existing locomotion edge"),
		ShouldPreserveHandoffInputAcrossMeasurementReset(true, true, true, true, false));
	TestTrue(TEXT("a release observed in the mobile phase survives as one Stop edge"),
		ShouldPreserveHandoffInputAcrossMeasurementReset(true, true, true, false, true));
	TestFalse(TEXT("a locked action release does not bypass the normal Start path"),
		ShouldPreserveHandoffInputAcrossMeasurementReset(true, false, true, true, false));
	TestTrue(TEXT("a no-input non-Handoff action publishes the idle-only candidate context"),
		ShouldPublishActionIdleContextOnMeasurementRelease(true, false, false));
	TestFalse(TEXT("a mobile Handoff takes precedence over the generic idle context"),
		ShouldPublishActionIdleContextOnMeasurementRelease(true, true, false));
	TestFalse(TEXT("a held stick after an action must return to normal movement routing"),
		ShouldPublishActionIdleContextOnMeasurementRelease(true, false, true));
	TestFalse(TEXT("a non-Exit route must not force a Stop handoff"),
		ShouldInterruptMobileActionExitForStop(false, true));
	TestFalse(TEXT("a held Exit must preserve its authored mobile transition"),
		ShouldInterruptMobileActionExitForStop(true, false));
	TestTrue(TEXT("a real release interrupts a non-functional mobile Exit for normal Stop selection"),
		ShouldInterruptMobileActionExitForStop(true, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZPMMConsumedStopSelection,
	"MHGZ.PMM.Query.ConsumedStopSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZPMMConsumedStopSelection::RunTest(const FString& Parameters)
{
	using namespace MHGZMotionMatching;

	TestTrue(TEXT("the accepted Stop may be sampled before another selection result arrives"),
		ShouldContinueConsumedStopSelection(true, false, false, 0.4f, 0.4f));
	TestTrue(TEXT("the same Stop may advance through Pose Search continuation"),
		ShouldContinueConsumedStopSelection(true, true, true, 0.4f, 0.45f));
	TestFalse(TEXT("a fresh Stop search cannot re-arm the consumed release"),
		ShouldContinueConsumedStopSelection(true, true, false, 1.2f, 0.4f));
	TestFalse(TEXT("a different Stop animation cannot re-arm the consumed release"),
		ShouldContinueConsumedStopSelection(false, true, false, 1.2f, 0.4f));
	TestFalse(TEXT("a continuing result must not seek backwards into a Stop entry"),
		ShouldContinueConsumedStopSelection(true, true, true, 1.2f, 0.4f));
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZPMMNormalMoveCandidateRouting,
	"MHGZ.PMM.Query.NormalMoveCandidateRouting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZPMMNormalMoveCandidateRouting::RunTest(const FString& Parameters)
{
	using namespace MHGZMotionMatching;

	FMHGZNormalMoveCandidateRouteInput Input;
	Input.bActionRouteIsMove = true;
	Input.bHasLocomotionInput = true;
	Input.TargetGait = EMHGZNormalMoveGait::Sprint;
	Input.bLatestSelectionIsRunLoop = true;
	TestEqual(TEXT("a real Run Loop plus Sprint target narrows to Sprint LoopOnly"),
		ResolveSheathedNormalMoveCandidateSet(Input),
		EMHGZNormalMoveCandidateSet::SprintLoopOnly);

	Input.TargetGait = EMHGZNormalMoveGait::Run;
	TestEqual(TEXT("an unchanged Run Loop stays on the full library"),
		ResolveSheathedNormalMoveCandidateSet(Input),
		EMHGZNormalMoveCandidateSet::FullMove);

	Input = FMHGZNormalMoveCandidateRouteInput();
	Input.bActionRouteIsMove = true;
	Input.bHasLocomotionInput = true;
	Input.CurrentCandidateSet = EMHGZNormalMoveCandidateSet::SprintLoopOnly;
	Input.TargetGait = EMHGZNormalMoveGait::Sprint;
	TestEqual(TEXT("the active Sprint LoopOnly bank stays active while the target remains Sprint"),
		ResolveSheathedNormalMoveCandidateSet(Input),
		EMHGZNormalMoveCandidateSet::SprintLoopOnly);

	Input.TargetGait = EMHGZNormalMoveGait::Run;
	TestEqual(TEXT("a target reversal swaps directly between LoopOnly banks"),
		ResolveSheathedNormalMoveCandidateSet(Input),
		EMHGZNormalMoveCandidateSet::RunLoopOnly);

	Input.TargetGait = EMHGZNormalMoveGait::Walk;
	TestEqual(TEXT("Walk exits the narrow bank and returns to the full library"),
		ResolveSheathedNormalMoveCandidateSet(Input),
		EMHGZNormalMoveCandidateSet::FullMove);

	Input.TargetGait = EMHGZNormalMoveGait::Sprint;
	Input.bStartQueryActive = true;
	TestEqual(TEXT("a Start query never receives a LoopOnly library"),
		ResolveSheathedNormalMoveCandidateSet(Input),
		EMHGZNormalMoveCandidateSet::FullMove);

	Input.bStartQueryActive = false;
	Input.bStopRequestActive = true;
	TestEqual(TEXT("a Stop request always restores the full library"),
		ResolveSheathedNormalMoveCandidateSet(Input),
		EMHGZNormalMoveCandidateSet::FullMove);

	Input.bStopRequestActive = false;
	Input.bActionRouteIsMove = false;
	TestEqual(TEXT("ActionExit routing owns its candidate context"),
		ResolveSheathedNormalMoveCandidateSet(Input),
		EMHGZNormalMoveCandidateSet::FullMove);
	return true;
}
#endif
