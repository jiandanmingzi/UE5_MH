#include "Commandlets/MHGZM63PowderVortexAssetSetupCommandlet.h"
#include "MHGZIGActionConfigAccess.h"
// Copyright MHGZ Project. All Rights Reserved.


#if WITH_EDITOR

#include "ActionSystem/AnimNotify_IG_PowderGatherBegin.h"
#include "ActionSystem/MHGZPowderVortexAbility.h"
#include "ActionSystem/MHGZWeaponComboData.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AnimationBlueprintLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "InsectGlaive/InsectGlaiveCombatConfig.h"
#include "WeaponRuntime/MHGZWeaponInputProfile.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/SavePackage.h"

namespace UE::MHGZ::M63PowderVortexSetup
{
constexpr TCHAR SourceSequencePath[] = TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_CaoChong.AS_Unsh_CaoChong");
constexpr TCHAR ExpectedSkeletonPath[] = TEXT("/Game/Characters/Demo/Meshes/Body/SK_Demo_Body.SK_Demo_Body");
constexpr TCHAR MontagePackage[] = TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_PowderVortex");
constexpr TCHAR AbilityPackage[] = TEXT("/Game/Blueprints/Ability/InsectGlaive/GA_IG_PowderVortex");
constexpr TCHAR ComboPath[] = TEXT("/Game/Weapons/InsectGlaive/Data/DA_IG_Combo.DA_IG_Combo");
constexpr TCHAR InputProfilePath[] = TEXT("/Game/Weapons/InsectGlaive/Data/DA_IG_InputProfile.DA_IG_InputProfile");
constexpr TCHAR ReportRelativePath[] = TEXT("Saved/M6Audit/m6_powder_vortex_asset_setup_report.json");
constexpr TCHAR MontageName[] = TEXT("AM_IG_PowderVortex");
constexpr TCHAR AbilityName[] = TEXT("GA_IG_PowderVortex");
constexpr TCHAR TransitionID[] = TEXT("Idle_LTYB_PowderVortex");
constexpr TCHAR TargetState[] = TEXT("IG.Special.PowderVortex");
constexpr TCHAR SectionName[] = TEXT("PowderVortex");
constexpr TCHAR SlotName[] = TEXT("UpperBody_IGAction");
constexpr TCHAR LegacySlotName[] = TEXT("DefaultSlot");
constexpr TCHAR SendKinsectMontagePath[] = TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_SendKinsect.AM_IG_SendKinsect");
constexpr TCHAR RecallKinsectMontagePath[] = TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_RecallKinsect.AM_IG_RecallKinsect");
constexpr TCHAR DrawAndSendKinsectMontagePath[] = TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_DrawAndSendKinsect.AM_IG_DrawAndSendKinsect");
constexpr TCHAR NotifyTrackName[] = TEXT("Action");
constexpr float NotifyTime = 0.0f;

struct FRun
{
	bool bApply = false;
	TArray<FString> Errors;
	TArray<FString> ChangedAssets;
	UAnimSequence* SourceSequence = nullptr;
	UAnimMontage* Montage = nullptr;
	UBlueprint* AbilityBlueprint = nullptr;
	UMHGZPowderVortexAbility* Ability = nullptr;
	UMHGZWeaponComboData* Combo = nullptr;
	UWeaponInputProfile* InputProfile = nullptr;
	float DelayAfterArrival = 0.0f;
};

FString ObjectPath(const TCHAR* Package, const TCHAR* Name)
{
	return FString::Printf(TEXT("%s.%s"), Package, Name);
}

FGameplayTag RequestTag(const TCHAR* Name)
{
	return FGameplayTag::RequestGameplayTag(FName(Name), false);
}

UObject* LoadObjectByPath(const FString& Path)
{
	return StaticLoadObject(UObject::StaticClass(), nullptr, *Path, nullptr, LOAD_NoWarn);
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
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	return UPackage::SavePackage(Package, &Asset, *Filename, Args);
}

int32 FindUniquePopulatedSlotTrackIndex(const UAnimMontage* Montage)
{
	if (!Montage)
	{
		return INDEX_NONE;
	}
	int32 PopulatedIndex = INDEX_NONE;
	for (int32 Index = 0; Index < Montage->SlotAnimTracks.Num(); ++Index)
	{
		if (Montage->SlotAnimTracks[Index].AnimTrack.AnimSegments.IsEmpty())
		{
			continue;
		}
		if (PopulatedIndex != INDEX_NONE)
		{
			return INDEX_NONE;
		}
		PopulatedIndex = Index;
	}
	return PopulatedIndex;
}

bool SetPowderVortexSlotOnly(const bool bApply)
{
	UAnimMontage* SendKinsect = LoadObject<UAnimMontage>(nullptr,
		SendKinsectMontagePath, nullptr, LOAD_NoWarn);
	UAnimMontage* RecallKinsect = LoadObject<UAnimMontage>(nullptr,
		RecallKinsectMontagePath, nullptr, LOAD_NoWarn);
	UAnimMontage* DrawAndSendKinsect = LoadObject<UAnimMontage>(nullptr,
		DrawAndSendKinsectMontagePath, nullptr, LOAD_NoWarn);
	const FString PowderVortexPath = ObjectPath(MontagePackage, MontageName);
	UAnimMontage* PowderVortex = LoadObject<UAnimMontage>(nullptr,
		*PowderVortexPath, nullptr, LOAD_NoWarn);
	if (!SendKinsect || !RecallKinsect || !DrawAndSendKinsect || !PowderVortex)
	{
		UE_LOG(LogTemp, Error, TEXT("[M63PowderVortexSlot] Could not load all source and target Montages."));
		return false;
	}

	const int32 SendTrackIndex = FindUniquePopulatedSlotTrackIndex(SendKinsect);
	const FName KinsectSlot = SendTrackIndex != INDEX_NONE
		? SendKinsect->SlotAnimTracks[SendTrackIndex].SlotName : NAME_None;
	const UAnimMontage* KinsectMontages[] = { SendKinsect, RecallKinsect, DrawAndSendKinsect };
	for (const UAnimMontage* KinsectMontage : KinsectMontages)
	{
		const int32 PopulatedTrackIndex = FindUniquePopulatedSlotTrackIndex(KinsectMontage);
		if (KinsectSlot != FName(SlotName)
			|| PopulatedTrackIndex == INDEX_NONE
			|| KinsectMontage->SlotAnimTracks[PopulatedTrackIndex].SlotName != KinsectSlot
			|| KinsectMontage->GetSkeleton() != PowderVortex->GetSkeleton())
		{
			UE_LOG(LogTemp, Error,
				TEXT("[M63PowderVortexSlot] Kinsect source slot/layout or Skeleton differs; refusing to guess."));
			return false;
		}
	}

	const int32 PowderTrackIndex = FindUniquePopulatedSlotTrackIndex(PowderVortex);
	if (PowderTrackIndex == INDEX_NONE
		|| PowderVortex->SlotAnimTracks[PowderTrackIndex].AnimTrack.AnimSegments.Num() != 1
		|| PowderVortex->CompositeSections.Num() != 1
		|| PowderVortex->GetSkeleton() != SendKinsect->GetSkeleton())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[M63PowderVortexSlot] Target Montage is not the audited one-slot/one-segment/one-section asset; refusing to modify it."));
		return false;
	}

