// Copyright MHGZ Project. All Rights Reserved.

#include "Commandlets/MHGZM6ReleaseFacingNotifyCommandlet.h"

#if WITH_EDITOR

#include "ActionSystem/AnimNotify_IG_AwakenedKinsectEvent.h"
#include "Animation/AnimMontage.h"
#include "AnimationBlueprintLibrary.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/SavePackage.h"

namespace UE::MHGZ::M6ReleaseFacingNotify
{
constexpr TCHAR MontagePath[] = TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_JueChongJi.AM_IG_JueChongJi");
constexpr TCHAR ReportRelativePath[] = TEXT("Saved/M6Audit/m6_release_facing_notify.json");
constexpr TCHAR TrackName[] = TEXT("Action");
constexpr float MontageFrameRate = 60.0f;
constexpr float TargetFrame = 230.0f;
constexpr float TargetTime = TargetFrame / MontageFrameRate;
constexpr float TimeTolerance = 0.0001f;

struct FAudit
{
	UAnimMontage* Montage = nullptr;
	int32 ReleaseFacingCount = 0;
	int32 KinsectLaunchCount = 0;
	int32 HunterFlightStartCount = 0;
	float ReleaseFacingDisplayTime = 0.0f;
	float ReleaseFacingTriggerTime = 0.0f;
	float KinsectLaunchTriggerTime = 0.0f;
	float HunterFlightStartTriggerTime = 0.0f;
	FString Error;
};

void ReadEvents(const UAnimMontage& Montage, FAudit& Audit)
{
	Audit.ReleaseFacingCount = 0;
	Audit.KinsectLaunchCount = 0;
	Audit.HunterFlightStartCount = 0;
	Audit.ReleaseFacingDisplayTime = 0.0f;
	Audit.ReleaseFacingTriggerTime = 0.0f;
	Audit.KinsectLaunchTriggerTime = 0.0f;
	Audit.HunterFlightStartTriggerTime = 0.0f;

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
		case EAwakenedKinsectAttackEvent::ReleaseFacingCorrection:
			++Audit.ReleaseFacingCount;
			Audit.ReleaseFacingDisplayTime = Event.GetTime();
			Audit.ReleaseFacingTriggerTime = Event.GetTriggerTime();
			break;
		case EAwakenedKinsectAttackEvent::KinsectLaunch:
			++Audit.KinsectLaunchCount;
			Audit.KinsectLaunchTriggerTime = Event.GetTriggerTime();
			break;
		case EAwakenedKinsectAttackEvent::HunterFlightStart:
			++Audit.HunterFlightStartCount;
			Audit.HunterFlightStartTriggerTime = Event.GetTriggerTime();
			break;
		default:
			break;
		}
	}
}

bool Prepare(FAudit& Audit)
{
	Audit.Montage = LoadObject<UAnimMontage>(nullptr, MontagePath);
	if (!Audit.Montage)
	{
		Audit.Error = TEXT("Could not load persistent AM_IG_JueChongJi.");
		return false;
	}
	if (!UAnimationBlueprintLibrary::IsValidAnimNotifyTrackName(
		Audit.Montage, FName(TrackName)))
	{
		Audit.Error = TEXT("The Action notify track is missing; no Montage changes were made.");
		return false;
	}

	ReadEvents(*Audit.Montage, Audit);
	if (Audit.KinsectLaunchCount != 1 || Audit.HunterFlightStartCount != 1
		|| Audit.KinsectLaunchTriggerTime <= TargetTime
		|| Audit.HunterFlightStartTriggerTime <= Audit.KinsectLaunchTriggerTime)
	{
		Audit.Error = TEXT("Expected one KinsectLaunch and one later HunterFlightStart, with frame 230 before launch; preserving the Montage.");
		return false;
	}
	if (Audit.ReleaseFacingCount > 1
		|| (Audit.ReleaseFacingCount == 1
			&& !FMath::IsNearlyEqual(Audit.ReleaseFacingDisplayTime,
				TargetTime, TimeTolerance)))
	{
		Audit.Error = TEXT("ReleaseFacingCorrection is duplicated or not at Montage frame 230; preserving the Montage for review.");
		return false;
	}
	return true;
}

bool SaveMontage(UAnimMontage& Montage)
{
	UPackage* Package = Montage.GetOutermost();
	FString Filename;
	if (!Package || !FPackageName::TryConvertLongPackageNameToFilename(
		Package->GetName(), Filename, FPackageName::GetAssetPackageExtension()))
	{
		return false;
	}

	Package->MarkPackageDirty();
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	return UPackage::SavePackage(Package, &Montage, *Filename, SaveArgs);
}

