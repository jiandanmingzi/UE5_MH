// Copyright MHGZ Project. All Rights Reserved.

#include "Editor/MHGZPMM4AssetFixupCommandlet.h"

#if WITH_EDITOR

#include "Animation/AnimSequence.h"
#include "Misc/PackageName.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchDerivedData.h"
#include "PoseSearch/PoseSearchFeatureChannel_Curve.h"
#include "PoseSearch/PoseSearchFeatureChannel_Pose.h"
#include "PoseSearch/PoseSearchFeatureChannel_Trajectory.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
constexpr TCHAR SchemaPath[] = TEXT("/Game/Blueprints/Characters/Demo/Animation/MotionMatching/PSS_MH_Move.PSS_MH_Move");
constexpr TCHAR SheathedDatabasePath[] = TEXT("/Game/Blueprints/Characters/Demo/Animation/MotionMatching/PSD_MH_Shth_Move.PSD_MH_Shth_Move");
constexpr TCHAR UnsheathedDatabasePath[] = TEXT("/Game/Blueprints/Characters/Demo/Animation/MotionMatching/PSD_MH_UnSh_Move.PSD_MH_UnSh_Move");

constexpr float ContinuingPoseCostBias = -0.05f;
constexpr float OrdinaryNonLoopSearchTailTrim = 0.05f;
constexpr float TrajectoryChannelWeight = 6.0f;

bool IsGeneratedStop(const UAnimSequence& Sequence)
{
	const FString Name = Sequence.GetName();
	return Name.Contains(TEXT("Extended")) || Name.Contains(TEXT("FirstStepCommitStop"));
}

FFloatInterval GetSamplingRange(const UAnimSequence& Sequence)
{
	// A zero interval means the complete animation range. Generated PMM-7 Stops
	// need their indexed zero tail for the final Continuing update, while normal
	// one-shots retain the old tail trim as a per-entry, not global, policy.
	if (Sequence.bLoop || Sequence.GetName().Contains(TEXT("_Idle")) || IsGeneratedStop(Sequence))
	{
		return FFloatInterval(0.0f, 0.0f);
	}

	return FFloatInterval(0.0f,
		FMath::Max(0.0f, Sequence.GetPlayLength() - OrdinaryNonLoopSearchTailTrim));
}

bool SaveAsset(UObject& Asset)
{
	UPackage* Package = Asset.GetOutermost();
	FString Filename;
	if (!Package || !FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), Filename, FPackageName::GetAssetPackageExtension()))
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
		if (const TChannel* FoundChannel = Cast<TChannel>(Channel.Get()))
		{
			return const_cast<TChannel*>(FoundChannel);
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
		}
	}

	if (!PoseChannel || !TrajectoryChannel || !IntentChannel || !DistanceChannel || PoseChannel->SampledBones.Num() != 2)
	{
		UE_LOG(LogTemp, Error, TEXT("[PMM4Fixup] PSS_MH_Move does not have the expected Pose, Trajectory, MM_Intent and MM_DistanceToStop channel layout."));
		return false;
	}

	const int32 RequiredPoseFlags = int32(EPoseSearchBoneFlags::Position) | int32(EPoseSearchBoneFlags::Velocity) | int32(EPoseSearchBoneFlags::Phase);
	Schema.Modify();
	Schema.SampleRate = 60;
	Schema.DataPreprocessor = EPoseSearchDataPreprocessor::Normalize;

	PoseChannel->Weight = 2.0f;
	PoseChannel->InputQueryPose = EInputQueryPose::UseContinuingPose;
	for (FPoseSearchBone& Bone : PoseChannel->SampledBones)
	{
		Bone.Flags = RequiredPoseFlags;
		Bone.Weight = 1.0f;
	}

	TrajectoryChannel->Weight = TrajectoryChannelWeight;
	TrajectoryChannel->Samples.Reset();
	for (const float Offset : { 0.2f, 0.5f, 0.8f, 1.0f })
	{
		FPoseSearchTrajectorySample& Sample = TrajectoryChannel->Samples.AddDefaulted_GetRef();
		Sample.Offset = Offset;
		Sample.Flags = int32(EPoseSearchTrajectoryFlags::Position);
		Sample.Weight = 1.0f;
	}

	IntentChannel->Weight = 10.0f;
	IntentChannel->SampleTimeOffset = 0.0f;
	IntentChannel->InputQueryPose = EInputQueryPose::UseContinuingPose;
	DistanceChannel->Weight = 4.0f;
	DistanceChannel->SampleTimeOffset = 0.0f;
	DistanceChannel->InputQueryPose = EInputQueryPose::UseContinuingPose;

	// UPoseSearchSchema performs its private Finalize pass from this public
	// editor notification, rebuilding transient channel and bone references.
	Schema.PostEditChange();
	return true;
}

