// Copyright MHGZ Project. All Rights Reserved.

#include "Commandlets/MHGZM6RecordedHunterFlightSetupCommandlet.h"

#if WITH_EDITOR

#include "ActionSystem/AnimNotify_IG_AwakenedKinsectEvent.h"
#include "ActionSystem/MHGZAwakenedKinsectAttackAbility.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "AnimationBlueprintLibrary.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/SavePackage.h"

namespace UE::MHGZ::M6RecordedHunterFlightSetup
{
constexpr TCHAR MontagePath[] = TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_JueChongJi.AM_IG_JueChongJi");
constexpr TCHAR Sequence196Path[] = TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_JueChongJi_Pose.AS_Unsh_JueChongJi_Pose");
constexpr TCHAR Sequence199Path[] = TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_JueChongJi_Flying.AS_Unsh_JueChongJi_Flying");
constexpr TCHAR ReportRelativePath[] = TEXT("Saved/M6Audit/m6_hunter_flight_notify_placement.json");
constexpr TCHAR ActionTrackName[] = TEXT("Action");
constexpr TCHAR PreFlightSectionName[] = TEXT("PreFlight196");
constexpr TCHAR HunterFlightSectionName[] = TEXT("HunterFlight199");
constexpr float Recorded196SampleTime = 55.05007227f;
constexpr float Recorded199FirstSampleTime = 55.05886133f;
constexpr float TimeTolerance = 0.0001f;

struct FPlacement
{
	FString Error;
	UAnimMontage* Montage = nullptr;
	UAnimSequence* Sequence196 = nullptr;
	UAnimSequence* Sequence199 = nullptr;
	FAnimSegment* Segment196 = nullptr;
	FAnimSegment* Segment199 = nullptr;
	FCompositeSection* PreFlightSection = nullptr;
	FCompositeSection* HunterFlightSection = nullptr;
	float RecordedDelta = Recorded199FirstSampleTime - Recorded196SampleTime;
	float Current196SourceEnd = 0.0f;
	float Target196SourceEnd = 0.0f;
	float TargetHunterFlightMontageTime = 0.0f;
	float KinsectLaunchNotifyDisplayTime = 0.0f;
	float KinsectLaunchNotifyTriggerTime = 0.0f;
	float HunterFlightNotifyDisplayTime = 0.0f;
	float HunterFlightNotifyTriggerTime = 0.0f;
	float KinsectLaunchToHunterFlightTriggerDelta = 0.0f;
	float OrdinaryKinsectFlightSpeed = 0.0f;
	float OrdinaryKinsectMissDistance = 0.0f;
	float TripleUpKinsectFlightSpeed = 0.0f;
	float TripleUpKinsectMissDistance = 0.0f;
	int32 KinsectLaunchNotifyCount = 0;
	int32 HunterFlightNotifyCount = 0;
	bool bNotifyAlreadyCorrect = false;
	bool bCurrent196SpanIsCanonical = false;
	bool bCurrent196SpanIsTarget = false;
};

void ReadAwakenedEventNotifies(const UAnimMontage& Montage, FPlacement& Placement)
{
	Placement.KinsectLaunchNotifyCount = 0;
	Placement.HunterFlightNotifyCount = 0;
	Placement.KinsectLaunchNotifyDisplayTime = 0.0f;
	Placement.KinsectLaunchNotifyTriggerTime = 0.0f;
	Placement.HunterFlightNotifyDisplayTime = 0.0f;
	Placement.HunterFlightNotifyTriggerTime = 0.0f;
	Placement.KinsectLaunchToHunterFlightTriggerDelta = 0.0f;
	Placement.bNotifyAlreadyCorrect = false;

	for (const FAnimNotifyEvent& Event : Montage.Notifies)
	{
		const UAnimNotify_IG_AwakenedKinsectEvent* Notify =
			Cast<UAnimNotify_IG_AwakenedKinsectEvent>(Event.Notify);
		if (!Notify)
		{
			continue;
		}

		switch (Notify->Event)
		{
		case EAwakenedKinsectAttackEvent::KinsectLaunch:
			++Placement.KinsectLaunchNotifyCount;
			Placement.KinsectLaunchNotifyDisplayTime = Event.GetTime();
			Placement.KinsectLaunchNotifyTriggerTime = Event.GetTriggerTime();
			break;
		case EAwakenedKinsectAttackEvent::HunterFlightStart:
			++Placement.HunterFlightNotifyCount;
			Placement.HunterFlightNotifyDisplayTime = Event.GetTime();
			Placement.HunterFlightNotifyTriggerTime = Event.GetTriggerTime();
			Placement.bNotifyAlreadyCorrect = FMath::IsNearlyEqual(
				Event.GetTime(), Placement.TargetHunterFlightMontageTime, TimeTolerance);
			break;
		default:
			break;
		}
	}

	if (Placement.KinsectLaunchNotifyCount == 1
		&& Placement.HunterFlightNotifyCount == 1)
	{
		Placement.KinsectLaunchToHunterFlightTriggerDelta =
			Placement.HunterFlightNotifyTriggerTime
			- Placement.KinsectLaunchNotifyTriggerTime;
	}
}

bool PopulateKinsectFlightProfile(FPlacement& Placement)
{
	Placement.OrdinaryKinsectFlightSpeed = 0.0f;
	Placement.OrdinaryKinsectMissDistance = 0.0f;
	Placement.TripleUpKinsectFlightSpeed = 0.0f;
	Placement.TripleUpKinsectMissDistance = 0.0f;
	if (Placement.KinsectLaunchNotifyCount != 1
		|| Placement.HunterFlightNotifyCount != 1
		|| !FMath::IsFinite(Placement.KinsectLaunchToHunterFlightTriggerDelta)
		|| Placement.KinsectLaunchToHunterFlightTriggerDelta <= 0.0f)
	{
		return false;
	}

	return UMHGZAwakenedKinsectAttackAbility::BuildAwakenedKinsectFlightProfile(
		Placement.KinsectLaunchToHunterFlightTriggerDelta, false,
		Placement.OrdinaryKinsectMissDistance,
		Placement.OrdinaryKinsectFlightSpeed)
		&& UMHGZAwakenedKinsectAttackAbility::BuildAwakenedKinsectFlightProfile(
			Placement.KinsectLaunchToHunterFlightTriggerDelta, true,
			Placement.TripleUpKinsectMissDistance,
			Placement.TripleUpKinsectFlightSpeed);
}

FCompositeSection* FindSection(UAnimMontage& Montage, const FName Name)
{
	return Montage.CompositeSections.FindByPredicate([Name](const FCompositeSection& Section)
	{
		return Section.SectionName == Name;
	});
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

bool Prepare(FPlacement& Placement, const bool bAllowMissingHunterFlightNotify)
{
	Placement.Montage = LoadObject<UAnimMontage>(nullptr, MontagePath);
	Placement.Sequence196 = LoadObject<UAnimSequence>(nullptr, Sequence196Path);
	Placement.Sequence199 = LoadObject<UAnimSequence>(nullptr, Sequence199Path);
	if (!Placement.Montage || !Placement.Sequence196 || !Placement.Sequence199)
	{
		Placement.Error = TEXT("Could not load the persistent Montage or source sequences 196/199.");
		return false;
	}

	UAnimMontage& Montage = *Placement.Montage;
	if (Montage.SlotAnimTracks.Num() != 1
		|| Montage.SlotAnimTracks[0].AnimTrack.AnimSegments.Num() != 5)
	{
		Placement.Error = TEXT("Montage layout differs from the reviewed five-segment M6 scaffold; preserving it.");
		return false;
	}
	TArray<FAnimSegment>& Segments = Montage.SlotAnimTracks[0].AnimTrack.AnimSegments;
	Placement.Segment196 = &Segments[3];
	Placement.Segment199 = &Segments[4];
	if (Placement.Segment196->GetAnimReference() != Placement.Sequence196
		|| Placement.Segment199->GetAnimReference() != Placement.Sequence199
		|| !FMath::IsNearlyEqual(Placement.Segment196->AnimStartTime, 0.0f)
		|| !FMath::IsNearlyEqual(Placement.Segment196->AnimPlayRate, 1.0f)
		|| !FMath::IsNearlyEqual(Placement.Segment199->AnimPlayRate, 1.0f))
	{
		Placement.Error = TEXT("196/199 source mapping or playback rate is not the reviewed scaffold; preserving it.");
		return false;
	}

	Placement.PreFlightSection = FindSection(Montage, FName(PreFlightSectionName));
	Placement.HunterFlightSection = FindSection(Montage, FName(HunterFlightSectionName));
	if (!Placement.PreFlightSection || !Placement.HunterFlightSection
		|| !FMath::IsNearlyEqual(Placement.PreFlightSection->GetTime(), Placement.Segment196->StartPos, 0.001f))
	{
		Placement.Error = TEXT("PreFlight196/HunterFlight199 sections do not match the expected montage mapping; preserving it.");
		return false;
	}

	const float EffectiveRate196 = Placement.Sequence196->RateScale * Placement.Segment196->AnimPlayRate;
	if (!FMath::IsFinite(EffectiveRate196) || EffectiveRate196 <= KINDA_SMALL_NUMBER)
	{
		Placement.Error = TEXT("Sequence 196 has an invalid effective playback rate.");
		return false;
	}
	Placement.Current196SourceEnd = Placement.Segment196->AnimEndTime;
	Placement.Target196SourceEnd = Placement.Segment196->AnimStartTime
		+ Placement.RecordedDelta * EffectiveRate196;
	if (Placement.Target196SourceEnd > Placement.Sequence196->GetPlayLength())
	{
		Placement.Error = TEXT("Recorded 196 interval exceeds source sequence 196 length.");
		return false;
	}
	Placement.bCurrent196SpanIsCanonical = FMath::IsNearlyEqual(
		Placement.Current196SourceEnd, Placement.Sequence196->GetPlayLength(), TimeTolerance);
	Placement.bCurrent196SpanIsTarget = FMath::IsNearlyEqual(
		Placement.Current196SourceEnd, Placement.Target196SourceEnd, TimeTolerance);
	if (!Placement.bCurrent196SpanIsCanonical && !Placement.bCurrent196SpanIsTarget)
	{
		Placement.Error = TEXT("Sequence 196 has a custom source trim; preserving it rather than overwriting editor work.");
		return false;
	}

	Placement.TargetHunterFlightMontageTime = Placement.Segment196->StartPos + Placement.RecordedDelta;
	const float Current196MontageDuration =
		(Placement.Current196SourceEnd - Placement.Segment196->AnimStartTime) / EffectiveRate196;
	const float CurrentHunterFlightMontageTime =
		Placement.Segment196->StartPos + Current196MontageDuration;
	if (!FMath::IsNearlyEqual(Placement.Segment199->StartPos,
			Placement.HunterFlightSection->GetTime(), TimeTolerance)
		|| !FMath::IsNearlyEqual(Placement.Segment199->StartPos,
			CurrentHunterFlightMontageTime, 0.001f))
	{
		Placement.Error = TEXT("199 segment/section do not follow the current 196 segment duration; preserving the Montage.");
		return false;
	}

	ReadAwakenedEventNotifies(Montage, Placement);
	if (Placement.KinsectLaunchNotifyCount != 1)
	{
		Placement.Error = TEXT("Expected exactly one KinsectLaunch notify; preserving the Montage and reporting its count.");
		return false;
	}
	if (Placement.HunterFlightNotifyCount > 1
		|| (Placement.HunterFlightNotifyCount == 1 && !Placement.bNotifyAlreadyCorrect))
	{
		Placement.Error = TEXT("A HunterFlightStart notify already exists at a different time or is duplicated; preserving it for review.");
		return false;
	}
	if (Placement.HunterFlightNotifyCount == 0 && !bAllowMissingHunterFlightNotify)
	{
		Placement.Error = TEXT("Expected exactly one HunterFlightStart notify; the read-only audit does not repair Montage assets.");
		return false;
	}
	if (Placement.KinsectLaunchNotifyTriggerTime
		>= Placement.TargetHunterFlightMontageTime)
	{
		Placement.Error = TEXT("KinsectLaunch must trigger before the authored HunterFlightStart boundary; preserving the Montage for review.");
		return false;
	}
	if (Placement.HunterFlightNotifyCount == 1
		&& Placement.KinsectLaunchToHunterFlightTriggerDelta <= 0.0f)
	{
		Placement.Error = TEXT("KinsectLaunch must trigger before HunterFlightStart; preserving both authored notifies.");
		return false;
	}
	if (Placement.HunterFlightNotifyCount == 1
		&& !PopulateKinsectFlightProfile(Placement))
	{
		Placement.Error = TEXT("Could not derive both fixed-duration kinsect profiles from the audited Montage interval.");
		return false;
	}
	return true;
}

bool WriteReport(const FPlacement& Placement, const TCHAR* Mode, const bool bSuccess)
{
	const FString OutputPath = FPaths::ProjectDir() / ReportRelativePath;
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutputPath), true);
	const FString Json = FString::Printf(
		TEXT("{\n  \"mode\": \"%s\",\n  \"success\": %s,\n  \"montage\": \"%s\",\n  \"notify_time_basis\": \"authored_montage_seconds\",\n  \"source_196_sample_s\": %.8f,\n  \"source_199_first_sample_s\": %.8f,\n  \"recorded_delta_s\": %.8f,\n  \"preflight_196_start_s\": %.8f,\n  \"source_196_end_s_before\": %.8f,\n  \"source_196_end_s_after\": %.8f,\n  \"kinsect_launch_notify_display_time_s\": %.8f,\n  \"kinsect_launch_notify_trigger_time_s\": %.8f,\n  \"kinsect_launch_notify_count\": %d,\n  \"hunter_flight_start_montage_s\": %.8f,\n  \"hunter_flight_notify_display_time_s\": %.8f,\n  \"hunter_flight_notify_trigger_time_s\": %.8f,\n  \"hunter_flight_notify_count\": %d,\n  \"kinsect_launch_to_hunter_flight_trigger_delta_s\": %.8f,\n  \"ordinary_kinsect_speed_cm_s\": %.3f,\n  \"ordinary_kinsect_miss_distance_cm\": %.3f,\n  \"triple_up_kinsect_speed_cm_s\": %.3f,\n  \"triple_up_kinsect_miss_distance_cm\": %.3f,\n  \"error\": \"%s\"\n}\n"),
		Mode, bSuccess ? TEXT("true") : TEXT("false"),
		Placement.Montage ? *Placement.Montage->GetPathName() : TEXT(""),
		Recorded196SampleTime, Recorded199FirstSampleTime, Placement.RecordedDelta,
		Placement.Segment196 ? Placement.Segment196->StartPos : 0.0f,
		Placement.Current196SourceEnd, Placement.Target196SourceEnd,
		Placement.KinsectLaunchNotifyDisplayTime, Placement.KinsectLaunchNotifyTriggerTime,
		Placement.KinsectLaunchNotifyCount,
		Placement.TargetHunterFlightMontageTime, Placement.HunterFlightNotifyDisplayTime,
		Placement.HunterFlightNotifyTriggerTime, Placement.HunterFlightNotifyCount,
		Placement.KinsectLaunchToHunterFlightTriggerDelta,
		Placement.OrdinaryKinsectFlightSpeed, Placement.OrdinaryKinsectMissDistance,
		Placement.TripleUpKinsectFlightSpeed, Placement.TripleUpKinsectMissDistance,
		*Placement.Error.Replace(TEXT("\""), TEXT("'")));
	return FFileHelper::SaveStringToFile(Json, *OutputPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

bool Apply(FPlacement& Placement)
{
	UAnimMontage& Montage = *Placement.Montage;
	if (Placement.bNotifyAlreadyCorrect && Placement.bCurrent196SpanIsTarget)
	{
		return true;
	}
	if (!Placement.bCurrent196SpanIsCanonical && !Placement.bCurrent196SpanIsTarget)
	{
		return false;
	}
	if (Placement.HunterFlightNotifyCount == 1 && !Placement.bNotifyAlreadyCorrect)
	{
		return false;
	}
	if (!UAnimationBlueprintLibrary::IsValidAnimNotifyTrackName(&Montage, FName(ActionTrackName)))
	{
		Placement.Error = TEXT("The Action notify track is missing; preserving the Montage.");
		return false;
	}

	Montage.Modify();
	Placement.Segment196->AnimEndTime = Placement.Target196SourceEnd;
	Placement.Segment199->StartPos = Placement.TargetHunterFlightMontageTime;
	Placement.HunterFlightSection->SetTime(Placement.TargetHunterFlightMontageTime);
	Montage.UpdateLinkableElements();

	if (Placement.HunterFlightNotifyCount == 0)
	{
		UAnimNotify_IG_AwakenedKinsectEvent* Notify = Cast<UAnimNotify_IG_AwakenedKinsectEvent>(
			UAnimationBlueprintLibrary::AddAnimationNotifyEvent(&Montage, FName(ActionTrackName),
				Placement.TargetHunterFlightMontageTime,
				UAnimNotify_IG_AwakenedKinsectEvent::StaticClass()));
		if (!Notify)
		{
			Placement.Error = TEXT("Could not create IG Awakened Kinsect Event on the Action track.");
			return false;
		}
		Notify->Event = EAwakenedKinsectAttackEvent::HunterFlightStart;
		Placement.HunterFlightNotifyCount = 1;
		Placement.bNotifyAlreadyCorrect = true;
		Placement.HunterFlightNotifyDisplayTime = Placement.TargetHunterFlightMontageTime;
		Placement.HunterFlightNotifyTriggerTime = Placement.TargetHunterFlightMontageTime;
	}

	const float NewLength = Montage.CalculateSequenceLength();
	if (!FMath::IsFinite(NewLength) || NewLength <= 0.0f)
	{
		Placement.Error = TEXT("Updated Montage length is invalid.");
		return false;
	}
	Montage.SetCompositeLength(NewLength);
	Montage.RefreshCacheData();
	ReadAwakenedEventNotifies(Montage, Placement);
	if (Placement.KinsectLaunchNotifyCount != 1
		|| Placement.HunterFlightNotifyCount != 1
		|| !FMath::IsNearlyEqual(Placement.HunterFlightNotifyDisplayTime,
			Placement.TargetHunterFlightMontageTime, TimeTolerance))
	{
		Placement.Error = TEXT("Awakened event notify readback did not resolve to one KinsectLaunch and one HunterFlightStart at the expected 199 boundary.");
		return false;
	}
	if (Placement.KinsectLaunchToHunterFlightTriggerDelta <= 0.0f)
	{
		Placement.Error = TEXT("Notify readback produced an invalid KinsectLaunch -> HunterFlightStart trigger interval.");
		return false;
	}
	if (!PopulateKinsectFlightProfile(Placement))
	{
		Placement.Error = TEXT("Could not derive the fixed-duration kinsect profiles after Montage readback.");
		return false;
	}
	if (!SaveAsset(Montage))
	{
		Placement.Error = TEXT("Failed to save AM_IG_JueChongJi.");
		return false;
	}
	return true;
}
}
#endif

UMHGZM6RecordedHunterFlightSetupCommandlet::UMHGZM6RecordedHunterFlightSetupCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMHGZM6RecordedHunterFlightSetupCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	using namespace UE::MHGZ::M6RecordedHunterFlightSetup;
	const bool bApply = FParse::Param(*Params, TEXT("Apply"));
	FPlacement Placement;
	bool bSuccess = Prepare(Placement, bApply);
	if (bSuccess && bApply)
	{
		bSuccess = Apply(Placement);
	}
	UE_LOG(LogTemp, Display,
		TEXT("[M6HunterFlightNotify] mode=%s success=%s source_delta=%.8fs kinsect_launch_count=%d kinsect_launch_trigger=%.8fs hunter_flight_trigger=%.8fs launch_to_takeoff=%.8fs hunter_count=%d"),
		bApply ? TEXT("Apply") : TEXT("AuditOnly"), bSuccess ? TEXT("true") : TEXT("false"),
		Placement.RecordedDelta, Placement.KinsectLaunchNotifyCount,
		Placement.KinsectLaunchNotifyTriggerTime, Placement.HunterFlightNotifyTriggerTime,
		Placement.KinsectLaunchToHunterFlightTriggerDelta,
		Placement.HunterFlightNotifyCount);
	if (!Placement.Error.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("[M6HunterFlightNotify] %s"), *Placement.Error);
	}
	if (!WriteReport(Placement, bApply ? TEXT("Apply") : TEXT("AuditOnly"), bSuccess))
	{
		UE_LOG(LogTemp, Error, TEXT("[M6HunterFlightNotify] Failed to write placement report."));
		return 1;
	}
	return bSuccess ? 0 : 1;
#else
	UE_LOG(LogTemp, Error, TEXT("[M6HunterFlightNotify] This commandlet requires an editor build."));
	return 1;
#endif
}
