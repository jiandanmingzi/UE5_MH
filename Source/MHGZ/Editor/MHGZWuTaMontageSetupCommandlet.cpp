// Copyright MHGZ Project. All Rights Reserved.

#include "Editor/MHGZWuTaMontageSetupCommandlet.h"

#include "Animation/AnimCompositeBase.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "InsectGlaive/InsectGlaiveCombatConfig.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace UE::MHGZ::WuTaMontage
{
const TCHAR* SequencePath =
	TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_WuTa.AS_Unsh_WuTa");
const TCHAR* MontageDirectory = TEXT("/Game/Weapons/InsectGlaive/Anims/Montage");
const TCHAR* MontageName = TEXT("AM_IG_WuTa");
const TCHAR* SlotName = TEXT("DefaultSlot");

/** Blend in/out baked into the asset; mirrors DanceVaultBlendInTime/BlendOutTime defaults. */
constexpr float BlendInTime = 0.0f;
constexpr float BlendOutTime = 0.05f;

/**
 * Same contract as UMHGZBackVaultAbility's RequiredSegmentPlayRate: the segment
 * rate that makes `DesiredDuration` seconds of wall clock consume the whole clip.
 */
float RequiredSegmentPlayRate(UAnimSequenceBase& Sequence, const float DesiredDuration)
{
	if (!FMath::IsFinite(DesiredDuration) || DesiredDuration <= KINDA_SMALL_NUMBER
		|| !FMath::IsFinite(Sequence.RateScale)
		|| FMath::IsNearlyZero(Sequence.RateScale))
	{
		return 0.0f;
	}
	return Sequence.GetPlayLength() / (DesiredDuration * FMath::Abs(Sequence.RateScale));
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

FString JsonEscape(const FString& Value)
{
	return Value.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\""));
}
}

int32 UMHGZWuTaMontageSetupCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	using namespace UE::MHGZ::WuTaMontage;
	(void)Params;

	UAnimSequenceBase* Sequence =
		LoadObject<UAnimSequenceBase>(nullptr, SequencePath);
	if (!Sequence || !Sequence->GetSkeleton())
	{
		UE_LOG(LogTemp, Error, TEXT("[WuTaMontage] Missing sequence or skeleton: %s"), SequencePath);
		return 1;
	}

	// The desired wall-clock length is the config's, not the clip's -- that is the
	// whole point of baking a rate. Read the CDO; DA_IG_Combat overrides are
	// picked up by the ability at runtime and can be re-baked by editing the
	// asset's segment rate directly.
	const UInsectGlaiveCombatConfig* Config =
		GetDefault<UInsectGlaiveCombatConfig>();
	const float DesiredDuration = Config ? Config->DanceVaultDuration : 0.0f;
	const float PlayRate = RequiredSegmentPlayRate(*Sequence, DesiredDuration);
	if (PlayRate <= 0.0f)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WuTaMontage] Cannot derive a play rate from DanceVaultDuration=%.4f."),
			DesiredDuration);
		return 1;
	}

	const FString PackageName = FString::Printf(TEXT("%s/%s"), MontageDirectory, MontageName);
	const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PackageName, MontageName);
	UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *ObjectPath);
	if (!Montage)
	{
		UPackage* Package = CreatePackage(*PackageName);
		Montage = NewObject<UAnimMontage>(Package, FName(MontageName), RF_Public | RF_Standalone);
	}
	if (!Montage)
	{
		UE_LOG(LogTemp, Error, TEXT("[WuTaMontage] Could not create %s."), *ObjectPath);
		return 1;
	}

	Montage->Modify();
	Montage->SetSkeleton(Sequence->GetSkeleton());
	Montage->BlendIn.SetBlendTime(BlendInTime);
	Montage->BlendOut.SetBlendTime(BlendOutTime);

	// Rebuilt from scratch so re-running is idempotent rather than additive.
	Montage->SlotAnimTracks.Reset();
	Montage->CompositeSections.Reset();

	FSlotAnimationTrack& SlotTrack = Montage->SlotAnimTracks.AddDefaulted_GetRef();
	SlotTrack.SlotName = FName(SlotName);
	FAnimSegment& Segment = SlotTrack.AnimTrack.AnimSegments.AddDefaulted_GetRef();
	Segment.SetAnimReference(Sequence, true);
	Segment.StartPos = 0.0f;
	Segment.AnimStartTime = 0.0f;
	Segment.AnimEndTime = Sequence->GetPlayLength();
	Segment.AnimPlayRate = PlayRate;
	Segment.LoopingCount = 1;

	FCompositeSection& Section = Montage->CompositeSections.AddDefaulted_GetRef();
	Section.SectionName = FName(TEXT("Default"));
	Section.Link(Sequence, Sequence->GetPlayLength());
	Section.SetTime(0.0f);

	Montage->UpdateLinkableElements();
	Montage->RefreshCacheData();

	const bool bSaved = SaveAsset(*Montage);
	const float EffectiveDuration = Sequence->GetPlayLength()
		/ (PlayRate * FMath::Abs(Sequence->RateScale));

	// The pythonscript commandlets swallow every print, so persist the numbers.
	const FString Report = FString::Printf(
		TEXT("{\n")
		TEXT("  \"montage\": \"%s\",\n")
		TEXT("  \"sequence\": \"%s\",\n")
		TEXT("  \"clip_play_length\": %.4f,\n")
		TEXT("  \"clip_rate_scale\": %.4f,\n")
		TEXT("  \"dance_vault_duration\": %.4f,\n")
		TEXT("  \"baked_segment_play_rate\": %.4f,\n")
		TEXT("  \"resulting_effective_duration\": %.4f,\n")
		TEXT("  \"sections\": %d,\n")
		TEXT("  \"segments\": %d,\n")
		TEXT("  \"saved\": %s\n")
		TEXT("}\n"),
		*JsonEscape(ObjectPath), *JsonEscape(Sequence->GetPathName()),
		Sequence->GetPlayLength(), Sequence->RateScale, DesiredDuration, PlayRate,
		EffectiveDuration, Montage->CompositeSections.Num(),
		Montage->SlotAnimTracks.Num() > 0 ? Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num() : 0,
		bSaved ? TEXT("true") : TEXT("false"));

	const FString ReportPath = FPaths::ProjectSavedDir() / TEXT("_wuta_montage.json");
	FFileHelper::SaveStringToFile(Report, *ReportPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

	UE_LOG(LogTemp, Display,
		TEXT("[WuTaMontage] rate=%.4f effective=%.4f saved=%s"),
		PlayRate, EffectiveDuration, bSaved ? TEXT("true") : TEXT("false"));
	return bSaved ? 0 : 1;
#else
	UE_LOG(LogTemp, Error, TEXT("[WuTaMontage] This commandlet requires an editor build."));
	return 1;
#endif
}

UMHGZWuTaMontageSetupCommandlet::UMHGZWuTaMontageSetupCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}
