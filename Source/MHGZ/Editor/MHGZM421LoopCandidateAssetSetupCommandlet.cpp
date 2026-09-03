// Copyright MHGZ Project. All Rights Reserved.

#include "Editor/MHGZM421LoopCandidateAssetSetupCommandlet.h"

#if WITH_EDITOR

#include "Animation/AnimSequence.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchDerivedData.h"
#include "PoseSearch/PoseSearchIndex.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace UE::MHGZ::M421LoopCandidateAssetSetup
{
constexpr TCHAR SchemaPath[] =
	TEXT("/Game/Blueprints/Characters/Demo/Animation/MotionMatching/PSS_MH_Move.PSS_MH_Move");
constexpr TCHAR FullMoveDatabasePath[] =
	TEXT("/Game/Blueprints/Characters/Demo/Animation/MotionMatching/PSD_MH_Shth_Move.PSD_MH_Shth_Move");
constexpr TCHAR DatabaseDirectory[] =
	TEXT("/Game/Blueprints/Characters/Demo/Animation/MotionMatching");
constexpr TCHAR AuditDirectoryName[] = TEXT("MotionMatchingAudit");

const FName IntentCurveName(TEXT("MM_Intent"));
const FName DistanceToStopCurveName(TEXT("MM_DistanceToStop"));
const FName StopGaitCurveName(TEXT("MM_StopGait"));
const FName MoveGaitCurveName(TEXT("MM_MoveGait"));

struct FLoopCandidateRoute
{
	const TCHAR* Label;
	const TCHAR* SequencePath;
	const TCHAR* DatabaseName;
	float ExpectedMoveGait;
};

static const FLoopCandidateRoute Routes[] =
{
	{
		TEXT("SheathedRunLoopOnly"),
		TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Run_Loop.AS_Shth_Run_Loop"),
		TEXT("PSD_MH_Shth_Run_LoopOnly"),
		2.0f / 3.0f
	},
	{
		TEXT("SheathedSprintLoopOnly"),
		TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Sprint_Loop_125x.AS_Shth_Sprint_Loop_125x"),
		TEXT("PSD_MH_Shth_Sprint_LoopOnly"),
		1.0f
	}
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

bool HasConstantCurveValue(const UAnimSequence& Sequence, const FName CurveName,
	const float ExpectedValue)
{
	const FFloatCurve* Curve = static_cast<const FFloatCurve*>(Sequence.GetCurveData().GetCurveData(CurveName, ERawCurveTrackTypes::RCT_Float));
	if (!Curve)
	{
		return false;
	}

	const TArray<FRichCurveKey>& Keys = Curve->FloatCurve.GetConstRefOfKeys();
	if (Keys.IsEmpty())
	{
		return false;
	}
	for (const FRichCurveKey& Key : Keys)
	{
		if (!FMath::IsNearlyEqual(Key.Value, ExpectedValue, KINDA_SMALL_NUMBER))
		{
			return false;
		}
	}
	return true;
}

bool HasUnexpectedPoseSearchNotify(const UAnimSequence& Sequence)
{
	return Sequence.Notifies.ContainsByPredicate([](const FAnimNotifyEvent& Notify)
	{
		return Notify.NotifyStateClass != nullptr || Notify.Notify != nullptr;
	});
}

bool ConfigureDatabase(UPoseSearchDatabase& Database, const UPoseSearchDatabase& FullMoveDatabase,
	UPoseSearchSchema& Schema, UAnimSequence& Sequence)
{
	Database.Modify();
	Database.Schema = &Schema;
	Database.ContinuingPoseCostBias = FullMoveDatabase.ContinuingPoseCostBias;
	Database.LoopingCostBias = FullMoveDatabase.LoopingCostBias;
	Database.ExcludeFromDatabaseParameters = FullMoveDatabase.ExcludeFromDatabaseParameters;
	Database.PoseSearchMode = FullMoveDatabase.PoseSearchMode;
	while (Database.GetNumAnimationAssets() > 0)
	{
		Database.RemoveAnimationAssetAt(Database.GetNumAnimationAssets() - 1);
	}

	FPoseSearchDatabaseSequence Entry;
	Entry.Sequence = &Sequence;
	Entry.SetDisableReselection(true);
	Entry.MirrorOption = EPoseSearchMirrorOption::UnmirroredOnly;
	Entry.SamplingRange = FFloatInterval(0.0f, 0.0f);
	Database.AddAnimationAsset(FInstancedStruct::Make(Entry));
	Database.PostEditChange();
	return SaveAsset(Database);
}

bool RebuildDatabaseIndex(UPoseSearchDatabase& Database)
{
	using namespace UE::PoseSearch;
	const EAsyncBuildIndexResult Result =
		FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(&Database,
			ERequestAsyncBuildFlag::NewRequest | ERequestAsyncBuildFlag::WaitForCompletion);
	if (Result != EAsyncBuildIndexResult::Success || Database.GetSearchIndex().GetNumPoses() <= 0)
	{
		UE_LOG(LogTemp, Error, TEXT("[M421LoopCandidate] Index build failed for %s (%d)."),
			*Database.GetPathName(), static_cast<int32>(Result));
		return false;
	}
	return true;
}

bool ValidateDatabase(const UPoseSearchSchema& Schema, const UPoseSearchDatabase& FullMoveDatabase,
	const UAnimSequence& Sequence, const UPoseSearchDatabase& Database,
	const float ExpectedMoveGait)
{
	if (Database.Schema != &Schema
		|| Database.GetNumAnimationAssets() != 1
		|| Database.PoseSearchMode != FullMoveDatabase.PoseSearchMode
		|| !FMath::IsNearlyEqual(Database.ContinuingPoseCostBias, FullMoveDatabase.ContinuingPoseCostBias)
		|| !FMath::IsNearlyEqual(Database.LoopingCostBias, FullMoveDatabase.LoopingCostBias)
		|| !FMath::IsNearlyEqual(Database.ExcludeFromDatabaseParameters.Min, FullMoveDatabase.ExcludeFromDatabaseParameters.Min)
		|| !FMath::IsNearlyEqual(Database.ExcludeFromDatabaseParameters.Max, FullMoveDatabase.ExcludeFromDatabaseParameters.Max)
		|| Database.GetSearchIndex().GetNumPoses() <= 0
		|| Database.GetSearchIndex().bAnyBlockTransition)
	{
		return false;
	}

	const FPoseSearchDatabaseSequence* Entry =
		Database.GetDatabaseAnimationAsset<FPoseSearchDatabaseSequence>(0);
	if (!Entry || Entry->Sequence != &Sequence || !Entry->bDisableReselection
		|| Entry->MirrorOption != EPoseSearchMirrorOption::UnmirroredOnly
		|| !FMath::IsNearlyZero(Entry->SamplingRange.Min)
		|| !FMath::IsNearlyZero(Entry->SamplingRange.Max)
		|| !Sequence.bLoop || HasUnexpectedPoseSearchNotify(Sequence))
	{
		return false;
	}

	return HasConstantCurveValue(Sequence, IntentCurveName, 0.0f)
		&& HasConstantCurveValue(Sequence, DistanceToStopCurveName, 0.0f)
		&& HasConstantCurveValue(Sequence, StopGaitCurveName, 0.0f)
		&& HasConstantCurveValue(Sequence, MoveGaitCurveName, ExpectedMoveGait);
}

bool WriteAudit(const TArray<FString>& Rows)
{
	const FString OutputDirectory = FPaths::ProjectSavedDir() / AuditDirectoryName;
	IFileManager::Get().MakeDirectory(*OutputDirectory, true);
	const FString OutputPath = OutputDirectory / TEXT("M4_2_1_LoopCandidateAssetSetup.md");
	TArray<FString> Lines;
	Lines.Add(TEXT("# M4.2.1 Sheathed LoopOnly Candidate Assets"));
	Lines.Add(TEXT(""));
	Lines.Add(TEXT("This report covers only the generated Run/Sprint LoopOnly Pose Search databases."));
	Lines.Add(TEXT("The source sequences and PSD_MH_Shth_Move are intentionally not modified."));
	Lines.Add(TEXT(""));
	Lines.Add(TEXT("| Route | Source Loop | LoopOnly PSD | Poses | MM_MoveGait |"));
	Lines.Add(TEXT("|---|---|---|---:|---:|"));
	Lines.Append(Rows);
	Lines.Add(TEXT(""));
	Lines.Add(TEXT("Both databases contain exactly one looping sequence, with Disable Reselection, unmirrored sampling, a full [0, 0] range, no animation notifies, and no indexed BlockTransition metadata. Their database-level search configuration matches PSD_MH_Shth_Move."));
	return FFileHelper::SaveStringToFile(FString::Join(Lines, TEXT("\n")), *OutputPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
}
#endif // WITH_EDITOR

UMHGZM421LoopCandidateAssetSetupCommandlet::UMHGZM421LoopCandidateAssetSetupCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMHGZM421LoopCandidateAssetSetupCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	using namespace UE::MHGZ::M421LoopCandidateAssetSetup;
	const bool bReplaceGenerated = FParse::Param(*Params, TEXT("ReplaceGenerated"));
	const bool bAuditOnly = FParse::Param(*Params, TEXT("AuditOnly"));
	if (bReplaceGenerated && bAuditOnly)
	{
		UE_LOG(LogTemp, Error, TEXT("[M421LoopCandidate] -ReplaceGenerated and -AuditOnly are mutually exclusive."));
		return 1;
	}

	UPoseSearchSchema* Schema = LoadObject<UPoseSearchSchema>(nullptr, SchemaPath);
	UPoseSearchDatabase* FullMoveDatabase = LoadObject<UPoseSearchDatabase>(nullptr, FullMoveDatabasePath);
	if (!Schema || !FullMoveDatabase || FullMoveDatabase->Schema != Schema)
	{
		UE_LOG(LogTemp, Error, TEXT("[M421LoopCandidate] Could not load the valid PSS_MH_Move / PSD_MH_Shth_Move baseline."));
		return 1;
	}

	bool bSucceeded = true;
	TArray<FString> Rows;
	for (const FLoopCandidateRoute& Route : Routes)
	{
		UAnimSequence* Sequence = LoadObject<UAnimSequence>(nullptr, Route.SequencePath);
		const FString PackageName = FString::Printf(TEXT("%s/%s"), DatabaseDirectory, Route.DatabaseName);
		const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PackageName, Route.DatabaseName);
		UPoseSearchDatabase* Database = LoadObject<UPoseSearchDatabase>(nullptr, *ObjectPath);
		const bool bCreated = Database == nullptr && !bAuditOnly;
		if (bCreated)
		{
			UPackage* Package = CreatePackage(*PackageName);
			Database = Package ? NewObject<UPoseSearchDatabase>(Package, Route.DatabaseName,
				RF_Public | RF_Standalone) : nullptr;
		}

		const bool bMayConfigure = bCreated || bReplaceGenerated;
		const bool bConfigured = !bMayConfigure || (Sequence && Database
			&& ConfigureDatabase(*Database, *FullMoveDatabase, *Schema, *Sequence));
		const bool bIndexed = Database && RebuildDatabaseIndex(*Database);
		const bool bValid = Sequence && Database && ValidateDatabase(*Schema, *FullMoveDatabase,
			*Sequence, *Database, Route.ExpectedMoveGait);
		if (!bConfigured || !bIndexed || !bValid)
		{
			UE_LOG(LogTemp, Error, TEXT("[M421LoopCandidate] Failed to configure or validate %s."), Route.Label);
			bSucceeded = false;
			continue;
		}

		UE_LOG(LogTemp, Display, TEXT("[M421LoopCandidate] Validated %s -> %s."),
			Route.Label, *Database->GetPathName());
		Rows.Add(FString::Printf(TEXT("| %s | %s | %s | %d | %.3f |"), Route.Label,
			*Sequence->GetPathName(), *Database->GetPathName(),
			Database->GetSearchIndex().GetNumPoses(), Route.ExpectedMoveGait));
	}

	if (!WriteAudit(Rows))
	{
		UE_LOG(LogTemp, Error, TEXT("[M421LoopCandidate] Failed to write audit report."));
		bSucceeded = false;
	}
	UE_LOG(LogTemp, Display, TEXT("[M421LoopCandidate] Completed %d/%d routes."),
		Rows.Num(), UE_ARRAY_COUNT(Routes));
	return bSucceeded ? 0 : 1;
#else
	UE_LOG(LogTemp, Error, TEXT("[M421LoopCandidate] This commandlet requires an editor build."));
	return 1;
#endif
}
