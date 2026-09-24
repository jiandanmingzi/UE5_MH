// Copyright MHGZ Project. All Rights Reserved.

#include "Editor/MHGZPMMNotifyTrackRepairCommandlet.h"

#if WITH_EDITOR

#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceBase.h"
#include "AnimationBlueprintLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "PoseSearch/PoseSearchAnimNotifies.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
const FName ControlTrackName(TEXT("PoseSearchControl"));

bool IsPoseSearchControlNotify(const FAnimNotifyEvent& Notify)
{
	return Cast<UAnimNotifyState_PoseSearchBlockTransition>(Notify.NotifyStateClass) != nullptr
		|| Cast<UAnimNotifyState_PoseSearchOverrideContinuingPoseCostBias>(Notify.NotifyStateClass) != nullptr;
}

bool SaveSequence(UAnimSequenceBase& Sequence)
{
	UPackage* Package = Sequence.GetOutermost();
	FString Filename;
	if (!FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), Filename,
		FPackageName::GetAssetPackageExtension()))
	{
		return false;
	}

	Package->MarkPackageDirty();
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	return UPackage::SavePackage(Package, &Sequence, *Filename, SaveArgs);
}

/** True when the track carries nothing but the offending Pose Search notifies. */
bool TrackHasOnlyPoseSearchNotifies(const UAnimSequenceBase& Sequence, const int32 TrackIndex)
{
	for (const FAnimNotifyEvent& Notify : Sequence.Notifies)
	{
		if (Notify.TrackIndex == TrackIndex && !IsPoseSearchControlNotify(Notify))
		{
			return false;
		}
	}
	return true;
}

bool TrackHasAnyNotify(const UAnimSequenceBase& Sequence, const int32 TrackIndex)
{
	for (const FAnimNotifyEvent& Notify : Sequence.Notifies)
	{
		if (Notify.TrackIndex == TrackIndex)
		{
			return true;
		}
	}
	return false;
}
}

#endif

