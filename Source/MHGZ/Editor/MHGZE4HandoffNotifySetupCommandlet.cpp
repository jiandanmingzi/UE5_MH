// Copyright MHGZ Project. All Rights Reserved.

#include "Editor/MHGZE4HandoffNotifySetupCommandlet.h"

#if WITH_EDITOR

#include "ActionSystem/AnimNotify_MotionMatchingHandoff.h"
#include "ActionSystem/AnimNotifyState_ActionRootMotionPhase.h"
#include "ActionSystem/MHGZDodgeAbility.h"
#include "ActionSystem/MHGZSheatheAbility.h"
#include "Animation/AnimMontage.h"
#include "AnimationBlueprintLibrary.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace UE::MHGZ::E4HandoffNotifySetup
{
constexpr TCHAR AuditDirectoryName[] = TEXT("ActionExitAudit");
const FName NotifyTrackName(TEXT("MotionMatching"));
constexpr float MontageFps = 60.0f;
// Dodge Montages now begin with a three-frame static entry pose. Keep this
// authored Handoff source-of-truth aligned to the moved Core timeline.
constexpr int32 DodgeEntryHoldFrames = 3;
// NotifyState End is evaluated after ordinary notifies when both are crossed
// in one animation update. Three source frames leave a deterministic
// ordering margin at 60fps while retaining the approved Handoff frame.
constexpr int32 PhaseToHandoffSafetyFrames = 3;
constexpr float TimeTolerance = 0.002f;

struct FMontageRoute
{
	const TCHAR* Label;
	const TCHAR* MontagePath;
	int32 HandoffFrame;
	float PhaseStartTime;
	EMHGZMotionMatchingHandoffType HandoffType;
	bool bObserveRawMovementInput;
};

static const FMontageRoute Routes[] =
{
	{
		TEXT("SheatheMoveExit"),
		TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_ShouDao.AM_IG_ShouDao"),
		115, 61.0f / MontageFps, EMHGZMotionMatchingHandoffType::SheatheMoveExit,
		true
	},
	{
		TEXT("SheathedDodgeMoveExit"),
		TEXT("/Game/Characters/Demo/Anims/Montage/AM_Shth_Dodge.AM_Shth_Dodge"),
		85 + DodgeEntryHoldFrames, static_cast<float>(DodgeEntryHoldFrames) / MontageFps,
		EMHGZMotionMatchingHandoffType::DodgeMoveExit, true
	},
	{
		TEXT("UnsheathedForwardDodgeMoveExit"),
		TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_Dodge_Forward.AM_IG_Dodge_Forward"),
		100 + DodgeEntryHoldFrames, static_cast<float>(DodgeEntryHoldFrames) / MontageFps,
		EMHGZMotionMatchingHandoffType::DodgeMoveExit, true
	},
	// Directional rolls never enter MoveExit. They still use a Phase so an
	// attack's existing root-motion owner is released by Dodge supersede before
	// the roll begins its authored root displacement. INDEX_NONE means Phase-only.
	{
		TEXT("UnsheathedBackDodge"),
		TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_Dodge_Back.AM_IG_Dodge_Back"),
		INDEX_NONE, static_cast<float>(DodgeEntryHoldFrames) / MontageFps,
		EMHGZMotionMatchingHandoffType::None, false
	},
	{
		TEXT("UnsheathedLeftDodge"),
		TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_Dodge_Left.AM_IG_Dodge_Left"),
		INDEX_NONE, static_cast<float>(DodgeEntryHoldFrames) / MontageFps,
		EMHGZMotionMatchingHandoffType::None, false
	},
	{
		TEXT("UnsheathedRightDodge"),
		TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_Dodge_Right.AM_IG_Dodge_Right"),
		INDEX_NONE, static_cast<float>(DodgeEntryHoldFrames) / MontageFps,
		EMHGZMotionMatchingHandoffType::None, false
	}
};

bool RouteHasHandoff(const FMontageRoute& Route)
{
	return Route.HandoffFrame != INDEX_NONE;
}

float GetPhaseEndTime(const FMontageRoute& Route, const UAnimMontage& Montage)
{
	return RouteHasHandoff(Route)
		? (static_cast<float>(Route.HandoffFrame - PhaseToHandoffSafetyFrames) / MontageFps)
		: Montage.GetPlayLength();
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

void RemoveExistingE4Notifies(UAnimMontage& Montage)
{
	Montage.Notifies.RemoveAll([](const FAnimNotifyEvent& Event)
	{
		return Cast<UAnimNotifyState_ActionRootMotionPhase>(Event.NotifyStateClass) != nullptr
			|| Cast<UAnimNotify_MotionMatchingHandoff>(Event.Notify) != nullptr;
	});
}

bool ValidateMontage(const FMontageRoute& Route, const UAnimMontage& Montage)
{
	const bool bHasHandoff = RouteHasHandoff(Route);
	const float HandoffTime = bHasHandoff
		? static_cast<float>(Route.HandoffFrame) / MontageFps
		: 0.0f;
	const float PhaseEndTime = GetPhaseEndTime(Route, Montage);
	int32 PhaseCount = 0;
	int32 HandoffCount = 0;
	for (const FAnimNotifyEvent& Event : Montage.Notifies)
	{
		if (const UAnimNotifyState_ActionRootMotionPhase* Phase =
			Cast<UAnimNotifyState_ActionRootMotionPhase>(Event.NotifyStateClass))
		{
			++PhaseCount;
			if (!FMath::IsNearlyEqual(Event.GetTriggerTime(), Route.PhaseStartTime, TimeTolerance)
				|| !FMath::IsNearlyEqual(Event.GetEndTriggerTime(), PhaseEndTime, TimeTolerance)
				|| !Phase->bOwnsMontageRootMotion
				|| Phase->bObserveRawMovementInput != Route.bObserveRawMovementInput)
			{
				return false;
			}
		}
		if (const UAnimNotify_MotionMatchingHandoff* Handoff =
			Cast<UAnimNotify_MotionMatchingHandoff>(Event.Notify))
		{
			++HandoffCount;
			if (!bHasHandoff
				|| !FMath::IsNearlyEqual(Event.GetTriggerTime(), HandoffTime, TimeTolerance)
				|| Handoff->HandoffType != Route.HandoffType)
			{
				return false;
			}
		}
	}
	return PhaseCount == 1 && HandoffCount == (bHasHandoff ? 1 : 0);
}

bool ConfigureMontage(const FMontageRoute& Route, UAnimMontage& Montage)
{
	const bool bHasHandoff = RouteHasHandoff(Route);
	const float HandoffTime = bHasHandoff
		? static_cast<float>(Route.HandoffFrame) / MontageFps
		: 0.0f;
	const float PhaseEndTime = GetPhaseEndTime(Route, Montage);
	if (Route.PhaseStartTime < 0.0f || PhaseEndTime <= Route.PhaseStartTime
		|| (bHasHandoff && HandoffTime >= Montage.GetPlayLength()))
	{
		UE_LOG(LogTemp, Error, TEXT("[E4HandoffSetup] Invalid approved time for %s."), Route.Label);
		return false;
	}

	Montage.Modify();
	RemoveExistingE4Notifies(Montage);
	if (!UAnimationBlueprintLibrary::IsValidAnimNotifyTrackName(&Montage, NotifyTrackName))
	{
		UAnimationBlueprintLibrary::AddAnimationNotifyTrack(&Montage, NotifyTrackName, FLinearColor::Yellow);
	}
	UAnimNotifyState_ActionRootMotionPhase* Phase =
		Cast<UAnimNotifyState_ActionRootMotionPhase>(
			UAnimationBlueprintLibrary::AddAnimationNotifyStateEvent(&Montage, NotifyTrackName,
				Route.PhaseStartTime, PhaseEndTime - Route.PhaseStartTime,
				UAnimNotifyState_ActionRootMotionPhase::StaticClass()));
	UAnimNotify_MotionMatchingHandoff* Handoff = nullptr;
	if (bHasHandoff)
	{
		Handoff = Cast<UAnimNotify_MotionMatchingHandoff>(
			UAnimationBlueprintLibrary::AddAnimationNotifyEvent(&Montage, NotifyTrackName,
				HandoffTime, UAnimNotify_MotionMatchingHandoff::StaticClass()));
	}
	if (!Phase || (bHasHandoff && !Handoff))
	{
		return false;
	}
	Phase->bOwnsMontageRootMotion = true;
	Phase->bObserveRawMovementInput = Route.bObserveRawMovementInput;
	if (Handoff)
	{
		Handoff->HandoffType = Route.HandoffType;
	}
	Montage.RefreshCacheData();
	return ValidateMontage(Route, Montage) && SaveAsset(Montage);
}

template <typename TAbility>
TAbility* GetAbilityCdo(UBlueprint& Blueprint)
{
	return Blueprint.GeneratedClass
		? Cast<TAbility>(Blueprint.GeneratedClass->GetDefaultObject())
		: nullptr;
}

bool ConfigureAbilityBlueprint(UBlueprint& Blueprint, const EMHGZMotionMatchingHandoffType Type,
	const bool bSheathe)
{
	Blueprint.Modify();
	if (bSheathe)
	{
		UMHGZSheatheAbility* Ability = GetAbilityCdo<UMHGZSheatheAbility>(Blueprint);
		if (!Ability)
		{
			return false;
		}
		Ability->Modify();
		Ability->bWalkUsesActionRootMotionPhase = true;
		Ability->AllowedMotionMatchingHandoffTypes.AddUnique(Type);
	}
	else
	{
		UMHGZDodgeAbility* Ability = GetAbilityCdo<UMHGZDodgeAbility>(Blueprint);
		if (!Ability)
		{
			return false;
		}
		Ability->Modify();
		Ability->bForwardDodgeUsesActionRootMotionPhase = true;
		Ability->AllowedMotionMatchingHandoffTypes.AddUnique(Type);
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(&Blueprint);
	FKismetEditorUtilities::CompileBlueprint(&Blueprint);
	if (Blueprint.Status == BS_Error)
	{
		return false;
	}

	if (bSheathe)
	{
		const UMHGZSheatheAbility* Ability = GetAbilityCdo<UMHGZSheatheAbility>(Blueprint);
		if (!Ability || !Ability->bWalkUsesActionRootMotionPhase
			|| !Ability->AllowedMotionMatchingHandoffTypes.Contains(Type))
		{
			return false;
		}
	}
	else
	{
		const UMHGZDodgeAbility* Ability = GetAbilityCdo<UMHGZDodgeAbility>(Blueprint);
		if (!Ability || !Ability->bForwardDodgeUsesActionRootMotionPhase
			|| !Ability->AllowedMotionMatchingHandoffTypes.Contains(Type))
		{
			return false;
		}
	}
	return SaveAsset(Blueprint);
}

bool WriteAudit(const TArray<FString>& Rows)
{
	const FString OutputDirectory = FPaths::ProjectSavedDir() / AuditDirectoryName;
	IFileManager::Get().MakeDirectory(*OutputDirectory, true);
	const FString OutputPath = OutputDirectory / TEXT("E4_2_HandoffNotifySetup.md");
	TArray<FString> Lines;
	Lines.Add(TEXT("# E4.2 Handoff Notify Setup"));
	Lines.Add(TEXT(""));
	Lines.Add(TEXT("Mobile routes receive approved 60fps Phase-to-Handoff selections; directional Dodge routes receive a Phase-only contract."));
	Lines.Add(FString::Printf(TEXT("Every mobile Action Root Motion Phase ends exactly %d 60fps Montage frames before its Handoff."), PhaseToHandoffSafetyFrames));
	Lines.Add(TEXT(""));
	Lines.Add(TEXT("| Route | Montage | Phase | Handoff | HandoffType |"));
	Lines.Add(TEXT("|---|---|---|---:|---|"));
	Lines.Append(Rows);
	Lines.Add(TEXT(""));
	Lines.Add(TEXT("GA_Sheathe: Walk Uses Action Root Motion Phase=true; allowed type includes SheatheMoveExit."));
	Lines.Add(TEXT("GA_Dodge: Dodge Uses Action Root Motion Phase=true; directional rolls use Phase-only ownership and never publish a MoveExit handoff."));
	return FFileHelper::SaveStringToFile(FString::Join(Lines, TEXT("\n")), *OutputPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
}
#endif // WITH_EDITOR

UMHGZE4HandoffNotifySetupCommandlet::UMHGZE4HandoffNotifySetupCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMHGZE4HandoffNotifySetupCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	using namespace UE::MHGZ::E4HandoffNotifySetup;
	(void)Params;
	bool bSucceeded = true;
	TArray<FString> Rows;
	for (const FMontageRoute& Route : Routes)
	{
		UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, Route.MontagePath);
		if (!Montage || !ConfigureMontage(Route, *Montage))
		{
			UE_LOG(LogTemp, Error, TEXT("[E4HandoffSetup] Failed to configure %s."), Route.Label);
			bSucceeded = false;
			continue;
		}
		const float PhaseEndTime = GetPhaseEndTime(Route, *Montage);
		if (RouteHasHandoff(Route))
		{
			const float HandoffTime = static_cast<float>(Route.HandoffFrame) / MontageFps;
			Rows.Add(FString::Printf(TEXT("| %s | %s | %.4f-%.4f | %.4f (frame %d) | %d |"),
				Route.Label, *Montage->GetPathName(), Route.PhaseStartTime, PhaseEndTime,
				HandoffTime, Route.HandoffFrame, static_cast<int32>(Route.HandoffType)));
		}
		else
		{
			Rows.Add(FString::Printf(TEXT("| %s | %s | %.4f-%.4f | none | none |"),
				Route.Label, *Montage->GetPathName(), Route.PhaseStartTime, PhaseEndTime));
		}
	}

	UBlueprint* SheatheBlueprint = LoadObject<UBlueprint>(nullptr,
		TEXT("/Game/Blueprints/Ability/Core/GA_Sheathe.GA_Sheathe"));
	UBlueprint* DodgeBlueprint = LoadObject<UBlueprint>(nullptr,
		TEXT("/Game/Blueprints/Ability/Core/GA_Dodge.GA_Dodge"));
	if (!SheatheBlueprint || !DodgeBlueprint
		|| !ConfigureAbilityBlueprint(*SheatheBlueprint,
			EMHGZMotionMatchingHandoffType::SheatheMoveExit, true)
		|| !ConfigureAbilityBlueprint(*DodgeBlueprint,
			EMHGZMotionMatchingHandoffType::DodgeMoveExit, false))
	{
		UE_LOG(LogTemp, Error, TEXT("[E4HandoffSetup] Failed to configure GA_Sheathe or GA_Dodge."));
		bSucceeded = false;
	}

	if (!WriteAudit(Rows))
	{
		UE_LOG(LogTemp, Error, TEXT("[E4HandoffSetup] Failed to write audit report."));
		bSucceeded = false;
	}
	UE_LOG(LogTemp, Display, TEXT("[E4HandoffSetup] Completed %d/%d Montages."),
		Rows.Num(), UE_ARRAY_COUNT(Routes));
	return bSucceeded ? 0 : 1;
#else
	UE_LOG(LogTemp, Error, TEXT("[E4HandoffSetup] This commandlet requires an editor build."));
	return 1;
#endif
}
