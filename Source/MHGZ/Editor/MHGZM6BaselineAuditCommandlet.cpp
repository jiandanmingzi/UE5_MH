// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZM6BaselineAuditCommandlet.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AnimationBlueprintLibrary.h"
#include "AnimPose.h"
#include "EditorFramework/AssetImportData.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Algo/Sort.h"

namespace UE::MHGZ::M6BaselineAudit
{
#if WITH_EDITOR
struct FAuditTarget
{
	const TCHAR* ID;
	const TCHAR* Name;
	const TCHAR* ObjectPath;
};

static const FAuditTarget Targets[] =
{
	{TEXT("193"), TEXT("AS_Unsh_JueChongJi"), TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_JueChongJi.AS_Unsh_JueChongJi")},
	{TEXT("194"), TEXT("AS_Unsh_JueChongJi_Hold"), TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_JueChongJi_Hold.AS_Unsh_JueChongJi_Hold")},
	{TEXT("195"), TEXT("AS_Unsh_JueChongJi_Send"), TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_JueChongJi_Send.AS_Unsh_JueChongJi_Send")},
	{TEXT("196"), TEXT("AS_Unsh_JueChongJi_Pose"), TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_JueChongJi_Pose.AS_Unsh_JueChongJi_Pose")},
	{TEXT("199"), TEXT("AS_Unsh_JueChongJi_Flying"), TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_JueChongJi_Flying.AS_Unsh_JueChongJi_Flying")},
	{TEXT("206"), TEXT("AS_Unsh_LieChongHuaJueChongJiFall"), TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_LieChongHuaJueChongJiFall.AS_Unsh_LieChongHuaJueChongJiFall")},
	{TEXT("189"), TEXT("AS_Unsh_CaoChongZhanJueChongJiWuTa"), TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_CaoChongZhanJueChongJiWuTa.AS_Unsh_CaoChongZhanJueChongJiWuTa")}
};

static const TCHAR* KinsectActionMontagePaths[] =
{
	TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_SendKinsect.AM_IG_SendKinsect"),
	TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_RecallKinsect.AM_IG_RecallKinsect"),
	TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_DrawAndSendKinsect.AM_IG_DrawAndSendKinsect")
};

static bool WriteKinsectActionMontageAudit(const FString& OutputPath)
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("read_only"), true);
	TArray<TSharedPtr<FJsonValue>> MontagesJson;
	TSet<FString> ReferencedSequencePaths;
	bool bAllMontagesLoaded = true;

	for (const TCHAR* MontagePath : KinsectActionMontagePaths)
	{
		UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, MontagePath, nullptr, LOAD_NoWarn);
		TSharedRef<FJsonObject> MontageJson = MakeShared<FJsonObject>();
		MontageJson->SetStringField(TEXT("requested_path"), MontagePath);
		if (!Montage)
		{
			MontageJson->SetStringField(TEXT("error"), TEXT("Could not load Montage"));
			MontagesJson.Add(MakeShared<FJsonValueObject>(MontageJson));
			bAllMontagesLoaded = false;
			continue;
		}

		MontageJson->SetStringField(TEXT("asset_path"), Montage->GetPathName());
		MontageJson->SetStringField(TEXT("skeleton_path"), GetPathNameSafe(Montage->GetSkeleton()));
		MontageJson->SetNumberField(TEXT("play_length_seconds"), Montage->GetPlayLength());
		TArray<TSharedPtr<FJsonValue>> SlotsJson;
		for (const FSlotAnimationTrack& Slot : Montage->SlotAnimTracks)
		{
			TSharedRef<FJsonObject> SlotJson = MakeShared<FJsonObject>();
			SlotJson->SetStringField(TEXT("slot_name"), Slot.SlotName.ToString());
			TArray<TSharedPtr<FJsonValue>> SegmentsJson;
			for (const FAnimSegment& Segment : Slot.AnimTrack.AnimSegments)
			{
				TSharedRef<FJsonObject> SegmentJson = MakeShared<FJsonObject>();
				const UAnimSequenceBase* Sequence = Segment.GetAnimReference();
				SegmentJson->SetStringField(TEXT("sequence_path"), GetPathNameSafe(Sequence));
				SegmentJson->SetNumberField(TEXT("timeline_start_seconds"), Segment.StartPos);
				SegmentJson->SetNumberField(TEXT("source_start_seconds"), Segment.AnimStartTime);
				SegmentJson->SetNumberField(TEXT("source_end_seconds"), Segment.AnimEndTime);
				SegmentJson->SetNumberField(TEXT("segment_play_rate"), Segment.AnimPlayRate);
				SegmentJson->SetNumberField(TEXT("sequence_rate_scale"),
					Sequence ? Sequence->RateScale : 0.0f);
				SegmentJson->SetNumberField(TEXT("looping_count"), Segment.LoopingCount);
				SegmentsJson.Add(MakeShared<FJsonValueObject>(SegmentJson));
				if (Sequence)
				{
					ReferencedSequencePaths.Add(Sequence->GetPathName());
				}
			}
			SlotJson->SetArrayField(TEXT("segments"), SegmentsJson);
			SlotsJson.Add(MakeShared<FJsonValueObject>(SlotJson));
		}
		MontageJson->SetArrayField(TEXT("slots"), SlotsJson);

		TArray<TSharedPtr<FJsonValue>> SectionsJson;
		for (const FCompositeSection& Section : Montage->CompositeSections)
		{
			TSharedRef<FJsonObject> SectionJson = MakeShared<FJsonObject>();
			SectionJson->SetStringField(TEXT("name"), Section.SectionName.ToString());
			SectionJson->SetNumberField(TEXT("time_seconds"), Section.GetTime());
			SectionJson->SetStringField(TEXT("next_section"), Section.NextSectionName.ToString());
			SectionsJson.Add(MakeShared<FJsonValueObject>(SectionJson));
		}
		MontageJson->SetArrayField(TEXT("sections"), SectionsJson);

		TArray<TSharedPtr<FJsonValue>> NotifiesJson;
		for (const FAnimNotifyEvent& Event : Montage->Notifies)
		{
			TSharedRef<FJsonObject> NotifyJson = MakeShared<FJsonObject>();
			const FString NotifyClass = Event.Notify
				? Event.Notify->GetClass()->GetPathName()
				: Event.NotifyStateClass
					? Event.NotifyStateClass->GetClass()->GetPathName()
					: FString();
			const FString TrackName = Montage->AnimNotifyTracks.IsValidIndex(Event.TrackIndex)
				? Montage->AnimNotifyTracks[Event.TrackIndex].TrackName.ToString()
				: FString();
			NotifyJson->SetStringField(TEXT("name"), Event.NotifyName.ToString());
			NotifyJson->SetStringField(TEXT("class"), NotifyClass);
			NotifyJson->SetStringField(TEXT("track"), TrackName);
			NotifyJson->SetNumberField(TEXT("display_time_seconds"), Event.GetTime());
			NotifyJson->SetNumberField(TEXT("trigger_time_seconds"), Event.GetTriggerTime());
			NotifyJson->SetNumberField(TEXT("end_trigger_time_seconds"), Event.GetEndTriggerTime());
			NotifiesJson.Add(MakeShared<FJsonValueObject>(NotifyJson));
		}
		MontageJson->SetArrayField(TEXT("notifies"), NotifiesJson);
		MontagesJson.Add(MakeShared<FJsonValueObject>(MontageJson));
	}

	TArray<FString> SortedSequencePaths = ReferencedSequencePaths.Array();
	SortedSequencePaths.Sort();
	TArray<TSharedPtr<FJsonValue>> SequencePathsJson;
	for (const FString& SequencePath : SortedSequencePaths)
	{
		SequencePathsJson.Add(MakeShared<FJsonValueString>(SequencePath));
	}
	Root->SetArrayField(TEXT("montages"), MontagesJson);
	Root->SetArrayField(TEXT("unique_referenced_sequences"), SequencePathsJson);
	Root->SetBoolField(TEXT("all_montages_loaded"), bAllMontagesLoaded);

	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	if (!FJsonSerializer::Serialize(Root, Writer))
	{
		return false;
	}
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutputPath), true);
	return FFileHelper::SaveStringToFile(Json, *OutputPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) && bAllMontagesLoaded;
}

static FString JoinNames(const TArray<FName>& Names)
{
	TArray<FString> Strings;
	for (const FName Name : Names)
	{
		Strings.Add(Name.ToString());
	}
	return Strings.IsEmpty() ? TEXT("(none)") : FString::Join(Strings, TEXT(", "));
}

static FString DescribeNotify(const FAnimNotifyEvent& Event)
{
	if (Event.NotifyStateClass)
	{
		return Event.NotifyStateClass->GetClass()->GetName();
	}
	if (Event.Notify)
	{
		return Event.Notify->GetClass()->GetName();
	}
	return Event.NotifyName.IsNone() ? TEXT("UnnamedNotify") : Event.NotifyName.ToString();
}

static void AddSequenceAudit(TArray<FString>& Lines, const FAuditTarget& Target,
	const UAnimSequence& Sequence)
{
	const FFrameRate SamplingRate = Sequence.GetSamplingFrameRate();
	const float Length = Sequence.GetPlayLength();
	const USkeleton* Skeleton = Sequence.GetSkeleton();
	TArray<FName> CurveNames;
	UAnimationBlueprintLibrary::GetAnimationCurveNames(&Sequence,
		ERawCurveTrackTypes::RCT_Float, CurveNames);

	Lines.Add(FString::Printf(TEXT("## %s — %s"), Target.ID, Target.Name));
	Lines.Add(FString::Printf(TEXT("- Asset: `%s`"), *Sequence.GetPathName()));
	Lines.Add(FString::Printf(TEXT("- Skeleton: `%s`"), *GetPathNameSafe(Skeleton)));
	Lines.Add(FString::Printf(TEXT("- Length: %.6fs | Sampled keys: %d | Sampling rate: %d/%d (%.3f fps) | RateScale: %.6f"),
		Length, Sequence.GetNumberOfSampledKeys(), SamplingRate.Numerator,
		SamplingRate.Denominator, SamplingRate.AsDecimal(), Sequence.RateScale));
	Lines.Add(FString::Printf(TEXT("- Root motion: %s | ForceRootLock: %s | RootMotionRootLock enum: %d"),
		Sequence.HasRootMotion() ? TEXT("true") : TEXT("false"),
		Sequence.bForceRootLock ? TEXT("true") : TEXT("false"),
		static_cast<int32>(Sequence.RootMotionRootLock.GetValue())));
	Lines.Add(FString::Printf(TEXT("- Float curves: %s"), *JoinNames(CurveNames)));

	FString RootTrackName = TEXT("(root track missing)");
	FTransform RootAtStart = FTransform::Identity;
	FTransform RootAtEnd = FTransform::Identity;
	if (const IAnimationDataModel* DataModel = Sequence.GetDataModel())
	{
		TArray<FName> BoneTrackNames;
		DataModel->GetBoneTrackNames(BoneTrackNames);
		const FName* RootTrack = BoneTrackNames.FindByPredicate([](const FName Name)
		{
			return Name.ToString().Equals(TEXT("root"), ESearchCase::IgnoreCase);
		});
		if (RootTrack)
		{
			RootTrackName = RootTrack->ToString();
			const float EndSampleTime = FMath::Max(0.0f, Length - KINDA_SMALL_NUMBER);
		FAnimPoseEvaluationOptions PoseOptions;
			PoseOptions.EvaluationType = EAnimDataEvalType::Compressed;
			PoseOptions.bShouldRetarget = false;
			PoseOptions.bExtractRootMotion = false;
			FAnimPose EvaluatedPose;
			UAnimPoseExtensions::GetAnimPoseAtTime(&Sequence, 0.0, PoseOptions, EvaluatedPose);
			RootAtStart = UAnimPoseExtensions::GetBonePose(
				EvaluatedPose, *RootTrack, EAnimPoseSpaces::Local);
			UAnimPoseExtensions::GetAnimPoseAtTime(&Sequence, EndSampleTime,
				PoseOptions, EvaluatedPose);
			RootAtEnd = UAnimPoseExtensions::GetBonePose(
				EvaluatedPose, *RootTrack, EAnimPoseSpaces::Local);
		}
	}
	const FVector RootStart = RootAtStart.GetTranslation();
	const FVector RootEnd = RootAtEnd.GetTranslation();
	const FVector RootDelta = RootEnd - RootStart;
	Lines.Add(FString::Printf(TEXT("- Root track: `%s` | sampled bone-pose translation start=(%.4f, %.4f, %.4f) cm end=(%.4f, %.4f, %.4f) cm delta=(%.4f, %.4f, %.4f) cm"),
		*RootTrackName, RootStart.X, RootStart.Y, RootStart.Z,
		RootEnd.X, RootEnd.Y, RootEnd.Z, RootDelta.X, RootDelta.Y, RootDelta.Z));

	TArray<FString> ImportFiles;
	if (Sequence.AssetImportData)
	{
		Sequence.AssetImportData->ExtractFilenames(ImportFiles);
	}
	Lines.Add(FString::Printf(TEXT("- Import source: %s"),
		ImportFiles.IsEmpty() ? TEXT("(none recorded)") : *FString::Join(ImportFiles, TEXT(", "))));

	Lines.Add(TEXT("- Sequence notifies:"));
	if (Sequence.Notifies.IsEmpty())
	{
		Lines.Add(TEXT("  - (none)"));
	}
	for (const FAnimNotifyEvent& Event : Sequence.Notifies)
	{
		Lines.Add(FString::Printf(TEXT("  - t=%.6fs duration=%.6fs track=%d class=%s"),
			Event.GetTime(), Event.GetDuration(), Event.TrackIndex, *DescribeNotify(Event)));
	}
	Lines.Add(TEXT(""));
}

struct FLoopCandidate
{
	int32 StartFrame = INDEX_NONE;
	double StartSequenceSeconds = 0.0;
	double StartRuntimeSeconds = 0.0;
	double BodyTranslationRmsCm = 0.0;
	double BodyRotationRmsDegrees = 0.0;
	double BodyTranslationStepRmsCm = 0.0;
	double BodyAngularStepRmsDegrees = 0.0;
	double RootTranslationDifferenceCm = 0.0;
	double RootRotationDifferenceDegrees = 0.0;
	double LoopRuntimeDurationSeconds = 0.0;
	double InteriorBodyTranslationStepRmsCm = 0.0;
	double InteriorBodyAngularStepRmsDegrees = 0.0;
	double InteriorRootTranslationStepRmsCm = 0.0;
	double InteriorRootAngularStepRmsDegrees = 0.0;
};

static bool Write193LoopCandidateAudit(const UAnimSequence& Sequence,
	const FString& OutputPath)
{
	const double SampleRate = Sequence.GetSamplingFrameRate().AsDecimal();
	const int32 SampleCount = Sequence.GetNumberOfSampledKeys();
	const double SequenceLength = Sequence.GetPlayLength();
	if (!FMath::IsFinite(SampleRate) || SampleRate <= 0.0
		|| SampleCount < 4 || !FMath::IsFinite(SequenceLength)
		|| SequenceLength <= 0.0)
	{
		return false;
	}

	TArray<double> SampleTimes;
	SampleTimes.Reserve(SampleCount);
	for (int32 Frame = 0; Frame < SampleCount; ++Frame)
	{
		SampleTimes.Add(FMath::Min(static_cast<double>(Frame) / SampleRate,
			SequenceLength));
	}

	FAnimPoseEvaluationOptions PoseOptions;
	PoseOptions.EvaluationType = EAnimDataEvalType::Compressed;
	PoseOptions.bShouldRetarget = false;
	PoseOptions.bExtractRootMotion = false;
	PoseOptions.bIncorporateRootMotionIntoPose = false;
	PoseOptions.bEvaluateCurves = false;

	TArray<FAnimPose> Poses;
	UAnimPoseExtensions::GetAnimPoseAtTimeIntervals(&Sequence, SampleTimes,
		PoseOptions, Poses);
	if (Poses.Num() != SampleCount || Poses.IsEmpty() || !Poses.Last().IsValid())
	{
		return false;
	}

	TArray<FName> BoneNames;
	UAnimPoseExtensions::GetBoneNames(Poses[0], BoneNames);
	BoneNames.RemoveAll([](const FName BoneName)
	{
		return BoneName.IsNone() || BoneName == FName(TEXT("root"));
	});
	if (BoneNames.IsEmpty())
	{
		return false;
	}

	const int32 LastFrame = SampleCount - 1;
	const FTransform& RootAtEnd = UAnimPoseExtensions::GetBonePose(
		Poses[LastFrame], FName(TEXT("root")), EAnimPoseSpaces::Local);
	const int32 FirstCandidateFrame = FMath::Max(1, SampleCount / 2);
	TArray<FLoopCandidate> Candidates;
	Candidates.Reserve(LastFrame - FirstCandidateFrame);

	for (int32 StartFrame = FirstCandidateFrame; StartFrame < LastFrame; ++StartFrame)
	{
		double TranslationErrorSquaredSum = 0.0;
		double RotationErrorRadiansSquaredSum = 0.0;
		double TranslationStepErrorSquaredSum = 0.0;
		double AngularStepErrorRadiansSquaredSum = 0.0;
		double InteriorBodyTranslationStepSquaredSum = 0.0;
		double InteriorBodyAngularStepRadiansSquaredSum = 0.0;
		double InteriorRootTranslationStepSquaredSum = 0.0;
		double InteriorRootAngularStepRadiansSquaredSum = 0.0;
		int32 ComparedBoneCount = 0;
		int32 InteriorStepCount = 0;

		for (const FName BoneName : BoneNames)
		{
			const FTransform& StartPose = UAnimPoseExtensions::GetBonePose(
				Poses[StartFrame], BoneName, EAnimPoseSpaces::Local);
			const FTransform& EndPose = UAnimPoseExtensions::GetBonePose(
				Poses[LastFrame], BoneName, EAnimPoseSpaces::Local);
			const FTransform& BeforeStartPose = UAnimPoseExtensions::GetBonePose(
				Poses[StartFrame - 1], BoneName, EAnimPoseSpaces::Local);
			const FTransform& BeforeEndPose = UAnimPoseExtensions::GetBonePose(
				Poses[LastFrame - 1], BoneName, EAnimPoseSpaces::Local);

			TranslationErrorSquaredSum += FVector::DistSquared(
				StartPose.GetTranslation(), EndPose.GetTranslation());
			RotationErrorRadiansSquaredSum += FMath::Square(
				StartPose.GetRotation().AngularDistance(EndPose.GetRotation()));

			const FVector StartTranslationStep = StartPose.GetTranslation()
				- BeforeStartPose.GetTranslation();
			const FVector EndTranslationStep = EndPose.GetTranslation()
				- BeforeEndPose.GetTranslation();
			TranslationStepErrorSquaredSum += FVector::DistSquared(
				StartTranslationStep, EndTranslationStep);

			const FQuat StartRotationStep = BeforeStartPose.GetRotation().Inverse()
				* StartPose.GetRotation();
			const FQuat EndRotationStep = BeforeEndPose.GetRotation().Inverse()
				* EndPose.GetRotation();
			AngularStepErrorRadiansSquaredSum += FMath::Square(
				StartRotationStep.AngularDistance(EndRotationStep));
			++ComparedBoneCount;
		}

		for (int32 Frame = StartFrame + 1; Frame <= LastFrame; ++Frame)
		{
			const FTransform& PreviousRootPose = UAnimPoseExtensions::GetBonePose(
				Poses[Frame - 1], FName(TEXT("root")), EAnimPoseSpaces::Local);
			const FTransform& CurrentRootPose = UAnimPoseExtensions::GetBonePose(
				Poses[Frame], FName(TEXT("root")), EAnimPoseSpaces::Local);
			InteriorRootTranslationStepSquaredSum += FVector::DistSquared(
				CurrentRootPose.GetTranslation(), PreviousRootPose.GetTranslation());
			InteriorRootAngularStepRadiansSquaredSum += FMath::Square(
				PreviousRootPose.GetRotation().AngularDistance(
					CurrentRootPose.GetRotation()));

			for (const FName BoneName : BoneNames)
			{
				const FTransform& PreviousPose = UAnimPoseExtensions::GetBonePose(
					Poses[Frame - 1], BoneName, EAnimPoseSpaces::Local);
				const FTransform& CurrentPose = UAnimPoseExtensions::GetBonePose(
					Poses[Frame], BoneName, EAnimPoseSpaces::Local);
				InteriorBodyTranslationStepSquaredSum += FVector::DistSquared(
					CurrentPose.GetTranslation(), PreviousPose.GetTranslation());
				InteriorBodyAngularStepRadiansSquaredSum += FMath::Square(
					PreviousPose.GetRotation().AngularDistance(
						CurrentPose.GetRotation()));
			}
			++InteriorStepCount;
		}

		if (ComparedBoneCount == 0)
		{
			return false;
		}

		const double BoneCount = static_cast<double>(ComparedBoneCount);
		const FTransform& RootAtStart = UAnimPoseExtensions::GetBonePose(
			Poses[StartFrame], FName(TEXT("root")), EAnimPoseSpaces::Local);
		FLoopCandidate& Candidate = Candidates.AddDefaulted_GetRef();
		Candidate.StartFrame = StartFrame;
		Candidate.StartSequenceSeconds = SampleTimes[StartFrame];
		Candidate.StartRuntimeSeconds = SampleTimes[StartFrame]
			/ FMath::Max(static_cast<double>(Sequence.RateScale), UE_DOUBLE_SMALL_NUMBER);
		Candidate.BodyTranslationRmsCm = FMath::Sqrt(
			TranslationErrorSquaredSum / BoneCount);
		Candidate.BodyRotationRmsDegrees = FMath::RadiansToDegrees(FMath::Sqrt(
			RotationErrorRadiansSquaredSum / BoneCount));
		Candidate.BodyTranslationStepRmsCm = FMath::Sqrt(
			TranslationStepErrorSquaredSum / BoneCount);
		Candidate.BodyAngularStepRmsDegrees = FMath::RadiansToDegrees(FMath::Sqrt(
			AngularStepErrorRadiansSquaredSum / BoneCount));
		Candidate.RootTranslationDifferenceCm = FVector::Distance(
			RootAtStart.GetTranslation(), RootAtEnd.GetTranslation());
		Candidate.RootRotationDifferenceDegrees = FMath::RadiansToDegrees(
			RootAtStart.GetRotation().AngularDistance(RootAtEnd.GetRotation()));
		Candidate.LoopRuntimeDurationSeconds = (SampleTimes[LastFrame]
			- SampleTimes[StartFrame])
			/ FMath::Max(static_cast<double>(Sequence.RateScale), UE_DOUBLE_SMALL_NUMBER);
		const double InteriorBoneStepCount = static_cast<double>(
			FMath::Max(1, ComparedBoneCount) * FMath::Max(1, InteriorStepCount));
		const double InteriorStepCountAsDouble = static_cast<double>(
			FMath::Max(1, InteriorStepCount));
		Candidate.InteriorBodyTranslationStepRmsCm = FMath::Sqrt(
			InteriorBodyTranslationStepSquaredSum / InteriorBoneStepCount);
		Candidate.InteriorBodyAngularStepRmsDegrees = FMath::RadiansToDegrees(
			FMath::Sqrt(InteriorBodyAngularStepRadiansSquaredSum / InteriorBoneStepCount));
		Candidate.InteriorRootTranslationStepRmsCm = FMath::Sqrt(
			InteriorRootTranslationStepSquaredSum / InteriorStepCountAsDouble);
		Candidate.InteriorRootAngularStepRmsDegrees = FMath::RadiansToDegrees(
			FMath::Sqrt(InteriorRootAngularStepRadiansSquaredSum / InteriorStepCountAsDouble));
	}

	Algo::Sort(Candidates, [](const FLoopCandidate& A, const FLoopCandidate& B)
	{
		if (!FMath::IsNearlyEqual(A.BodyTranslationRmsCm, B.BodyTranslationRmsCm, 1.e-6))
		{
			return A.BodyTranslationRmsCm < B.BodyTranslationRmsCm;
		}
		return A.BodyRotationRmsDegrees < B.BodyRotationRmsDegrees;
	});

	TArray<FString> Lines;
	Lines.Add(TEXT("rank,start_frame,start_sequence_seconds,start_runtime_seconds,body_pose_translation_rms_cm,body_pose_rotation_rms_deg,body_boundary_translation_step_rms_cm,body_boundary_angular_step_rms_deg,root_translation_delta_cm,root_rotation_delta_deg,loop_runtime_duration_seconds,interior_body_translation_step_rms_cm,interior_body_angular_step_rms_deg,interior_root_translation_step_rms_cm,interior_root_angular_step_rms_deg"));
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		const FLoopCandidate& Candidate = Candidates[Index];
		Lines.Add(FString::Printf(TEXT("%d,%d,%.9f,%.9f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f"),
			Index + 1, Candidate.StartFrame, Candidate.StartSequenceSeconds,
			Candidate.StartRuntimeSeconds, Candidate.BodyTranslationRmsCm,
			Candidate.BodyRotationRmsDegrees, Candidate.BodyTranslationStepRmsCm,
			Candidate.BodyAngularStepRmsDegrees,
			Candidate.RootTranslationDifferenceCm,
			Candidate.RootRotationDifferenceDegrees,
			Candidate.LoopRuntimeDurationSeconds,
			Candidate.InteriorBodyTranslationStepRmsCm,
			Candidate.InteriorBodyAngularStepRmsDegrees,
			Candidate.InteriorRootTranslationStepRmsCm,
			Candidate.InteriorRootAngularStepRmsDegrees));
	}

	return FFileHelper::SaveStringToFile(FString::Join(Lines, TEXT("\n")),
		*OutputPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
#endif
}

UMHGZM6BaselineAuditCommandlet::UMHGZM6BaselineAuditCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMHGZM6BaselineAuditCommandlet::Main(const FString& Params)
{
	(void)Params;
#if WITH_EDITOR
	using namespace UE::MHGZ::M6BaselineAudit;
	TArray<FString> Lines;
	Lines.Add(TEXT("# M6.1 Insect Glaive Animation Read-Only Audit"));
	Lines.Add(TEXT(""));
	Lines.Add(TEXT("Generated by `MHGZM6BaselineAudit`; assets are loaded read-only. No Modify/SavePackage calls are made."));
	Lines.Add(TEXT("Root track rows report sampled bone-pose translation, not world-space actor movement."));
	Lines.Add(TEXT(""));

	bool bAllTargetsLoaded = true;
	for (const FAuditTarget& Target : Targets)
	{
		UAnimSequence* Sequence = LoadObject<UAnimSequence>(nullptr, Target.ObjectPath);
		if (!Sequence)
		{
			bAllTargetsLoaded = false;
			Lines.Add(FString::Printf(TEXT("## %s — %s"), Target.ID, Target.Name));
			Lines.Add(FString::Printf(TEXT("- ERROR: could not load `%s`"), Target.ObjectPath));
			Lines.Add(TEXT(""));
			continue;
		}
		AddSequenceAudit(Lines, Target, *Sequence);
	}

	const FString OutputDirectory = FPaths::ProjectSavedDir() / TEXT("M6Audit");
	const FString OutputPath = OutputDirectory / TEXT("m6_animation_audit.md");
	IFileManager::Get().MakeDirectory(*OutputDirectory, true);
	if (!FFileHelper::SaveStringToFile(FString::Join(Lines, TEXT("\n")), *OutputPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogTemp, Error, TEXT("[M6BaselineAudit] Failed to write %s"), *OutputPath);
		return 1;
	}
	UE_LOG(LogTemp, Display, TEXT("[M6BaselineAudit] Wrote %s (AllTargetsLoaded=%d)"),
		*OutputPath, bAllTargetsLoaded ? 1 : 0);

	const FString KinsectMontageAuditPath = OutputDirectory
		/ TEXT("m6_kinsect_montage_sequence_audit.json");
	const bool bKinsectMontagesAudited = WriteKinsectActionMontageAudit(
		KinsectMontageAuditPath);
	UE_LOG(LogTemp, Display,
		TEXT("[M6BaselineAudit] Kinsect action Montage audit %s: %s"),
		bKinsectMontagesAudited ? TEXT("succeeded") : TEXT("failed"),
		*KinsectMontageAuditPath);

	UAnimSequence* Sequence193 = LoadObject<UAnimSequence>(nullptr,
		TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_JueChongJi.AS_Unsh_JueChongJi"));
	const FString LoopCandidatePath = OutputDirectory / TEXT("m6_193_loop_candidates.csv");
	const bool bLoopCandidatesWritten = Sequence193
		&& Write193LoopCandidateAudit(*Sequence193, LoopCandidatePath);
	if (bLoopCandidatesWritten)
	{
		UE_LOG(LogTemp, Display, TEXT("[M6BaselineAudit] 193 loop candidates written: %s"),
			*LoopCandidatePath);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[M6BaselineAudit] 193 loop candidate audit failed: %s"),
			*LoopCandidatePath);
	}
	return bAllTargetsLoaded && bKinsectMontagesAudited && bLoopCandidatesWritten ? 0 : 1;
#else
	UE_LOG(LogTemp, Error, TEXT("[M6BaselineAudit] Requires an editor build."));
	return 1;
#endif
}
