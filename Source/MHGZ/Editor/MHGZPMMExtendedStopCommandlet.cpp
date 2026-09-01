// Copyright MHGZ Project. All Rights Reserved.

#include "Editor/MHGZPMMExtendedStopCommandlet.h"

#if WITH_EDITOR

#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimCurveTypes.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/Skeleton.h"
#include "AnimationBlueprintLibrary.h"
#include "BoneIndices.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/PackageName.h"
#include "PoseSearch/PoseSearchAnimNotifies.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "ReferenceSkeleton.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
constexpr TCHAR RunLoopPath[] = TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Run_Loop.AS_Shth_Run_Loop");
constexpr TCHAR RunStopLeftPath[] = TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Run_Stop_Left.AS_Shth_Run_Stop_Left");
constexpr TCHAR RunStopRightPath[] = TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Run_Stop_Right.AS_Shth_Run_Stop_Right");
constexpr TCHAR WalkLoopPath[] = TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Walk_Loop.AS_Shth_Walk_Loop");
constexpr TCHAR WalkStopLeftPath[] = TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Walk_Stop_Left.AS_Shth_Walk_Stop_Left");
constexpr TCHAR WalkStopRightPath[] = TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Walk_Stop_Right.AS_Shth_Walk_Stop_Right");
constexpr TCHAR SprintLoopPath[] = TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Sprint_Loop_125x.AS_Shth_Sprint_Loop_125x");
constexpr TCHAR WalkStartPath[] = TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Walk_Start.AS_Shth_Walk_Start");
constexpr TCHAR RunStartPath[] = TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Run_Start.AS_Shth_Run_Start");
constexpr TCHAR SprintStartPath[] = TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Sprint_Start.AS_Shth_Sprint_Start");
constexpr TCHAR GeneratedDirectory[] = TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/Generated");
constexpr TCHAR SchemaPath[] = TEXT("/Game/Blueprints/Characters/Demo/Animation/MotionMatching/PSS_MH_Move.PSS_MH_Move");
constexpr TCHAR GeneratedLeftName[] = TEXT("AS_Shth_Run_Stop_Left_Extended");
constexpr TCHAR GeneratedRightName[] = TEXT("AS_Shth_Run_Stop_Right_Extended");
constexpr TCHAR GeneratedWalkLeftName[] = TEXT("AS_Shth_Walk_Stop_Left_Extended");
constexpr TCHAR GeneratedWalkRightName[] = TEXT("AS_Shth_Walk_Stop_Right_Extended");
constexpr TCHAR GeneratedSprintLeftName[] = TEXT("AS_Shth_Sprint_Stop_Left_Extended");
constexpr TCHAR GeneratedSprintRightName[] = TEXT("AS_Shth_Sprint_Stop_Right_Extended");
constexpr TCHAR GeneratedWalkFirstStepCommitStopName[] = TEXT("AS_Shth_Walk_FirstStepCommitStop");
constexpr TCHAR GeneratedRunFirstStepCommitStopName[] = TEXT("AS_Shth_Run_FirstStepCommitStop");
constexpr TCHAR GeneratedSprintFirstStepCommitStopName[] = TEXT("AS_Shth_Sprint_FirstStepCommitStop");

// PMM-7.1 lifecycle boundaries are expressed in PSS sample indexes, never in
// arbitrary millisecond padding.  Three authored zero samples leave at least
// two real indexed zero poses even if Pose Search omits the exact sequence end.
constexpr int32 GeneratedStopZeroTailSampleCount = 3;
constexpr int32 GeneratedStopBlockLeadSampleCount = 1;
constexpr float StopContinuingModifier = -0.50f;
constexpr int32 MinimumFirstStepCommitPrefixFrames = 3;
constexpr int32 RunAndSprintFirstStepSearchFrame = 20;
constexpr float WalkStopGaitValue = 1.0f / 3.0f;
constexpr float RunStopGaitValue = 2.0f / 3.0f;
constexpr float SprintStopGaitValue = 1.0f;

struct FFrameRef
{
	const UAnimSequence* Source = nullptr;
	int32 Frame = INDEX_NONE;
	bool bIsPrefix = false;
};

struct FMatchResult
{
	int32 LoopFrame = INDEX_NONE;
	float Cost = TNumericLimits<float>::Max();
};

struct FGeneratedStopInfo
{
	FString OutputPath;
	FName CandidateKind;
	FName Family;
	FName Side;
	int32 LoopEntryFrame = INDEX_NONE;
	int32 OtherLoopEntryFrame = INDEX_NONE;
	float MatchCost = 0.0f;
	int32 PrefixFrames = 0;
	int32 StopFrames = 0;
	int32 TotalFrames = 0;
	float PrefixDuration = 0.0f;
	float SeamRootStepCm = 0.0f;
	float ExtractedRootMotionCm = 0.0f;
	bool bEnableRootMotion = false;
	bool bForceRootLock = true;
};

struct FGeneratedStopTiming
{
	int32 SampleRate = 0;
	int32 CommitSampleIndex = INDEX_NONE;
	int32 BlockStartSampleIndex = INDEX_NONE;
	int32 ZeroTailStartSampleIndex = INDEX_NONE;
	float SampleInterval = 0.0f;
	float CommitTime = 0.0f;
	float BlockStartTime = 0.0f;
	float ZeroTailStartTime = 0.0f;
};

bool BuildGeneratedStopTiming(const float SequenceLength, const float PrefixDuration,
	const int32 SampleRate, FGeneratedStopTiming& OutTiming)
{
	if (SampleRate <= 0 || SequenceLength <= KINDA_SMALL_NUMBER || PrefixDuration < 0.0f)
	{
		return false;
	}

	OutTiming.SampleRate = SampleRate;
	OutTiming.SampleInterval = 1.0f / static_cast<float>(SampleRate);
	OutTiming.CommitSampleIndex = FMath::Max(0, FMath::CeilToInt(PrefixDuration * SampleRate));
	OutTiming.CommitTime = OutTiming.CommitSampleIndex * OutTiming.SampleInterval;
	OutTiming.BlockStartSampleIndex = FMath::Max(0,
		OutTiming.CommitSampleIndex - GeneratedStopBlockLeadSampleCount);
	OutTiming.BlockStartTime = OutTiming.BlockStartSampleIndex * OutTiming.SampleInterval;

	// Animation sequences are authored on the same 60 Hz grid as PSS_MH_Move.
	// Round here rather than subtracting a fractional epsilon so a generated
	// sequence of N samples always receives a deterministic indexed tail.
	const int32 LastSequenceSampleIndex = FMath::Max(0, FMath::RoundToInt(SequenceLength * SampleRate));
	OutTiming.ZeroTailStartSampleIndex = LastSequenceSampleIndex - GeneratedStopZeroTailSampleCount;
	OutTiming.ZeroTailStartTime = OutTiming.ZeroTailStartSampleIndex * OutTiming.SampleInterval;
	if (OutTiming.ZeroTailStartSampleIndex <= OutTiming.CommitSampleIndex
		|| OutTiming.ZeroTailStartTime <= OutTiming.CommitTime
		|| OutTiming.ZeroTailStartTime >= SequenceLength)
	{
		return false;
	}

	return true;
}

