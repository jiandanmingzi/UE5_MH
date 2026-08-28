// Copyright MHGZ Project. All Rights Reserved.

#include "Editor/MHGZPMM7AssetFixupCommandlet.h"

#if WITH_EDITOR

#include "Animation/AnimSequence.h"
#include "AnimationBlueprintLibrary.h"
#include "Animation/MHGZPoseSearchFeatureChannel_MoveGait.h"
#include "Animation/MHGZPoseSearchFeatureChannel_StopGait.h"
#include "Misc/PackageName.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchDerivedData.h"
#include "PoseSearch/PoseSearchFeatureChannel_Curve.h"
#include "PoseSearch/PoseSearchFeatureChannel_Pose.h"
#include "PoseSearch/PoseSearchFeatureChannel_Trajectory.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
constexpr TCHAR SchemaPath[] = TEXT("/Game/Blueprints/Characters/Demo/Animation/MotionMatching/PSS_MH_Move.PSS_MH_Move");
constexpr TCHAR SheathedDatabasePath[] = TEXT("/Game/Blueprints/Characters/Demo/Animation/MotionMatching/PSD_MH_Shth_Move.PSD_MH_Shth_Move");
constexpr TCHAR UnsheathedDatabasePath[] = TEXT("/Game/Blueprints/Characters/Demo/Animation/MotionMatching/PSD_MH_UnSh_Move.PSD_MH_UnSh_Move");

constexpr float ContinuingPoseCostBias = -0.05f;
constexpr float ExcludeEndTime = -0.05f;
// Stop family is a categorical constraint at the input-release edge.  With
// lanes spaced by only 1/3, weight 8 allowed a pose-similar Walk Stop to beat
// a Run/Sprint request.  Keep MoveGait permissive, but make StopGait decisive.
constexpr float StopGaitWeight = 64.0f;
constexpr float MoveGaitWeight = 8.0f;

struct FPMM7SequenceSpec
{
	const TCHAR* ObjectPath;
	float StopGaitValue = 0.0f;
	float MoveGaitValue = 0.0f;
};

