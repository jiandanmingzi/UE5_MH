// Copyright MHGZ Project. All Rights Reserved.

#include "Editor/MHGZE4ExitTransitionCommandlet.h"

#if WITH_EDITOR

#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AnimationBlueprintLibrary.h"
#include "BoneIndices.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "ReferenceSkeleton.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace UE::MHGZ::E4ExitTransition
{
constexpr TCHAR AuditDirectoryName[] = TEXT("ActionExitAudit");
constexpr int32 BakedExitSamplingMultiplier = 2;
constexpr float BakedExitPlaybackRate = static_cast<float>(BakedExitSamplingMultiplier);

struct FExitTarget
{
	const TCHAR* Label;
	const TCHAR* SourcePath;
	const TCHAR* OutputPackagePath;
	const TCHAR* OutputName;
	int32 SourceStartFrame;
	int32 SelectedMontageFrame;
	float MontageFrameRate;
	float MontageSectionStartTime;
};

static const FExitTarget Targets[] =
{
	{
		TEXT("SheatheMoveExit"),
		TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Locomotion/AS_UnSh_ShouDao_Walk.AS_UnSh_ShouDao_Walk"),
		TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Locomotion/Transitions/Exit"),
		TEXT("AS_Shth_Sheathe_MoveExit"),
		108, 115, 60.0f, 1.0166667f
	},
	{
		TEXT("SheathedDodgeMoveExit"),
		TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Dash_Walk.AS_Shth_Dash_Walk"),
		TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/Transitions/Exit"),
		TEXT("AS_Shth_Dodge_MoveExit"),
		24, 85, 60.0f, 1.2166667f
	},
	{
		TEXT("UnsheathedForwardDodgeMoveExit"),
		TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Locomotion/AS_UnSh_Dash_Walk.AS_UnSh_Dash_Walk"),
		TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Locomotion/Transitions/Exit"),
		TEXT("AS_UnSh_Dodge_Forward_MoveExit"),
		27, 100, 60.0f, 1.2166667f
	}
};

struct FGeneratedExitInfo
{
	FString Label;
	FString SourcePath;
	FString OutputPath;
	int32 SourceStartFrame = INDEX_NONE;
	int32 SourceEndFrame = INDEX_NONE;
	int32 OutputKeyCount = 0;
	float HandoffMontageTime = 0.0f;
	float OutputPlayLength = 0.0f;
	float SourceSamplingRate = 0.0f;
	float OutputSamplingRate = 0.0f;
	FVector RootDelta = FVector::ZeroVector;
};

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

int32 GetKeyCount(const UAnimSequence& Sequence)
{
	const IAnimationDataModel* Model = Sequence.GetDataModel();
	return Model ? Model->GetNumberOfFrames() + 1 : 0;
}

bool SampleBoneLocalTransform(const UAnimSequence& Sequence, const int32 SkeletonBoneIndex,
	const FFrameRate& FrameRate, const int32 Frame, FTransform& OutTransform)
{
	if (SkeletonBoneIndex == INDEX_NONE || Frame < 0)
	{
		return false;
	}

	Sequence.GetBoneTransform(OutTransform, FSkeletonPoseBoneIndex(SkeletonBoneIndex),
		FAnimExtractContext(FrameRate.AsSeconds(FFrameNumber(Frame))), true);
	return true;
}

bool BuildCroppedTrack(const UAnimSequence& Source, const FName TrackName,
	const int32 SourceStartFrame, const int32 SourceKeyCount,
	TArray<FVector3f>& OutPositions, TArray<FQuat4f>& OutRotations,
	TArray<FVector3f>& OutScales)
{
	const USkeleton* Skeleton = Source.GetSkeleton();
	const IAnimationDataModel* Model = Source.GetDataModel();
	if (!Skeleton || !Model || SourceStartFrame < 0 || SourceStartFrame >= SourceKeyCount)
	{
		return false;
	}

	const int32 BoneIndex = Skeleton->GetReferenceSkeleton().FindBoneIndex(TrackName);
	if (BoneIndex == INDEX_NONE)
	{
		return false;
	}

	const int32 OutputKeyCount = SourceKeyCount - SourceStartFrame;
	const FFrameRate FrameRate = Model->GetFrameRate();
	OutPositions.Reset(OutputKeyCount);
	OutRotations.Reset(OutputKeyCount);
	OutScales.Reset(OutputKeyCount);

	for (int32 OutputFrame = 0; OutputFrame < OutputKeyCount; ++OutputFrame)
	{
		FTransform LocalTransform;
		if (!SampleBoneLocalTransform(Source, BoneIndex, FrameRate,
			SourceStartFrame + OutputFrame, LocalTransform))
		{
			return false;
		}


		OutPositions.Add(FVector3f(LocalTransform.GetTranslation()));
		OutRotations.Add(FQuat4f(LocalTransform.GetRotation()));
		OutScales.Add(FVector3f(LocalTransform.GetScale3D()));
	}
	return true;
}

bool ValidateGeneratedExit(const UAnimSequence& Output, const UAnimSequence& Source,
	const int32 SourceStartFrame, const int32 ExpectedKeyCount, const FName RootBoneName,
	const FFrameRate& ExpectedOutputFrameRate,
	FGeneratedExitInfo& InOutInfo)
{
	const IAnimationDataModel* Model = Output.GetDataModel();
	const IAnimationDataModel* SourceModel = Source.GetDataModel();
	const USkeleton* Skeleton = Output.GetSkeleton();
	if (!Model || !SourceModel || !Skeleton || Skeleton != Source.GetSkeleton()
		|| GetKeyCount(Output) != ExpectedKeyCount || SourceStartFrame < 0
		|| SourceStartFrame >= GetKeyCount(Source) - 1 || !Output.bEnableRootMotion
		|| Output.bForceRootLock || !Output.Notifies.IsEmpty()
		|| !FMath::IsNearlyEqual(Output.RateScale, 1.0f)
		|| Model->GetFrameRate().Numerator != ExpectedOutputFrameRate.Numerator
		|| Model->GetFrameRate().Denominator != ExpectedOutputFrameRate.Denominator)
	{
		return false;
	}

	TArray<FName> CurveNames;
	UAnimationBlueprintLibrary::GetAnimationCurveNames(&Output,
		ERawCurveTrackTypes::RCT_Float, CurveNames);
	if (!CurveNames.IsEmpty())
	{
		return false;
	}

	const int32 RootBoneIndex = Skeleton->GetReferenceSkeleton().FindBoneIndex(RootBoneName);
	FTransform RootAtStart;
	FTransform RootAtSecond;
	FTransform RootAtEnd;
	FTransform SourceRootAtStart;
	FTransform SourceRootAtSecond;
	if (!SampleBoneLocalTransform(Output, RootBoneIndex, Model->GetFrameRate(), 0, RootAtStart)
		|| !SampleBoneLocalTransform(Output, RootBoneIndex, Model->GetFrameRate(), 1, RootAtSecond)
		|| !SampleBoneLocalTransform(Output, RootBoneIndex, Model->GetFrameRate(),
			ExpectedKeyCount - 1, RootAtEnd)
		|| !SampleBoneLocalTransform(Source, RootBoneIndex, SourceModel->GetFrameRate(),
			SourceStartFrame, SourceRootAtStart)
		|| !SampleBoneLocalTransform(Source, RootBoneIndex, SourceModel->GetFrameRate(),
			SourceStartFrame + 1, SourceRootAtSecond)
		|| !RootAtStart.Equals(SourceRootAtStart, 0.001f)
		|| !RootAtSecond.GetRelativeTransform(RootAtStart).Equals(
			SourceRootAtSecond.GetRelativeTransform(SourceRootAtStart), 0.01f))
	{
		return false;
	}

	InOutInfo.OutputKeyCount = ExpectedKeyCount;
	InOutInfo.OutputPlayLength = Output.GetPlayLength();
	InOutInfo.SourceSamplingRate = SourceModel->GetFrameRate().AsDecimal();
	InOutInfo.OutputSamplingRate = Model->GetFrameRate().AsDecimal();
	InOutInfo.RootDelta = RootAtEnd.GetRelativeTransform(RootAtStart).GetTranslation();
	return true;
}

bool GenerateExitTransition(const FExitTarget& Target, const bool bReplaceGenerated,
	FGeneratedExitInfo& OutInfo)
{
	UAnimSequence* Source = LoadObject<UAnimSequence>(nullptr, Target.SourcePath);
	if (!Source)
	{
		UE_LOG(LogTemp, Error, TEXT("[E4ExitTransition] Could not load source %s."), Target.SourcePath);
		return false;
	}

	const USkeleton* Skeleton = Source->GetSkeleton();
	const IAnimationDataModel* Model = Source->GetDataModel();
	const int32 SourceKeyCount = GetKeyCount(*Source);
	if (!Skeleton || !Model || Target.SourceStartFrame < 0
		|| Target.SourceStartFrame >= SourceKeyCount - 1
		|| !FMath::IsNearlyEqual(Source->RateScale, BakedExitPlaybackRate))
	{
		UE_LOG(LogTemp, Error, TEXT("[E4ExitTransition] Invalid source frame range for %s."), Target.Label);
		return false;
	}

	const FString PackageName = FString::Printf(TEXT("%s/%s"),
		Target.OutputPackagePath, Target.OutputName);
	const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PackageName, Target.OutputName);
	UAnimSequence* Output = LoadObject<UAnimSequence>(nullptr, *ObjectPath);
	if (Output && !bReplaceGenerated)
	{
		UE_LOG(LogTemp, Error, TEXT("[E4ExitTransition] %s already exists; rerun with -ReplaceGenerated to rebuild it."),
			*ObjectPath);
		return false;
	}
	if (!Output)
	{
		UPackage* Package = CreatePackage(*PackageName);
		Output = NewObject<UAnimSequence>(Package, Target.OutputName, RF_Public | RF_Standalone);
	}
	if (!Output)
	{
		return false;
	}

	TArray<FName> TrackNames;
	Model->GetBoneTrackNames(TrackNames);
	const FName RootBoneName = Skeleton->GetReferenceSkeleton().GetBoneName(0);
	const int32 OutputKeyCount = SourceKeyCount - Target.SourceStartFrame;
	const FFrameRate SourceFrameRate = Model->GetFrameRate();
	const FFrameRate OutputFrameRate(
		SourceFrameRate.Numerator * BakedExitSamplingMultiplier, SourceFrameRate.Denominator);
	if (TrackNames.IsEmpty() || RootBoneName.IsNone() || OutputKeyCount < 2)
	{
		return false;
	}

	Output->Modify();
	Output->SetSkeleton(const_cast<USkeleton*>(Skeleton));
	Output->bLoop = false;
	Output->RootMotionRootLock = Source->RootMotionRootLock;
	// Pose Search plays the data timeline. Bake the source's editor-only 2x
	// RateScale into the output sampling rate, then leave RateScale at one.
	Output->RateScale = 1.0f;
	Output->bUseNormalizedRootMotionScale = Source->bUseNormalizedRootMotionScale;

	IAnimationDataController& Controller = Output->GetController();
	Controller.InitializeModel();
	Output->ResetAnimation();
	Controller.OpenBracket(FText::FromString(TEXT("Build E4.2 Exit Transition")), false);
	Controller.SetFrameRate(OutputFrameRate, false);
	Controller.SetNumberOfFrames(FFrameNumber(OutputKeyCount - 1), false);
	for (const FName TrackName : TrackNames)
	{
		TArray<FVector3f> Positions;
		TArray<FQuat4f> Rotations;
		TArray<FVector3f> Scales;
		if (!BuildCroppedTrack(*Source, TrackName, Target.SourceStartFrame,
			SourceKeyCount, Positions, Rotations, Scales)
			|| !Controller.AddBoneCurve(TrackName, false)
			|| !Controller.SetBoneTrackKeys(TrackName, Positions, Rotations, Scales, false))
		{
			Controller.CloseBracket(false);
			UE_LOG(LogTemp, Error, TEXT("[E4ExitTransition] Failed to bake %s for %s."),
				*TrackName.ToString(), *ObjectPath);
			return false;
		}
	}
	Controller.NotifyPopulated();
	Controller.CloseBracket(false);

	Output->Notifies.Reset();
	UAnimationBlueprintLibrary::RemoveAllAnimationNotifyTracks(Output);
	UAnimationBlueprintLibrary::RemoveAllCurveData(Output);
	Output->bEnableRootMotion = true;
	Output->bForceRootLock = false;
	Output->PostEditChange();
	Output->RefreshCacheData();

	OutInfo.Label = Target.Label;
	OutInfo.SourcePath = Source->GetPathName();
	OutInfo.OutputPath = Output->GetPathName();
	OutInfo.SourceStartFrame = Target.SourceStartFrame;
	OutInfo.SourceEndFrame = SourceKeyCount - 1;
	OutInfo.HandoffMontageTime = static_cast<float>(
		Target.SelectedMontageFrame / Target.MontageFrameRate);
	if (!ValidateGeneratedExit(*Output, *Source, Target.SourceStartFrame,
		OutputKeyCount, RootBoneName, OutputFrameRate, OutInfo))
	{
		UE_LOG(LogTemp, Error, TEXT("[E4ExitTransition] Validation failed for %s."), *ObjectPath);
		return false;
	}

	if (!SaveAsset(*Output))
	{
		UE_LOG(LogTemp, Error, TEXT("[E4ExitTransition] Failed to save %s."), *ObjectPath);
		return false;
	}

	UE_LOG(LogTemp, Display, TEXT("[E4ExitTransition] Generated %s from source frames %d-%d."),
		*OutInfo.OutputPath, OutInfo.SourceStartFrame, OutInfo.SourceEndFrame);
	return true;
}