bool LoadPoseSearchSampleRate(int32& OutSampleRate)
{
	const UPoseSearchSchema* Schema = LoadObject<UPoseSearchSchema>(nullptr, SchemaPath);
	if (!Schema || Schema->SampleRate <= 0)
	{
		UE_LOG(LogTemp, Error, TEXT("[PMMExtendedStop] Could not load a valid PSS_MH_Move sample rate."));
		return false;
	}

	OutSampleRate = Schema->SampleRate;
	return true;
}

bool SaveAsset(UObject& Asset)
{
	UPackage* Package = Asset.GetOutermost();
	FString Filename;
	if (!Package || !FPackageName::TryConvertLongPackageNameToFilename(
		Package->GetName(), Filename, FPackageName::GetAssetPackageExtension()))
	{
		return false;
	}

	Package->MarkPackageDirty();
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	return UPackage::SavePackage(Package, &Asset, *Filename, SaveArgs);
}

float GetFrameTime(const UAnimSequence& Sequence, const int32 Frame)
{
	const IAnimationDataModel* Model = Sequence.GetDataModel();
	return Model ? static_cast<float>(Model->GetFrameRate().AsSeconds(FFrameNumber(Frame))) : 0.0f;
}

int32 GetFrameCount(const UAnimSequence& Sequence)
{
	const IAnimationDataModel* Model = Sequence.GetDataModel();
	return Model ? Model->GetNumberOfFrames() + 1 : 0;
}

bool GetTrackNames(const UAnimSequence& Sequence, TArray<FName>& OutTrackNames)
{
	const IAnimationDataModel* Model = Sequence.GetDataModel();
	if (!Model)
	{
		return false;
	}

	Model->GetBoneTrackNames(OutTrackNames);
	return !OutTrackNames.IsEmpty();
}

bool SampleBoneLocalTransform(const UAnimSequence& Sequence, const int32 SkeletonBoneIndex,
	const float Time, FTransform& OutTransform)
{
	if (SkeletonBoneIndex == INDEX_NONE)
	{
		return false;
	}

	Sequence.GetBoneTransform(OutTransform, FSkeletonPoseBoneIndex(SkeletonBoneIndex),
		FAnimExtractContext(static_cast<double>(Time)), true);
	return true;
}

FVector SampleLocalVelocity(const UAnimSequence& Sequence, const int32 SkeletonBoneIndex,
	const int32 Frame, const bool bLooping)
{
	const int32 FrameCount = GetFrameCount(Sequence);
	if (FrameCount < 2)
	{
		return FVector::ZeroVector;
	}

	const int32 PreviousFrame = bLooping
		? (Frame - 1 + FrameCount) % FrameCount
		: FMath::Max(0, Frame - 1);
	const int32 NextFrame = bLooping
		? (Frame + 1) % FrameCount
		: FMath::Min(FrameCount - 1, Frame + 1);
	const float PreviousTime = GetFrameTime(Sequence, PreviousFrame);
	const float NextTime = GetFrameTime(Sequence, NextFrame);
	FTransform PreviousTransform;
	FTransform NextTransform;
	if (!SampleBoneLocalTransform(Sequence, SkeletonBoneIndex, PreviousTime, PreviousTransform)
		|| !SampleBoneLocalTransform(Sequence, SkeletonBoneIndex, NextTime, NextTransform))
	{
		return FVector::ZeroVector;
	}

	const float DeltaTime = bLooping && NextFrame < PreviousFrame
		? Sequence.GetPlayLength() - PreviousTime + NextTime
		: FMath::Max(NextTime - PreviousTime, KINDA_SMALL_NUMBER);
	return (NextTransform.GetTranslation() - PreviousTransform.GetTranslation()) / DeltaTime;
}

TArray<FName> GetSharedPoseTracks(const UAnimSequence& Loop, const UAnimSequence& Stop)
{
	TArray<FName> LoopTracks;
	TArray<FName> StopTracks;
	if (!GetTrackNames(Loop, LoopTracks) || !GetTrackNames(Stop, StopTracks))
	{
		return {};
	}

	const USkeleton* Skeleton = Loop.GetSkeleton();
	if (!Skeleton)
	{
		return {};
	}

	const FName RootName = Skeleton->GetReferenceSkeleton().GetBoneName(0);
	TSet<FName> StopTrackSet(StopTracks);
	TArray<FName> SharedTracks;
	for (const FName TrackName : LoopTracks)
	{
		if (TrackName != RootName && StopTrackSet.Contains(TrackName))
		{
			SharedTracks.Add(TrackName);
		}
	}
	return SharedTracks;
}

float ScorePrefixFrameAgainstStopStart(const UAnimSequence& Prefix, const UAnimSequence& Stop,
	const int32 PrefixFrame, const bool bPrefixLoops, const TArray<FName>& PoseTracks)
{
	const USkeleton* Skeleton = Prefix.GetSkeleton();
	if (!Skeleton)
	{
		return TNumericLimits<float>::Max();
	}

	const FReferenceSkeleton& ReferenceSkeleton = Skeleton->GetReferenceSkeleton();
	const float PrefixTime = GetFrameTime(Prefix, PrefixFrame);
	const float StopTime = 0.0f;
	float Cost = 0.0f;
	int32 SampleCount = 0;
	for (const FName TrackName : PoseTracks)
	{
		const int32 BoneIndex = ReferenceSkeleton.FindBoneIndex(TrackName);
		FTransform LoopTransform;
		FTransform StopTransform;
		if (!SampleBoneLocalTransform(Prefix, BoneIndex, PrefixTime, LoopTransform)
			|| !SampleBoneLocalTransform(Stop, BoneIndex, StopTime, StopTransform))
		{
			continue;
		}

		const FVector PositionDelta = LoopTransform.GetTranslation() - StopTransform.GetTranslation();
		const float RotationDelta = LoopTransform.GetRotation().AngularDistance(StopTransform.GetRotation());
		const FVector VelocityDelta = SampleLocalVelocity(Prefix, BoneIndex, PrefixFrame, bPrefixLoops)
			- SampleLocalVelocity(Stop, BoneIndex, 0, false);

		// Position carries the visual seam, velocity distinguishes the two foot
		// phases when their static poses are similar, and rotation prevents a
		// superficially close but twisted torso/leg pose from winning.
		Cost += PositionDelta.SizeSquared() * 0.01f;
		Cost += FMath::Square(RotationDelta) * 10.0f;
		Cost += VelocityDelta.SizeSquared() * 0.0002f;
		++SampleCount;
	}

	return SampleCount > 0 ? Cost / static_cast<float>(SampleCount) : TNumericLimits<float>::Max();
}

FMatchResult FindBestLoopEntry(const UAnimSequence& Loop, const UAnimSequence& Stop,
	const TArray<FName>& PoseTracks)
{
	FMatchResult Result;
	const int32 FrameCount = GetFrameCount(Loop);
	for (int32 Frame = 0; Frame < FrameCount; ++Frame)
	{
		const float Cost = ScorePrefixFrameAgainstStopStart(Loop, Stop, Frame, true, PoseTracks);
		if (Cost < Result.Cost)
		{
			Result.LoopFrame = Frame;
			Result.Cost = Cost;
		}
	}
	return Result;
}

