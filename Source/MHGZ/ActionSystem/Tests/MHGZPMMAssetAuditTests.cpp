// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Animation/AnimCurveTypes.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/MHGZPoseSearchFeatureChannel_MoveGait.h"
#include "Animation/MHGZPoseSearchFeatureChannel_StopGait.h"
#include "PoseSearch/PoseSearchAnimNotifies.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchDerivedData.h"
#include "PoseSearch/PoseSearchFeatureChannel_Curve.h"
#include "PoseSearch/PoseSearchIndex.h"
#include "PoseSearch/PoseSearchSchema.h"

namespace
{
constexpr float PMMTimeTolerance = 0.05f;
constexpr float PMMValueTolerance = 0.01f;
constexpr float PMMZeroTolerance = 0.02f;
constexpr float PMMGeneratedStopControlTolerance = 0.01f;
constexpr float PMMSampleInterval = 1.0f / 60.0f;
constexpr int32 PMMGeneratedStopZeroTailSampleCount = 3;
constexpr int32 PMMGeneratedStopBlockLeadSampleCount = 1;
constexpr float PMMStartContinuingWindow = 0.10f;
constexpr float PMMStartBlockTailWindow = 1.0f / 30.0f;
constexpr float PMMStartContinuingModifier = 0.0f;
constexpr float PMMStopContinuingModifier = -0.50f;
constexpr float PMMStopGaitWeight = 64.0f;
constexpr float PMMMoveGaitWeight = 8.0f;

enum class EPMMAssetRole : uint8
{
	Idle,
	Loop,
	Start,
	Stop,
	Unknown
};

struct FPMMDatabaseSpec
{
	const TCHAR* ObjectPath;
	TArray<FName> ExpectedAssetNames;
};

const TArray<FPMMDatabaseSpec>& GetPMMDatabaseSpecs()
{
	static const TArray<FPMMDatabaseSpec> Specs =
	{
		{
			TEXT("/Game/Blueprints/Characters/Demo/Animation/MotionMatching/PSD_MH_Shth_Move.PSD_MH_Shth_Move"),
			{
				FName(TEXT("AS_Shth_Idle")),
				FName(TEXT("AS_Shth_Walk_Start")),
				FName(TEXT("AS_Shth_Walk_Loop")),
				FName(TEXT("AS_Shth_Run_Start")),
				FName(TEXT("AS_Shth_Run_Loop")),
				FName(TEXT("AS_Shth_Sprint_Start")),
				FName(TEXT("AS_Shth_Sprint_Loop_125x")),
				FName(TEXT("AS_Shth_Walk_Stop_Left_Extended")),
				FName(TEXT("AS_Shth_Walk_Stop_Right_Extended")),
				FName(TEXT("AS_Shth_Walk_FirstStepCommitStop")),
				FName(TEXT("AS_Shth_Run_Stop_Left_Extended")),
				FName(TEXT("AS_Shth_Run_Stop_Right_Extended")),
				FName(TEXT("AS_Shth_Run_FirstStepCommitStop")),
				FName(TEXT("AS_Shth_Sprint_Stop_Left_Extended")),
				FName(TEXT("AS_Shth_Sprint_Stop_Right_Extended")),
				FName(TEXT("AS_Shth_Sprint_FirstStepCommitStop"))
			}
		},
		{
			TEXT("/Game/Blueprints/Characters/Demo/Animation/MotionMatching/PSD_MH_UnSh_Move.PSD_MH_UnSh_Move"),
			{
				FName(TEXT("AS_UnSh_Idle")),
				FName(TEXT("AS_UnSh_Walk_Start")),
				FName(TEXT("AS_UnSh_Walk_Loop")),
				FName(TEXT("AS_UnSh_Walk_Stop"))
			}
		}
	};

	return Specs;
}

EPMMAssetRole GetPMMAssetRole(const UAnimSequenceBase& Sequence)
{
	const FString Name = Sequence.GetName();
	if (Name.Contains(TEXT("_Stop")) || Name.Contains(TEXT("FirstStepCommitStop")))
	{
		return EPMMAssetRole::Stop;
	}
	if (Name.Contains(TEXT("_Start")))
	{
		return EPMMAssetRole::Start;
	}
	if (Name.Contains(TEXT("_Idle")))
	{
		return EPMMAssetRole::Idle;
	}
	if (Name.Contains(TEXT("_Loop")))
	{
		return EPMMAssetRole::Loop;
	}
	return EPMMAssetRole::Unknown;
}

float GetExpectedStopGait(const UAnimSequenceBase& Sequence)
{
	const FString Name = Sequence.GetName();
	if (!Name.Contains(TEXT("Extended")) && !Name.Contains(TEXT("FirstStepCommitStop")))
	{
		return 0.0f;
	}
	if (Name.Contains(TEXT("Walk")))
	{
		return 1.0f / 3.0f;
	}
	if (Name.Contains(TEXT("Run")))
	{
		return 2.0f / 3.0f;
	}
	return Name.Contains(TEXT("Sprint")) ? 1.0f : -1.0f;
}

float GetExpectedMoveGait(const UAnimSequenceBase& Sequence)
{
	const EPMMAssetRole Role = GetPMMAssetRole(Sequence);
	if (Role != EPMMAssetRole::Start && Role != EPMMAssetRole::Loop)
	{
		return 0.0f;
	}

	const FString Name = Sequence.GetName();
	if (Name.Contains(TEXT("UnSh")))
	{
		return 2.0f / 3.0f;
	}
	if (Name.Contains(TEXT("Sprint")))
	{
		return 1.0f;
	}
	if (Name.Contains(TEXT("Run")))
	{
		return 2.0f / 3.0f;
	}
	return Name.Contains(TEXT("Walk")) ? 1.0f / 3.0f : -1.0f;
}

bool IsGeneratedStop(const UAnimSequenceBase& Sequence)
{
	const FString Name = Sequence.GetName();
	return Name.Contains(TEXT("Extended")) || Name.Contains(TEXT("FirstStepCommitStop"));
}

FString GetAssetLabel(const UAnimSequenceBase& Sequence)
{
	return Sequence.GetPathName();
}

const FFloatCurve* FindFloatCurve(const UAnimSequenceBase& Sequence, const FName CurveName)
{
	return static_cast<const FFloatCurve*>(Sequence.GetCurveData().GetCurveData(CurveName, ERawCurveTrackTypes::RCT_Float));
}

const FFloatCurve* RequireFloatCurve(FAutomationTestBase& Test, const UAnimSequenceBase& Sequence, const FName CurveName)
{
	const FFloatCurve* Curve = FindFloatCurve(Sequence, CurveName);
	Test.TestTrue(
		FString::Printf(TEXT("%s has Float Curve '%s'"), *GetAssetLabel(Sequence), *CurveName.ToString()),
		Curve != nullptr);
	return Curve;
}

bool GetCurveKeys(FAutomationTestBase& Test, const UAnimSequenceBase& Sequence, const FName CurveName, const FFloatCurve* Curve, TArray<float>& OutTimes, TArray<float>& OutValues)
{
	if (!Curve)
	{
		return false;
	}

	Curve->GetKeys(OutTimes, OutValues);
	return Test.TestTrue(
		FString::Printf(TEXT("%s curve '%s' has at least one key"), *GetAssetLabel(Sequence), *CurveName.ToString()),
		OutTimes.Num() > 0 && OutTimes.Num() == OutValues.Num());
}

bool TestNearlyEqual(FAutomationTestBase& Test, const FString& What, const float Actual, const float Expected, const float Tolerance = PMMValueTolerance)
{
	return Test.TestTrue(
		FString::Printf(TEXT("%s (actual %.4f, expected %.4f, tolerance %.4f)"), *What, Actual, Expected, Tolerance),
		FMath::IsNearlyEqual(Actual, Expected, Tolerance));
}

bool ValidateConstantZeroCurve(FAutomationTestBase& Test, const UAnimSequenceBase& Sequence, const FName CurveName, const FFloatCurve* Curve)
{
	TArray<float> Times;
	TArray<float> Values;
	if (!GetCurveKeys(Test, Sequence, CurveName, Curve, Times, Values))
	{
		return false;
	}

	bool bIsValid = true;
	bIsValid &= Test.TestTrue(
		FString::Printf(TEXT("%s curve '%s' has a start and an end key"), *GetAssetLabel(Sequence), *CurveName.ToString()),
		Times.Num() >= 2);
	bIsValid &= TestNearlyEqual(Test, FString::Printf(TEXT("%s curve '%s' starts at time zero"), *GetAssetLabel(Sequence), *CurveName.ToString()), Times[0], 0.0f, PMMTimeTolerance);
	bIsValid &= TestNearlyEqual(Test, FString::Printf(TEXT("%s curve '%s' ends with the sequence"), *GetAssetLabel(Sequence), *CurveName.ToString()), Times.Last(), Sequence.GetPlayLength(), PMMTimeTolerance);

	for (int32 Index = 0; Index < Values.Num(); ++Index)
	{
		if (!FMath::IsNearlyZero(Values[Index], PMMZeroTolerance))
		{
			Test.AddError(FString::Printf(TEXT("%s curve '%s' must remain zero; key %d is %.4f."), *GetAssetLabel(Sequence), *CurveName.ToString(), Index, Values[Index]));
			bIsValid = false;
			break;
		}
	}

	if (!Curve)
	{
		return false;
	}

	for (float Time = 0.0f; Time < Sequence.GetPlayLength(); Time += PMMSampleInterval)
	{
		if (!FMath::IsNearlyZero(Curve->Evaluate(Time), PMMZeroTolerance))
		{
			Test.AddError(FString::Printf(TEXT("%s curve '%s' must evaluate to zero at %.3f s."), *GetAssetLabel(Sequence), *CurveName.ToString(), Time));
			bIsValid = false;
			break;
		}
	}

	return bIsValid;
}

bool ValidateConstantCurve(FAutomationTestBase& Test, const UAnimSequenceBase& Sequence,
	const FName CurveName, const FFloatCurve* Curve, const float ExpectedValue)
{
	TArray<float> Times;
	TArray<float> Values;
	if (!GetCurveKeys(Test, Sequence, CurveName, Curve, Times, Values))
	{
		return false;
	}

	bool bIsValid = true;
	bIsValid &= Test.TestTrue(
		FString::Printf(TEXT("%s curve '%s' has a start and an end key"), *GetAssetLabel(Sequence), *CurveName.ToString()),
		Times.Num() >= 2);
	bIsValid &= TestNearlyEqual(Test, FString::Printf(TEXT("%s curve '%s' starts at time zero"), *GetAssetLabel(Sequence), *CurveName.ToString()), Times[0], 0.0f, PMMTimeTolerance);
	bIsValid &= TestNearlyEqual(Test, FString::Printf(TEXT("%s curve '%s' ends with the sequence"), *GetAssetLabel(Sequence), *CurveName.ToString()), Times.Last(), Sequence.GetPlayLength(), PMMTimeTolerance);

	for (int32 Index = 0; Index < Values.Num(); ++Index)
	{
		bIsValid &= TestNearlyEqual(Test,
			FString::Printf(TEXT("%s curve '%s' key %d stays on its family lane"), *GetAssetLabel(Sequence), *CurveName.ToString(), Index),
			Values[Index], ExpectedValue, PMMValueTolerance);
	}
	return bIsValid;
}

bool ValidateStartIntentCurve(FAutomationTestBase& Test, const UAnimSequenceBase& Sequence, const FFloatCurve* Curve)
{
	TArray<float> Times;
	TArray<float> Values;
	if (!GetCurveKeys(Test, Sequence, TEXT("MM_Intent"), Curve, Times, Values))
	{
		return false;
	}
	const FString Name = Sequence.GetName();
	const float ExpectedIntentEnd = Name.Equals(TEXT("AS_Shth_Walk_Start"), ESearchCase::IgnoreCase)
		? 1.25f
		: (Name.Equals(TEXT("AS_Shth_Run_Start"), ESearchCase::IgnoreCase)
			|| Name.Equals(TEXT("AS_Shth_Sprint_Start"), ESearchCase::IgnoreCase))
			? 0.80f
			: (Name.Equals(TEXT("AS_UnSh_Walk_Start"), ESearchCase::IgnoreCase) ? 0.60f : -1.0f);

	bool bIsValid = Test.TestTrue(FString::Printf(TEXT("%s has a documented finite Start intent duration"), *GetAssetLabel(Sequence)), ExpectedIntentEnd > 0.0f);
	bIsValid &= Test.TestEqual(FString::Printf(TEXT("%s MM_Intent Start uses an edge and an explicit semantic end"), *GetAssetLabel(Sequence)), Times.Num(), 2);
	bIsValid &= TestNearlyEqual(Test, FString::Printf(TEXT("%s MM_Intent starts at time zero"), *GetAssetLabel(Sequence)), Times[0], 0.0f, PMMTimeTolerance);
	bIsValid &= TestNearlyEqual(Test, FString::Printf(TEXT("%s MM_Intent starts at +1"), *GetAssetLabel(Sequence)), Values[0], 1.0f, PMMValueTolerance);
	if (Times.Num() == 2 && Values.Num() == 2 && ExpectedIntentEnd > 0.0f)
	{
		bIsValid &= TestNearlyEqual(Test, FString::Printf(TEXT("%s MM_Intent reaches zero at its finite semantic end"), *GetAssetLabel(Sequence)), Values[1], 0.0f, PMMZeroTolerance);
		bIsValid &= TestNearlyEqual(Test, FString::Printf(TEXT("%s MM_Intent semantic end matches its contract"), *GetAssetLabel(Sequence)), Times[1], ExpectedIntentEnd, PMMTimeTolerance);
	}

	return bIsValid;
}

bool ValidateStopIntentCurve(FAutomationTestBase& Test, const UAnimSequenceBase& Sequence, const FFloatCurve* Curve)
{
	TArray<float> Times;
	TArray<float> Values;
	if (!GetCurveKeys(Test, Sequence, TEXT("MM_Intent"), Curve, Times, Values))
	{
		return false;
	}

	bool bIsValid = true;
	bIsValid &= TestNearlyEqual(Test, FString::Printf(TEXT("%s MM_Intent starts at time zero"), *GetAssetLabel(Sequence)), Times[0], 0.0f, PMMTimeTolerance);
	bIsValid &= TestNearlyEqual(Test, FString::Printf(TEXT("%s MM_Intent starts at -1"), *GetAssetLabel(Sequence)), Values[0], -1.0f, 0.05f);
	bIsValid &= TestNearlyEqual(Test, FString::Printf(TEXT("%s MM_Intent ends with the sequence"), *GetAssetLabel(Sequence)), Times.Last(), Sequence.GetPlayLength(), PMMTimeTolerance);
	bIsValid &= TestNearlyEqual(Test, FString::Printf(TEXT("%s MM_Intent ends at zero"), *GetAssetLabel(Sequence)), Values.Last(), 0.0f, PMMZeroTolerance);

	for (const float Value : Values)
	{
		if (Value > PMMZeroTolerance || Value < -1.0f - PMMZeroTolerance)
		{
			Test.AddError(FString::Printf(TEXT("%s MM_Intent Stop values must stay in [-1, 0]; found %.4f."), *GetAssetLabel(Sequence), Value));
			bIsValid = false;
			break;
		}
	}

	return bIsValid;
}

bool ValidateStopDistanceCurve(FAutomationTestBase& Test, const UAnimSequenceBase& Sequence, const FFloatCurve* Curve)
{
	TArray<float> StopTimes;
	TArray<float> StopValues;
	if (!GetCurveKeys(Test, Sequence, TEXT("MM_DistanceToStop"), Curve, StopTimes, StopValues))
	{
		return false;
	}

	bool bIsValid = true;
	bIsValid &= Test.TestTrue(
		FString::Printf(TEXT("%s MM_DistanceToStop has at least a start and end key"), *GetAssetLabel(Sequence)),
		StopTimes.Num() >= 2);
	bIsValid &= TestNearlyEqual(Test, FString::Printf(TEXT("%s MM_DistanceToStop starts at time zero"), *GetAssetLabel(Sequence)), StopTimes[0], 0.0f, PMMTimeTolerance);
	bIsValid &= TestNearlyEqual(Test, FString::Printf(TEXT("%s MM_DistanceToStop ends with the sequence"), *GetAssetLabel(Sequence)), StopTimes.Last(), Sequence.GetPlayLength(), PMMTimeTolerance);
	if (IsGeneratedStop(Sequence))
	{
		bIsValid &= TestNearlyEqual(Test, FString::Printf(TEXT("%s generated Stop has neutral prefix distance"), *GetAssetLabel(Sequence)), StopValues[0], 0.0f, PMMZeroTolerance);
		bIsValid &= Test.TestTrue(FString::Printf(TEXT("%s generated Stop reaches a negative remaining distance after its prefix"), *GetAssetLabel(Sequence)),
			StopValues.ContainsByPredicate([](const float Value) { return Value < -PMMZeroTolerance; }));
	}
	else
	{
		bIsValid &= Test.TestTrue(FString::Printf(TEXT("%s MM_DistanceToStop starts with negative remaining distance"), *GetAssetLabel(Sequence)), StopValues[0] < -PMMZeroTolerance);
	}
	bIsValid &= TestNearlyEqual(Test, FString::Printf(TEXT("%s MM_DistanceToStop ends at zero"), *GetAssetLabel(Sequence)), StopValues.Last(), 0.0f, PMMZeroTolerance);

	for (const float Value : StopValues)
	{
		if (Value > PMMZeroTolerance)
		{
			Test.AddError(FString::Printf(TEXT("%s MM_DistanceToStop must not become positive; found %.4f."), *GetAssetLabel(Sequence), Value));
			bIsValid = false;
			break;
		}

	}

	return bIsValid;
}

bool ValidateStopGaitCurve(FAutomationTestBase& Test, const UAnimSequenceBase& Sequence,
	const FFloatCurve* Curve)
{
	const float ExpectedValue = GetExpectedStopGait(Sequence);
	if (ExpectedValue < 0.0f)
	{
		Test.AddError(FString::Printf(TEXT("%s cannot be mapped to a PMM-7 StopGait lane."),
			*GetAssetLabel(Sequence)));
		return false;
	}
	if (FMath::IsNearlyZero(ExpectedValue, PMMZeroTolerance))
	{
		return ValidateConstantZeroCurve(Test, Sequence, TEXT("MM_StopGait"), Curve);
	}

	TArray<float> Times;
	TArray<float> Values;
	if (!GetCurveKeys(Test, Sequence, TEXT("MM_StopGait"), Curve, Times, Values))
	{
		return false;
	}
	bool bIsValid = true;
	bIsValid &= TestNearlyEqual(Test, FString::Printf(TEXT("%s MM_StopGait starts at its family lane"),
		*GetAssetLabel(Sequence)), Values[0], ExpectedValue, PMMValueTolerance);
	return bIsValid && ValidateConstantCurve(Test, Sequence, TEXT("MM_StopGait"), Curve, ExpectedValue);
}

bool ValidateMoveGaitCurve(FAutomationTestBase& Test, const UAnimSequenceBase& Sequence,
	const FFloatCurve* Curve)
{
	const float ExpectedValue = GetExpectedMoveGait(Sequence);
	if (ExpectedValue < 0.0f)
	{
		Test.AddError(FString::Printf(TEXT("%s cannot be mapped to a PMM MoveGait lane."),
			*GetAssetLabel(Sequence)));
		return false;
	}
	return FMath::IsNearlyZero(ExpectedValue, PMMZeroTolerance)
		? ValidateConstantZeroCurve(Test, Sequence, TEXT("MM_MoveGait"), Curve)
		: ValidateConstantCurve(Test, Sequence, TEXT("MM_MoveGait"), Curve, ExpectedValue);
}

bool GatherDatabaseSequences(FAutomationTestBase& Test, const FPMMDatabaseSpec& Spec, TArray<UAnimSequenceBase*>& OutSequences, bool bCheckExpectedMembership)
{
	UPoseSearchDatabase* Database = LoadObject<UPoseSearchDatabase>(nullptr, Spec.ObjectPath);
	if (!Test.TestTrue(FString::Printf(TEXT("Loads Pose Search database %s"), Spec.ObjectPath), Database != nullptr))
	{
		return false;
	}

	TSet<FName> MissingExpectedAssets(Spec.ExpectedAssetNames);
	bool bIsValid = true;
	for (int32 Index = 0; Index < Database->GetNumAnimationAssets(); ++Index)
	{
		UObject* AnimationAsset = Database->GetAnimationAsset(Index);
		UAnimSequenceBase* Sequence = Cast<UAnimSequenceBase>(AnimationAsset);
		bIsValid &= Test.TestTrue(
			FString::Printf(TEXT("%s database entry %d is an animation sequence"), Spec.ObjectPath, Index),
			Sequence != nullptr);
		if (!Sequence)
		{
			continue;
		}

		if (bCheckExpectedMembership)
		{
			const bool bWasExpected = MissingExpectedAssets.Remove(Sequence->GetFName()) > 0;
			bIsValid &= Test.TestTrue(
			FString::Printf(TEXT("%s is an approved PMM-7 formal candidate"), *GetAssetLabel(*Sequence)),
				bWasExpected);
		}
		OutSequences.Add(Sequence);
	}

	if (bCheckExpectedMembership)
	{
		for (const FName MissingAsset : MissingExpectedAssets)
		{
			Test.AddError(FString::Printf(TEXT("%s is missing its required PMM-7 candidate '%s'."), Spec.ObjectPath, *MissingAsset.ToString()));
			bIsValid = false;
		}
	}

	return bIsValid;
}

void GatherAllFormalSequences(FAutomationTestBase& Test, TArray<UAnimSequenceBase*>& OutSequences, bool bCheckExpectedMembership)
{
	for (const FPMMDatabaseSpec& Spec : GetPMMDatabaseSpecs())
	{
		GatherDatabaseSequences(Test, Spec, OutSequences, bCheckExpectedMembership);
	}
}

bool ValidateRoleAndCurves(FAutomationTestBase& Test, const UAnimSequenceBase& Sequence)
{
	const EPMMAssetRole Role = GetPMMAssetRole(Sequence);
	if (!Test.TestTrue(FString::Printf(TEXT("%s has a PMM-7 locomotion role"), *GetAssetLabel(Sequence)), Role != EPMMAssetRole::Unknown))
	{
		return false;
	}

	const bool bShouldLoop = Role == EPMMAssetRole::Idle || Role == EPMMAssetRole::Loop;
	bool bIsValid = Test.TestTrue(FString::Printf(TEXT("%s has the expected looping flag"), *GetAssetLabel(Sequence)), Sequence.bLoop == bShouldLoop);

	const FFloatCurve* IntentCurve = RequireFloatCurve(Test, Sequence, TEXT("MM_Intent"));
	const FFloatCurve* DistanceCurve = RequireFloatCurve(Test, Sequence, TEXT("MM_DistanceToStop"));
	const FFloatCurve* StopGaitCurve = RequireFloatCurve(Test, Sequence, TEXT("MM_StopGait"));
	const FFloatCurve* MoveGaitCurve = RequireFloatCurve(Test, Sequence, TEXT("MM_MoveGait"));
	if (!IntentCurve || !DistanceCurve || !StopGaitCurve || !MoveGaitCurve)
	{
		return false;
	}

	switch (Role)
	{
	case EPMMAssetRole::Idle:
	case EPMMAssetRole::Loop:
		bIsValid &= ValidateConstantZeroCurve(Test, Sequence, TEXT("MM_Intent"), IntentCurve);
		bIsValid &= ValidateConstantZeroCurve(Test, Sequence, TEXT("MM_DistanceToStop"), DistanceCurve);
		break;
	case EPMMAssetRole::Start:
	{
		bIsValid &= ValidateStartIntentCurve(Test, Sequence, IntentCurve);
		bIsValid &= ValidateConstantZeroCurve(Test, Sequence, TEXT("MM_DistanceToStop"), DistanceCurve);
		break;
	}
	case EPMMAssetRole::Stop:
		bIsValid &= ValidateStopIntentCurve(Test, Sequence, IntentCurve);
		bIsValid &= ValidateStopDistanceCurve(Test, Sequence, DistanceCurve);
		bIsValid &= Test.TestFalse(
			FString::Printf(TEXT("%s does not retain the legacy Distance curve after the rename"), *GetAssetLabel(Sequence)),
			Sequence.HasCurveData(TEXT("Distance")));
		break;
	default:
		break;
	}
	bIsValid &= ValidateStopGaitCurve(Test, Sequence, StopGaitCurve);
	bIsValid &= ValidateMoveGaitCurve(Test, Sequence, MoveGaitCurve);

	return bIsValid;
}

void ValidatePoseSearchControlNotifies(FAutomationTestBase& Test, const UAnimSequenceBase& Sequence)
{
	const EPMMAssetRole Role = GetPMMAssetRole(Sequence);
	if (Role != EPMMAssetRole::Start && Role != EPMMAssetRole::Stop)
	{
		return;
	}

	const FAnimNotifyEvent* BlockTransition = nullptr;
	const FAnimNotifyEvent* ContinuingBias = nullptr;
	int32 BlockTransitionCount = 0;
	int32 ContinuingBiasCount = 0;

	for (const FAnimNotifyEvent& Notify : Sequence.Notifies)
	{
		const FName TrackName = Sequence.AnimNotifyTracks.IsValidIndex(Notify.TrackIndex)
			? Sequence.AnimNotifyTracks[Notify.TrackIndex].TrackName
			: NAME_None;

		if (Cast<UAnimNotifyState_PoseSearchBlockTransition>(Notify.NotifyStateClass))
		{
			++BlockTransitionCount;
			BlockTransition = &Notify;
			Test.TestEqual(
				FString::Printf(TEXT("%s Block Transition In is on PoseSearchControl"), *GetAssetLabel(Sequence)),
				TrackName,
				FName(TEXT("PoseSearchControl")));
		}
		else if (Cast<UAnimNotifyState_PoseSearchOverrideContinuingPoseCostBias>(Notify.NotifyStateClass))
		{
			++ContinuingBiasCount;
			ContinuingBias = &Notify;
			Test.TestEqual(
				FString::Printf(TEXT("%s Continuing Pose Cost Bias is on PoseSearchControl"), *GetAssetLabel(Sequence)),
				TrackName,
				FName(TEXT("PoseSearchControl")));
		}
	}

	Test.TestEqual(FString::Printf(TEXT("%s has one Block Transition In state"), *GetAssetLabel(Sequence)), BlockTransitionCount, 1);
	Test.TestEqual(FString::Printf(TEXT("%s has one Continuing Pose Cost Bias state"), *GetAssetLabel(Sequence)), ContinuingBiasCount, 1);
	if (!BlockTransition || !ContinuingBias)
	{
		return;
	}

	const float Length = Sequence.GetPlayLength();
	const bool bGeneratedStop = Role == EPMMAssetRole::Stop && IsGeneratedStop(Sequence);
	float ExpectedBlockStart = Role == EPMMAssetRole::Start ? 0.10f : 0.12f;
	float ExpectedBlockEnd = Role == EPMMAssetRole::Start
		? FMath::Max(0.0f, Length - PMMStartBlockTailWindow)
		: FMath::Max(0.0f, Length - 0.05f);
	float ExpectedContinuingEnd = Role == EPMMAssetRole::Stop
		? FMath::Max(0.0f, Length - 0.08f)
		: FMath::Min(PMMStartContinuingWindow, Length);
	float ControlTolerance = PMMTimeTolerance;
	if (bGeneratedStop)
	{
		const FFloatCurve* DistanceCurve = RequireFloatCurve(Test, Sequence, TEXT("MM_DistanceToStop"));
		TArray<float> DistanceTimes;
		TArray<float> DistanceValues;
		int32 CommitKeyIndex = INDEX_NONE;
		if (GetCurveKeys(Test, Sequence, TEXT("MM_DistanceToStop"), DistanceCurve, DistanceTimes, DistanceValues))
		{
			for (int32 KeyIndex = 0; KeyIndex < DistanceValues.Num(); ++KeyIndex)
			{
				if (DistanceValues[KeyIndex] < -PMMZeroTolerance)
				{
					CommitKeyIndex = KeyIndex;
					break;
				}
			}
		}
		Test.TestTrue(FString::Printf(TEXT("%s generated Stop has a real Stop commit key"), *GetAssetLabel(Sequence)),
			CommitKeyIndex != INDEX_NONE);
		if (CommitKeyIndex != INDEX_NONE)
		{
			// PMM-7.1 aligns lifecycle controls to the PSS grid. Start one full
			// indexed sample before the real Stop commit, so the commit frame is
			// never globally selectable even at an exact Notify boundary.
			const int32 CommitSampleIndex = FMath::Max(0,
				FMath::CeilToInt(DistanceTimes[CommitKeyIndex] / PMMSampleInterval));
			ExpectedBlockStart = FMath::Max(0.0f,
				(CommitSampleIndex - PMMGeneratedStopBlockLeadSampleCount) * PMMSampleInterval);
		}
		ExpectedBlockEnd = Length;
		ExpectedContinuingEnd = Length;
		ControlTolerance = PMMGeneratedStopControlTolerance;
	}
	TestNearlyEqual(Test, FString::Printf(TEXT("%s Block Transition In start time"), *GetAssetLabel(Sequence)), BlockTransition->GetTriggerTime(), ExpectedBlockStart, ControlTolerance);
	TestNearlyEqual(Test, FString::Printf(TEXT("%s Block Transition In end time"), *GetAssetLabel(Sequence)), BlockTransition->GetEndTriggerTime(), ExpectedBlockEnd, ControlTolerance);

	TestNearlyEqual(Test, FString::Printf(TEXT("%s Continuing Pose Cost Bias start time"), *GetAssetLabel(Sequence)), ContinuingBias->GetTriggerTime(), 0.0f, PMMTimeTolerance);
	Test.TestTrue(FString::Printf(TEXT("%s has a valid Continuing Pose Cost Bias end time"), *GetAssetLabel(Sequence)), ExpectedContinuingEnd >= 0.0f);
	if (ExpectedContinuingEnd >= 0.0f)
	{
		TestNearlyEqual(Test, FString::Printf(TEXT("%s Continuing Pose Cost Bias end time"), *GetAssetLabel(Sequence)), ContinuingBias->GetEndTriggerTime(), ExpectedContinuingEnd, ControlTolerance);
	}

	const UAnimNotifyState_PoseSearchOverrideContinuingPoseCostBias* ContinuingBiasState = Cast<UAnimNotifyState_PoseSearchOverrideContinuingPoseCostBias>(ContinuingBias->NotifyStateClass);
	const float ExpectedContinuingModifier = Role == EPMMAssetRole::Start
		? PMMStartContinuingModifier
		: PMMStopContinuingModifier;
	TestNearlyEqual(Test,
		FString::Printf(TEXT("%s Continuing Pose Cost Bias modifier"), *GetAssetLabel(Sequence)),
		ContinuingBiasState->CostAddend,
		ExpectedContinuingModifier,
		PMMValueTolerance);
}

void ValidateGeneratedStopIndexLifecycle(FAutomationTestBase& Test)
{
	using namespace UE::PoseSearch;

	int32 GeneratedStopAssetCount = 0;
	for (const FPMMDatabaseSpec& Spec : GetPMMDatabaseSpecs())
	{
		UPoseSearchDatabase* Database = LoadObject<UPoseSearchDatabase>(nullptr, Spec.ObjectPath);
		if (!Test.TestNotNull(FString::Printf(TEXT("Loads Pose Search database for indexed Stop audit: %s"), Spec.ObjectPath), Database))
		{
			continue;
		}

		const ERequestAsyncBuildFlag BuildFlags = ERequestAsyncBuildFlag::NewRequest
			| ERequestAsyncBuildFlag::WaitForCompletion;
		if (!Test.TestEqual(FString::Printf(TEXT("Builds current Pose Search index: %s"), Spec.ObjectPath),
			FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(Database, BuildFlags),
			EAsyncBuildIndexResult::Success))
		{
			continue;
		}

		const FSearchIndex& SearchIndex = Database->GetSearchIndex();
		for (const FSearchIndexAsset& IndexAsset : SearchIndex.Assets)
		{
			const FPoseSearchDatabaseSequence* Entry =
				Database->GetDatabaseAnimationAsset<FPoseSearchDatabaseSequence>(IndexAsset);
			const UAnimSequenceBase* Sequence = Entry ? Entry->Sequence.Get() : nullptr;
			if (!Sequence || !IsGeneratedStop(*Sequence))
			{
				continue;
			}

			++GeneratedStopAssetCount;
			Test.TestTrue(FString::Printf(TEXT("%s generated Stop uses the complete entry sampling range"),
				*GetAssetLabel(*Sequence)),
				FMath::IsNearlyZero(Entry->SamplingRange.Min, PMMValueTolerance)
					&& FMath::IsNearlyZero(Entry->SamplingRange.Max, PMMValueTolerance));

			const FFloatCurve* IntentCurve = RequireFloatCurve(Test, *Sequence, TEXT("MM_Intent"));
			const FFloatCurve* DistanceCurve = RequireFloatCurve(Test, *Sequence, TEXT("MM_DistanceToStop"));
			if (!IntentCurve || !DistanceCurve || IndexAsset.GetNumPoses() <= 0)
			{
				Test.AddError(FString::Printf(TEXT("%s cannot be checked for an indexed zero tail."),
					*GetAssetLabel(*Sequence)));
				continue;
			}

			int32 FirstZeroPoseIndex = INDEX_NONE;
			int32 ZeroTailPoseCount = 0;
			bool bZeroTailContiguous = true;
			bool bZeroTailBlocked = true;
			const int32 FirstPoseIndex = IndexAsset.GetFirstPoseIdx();
			const int32 EndPoseIndex = FirstPoseIndex + IndexAsset.GetNumPoses();
			for (int32 PoseIndex = FirstPoseIndex; PoseIndex < EndPoseIndex; ++PoseIndex)
			{
				const float Time = Database->GetRealAssetTime(PoseIndex);
				const bool bZeroQuery = FMath::IsNearlyZero(IntentCurve->Evaluate(Time), PMMZeroTolerance)
					&& FMath::IsNearlyZero(DistanceCurve->Evaluate(Time), PMMZeroTolerance);
				if (bZeroQuery && FirstZeroPoseIndex == INDEX_NONE)
				{
					FirstZeroPoseIndex = PoseIndex;
				}
				if (FirstZeroPoseIndex != INDEX_NONE)
				{
					bZeroTailContiguous &= bZeroQuery;
					if (bZeroQuery)
					{
						++ZeroTailPoseCount;
						bZeroTailBlocked &= SearchIndex.PoseMetadata[PoseIndex].IsBlockTransition();
					}
				}
			}

			const float LastIndexedTime = Database->GetRealAssetTime(EndPoseIndex - 1);
			Test.TestTrue(FString::Printf(TEXT("%s has at least %d indexed zero-query tail poses"),
				*GetAssetLabel(*Sequence), PMMGeneratedStopZeroTailSampleCount - 1),
				ZeroTailPoseCount >= PMMGeneratedStopZeroTailSampleCount - 1);
			Test.TestTrue(FString::Printf(TEXT("%s indexed zero-query tail is contiguous"),
				*GetAssetLabel(*Sequence)), bZeroTailContiguous);
			Test.TestTrue(FString::Printf(TEXT("%s indexed zero-query tail is BlockTransition metadata"),
				*GetAssetLabel(*Sequence)), bZeroTailBlocked);

			const FAnimNotifyEvent* BlockState = nullptr;
			const FAnimNotifyEvent* ContinuingState = nullptr;
			for (const FAnimNotifyEvent& Notify : Sequence->Notifies)
			{
				if (Cast<UAnimNotifyState_PoseSearchBlockTransition>(Notify.NotifyStateClass))
				{
					BlockState = &Notify;
				}
				else if (Cast<UAnimNotifyState_PoseSearchOverrideContinuingPoseCostBias>(Notify.NotifyStateClass))
				{
					ContinuingState = &Notify;
				}
			}
			Test.TestNotNull(FString::Printf(TEXT("%s has a Block state for the zero tail"),
				*GetAssetLabel(*Sequence)), BlockState);
			Test.TestNotNull(FString::Printf(TEXT("%s has a Continuing state for the zero tail"),
				*GetAssetLabel(*Sequence)), ContinuingState);
			if (BlockState && ContinuingState)
			{
				Test.TestTrue(FString::Printf(TEXT("%s Block state covers its final indexed zero pose"),
					*GetAssetLabel(*Sequence)), BlockState->GetEndTriggerTime() + PMMSampleInterval >= LastIndexedTime);
				Test.TestTrue(FString::Printf(TEXT("%s Continuing state covers its final indexed zero pose"),
					*GetAssetLabel(*Sequence)), ContinuingState->GetEndTriggerTime() + PMMSampleInterval >= LastIndexedTime);
			}
		}
	}

	Test.TestEqual(TEXT("PMM-7 sheathed database exposes nine generated Stop index assets"),
		GeneratedStopAssetCount, 9);
}

void ValidatePMM7Schema(FAutomationTestBase& Test)
{
	UPoseSearchSchema* Schema = LoadObject<UPoseSearchSchema>(nullptr,
		TEXT("/Game/Blueprints/Characters/Demo/Animation/MotionMatching/PSS_MH_Move.PSS_MH_Move"));
	if (!Test.TestNotNull(TEXT("loads PSS_MH_Move"), Schema))
	{
		return;
	}

	const UPoseSearchFeatureChannel_Curve* StopGaitChannel = nullptr;
	const UPoseSearchFeatureChannel_Curve* MoveGaitChannel = nullptr;
	for (const TObjectPtr<UPoseSearchFeatureChannel>& Channel : Schema->GetChannels())
	{
		if (const UPoseSearchFeatureChannel_Curve* Curve = Cast<UPoseSearchFeatureChannel_Curve>(Channel.Get()))
		{
			if (Curve->CurveName == TEXT("MM_StopGait"))
			{
				StopGaitChannel = Curve;
			}
			else if (Curve->CurveName == TEXT("MM_MoveGait"))
			{
				MoveGaitChannel = Curve;
			}
		}
	}
	Test.TestEqual(TEXT("PMM-7 PSS has six formal channels"), Schema->GetChannels().Num(), 6);
	Test.TestEqual(TEXT("PMM-7 PSS has MoveGait and StopGait dimensions"), Schema->SchemaCardinality, 32);
	Test.TestNotNull(TEXT("PSS has MM_StopGait channel"), StopGaitChannel);
	if (StopGaitChannel)
	{
		Test.TestTrue(TEXT("MM_StopGait uses the native query channel"),
			StopGaitChannel->IsA<UMHGZPoseSearchFeatureChannel_StopGait>());
		Test.TestTrue(TEXT("MM_StopGait uses its documented initial weight"),
			FMath::IsNearlyEqual(StopGaitChannel->Weight, PMMStopGaitWeight, PMMValueTolerance));
	}
	Test.TestNotNull(TEXT("PSS has MM_MoveGait channel"), MoveGaitChannel);
	if (MoveGaitChannel)
	{
		Test.TestTrue(TEXT("MM_MoveGait uses the native query channel"),
			MoveGaitChannel->IsA<UMHGZPoseSearchFeatureChannel_MoveGait>());
		Test.TestTrue(TEXT("MM_MoveGait uses its documented initial weight"),
			FMath::IsNearlyEqual(MoveGaitChannel->Weight, PMMMoveGaitWeight, PMMValueTolerance));
	}
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZPMMAssetDatabaseMembership,
	"MHGZ.PMM.Assets.DatabaseMembership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZPMMAssetDatabaseMembership::RunTest(const FString& Parameters)
{
	TArray<UAnimSequenceBase*> Sequences;
	GatherAllFormalSequences(*this, Sequences, true);
	TestEqual(TEXT("PMM-7 formal candidate count"), Sequences.Num(), 20);
	ValidatePMM7Schema(*this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZPMMAssetCurveSemantics,
	"MHGZ.PMM.Assets.CurveSemantics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZPMMAssetCurveSemantics::RunTest(const FString& Parameters)
{
	TArray<UAnimSequenceBase*> Sequences;
	GatherAllFormalSequences(*this, Sequences, true);
	for (const UAnimSequenceBase* Sequence : Sequences)
	{
		if (Sequence)
		{
			ValidateRoleAndCurves(*this, *Sequence);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZPMMAssetPoseSearchControlNotifies,
	"MHGZ.PMM.Assets.PoseSearchControlNotifies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZPMMAssetPoseSearchControlNotifies::RunTest(const FString& Parameters)
{
	TArray<UAnimSequenceBase*> Sequences;
	GatherAllFormalSequences(*this, Sequences, true);
	for (const UAnimSequenceBase* Sequence : Sequences)
	{
		if (Sequence)
		{
			ValidatePoseSearchControlNotifies(*this, *Sequence);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZPMMGeneratedStopIndexLifecycle,
	"MHGZ.PMM.Assets.GeneratedStopIndexLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZPMMGeneratedStopIndexLifecycle::RunTest(const FString& Parameters)
{
	ValidateGeneratedStopIndexLifecycle(*this);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZPMMLoopOnlyCandidateAssets,
	"MHGZ.PMM.Assets.LoopOnlyCandidates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZPMMLoopOnlyCandidateAssets::RunTest(const FString& Parameters)
{
	using namespace UE::PoseSearch;

	struct FLoopOnlySpec
	{
		const TCHAR* DatabasePath;
		const TCHAR* SequenceName;
	};
	const FLoopOnlySpec Specs[] =
	{
		{
			TEXT("/Game/Blueprints/Characters/Demo/Animation/MotionMatching/PSD_MH_Shth_Run_LoopOnly.PSD_MH_Shth_Run_LoopOnly"),
			TEXT("AS_Shth_Run_Loop")
		},
		{
			TEXT("/Game/Blueprints/Characters/Demo/Animation/MotionMatching/PSD_MH_Shth_Sprint_LoopOnly.PSD_MH_Shth_Sprint_LoopOnly"),
			TEXT("AS_Shth_Sprint_Loop_125x")
		}
	};

	UPoseSearchDatabase* FullMoveDatabase = LoadObject<UPoseSearchDatabase>(nullptr,
		TEXT("/Game/Blueprints/Characters/Demo/Animation/MotionMatching/PSD_MH_Shth_Move.PSD_MH_Shth_Move"));
	if (!TestNotNull(TEXT("loads full sheathed Move database for LoopOnly comparison"), FullMoveDatabase))
	{
		return false;
	}

	for (const FLoopOnlySpec& Spec : Specs)
	{
		UPoseSearchDatabase* Database = LoadObject<UPoseSearchDatabase>(nullptr, Spec.DatabasePath);
		if (!TestNotNull(FString::Printf(TEXT("loads LoopOnly database %s"), Spec.DatabasePath), Database))
		{
			continue;
		}
		TestEqual(FString::Printf(TEXT("%s has one candidate"), Spec.DatabasePath),
			Database->GetNumAnimationAssets(), 1);
		TestTrue(FString::Printf(TEXT("%s shares the full-move PSS"), Spec.DatabasePath),
			Database->Schema == FullMoveDatabase->Schema);
		TestEqual(FString::Printf(TEXT("%s shares the full-move search mode"), Spec.DatabasePath),
			Database->PoseSearchMode, FullMoveDatabase->PoseSearchMode);
		TestTrue(FString::Printf(TEXT("%s shares Continuing Pose Cost Bias"), Spec.DatabasePath),
			FMath::IsNearlyEqual(Database->ContinuingPoseCostBias,
				FullMoveDatabase->ContinuingPoseCostBias, PMMValueTolerance));
		TestEqual(FString::Printf(TEXT("%s index builds"), Spec.DatabasePath),
			FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(Database,
				ERequestAsyncBuildFlag::NewRequest | ERequestAsyncBuildFlag::WaitForCompletion),
			EAsyncBuildIndexResult::Success);
		TestTrue(FString::Printf(TEXT("%s has no indexed BlockTransition"), Spec.DatabasePath),
			!Database->GetSearchIndex().bAnyBlockTransition);

		const FPoseSearchDatabaseSequence* Entry =
			Database->GetDatabaseAnimationAsset<FPoseSearchDatabaseSequence>(0);
		const UAnimSequenceBase* Sequence = Entry ? Entry->Sequence.Get() : nullptr;
		if (!TestNotNull(FString::Printf(TEXT("%s resolves its one sequence"), Spec.DatabasePath), Sequence))
		{
			continue;
		}
		TestEqual(FString::Printf(TEXT("%s exposes the expected Loop"), Spec.DatabasePath),
			Sequence->GetName(), FString(Spec.SequenceName));
		TestTrue(FString::Printf(TEXT("%s candidate disables reselection"), Spec.DatabasePath),
			Entry->bDisableReselection);
		TestTrue(FString::Printf(TEXT("%s candidate uses an unbounded range"), Spec.DatabasePath),
			FMath::IsNearlyZero(Entry->SamplingRange.Min, PMMValueTolerance)
				&& FMath::IsNearlyZero(Entry->SamplingRange.Max, PMMValueTolerance));
		TestTrue(FString::Printf(TEXT("%s Loop source carries no PoseSearch control notifies"), Spec.DatabasePath),
			Sequence->Notifies.IsEmpty());
		ValidateRoleAndCurves(*this, *Sequence);
	}
	return true;
}
#endif