	const FName CurrentSlot = PowderVortex->SlotAnimTracks[PowderTrackIndex].SlotName;
	if (CurrentSlot == FName(SlotName))
	{
		UE_LOG(LogTemp, Display,
			TEXT("[M63PowderVortexSlot] Already matches Kinsect slot %s; no change."), SlotName);
		return true;
	}
	if (CurrentSlot != FName(LegacySlotName))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[M63PowderVortexSlot] Unexpected current slot %s; only the known DefaultSlot may be migrated."),
			*CurrentSlot.ToString());
		return false;
	}
	if (!bApply)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[M63PowderVortexSlot] Audit found %s; rerun with -SetSlotOnly -Apply to change only this Slot track."),
			*CurrentSlot.ToString());
		return false;
	}

	PowderVortex->SlotAnimTracks[PowderTrackIndex].SlotName = KinsectSlot;
	PowderVortex->MarkPackageDirty();
	if (!SaveAsset(*PowderVortex))
	{
		UE_LOG(LogTemp, Error, TEXT("[M63PowderVortexSlot] Failed to save the target Montage."));
		return false;
	}
	UE_LOG(LogTemp, Display,
		TEXT("[M63PowderVortexSlot] Changed only %s Slot track: %s -> %s."),
		*PowderVortex->GetPathName(), *CurrentSlot.ToString(), SlotName);
	return true;
}