const TArray<FPMM7SequenceSpec>& GetSheathedSequenceSpecs()
{
	static const TArray<FPMM7SequenceSpec> Specs =
	{
		{ TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Idle.AS_Shth_Idle") },
		{ TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Walk_Start.AS_Shth_Walk_Start"), 0.0f, 1.0f / 3.0f },
		{ TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Walk_Loop.AS_Shth_Walk_Loop"), 0.0f, 1.0f / 3.0f },
		{ TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Run_Start.AS_Shth_Run_Start"), 0.0f, 2.0f / 3.0f },
		{ TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Run_Loop.AS_Shth_Run_Loop"), 0.0f, 2.0f / 3.0f },
		{ TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Sprint_Start.AS_Shth_Sprint_Start"), 0.0f, 1.0f },
		{ TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Sprint_Loop_125x.AS_Shth_Sprint_Loop_125x"), 0.0f, 1.0f },
		{ TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/Generated/AS_Shth_Walk_Stop_Left_Extended.AS_Shth_Walk_Stop_Left_Extended"), 1.0f / 3.0f },
		{ TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/Generated/AS_Shth_Walk_Stop_Right_Extended.AS_Shth_Walk_Stop_Right_Extended"), 1.0f / 3.0f },
		{ TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/Generated/AS_Shth_Walk_FirstStepCommitStop.AS_Shth_Walk_FirstStepCommitStop"), 1.0f / 3.0f },
		{ TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/Generated/AS_Shth_Run_Stop_Left_Extended.AS_Shth_Run_Stop_Left_Extended"), 2.0f / 3.0f },
		{ TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/Generated/AS_Shth_Run_Stop_Right_Extended.AS_Shth_Run_Stop_Right_Extended"), 2.0f / 3.0f },
		{ TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/Generated/AS_Shth_Run_FirstStepCommitStop.AS_Shth_Run_FirstStepCommitStop"), 2.0f / 3.0f },
		{ TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/Generated/AS_Shth_Sprint_Stop_Left_Extended.AS_Shth_Sprint_Stop_Left_Extended"), 1.0f },
		{ TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/Generated/AS_Shth_Sprint_Stop_Right_Extended.AS_Shth_Sprint_Stop_Right_Extended"), 1.0f },
		{ TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/Generated/AS_Shth_Sprint_FirstStepCommitStop.AS_Shth_Sprint_FirstStepCommitStop"), 1.0f }
	};
	return Specs;
}

const TArray<FPMM7SequenceSpec>& GetUnsheathedSequenceSpecs()
{
	static const TArray<FPMM7SequenceSpec> Specs =
	{
		{ TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Locomotion/AS_UnSh_Idle.AS_UnSh_Idle") },
		{ TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Locomotion/AS_UnSh_Walk_Start.AS_UnSh_Walk_Start"), 0.0f, 2.0f / 3.0f },
		{ TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Locomotion/AS_UnSh_Walk_Loop.AS_UnSh_Walk_Loop"), 0.0f, 2.0f / 3.0f },
		{ TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Locomotion/AS_UnSh_Walk_Stop.AS_UnSh_Walk_Stop") }
	};
	return Specs;
}

bool SaveAsset(UObject& Asset)
{
	UPackage* Package = Asset.GetOutermost();
	FString Filename;
	if (!Package || !FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), Filename,
		FPackageName::GetAssetPackageExtension()))
	{
		return false;
	}

	Package->MarkPackageDirty();
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	return UPackage::SavePackage(Package, &Asset, *Filename, SaveArgs);
}

template <typename TChannel>
TChannel* FindMutableChannel(UPoseSearchSchema& Schema)
{
	for (const TObjectPtr<UPoseSearchFeatureChannel>& Channel : Schema.GetChannels())
	{
		if (const TChannel* Found = Cast<TChannel>(Channel.Get()))
		{
			return const_cast<TChannel*>(Found);
		}
	}
	return nullptr;
}

bool ConfigureSchema(UPoseSearchSchema& Schema)
{
	UPoseSearchFeatureChannel_Pose* PoseChannel = FindMutableChannel<UPoseSearchFeatureChannel_Pose>(Schema);
	UPoseSearchFeatureChannel_Trajectory* TrajectoryChannel = FindMutableChannel<UPoseSearchFeatureChannel_Trajectory>(Schema);
	UPoseSearchFeatureChannel_Curve* IntentChannel = nullptr;
	UPoseSearchFeatureChannel_Curve* DistanceChannel = nullptr;
	UPoseSearchFeatureChannel_Curve* ExistingStopGaitChannel = nullptr;
	UPoseSearchFeatureChannel_Curve* ExistingMoveGaitChannel = nullptr;
	for (const TObjectPtr<UPoseSearchFeatureChannel>& Channel : Schema.GetChannels())
	{
		if (const UPoseSearchFeatureChannel_Curve* Curve = Cast<UPoseSearchFeatureChannel_Curve>(Channel.Get()))
		{
			if (Curve->CurveName == TEXT("MM_Intent"))
			{
				IntentChannel = const_cast<UPoseSearchFeatureChannel_Curve*>(Curve);
			}
			else if (Curve->CurveName == TEXT("MM_DistanceToStop"))
			{
				DistanceChannel = const_cast<UPoseSearchFeatureChannel_Curve*>(Curve);
			}
			else if (Curve->CurveName == TEXT("MM_StopGait"))
			{
				ExistingStopGaitChannel = const_cast<UPoseSearchFeatureChannel_Curve*>(Curve);
			}
			else if (Curve->CurveName == TEXT("MM_MoveGait"))
			{
				ExistingMoveGaitChannel = const_cast<UPoseSearchFeatureChannel_Curve*>(Curve);
			}
		}
	}
	if (!PoseChannel || !TrajectoryChannel || !IntentChannel || !DistanceChannel)
	{
		UE_LOG(LogTemp, Error, TEXT("[PMM7Fixup] PSS_MH_Move is missing one of Pose, Trajectory, MM_Intent, or MM_DistanceToStop."));
		return false;
	}

	Schema.Modify();
	UMHGZPoseSearchFeatureChannel_StopGait* StopGaitChannel =
		Cast<UMHGZPoseSearchFeatureChannel_StopGait>(ExistingStopGaitChannel);
	if (ExistingStopGaitChannel && !StopGaitChannel)
	{
		UE_LOG(LogTemp, Error, TEXT("[PMM7Fixup] MM_StopGait already exists but is not the native MHGZ Stop Gait channel."));
		return false;
	}
	if (!StopGaitChannel)
	{
		StopGaitChannel = NewObject<UMHGZPoseSearchFeatureChannel_StopGait>(&Schema, NAME_None,
			RF_Transactional);
		if (!StopGaitChannel)
		{
			return false;
		}
		Schema.AddChannel(StopGaitChannel);
	}
	UMHGZPoseSearchFeatureChannel_MoveGait* MoveGaitChannel =
		Cast<UMHGZPoseSearchFeatureChannel_MoveGait>(ExistingMoveGaitChannel);
	if (ExistingMoveGaitChannel && !MoveGaitChannel)
	{
		UE_LOG(LogTemp, Error, TEXT("[PMM7Fixup] MM_MoveGait already exists but is not the native MHGZ Move Gait channel."));
		return false;
	}
	if (!MoveGaitChannel)
	{
		MoveGaitChannel = NewObject<UMHGZPoseSearchFeatureChannel_MoveGait>(&Schema, NAME_None,
			RF_Transactional);
		if (!MoveGaitChannel)
		{
			return false;
		}
		Schema.AddChannel(MoveGaitChannel);
	}

	StopGaitChannel->CurveName = TEXT("MM_StopGait");
	StopGaitChannel->Weight = StopGaitWeight;
	StopGaitChannel->SampleTimeOffset = 0.0f;
	StopGaitChannel->InputQueryPose = EInputQueryPose::UseContinuingPose;
	MoveGaitChannel->CurveName = TEXT("MM_MoveGait");
	MoveGaitChannel->Weight = MoveGaitWeight;
	MoveGaitChannel->SampleTimeOffset = 0.0f;
	MoveGaitChannel->InputQueryPose = EInputQueryPose::UseContinuingPose;
	Schema.PostEditChange();
	return true;
}

bool ReplaceConstantFloatCurve(UAnimSequence& Sequence, const FName CurveName, const float Value)
{
	const float Length = Sequence.GetPlayLength();
	if (Length <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	if (Sequence.HasCurveData(CurveName, true))
	{
		UAnimationBlueprintLibrary::RemoveCurve(&Sequence, CurveName);
	}
	UAnimationBlueprintLibrary::AddCurve(&Sequence, CurveName, ERawCurveTrackTypes::RCT_Float);
	UAnimationBlueprintLibrary::AddFloatCurveKeys(&Sequence, CurveName,
		{ 0.0f, Length }, { Value, Value });
	return Sequence.HasCurveData(CurveName, true) && SaveAsset(Sequence);
}

bool ConfigureSequenceCurves(UAnimSequence& Sequence, const FPMM7SequenceSpec& Spec)
{
	return ReplaceConstantFloatCurve(Sequence, TEXT("MM_StopGait"), Spec.StopGaitValue)
		&& ReplaceConstantFloatCurve(Sequence, TEXT("MM_MoveGait"), Spec.MoveGaitValue);
}

bool LoadAndConfigureSequences(const TArray<FPMM7SequenceSpec>& Specs,
	TArray<UAnimSequence*>& OutSequences)
{
	OutSequences.Reset();
	OutSequences.Reserve(Specs.Num());
	for (const FPMM7SequenceSpec& Spec : Specs)
	{
		UAnimSequence* Sequence = LoadObject<UAnimSequence>(nullptr, Spec.ObjectPath);
		if (!Sequence)
		{
			UE_LOG(LogTemp, Error, TEXT("[PMM7Fixup] Could not load %s."), Spec.ObjectPath);
			return false;
		}
		if (!ConfigureSequenceCurves(*Sequence, Spec))
		{
			UE_LOG(LogTemp, Error, TEXT("[PMM7Fixup] Could not write gait curves on %s."),
				*Sequence->GetPathName());
			return false;
		}
		OutSequences.Add(Sequence);
	}
	return true;
}

bool ConfigureDatabase(UPoseSearchDatabase& Database, const TArray<UAnimSequence*>& RequiredSequences)
{
	TSet<UAnimSequence*> RequiredSet(RequiredSequences);
	Database.Modify();
	for (int32 Index = Database.GetNumAnimationAssets() - 1; Index >= 0; --Index)
	{
		const FPoseSearchDatabaseSequence* Existing =
			Database.GetDatabaseAnimationAsset<FPoseSearchDatabaseSequence>(Index);
		if (!Existing || !RequiredSet.Contains(Existing->Sequence))
		{
			Database.RemoveAnimationAssetAt(Index);
		}
	}

	for (UAnimSequence* Sequence : RequiredSequences)
	{
		bool bFound = false;
		for (int32 Index = 0; Index < Database.GetNumAnimationAssets(); ++Index)
		{
			const FPoseSearchDatabaseSequence* Existing =
				Database.GetDatabaseAnimationAsset<FPoseSearchDatabaseSequence>(Index);
			bFound |= Existing && Existing->Sequence == Sequence;
		}
		if (!bFound)
		{
			FPoseSearchDatabaseSequence NewEntry;
			NewEntry.Sequence = Sequence;
			Database.AddAnimationAsset(FInstancedStruct::Make(NewEntry));
		}
	}

	Database.ContinuingPoseCostBias = ContinuingPoseCostBias;
	Database.LoopingCostBias = 0.0f;
	Database.ExcludeFromDatabaseParameters = FFloatInterval(0.0f, ExcludeEndTime);
	Database.PoseSearchMode = EPoseSearchMode::BruteForce;
	for (int32 Index = 0; Index < Database.GetNumAnimationAssets(); ++Index)
	{
		FPoseSearchDatabaseSequence* Entry =
			Database.GetMutableDatabaseAnimationAsset<FPoseSearchDatabaseSequence>(Index);
		if (!Entry || !Entry->Sequence)
		{
			return false;
		}
		Entry->SetDisableReselection(true);
		Entry->MirrorOption = EPoseSearchMirrorOption::UnmirroredOnly;
	}
	return true;
}

bool RebuildDatabaseIndex(const UPoseSearchDatabase& Database)
{
	using namespace UE::PoseSearch;
	const EAsyncBuildIndexResult Result = FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(
		&Database, ERequestAsyncBuildFlag::NewRequest | ERequestAsyncBuildFlag::WaitForCompletion);
	if (Result != EAsyncBuildIndexResult::Success)
	{
		UE_LOG(LogTemp, Error, TEXT("[PMM7Fixup] Pose Search index rebuild failed for %s (%d)."),
			*Database.GetPathName(), static_cast<int32>(Result));
		return false;
	}
	return true;
}
}

#endif

UMHGZPMM7AssetFixupCommandlet::UMHGZPMM7AssetFixupCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMHGZPMM7AssetFixupCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	UPoseSearchSchema* Schema = LoadObject<UPoseSearchSchema>(nullptr, SchemaPath);
	UPoseSearchDatabase* SheathedDatabase = LoadObject<UPoseSearchDatabase>(nullptr, SheathedDatabasePath);
	UPoseSearchDatabase* UnsheathedDatabase = LoadObject<UPoseSearchDatabase>(nullptr, UnsheathedDatabasePath);
	if (!Schema || !SheathedDatabase || !UnsheathedDatabase)
	{
		UE_LOG(LogTemp, Error, TEXT("[PMM7Fixup] Failed to load PSS_MH_Move or a locomotion PSD."));
		return 1;
	}

	TArray<UAnimSequence*> SheathedSequences;
	TArray<UAnimSequence*> UnsheathedSequences;
	const bool bConfigured = ConfigureSchema(*Schema)
		&& LoadAndConfigureSequences(GetSheathedSequenceSpecs(), SheathedSequences)
		&& LoadAndConfigureSequences(GetUnsheathedSequenceSpecs(), UnsheathedSequences)
		&& ConfigureDatabase(*SheathedDatabase, SheathedSequences)
		&& ConfigureDatabase(*UnsheathedDatabase, UnsheathedSequences);
	const bool bSaved = bConfigured && SaveAsset(*Schema) && SaveAsset(*SheathedDatabase)
		&& SaveAsset(*UnsheathedDatabase);
	const bool bIndexed = bSaved && RebuildDatabaseIndex(*SheathedDatabase)
		&& RebuildDatabaseIndex(*UnsheathedDatabase);
	if (!bIndexed)
	{
		UE_LOG(LogTemp, Error, TEXT("[PMM7Fixup] Failed to apply the PMM-7 StopGait asset contract."));
		return 1;
	}

	UE_LOG(LogTemp, Display, TEXT("[PMM7Fixup] Installed native MM_StopGait/MM_MoveGait, configured 16 sheathed + 4 unsheathed candidates, and rebuilt both Pose Search indices."));
	return 0;
#else
	UE_LOG(LogTemp, Error, TEXT("[PMM7Fixup] This commandlet requires an editor build."));
	return 1;
#endif
}