bool ConfigureDatabase(UPoseSearchDatabase& Database)
{
	Database.Modify();
	Database.ContinuingPoseCostBias = ContinuingPoseCostBias;
	Database.LoopingCostBias = 0.0f;
	Database.ExcludeFromDatabaseParameters = FFloatInterval(0.0f, 0.0f);
	Database.PoseSearchMode = EPoseSearchMode::BruteForce;

	bool bSuccess = true;
	for (int32 EntryIndex = 0; EntryIndex < Database.GetNumAnimationAssets(); ++EntryIndex)
	{
		FPoseSearchDatabaseSequence* Entry = Database.GetMutableDatabaseAnimationAsset<FPoseSearchDatabaseSequence>(EntryIndex);
		if (!Entry || !Entry->Sequence)
		{
			UE_LOG(LogTemp, Error, TEXT("[PMM4Fixup] %s entry %d is not a valid animation sequence."), *Database.GetPathName(), EntryIndex);
			bSuccess = false;
			continue;
		}

		// Continuing-pose evaluation remains available, but a global search may not
		// jump back into the currently playing source asset. Applying this to Start
		// and Stop is essential: otherwise every throttled global search can restart
		// the same transition from frame zero.
		Entry->SetDisableReselection(true);
		Entry->MirrorOption = EPoseSearchMirrorOption::UnmirroredOnly;
		Entry->SamplingRange = GetSamplingRange(*Entry->Sequence);
	}

	return bSuccess;
}

bool ConfigureDisableReselectionOnly(UPoseSearchDatabase& Database)
{
	Database.Modify();
	bool bSuccess = true;
	for (int32 EntryIndex = 0; EntryIndex < Database.GetNumAnimationAssets(); ++EntryIndex)
	{
		FPoseSearchDatabaseSequence* Entry = Database.GetMutableDatabaseAnimationAsset<FPoseSearchDatabaseSequence>(EntryIndex);
		if (!Entry || !Entry->Sequence)
		{
			UE_LOG(LogTemp, Error, TEXT("[PMM4Fixup] %s entry %d is not a valid animation sequence."), *Database.GetPathName(), EntryIndex);
			bSuccess = false;
			continue;
		}

		Entry->SetDisableReselection(true);
	}

	return bSuccess;
}

bool RebuildDatabaseIndex(const UPoseSearchDatabase& Database)
{
	using namespace UE::PoseSearch;
	const ERequestAsyncBuildFlag Flags = ERequestAsyncBuildFlag::NewRequest | ERequestAsyncBuildFlag::WaitForCompletion;
	const EAsyncBuildIndexResult Result = FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(&Database, Flags);
	if (Result != EAsyncBuildIndexResult::Success)
	{
		UE_LOG(LogTemp, Error, TEXT("[PMM4Fixup] Pose Search index rebuild failed for %s (result=%d)."), *Database.GetPathName(), static_cast<int32>(Result));
		return false;
	}
	return true;
}
}

#endif

UMHGZPMM4AssetFixupCommandlet::UMHGZPMM4AssetFixupCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMHGZPMM4AssetFixupCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	UPoseSearchSchema* Schema = LoadObject<UPoseSearchSchema>(nullptr, SchemaPath);
	UPoseSearchDatabase* SheathedDatabase = LoadObject<UPoseSearchDatabase>(nullptr, SheathedDatabasePath);
	UPoseSearchDatabase* UnsheathedDatabase = LoadObject<UPoseSearchDatabase>(nullptr, UnsheathedDatabasePath);
	if (!Schema || !SheathedDatabase || !UnsheathedDatabase)
	{
		UE_LOG(LogTemp, Error, TEXT("[PMM4Fixup] Failed to load one or more PMM-4 assets."));
		return 1;
	}

	const bool bOnlyDisableReselection = FParse::Param(*Params, TEXT("OnlyDisableReselection"));
	const bool bConfigured = bOnlyDisableReselection
		? ConfigureDisableReselectionOnly(*SheathedDatabase)
			&& ConfigureDisableReselectionOnly(*UnsheathedDatabase)
		: ConfigureSchema(*Schema)
			&& ConfigureDatabase(*SheathedDatabase)
			&& ConfigureDatabase(*UnsheathedDatabase);
	const bool bSaved = bConfigured
		&& (!bOnlyDisableReselection || SaveAsset(*SheathedDatabase))
		&& (!bOnlyDisableReselection || SaveAsset(*UnsheathedDatabase))
		&& (bOnlyDisableReselection || SaveAsset(*Schema))
		&& (bOnlyDisableReselection || SaveAsset(*SheathedDatabase))
		&& (bOnlyDisableReselection || SaveAsset(*UnsheathedDatabase));
	const bool bIndexed = bSaved
		&& RebuildDatabaseIndex(*SheathedDatabase)
		&& RebuildDatabaseIndex(*UnsheathedDatabase);
	if (!bIndexed)
	{
		UE_LOG(LogTemp, Error, TEXT("[PMM4Fixup] Failed to configure or save PMM-4 assets."));
		return 1;
	}

	if (bOnlyDisableReselection)
	{
		UE_LOG(LogTemp, Display, TEXT("[PMM4Fixup] Updated DisableReselection and rebuilt PSD_MH_Shth_Move and PSD_MH_UnSh_Move."));
	}
	else
	{
		UE_LOG(LogTemp, Display, TEXT("[PMM4Fixup] Updated PSS_MH_Move, PSD_MH_Shth_Move and PSD_MH_UnSh_Move."));
	}
	return 0;
#else
	UE_LOG(LogTemp, Error, TEXT("[PMM4Fixup] This commandlet requires an editor build."));
	return 1;
#endif
}