bool LoadDependencies(FRun& Run)
{
	Run.SourceSequence = LoadObject<UAnimSequence>(nullptr, SourceSequencePath,
		nullptr, LOAD_NoWarn);
	Run.Combo = LoadObject<UMHGZWeaponComboData>(nullptr, ComboPath,
		nullptr, LOAD_NoWarn);
	Run.InputProfile = LoadObject<UWeaponInputProfile>(nullptr, InputProfilePath,
		nullptr, LOAD_NoWarn);
	UInsectGlaiveCombatConfig* Combat = LoadObject<UInsectGlaiveCombatConfig>(nullptr,
		TEXT("/Game/Weapons/InsectGlaive/Data/DA_IG_Combat.DA_IG_Combat"),
		nullptr, LOAD_NoWarn);
	if (!Run.SourceSequence || !Run.Combo || !Run.InputProfile || !Combat)
	{
		Run.Errors.Add(TEXT("Could not load source sequence, DA_IG_Combo, DA_IG_InputProfile, or DA_IG_Combat."));
		return false;
	}
	if (!Run.SourceSequence->GetSkeleton()
		|| Run.SourceSequence->GetSkeleton()->GetPathName() != ExpectedSkeletonPath
		|| !FMath::IsNearlyEqual(Run.SourceSequence->RateScale, 2.0f)
		|| Run.SourceSequence->GetPlayLength() <= 0.0f)
	{
		Run.Errors.Add(TEXT("AS_Unsh_CaoChong no longer matches the audited SK_Demo_Body / RateScale=2 source contract."));
		return false;
	}
	Run.DelayAfterArrival = ::MHGZ::IGEditor::RequireAction<UMHGZPowderVortexAbility>(TEXT("PowderVortex")).PowderVortex.GatherDelay;
	if (!FMath::IsFinite(Run.DelayAfterArrival)
		|| !FMath::IsNearlyEqual(Run.DelayAfterArrival, 1.0f, 0.001f))
	{
		Run.Errors.Add(TEXT("PowderVortexDelay must remain 1.0 second after arrival; it is independent of Montage length."));
		return false;
	}
	int32 LTYBChordCount = 0;
	bool bInputProfileNeedsUpdate = false;
	for (const FWeaponChordDefinition& Chord : Run.InputProfile->Chords)
	{
		if (Chord.OutputTag.MatchesTagExact(RequestTag(TEXT("Input.Weapon.LTYB"))))
		{
			++LTYBChordCount;
			bInputProfileNeedsUpdate = Chord.AimSnapshotContext
				!= EWeaponAimSnapshotContext::Kinsect;
		}
	}
	if (LTYBChordCount != 1)
	{
		Run.Errors.Add(TEXT("DA_IG_InputProfile must contain exactly one LTYB chord; refusing to guess or duplicate it."));
		return false;
	}
	if (bInputProfileNeedsUpdate)
	{
		if (!Run.bApply)
		{
			Run.Errors.Add(TEXT("The unique LTYB chord AimSnapshotContext is not Kinsect; rerun with -Apply to update only that field."));
			return false;
		}
		for (FWeaponChordDefinition& Chord : Run.InputProfile->Chords)
		{
			if (Chord.OutputTag.MatchesTagExact(RequestTag(TEXT("Input.Weapon.LTYB"))))
			{
				Chord.AimSnapshotContext = EWeaponAimSnapshotContext::Kinsect;
				Run.ChangedAssets.AddUnique(Run.InputProfile->GetPathName());
			}
		}
	}
	return true;
}

