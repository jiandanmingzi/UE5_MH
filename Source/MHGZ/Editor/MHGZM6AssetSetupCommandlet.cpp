// Copyright MHGZ Project. All Rights Reserved.

#include "Editor/MHGZM6AssetSetupCommandlet.h"

#if WITH_EDITOR

#include "ActionSystem/MHGZAwakenedKinsectAttackAbility.h"
#include "ActionSystem/AnimNotifyState_AttackCollision.h"
#include "ActionSystem/MHGZKinsectSlashAbility.h"
#include "ActionSystem/MHGZWeaponComboData.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AnimationBlueprintLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "InsectGlaive/InsectGlaiveCombatConfig.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/SavePackage.h"
#include "WeaponRuntime/MHGZWeaponInputProfile.h"

namespace UE::MHGZ::M6AssetSetup
{
constexpr TCHAR Source193Path[] = TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_JueChongJi.AS_Unsh_JueChongJi");
constexpr TCHAR Source195Path[] = TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_JueChongJi_Send.AS_Unsh_JueChongJi_Send");
constexpr TCHAR Source196Path[] = TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_JueChongJi_Pose.AS_Unsh_JueChongJi_Pose");
constexpr TCHAR Source199Path[] = TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_JueChongJi_Flying.AS_Unsh_JueChongJi_Flying");
constexpr TCHAR Source206Path[] = TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_LieChongHuaJueChongJiFall.AS_Unsh_LieChongHuaJueChongJiFall");

constexpr TCHAR MainMontagePackage[] = TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_JueChongJi");
constexpr TCHAR FallMontagePackage[] = TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_JueChongJi_Fall");
constexpr TCHAR LandingMontagePackage[] = TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_AerialLanding");
constexpr TCHAR AbilityPackage[] = TEXT("/Game/Blueprints/Ability/InsectGlaive/GA_IG_JueChongJi");
constexpr TCHAR KinsectSlashAbilityPackage[] = TEXT("/Game/Blueprints/Ability/InsectGlaive/GA_IG_CaoChongZhan");
constexpr TCHAR LegacyKinsectSlashAbilityPackage[] = TEXT("/Game/Weapons/InsectGlaive/Abilities/GA_IG_CaoChongZhan");
constexpr TCHAR InputProfilePath[] = TEXT("/Game/Weapons/InsectGlaive/Data/DA_IG_InputProfile.DA_IG_InputProfile");
constexpr TCHAR ComboPath[] = TEXT("/Game/Weapons/InsectGlaive/Data/DA_IG_Combo.DA_IG_Combo");
constexpr TCHAR CombatPath[] = TEXT("/Game/Weapons/InsectGlaive/Data/DA_IG_Combat.DA_IG_Combat");
constexpr TCHAR AuditRelativePath[] = TEXT("Saved/M6Audit/m6_awakened_asset_setup_report.json");

constexpr float HoldLoopStartFrameCandidate = 220.0f;
constexpr float SourceRate = 60.0f;
// F9 capture mhrise_20261006_205745_02_samples.csv:
// action 196 at 55.05007227s, first action 199 sample at 55.05886133s.
// Use the measured one-sample interval as the 196 montage span.
constexpr float RecordedPreFlight196DurationSeconds = 0.00878906f;
constexpr float AwakenedActivationCorrectionAngle = 90.0f;
const FName SlotName(TEXT("DefaultSlot"));
const FName StartupSection(TEXT("Startup193"));
const FName HoldLoopSection(TEXT("Hold193Loop"));
const FName SendSection(TEXT("Send195"));
const FName PreFlightSection(TEXT("PreFlight196"));
const FName HunterFlightSection(TEXT("HunterFlight199"));
const FName FallSection(TEXT("Fall206"));
const FName TransitionID(TEXT("Idle_RTYB_AwakenedKinsectAttack"));
const FName TargetState(TEXT("IG.Special.AwakenedKinsectAttack"));

const TCHAR* MainTrackNames[] = { TEXT("Kinsect"), TEXT("Action"), TEXT("IGAerialHandoff") };
const TCHAR* FallTrackNames[] = { TEXT("Action") };

float SequenceMontageLength(const UAnimSequence& Sequence)
{
	const float EffectiveRate = FMath::Abs(Sequence.RateScale);
	return EffectiveRate > KINDA_SMALL_NUMBER
		? Sequence.GetPlayLength() / EffectiveRate
		: TNumericLimits<float>::Max();
}

float SourceTimeOnMontage(const UAnimSequence& Sequence, const float SourceTime)
{
	const float EffectiveRate = FMath::Abs(Sequence.RateScale);
	return EffectiveRate > KINDA_SMALL_NUMBER
		? SourceTime / EffectiveRate
		: TNumericLimits<float>::Max();
}

struct FRun
{
	bool bApply = false;
	bool bInputProfileOnly = false;
	bool bCorrectionOnly = false;
	bool bLandingOnly = false;
	TArray<FString> Errors;
	TArray<FString> ChangedAssets;
	UAnimSequence* Sequence193 = nullptr;
	UAnimSequence* Sequence195 = nullptr;
	UAnimSequence* Sequence196 = nullptr;
	UAnimSequence* Sequence199 = nullptr;
	UAnimSequence* Sequence206 = nullptr;
	UAnimMontage* MainMontage = nullptr;
	UAnimMontage* FallMontage = nullptr;
	UAnimMontage* LandingMontage = nullptr;
	UBlueprint* AbilityBlueprint = nullptr;
	UMHGZAwakenedKinsectAttackAbility* Ability = nullptr;
	UMHGZKinsectSlashAbility* KinsectSlashAbility = nullptr;
	UWeaponInputProfile* InputProfile = nullptr;
	UMHGZWeaponComboData* Combo = nullptr;
	UInsectGlaiveCombatConfig* Combat = nullptr;
};

FString ObjectPath(const TCHAR* Package, const TCHAR* Name)
{
	return FString::Printf(TEXT("%s.%s"), Package, Name);
}

FGameplayTag Tag(const TCHAR* Name)
{
	return FGameplayTag::RequestGameplayTag(FName(Name), false);
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

bool LoadDependencies(FRun& Run)
{
	Run.Sequence193 = LoadObject<UAnimSequence>(nullptr, Source193Path, nullptr, LOAD_NoWarn);
	Run.Sequence195 = LoadObject<UAnimSequence>(nullptr, Source195Path, nullptr, LOAD_NoWarn);
	Run.Sequence196 = LoadObject<UAnimSequence>(nullptr, Source196Path, nullptr, LOAD_NoWarn);
	Run.Sequence199 = LoadObject<UAnimSequence>(nullptr, Source199Path, nullptr, LOAD_NoWarn);
	Run.Sequence206 = LoadObject<UAnimSequence>(nullptr, Source206Path, nullptr, LOAD_NoWarn);
	Run.InputProfile = LoadObject<UWeaponInputProfile>(nullptr, InputProfilePath, nullptr, LOAD_NoWarn);
	Run.Combo = LoadObject<UMHGZWeaponComboData>(nullptr, ComboPath, nullptr, LOAD_NoWarn);
	Run.Combat = LoadObject<UInsectGlaiveCombatConfig>(nullptr, CombatPath, nullptr, LOAD_NoWarn);
	// Asset migration moved the GA under Blueprints/Ability. Keep the older
	// package path as a read-only compatibility fallback, matching the M5 audit.
	const TCHAR* KinsectSlashPackage = FPackageName::DoesPackageExist(
		KinsectSlashAbilityPackage) ? KinsectSlashAbilityPackage
		: FPackageName::DoesPackageExist(LegacyKinsectSlashAbilityPackage)
			? LegacyKinsectSlashAbilityPackage : nullptr;
	const FString KinsectSlashObjectPath = KinsectSlashPackage
		? FString::Printf(TEXT("%s.GA_IG_CaoChongZhan"), KinsectSlashPackage)
		: FString();
	UBlueprint* KinsectSlashBlueprint = KinsectSlashPackage
		? LoadObject<UBlueprint>(nullptr, *KinsectSlashObjectPath, nullptr, LOAD_NoWarn)
		: nullptr;
	Run.KinsectSlashAbility = KinsectSlashBlueprint
		&& KinsectSlashBlueprint->GeneratedClass
		? Cast<UMHGZKinsectSlashAbility>(
			KinsectSlashBlueprint->GeneratedClass->GetDefaultObject())
		: nullptr;

	const UAnimSequence* Sequences[] = {
		Run.Sequence193, Run.Sequence195, Run.Sequence196, Run.Sequence199, Run.Sequence206 };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Sequences); ++Index)
	{
		if (!Sequences[Index])
		{
			Run.Errors.Add(FString::Printf(TEXT("Required source animation %d could not be loaded."), Index));
			continue;
		}
		if (!FMath::IsFinite(Sequences[Index]->RateScale)
			|| FMath::IsNearlyZero(Sequences[Index]->RateScale))
		{
			Run.Errors.Add(FString::Printf(TEXT("Required source animation %d has an invalid RateScale."), Index));
		}
	}
	if (Run.Sequence193 && (Run.Sequence193->GetSkeleton() == nullptr
		|| HoldLoopStartFrameCandidate / SourceRate >= Run.Sequence193->GetPlayLength()))
	{
		Run.Errors.Add(TEXT("193 source is missing a Skeleton or does not contain the frame-220 loop candidate."));
	}
	if (Run.Sequence193 && Run.Sequence195 && Run.Sequence196 && Run.Sequence199
		&& (Run.Sequence193->GetSkeleton() != Run.Sequence195->GetSkeleton()
			|| Run.Sequence193->GetSkeleton() != Run.Sequence196->GetSkeleton()
			|| Run.Sequence193->GetSkeleton() != Run.Sequence199->GetSkeleton()))
	{
		Run.Errors.Add(TEXT("193/195/196/199 source animations do not share one Skeleton."));
	}
	if (Run.Sequence193 && Run.Sequence206
		&& Run.Sequence193->GetSkeleton() != Run.Sequence206->GetSkeleton())
	{
		Run.Errors.Add(TEXT("206 falling animation Skeleton differs from the attack Montage Skeleton."));
	}
	if (!Run.InputProfile || !Run.Combo || !Run.Combat
		|| !Run.KinsectSlashAbility)
	{
		Run.Errors.Add(TEXT("DA_IG_InputProfile, DA_IG_Combo, DA_IG_Combat, or the audited GA_IG_CaoChongZhan 189 source could not be loaded."));
	}
	return Run.Errors.IsEmpty();
}

