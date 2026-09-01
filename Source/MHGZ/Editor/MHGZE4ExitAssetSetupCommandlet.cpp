// Copyright MHGZ Project. All Rights Reserved.

#include "Editor/MHGZE4ExitAssetSetupCommandlet.h"

#if WITH_EDITOR

#include "Animation/AnimSequence.h"
#include "AnimationBlueprintLibrary.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "PoseSearch/PoseSearchAnimNotifies.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchDerivedData.h"
#include "PoseSearch/PoseSearchIndex.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace UE::MHGZ::E4ExitAssetSetup
{
constexpr TCHAR SchemaPath[] =
	TEXT("/Game/Blueprints/Characters/Demo/Animation/MotionMatching/PSS_MH_Move.PSS_MH_Move");
constexpr TCHAR AuditDirectoryName[] = TEXT("ActionExitAudit");
constexpr float ContinuingPoseCostBias = -0.05f;
constexpr float ExitEntryOpenSeconds = 1.0f / 60.0f;
const FName PoseSearchControlTrackName(TEXT("PoseSearchControl"));

struct FExitRoute
{
	const TCHAR* Label;
	const TCHAR* SequencePath;
	const TCHAR* DatabasePackagePath;
	const TCHAR* DatabaseName;
};

static const FExitRoute Routes[] =
{
	{
		TEXT("SheatheMoveExit"),
		TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Locomotion/Transitions/Exit/AS_Shth_Sheathe_MoveExit.AS_Shth_Sheathe_MoveExit"),
		TEXT("/Game/Blueprints/Characters/Demo/Animation/MotionMatching"),
		TEXT("PSD_MH_Shth_SheatheExit")
	},
	{
		TEXT("SheathedDodgeMoveExit"),
		TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/Transitions/Exit/AS_Shth_Dodge_MoveExit.AS_Shth_Dodge_MoveExit"),
		TEXT("/Game/Blueprints/Characters/Demo/Animation/MotionMatching"),
		TEXT("PSD_MH_Shth_DodgeExit")
	},
	{
		TEXT("UnsheathedForwardDodgeMoveExit"),
		TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Locomotion/Transitions/Exit/AS_UnSh_Dodge_Forward_MoveExit.AS_UnSh_Dodge_Forward_MoveExit"),
		TEXT("/Game/Blueprints/Characters/Demo/Animation/MotionMatching"),
		TEXT("PSD_MH_UnSh_DodgeExit")
	}
};

const TArray<FName>& GetNeutralCurveNames()
{
	static const TArray<FName> Names =
	{
		TEXT("MM_Intent"),
		TEXT("MM_DistanceToStop"),
		TEXT("MM_StopGait"),
		TEXT("MM_MoveGait")
	};
	return Names;
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

bool IsExitPoseSearchControlNotify(const FAnimNotifyEvent& Notify)
{
	return Cast<UAnimNotifyState_PoseSearchBlockTransition>(Notify.NotifyStateClass) != nullptr
		|| Cast<UAnimNotifyState_PoseSearchOverrideContinuingPoseCostBias>(Notify.NotifyStateClass) != nullptr;
}

bool ConfigurePoseSearchControlNotifies(UAnimSequence& Sequence)
{
	Sequence.Notifies.RemoveAll([](const FAnimNotifyEvent& Notify)
	{
		return IsExitPoseSearchControlNotify(Notify);
	});
	Sequence.RefreshCacheData();

	if (!UAnimationBlueprintLibrary::IsValidAnimNotifyTrackName(&Sequence,
		PoseSearchControlTrackName))
	{
		UAnimationBlueprintLibrary::AddAnimationNotifyTrack(&Sequence,
			PoseSearchControlTrackName, FLinearColor::White);
	}

	const float Length = Sequence.GetPlayLength();
	const float BlockStart = FMath::Min(ExitEntryOpenSeconds,
		FMath::Max(0.0f, Length - KINDA_SMALL_NUMBER));
	const float BlockEnd = Length;
	UAnimNotifyState* BlockTransition = UAnimationBlueprintLibrary::AddAnimationNotifyStateEvent(
		&Sequence, PoseSearchControlTrackName, BlockStart, BlockEnd - BlockStart,
		UAnimNotifyState_PoseSearchBlockTransition::StaticClass());
	UAnimNotifyState_PoseSearchOverrideContinuingPoseCostBias* ContinuingBias =
		Cast<UAnimNotifyState_PoseSearchOverrideContinuingPoseCostBias>(
			UAnimationBlueprintLibrary::AddAnimationNotifyStateEvent(&Sequence,
				PoseSearchControlTrackName, 0.0f, Length,
				UAnimNotifyState_PoseSearchOverrideContinuingPoseCostBias::StaticClass()));
	if (ContinuingBias)
	{
		ContinuingBias->CostAddend = ContinuingPoseCostBias;
	}

	Sequence.SortNotifies();
	Sequence.RefreshCacheData();
	return BlockTransition != nullptr && ContinuingBias != nullptr;
}

bool ValidatePoseSearchControlNotifies(const UAnimSequence& Sequence)
{
	int32 BlockTransitionCount = 0;
	int32 ContinuingBiasCount = 0;
	for (const FAnimNotifyEvent& Notify : Sequence.Notifies)
	{
		const FName TrackName = Sequence.AnimNotifyTracks.IsValidIndex(Notify.TrackIndex)
			? Sequence.AnimNotifyTracks[Notify.TrackIndex].TrackName : NAME_None;
		if (Cast<UAnimNotifyState_PoseSearchBlockTransition>(Notify.NotifyStateClass))
		{
			++BlockTransitionCount;
			if (TrackName != PoseSearchControlTrackName
				|| !FMath::IsNearlyEqual(Notify.GetTriggerTime(), ExitEntryOpenSeconds, 0.01f)
				|| !FMath::IsNearlyEqual(Notify.GetEndTriggerTime(),
					Sequence.GetPlayLength(), 0.01f))
			{
				return false;
			}
		}
		else if (const UAnimNotifyState_PoseSearchOverrideContinuingPoseCostBias* ContinuingBias =
			Cast<UAnimNotifyState_PoseSearchOverrideContinuingPoseCostBias>(Notify.NotifyStateClass))
		{
			++ContinuingBiasCount;
			if (TrackName != PoseSearchControlTrackName
				|| !FMath::IsNearlyZero(Notify.GetTriggerTime(), 0.01f)
				|| !FMath::IsNearlyEqual(Notify.GetEndTriggerTime(), Sequence.GetPlayLength(), 0.01f)
				|| !FMath::IsNearlyEqual(ContinuingBias->CostAddend, ContinuingPoseCostBias, 0.01f))
			{
				return false;
			}
		}
	}
	return BlockTransitionCount == 1 && ContinuingBiasCount == 1;
}

bool ConfigureSequence(UAnimSequence& Sequence)
{
	const bool bHasUnexpectedNotifies = Sequence.Notifies.ContainsByPredicate(
		[](const FAnimNotifyEvent& Notify)
		{
			return !IsExitPoseSearchControlNotify(Notify);
		});
	if (Sequence.bLoop || !Sequence.bEnableRootMotion || Sequence.bForceRootLock
		|| bHasUnexpectedNotifies || Sequence.GetPlayLength() <= KINDA_SMALL_NUMBER
		|| !FMath::IsNearlyEqual(Sequence.RateScale, 1.0f))
	{
		UE_LOG(LogTemp, Error, TEXT("[E4ExitSetup] Sequence contract failed: %s"), *Sequence.GetPathName());
		return false;
	}

	Sequence.Modify();
	for (const FName CurveName : GetNeutralCurveNames())
	{
		if (Sequence.HasCurveData(CurveName, true))
		{
			UAnimationBlueprintLibrary::RemoveCurve(&Sequence, CurveName);
		}
		UAnimationBlueprintLibrary::AddCurve(&Sequence, CurveName, ERawCurveTrackTypes::RCT_Float);
		UAnimationBlueprintLibrary::AddFloatCurveKeys(&Sequence, CurveName,
			{0.0f, Sequence.GetPlayLength()}, {0.0f, 0.0f});
	}
	if (!ConfigurePoseSearchControlNotifies(Sequence))
	{
		return false;
	}
	Sequence.PostEditChange();
	Sequence.RefreshCacheData();

	for (const FName CurveName : GetNeutralCurveNames())
	{
		if (!Sequence.HasCurveData(CurveName, true))
		{
			return false;
		}
	}
	return SaveAsset(Sequence);
}

bool ConfigureDatabase(UPoseSearchDatabase& Database, UPoseSearchSchema& Schema,
	UAnimSequence& Sequence)
{
	Database.Modify();
	Database.Schema = &Schema;
	Database.ContinuingPoseCostBias = ContinuingPoseCostBias;
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
	const EAsyncBuildIndexResult Result =
		FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(&Database,
			ERequestAsyncBuildFlag::NewRequest | ERequestAsyncBuildFlag::WaitForCompletion);
	if (Result != EAsyncBuildIndexResult::Success || Database.GetSearchIndex().GetNumPoses() <= 0)
	{
		UE_LOG(LogTemp, Error, TEXT("[E4ExitSetup] Index build failed for %s (%d)."),
			*Database.GetPathName(), static_cast<int32>(Result));
		return false;
	}
	return true;
}

bool ValidateDatabase(const UPoseSearchSchema& Schema, const UAnimSequence& Sequence,
	const UPoseSearchDatabase& Database)
{
	if (Database.Schema != &Schema || Database.GetNumAnimationAssets() != 1)
	{
		return false;
	}

	const FPoseSearchDatabaseSequence* Entry =
		Database.GetDatabaseAnimationAsset<FPoseSearchDatabaseSequence>(0);
	if (!Entry || Entry->Sequence != &Sequence || !Entry->bDisableReselection
		|| Entry->MirrorOption != EPoseSearchMirrorOption::UnmirroredOnly
		|| !FMath::IsNearlyZero(Entry->SamplingRange.Min)
		|| !FMath::IsNearlyZero(Entry->SamplingRange.Max)
		|| !ValidatePoseSearchControlNotifies(Sequence))
	{
		return false;
	}

	for (const FName CurveName : GetNeutralCurveNames())
	{
		if (!Sequence.HasCurveData(CurveName, true))
		{
			return false;
		}
	}
	return true;
}

bool WriteAudit(const TArray<FString>& Rows)
{
	const FString OutputDirectory = FPaths::ProjectSavedDir() / AuditDirectoryName;
	IFileManager::Get().MakeDirectory(*OutputDirectory, true);
	const FString OutputPath = OutputDirectory / TEXT("E4_2_ExitAssetSetup.md");
	TArray<FString> Lines;
	Lines.Add(TEXT("# E4.2 ExitTransition Asset Setup"));
	Lines.Add(TEXT(""));
	Lines.Add(TEXT("This report covers only the three generated ExitTransition sequences and their isolated Pose Search databases."));
	Lines.Add(TEXT("Existing PSD_MH_Shth_Move and PSD_MH_UnSh_Move are intentionally not changed."));
	Lines.Add(TEXT(""));
	Lines.Add(TEXT("| Handoff route | Sequence | Actual duration | Exit PSD | Curves | Index |"));
	Lines.Add(TEXT("|---|---|---:|---|---|---|"));
	Lines.Append(Rows);
	Lines.Add(TEXT(""));
	Lines.Add(TEXT("All four scalar curves are neutral (0): MM_Intent, MM_DistanceToStop, MM_StopGait, MM_MoveGait."));
	Lines.Add(TEXT("Each sequence is fully indexed in one Exit PSD. PoseSearchBlockTransition starts at 1/60s and runs through the end, so a fresh search can only enter at the authored beginning while the same selected PSD can continue through the blocked body. The runtime exposes the ordinary Move PSD in the final safety window, where it competes only against the continuing Exit pose."));
	return FFileHelper::SaveStringToFile(FString::Join(Lines, TEXT("\n")), *OutputPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
}
#endif // WITH_EDITOR

UMHGZE4ExitAssetSetupCommandlet::UMHGZE4ExitAssetSetupCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMHGZE4ExitAssetSetupCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	using namespace UE::MHGZ::E4ExitAssetSetup;
	const bool bReplaceGenerated = FParse::Param(*Params, TEXT("ReplaceGenerated"));
	UPoseSearchSchema* Schema = LoadObject<UPoseSearchSchema>(nullptr, SchemaPath);
	if (!Schema)
	{
		UE_LOG(LogTemp, Error, TEXT("[E4ExitSetup] Could not load PSS_MH_Move."));
		return 1;
	}

	bool bSucceeded = true;
	TArray<FString> Rows;
	for (const FExitRoute& Route : Routes)
	{
		UAnimSequence* Sequence = LoadObject<UAnimSequence>(nullptr, Route.SequencePath);
		const FString PackageName = FString::Printf(TEXT("%s/%s"),
			Route.DatabasePackagePath, Route.DatabaseName);
		const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PackageName, Route.DatabaseName);
		UPoseSearchDatabase* Database = LoadObject<UPoseSearchDatabase>(nullptr, *ObjectPath);
		if (Database && !bReplaceGenerated)
		{
			UE_LOG(LogTemp, Error, TEXT("[E4ExitSetup] %s already exists; rerun with -ReplaceGenerated to rebuild it."),
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

		const bool bConfigured = Sequence && Database && ConfigureSequence(*Sequence)
			&& ConfigureDatabase(*Database, *Schema, *Sequence) && RebuildDatabaseIndex(*Database)
			&& ValidateDatabase(*Schema, *Sequence, *Database);
		if (!bConfigured)
		{
			UE_LOG(LogTemp, Error, TEXT("[E4ExitSetup] Failed to configure %s."), Route.Label);
			bSucceeded = false;
			continue;
		}

		UE_LOG(LogTemp, Display, TEXT("[E4ExitSetup] Validated %s -> %s."),
			Route.Label, *Database->GetPathName());
		Rows.Add(FString::Printf(TEXT("| %s | %s | %.4fs | %s | neutral x4 | %d poses |"),
			Route.Label, *Sequence->GetPathName(), Sequence->GetPlayLength(),
			*Database->GetPathName(), Database->GetSearchIndex().GetNumPoses()));
	}

	if (!WriteAudit(Rows))
	{
		UE_LOG(LogTemp, Error, TEXT("[E4ExitSetup] Failed to write audit report."));
		bSucceeded = false;
	}
	UE_LOG(LogTemp, Display, TEXT("[E4ExitSetup] Completed %d/%d routes."),
		Rows.Num(), UE_ARRAY_COUNT(Routes));
	return bSucceeded ? 0 : 1;
#else
	UE_LOG(LogTemp, Error, TEXT("[E4ExitSetup] This commandlet requires an editor build."));
	return 1;
#endif
}