bool EnsureMontage(FRun& Run)
{
	const FString Path = ObjectPath(MontagePackage, MontageName);
	Run.Montage = LoadObject<UAnimMontage>(nullptr, *Path, nullptr, LOAD_NoWarn);
	UObject* Existing = LoadObjectByPath(Path);
	if (Existing && !Run.Montage)
	{
		Run.Errors.Add(TEXT("AM_IG_PowderVortex path contains an incompatible asset class."));
		return false;
	}
	if (!Run.Montage && !Run.bApply)
	{
		Run.Errors.Add(TEXT("AM_IG_PowderVortex is missing; rerun with -Apply to create the persistent Montage."));
		return false;
	}
	if (!Run.Montage)
	{
		UPackage* Package = CreatePackage(MontagePackage);
		Run.Montage = Package ? NewObject<UAnimMontage>(Package,
			FName(MontageName), RF_Public | RF_Standalone) : nullptr;
		if (!Run.Montage)
		{
			Run.Errors.Add(TEXT("Could not create AM_IG_PowderVortex."));
			return false;
		}
		Run.Montage->SetSkeleton(Run.SourceSequence->GetSkeleton());
		Run.Montage->SlotAnimTracks.Reset();
		Run.Montage->CompositeSections.Reset();
		FSlotAnimationTrack& Slot = Run.Montage->SlotAnimTracks.AddDefaulted_GetRef();
		Slot.SlotName = FName(SlotName);
		FAnimSegment& Segment = Slot.AnimTrack.AnimSegments.AddDefaulted_GetRef();
		Segment.SetAnimReference(Run.SourceSequence, true);
		Segment.StartPos = 0.0f;
		Segment.AnimStartTime = 0.0f;
		Segment.AnimEndTime = Run.SourceSequence->GetPlayLength();
		Segment.AnimPlayRate = 1.0f;
		Segment.LoopingCount = 1;
		FCompositeSection& Section = Run.Montage->CompositeSections.AddDefaulted_GetRef();
		Section.SectionName = FName(SectionName);
		Section.Link(Run.Montage, 0.0f, 0);
		Section.SetTime(0.0f);
		Section.NextSectionName = NAME_None;
		const float Length = Run.Montage->CalculateSequenceLength();
		if (!FMath::IsFinite(Length) || Length <= 0.0f)
		{
			Run.Errors.Add(TEXT("Could not calculate the Powder Vortex Montage duration."));
			return false;
		}
		Run.Montage->SetCompositeLength(Length);
		Run.Montage->UpdateLinkableElements();
		Run.Montage->bEnableAutoBlendOut = true;
		FAssetRegistryModule::AssetCreated(Run.Montage);
		Run.ChangedAssets.Add(Path);
	}

	if (Run.Montage->GetSkeleton() != Run.SourceSequence->GetSkeleton())
	{
		Run.Errors.Add(TEXT("AM_IG_PowderVortex uses a different Skeleton; preserving the existing asset."));
		return false;
	}
	const int32 PopulatedTrackIndex = FindUniquePopulatedSlotTrackIndex(Run.Montage);
	if (PopulatedTrackIndex == INDEX_NONE
		|| Run.Montage->SlotAnimTracks[PopulatedTrackIndex].AnimTrack.AnimSegments.Num() != 1
		|| Run.Montage->CompositeSections.Num() != 1)
	{
		Run.Errors.Add(TEXT("AM_IG_PowderVortex has a non-canonical slot/segment/section layout; preserving it for review."));
		return false;
	}
	if (Run.Montage->SlotAnimTracks[PopulatedTrackIndex].SlotName != FName(SlotName))
	{
		const FName CurrentSlot = Run.Montage->SlotAnimTracks[PopulatedTrackIndex].SlotName;
		if (CurrentSlot != FName(LegacySlotName) || !Run.bApply)
		{
			Run.Errors.Add(FString::Printf(
				TEXT("AM_IG_PowderVortex uses slot %s; expected %s. Use -SetSlotOnly -Apply for the narrow slot migration."),
				*CurrentSlot.ToString(), SlotName));
			return false;
		}
		Run.Montage->SlotAnimTracks[PopulatedTrackIndex].SlotName = FName(SlotName);
		Run.Montage->MarkPackageDirty();
		Run.ChangedAssets.AddUnique(Run.Montage->GetPathName());
	}
	const FAnimSegment& Segment = Run.Montage->SlotAnimTracks[PopulatedTrackIndex].AnimTrack.AnimSegments[0];
	const FCompositeSection& Section = Run.Montage->CompositeSections[0];
	if (Segment.GetAnimReference() != Run.SourceSequence
		|| !FMath::IsNearlyEqual(Segment.StartPos, 0.0f)
		|| !FMath::IsNearlyEqual(Segment.AnimStartTime, 0.0f)
		|| !FMath::IsNearlyEqual(Segment.AnimEndTime, Run.SourceSequence->GetPlayLength())
		|| !FMath::IsNearlyEqual(Segment.AnimPlayRate, 1.0f)
		|| Segment.LoopingCount != 1
		|| Section.SectionName != FName(SectionName)
		|| !FMath::IsNearlyEqual(Section.GetTime(), 0.0f)
		|| !Section.NextSectionName.IsNone())
	{
		Run.Errors.Add(TEXT("AM_IG_PowderVortex sequence, section, or gather-duration layout differs from the M6.3 contract."));
		return false;
	}

	const FName TrackName(NotifyTrackName);
	if (!UAnimationBlueprintLibrary::IsValidAnimNotifyTrackName(Run.Montage, TrackName))
	{
		if (!Run.bApply)
		{
			Run.Errors.Add(TEXT("AM_IG_PowderVortex is missing its Action notify track."));
			return false;
		}
		UAnimationBlueprintLibrary::AddAnimationNotifyTrack(Run.Montage,
			TrackName, FLinearColor(0.95f, 0.5f, 0.1f));
		if (UAnimationBlueprintLibrary::IsValidAnimNotifyTrackName(Run.Montage, TrackName))
		{
			Run.ChangedAssets.AddUnique(Run.Montage->GetPathName());
		}
		else
		{
			Run.Errors.Add(TEXT("Could not create the Action notify track."));
			return false;
		}
	}

	TArray<const FAnimNotifyEvent*> GatherNotifies;
	for (const FAnimNotifyEvent& Event : Run.Montage->Notifies)
	{
		if ((Event.Notify && Event.Notify->IsA(UAnimNotify_IG_PowderGatherBegin::StaticClass()))
			|| (Event.NotifyStateClass
				&& Event.NotifyStateClass->IsA(UAnimNotify_IG_PowderGatherBegin::StaticClass())))
		{
			GatherNotifies.Add(&Event);
		}
	}
	if (GatherNotifies.IsEmpty() && Run.bApply)
	{
		UAnimNotify* Added = UAnimationBlueprintLibrary::AddAnimationNotifyEvent(
			Run.Montage, TrackName, NotifyTime,
			UAnimNotify_IG_PowderGatherBegin::StaticClass());
		if (!Added)
		{
			Run.Errors.Add(TEXT("Could not add IG Powder Gather Begin to the Action track."));
			return false;
		}
		Run.ChangedAssets.AddUnique(Run.Montage->GetPathName());
		GatherNotifies.Reset();
		for (const FAnimNotifyEvent& Event : Run.Montage->Notifies)
		{
			if (Event.Notify && Event.Notify->IsA(UAnimNotify_IG_PowderGatherBegin::StaticClass()))
			{
				GatherNotifies.Add(&Event);
			}
		}
	}
	if (GatherNotifies.Num() != 1 || !GatherNotifies[0]->Notify
		|| GatherNotifies[0]->TrackIndex < 0
		|| !Run.Montage->AnimNotifyTracks.IsValidIndex(GatherNotifies[0]->TrackIndex)
		|| Run.Montage->AnimNotifyTracks[GatherNotifies[0]->TrackIndex].TrackName != TrackName
		|| FMath::Abs(GatherNotifies[0]->GetTime() - NotifyTime) > 0.001f)
	{
		Run.Errors.Add(TEXT("AM_IG_PowderVortex must contain exactly one Action-track IG Powder Gather Begin point notify at 0s; Resource timer continues after Montage end."));
		return false;
	}
	if (Run.bApply && Run.ChangedAssets.Contains(Run.Montage->GetPathName()))
	{
		Run.Montage->RefreshCacheData();
	}
	return true;
}