UMHGZPMMNotifyTrackRepairCommandlet::UMHGZPMMNotifyTrackRepairCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMHGZPMMNotifyTrackRepairCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	const bool bApply = FParse::Param(*Params, TEXT("Apply"));
	UE_LOG(LogTemp, Display, TEXT("[PMMNotifyRepair] Start (Apply=%d). Track convention: %s"),
		bApply ? 1 : 0, *ControlTrackName.ToString());

	const FAssetRegistryModule& RegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	FARFilter Filter;
	Filter.ClassPaths.Add(UAnimSequence::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	Filter.PackagePaths.Add(TEXT("/Game"));
	Filter.bRecursivePaths = true;

	TArray<FAssetData> Assets;
	RegistryModule.Get().GetAssets(Filter, Assets);

	int32 InspectedCount = 0;
	int32 MismatchCount = 0;
	int32 RenamedTrackCount = 0;
	int32 RepointedNotifyCount = 0;
	int32 RemovedTrackCount = 0;
	int32 FailureCount = 0;

	for (const FAssetData& AssetData : Assets)
	{
		UAnimSequenceBase* Sequence = Cast<UAnimSequenceBase>(AssetData.GetAsset());
		if (!Sequence)
		{
			continue;
		}
		++InspectedCount;

		TArray<int32> OffendingTrackIndices;
		for (const FAnimNotifyEvent& Notify : Sequence->Notifies)
		{
			if (!IsPoseSearchControlNotify(Notify))
			{
				continue;
			}
			if (!Sequence->AnimNotifyTracks.IsValidIndex(Notify.TrackIndex))
			{
				UE_LOG(LogTemp, Error, TEXT("[PMMNotifyRepair] %s has a Pose Search notify with invalid TrackIndex %d"),
					*Sequence->GetPathName(), Notify.TrackIndex);
				++FailureCount;
				continue;
			}
			if (Sequence->AnimNotifyTracks[Notify.TrackIndex].TrackName != ControlTrackName)
			{
				OffendingTrackIndices.AddUnique(Notify.TrackIndex);
			}
		}
		if (OffendingTrackIndices.IsEmpty())
		{
			continue;
		}

		++MismatchCount;
		const FString OffendingTracks = FString::JoinBy(OffendingTrackIndices, TEXT(","),
			[Sequence](const int32 Index)
			{
				return Sequence->AnimNotifyTracks[Index].TrackName.ToString();
			});
		UE_LOG(LogTemp, Display, TEXT("[PMMNotifyRepair] %s: %d Pose Search notify track(s) named [%s] instead of %s"),
			*Sequence->GetPathName(), OffendingTrackIndices.Num(), *OffendingTracks, *ControlTrackName.ToString());
		if (!bApply)
		{
			continue;
		}

		int32 ControlIndex = Sequence->AnimNotifyTracks.IndexOfByPredicate(
			[](const FAnimNotifyTrack& Track) { return Track.TrackName == ControlTrackName; });
		TArray<int32> EmptiedTrackIndices;
		for (const int32 TrackIndex : OffendingTrackIndices)
		{
			if (ControlIndex == INDEX_NONE && TrackHasOnlyPoseSearchNotifies(*Sequence, TrackIndex))
			{
				// The lane exists only for these notifies: rename it in place so any
				// other lane keeps its own name and no track is duplicated.
				Sequence->AnimNotifyTracks[TrackIndex].TrackName = ControlTrackName;
				ControlIndex = TrackIndex;
				++RenamedTrackCount;
				continue;
			}

			if (ControlIndex == INDEX_NONE)
			{
				UAnimationBlueprintLibrary::AddAnimationNotifyTrack(Sequence, ControlTrackName,
					FLinearColor::White);
				ControlIndex = Sequence->AnimNotifyTracks.Num() - 1;
			}
			for (FAnimNotifyEvent& Notify : Sequence->Notifies)
			{
				if (Notify.TrackIndex == TrackIndex && IsPoseSearchControlNotify(Notify))
				{
					Notify.TrackIndex = ControlIndex;
					++RepointedNotifyCount;
				}
			}
			if (!TrackHasAnyNotify(*Sequence, TrackIndex))
			{
				EmptiedTrackIndices.Add(TrackIndex);
			}
		}

		// Drop the lanes we just emptied (highest index first so the remaining
		// indices stay valid), otherwise every repaired asset keeps a stray empty
		// lane named after the notify class.
		EmptiedTrackIndices.Sort([](const int32 Left, const int32 Right) { return Left > Right; });
		for (const int32 TrackIndex : EmptiedTrackIndices)
		{
			Sequence->AnimNotifyTracks.RemoveAt(TrackIndex);
			if (TrackIndex < ControlIndex)
			{
				--ControlIndex;
			}
			for (FAnimNotifyEvent& Notify : Sequence->Notifies)
			{
				if (Notify.TrackIndex > TrackIndex)
				{
					--Notify.TrackIndex;
				}
			}
			++RemovedTrackCount;
		}

		// The notifies themselves are untouched, so the cache only needs rebuilding
		// because TrackIndex/AnimNotifyTracks changed.
		Sequence->RefreshCacheData();
		if (!SaveSequence(*Sequence))
		{
			UE_LOG(LogTemp, Error, TEXT("[PMMNotifyRepair] Failed to save %s"), *Sequence->GetPathName());
			++FailureCount;
			continue;
		}
		UE_LOG(LogTemp, Display, TEXT("[PMMNotifyRepair] Saved %s"), *Sequence->GetPathName());
	}

	UE_LOG(LogTemp, Display,
		TEXT("[PMMNotifyRepair] Done. inspected=%d mismatched=%d renamedTracks=%d repointedNotifies=%d removedTracks=%d failures=%d (Apply=%d)"),
		InspectedCount, MismatchCount, RenamedTrackCount, RepointedNotifyCount, RemovedTrackCount,
		FailureCount, bApply ? 1 : 0);
	return FailureCount == 0 ? 0 : 1;
#else
	UE_LOG(LogTemp, Error, TEXT("[PMMNotifyRepair] This commandlet requires an editor build."));
	return 1;
#endif
}
