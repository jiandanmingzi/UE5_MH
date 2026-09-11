// Copyright MHGZ Project. All Rights Reserved.

#include "Editor/MHGZDodgeBlendInSetupCommandlet.h"

#if WITH_EDITOR

#include "Animation/AnimCompositeBase.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace UE::MHGZ::DodgeBlendInSetup
{
constexpr int32 EntryHoldFrameCount = 3;
constexpr float MontageFps = 60.0f;
constexpr float EntryHoldDuration = static_cast<float>(EntryHoldFrameCount) / MontageFps;
constexpr float TimeTolerance = 0.002f;
const FName DodgeEntrySectionName(TEXT("DodgeEntry"));
const FName DodgeCoreSectionName(TEXT("DodgeCore"));

struct FDodgeMontageRoute
{
	const TCHAR* Label;
	const TCHAR* MontagePath;
	const TCHAR* GeneratedDirectory;
	const TCHAR* GeneratedNamePrefix;
};

static const FDodgeMontageRoute Routes[] =
{
	{
		TEXT("Sheathed"),
		TEXT("/Game/Characters/Demo/Anims/Montage/AM_Shth_Dodge.AM_Shth_Dodge"),
		TEXT("/Game/Characters/Demo/Anims/Sequences/Generated"),
		TEXT("AS_Shth_Dodge_EntryHold")
	},
	{
		TEXT("Forward"),
		TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_Dodge_Forward.AM_IG_Dodge_Forward"),
		TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Generated"),
		TEXT("AS_IG_Dodge_Forward_EntryHold")
	},
	{
		TEXT("Back"),
		TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_Dodge_Back.AM_IG_Dodge_Back"),
		TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Generated"),
		TEXT("AS_IG_Dodge_Back_EntryHold")
	},
	{
		TEXT("Left"),
		TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_Dodge_Left.AM_IG_Dodge_Left"),
		TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Generated"),
		TEXT("AS_IG_Dodge_Left_EntryHold")
	},
	{
		TEXT("Right"),
		TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_Dodge_Right.AM_IG_Dodge_Right"),
		TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Generated"),
		TEXT("AS_IG_Dodge_Right_EntryHold")
	}
};

struct FExistingSection
{
	FName Name;
	float Time = 0.0f;
};

struct FExistingNotify
{
	int32 Index = INDEX_NONE;
	float Time = 0.0f;
	float Duration = 0.0f;
	int32 SlotIndex = 0;
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

bool BuildEntryHold(UAnimSequence& Source, const float SourceTime,
	const FString& PackageName, const FName ObjectName, UAnimSequence*& OutHold)
{
	OutHold = nullptr;
	const IAnimationDataModel* SourceModel = Source.GetDataModel();
	if (!SourceModel || !Source.GetSkeleton())
	{
		return false;
	}
	const FFrameRate FrameRate = SourceModel->GetFrameRate();
	if (!FMath::IsNearlyEqual(static_cast<float>(FrameRate.AsDecimal()), MontageFps, TimeTolerance))
	{
		UE_LOG(LogTemp, Error, TEXT("[DodgeBlendIn] %s is %.3ffps; expected 60fps for a three-frame entry hold."),
			*Source.GetPathName(), FrameRate.AsDecimal());
		return false;
	}

	const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PackageName, *ObjectName.ToString());
	UAnimSequence* Hold = LoadObject<UAnimSequence>(nullptr, *ObjectPath);
	if (!Hold)
	{
		UPackage* Package = CreatePackage(*PackageName);
		Hold = NewObject<UAnimSequence>(Package, ObjectName, RF_Public | RF_Standalone);
	}
	if (!Hold)
	{
		return false;
	}

	TArray<FName> TrackNames;
	SourceModel->GetBoneTrackNames(TrackNames);
	if (TrackNames.IsEmpty())
	{
		return false;
	}

	Hold->Modify();
	Hold->SetSkeleton(Source.GetSkeleton());
	Hold->bLoop = false;
	Hold->RootMotionRootLock = Source.RootMotionRootLock;
	Hold->RateScale = 1.0f;
	Hold->bUseNormalizedRootMotionScale = Source.bUseNormalizedRootMotionScale;

	IAnimationDataController& Controller = Hold->GetController();
	Controller.InitializeModel();
	Hold->ResetAnimation();
	Controller.OpenBracket(FText::FromString(TEXT("Build Dodge Entry Hold")), false);
	Controller.SetFrameRate(FrameRate, false);
	Controller.SetNumberOfFrames(FFrameNumber(EntryHoldFrameCount), false);

	const FFrameTime SourceFrame = FrameRate.AsFrameTime(SourceTime);
	bool bBuiltEveryTrack = true;
	for (const FName TrackName : TrackNames)
	{
		const FTransform Pose = SourceModel->EvaluateBoneTrackTransform(
			TrackName, SourceFrame, Source.Interpolation);
		TArray<FVector3f> Positions;
		TArray<FQuat4f> Rotations;
		TArray<FVector3f> Scales;
		Positions.Init(FVector3f(Pose.GetTranslation()), EntryHoldFrameCount + 1);
		Rotations.Init(FQuat4f(Pose.GetRotation()), EntryHoldFrameCount + 1);
		Scales.Init(FVector3f(Pose.GetScale3D()), EntryHoldFrameCount + 1);
		if (!Controller.AddBoneCurve(TrackName, false)
			|| !Controller.SetBoneTrackKeys(TrackName, Positions, Rotations, Scales, false))
		{
			bBuiltEveryTrack = false;
			break;
		}
	}
	if (bBuiltEveryTrack)
	{
		Controller.NotifyPopulated();
	}
	Controller.CloseBracket(false);
	if (!bBuiltEveryTrack)
	{
		return false;
	}

	// Keep the generated sequence in the same root-motion contract as the source.
	// Every key is the same source pose, so this entry segment extracts no delta.
	Hold->bEnableRootMotion = Source.bEnableRootMotion;
	Hold->RefreshCacheData();
	Hold->PostEditChange();
	if (!FMath::IsNearlyEqual(Hold->GetPlayLength(), EntryHoldDuration, TimeTolerance)
		|| !SaveAsset(*Hold))
	{
		return false;
	}
	OutHold = Hold;
	return true;
}

bool PreflightRoute(const FDodgeMontageRoute& Route, UAnimMontage& Montage)
{
	if (!Montage.IsValidSectionName(DodgeCoreSectionName) || Montage.SlotAnimTracks.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("[DodgeBlendIn] %s has no DodgeCore or no Slot tracks."), *Montage.GetPathName());
		return false;
	}
	for (const FSlotAnimationTrack& SlotTrack : Montage.SlotAnimTracks)
	{
		if (SlotTrack.AnimTrack.AnimSegments.IsEmpty()
			|| !Cast<UAnimSequence>(SlotTrack.AnimTrack.AnimSegments[0].GetAnimReference()))
		{
			UE_LOG(LogTemp, Error, TEXT("[DodgeBlendIn] %s has no direct UAnimSequence at the beginning of slot %s."),
				*Montage.GetPathName(), *SlotTrack.SlotName.ToString());
			return false;
		}
	}
	return true;
}