UObject* LoadObjectByPath(const FString& Path)
{
	return StaticLoadObject(UObject::StaticClass(), nullptr, *Path, nullptr, LOAD_NoWarn);
}

UAnimMontage* GetOrCreateMontage(FRun& Run, const TCHAR* Package,
	const TCHAR* Name, USkeleton* Skeleton)
{
	const FString Path = ObjectPath(Package, Name);
	UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *Path, nullptr, LOAD_NoWarn);
	UObject* Existing = LoadObjectByPath(Path);
	if (Existing && !Montage)
	{
		Run.Errors.Add(TEXT("Montage path exists with an incompatible asset class: ") + Path);
		return nullptr;
	}
	if (!Montage && Run.bApply)
	{
		UPackage* AssetPackage = CreatePackage(Package);
		Montage = AssetPackage
			? NewObject<UAnimMontage>(AssetPackage, FName(Name), RF_Public | RF_Standalone)
			: nullptr;
		if (!Montage)
		{
			Run.Errors.Add(TEXT("Could not create Montage: ") + Path);
			return nullptr;
		}
		Montage->SetSkeleton(Skeleton);
		FAssetRegistryModule::AssetCreated(Montage);
		Run.ChangedAssets.Add(Path);
	}
	else if (!Montage)
	{
		Run.Errors.Add(TEXT("Missing persistent Montage: ") + Path);
	}
	if (Montage && Montage->GetSkeleton() != Skeleton)
	{
		Run.Errors.Add(TEXT("Existing Montage uses a different Skeleton: ") + Path);
		return nullptr;
	}
	return Montage;
}

void AddSegment(UAnimMontage& Montage, UAnimSequence& Sequence,
	const float TimelineStart, const float SourceStart, const float SourceEnd)
{
	FAnimSegment& Segment = Montage.SlotAnimTracks[0].AnimTrack.AnimSegments.AddDefaulted_GetRef();
	Segment.SetAnimReference(&Sequence, true);
	Segment.StartPos = TimelineStart;
	Segment.AnimStartTime = SourceStart;
	Segment.AnimEndTime = SourceEnd;
	Segment.AnimPlayRate = 1.0f;
	Segment.LoopingCount = 1;
}

void AddSection(UAnimMontage& Montage, const FName Name, const float Time,
	const FName NextSection)
{
	FCompositeSection& Section = Montage.CompositeSections.AddDefaulted_GetRef();
	Section.SectionName = Name;
	Section.Link(&Montage, Time, 0);
	Section.SetTime(Time);
	Section.NextSectionName = NextSection;
}

bool HasTrack(const UAnimMontage& Montage, const FName TrackName)
{
	return Montage.AnimNotifyTracks.ContainsByPredicate([TrackName](const FAnimNotifyTrack& Track)
	{
		return Track.TrackName == TrackName;
	});
}

bool EnsureTracks(FRun& Run, UAnimMontage& Montage,
	const TCHAR* const* TrackNames, const int32 TrackCount)
{
	for (int32 Index = 0; Index < TrackCount; ++Index)
	{
		const FName Name(TrackNames[Index]);
		if (HasTrack(Montage, Name))
		{
			continue;
		}
		if (!Run.bApply)
		{
			Run.Errors.Add(FString::Printf(TEXT("%s is missing notify track '%s'."),
				*Montage.GetPathName(), TrackNames[Index]));
			continue;
		}
		Montage.Modify();
		const FLinearColor Color = Index % 3 == 0 ? FLinearColor(0.85f, 0.2f, 0.2f)
			: Index % 3 == 1 ? FLinearColor(0.2f, 0.65f, 0.95f)
			: FLinearColor(0.25f, 0.8f, 0.35f);
		UAnimationBlueprintLibrary::AddAnimationNotifyTrack(&Montage, Name, Color);
		if (HasTrack(Montage, Name))
		{
			Run.ChangedAssets.AddUnique(Montage.GetPathName());
		}
		else
		{
			Run.Errors.Add(FString::Printf(TEXT("Could not create notify track '%s' on %s."),
				TrackNames[Index], *Montage.GetPathName()));
		}
	}
	return Run.Errors.IsEmpty();
}