bool EnsureAbility(FRun& Run)
{
	const FString Path = ObjectPath(AbilityPackage, AbilityName);
	Run.AbilityBlueprint = LoadObject<UBlueprint>(nullptr, *Path, nullptr, LOAD_NoWarn);
	UObject* Existing = LoadObjectByPath(Path);
	if (Existing && !Run.AbilityBlueprint)
	{
		Run.Errors.Add(TEXT("GA_IG_PowderVortex path contains an incompatible asset class."));
		return false;
	}
	if (Run.AbilityBlueprint && (!Run.AbilityBlueprint->GeneratedClass
		|| !Run.AbilityBlueprint->GeneratedClass->IsChildOf(
			UMHGZPowderVortexAbility::StaticClass())))
	{
		Run.Errors.Add(TEXT("Existing GA_IG_PowderVortex does not derive from UMHGZPowderVortexAbility."));
		return false;
	}
	if (!Run.AbilityBlueprint && !Run.bApply)
	{
		Run.Errors.Add(TEXT("GA_IG_PowderVortex is missing; rerun with -Apply to create the persistent Blueprint."));
		return false;
	}
	if (!Run.AbilityBlueprint)
	{
		UPackage* Package = CreatePackage(AbilityPackage);
		Run.AbilityBlueprint = Package ? FKismetEditorUtilities::CreateBlueprint(
			UMHGZPowderVortexAbility::StaticClass(), Package, FName(AbilityName),
			BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(),
			FName(TEXT("MHGZM63PowderVortexAssetSetup"))) : nullptr;
		if (!Run.AbilityBlueprint || !Run.AbilityBlueprint->GeneratedClass)
		{
			Run.Errors.Add(TEXT("Could not create GA_IG_PowderVortex."));
			return false;
		}
		FAssetRegistryModule::AssetCreated(Run.AbilityBlueprint);
		Run.ChangedAssets.Add(Path);
	}

	Run.Ability = Run.AbilityBlueprint->GeneratedClass
		? Cast<UMHGZPowderVortexAbility>(
			Run.AbilityBlueprint->GeneratedClass->GetDefaultObject()) : nullptr;
	if (!Run.Ability)
	{
		Run.Errors.Add(TEXT("GA_IG_PowderVortex has no valid generated ability CDO."));
		return false;
	}
	const FGameplayTag ExpectedInput = RequestTag(TEXT("Input.Weapon.LTYB"));
	if (!ExpectedInput.IsValid())
	{
		Run.Errors.Add(TEXT("Input.Weapon.LTYB is not registered."));
		return false;
	}
	const bool bNeedsUpdate = Run.Ability->InputTag != ExpectedInput
		|| Run.Ability->AttackMontage != Run.Montage
		|| Run.Ability->DefaultEntrySection != FName(SectionName)
		|| !FMath::IsNearlyZero(Run.Ability->MaxCorrectionAngle)
		|| !Run.Ability->AttackSegments.IsEmpty()
		|| !Run.Ability->WeaponResourceCosts.IsEmpty();
	if (bNeedsUpdate && !Run.bApply)
	{
		Run.Errors.Add(TEXT("GA_IG_PowderVortex CDO differs from the M6.3 asset contract."));
		return false;
	}
	if (bNeedsUpdate)
	{
		Run.AbilityBlueprint->Modify();
		Run.Ability->Modify();
		Run.Ability->InputTag = ExpectedInput;
		Run.Ability->AttackMontage = Run.Montage;
		Run.Ability->DefaultEntrySection = FName(SectionName);
		Run.Ability->MaxCorrectionAngle = 0.0f;
		Run.Ability->AttackSegments.Reset();
		Run.Ability->WeaponResourceCosts.Reset();
		FKismetEditorUtilities::CompileBlueprint(Run.AbilityBlueprint,
			EBlueprintCompileOptions::SkipSave
			| EBlueprintCompileOptions::SkipDefaultObjectValidation
			| EBlueprintCompileOptions::SkipGarbageCollection);
		if (Run.AbilityBlueprint->Status == BS_Error
			|| !Run.AbilityBlueprint->GeneratedClass)
		{
			Run.Errors.Add(TEXT("GA_IG_PowderVortex Blueprint failed to compile."));
			return false;
		}
		Run.Ability = Cast<UMHGZPowderVortexAbility>(
			Run.AbilityBlueprint->GeneratedClass->GetDefaultObject());
		Run.ChangedAssets.AddUnique(Run.AbilityBlueprint->GetPathName());
	}
	return Run.Ability
		&& Run.Ability->InputTag == ExpectedInput
		&& Run.Ability->AttackMontage == Run.Montage
		&& Run.Ability->DefaultEntrySection == FName(SectionName)
		&& FMath::IsNearlyZero(Run.Ability->MaxCorrectionAngle)
		&& Run.Ability->AttackSegments.IsEmpty()
		&& Run.Ability->WeaponResourceCosts.IsEmpty();
}

