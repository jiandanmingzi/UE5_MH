// Copyright MHGZ Project. All Rights Reserved.

#include "Editor/MHGZE4ActionIdleAssetSetupCommandlet.h"

#if WITH_EDITOR

#include "Animation/AnimSequence.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchDerivedData.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace UE::MHGZ::E4ActionIdleAssetSetup
{
constexpr TCHAR SchemaPath[] =
	TEXT("/Game/Blueprints/Characters/Demo/Animation/MotionMatching/PSS_MH_Move.PSS_MH_Move");
constexpr TCHAR DatabaseDirectory[] =
	TEXT("/Game/Blueprints/Characters/Demo/Animation/MotionMatching");
constexpr TCHAR AuditDirectoryName[] = TEXT("ActionExitAudit");

struct FActionIdleRoute
{
	const TCHAR* Label;
	const TCHAR* SequencePath;
	const TCHAR* DatabaseName;
};

constexpr FActionIdleRoute Routes[] =
{
	{
		TEXT("SheathedActionIdle"),
		TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Idle.AS_Shth_Idle"),
		TEXT("PSD_MH_Shth_ActionIdle")
	},
	{
		TEXT("UnsheathedActionIdle"),
		TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Locomotion/AS_UnSh_Idle.AS_UnSh_Idle"),
		TEXT("PSD_MH_UnSh_ActionIdle")
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

bool ConfigureDatabase(UPoseSearchDatabase& Database, UPoseSearchSchema& Schema,
	UAnimSequence& Sequence)
{
	if (!Sequence.bLoop || Sequence.GetPlayLength() <= KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogTemp, Error, TEXT("[E4ActionIdleSetup] Idle sequence contract failed: %s"),
			*Sequence.GetPathName());
		return false;
	}

	Database.Modify();
	Database.Schema = &Schema;
	Database.ContinuingPoseCostBias = 0.0f;
	Database.LoopingCostBias = 0.0f;
	Database.ExcludeFromDatabaseParameters = FFloatInterval(0.0f, 0.0f);
	Database.PoseSearchMode = EPoseSearchMode::BruteForce;
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
	const EAsyncBuildIndexResult Result = FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(
		&Database, ERequestAsyncBuildFlag::NewRequest | ERequestAsyncBuildFlag::WaitForCompletion);
	if (Result != EAsyncBuildIndexResult::Success || Database.GetSearchIndex().GetNumPoses() <= 0)
	{
		UE_LOG(LogTemp, Error, TEXT("[E4ActionIdleSetup] Index build failed for %s (%d)."),
			*Database.GetPathName(), static_cast<int32>(Result));
		return false;
	}
	return true;
}

bool ValidateRoute(const FActionIdleRoute& Route, const UPoseSearchSchema& Schema,
	const UAnimSequence& Sequence, const UPoseSearchDatabase& Database)
{
	const FPoseSearchDatabaseSequence* Entry =
		Database.GetDatabaseAnimationAsset<FPoseSearchDatabaseSequence>(0);
	const bool bValid = Database.Schema == &Schema
		&& Database.GetNumAnimationAssets() == 1
		&& Entry && Entry->Sequence == &Sequence && Entry->bDisableReselection
		&& Entry->MirrorOption == EPoseSearchMirrorOption::UnmirroredOnly
		&& FMath::IsNearlyZero(Entry->SamplingRange.Min)
		&& FMath::IsNearlyZero(Entry->SamplingRange.Max)
		&& Database.GetSearchIndex().GetNumPoses() > 0;
	if (!bValid)
	{
		UE_LOG(LogTemp, Error, TEXT("[E4ActionIdleSetup] Validation failed for %s."), Route.Label);
	}
	return bValid;
}

bool WriteAudit(const TArray<FString>& Rows)
{
	const FString OutputDirectory = FPaths::ProjectSavedDir() / AuditDirectoryName;
	IFileManager::Get().MakeDirectory(*OutputDirectory, true);
	const FString OutputPath = OutputDirectory / TEXT("E4_2_ActionIdleAssetSetup.md");
	TArray<FString> Lines;
	Lines.Add(TEXT("# E4.2 Action Idle Candidate Assets"));
	Lines.Add(TEXT(""));
	Lines.Add(TEXT("These databases are selected only when a non-handoff functional action releases with no locomotion input."));
	Lines.Add(TEXT("They contain exactly one existing looping Idle sequence and do not modify that sequence or either normal Move database."));
	Lines.Add(TEXT(""));
	Lines.Add(TEXT("| Route | Idle sequence | Idle-only PSD | Indexed poses |"));
	Lines.Add(TEXT("|---|---|---|---:|"));
	Lines.Append(Rows);
	return FFileHelper::SaveStringToFile(FString::Join(Lines, TEXT("\n")), *OutputPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
}

#endif // WITH_EDITOR

UMHGZE4ActionIdleAssetSetupCommandlet::UMHGZE4ActionIdleAssetSetupCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMHGZE4ActionIdleAssetSetupCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	using namespace UE::MHGZ::E4ActionIdleAssetSetup;
	const bool bReplaceGenerated = FParse::Param(*Params, TEXT("ReplaceGenerated"));
	UPoseSearchSchema* Schema = LoadObject<UPoseSearchSchema>(nullptr, SchemaPath);
	if (!Schema)
	{
		UE_LOG(LogTemp, Error, TEXT("[E4ActionIdleSetup] Could not load PSS_MH_Move."));
		return 1;
	}

	bool bSucceeded = true;
	TArray<FString> Rows;
	for (const FActionIdleRoute& Route : Routes)
	{
		UAnimSequence* Sequence = LoadObject<UAnimSequence>(nullptr, Route.SequencePath);
		const FString PackageName = FString::Printf(TEXT("%s/%s"), DatabaseDirectory, Route.DatabaseName);
		const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PackageName, Route.DatabaseName);
		UPoseSearchDatabase* Database = LoadObject<UPoseSearchDatabase>(nullptr, *ObjectPath);
		if (Database && !bReplaceGenerated)
		{
			UE_LOG(LogTemp, Error, TEXT("[E4ActionIdleSetup] %s already exists; rerun with -ReplaceGenerated to rebuild it."),
				*ObjectPath);
			bSucceeded = false;
			continue;
		}
		if (!Database)
		{
			UPackage* Package = CreatePackage(*PackageName);
			Database = Package ? NewObject<UPoseSearchDatabase>(Package, Route.DatabaseName,
				RF_Public | RF_Standalone) : nullptr;
		}

		if (!Sequence || !Database || !ConfigureDatabase(*Database, *Schema, *Sequence)
			|| !RebuildDatabaseIndex(*Database) || !ValidateRoute(Route, *Schema, *Sequence, *Database))
		{
			bSucceeded = false;
			continue;
		}

		Rows.Add(FString::Printf(TEXT("| %s | %s | %s | %d |"), Route.Label,
			*Sequence->GetPathName(), *Database->GetPathName(), Database->GetSearchIndex().GetNumPoses()));
	}

	if (!WriteAudit(Rows))
	{
		UE_LOG(LogTemp, Error, TEXT("[E4ActionIdleSetup] Failed to write the setup audit."));
		bSucceeded = false;
	}
	return bSucceeded ? 0 : 1;
#else
	UE_LOG(LogTemp, Error, TEXT("[E4ActionIdleSetup] This commandlet requires an editor build."));
	return 1;
#endif
}