bool ValidateEntryLayout(const UAnimMontage& Montage)
{
	const int32 EntryIndex = Montage.GetSectionIndex(DodgeEntrySectionName);
	const int32 CoreIndex = Montage.GetSectionIndex(DodgeCoreSectionName);
	if (EntryIndex != 0 || CoreIndex == INDEX_NONE
		|| !FMath::IsNearlyEqual(Montage.GetAnimCompositeSection(EntryIndex).GetTime(), 0.0f, TimeTolerance)
		|| Montage.GetAnimCompositeSection(EntryIndex).NextSectionName != DodgeCoreSectionName
		|| !FMath::IsNearlyEqual(Montage.GetAnimCompositeSection(CoreIndex).GetTime(), EntryHoldDuration, TimeTolerance))
	{
		return false;
	}
	for (int32 SectionIndex = 1; SectionIndex < Montage.CompositeSections.Num(); ++SectionIndex)
	{
		if (Montage.GetAnimCompositeSection(SectionIndex).NextSectionName != NAME_None)
		{
			return false;
		}
	}
	return true;
}

bool RepairExistingEntryLayout(const FDodgeMontageRoute& Route, UAnimMontage& Montage,
	TArray<FString>& AuditRows)
{
	if (!Montage.IsValidSectionName(DodgeEntrySectionName) || !PreflightRoute(Route, Montage))
	{
		return false;
	}

	Montage.Modify();
	for (FCompositeSection& Section : Montage.CompositeSections)
	{
		if (Section.SectionName == DodgeEntrySectionName)
		{
			Section.Link(&Montage, 0.0f, 0);
			Section.NextSectionName = DodgeCoreSectionName;
		}
		else
		{
			// GA_Dodge chooses DodgeCore -> IdleExit at runtime. The asset itself
			// must not route its terminal sections back through the new entry.
			Section.NextSectionName = NAME_None;
		}
	}
	Montage.CompositeSections.Sort([](const FCompositeSection& Left, const FCompositeSection& Right)
	{
		return Left.GetTime() < Right.GetTime();
	});
	Montage.UpdateLinkableElements();
	Montage.RefreshCacheData();
	Montage.PostEditChange();

	if (!ValidateEntryLayout(Montage) || !SaveAsset(Montage))
	{
		UE_LOG(LogTemp, Error, TEXT("[DodgeBlendIn] Existing entry layout repair failed for %s."),
			*Montage.GetPathName());
		return false;
	}
	AuditRows.Add(FString::Printf(TEXT("| %s | %s | %d | %.4f | repaired |"), Route.Label,
		*Montage.GetPathName(), Montage.SlotAnimTracks.Num(), EntryHoldDuration));
	return true;
}