FComboTransition BuildExpectedTransition(UClass* AbilityClass)
{
	FComboTransition Transition;
	Transition.TransitionID = FName(TransitionID);
	Transition.SourceState = FName(TEXT("Idle"));
	Transition.InputTag = RequestTag(TEXT("Input.Weapon.LTYB"));
	Transition.ExecutionPolicy = EComboExecutionPolicy::ActivateAbility;
	Transition.AbilityClass = AbilityClass;
	Transition.TargetState = FName(TargetState);
	Transition.StatePolicy = EComboStatePolicy::Replace;
	Transition.LandingPolicy = EComboLandingPolicy::ResetToIdle;
	Transition.RequiredTags.AddTag(RequestTag(TEXT("Combat.State.Grounded")));
	Transition.RequiredTags.AddTag(RequestTag(TEXT("Combat.State.Unsheathed")));
	Transition.BlockedTags.AddTag(RequestTag(TEXT("Combat.State.Hitstun")));
	Transition.GrantedTags.AddTag(RequestTag(TEXT("Combat.State.Attacking")));
	Transition.bRequiresComboWindow = false;
	Transition.bRequiresDodgeAcceptWindow = false;
	Transition.bAutoTransition = false;
	Transition.MaxCorrectionAngle = -1.0f;
	Transition.Priority = 0;
	return Transition;
}

bool TransitionMatches(const FComboTransition& Actual,
	const FComboTransition& Expected)
{
	return FComboTransition::StaticStruct()->CompareScriptStruct(
		&Actual, &Expected, 0);
}

bool EnsureComboEdge(FRun& Run)
{
	if (!Run.Combo || !Run.AbilityBlueprint || !Run.AbilityBlueprint->GeneratedClass)
	{
		Run.Errors.Add(TEXT("Cannot create the Powder Vortex Combo edge without DA_IG_Combo and its generated GA class."));
		return false;
	}
	const FComboTransition Expected = BuildExpectedTransition(
		Run.AbilityBlueprint->GeneratedClass);
	if (!Expected.InputTag.IsValid() || !Expected.RequiredTags.HasTagExact(
		RequestTag(TEXT("Combat.State.Grounded")))
		|| !Expected.RequiredTags.HasTagExact(RequestTag(TEXT("Combat.State.Unsheathed")))
		|| !Expected.BlockedTags.HasTagExact(RequestTag(TEXT("Combat.State.Hitstun")))
		|| !Expected.GrantedTags.HasTagExact(RequestTag(TEXT("Combat.State.Attacking"))))
	{
		Run.Errors.Add(TEXT("One or more Powder Vortex Combo Gameplay Tags are not registered."));
		return false;
	}
	TArray<int32> Matches;
	for (int32 Index = 0; Index < Run.Combo->Transitions.Num(); ++Index)
	{
		if (Run.Combo->Transitions[Index].TransitionID == FName(TransitionID))
		{
			Matches.Add(Index);
		}
	}
	if (Matches.Num() > 1)
	{
		Run.Errors.Add(TEXT("DA_IG_Combo contains duplicate Idle_LTYB_PowderVortex IDs; preserving the asset."));
		return false;
	}
	if (Matches.IsEmpty())
	{
		if (!Run.bApply)
		{
			Run.Errors.Add(TEXT("DA_IG_Combo is missing Idle_LTYB_PowderVortex; rerun with -Apply to add the edge."));
			return false;
		}
		Run.Combo->Modify();
		Run.Combo->Transitions.Add(Expected);
		Run.ChangedAssets.AddUnique(Run.Combo->GetPathName());
		return true;
	}
	FComboTransition& Existing = Run.Combo->Transitions[Matches[0]];
	if (TransitionMatches(Existing, Expected))
	{
		return true;
	}
	if (!Run.bApply)
	{
		Run.Errors.Add(TEXT("Existing Idle_LTYB_PowderVortex Combo edge differs from the M6.3 contract."));
		return false;
	}
	Run.Combo->Modify();
	Existing = Expected;
	Run.ChangedAssets.AddUnique(Run.Combo->GetPathName());
	return true;
}

bool SaveChangedAssets(FRun& Run)
{
	if (!Run.Errors.IsEmpty())
	{
		return false;
	}
	UObject* Assets[] = { Run.Montage, Run.AbilityBlueprint, Run.Combo,
		Run.InputProfile };
	for (UObject* Asset : Assets)
	{
		if (Asset && Run.ChangedAssets.Contains(Asset->GetPathName())
			&& !SaveAsset(*Asset))
		{
			Run.Errors.Add(TEXT("Failed to save M6.3 asset: ") + Asset->GetPathName());
			return false;
		}
	}
	return Run.Errors.IsEmpty();
}