bool Apply(FAudit& Audit)
{
	if (Audit.ReleaseFacingCount == 1)
	{
		return true;
	}

	UAnimNotify_IG_AwakenedKinsectEvent* Notify =
		Cast<UAnimNotify_IG_AwakenedKinsectEvent>(
			UAnimationBlueprintLibrary::AddAnimationNotifyEvent(
				Audit.Montage, FName(TrackName), TargetTime,
				UAnimNotify_IG_AwakenedKinsectEvent::StaticClass()));
	if (!Notify)
	{
		Audit.Error = TEXT("Could not add the IG Awakened Kinsect Event point notify.");
		return false;
	}
	Notify->Event = EAwakenedKinsectAttackEvent::ReleaseFacingCorrection;
	Audit.Montage->RefreshCacheData();
	ReadEvents(*Audit.Montage, Audit);
	if (Audit.ReleaseFacingCount != 1
		|| !FMath::IsNearlyEqual(Audit.ReleaseFacingDisplayTime,
			TargetTime, TimeTolerance)
		|| Audit.ReleaseFacingTriggerTime >= Audit.KinsectLaunchTriggerTime)
	{
		Audit.Error = TEXT("Read-back did not find exactly one frame-230 ReleaseFacingCorrection before KinsectLaunch; preserving unsaved changes.");
		return false;
	}
	if (!SaveMontage(*Audit.Montage))
	{
		Audit.Error = TEXT("Failed to save AM_IG_JueChongJi after adding the point notify.");
		return false;
	}
	return true;
}

bool WriteReport(const FAudit& Audit, const TCHAR* Mode, const bool bSuccess)
{
	const FString OutputPath = FPaths::ProjectDir() / ReportRelativePath;
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutputPath), true);
	const FString Json = FString::Printf(
		TEXT("{\n  \"mode\": \"%s\",\n  \"success\": %s,\n  \"montage\": \"%s\",\n  \"track\": \"%s\",\n  \"target_frame\": %.0f,\n  \"frame_rate\": %.1f,\n  \"target_time_s\": %.8f,\n  \"release_facing_count\": %d,\n  \"release_facing_display_time_s\": %.8f,\n  \"release_facing_trigger_time_s\": %.8f,\n  \"kinsect_launch_count\": %d,\n  \"kinsect_launch_trigger_time_s\": %.8f,\n  \"hunter_flight_start_count\": %d,\n  \"hunter_flight_start_trigger_time_s\": %.8f,\n  \"error\": \"%s\"\n}\n"),
		Mode, bSuccess ? TEXT("true") : TEXT("false"),
		Audit.Montage ? *Audit.Montage->GetPathName() : TEXT(""), TrackName,
		TargetFrame, MontageFrameRate, TargetTime, Audit.ReleaseFacingCount,
		Audit.ReleaseFacingDisplayTime, Audit.ReleaseFacingTriggerTime,
		Audit.KinsectLaunchCount, Audit.KinsectLaunchTriggerTime,
		Audit.HunterFlightStartCount, Audit.HunterFlightStartTriggerTime,
		*Audit.Error.Replace(TEXT("\""), TEXT("'")));
	return FFileHelper::SaveStringToFile(Json, *OutputPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
}
#endif

UMHGZM6ReleaseFacingNotifyCommandlet::UMHGZM6ReleaseFacingNotifyCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMHGZM6ReleaseFacingNotifyCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	using namespace UE::MHGZ::M6ReleaseFacingNotify;
	const bool bApply = FParse::Param(*Params, TEXT("Apply"));
	FAudit Audit;
	bool bSuccess = Prepare(Audit);
	if (bSuccess && bApply)
	{
		bSuccess = Apply(Audit);
	}
	else if (bSuccess && Audit.ReleaseFacingCount == 0)
	{
		Audit.Error = TEXT("ReleaseFacingCorrection is missing; rerun with -Apply to place the single point notify.");
		bSuccess = false;
	}

	if (!Audit.Error.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("[M6ReleaseFacingNotify] %s"), *Audit.Error);
	}
	UE_LOG(LogTemp, Display,
		TEXT("[M6ReleaseFacingNotify] mode=%s success=%s montage_frame=%.0f target_time=%.8fs release_count=%d release_display=%.8fs release_trigger=%.8fs launch_count=%d launch_trigger=%.8fs hunter_count=%d hunter_trigger=%.8fs"),
		bApply ? TEXT("Apply") : TEXT("AuditOnly"), bSuccess ? TEXT("true") : TEXT("false"),
		TargetFrame, TargetTime, Audit.ReleaseFacingCount,
		Audit.ReleaseFacingDisplayTime, Audit.ReleaseFacingTriggerTime,
		Audit.KinsectLaunchCount, Audit.KinsectLaunchTriggerTime,
		Audit.HunterFlightStartCount, Audit.HunterFlightStartTriggerTime);
	if (!WriteReport(Audit, bApply ? TEXT("Apply") : TEXT("AuditOnly"), bSuccess))
	{
		UE_LOG(LogTemp, Error, TEXT("[M6ReleaseFacingNotify] Failed to write placement report."));
		return 1;
	}
	return bSuccess ? 0 : 1;
#else
	UE_LOG(LogTemp, Error, TEXT("[M6ReleaseFacingNotify] This commandlet requires an editor build."));
	return 1;
#endif
}