bool MontageLayoutMatches(const UAnimMontage& Montage, const bool bFall,
	const FRun& Run)
{
	if (Montage.SlotAnimTracks.Num() != 1 || Montage.SlotAnimTracks[0].SlotName != SlotName)
	{
		return false;
	}
	const TArray<FAnimSegment>& Segments = Montage.SlotAnimTracks[0].AnimTrack.AnimSegments;
	if (bFall)
	{
		if (Segments.Num() != 1 || Segments[0].GetAnimReference() != Run.Sequence206
			|| !FMath::IsNearlyEqual(Segments[0].StartPos, 0.0f)
			|| !FMath::IsNearlyEqual(Segments[0].AnimStartTime, 0.0f)
			|| !FMath::IsNearlyEqual(Segments[0].AnimEndTime, Run.Sequence206->GetPlayLength())
			|| !FMath::IsNearlyEqual(Segments[0].AnimPlayRate, 1.0f)
			|| Segments[0].LoopingCount != 1 || Montage.CompositeSections.Num() != 1)
		{
			return false;
		}
		const FCompositeSection& Section = Montage.CompositeSections[0];
		return Section.SectionName == FallSection
			&& FMath::IsNearlyEqual(Section.GetTime(), 0.0f)
			&& Section.NextSectionName.IsNone();
	}
	if (Segments.Num() != 5 || Montage.CompositeSections.Num() != 5)
	{
		return false;
	}
	const UAnimSequence* ExpectedSequences[] = {
		Run.Sequence193, Run.Sequence193, Run.Sequence195, Run.Sequence196, Run.Sequence199 };
	const float CandidateTime = HoldLoopStartFrameCandidate / SourceRate;
	const float CandidateMontageTime = SourceTimeOnMontage(*Run.Sequence193, CandidateTime);
	const float Sequence193MontageLength = SequenceMontageLength(*Run.Sequence193);
	const float Sequence195MontageLength = SequenceMontageLength(*Run.Sequence195);
	const float Sequence196SourceEndTime = Run.Sequence196->RateScale
		* RecordedPreFlight196DurationSeconds;
	const float HunterFlightMontageStart = Sequence193MontageLength
		+ Sequence195MontageLength + RecordedPreFlight196DurationSeconds;
	const float ExpectedStartPos[] = {
		0.0f, CandidateMontageTime, Sequence193MontageLength,
		Sequence193MontageLength + Sequence195MontageLength,
		HunterFlightMontageStart };
	const float ExpectedAnimStart[] = { 0.0f, CandidateTime, 0.0f, 0.0f, 0.0f };
	const float ExpectedAnimEnd[] = { CandidateTime, Run.Sequence193->GetPlayLength(),
		Run.Sequence195->GetPlayLength(), Sequence196SourceEndTime,
		Run.Sequence199->GetPlayLength() };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(ExpectedSequences); ++Index)
	{
		if (Segments[Index].GetAnimReference() != ExpectedSequences[Index]
			|| !FMath::IsNearlyEqual(Segments[Index].StartPos, ExpectedStartPos[Index])
			|| !FMath::IsNearlyEqual(Segments[Index].AnimStartTime, ExpectedAnimStart[Index])
			|| !FMath::IsNearlyEqual(Segments[Index].AnimEndTime, ExpectedAnimEnd[Index])
			|| !FMath::IsNearlyEqual(Segments[Index].AnimPlayRate, 1.0f)
			|| Segments[Index].LoopingCount != 1)
		{
			return false;
		}
	}
	const FName ExpectedSections[] = {
		StartupSection, HoldLoopSection, SendSection, PreFlightSection, HunterFlightSection };
	const float ExpectedSectionTimes[] = { 0.0f, CandidateMontageTime,
		Sequence193MontageLength,
		Sequence193MontageLength + Sequence195MontageLength,
		HunterFlightMontageStart };
	const FName ExpectedNextSections[] = {
		HoldLoopSection, HoldLoopSection, PreFlightSection, HunterFlightSection, NAME_None };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(ExpectedSections); ++Index)
	{
		const FCompositeSection* Section = Montage.CompositeSections.FindByPredicate(
			[&ExpectedSections, Index](const FCompositeSection& Candidate)
			{ return Candidate.SectionName == ExpectedSections[Index]; });
		if (!Section || !FMath::IsNearlyEqual(Section->GetTime(), ExpectedSectionTimes[Index])
			|| Section->NextSectionName != ExpectedNextSections[Index])
		{
			return false;
		}
	}
	return true;
}

bool ConfigureMainMontage(FRun& Run)
{
	if (!Run.Sequence193 || !Run.Sequence195 || !Run.Sequence196 || !Run.Sequence199)
	{
		return false;
	}
	Run.MainMontage = GetOrCreateMontage(Run, MainMontagePackage,
		TEXT("AM_IG_JueChongJi"), Run.Sequence193->GetSkeleton());
	if (!Run.MainMontage)
	{
		return false;
	}

	const bool bCreated = Run.ChangedAssets.Contains(Run.MainMontage->GetPathName());
	if (!MontageLayoutMatches(*Run.MainMontage, false, Run))
	{
		if (!bCreated)
		{
			if (!Run.bApply)
			{
				Run.Errors.Add(TEXT("Awakened Kinsect attack Montage is absent or differs from the generated candidate layout; no asset was changed."));
			}
			else
			{
				Run.Errors.Add(TEXT("Existing Awakened Kinsect attack Montage has a non-canonical layout; preserving it for review."));
			}
			return false;
		}
		UAnimMontage& Montage = *Run.MainMontage;
		Montage.Modify();
		Montage.SlotAnimTracks.Reset();
		Montage.CompositeSections.Reset();
		FSlotAnimationTrack& Slot = Montage.SlotAnimTracks.AddDefaulted_GetRef();
		Slot.SlotName = SlotName;

		const float CandidateSourceTime = HoldLoopStartFrameCandidate / SourceRate;
		const float CandidateMontageTime = SourceTimeOnMontage(*Run.Sequence193, CandidateSourceTime);
		AddSegment(Montage, *Run.Sequence193, 0.0f, 0.0f, CandidateSourceTime);
		AddSegment(Montage, *Run.Sequence193, CandidateMontageTime, CandidateSourceTime,
			Run.Sequence193->GetPlayLength());
		const float SendStart = SequenceMontageLength(*Run.Sequence193);
		const float PreFlightStart = SendStart + SequenceMontageLength(*Run.Sequence195);
		const float HunterFlightStart = PreFlightStart + RecordedPreFlight196DurationSeconds;
		AddSegment(Montage, *Run.Sequence195, SendStart, 0.0f, Run.Sequence195->GetPlayLength());
		AddSegment(Montage, *Run.Sequence196, PreFlightStart, 0.0f,
			Run.Sequence196->RateScale * RecordedPreFlight196DurationSeconds);
		AddSegment(Montage, *Run.Sequence199, HunterFlightStart, 0.0f, Run.Sequence199->GetPlayLength());

		AddSection(Montage, StartupSection, 0.0f, HoldLoopSection);
		AddSection(Montage, HoldLoopSection, CandidateMontageTime, HoldLoopSection);
		AddSection(Montage, SendSection, SendStart, PreFlightSection);
		AddSection(Montage, PreFlightSection, PreFlightStart, HunterFlightSection);
		AddSection(Montage, HunterFlightSection, HunterFlightStart, NAME_None);
		const float Length = Montage.CalculateSequenceLength();
		if (!FMath::IsFinite(Length) || Length <= 0.0f)
		{
			Run.Errors.Add(TEXT("Could not calculate Awakened Kinsect attack Montage length."));
			return false;
		}
		Montage.SetCompositeLength(Length);
		Montage.UpdateLinkableElements();
		Montage.bEnableAutoBlendOut = true;
		Montage.RefreshCacheData();
	}
	return EnsureTracks(Run, *Run.MainMontage, MainTrackNames, UE_ARRAY_COUNT(MainTrackNames));
}