bool WriteReport(const FRun& Run, const bool bSuccess)
{
	const FString Path = FPaths::Combine(FPaths::ProjectDir(), ReportRelativePath);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("mode"), Run.bApply ? TEXT("Apply") : TEXT("AuditOnly"));
	Root->SetBoolField(TEXT("success"), bSuccess);
	Root->SetStringField(TEXT("source_sequence"), SourceSequencePath);
	Root->SetStringField(TEXT("source_skeleton"), ExpectedSkeletonPath);
	Root->SetNumberField(TEXT("source_rate_scale"), Run.SourceSequence
		? Run.SourceSequence->RateScale : 0.0);
	Root->SetStringField(TEXT("montage_skeleton"), Run.Montage
		? GetPathNameSafe(Run.Montage->GetSkeleton()) : TEXT("None"));
	Root->SetNumberField(TEXT("montage_duration_seconds"), Run.Montage
		? Run.Montage->GetPlayLength() : 0.0);
	if (Run.Montage)
	{
		TArray<TSharedPtr<FJsonValue>> Slots;
		for (const FSlotAnimationTrack& Slot : Run.Montage->SlotAnimTracks)
		{
			TSharedRef<FJsonObject> SlotJson = MakeShared<FJsonObject>();
			SlotJson->SetStringField(TEXT("slot_name"), Slot.SlotName.ToString());
			TArray<TSharedPtr<FJsonValue>> Segments;
			for (const FAnimSegment& Segment : Slot.AnimTrack.AnimSegments)
			{
				TSharedRef<FJsonObject> SegmentJson = MakeShared<FJsonObject>();
				SegmentJson->SetStringField(TEXT("sequence"),
					GetPathNameSafe(Segment.GetAnimReference()));
				SegmentJson->SetNumberField(TEXT("source_start_seconds"), Segment.AnimStartTime);
				SegmentJson->SetNumberField(TEXT("source_end_seconds"), Segment.AnimEndTime);
				SegmentJson->SetNumberField(TEXT("segment_play_rate"), Segment.AnimPlayRate);
				Segments.Add(MakeShared<FJsonValueObject>(SegmentJson));
			}
			SlotJson->SetArrayField(TEXT("segments"), Segments);
			Slots.Add(MakeShared<FJsonValueObject>(SlotJson));
		}
		Root->SetArrayField(TEXT("slots"), Slots);

		TArray<TSharedPtr<FJsonValue>> Sections;
		for (const FCompositeSection& Section : Run.Montage->CompositeSections)
		{
			TSharedRef<FJsonObject> SectionJson = MakeShared<FJsonObject>();
			SectionJson->SetStringField(TEXT("name"), Section.SectionName.ToString());
			SectionJson->SetNumberField(TEXT("time_seconds"), Section.GetTime());
			SectionJson->SetStringField(TEXT("next_section"),
				Section.NextSectionName.IsNone() ? TEXT("None") : *Section.NextSectionName.ToString());
			Sections.Add(MakeShared<FJsonValueObject>(SectionJson));
		}
		Root->SetArrayField(TEXT("sections"), Sections);

		TArray<TSharedPtr<FJsonValue>> Notifies;
		for (const FAnimNotifyEvent& Event : Run.Montage->Notifies)
		{
			TSharedRef<FJsonObject> NotifyJson = MakeShared<FJsonObject>();
			const UObject* NotifyObject = Event.Notify
				? static_cast<const UObject*>(Event.Notify.Get())
				: static_cast<const UObject*>(Event.NotifyStateClass.Get());
			NotifyJson->SetStringField(TEXT("class"), GetPathNameSafe(NotifyObject));
			NotifyJson->SetNumberField(TEXT("time_seconds"), Event.GetTime());
			NotifyJson->SetNumberField(TEXT("trigger_time_seconds"), Event.GetTriggerTime());
			NotifyJson->SetStringField(TEXT("track"),
				Run.Montage->AnimNotifyTracks.IsValidIndex(Event.TrackIndex)
					? Run.Montage->AnimNotifyTracks[Event.TrackIndex].TrackName.ToString()
					: TEXT("InvalidTrack"));
			Notifies.Add(MakeShared<FJsonValueObject>(NotifyJson));
		}
		Root->SetArrayField(TEXT("notifies"), Notifies);
	}
	Root->SetNumberField(TEXT("post_arrival_delay_seconds"), Run.DelayAfterArrival);
	Root->SetStringField(TEXT("input_profile"), InputProfilePath);
	Root->SetStringField(TEXT("ltyb_aim_snapshot_context"), TEXT("Kinsect"));
	Root->SetStringField(TEXT("montage"), ObjectPath(MontagePackage, MontageName));
	Root->SetStringField(TEXT("ability"), ObjectPath(AbilityPackage, AbilityName));
	Root->SetStringField(TEXT("combo_transition_id"), TransitionID);
	if (Run.Ability)
	{
		Root->SetStringField(TEXT("ability_input_tag"), Run.Ability->InputTag.ToString());
		Root->SetStringField(TEXT("ability_entry_section"), Run.Ability->DefaultEntrySection.ToString());
		Root->SetNumberField(TEXT("ability_max_correction_angle"), Run.Ability->MaxCorrectionAngle);
		Root->SetNumberField(TEXT("ability_attack_segment_count"), Run.Ability->AttackSegments.Num());
		Root->SetNumberField(TEXT("ability_weapon_resource_cost_count"), Run.Ability->WeaponResourceCosts.Num());
	}
	if (Run.Combo)
	{
		const FComboTransition* Transition = Run.Combo->Transitions.FindByPredicate(
			[](const FComboTransition& Candidate)
			{ return Candidate.TransitionID == FName(TransitionID); });
		if (Transition)
		{
			TSharedRef<FJsonObject> EdgeJson = MakeShared<FJsonObject>();
			EdgeJson->SetStringField(TEXT("source_state"), Transition->SourceState.ToString());
			EdgeJson->SetStringField(TEXT("input_tag"), Transition->InputTag.ToString());
			EdgeJson->SetStringField(TEXT("ability_class"),
				Transition->AbilityClass ? Transition->AbilityClass->GetPathName() : TEXT("None"));
			EdgeJson->SetStringField(TEXT("target_state"), Transition->TargetState.ToString());
			EdgeJson->SetNumberField(TEXT("max_correction_angle"), Transition->MaxCorrectionAngle);
			EdgeJson->SetBoolField(TEXT("requires_combo_window"), Transition->bRequiresComboWindow);
			TArray<TSharedPtr<FJsonValue>> RequiredTags;
			for (const FGameplayTag& Tag : Transition->RequiredTags)
			{
				RequiredTags.Add(MakeShared<FJsonValueString>(Tag.ToString()));
			}
			EdgeJson->SetArrayField(TEXT("required_tags"), RequiredTags);
			TArray<TSharedPtr<FJsonValue>> BlockedTags;
			for (const FGameplayTag& Tag : Transition->BlockedTags)
			{
				BlockedTags.Add(MakeShared<FJsonValueString>(Tag.ToString()));
			}
			EdgeJson->SetArrayField(TEXT("blocked_tags"), BlockedTags);
			Root->SetObjectField(TEXT("combo_edge"), EdgeJson);
		}
	}
	TArray<TSharedPtr<FJsonValue>> Changed;
	for (const FString& Asset : Run.ChangedAssets)
	{
		Changed.Add(MakeShared<FJsonValueString>(Asset));
	}
	Root->SetArrayField(TEXT("changed_assets"), Changed);
	TArray<TSharedPtr<FJsonValue>> Errors;
	for (const FString& Error : Run.Errors)
	{
		Errors.Add(MakeShared<FJsonValueString>(Error));
	}
	Root->SetArrayField(TEXT("errors"), Errors);
	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	return FJsonSerializer::Serialize(Root, Writer)
		&& FFileHelper::SaveStringToFile(Json, *Path,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

bool Execute(FRun& Run)
{
	if (!LoadDependencies(Run))
	{
		return false;
	}
	bool bValid = EnsureMontage(Run);
	if (!bValid)
	{
		return false;
	}
	bValid &= EnsureAbility(Run);
	bValid &= EnsureComboEdge(Run);
	if (bValid && Run.bApply)
	{
		bValid &= SaveChangedAssets(Run);
	}
	return bValid && Run.Errors.IsEmpty();
}
}