FMatchResult FindFirstStepCommitJoin(const UAnimSequence& Start, const UAnimSequence& Stop,
	const TArray<FName>& PoseTracks, const int32 RequestedFirstSearchFrame)
{
	const int32 FrameCount = GetFrameCount(Start);
	if (FrameCount <= MinimumFirstStepCommitPrefixFrames)
	{
		return {};
	}

	// FirstStepCommitStop intentionally commits the first Start step. Stop frame
	// zero represents its planted-foot pose, and the first local minimum of the
	// Start -> Stop seam cost identifies that first usable landing. Later minima
	// belong to the loop-like tail and are handled by normal ExtendedStop.
	TArray<float> Costs;
	Costs.Reserve(FrameCount);
	for (int32 Frame = 0; Frame < FrameCount; ++Frame)
	{
		Costs.Add(ScorePrefixFrameAgainstStopStart(Start, Stop, Frame, false, PoseTracks));
	}

	const int32 FirstFrame = FMath::Clamp(RequestedFirstSearchFrame,
		MinimumFirstStepCommitPrefixFrames - 1, FrameCount - 1);
	for (int32 Frame = FirstFrame; Frame < FrameCount - 1; ++Frame)
	{
		const float PreviousCost = Costs[Frame - 1];
		const float ThisCost = Costs[Frame];
		const float NextCost = Costs[Frame + 1];
		if (FMath::IsFinite(ThisCost) && ThisCost <= PreviousCost && ThisCost < NextCost)
		{
			return { Frame, ThisCost };
		}
	}

	// Imported sequences can contain a completely flat contact plateau. If no
	// strict local minimum survives, use the best frame inside the requested
	// range, never an arbitrary first frame before the committed first step.
	FMatchResult Fallback;
	for (int32 Frame = FirstFrame; Frame < FrameCount; ++Frame)
	{
		if (FMath::IsFinite(Costs[Frame]) && Costs[Frame] < Fallback.Cost)
		{
			Fallback = { Frame, Costs[Frame] };
		}
	}
	return Fallback;
}

bool AppendCircularPrefixFrames(const UAnimSequence& Loop, const int32 StartExclusive,
	const int32 EndInclusive, TArray<FFrameRef>& OutFrames)
{
	const int32 FrameCount = GetFrameCount(Loop);
	if (FrameCount < 2 || StartExclusive < 0 || StartExclusive >= FrameCount
		|| EndInclusive < 0 || EndInclusive >= FrameCount)
	{
		return false;
	}

	int32 Frame = (StartExclusive + 1) % FrameCount;
	for (int32 Iteration = 0; Iteration < FrameCount; ++Iteration)
	{
		OutFrames.Add({ &Loop, Frame, true });
		if (Frame == EndInclusive)
		{
			return true;
		}
		Frame = (Frame + 1) % FrameCount;
	}
	return false;
}

bool BuildFramePlan(const UAnimSequence& Loop, const UAnimSequence& Stop,
	const int32 OtherEntryFrame, const int32 ThisEntryFrame, TArray<FFrameRef>& OutFrames,
	int32& OutPrefixFrameCount)
{
	OutFrames.Reset();
	if (!AppendCircularPrefixFrames(Loop, OtherEntryFrame, ThisEntryFrame, OutFrames))
	{
		return false;
	}
	OutPrefixFrameCount = OutFrames.Num();

	const int32 StopFrameCount = GetFrameCount(Stop);
	if (StopFrameCount < 2)
	{
		return false;
	}
	for (int32 Frame = 0; Frame < StopFrameCount; ++Frame)
	{
		OutFrames.Add({ &Stop, Frame, false });
	}
	return true;
}

bool BuildFirstStepCommitFramePlan(const UAnimSequence& Start, const UAnimSequence& Stop,
	const int32 StartJoinFrame, TArray<FFrameRef>& OutFrames, int32& OutPrefixFrameCount)
{
	OutFrames.Reset();
	const int32 StartFrameCount = GetFrameCount(Start);
	if (StartJoinFrame < MinimumFirstStepCommitPrefixFrames - 1 || StartJoinFrame >= StartFrameCount)
	{
		return false;
	}
	for (int32 Frame = 0; Frame <= StartJoinFrame; ++Frame)
	{
		OutFrames.Add({ &Start, Frame, true });
	}
	OutPrefixFrameCount = OutFrames.Num();

	const int32 StopFrameCount = GetFrameCount(Stop);
	if (StopFrameCount < 2)
	{
		return false;
	}
	for (int32 Frame = 0; Frame < StopFrameCount; ++Frame)
	{
		OutFrames.Add({ &Stop, Frame, false });
	}
	return true;
}

bool CollectOutputTracks(const UAnimSequence& Loop, const UAnimSequence& Stop,
	TArray<FName>& OutTrackNames)
{
	TArray<FName> LoopTracks;
	TArray<FName> StopTracks;
	if (!GetTrackNames(Loop, LoopTracks) || !GetTrackNames(Stop, StopTracks))
	{
		return false;
	}

	TSet<FName> UniqueTracks;
	for (const FName TrackName : LoopTracks)
	{
		UniqueTracks.Add(TrackName);
	}
	for (const FName TrackName : StopTracks)
	{
		UniqueTracks.Add(TrackName);
	}

	// UAnimSequence root-motion extraction expects the root to be compressed as
	// the first track. Sorting every track alphabetically can put Hips (or an
	// equivalent bone) before Root, causing ExtractRootMotion to fall back to
	// the reference pose even though the raw root keys exist.
	const USkeleton* Skeleton = Loop.GetSkeleton();
	if (!Skeleton)
	{
		return false;
	}
	const FName RootName = Skeleton->GetReferenceSkeleton().GetBoneName(0);
	if (!UniqueTracks.Remove(RootName))
	{
		return false;
	}
	OutTrackNames.Reset();
	OutTrackNames.Reserve(UniqueTracks.Num() + 1);
	OutTrackNames.Add(RootName);
	TArray<FName> RemainingTracks;
	RemainingTracks.Reserve(UniqueTracks.Num());
	for (const FName TrackName : UniqueTracks)
	{
		RemainingTracks.Add(TrackName);
	}
	RemainingTracks.Sort([](const FName A, const FName B)
	{
		return A.LexicalLess(B);
	});
	OutTrackNames.Append(RemainingTracks);
	return !OutTrackNames.IsEmpty();
}