bool ConfigureRoute(const FDodgeMontageRoute& Route, UAnimMontage& Montage,
	TArray<FString>& AuditRows)
{
	if (Montage.IsValidSectionName(DodgeEntrySectionName))
	{
		return RepairExistingEntryLayout(Route, Montage, AuditRows);
	}
	if (!PreflightRoute(Route, Montage))
	{
		return false;
	}

	TArray<FExistingSection> ExistingSections;
	ExistingSections.Reserve(Montage.CompositeSections.Num());
	for (const FCompositeSection& Section : Montage.CompositeSections)
	{
		ExistingSections.Add({Section.SectionName, Section.GetTime()});
	}
	TArray<FExistingNotify> ExistingNotifies;
	ExistingNotifies.Reserve(Montage.Notifies.Num());
	for (int32 NotifyIndex = 0; NotifyIndex < Montage.Notifies.Num(); ++NotifyIndex)
	{
		const FAnimNotifyEvent& Event = Montage.Notifies[NotifyIndex];
		ExistingNotifies.Add({NotifyIndex, Event.GetTime(), Event.GetDuration(), Event.GetSlotIndex()});
	}

	TArray<UAnimSequence*> Holds;
	Holds.Reserve(Montage.SlotAnimTracks.Num());
	for (int32 SlotIndex = 0; SlotIndex < Montage.SlotAnimTracks.Num(); ++SlotIndex)
	{
		const FAnimSegment& FirstSegment = Montage.SlotAnimTracks[SlotIndex].AnimTrack.AnimSegments[0];
		UAnimSequence* Source = CastChecked<UAnimSequence>(FirstSegment.GetAnimReference());
		const FName HoldName(*FString::Printf(TEXT("%s_Slot%d"), Route.GeneratedNamePrefix, SlotIndex));
		const FString PackageName = FString::Printf(TEXT("%s/%s"), Route.GeneratedDirectory, *HoldName.ToString());
		UAnimSequence* Hold = nullptr;
		if (!BuildEntryHold(*Source, FirstSegment.AnimStartTime, PackageName, HoldName, Hold))
		{
			UE_LOG(LogTemp, Error, TEXT("[DodgeBlendIn] Failed to build %s for %s."),
				*HoldName.ToString(), *Montage.GetPathName());
			return false;
		}
		Holds.Add(Hold);
	}

	Montage.Modify();
	for (int32 SlotIndex = 0; SlotIndex < Montage.SlotAnimTracks.Num(); ++SlotIndex)
	{
		FAnimSegment EntrySegment;
		EntrySegment.SetAnimReference(Holds[SlotIndex], true);
		EntrySegment.StartPos = 0.0f;
		EntrySegment.AnimStartTime = 0.0f;
		EntrySegment.AnimEndTime = Holds[SlotIndex]->GetPlayLength();
		EntrySegment.AnimPlayRate = 1.0f;
		EntrySegment.LoopingCount = 1;
		FAnimTrack& Track = Montage.SlotAnimTracks[SlotIndex].AnimTrack;
		// ValidateSegmentTimes is not exported from Engine in UE 5.6. The
		// existing Montage tracks are already authored contiguous, so preserving
		// each segment's relative start while shifting them is the equivalent
		// operation here.
		for (FAnimSegment& ExistingSegment : Track.AnimSegments)
		{
			ExistingSegment.StartPos += EntryHoldDuration;
		}
		Track.AnimSegments.Insert(EntrySegment, 0);
	}

	for (const FExistingSection& Existing : ExistingSections)
	{
		const int32 SectionIndex = Montage.GetSectionIndex(Existing.Name);
		if (SectionIndex == INDEX_NONE)
		{
			return false;
		}
		Montage.GetAnimCompositeSection(SectionIndex).Link(&Montage,
			Existing.Time + EntryHoldDuration, 0);
	}
	const int32 EntryIndex = Montage.AddAnimCompositeSection(DodgeEntrySectionName, 0.0f);
	if (EntryIndex == INDEX_NONE)
	{
		return false;
	}
	Montage.GetAnimCompositeSection(EntryIndex).NextSectionName = DodgeCoreSectionName;
	for (FCompositeSection& Section : Montage.CompositeSections)
	{
		if (Section.SectionName != DodgeEntrySectionName)
		{
			Section.NextSectionName = NAME_None;
		}
	}
	Montage.CompositeSections.Sort([](const FCompositeSection& Left, const FCompositeSection& Right)
	{
		return Left.GetTime() < Right.GetTime();
	});

	for (const FExistingNotify& Existing : ExistingNotifies)
	{
		FAnimNotifyEvent& Event = Montage.Notifies[Existing.Index];
		const float NewTime = Existing.Time + EntryHoldDuration;
		Event.Link(&Montage, NewTime, Existing.SlotIndex);
		if (Event.NotifyStateClass)
		{
			Event.EndLink.Link(&Montage, NewTime + Existing.Duration, Existing.SlotIndex);
			Event.SetDuration(Existing.Duration);
		}
	}

	Montage.UpdateLinkableElements();
	Montage.RefreshCacheData();
	Montage.PostEditChange();

	bool bValid = ValidateEntryLayout(Montage);
	for (int32 SlotIndex = 0; bValid && SlotIndex < Montage.SlotAnimTracks.Num(); ++SlotIndex)
	{
		const TArray<FAnimSegment>& Segments = Montage.SlotAnimTracks[SlotIndex].AnimTrack.AnimSegments;
		bValid = Segments.Num() >= 2 && Segments[0].GetAnimReference() == Holds[SlotIndex]
			&& FMath::IsNearlyEqual(Segments[0].GetLength(), EntryHoldDuration, TimeTolerance)
			&& FMath::IsNearlyEqual(Segments[1].StartPos, EntryHoldDuration, TimeTolerance);
	}
	for (const FExistingNotify& Existing : ExistingNotifies)
	{
		if (Existing.Index >= Montage.Notifies.Num()
			|| !FMath::IsNearlyEqual(Montage.Notifies[Existing.Index].GetTime(),
				Existing.Time + EntryHoldDuration, TimeTolerance))
		{
			bValid = false;
			break;
		}
	}
	if (!bValid || !SaveAsset(Montage))
	{
		UE_LOG(LogTemp, Error, TEXT("[DodgeBlendIn] Validation or save failed for %s."), *Montage.GetPathName());
		return false;
	}

	AuditRows.Add(FString::Printf(TEXT("| %s | %s | %d | %.4f | %d |"), Route.Label,
		*Montage.GetPathName(), Montage.SlotAnimTracks.Num(), EntryHoldDuration,
		ExistingNotifies.Num()));
	return true;
}