bool ConfigureFallMontage(FRun& Run)
{
	if (!Run.Sequence206)
	{
		return false;
	}
	Run.FallMontage = GetOrCreateMontage(Run, FallMontagePackage,
		TEXT("AM_IG_JueChongJi_Fall"), Run.Sequence206->GetSkeleton());
	if (!Run.FallMontage)
	{
		return false;
	}
	const bool bCreated = Run.ChangedAssets.Contains(Run.FallMontage->GetPathName());
	if (!MontageLayoutMatches(*Run.FallMontage, true, Run))
	{
		if (!bCreated)
		{
			Run.Errors.Add(Run.bApply
				? TEXT("Existing Awakened Kinsect fall Montage has a non-canonical layout; preserving it for review.")
				: TEXT("Awakened Kinsect fall Montage is absent or differs from the generated layout."));
			return false;
		}
		UAnimMontage& Montage = *Run.FallMontage;
		Montage.Modify();
		Montage.SlotAnimTracks.Reset();
		Montage.CompositeSections.Reset();
		FSlotAnimationTrack& Slot = Montage.SlotAnimTracks.AddDefaulted_GetRef();
		Slot.SlotName = SlotName;
		AddSegment(Montage, *Run.Sequence206, 0.0f, 0.0f, Run.Sequence206->GetPlayLength());
		AddSection(Montage, FallSection, 0.0f, NAME_None);
		const float Length = Montage.CalculateSequenceLength();
		if (!FMath::IsFinite(Length) || Length <= 0.0f)
		{
			Run.Errors.Add(TEXT("Could not calculate Awakened Kinsect fall Montage length."));
			return false;
		}
		Montage.SetCompositeLength(Length);
		Montage.UpdateLinkableElements();
		Montage.bEnableAutoBlendOut = true;
		Montage.RefreshCacheData();
	}
	return EnsureTracks(Run, *Run.FallMontage, FallTrackNames, UE_ARRAY_COUNT(FallTrackNames));
}

UBlueprint* GetOrCreateAbilityBlueprint(FRun& Run)
{
	const FString Path = ObjectPath(AbilityPackage, TEXT("GA_IG_JueChongJi"));
	Run.AbilityBlueprint = LoadObject<UBlueprint>(nullptr, *Path, nullptr, LOAD_NoWarn);
	UObject* Existing = LoadObjectByPath(Path);
	if (Existing && !Run.AbilityBlueprint)
	{
		Run.Errors.Add(TEXT("Awakened Kinsect ability path contains an incompatible object: ") + Path);
		return nullptr;
	}
	if (Run.AbilityBlueprint)
	{
		if (!Run.AbilityBlueprint->GeneratedClass
			|| !Run.AbilityBlueprint->GeneratedClass->IsChildOf(UMHGZAwakenedKinsectAttackAbility::StaticClass()))
		{
			Run.Errors.Add(TEXT("Existing GA_IG_JueChongJi does not derive from UMHGZAwakenedKinsectAttackAbility."));
			return nullptr;
		}
		Run.Ability = Cast<UMHGZAwakenedKinsectAttackAbility>(
			Run.AbilityBlueprint->GeneratedClass->GetDefaultObject());
		return Run.AbilityBlueprint;
	}
	if (!Run.bApply)
	{
		Run.Errors.Add(TEXT("Missing persistent GA_IG_JueChongJi Blueprint."));
		return nullptr;
	}
	UPackage* Package = CreatePackage(AbilityPackage);
	Run.AbilityBlueprint = Package ? FKismetEditorUtilities::CreateBlueprint(
		UMHGZAwakenedKinsectAttackAbility::StaticClass(), Package,
		FName(TEXT("GA_IG_JueChongJi")), BPTYPE_Normal,
		UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(),
		FName(TEXT("MHGZM6AssetSetup"))) : nullptr;
	Run.Ability = Run.AbilityBlueprint && Run.AbilityBlueprint->GeneratedClass
		? Cast<UMHGZAwakenedKinsectAttackAbility>(
			Run.AbilityBlueprint->GeneratedClass->GetDefaultObject()) : nullptr;
	if (!Run.AbilityBlueprint || !Run.Ability)
	{
		Run.Errors.Add(TEXT("Could not create the persistent Awakened Kinsect ability Blueprint."));
		return nullptr;
	}
	Run.ChangedAssets.Add(Path);
	return Run.AbilityBlueprint;
}

bool AuditHunterFlightCollisionWindow(const UAnimMontage& Montage,
	FString& OutError)
{
	OutError.Reset();
	const int32 SectionIndex = Montage.GetSectionIndex(HunterFlightSection);
	float SectionStart = 0.0f;
	float SectionEnd = 0.0f;
	if (SectionIndex == INDEX_NONE)
	{
		OutError = TEXT("AM_IG_JueChongJi has no HunterFlight199 section for the configured Attack Collision window.");
		return false;
	}
	Montage.GetSectionStartAndEndTime(SectionIndex, SectionStart, SectionEnd);
	int32 WindowCount = 0;
	for (const FAnimNotifyEvent& Event : Montage.Notifies)
	{
		const UAnimNotifyState_AttackCollision* CollisionWindow =
			Cast<UAnimNotifyState_AttackCollision>(Event.NotifyStateClass);
		if (!CollisionWindow)
		{
			continue;
		}
		if (CollisionWindow->ConfigIndex != 0)
		{
			OutError = FString::Printf(
				TEXT("AttackCollision notify ConfigIndex=%d is invalid:觉虫击 uses only AttackSegments[0]."),
				CollisionWindow->ConfigIndex);
			return false;
		}
		if (Event.GetTriggerTime() < SectionStart - 0.001f
			|| Event.GetEndTriggerTime() > SectionEnd + 0.001f)
		{
			OutError = FString::Printf(
				TEXT("The existing AttackCollision window (%.4f-%.4f s) must lie wholly inside HunterFlight199 (%.4f-%.4f s); its timing is user-authored and was not changed."),
				Event.GetTriggerTime(), Event.GetEndTriggerTime(),
				SectionStart, SectionEnd);
			return false;
		}
		++WindowCount;
	}
	if (WindowCount == 0)
	{
		OutError = TEXT("AM_IG_JueChongJi has no existing ConfigIndex=0 AttackCollision notify inside HunterFlight199. Add the window in the editor; the setup tool will not invent its frame timing.");
		return false;
	}
	return true;
}