bool SampleOutputTrack(const TArray<FFrameRef>& Frames, const FName TrackName,
	const USkeleton& Skeleton, TArray<FVector3f>& OutPositions, TArray<FQuat4f>& OutRotations,
	TArray<FVector3f>& OutScales)
{
	const FReferenceSkeleton& ReferenceSkeleton = Skeleton.GetReferenceSkeleton();
	const int32 BoneIndex = ReferenceSkeleton.FindBoneIndex(TrackName);
	if (BoneIndex == INDEX_NONE || Frames.IsEmpty())
	{
		return false;
	}

	const FName RootName = ReferenceSkeleton.GetBoneName(0);
	const bool bIsRoot = TrackName == RootName;
	FTransform PreviousRawRoot;
	FTransform PreviousOutputRoot;
	const UAnimSequence* PreviousSource = nullptr;
	int32 PreviousFrame = INDEX_NONE;

	OutPositions.Reset(Frames.Num());
	OutRotations.Reset(Frames.Num());
	OutScales.Reset(Frames.Num());
	for (int32 Index = 0; Index < Frames.Num(); ++Index)
	{
		const FFrameRef& FrameRef = Frames[Index];
		if (!FrameRef.Source)
		{
			return false;
		}

		FTransform LocalTransform;
		if (!SampleBoneLocalTransform(*FrameRef.Source, BoneIndex,
			GetFrameTime(*FrameRef.Source, FrameRef.Frame), LocalTransform))
		{
			return false;
		}

		if (bIsRoot)
		{
			const FTransform RawRoot = LocalTransform;
			if (Index == 0)
			{
				PreviousOutputRoot = LocalTransform;
			}
			else
			{
				FTransform RootDelta = FTransform::Identity;
				const bool bSameForwardSource = FrameRef.Source == PreviousSource
					&& FrameRef.Frame > PreviousFrame;
				if (bSameForwardSource)
				{
					RootDelta = LocalTransform.GetRelativeTransform(PreviousRawRoot);
				}
				else if (FrameRef.Source == PreviousSource && FrameRef.Source->bLoop)
				{
					FTransform LoopEndRoot;
					FTransform LoopStartRoot;
					const int32 SourceLastFrame = GetFrameCount(*FrameRef.Source) - 1;
					if (!SampleBoneLocalTransform(*FrameRef.Source, BoneIndex,
						GetFrameTime(*FrameRef.Source, SourceLastFrame), LoopEndRoot)
						|| !SampleBoneLocalTransform(*FrameRef.Source, BoneIndex, 0.0f, LoopStartRoot))
					{
						return false;
					}
					RootDelta = LocalTransform.GetRelativeTransform(LoopStartRoot)
						* LoopEndRoot.GetRelativeTransform(PreviousRawRoot);
				}
				// A Loop -> Stop seam is authored as a pose match. Its source roots
				// start in unrelated local spaces, so carry the previous output root
				// across that one frame and resume with Stop-relative deltas next frame.
				PreviousOutputRoot = RootDelta * PreviousOutputRoot;
				LocalTransform = PreviousOutputRoot;
			}

			PreviousRawRoot = RawRoot;
		}

		OutPositions.Add(FVector3f(LocalTransform.GetTranslation()));
		OutRotations.Add(FQuat4f(LocalTransform.GetRotation()));
		OutScales.Add(FVector3f(LocalTransform.GetScale3D()));
		PreviousSource = FrameRef.Source;
		PreviousFrame = FrameRef.Frame;
	}
	return true;
}

bool ReplaceFloatCurve(UAnimSequence& Sequence, const FName CurveName,
	const TArray<float>& Times, const TArray<float>& Values)
{
	if (Times.Num() != Values.Num() || Times.Num() < 2)
	{
		return false;
	}
	if (Sequence.HasCurveData(CurveName, true))
	{
		UAnimationBlueprintLibrary::RemoveCurve(&Sequence, CurveName);
	}
	UAnimationBlueprintLibrary::AddCurve(&Sequence, CurveName, ERawCurveTrackTypes::RCT_Float);
	UAnimationBlueprintLibrary::AddFloatCurveKeys(&Sequence, CurveName, Times, Values);
	return Sequence.HasCurveData(CurveName, true);
}

bool GetFloatCurveKeys(const UAnimSequence& Sequence, const FName CurveName,
	TArray<float>& OutTimes, TArray<float>& OutValues)
{
	const FFloatCurve* Curve = static_cast<const FFloatCurve*>(
		Sequence.GetCurveData().GetCurveData(CurveName, ERawCurveTrackTypes::RCT_Float));
	if (!Curve)
	{
		return false;
	}
	Curve->GetKeys(OutTimes, OutValues);
	return OutTimes.Num() == OutValues.Num() && OutTimes.Num() >= 2;
}

bool ConfigureExtendedCurves(UAnimSequence& Output, const UAnimSequence& SourceStop,
	const float PrefixDuration, const float StopGaitValue, const FGeneratedStopTiming& Timing)
{
	TArray<float> SourceIntentTimes;
	TArray<float> SourceIntentValues;
	TArray<float> SourceDistanceTimes;
	TArray<float> SourceDistanceValues;
	if (!GetFloatCurveKeys(SourceStop, TEXT("MM_Intent"), SourceIntentTimes, SourceIntentValues)
		|| !GetFloatCurveKeys(SourceStop, TEXT("MM_DistanceToStop"), SourceDistanceTimes, SourceDistanceValues))
	{
		return false;
	}

	auto AddKey = [](TArray<float>& Times, TArray<float>& Values, const float Time, const float Value)
	{
		if (!Times.IsEmpty() && FMath::IsNearlyEqual(Times.Last(), Time, KINDA_SMALL_NUMBER))
		{
			Values.Last() = Value;
			return;
		}
		Times.Add(Time);
		Values.Add(Value);
	};

	TArray<float> IntentTimes;
	TArray<float> IntentValues;
	AddKey(IntentTimes, IntentValues, 0.0f, -1.0f);
	AddKey(IntentTimes, IntentValues, PrefixDuration, -1.0f);
	for (int32 Index = 0; Index < SourceIntentTimes.Num(); ++Index)
	{
		const float OutputTime = PrefixDuration + SourceIntentTimes[Index];
		if (SourceIntentTimes[Index] > KINDA_SMALL_NUMBER && OutputTime < Timing.ZeroTailStartTime)
		{
			AddKey(IntentTimes, IntentValues, OutputTime, SourceIntentValues[Index]);
		}
	}
	AddKey(IntentTimes, IntentValues, Timing.ZeroTailStartTime, 0.0f);
	AddKey(IntentTimes, IntentValues, Output.GetPlayLength(), 0.0f);

	// Distance is deliberately neutral throughout the copied Loop prefix. Once
	// the real Stop begins, it preserves the source stop's authored curve so
	// PMM-7 can later sample that selected candidate instead of re-deriving it
	// from CharacterMovement speed.
	TArray<float> DistanceTimes;
	TArray<float> DistanceValues;
	AddKey(DistanceTimes, DistanceValues, 0.0f, 0.0f);
	AddKey(DistanceTimes, DistanceValues,
		FMath::Max(0.0f, PrefixDuration - Timing.SampleInterval), 0.0f);
	for (int32 Index = 0; Index < SourceDistanceTimes.Num(); ++Index)
	{
		const float OutputTime = PrefixDuration + SourceDistanceTimes[Index];
		if (OutputTime < Timing.ZeroTailStartTime)
		{
			AddKey(DistanceTimes, DistanceValues, OutputTime, SourceDistanceValues[Index]);
		}
	}
	AddKey(DistanceTimes, DistanceValues, Timing.ZeroTailStartTime, 0.0f);
	AddKey(DistanceTimes, DistanceValues, Output.GetPlayLength(), 0.0f);

	return ReplaceFloatCurve(Output, TEXT("MM_Intent"), IntentTimes, IntentValues)
		&& ReplaceFloatCurve(Output, TEXT("MM_DistanceToStop"), DistanceTimes, DistanceValues)
		// Keep the family lane constant across the complete generated candidate.
		// The runtime Stop query owns this lane from the release edge until
		// MM_Intent/MM_DistanceToStop reach zero; fading this curve previously
		// allowed a Run release to resemble a Walk Stop partway through a search.
		&& ReplaceFloatCurve(Output, TEXT("MM_StopGait"),
			{ 0.0f, Output.GetPlayLength() }, { StopGaitValue, StopGaitValue });
}