bool WriteAudit(const TArray<FString>& Rows)
{
	const FString OutputDirectory = FPaths::ProjectSavedDir() / TEXT("ActionExitAudit");
	IFileManager::Get().MakeDirectory(*OutputDirectory, true);
	const FString OutputPath = OutputDirectory / TEXT("DodgeBlendInSetup.md");
	TArray<FString> Lines;
	Lines.Add(TEXT("# Dodge entry hold setup"));
	Lines.Add(TEXT(""));
	Lines.Add(TEXT("Each Dodge montage slot begins with three static 60fps frames sampled from that slot's original first segment."));
	Lines.Add(TEXT("Existing sections and Montage-level notifies move forward by 0.0500 seconds; DodgeEntry routes to DodgeCore."));
	Lines.Add(TEXT(""));
	Lines.Add(TEXT("| Route | Montage | Slots | Entry duration | Moved notifies |"));
	Lines.Add(TEXT("|---|---|---:|---:|---:|"));
	Lines.Append(Rows);
	return FFileHelper::SaveStringToFile(FString::Join(Lines, TEXT("\n")), *OutputPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
}
#endif

UMHGZDodgeBlendInSetupCommandlet::UMHGZDodgeBlendInSetupCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMHGZDodgeBlendInSetupCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	using namespace UE::MHGZ::DodgeBlendInSetup;
	(void)Params;
	bool bSucceeded = true;
	TArray<FString> AuditRows;
	for (const FDodgeMontageRoute& Route : Routes)
	{
		UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, Route.MontagePath);
		if (!Montage || !ConfigureRoute(Route, *Montage, AuditRows))
		{
			UE_LOG(LogTemp, Error, TEXT("[DodgeBlendIn] Failed to configure %s."), Route.Label);
			bSucceeded = false;
		}
	}
	if (!WriteAudit(AuditRows))
	{
		UE_LOG(LogTemp, Error, TEXT("[DodgeBlendIn] Failed to write audit."));
		bSucceeded = false;
	}
	UE_LOG(LogTemp, Display, TEXT("[DodgeBlendIn] Completed %d/%d Montages."),
		AuditRows.Num(), UE_ARRAY_COUNT(Routes));
	return bSucceeded ? 0 : 1;
#else
	UE_LOG(LogTemp, Error, TEXT("[DodgeBlendIn] This commandlet requires an editor build."));
	return 1;
#endif
}