bool ConfigureAbility(FRun& Run)
{
	if (!Run.AbilityBlueprint || !Run.Ability || !Run.MainMontage
		|| !Run.KinsectSlashAbility
		|| !Run.KinsectSlashAbility->DanceMontage
		|| Run.KinsectSlashAbility->DanceFromDashTuning.IsValid()	== false
		|| !Run.KinsectSlashAbility->AttackSegments.IsValidIndex(1))
	{
		Run.Errors.Add(TEXT("Cannot audit/configure the Awakened Kinsect GA without its Blueprint, attack Montage, or valid M5 Dash->189 source data."));
		return false;
	}
	const UMHGZKinsectSlashAbility& SlashSource = *Run.KinsectSlashAbility;
	const FAttackSegmentConfig& ExpectedHunterDamage = SlashSource.AttackSegments[1];
	const bool bSourceDamageValid = ExpectedHunterDamage.Damage.DamageEffectClass
		&& ExpectedHunterDamage.Damage.MotionValue.GetValueAtLevel(1.0f) > 0.0f
		&& ExpectedHunterDamage.Damage.bUseHitzoneDefense
		&& !ExpectedHunterDamage.Damage.bRequiresHitToContinue
		&& ExpectedHunterDamage.MultiHitCount == 1
		&& ExpectedHunterDamage.MultiHitPolicy == EAttackMultiHitPolicy::ContactOnly
		&& ExpectedHunterDamage.Collision.TraceRegions.Num() == 1
		&& ExpectedHunterDamage.Collision.TraceRegions[0].StartSocketName == FName(TEXT("IG_RearTip"))
		&& ExpectedHunterDamage.Collision.TraceRegions[0].EndSocketName == FName(TEXT("Root"));
	if (!bSourceDamageValid)
	{
		Run.Errors.Add(TEXT("M5 GA_IG_CaoChongZhan AttackSegments[1] is not the expected one-hit IG_RearTip->Root damage source; refusing to invent a different M6 damage setup."));
		return false;
	}
	FString CollisionWindowError;
	if (!AuditHunterFlightCollisionWindow(*Run.MainMontage, CollisionWindowError))
	{
		Run.Errors.Add(CollisionWindowError);
		return false;
	}
	const FGameplayTag ExpectedInput = Tag(TEXT("Input.Weapon.RTYB"));
	if (!ExpectedInput.IsValid())
	{
		Run.Errors.Add(TEXT("Input.Weapon.RTYB is not registered."));
		return false;
	}
	TArray<FString> CdoMismatches;
	if (Run.Ability->InputTag != ExpectedInput)
	{
		CdoMismatches.Add(TEXT("InputTag"));
	}
	if (Run.Ability->AttackMontage != Run.MainMontage)
	{
		CdoMismatches.Add(TEXT("AttackMontage"));
	}
	if (Run.Ability->DefaultEntrySection != StartupSection)
	{
		CdoMismatches.Add(TEXT("DefaultEntrySection"));
	}
	if (Run.Ability->MaxCorrectionAngle != AwakenedActivationCorrectionAngle)
	{
		CdoMismatches.Add(TEXT("MaxCorrectionAngle"));
	}
	if (Run.Ability->DanceMontage != SlashSource.DanceMontage)
	{
		CdoMismatches.Add(TEXT("DanceMontage"));
	}
	if (Run.Ability->DanceSectionName != SlashSource.DanceSectionName)
	{
		CdoMismatches.Add(TEXT("DanceSectionName"));
	}
	if (Run.Ability->DanceMontageBlendInTime != SlashSource.DanceMontageBlendInTime)
	{
		CdoMismatches.Add(TEXT("DanceMontageBlendInTime"));
	}
	if (!FKinsectSlashDanceEntryTuning::StaticStruct()->CompareScriptStruct(
		&Run.Ability->DanceFromDashTuning, &SlashSource.DanceFromDashTuning, 0))
	{
		CdoMismatches.Add(TEXT("DanceFromDashTuning"));
	}
	if (Run.Ability->AttackSegments.Num() != 1)
	{
		CdoMismatches.Add(TEXT("AttackSegments.Num"));
	}
	else if (!FAttackSegmentConfig::StaticStruct()->CompareScriptStruct(
		&Run.Ability->AttackSegments[0], &ExpectedHunterDamage, 0))
	{
		CdoMismatches.Add(TEXT("AttackSegments[0]"));
	}
	if (!Run.Ability->WeaponResourceCosts.IsEmpty())
	{
		CdoMismatches.Add(TEXT("WeaponResourceCosts"));
	}
	const bool bChanged = !CdoMismatches.IsEmpty();
	if (bChanged && !Run.bApply)
	{
		Run.Errors.Add(FString::Printf(
			TEXT("GA_IG_JueChongJi CDO differs from the M6.2 setup contract. Fields=[%s]."),
			*FString::Join(CdoMismatches, TEXT(", "))));
		return false;
	}
	if (bChanged)
	{
		Run.AbilityBlueprint->Modify();
		Run.Ability->Modify();
		Run.Ability->InputTag = ExpectedInput;
		Run.Ability->AttackMontage = Run.MainMontage;
		Run.Ability->DefaultEntrySection = StartupSection;
		Run.Ability->MaxCorrectionAngle = AwakenedActivationCorrectionAngle;
		Run.Ability->DanceMontage = SlashSource.DanceMontage;
		Run.Ability->DanceSectionName = SlashSource.DanceSectionName;
		Run.Ability->DanceMontageBlendInTime = SlashSource.DanceMontageBlendInTime;
		Run.Ability->DanceFromDashTuning = SlashSource.DanceFromDashTuning;
		Run.Ability->AttackSegments.SetNum(1);
		Run.Ability->AttackSegments[0] = ExpectedHunterDamage;
		Run.Ability->WeaponResourceCosts.Reset();
		Run.ChangedAssets.AddUnique(Run.AbilityBlueprint->GetPathName());
		FKismetEditorUtilities::CompileBlueprint(Run.AbilityBlueprint,
			EBlueprintCompileOptions::SkipSave
			| EBlueprintCompileOptions::SkipDefaultObjectValidation
			| EBlueprintCompileOptions::SkipGarbageCollection);
		if (Run.AbilityBlueprint->Status == BS_Error || !Run.AbilityBlueprint->GeneratedClass)
		{
			Run.Errors.Add(TEXT("GA_IG_JueChongJi failed Blueprint compilation."));
			return false;
		}
		Run.Ability = Cast<UMHGZAwakenedKinsectAttackAbility>(
			Run.AbilityBlueprint->GeneratedClass->GetDefaultObject());
	}
	if (!Run.Ability->AttackMontage || Run.Ability->AttackMontage != Run.MainMontage
		|| Run.Ability->DefaultEntrySection != StartupSection
		|| Run.Ability->HoldReleaseControls.Num() != 2
		|| !Run.Ability->HoldReleaseControls.Contains(Tag(TEXT("Input.Weapon.Y")))
		|| !Run.Ability->HoldReleaseControls.Contains(Tag(TEXT("Input.Weapon.B")))
		|| Run.Ability->DanceMontage != SlashSource.DanceMontage
		|| Run.Ability->DanceSectionName != SlashSource.DanceSectionName
		|| Run.Ability->DanceMontageBlendInTime != SlashSource.DanceMontageBlendInTime
		|| !FKinsectSlashDanceEntryTuning::StaticStruct()->CompareScriptStruct(
			&Run.Ability->DanceFromDashTuning, &SlashSource.DanceFromDashTuning, 0)
		|| Run.Ability->AttackSegments.Num() != 1
		|| !FAttackSegmentConfig::StaticStruct()->CompareScriptStruct(
			&Run.Ability->AttackSegments[0], &ExpectedHunterDamage, 0)
		|| !Run.Ability->WeaponResourceCosts.IsEmpty())
	{
		Run.Errors.Add(TEXT("GA CDO failed its persistent Montage, 189 data, one-segment rear-tip damage, hold controls, or resource-cost audit."));
		return false;
	}
	return true;
}