bool ConfigureExtendedStopNotifies(UAnimSequence& Sequence, const FGeneratedStopTiming& Timing)
{
	const FName ControlTrackName(TEXT("PoseSearchControl"));
	Sequence.Notifies.RemoveAll([](const FAnimNotifyEvent& Notify)
	{
		return Cast<UAnimNotifyState_PoseSearchBlockTransition>(Notify.NotifyStateClass) != nullptr
			|| Cast<UAnimNotifyState_PoseSearchOverrideContinuingPoseCostBias>(Notify.NotifyStateClass) != nullptr;
	});
	Sequence.RefreshCacheData();
	if (!UAnimationBlueprintLibrary::IsValidAnimNotifyTrackName(&Sequence, ControlTrackName))
	{
		UAnimationBlueprintLibrary::AddAnimationNotifyTrack(&Sequence, ControlTrackName, FLinearColor::White);
	}

	const float Length = Sequence.GetPlayLength();
	// The prefix is the only globally searchable part of a generated Stop. The
	// Block starts one PSS sample before the committed source Stop and covers the
	// complete authored tail. Continuing playback is intentionally not filtered
	// by BlockTransition, so it remains legal through the final indexed zero pose.
	const float BlockStart = Timing.BlockStartTime;
	const float BlockEnd = Length;
	UAnimNotifyState* BlockState = UAnimationBlueprintLibrary::AddAnimationNotifyStateEvent(
		&Sequence, ControlTrackName, BlockStart, BlockEnd - BlockStart,
		UAnimNotifyState_PoseSearchBlockTransition::StaticClass());
	UAnimNotifyState_PoseSearchOverrideContinuingPoseCostBias* ContinuingState =
		Cast<UAnimNotifyState_PoseSearchOverrideContinuingPoseCostBias>(
			UAnimationBlueprintLibrary::AddAnimationNotifyStateEvent(&Sequence, ControlTrackName,
				0.0f, Length,
				UAnimNotifyState_PoseSearchOverrideContinuingPoseCostBias::StaticClass()));
	if (ContinuingState)
	{
		ContinuingState->CostAddend = StopContinuingModifier;
	}

	Sequence.SortNotifies();
	Sequence.RefreshCacheData();
	return BlockState != nullptr && ContinuingState != nullptr;
}

const FFloatCurve* FindFloatCurve(const UAnimSequence& Sequence, const FName CurveName)
{
	return static_cast<const FFloatCurve*>(Sequence.GetCurveData().GetCurveData(
		CurveName, ERawCurveTrackTypes::RCT_Float));
}

bool CalculateRootStep(const UAnimSequence& Sequence, const int32 FromFrame,
	const int32 ToFrame, float& OutDistance)
{
	const USkeleton* Skeleton = Sequence.GetSkeleton();
	if (!Skeleton || FromFrame < 0 || ToFrame < 0)
	{
		return false;
	}

	const int32 RootIndex = Skeleton->GetReferenceSkeleton().FindBoneIndex(
		Skeleton->GetReferenceSkeleton().GetBoneName(0));
	FTransform FromRoot;
	FTransform ToRoot;
	if (!SampleBoneLocalTransform(Sequence, RootIndex, GetFrameTime(Sequence, FromFrame), FromRoot)
		|| !SampleBoneLocalTransform(Sequence, RootIndex, GetFrameTime(Sequence, ToFrame), ToRoot))
	{
		return false;
	}
	OutDistance = FVector::Distance(FromRoot.GetTranslation(), ToRoot.GetTranslation());
	return true;
}

FTransform GetExtractedRootMotionTransform(const UAnimSequence& Sequence, const float StartTime,
	const float DeltaTime)
{
	if (Sequence.GetPlayLength() <= KINDA_SMALL_NUMBER || FMath::IsNearlyZero(DeltaTime))
	{
		return FTransform::Identity;
	}

	const FAnimExtractContext Context(static_cast<double>(StartTime), true,
		FDeltaTimeRecord(DeltaTime), false);
	return Sequence.ExtractRootMotion(Context);
}

float GetExtractedRootMotionDistance(const UAnimSequence& Sequence)
{
	return GetExtractedRootMotionTransform(Sequence, 0.0f, Sequence.GetPlayLength()).GetTranslation().Size();
}