bool WriteGenerationAudit(const TArray<FGeneratedExitInfo>& Infos)
{
	const FString OutputDirectory = FPaths::ProjectSavedDir() / AuditDirectoryName;
	IFileManager::Get().MakeDirectory(*OutputDirectory, true);
	const FString OutputPath = OutputDirectory / TEXT("E4_2_ExitTransitionGeneration.md");

	TArray<FString> Lines;
	Lines.Add(TEXT("# E4.2 ExitTransition Generation"));
	Lines.Add(TEXT(""));
	Lines.Add(TEXT("Generated by MHGZE4ExitTransition from the visually approved 60fps Montage frames."));
	Lines.Add(TEXT("The commandlet creates dedicated assets only; it never modifies source Sequences or Montages."));
	Lines.Add(TEXT(""));
	Lines.Add(TEXT("| Route | Source frame range | Sampling baked | Handoff montage time | Output | Output length | Root delta |"));
	Lines.Add(TEXT("|---|---:|---|---:|---|---:|---|"));
	for (const FGeneratedExitInfo& Info : Infos)
	{
		Lines.Add(FString::Printf(TEXT("| %s | %d-%d | %.0ffps -> %.0ffps; RateScale=1 | %.4fs | %s | %.4fs | (%.3f, %.3f, %.3f) |"),
			*Info.Label, Info.SourceStartFrame, Info.SourceEndFrame,
			Info.SourceSamplingRate, Info.OutputSamplingRate, Info.HandoffMontageTime,
			*Info.OutputPath, Info.OutputPlayLength, Info.RootDelta.X, Info.RootDelta.Y, Info.RootDelta.Z));
	}
	Lines.Add(TEXT(""));
	Lines.Add(TEXT("The authored Handoff Notify must be placed at the listed Montage time."));
	Lines.Add(TEXT("The Action Root Motion Phase must end three 60fps Montage frames before that Notify."));
	return FFileHelper::SaveStringToFile(FString::Join(Lines, TEXT("\n")), *OutputPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
}

UMHGZE4ExitTransitionCommandlet::UMHGZE4ExitTransitionCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMHGZE4ExitTransitionCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	const bool bReplaceGenerated = FParse::Param(*Params, TEXT("ReplaceGenerated"));
	TArray<UE::MHGZ::E4ExitTransition::FGeneratedExitInfo> Generated;
	bool bSucceeded = true;
	for (const UE::MHGZ::E4ExitTransition::FExitTarget& Target :
		UE::MHGZ::E4ExitTransition::Targets)
	{
		UE::MHGZ::E4ExitTransition::FGeneratedExitInfo Info;
		if (UE::MHGZ::E4ExitTransition::GenerateExitTransition(Target,
			bReplaceGenerated, Info))
		{
			Generated.Add(MoveTemp(Info));
		}
		else
		{
			bSucceeded = false;
		}
	}

	if (!UE::MHGZ::E4ExitTransition::WriteGenerationAudit(Generated))
	{
		UE_LOG(LogTemp, Error, TEXT("[E4ExitTransition] Failed to write generation audit."));
		bSucceeded = false;
	}

	UE_LOG(LogTemp, Display, TEXT("[E4ExitTransition] Completed %d/%d targets."),
		Generated.Num(), UE_ARRAY_COUNT(UE::MHGZ::E4ExitTransition::Targets));
	return bSucceeded ? 0 : 1;
#else
	UE_LOG(LogTemp, Error, TEXT("[E4ExitTransition] This commandlet requires an editor build."));
	return 1;
#endif
}

#endif // WITH_EDITOR