FWeaponChordDefinition* FindRTYBChord(UWeaponInputProfile& Profile)
{
	const FGameplayTag Output = Tag(TEXT("Input.Weapon.RTYB"));
	TArray<FWeaponChordDefinition*> Matches;
	for (FWeaponChordDefinition& Chord : Profile.Chords)
	{
		if (Chord.OutputTag == Output)
		{
			Matches.Add(&Chord);
		}
	}
	return Matches.Num() == 1 ? Matches[0] : nullptr;
}

bool ConfigureInputProfile(FRun& Run)
{
	if (!Run.InputProfile)
	{
		return false;
	}
	FWeaponChordDefinition* Chord = FindRTYBChord(*Run.InputProfile);
	if (!Chord || Chord->TriggerControls.Num() != 2
		|| !Chord->TriggerControls.Contains(Tag(TEXT("Input.Weapon.Y")))
		|| !Chord->TriggerControls.Contains(Tag(TEXT("Input.Weapon.B")))
		|| Chord->RequiredHeldModifiers.Num() != 1
		|| !Chord->RequiredHeldModifiers.Contains(Tag(TEXT("Input.Modifier.RT")))
		|| Chord->DispatchPolicy != EWeaponChordDispatchPolicy::OnPress
		|| !Chord->bConsumeTriggerControls)
	{
		Run.Errors.Add(TEXT("DA_IG_InputProfile must contain exactly one RTYB chord with Y+B, held RT, OnPress, and trigger consumption."));
		return false;
	}
	const FGameplayTag YControl = Tag(TEXT("Input.Weapon.Y"));
	const FGameplayTag BControl = Tag(TEXT("Input.Weapon.B"));
	const bool bReleaseControlsCorrect = Chord->ReleaseControlTag == YControl
		&& Chord->AdditionalReleaseControlTags.Num() == 1
		&& Chord->AdditionalReleaseControlTags[0] == BControl;
	if (Chord->AimSnapshotContext != EWeaponAimSnapshotContext::Action
		|| !Chord->bRequireExactModifiers || !bReleaseControlsCorrect)
	{
		if (!Run.bApply)
		{
			Run.Errors.Add(TEXT("DA_IG_InputProfile.RTYB must use Action AimSnapshotContext, exact modifiers, and register Completed on both Y and B (ReleaseControlTag=Y, AdditionalReleaseControlTags=[B])."));
			return false;
		}
		Run.InputProfile->Modify();
		Chord->AimSnapshotContext = EWeaponAimSnapshotContext::Action;
		Chord->bRequireExactModifiers = true;
		Chord->ReleaseControlTag = YControl;
		Chord->AdditionalReleaseControlTags = { BControl };
		Run.ChangedAssets.AddUnique(Run.InputProfile->GetPathName());
	}
	return true;
}

FComboTransition BuildExpectedTransition(UClass* AbilityClass)
{
	FComboTransition Transition;
	Transition.TransitionID = TransitionID;
	Transition.SourceState = TEXT("Idle");
	Transition.InputTag = Tag(TEXT("Input.Weapon.RTYB"));
	Transition.ExecutionPolicy = EComboExecutionPolicy::ActivateAbility;
	Transition.AbilityClass = AbilityClass;
	Transition.MontageBlendInTime = -1.0f;
	// Inherit the GA's 90-degree camera-aim activation correction cap.
	Transition.MaxCorrectionAngle = -1.0f;
	Transition.TargetState = TargetState;
	Transition.StatePolicy = EComboStatePolicy::Replace;
	Transition.LandingPolicy = EComboLandingPolicy::ResetToIdle;
	Transition.RequiredTags.AddTag(Tag(TEXT("Combat.State.Grounded")));
	Transition.RequiredTags.AddTag(Tag(TEXT("Combat.State.Unsheathed")));
	Transition.BlockedTags.AddTag(Tag(TEXT("Combat.State.Attacking")));
	Transition.BlockedTags.AddTag(Tag(TEXT("Combat.State.Dodging")));
	Transition.BlockedTags.AddTag(Tag(TEXT("Combat.State.Sheathing")));
	Transition.BlockedTags.AddTag(Tag(TEXT("Combat.State.Hitstun")));
	Transition.BlockedTags.AddTag(Tag(TEXT("Combat.State.Knockdown")));
	Transition.BlockedTags.AddTag(Tag(TEXT("Combat.State.Dead")));
	Transition.BlockedTags.AddTag(Tag(TEXT("Combat.State.Aerial")));
	Transition.StaminaRequired = 0.0f;
	Transition.bRequiresComboWindow = false;
	Transition.bRequiresDodgeAcceptWindow = false;
	Transition.GrantTiming = ETransitionGrantTiming::OnActivation;
	Transition.GrantedTags.AddTag(Tag(TEXT("Combat.State.Attacking")));
	Transition.bAutoTransition = false;
	Transition.Priority = 0;
	return Transition;
}

bool ConfigureCombo(FRun& Run)
{
	if (!Run.Combo || !Run.AbilityBlueprint || !Run.AbilityBlueprint->GeneratedClass)
	{
		Run.Errors.Add(TEXT("Cannot audit/configure the RTYB Combo row without DA_IG_Combo and the generated GA class."));
		return false;
	}
	const FComboTransition Expected = BuildExpectedTransition(Run.AbilityBlueprint->GeneratedClass);
	const TCHAR* RequiredTagNames[] = {
		TEXT("Combat.State.Grounded"), TEXT("Combat.State.Unsheathed") };
	const TCHAR* BlockedTagNames[] = {
		TEXT("Combat.State.Attacking"), TEXT("Combat.State.Dodging"),
		TEXT("Combat.State.Sheathing"), TEXT("Combat.State.Hitstun"),
		TEXT("Combat.State.Knockdown"), TEXT("Combat.State.Dead"),
		TEXT("Combat.State.Aerial") };
	bool bAllTagsValid = true;
	for (const TCHAR* TagName : RequiredTagNames)
	{
		bAllTagsValid &= Tag(TagName).IsValid();
	}
	for (const TCHAR* TagName : BlockedTagNames)
	{
		bAllTagsValid &= Tag(TagName).IsValid();
	}
	if (!Expected.InputTag.IsValid() || !Expected.AbilityClass
		|| !bAllTagsValid
		|| !Expected.RequiredTags.HasTagExact(Tag(TEXT("Combat.State.Grounded")))
		|| !Expected.GrantedTags.HasTagExact(Tag(TEXT("Combat.State.Attacking"))))
	{
		Run.Errors.Add(TEXT("One or more required M6 Combo Gameplay Tags are not registered."));
		return false;
	}
	FComboTransition* Existing = nullptr;
	int32 ExistingCount = 0;
	for (FComboTransition& Row : Run.Combo->Transitions)
	{
		if (Row.TransitionID == TransitionID)
		{
			Existing = &Row;
			++ExistingCount;
		}
	}
	if (ExistingCount > 1)
	{
		Run.Errors.Add(TEXT("DA_IG_Combo contains duplicate Idle_RTYB_AwakenedKinsectAttack IDs; preserving the asset."));
		return false;
	}
	if (Existing)
	{
		FComboTransition ExistingWithExpectedCorrection = *Existing;
		ExistingWithExpectedCorrection.MaxCorrectionAngle = Expected.MaxCorrectionAngle;
		if (FComboTransition::StaticStruct()->CompareScriptStruct(
			&ExistingWithExpectedCorrection, &Expected, PPF_None))
		{
			if (!FMath::IsNearlyEqual(Existing->MaxCorrectionAngle,
				Expected.MaxCorrectionAngle, KINDA_SMALL_NUMBER))
			{
				if (!Run.bApply)
				{
					Run.Errors.Add(TEXT("Existing M6 RTYB Combo row has a stale activation-correction override; use -CorrectionOnly -Apply to update only the GA/Combo correction fields."));
					return false;
				}
				Run.Combo->Modify();
				Existing->MaxCorrectionAngle = Expected.MaxCorrectionAngle;
				Run.ChangedAssets.AddUnique(Run.Combo->GetPathName());
			}
			return true;
		}
		else
		{
			Run.Errors.Add(TEXT("Existing M6 RTYB Combo row differs from the canonical contract; preserving it."));
			return false;
		}
	}
	if (!Run.bApply)
	{
		Run.Errors.Add(TEXT("DA_IG_Combo is missing Ground_AwakenedKinsect_RTYB."));
		return false;
	}
	Run.Combo->Modify();
	Run.Combo->Transitions.Add(Expected);
	Run.ChangedAssets.AddUnique(Run.Combo->GetPathName());
	return true;
}