bool ValidateGeneratedStop(const UAnimSequence& Output, const UAnimSequence& Loop,
	const UAnimSequence& SourceStop, const FGeneratedStopTiming& Timing, FGeneratedStopInfo& Info)
{
	bool bValid = true;
	bValid &= Output.GetSkeleton() == Loop.GetSkeleton() && Output.GetSkeleton() == SourceStop.GetSkeleton();
	bValid &= !Output.bLoop;
	// These are ordinary locomotion candidates. They must have extractable
	// Animation Root Motion, not a root-locked pose-only presentation.
	bValid &= Output.bEnableRootMotion;
	bValid &= !Output.bForceRootLock;
	bValid &= GetFrameCount(Output) == Info.TotalFrames;
	bValid &= Info.PrefixFrames >= 2 && Info.StopFrames >= 2;

	const FFloatCurve* IntentCurve = FindFloatCurve(Output, TEXT("MM_Intent"));
	const FFloatCurve* DistanceCurve = FindFloatCurve(Output, TEXT("MM_DistanceToStop"));
	const FFloatCurve* StopGaitCurve = FindFloatCurve(Output, TEXT("MM_StopGait"));
	if (!IntentCurve || !DistanceCurve || !StopGaitCurve)
	{
		bValid = false;
	}
	else
	{
		const float PrefixLastTime = GetFrameTime(Output, Info.PrefixFrames - 1);
		bValid &= FMath::IsNearlyEqual(IntentCurve->Evaluate(0.0f), -1.0f, 0.01f);
		bValid &= FMath::IsNearlyEqual(IntentCurve->Evaluate(PrefixLastTime), -1.0f, 0.01f);
		bValid &= FMath::IsNearlyZero(IntentCurve->Evaluate(Timing.ZeroTailStartTime), 0.01f);
		bValid &= FMath::IsNearlyEqual(IntentCurve->Evaluate(Output.GetPlayLength()), 0.0f, 0.01f);
		bValid &= FMath::IsNearlyZero(DistanceCurve->Evaluate(0.0f), 0.01f);
		bValid &= FMath::IsNearlyZero(DistanceCurve->Evaluate(PrefixLastTime), 0.01f);
		bValid &= FMath::IsNearlyZero(DistanceCurve->Evaluate(Timing.ZeroTailStartTime), 0.01f);
		bValid &= FMath::IsNearlyZero(DistanceCurve->Evaluate(Output.GetPlayLength()), 0.01f);
		bValid &= StopGaitCurve->Evaluate(0.0f) > 0.0f;
		bValid &= StopGaitCurve->Evaluate(PrefixLastTime) > 0.0f;
		// StopGait is a discrete family lane, not a fade-out signal. It must
		// remain constant through the final indexed frame so an accepted Run or
		// Sprint Stop cannot be replaced by a pose-similar Walk Stop at its tail.
		bValid &= StopGaitCurve->Evaluate(Output.GetPlayLength()) > 0.0f;
	}

	int32 BlockCount = 0;
	int32 ContinuingCount = 0;
	for (const FAnimNotifyEvent& Notify : Output.Notifies)
	{
		if (Cast<UAnimNotifyState_PoseSearchBlockTransition>(Notify.NotifyStateClass))
		{
			++BlockCount;
			bValid &= FMath::IsNearlyEqual(Notify.GetTriggerTime(), Timing.BlockStartTime, 0.01f);
			bValid &= FMath::IsNearlyEqual(Notify.GetEndTriggerTime(), Output.GetPlayLength(), 0.01f);
		}
		else if (const UAnimNotifyState_PoseSearchOverrideContinuingPoseCostBias* Continuing =
			Cast<UAnimNotifyState_PoseSearchOverrideContinuingPoseCostBias>(Notify.NotifyStateClass))
		{
			++ContinuingCount;
			bValid &= FMath::IsNearlyZero(Notify.GetTriggerTime(), 0.01f);
			bValid &= FMath::IsNearlyEqual(Notify.GetEndTriggerTime(), Output.GetPlayLength(), 0.01f);
			bValid &= FMath::IsNearlyEqual(Continuing->CostAddend, StopContinuingModifier, 0.01f);
		}
	}
	bValid &= BlockCount == 1 && ContinuingCount == 1;

	float PreviousPrefixStep = 0.0f;
	if (!CalculateRootStep(Output, Info.PrefixFrames - 2, Info.PrefixFrames - 1, PreviousPrefixStep)
		|| !CalculateRootStep(Output, Info.PrefixFrames - 1, Info.PrefixFrames, Info.SeamRootStepCm))
	{
		bValid = false;
	}
	else
	{
		// Root motion may legitimately decelerate at the authored seam, but a
		// source-space reset must not create a one-frame teleport several times
		// larger than the immediately preceding Run root step.
		bValid &= Info.SeamRootStepCm <= FMath::Max(15.0f, PreviousPrefixStep * 3.0f);
	}

	Info.ExtractedRootMotionCm = GetExtractedRootMotionDistance(Output);
	Info.bEnableRootMotion = Output.bEnableRootMotion;
	Info.bForceRootLock = Output.bForceRootLock;
	// A valid Stop has authored forward travel. A near-zero extracted result
	// would be a pose-only asset that cannot drive the capsule in PIE.
	bValid &= Info.ExtractedRootMotionCm > 1.0f;

	if (!bValid)
	{
		const FTransform PrefixRootMotion = GetExtractedRootMotionTransform(Output, 0.0f, Info.PrefixDuration);
		const FTransform StopRootMotion = GetExtractedRootMotionTransform(Output, Info.PrefixDuration,
			Output.GetPlayLength() - Info.PrefixDuration);
		UE_LOG(LogTemp, Error, TEXT("[PMMExtendedStop] Validation failed for %s (EnableRootMotion=%d ForceRootLock=%d Frames=%d/%d Curves=%d/%d Notifies=%d/%d Seam=%.3f PrefixRM=%s StopRM=%s TotalRM=%.3fcm)."),
			*Output.GetPathName(), static_cast<int32>(Output.bEnableRootMotion), static_cast<int32>(Output.bForceRootLock),
			GetFrameCount(Output), Info.TotalFrames, IntentCurve != nullptr, DistanceCurve != nullptr,
			BlockCount, ContinuingCount, Info.SeamRootStepCm,
			*PrefixRootMotion.GetTranslation().ToString(), *StopRootMotion.GetTranslation().ToString(),
			Info.ExtractedRootMotionCm);
	}
	return bValid;
}

bool CreateStopCandidate(const UAnimSequence& PrefixSource, const UAnimSequence& SourceStop,
	const FName OutputName, const FName CandidateKind, const FName Family, const FName Side,
	const int32 OtherEntryFrame, const FMatchResult& ThisMatch, const bool bCircularPrefix,
	const float StopGaitValue, const bool bReplaceGenerated, FGeneratedStopInfo& OutInfo)
{
	TArray<FFrameRef> FramePlan;
	int32 PrefixFrameCount = 0;
	const bool bBuiltFramePlan = bCircularPrefix
		? BuildFramePlan(PrefixSource, SourceStop, OtherEntryFrame, ThisMatch.LoopFrame,
			FramePlan, PrefixFrameCount)
		: BuildFirstStepCommitFramePlan(PrefixSource, SourceStop, ThisMatch.LoopFrame,
			FramePlan, PrefixFrameCount);
	if (!bBuiltFramePlan)
	{
		UE_LOG(LogTemp, Error, TEXT("[PMMExtendedStop] Could not build the %s %s frame plan."),
			*Family.ToString(), *CandidateKind.ToString());
		return false;
	}

	const USkeleton* Skeleton = PrefixSource.GetSkeleton();
	if (!Skeleton || Skeleton != SourceStop.GetSkeleton())
	{
		UE_LOG(LogTemp, Error, TEXT("[PMMExtendedStop] %s uses an incompatible Skeleton."), *SourceStop.GetPathName());
		return false;
	}
	const IAnimationDataModel* PrefixModel = PrefixSource.GetDataModel();
	const IAnimationDataModel* StopModel = SourceStop.GetDataModel();
	if (!PrefixModel || !StopModel || PrefixModel->GetFrameRate() != StopModel->GetFrameRate())
	{
		UE_LOG(LogTemp, Error, TEXT("[PMMExtendedStop] Loop and %s must have the same source frame rate."), *SourceStop.GetPathName());
		return false;
	}

	TArray<FName> OutputTracks;
	if (!CollectOutputTracks(PrefixSource, SourceStop, OutputTracks))
	{
		UE_LOG(LogTemp, Error, TEXT("[PMMExtendedStop] Could not collect output tracks for %s."), *Side.ToString());
		return false;
	}

	const FString PackageName = FString::Printf(TEXT("%s/%s"), GeneratedDirectory, *OutputName.ToString());
	const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PackageName, *OutputName.ToString());
	UAnimSequence* Output = LoadObject<UAnimSequence>(nullptr, *ObjectPath);
	if (Output && !bReplaceGenerated)
	{
		UE_LOG(LogTemp, Error, TEXT("[PMMExtendedStop] %s already exists; rerun with -ReplaceGenerated to rebuild only generated assets."), *PackageName);
		return false;
	}
	if (!Output)
	{
		UPackage* Package = CreatePackage(*PackageName);
		Output = NewObject<UAnimSequence>(Package, OutputName, RF_Public | RF_Standalone);
	}
	if (!Output)
	{
		return false;
	}

	Output->Modify();
	Output->SetSkeleton(const_cast<USkeleton*>(Skeleton));
	Output->bLoop = false;
	Output->RootMotionRootLock = PrefixSource.RootMotionRootLock;
	Output->bUseNormalizedRootMotionScale = PrefixSource.bUseNormalizedRootMotionScale;

	IAnimationDataController& Controller = Output->GetController();
	Controller.InitializeModel();
	Output->ResetAnimation();
	Controller.OpenBracket(FText::FromString(TEXT("Build PMM Extended Stop")), false);
	Controller.SetFrameRate(PrefixModel->GetFrameRate(), false);
	Controller.SetNumberOfFrames(FFrameNumber(FramePlan.Num() - 1), false);
	for (const FName TrackName : OutputTracks)
	{
		TArray<FVector3f> Positions;
		TArray<FQuat4f> Rotations;
		TArray<FVector3f> Scales;
		if (!SampleOutputTrack(FramePlan, TrackName, *Skeleton, Positions, Rotations, Scales)
			|| !Controller.AddBoneCurve(TrackName, false)
			|| !Controller.SetBoneTrackKeys(TrackName, Positions, Rotations, Scales, false))
		{
			Controller.CloseBracket(false);
			UE_LOG(LogTemp, Error, TEXT("[PMMExtendedStop] Failed to bake track %s for %s."), *TrackName.ToString(), *PackageName);
			return false;
		}
	}
	Controller.NotifyPopulated();
	Controller.CloseBracket(false);
	// ResetAnimation clears sequence data. Assign the locomotion root-motion
	// contract afterwards so rebuilds of an existing generated asset cannot
	// retain an earlier root-lock state.
	Output->bEnableRootMotion = true;
	Output->bForceRootLock = false;

	const float PrefixDuration = static_cast<float>(PrefixModel->GetFrameRate().AsSeconds(
		FFrameNumber(PrefixFrameCount)));
	int32 PoseSearchSampleRate = 0;
	FGeneratedStopTiming Timing;
	if (!LoadPoseSearchSampleRate(PoseSearchSampleRate)
		|| !BuildGeneratedStopTiming(Output->GetPlayLength(), PrefixDuration, PoseSearchSampleRate, Timing)
		|| !ConfigureExtendedCurves(*Output, SourceStop, PrefixDuration, StopGaitValue, Timing)
		|| !ConfigureExtendedStopNotifies(*Output, Timing))
	{
		UE_LOG(LogTemp, Error, TEXT("[PMMExtendedStop] Failed to configure PMM-7.1 indexed curves or Pose Search notifies for %s."), *PackageName);
		return false;
	}

	Output->PostEditChange();
	Output->RefreshCacheData();
	if (!SaveAsset(*Output))
	{
		UE_LOG(LogTemp, Error, TEXT("[PMMExtendedStop] Failed to save %s."), *PackageName);
		return false;
	}

	OutInfo.OutputPath = Output->GetPathName();
	OutInfo.CandidateKind = CandidateKind;
	OutInfo.Family = Family;
	OutInfo.Side = Side;
	OutInfo.LoopEntryFrame = ThisMatch.LoopFrame;
	OutInfo.OtherLoopEntryFrame = OtherEntryFrame;
	OutInfo.MatchCost = ThisMatch.Cost;
	OutInfo.PrefixFrames = PrefixFrameCount;
	OutInfo.StopFrames = GetFrameCount(SourceStop);
	OutInfo.TotalFrames = FramePlan.Num();
	OutInfo.PrefixDuration = PrefixDuration;
	if (!ValidateGeneratedStop(*Output, PrefixSource, SourceStop, Timing, OutInfo))
	{
		return false;
	}
	UE_LOG(LogTemp, Display, TEXT("[PMMExtendedStop] Built %s %s PrefixFrames=%d StopFrames=%d TotalFrames=%d EntryFrame=%d Cost=%.6f ZeroTail=%d@%.3fs BlockStart=%d@%.3fs RootMotion=%.3fcm"),
		*CandidateKind.ToString(),
		*OutInfo.OutputPath, OutInfo.PrefixFrames, OutInfo.StopFrames, OutInfo.TotalFrames,
		OutInfo.LoopEntryFrame, OutInfo.MatchCost, Timing.ZeroTailStartSampleIndex,
		Timing.ZeroTailStartTime, Timing.BlockStartSampleIndex, Timing.BlockStartTime,
		OutInfo.ExtractedRootMotionCm);
	return true;
}