#endif

UMHGZM63PowderVortexAssetSetupCommandlet::UMHGZM63PowderVortexAssetSetupCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMHGZM63PowderVortexAssetSetupCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	using namespace UE::MHGZ::M63PowderVortexSetup;
	if (FParse::Param(*Params, TEXT("SetSlotOnly")))
	{
		const bool bApplySlot = FParse::Param(*Params, TEXT("Apply"));
		const bool bSuccess = SetPowderVortexSlotOnly(bApplySlot);
		UE_LOG(LogTemp, Display,
			TEXT("[M63PowderVortexSlot] mode=%s success=%s"),
			bApplySlot ? TEXT("Apply") : TEXT("AuditOnly"),
			bSuccess ? TEXT("true") : TEXT("false"));
		return bSuccess ? 0 : 1;
	}
	FRun Run;
	Run.bApply = FParse::Param(*Params, TEXT("Apply"));
	const bool bSuccess = Execute(Run);
	for (const FString& Error : Run.Errors)
	{
		UE_LOG(LogTemp, Error, TEXT("[M63PowderVortexSetup] %s"), *Error);
	}
	UE_LOG(LogTemp, Display,
		TEXT("[M63PowderVortexSetup] mode=%s success=%s changed=%d errors=%d sequence=%s skeleton=%s length=%.6f postArrivalDelay=%.3f"),
		Run.bApply ? TEXT("Apply") : TEXT("AuditOnly"),
		bSuccess ? TEXT("true") : TEXT("false"), Run.ChangedAssets.Num(),
		Run.Errors.Num(), *GetPathNameSafe(Run.SourceSequence),
		Run.SourceSequence ? *GetPathNameSafe(Run.SourceSequence->GetSkeleton()) : TEXT("None"),
		Run.Montage ? Run.Montage->GetPlayLength() : 0.0f, Run.DelayAfterArrival);
	if (!WriteReport(Run, bSuccess))
	{
		UE_LOG(LogTemp, Error, TEXT("[M63PowderVortexSetup] Failed to write report: %s"), ReportRelativePath);
		return 1;
	}
	return bSuccess ? 0 : 1;
#else
	UE_LOG(LogTemp, Error, TEXT("[M63PowderVortexSetup] This commandlet requires an editor build."));
	return 1;
#endif
}