bool ConfigureCombat(FRun& Run)
{
	if (!Run.Combat || !Run.FallMontage)
	{
		Run.Errors.Add(TEXT("Cannot audit/configure AwakenedHunterFallMontage without DA_IG_Combat and the fall Montage."));
		return false;
	}
	if (Run.Combat->AwakenedHunterFallMontage == Run.FallMontage)
	{
		return true;
	}
	if (Run.Combat->AwakenedHunterFallMontage)
	{
		Run.Errors.Add(TEXT("DA_IG_Combat already references a different AwakenedHunterFallMontage; preserving it."));
		return false;
	}
	if (!Run.bApply)
	{
		Run.Errors.Add(TEXT("DA_IG_Combat.AwakenedHunterFallMontage is not assigned."));
		return false;
	}
	Run.Combat->Modify();
	Run.Combat->AwakenedHunterFallMontage = Run.FallMontage;
	Run.ChangedAssets.AddUnique(Run.Combat->GetPathName());
	return true;
}

bool WriteReport(const FRun& Run)
{
	const FString Path = FPaths::Combine(FPaths::ProjectDir(), Run.bLandingOnly
		? TEXT("Saved/M6Audit/m6_awakened_landing_setup_report.json") : AuditRelativePath);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("mode"), Run.bApply ? TEXT("Apply") : TEXT("AuditOnly"));
	Root->SetBoolField(TEXT("input_profile_only"), Run.bInputProfileOnly);
	Root->SetBoolField(TEXT("correction_only"), Run.bCorrectionOnly);
	Root->SetBoolField(TEXT("landing_only"), Run.bLandingOnly);
	if (Run.bLandingOnly)
	{
		Root->SetStringField(TEXT("landing_montage"), LandingMontagePackage);
		Root->SetStringField(TEXT("landing_scope"), TEXT("Shared AerialLandingMontage reference only; never generate/trim a landing Montage."));
		Root->SetNumberField(TEXT("landing_length_seconds"), Run.LandingMontage ? Run.LandingMontage->GetPlayLength() : 0.0);
		Root->SetBoolField(TEXT("success"), Run.Errors.IsEmpty());
	}
	if (Run.bInputProfileOnly)
	{
		Root->SetStringField(TEXT("input_profile_scope"),
			TEXT("RTYB Action aim, exact modifiers, and Y/B release registrations only"));
	}
	Root->SetStringField(TEXT("main_montage"), MainMontagePackage);
	Root->SetStringField(TEXT("fall_montage"), FallMontagePackage);
	Root->SetStringField(TEXT("ability"), AbilityPackage);
	Root->SetNumberField(TEXT("loop_start_frame_candidate"), HoldLoopStartFrameCandidate);
	Root->SetBoolField(TEXT("loop_frame_is_final_truth"), false);
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
		&& FFileHelper::SaveStringToFile(Json, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

bool SaveChangedAssets(FRun& Run)
{
	if (Run.Errors.Num() > 0)
	{
		return false;
	}
	TArray<UObject*> Assets = {
		Run.MainMontage, Run.FallMontage, Run.LandingMontage, Run.AbilityBlueprint,
		Run.InputProfile, Run.Combo, Run.Combat };
	for (UObject* Asset : Assets)
	{
		if (!Asset || !Run.ChangedAssets.Contains(Asset->GetPathName()))
		{
			continue;
		}
		if (!SaveAsset(*Asset))
		{
			Run.Errors.Add(TEXT("Failed to save generated M6 asset: ") + Asset->GetPathName());
			return false;
		}
	}
	return Run.Errors.IsEmpty();
}

/** Narrow editor migration: changes only GA_IG_JueChongJi.MaxCorrectionAngle and
 *  the matching RTYB Combo row's override, preserving Montages and all other rows. */
bool ExecuteCorrectionOnly(FRun& Run)
{
	Run.AbilityBlueprint = LoadObject<UBlueprint>(nullptr,
		*ObjectPath(AbilityPackage, TEXT("GA_IG_JueChongJi")), nullptr, LOAD_NoWarn);
	Run.Combo = LoadObject<UMHGZWeaponComboData>(nullptr, ComboPath, nullptr, LOAD_NoWarn);
	if (!Run.AbilityBlueprint || !Run.AbilityBlueprint->GeneratedClass || !Run.Combo)
	{
		Run.Errors.Add(TEXT("CorrectionOnly requires the existing GA_IG_JueChongJi Blueprint and DA_IG_Combo; it will not create assets."));
		return false;
	}
	Run.Ability = Cast<UMHGZAwakenedKinsectAttackAbility>(
		Run.AbilityBlueprint->GeneratedClass->GetDefaultObject());
	if (!Run.Ability)
	{
		Run.Errors.Add(TEXT("GA_IG_JueChongJi does not derive from UMHGZAwakenedKinsectAttackAbility."));
		return false;
	}

	FComboTransition* Transition = nullptr;
	int32 TransitionCount = 0;
	for (FComboTransition& Candidate : Run.Combo->Transitions)
	{
		if (Candidate.TransitionID == TransitionID)
		{
			Transition = &Candidate;
			++TransitionCount;
		}
	}
	if (TransitionCount != 1 || !Transition)
	{
		Run.Errors.Add(TEXT("CorrectionOnly requires exactly one Idle_RTYB_AwakenedKinsectAttack row; no asset was changed."));
		return false;
	}

	const bool bAbilityNeedsUpdate = !FMath::IsNearlyEqual(
		Run.Ability->MaxCorrectionAngle,
		AwakenedActivationCorrectionAngle, KINDA_SMALL_NUMBER);
	const bool bComboNeedsUpdate = !FMath::IsNearlyEqual(
		Transition->MaxCorrectionAngle, -1.0f, KINDA_SMALL_NUMBER);
	if (!bAbilityNeedsUpdate && !bComboNeedsUpdate)
	{
		return true;
	}
	if (!Run.bApply)
	{
		Run.Errors.Add(TEXT("Activation correction differs from the 90-degree camera-aim contract; rerun with -CorrectionOnly -Apply to update only these two fields."));
		return false;
	}

	if (bAbilityNeedsUpdate)
	{
		Run.AbilityBlueprint->Modify();
		Run.Ability->Modify();
		Run.Ability->MaxCorrectionAngle = AwakenedActivationCorrectionAngle;
		FKismetEditorUtilities::CompileBlueprint(Run.AbilityBlueprint,
			EBlueprintCompileOptions::SkipSave
			| EBlueprintCompileOptions::SkipDefaultObjectValidation
			| EBlueprintCompileOptions::SkipGarbageCollection);
		if (Run.AbilityBlueprint->Status == BS_Error
			|| !Run.AbilityBlueprint->GeneratedClass)
		{
			Run.Errors.Add(TEXT("GA_IG_JueChongJi failed Blueprint compilation during CorrectionOnly."));
			return false;
		}
		Run.ChangedAssets.AddUnique(Run.AbilityBlueprint->GetPathName());
	}
	if (bComboNeedsUpdate)
	{
		Run.Combo->Modify();
		Transition->MaxCorrectionAngle = -1.0f;
		Run.ChangedAssets.AddUnique(Run.Combo->GetPathName());
	}
	return SaveChangedAssets(Run);
}

bool ExecuteLandingOnly(FRun& Run)
{
	// Ordinary landings all use the existing shared asset. Never create a per-action
	// landing or modify the user's authored common Montage/sequence/notifies.
	Run.Combat = LoadObject<UInsectGlaiveCombatConfig>(nullptr, CombatPath, nullptr, LOAD_NoWarn);
	Run.LandingMontage = LoadObject<UAnimMontage>(nullptr,
		*ObjectPath(LandingMontagePackage, TEXT("AM_IG_AerialLanding")), nullptr, LOAD_NoWarn);
	if (!Run.Combat || !Run.LandingMontage || !Run.LandingMontage->GetSkeleton()
		|| Run.LandingMontage->GetPlayLength() <= 0.0f)
	{
		Run.Errors.Add(TEXT("Existing AM_IG_AerialLanding or DA_IG_Combat unavailable; no dynamic/generated fallback."));
		return false;
	}
	if (!Run.bApply && Run.Combat->GetAerialLandingMontage() != Run.LandingMontage)
	{
		Run.Errors.Add(TEXT("DA_IG_Combat.AerialLandingMontage must reference the existing AM_IG_AerialLanding."));
		return false;
	}
	if (Run.bApply)
	{
		// Also resave the config to remove the retired per-action property/import from
		// its serialized package. The shared Montage itself is deliberately not dirty.
		Run.Combat->Modify();
		Run.Combat->AerialLandingMontage = Run.LandingMontage;
		Run.ChangedAssets.AddUnique(Run.Combat->GetPathName());
	}
	return !Run.bApply || SaveChangedAssets(Run);
}

bool Execute(FRun& Run)
{
	if (Run.bLandingOnly) { return ExecuteLandingOnly(Run); }
	if (Run.bCorrectionOnly)
	{
		return ExecuteCorrectionOnly(Run);
	}
	if (Run.bInputProfileOnly)
	{
		Run.InputProfile = LoadObject<UWeaponInputProfile>(nullptr,
			InputProfilePath, nullptr, LOAD_NoWarn);
		if (!Run.InputProfile)
		{
			Run.Errors.Add(TEXT("DA_IG_InputProfile could not be loaded for the isolated RTYB release audit."));
			return false;
		}
		if (!ConfigureInputProfile(Run))
		{
			return false;
		}
		if (Run.bApply && !Run.ChangedAssets.IsEmpty()
			&& !SaveAsset(*Run.InputProfile))
		{
			Run.Errors.Add(TEXT("Failed to save the isolated DA_IG_InputProfile RTYB release update."));
			return false;
		}
		return Run.Errors.IsEmpty();
	}

	if (!LoadDependencies(Run))
	{
		return false;
	}
	const FString MainMontagePath = ObjectPath(MainMontagePackage,
		TEXT("AM_IG_JueChongJi"));
	const FString FallMontagePath = ObjectPath(FallMontagePackage,
		TEXT("AM_IG_JueChongJi_Fall"));
	Run.MainMontage = LoadObject<UAnimMontage>(nullptr, *MainMontagePath, nullptr, LOAD_NoWarn);
	Run.FallMontage = LoadObject<UAnimMontage>(nullptr, *FallMontagePath, nullptr, LOAD_NoWarn);
	bool bSetupValid = true;
	bSetupValid &= ConfigureMainMontage(Run);
	bSetupValid &= ConfigureFallMontage(Run);
	const bool bAbilityLoaded = GetOrCreateAbilityBlueprint(Run) != nullptr;
	bSetupValid &= bAbilityLoaded;
	bSetupValid &= ConfigureAbility(Run);
	bSetupValid &= ConfigureInputProfile(Run);
	bSetupValid &= ConfigureCombo(Run);
	bSetupValid &= ConfigureCombat(Run);
	if (!bSetupValid || !Run.Errors.IsEmpty())
	{
		return false;
	}
	if (Run.bApply && !SaveChangedAssets(Run))
	{
		return false;
	}
	return Run.Errors.IsEmpty();
}
}
#endif // WITH_EDITOR