bool WriteBuildAudit(const TArray<FGeneratedStopInfo>& GeneratedStops)
{
	const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MotionMatching"));
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FDateTime Now = FDateTime::Now();
	const FString Filename = FPaths::Combine(Directory, FString::Printf(
		TEXT("ExtendedStopBuild-%04d%02d%02d-%02d%02d%02d.csv"), Now.GetYear(), Now.GetMonth(), Now.GetDay(),
		Now.GetHour(), Now.GetMinute(), Now.GetSecond()));
	FString Csv = TEXT("CandidateKind,Family,Side,EntryFrame,OtherEntryFrame,MatchCost,PrefixFrames,PrefixDurationSeconds,StopFrames,TotalFrames,SeamRootStepCm,ExtractedRootMotionCm,EnableRootMotion,ForceRootLock,Output\n");
	for (const FGeneratedStopInfo& Info : GeneratedStops)
	{
		Csv += FString::Printf(TEXT("%s,%s,%s,%d,%d,%.6f,%d,%.6f,%d,%d,%.6f,%.6f,%d,%d,%s\n"),
			*Info.CandidateKind.ToString(), *Info.Family.ToString(), *Info.Side.ToString(),
			Info.LoopEntryFrame, Info.OtherLoopEntryFrame,
			Info.MatchCost, Info.PrefixFrames, Info.PrefixDuration, Info.StopFrames,
			Info.TotalFrames, Info.SeamRootStepCm, Info.ExtractedRootMotionCm,
			static_cast<int32>(Info.bEnableRootMotion), static_cast<int32>(Info.bForceRootLock), *Info.OutputPath);
	}
	if (!FFileHelper::SaveStringToFile(Csv, *Filename, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		return false;
	}
	UE_LOG(LogTemp, Display, TEXT("[PMMExtendedStop] Wrote audit %s for %d generated assets."), *Filename, GeneratedStops.Num());
	return true;
}

bool GenerateFamily(const FName Family, const TCHAR* LoopPath, const TCHAR* StopLeftPath,
	const TCHAR* StopRightPath, const FName LeftOutputName, const FName RightOutputName,
	const float StopGaitValue, const bool bReplaceGenerated, TArray<FGeneratedStopInfo>& InOutGeneratedStops)
{
	UAnimSequence* Loop = LoadObject<UAnimSequence>(nullptr, LoopPath);
	UAnimSequence* StopLeft = LoadObject<UAnimSequence>(nullptr, StopLeftPath);
	UAnimSequence* StopRight = LoadObject<UAnimSequence>(nullptr, StopRightPath);
	if (!Loop || !StopLeft || !StopRight)
	{
		UE_LOG(LogTemp, Error, TEXT("[PMMExtendedStop] Could not load the %s Loop/Stop sources."), *Family.ToString());
		return false;
	}

	const TArray<FName> LeftPoseTracks = GetSharedPoseTracks(*Loop, *StopLeft);
	const TArray<FName> RightPoseTracks = GetSharedPoseTracks(*Loop, *StopRight);
	if (LeftPoseTracks.IsEmpty() || RightPoseTracks.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("[PMMExtendedStop] %s assets have no shared non-root pose tracks."), *Family.ToString());
		return false;
	}

	const FMatchResult LeftMatch = FindBestLoopEntry(*Loop, *StopLeft, LeftPoseTracks);
	const FMatchResult RightMatch = FindBestLoopEntry(*Loop, *StopRight, RightPoseTracks);
	if (LeftMatch.LoopFrame == INDEX_NONE || RightMatch.LoopFrame == INDEX_NONE
		|| !FMath::IsFinite(LeftMatch.Cost) || !FMath::IsFinite(RightMatch.Cost)
		|| LeftMatch.LoopFrame == RightMatch.LoopFrame)
	{
		UE_LOG(LogTemp, Error, TEXT("[PMMExtendedStop] Could not produce two distinct %s stop entry phases (Left=%d Right=%d)."),
			*Family.ToString(), LeftMatch.LoopFrame, RightMatch.LoopFrame);
		return false;
	}

	FGeneratedStopInfo LeftInfo;
	FGeneratedStopInfo RightInfo;
	const bool bGenerated = CreateStopCandidate(*Loop, *StopLeft, LeftOutputName, TEXT("ExtendedStop"),
		Family, TEXT("Left"), RightMatch.LoopFrame, LeftMatch, true, StopGaitValue, bReplaceGenerated, LeftInfo)
		&& CreateStopCandidate(*Loop, *StopRight, RightOutputName, TEXT("ExtendedStop"),
			Family, TEXT("Right"), LeftMatch.LoopFrame, RightMatch, true, StopGaitValue, bReplaceGenerated, RightInfo);
	if (!bGenerated)
	{
		return false;
	}

	InOutGeneratedStops.Add(LeftInfo);
	InOutGeneratedStops.Add(RightInfo);
	return true;
}