UMHGZM6AssetSetupCommandlet::UMHGZM6AssetSetupCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMHGZM6AssetSetupCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	using namespace UE::MHGZ::M6AssetSetup;
	FRun Run;
	Run.bApply = FParse::Param(*Params, TEXT("Apply"));
	Run.bInputProfileOnly = FParse::Param(*Params, TEXT("InputProfileOnly"));
	Run.bCorrectionOnly = FParse::Param(*Params, TEXT("CorrectionOnly"));
	Run.bLandingOnly = FParse::Param(*Params, TEXT("LandingOnly"));
	if (Run.bLandingOnly && (Run.bCorrectionOnly || Run.bInputProfileOnly))
	{
		Run.Errors.Add(TEXT("-LandingOnly cannot be combined with other isolated operations."));
	}
	if (Run.bCorrectionOnly && Run.bInputProfileOnly)
	{
		Run.Errors.Add(TEXT("-CorrectionOnly and -InputProfileOnly are separate operations and cannot be combined."));
	}
	const bool bSuccess = Run.Errors.IsEmpty() && Execute(Run);
	for (const FString& Error : Run.Errors)
	{
		UE_LOG(LogTemp, Error, TEXT("[M6AssetSetup] %s"), *Error);
	}
	UE_LOG(LogTemp, Display,
		TEXT("[M6AssetSetup] mode=%s input_profile_only=%s correction_only=%s success=%s changed=%d errors=%d (193 HoldLoop source frame %d is provisional; 196-to-199 timing uses the recorded one-sample interval; visual pose mapping remains E6.1 review)."),
		Run.bApply ? TEXT("Apply") : TEXT("AuditOnly"),
		Run.bInputProfileOnly ? TEXT("true") : TEXT("false"),
		Run.bCorrectionOnly ? TEXT("true") : TEXT("false"), bSuccess ? TEXT("true") : TEXT("false"),
		Run.ChangedAssets.Num(), Run.Errors.Num(), static_cast<int32>(HoldLoopStartFrameCandidate));
	if (!WriteReport(Run))
	{
		UE_LOG(LogTemp, Error, TEXT("[M6AssetSetup] Failed to write Saved/M6Audit/m6_awakened_asset_setup_report.json."));
		return 1;
	}
	return bSuccess ? 0 : 1;
#else
	UE_LOG(LogTemp, Error, TEXT("[M6AssetSetup] This commandlet requires an editor build."));
	return 1;
#endif
}