bool GenerateFirstStepCommitFamily(const FName Family, const TCHAR* StartPath, const TCHAR* StopLeftPath,
	const TCHAR* StopRightPath, const FName OutputName, const int32 FirstSearchFrame,
	const float StopGaitValue, const bool bReplaceGenerated, TArray<FGeneratedStopInfo>& InOutGeneratedStops)
{
	UAnimSequence* Start = LoadObject<UAnimSequence>(nullptr, StartPath);
	UAnimSequence* StopLeft = LoadObject<UAnimSequence>(nullptr, StopLeftPath);
	UAnimSequence* StopRight = LoadObject<UAnimSequence>(nullptr, StopRightPath);
	if (!Start || !StopLeft || !StopRight)
	{
		UE_LOG(LogTemp, Error, TEXT("[PMMExtendedStop] Could not load the %s FirstStepCommitStop sources."), *Family.ToString());
		return false;
	}

	const TArray<FName> LeftPoseTracks = GetSharedPoseTracks(*Start, *StopLeft);
	const TArray<FName> RightPoseTracks = GetSharedPoseTracks(*Start, *StopRight);
	if (LeftPoseTracks.IsEmpty() || RightPoseTracks.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("[PMMExtendedStop] %s FirstStepCommitStop sources have no shared non-root pose tracks."), *Family.ToString());
		return false;
	}

	const FMatchResult LeftMatch = FindFirstStepCommitJoin(*Start, *StopLeft, LeftPoseTracks, FirstSearchFrame);
	const FMatchResult RightMatch = FindFirstStepCommitJoin(*Start, *StopRight, RightPoseTracks, FirstSearchFrame);
	if (LeftMatch.LoopFrame == INDEX_NONE || RightMatch.LoopFrame == INDEX_NONE
		|| !FMath::IsFinite(LeftMatch.Cost) || !FMath::IsFinite(RightMatch.Cost))
	{
		UE_LOG(LogTemp, Error, TEXT("[PMMExtendedStop] Could not find a valid %s FirstStepCommitStop -> Stop join (Left=%d Right=%d)."),
			*Family.ToString(), LeftMatch.LoopFrame, RightMatch.LoopFrame);
		return false;
	}

	// The generated sequence must reach the first planted foot even after input
	// was released. Once that landing happens, Stop takes over. Choose the
	// earliest compatible side rather than a late lower-cost contact.
	const bool bUseLeftStop = LeftMatch.LoopFrame < RightMatch.LoopFrame;
	const UAnimSequence& ChosenStop = bUseLeftStop ? *StopLeft : *StopRight;
	const FMatchResult& ChosenMatch = bUseLeftStop ? LeftMatch : RightMatch;
	FGeneratedStopInfo FirstStepCommitInfo;
	if (!CreateStopCandidate(*Start, ChosenStop, OutputName, TEXT("FirstStepCommitStop"),
		Family, bUseLeftStop ? TEXT("Left") : TEXT("Right"), INDEX_NONE, ChosenMatch, false,
		StopGaitValue, bReplaceGenerated, FirstStepCommitInfo))
	{
		return false;
	}

	InOutGeneratedStops.Add(FirstStepCommitInfo);
	return true;
}
}

#endif

UMHGZPMMExtendedStopCommandlet::UMHGZPMMExtendedStopCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMHGZPMMExtendedStopCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	const bool bReplaceGenerated = FParse::Param(*Params, TEXT("ReplaceGenerated"));
	TArray<FGeneratedStopInfo> GeneratedStops;
	GeneratedStops.Reserve(9);
	bool bGenerated = true;
	bGenerated &= GenerateFamily(TEXT("Run"), RunLoopPath, RunStopLeftPath, RunStopRightPath,
		GeneratedLeftName, GeneratedRightName, RunStopGaitValue, bReplaceGenerated, GeneratedStops);
	bGenerated &= GenerateFirstStepCommitFamily(TEXT("Run"), RunStartPath, RunStopLeftPath, RunStopRightPath,
		GeneratedRunFirstStepCommitStopName, RunAndSprintFirstStepSearchFrame, RunStopGaitValue, bReplaceGenerated, GeneratedStops);
	bGenerated &= GenerateFamily(TEXT("Walk"), WalkLoopPath, WalkStopLeftPath, WalkStopRightPath,
		GeneratedWalkLeftName, GeneratedWalkRightName, WalkStopGaitValue, bReplaceGenerated, GeneratedStops);
	bGenerated &= GenerateFirstStepCommitFamily(TEXT("Walk"), WalkStartPath, WalkStopLeftPath, WalkStopRightPath,
		GeneratedWalkFirstStepCommitStopName, MinimumFirstStepCommitPrefixFrames - 1, WalkStopGaitValue, bReplaceGenerated, GeneratedStops);
	bGenerated &= GenerateFamily(TEXT("Sprint"), SprintLoopPath, RunStopLeftPath, RunStopRightPath,
		GeneratedSprintLeftName, GeneratedSprintRightName, SprintStopGaitValue, bReplaceGenerated, GeneratedStops);
	bGenerated &= GenerateFirstStepCommitFamily(TEXT("Sprint"), SprintStartPath, RunStopLeftPath, RunStopRightPath,
		GeneratedSprintFirstStepCommitStopName, RunAndSprintFirstStepSearchFrame, SprintStopGaitValue, bReplaceGenerated, GeneratedStops);
	if (!bGenerated)
	{
		return 1;
	}
	if (!WriteBuildAudit(GeneratedStops))
	{
		UE_LOG(LogTemp, Error, TEXT("[PMMExtendedStop] Generated assets but failed to write the audit CSV."));
		return 1;
	}

	UE_LOG(LogTemp, Display, TEXT("[PMMExtendedStop] Generated nine staging ExtendedStop/FirstStepCommitStop assets. Run/Sprint search from Start frame 20 and use Run Stop L/R; Walk retains its early Walk Stop join. PSD/AnimGraph were not modified."));
	return 0;
#else
	UE_LOG(LogTemp, Error, TEXT("[PMMExtendedStop] This commandlet requires an editor build."));
	return 1;
#endif
}
