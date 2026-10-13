// Copyright MHGZ Project. All Rights Reserved.

#include "Editor/MHGZM5KinsectSlashAssetSetupCommandlet.h"

#if WITH_EDITOR

#include "ActionSystem/AnimNotify_IG_AerialHandoff.h"
#include "ActionSystem/AnimNotify_KinsectSlashEvent.h"
#include "ActionSystem/MHGZDamageGameplayEffect.h"
#include "ActionSystem/MHGZKinsectSlashAbility.h"
#include "ActionSystem/AnimNotifyState_AttackCollision.h"
#include "ActionSystem/MHGZWeaponComboData.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "AnimationBlueprintLibrary.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/Blueprint.h"
#include "InputAction.h"
#include "HAL/FileManager.h"
#include "InsectGlaive/InsectGlaiveCombatConfig.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/DataValidation.h"
#include "Misc/Parse.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "WeaponRuntime/MHGZWeaponRuntimeDefinition.h"
#include "WeaponRuntime/MHGZWeaponInputProfile.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

namespace UE::MHGZ::KinsectSlashAssetSetup
{
constexpr TCHAR HoverSequencePath[] =
	TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_CaoChongZhan.AS_Unsh_CaoChongZhan");
constexpr TCHAR DashSequencePath[] =
	TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_CaoChongZhan_Flying.AS_Unsh_CaoChongZhan_Flying");
constexpr TCHAR MissSequencePath[] =
	TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_CaoChongZhan_Over.AS_Unsh_CaoChongZhan_Over");
constexpr TCHAR DanceSequencePath[] =
	TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_CaoChongZhanJueChongJiWuTa.AS_Unsh_CaoChongZhanJueChongJiWuTa");
constexpr TCHAR MainMontagePackage[] =
	TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_CaoChongZhan");
constexpr TCHAR DanceMontagePackage[] =
	TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_CaoChongZhan_Dance");
constexpr TCHAR AbilityPackage[] =
	TEXT("/Game/Weapons/InsectGlaive/Abilities/GA_IG_CaoChongZhan");
constexpr TCHAR ComboDataPath[] =
	TEXT("/Game/Weapons/InsectGlaive/Data/DA_IG_Combo.DA_IG_Combo");
constexpr TCHAR CombatConfigPath[] =
	TEXT("/Game/Weapons/InsectGlaive/Data/DA_IG_Combat.DA_IG_Combat");
constexpr TCHAR GlaiveSkeletalMeshPath[] =
	TEXT("/Game/Weapons/InsectGlaive/Meshes/Glaive/SKM_IG_Glaive.SKM_IG_Glaive");
constexpr TCHAR WeaponRuntimeDefinitionPath[] =
	TEXT("/Game/Weapons/InsectGlaive/Data/DA_WeaponRuntime_IG.DA_WeaponRuntime_IG");
constexpr TCHAR NotifyTrackName[] = TEXT("KinsectSlash");
constexpr TCHAR CollisionNotifyTrackName[] = TEXT("KinsectSlashCollision");
constexpr TCHAR DefaultSlotName[] = TEXT("DefaultSlot");
constexpr float HoverDuration = 0.718f;
constexpr float DashDuration = 0.242f;
constexpr float DanceBlendTime = 0.1f;
constexpr float DanceHandoffSourceFrame = 90.0f;
constexpr float MissLateHitCloseSourceFrame = 14.0f;
constexpr float ProvisionalMotionValue = 0.20f;
constexpr TCHAR KinsectSlashSocketName[] = TEXT("IG_Kinsect");
constexpr TCHAR PreviousKinsectSlashSocketName[] = TEXT("IG_KinsectSlashAnchor");

struct FSequenceSet
{
	UAnimSequence* Hover = nullptr;
	UAnimSequence* Dash = nullptr;
	UAnimSequence* Miss = nullptr;
	UAnimSequence* Dance = nullptr;
};

struct FSegmentInfo
{
	float Start = 0.0f;
	float Duration = 0.0f;
	float PlayRate = 0.0f;
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
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
	Package->MarkPackageDirty();
	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	return UPackage::SavePackage(Package, &Asset, *Filename, Args);
}

UAnimMontage* GetOrCreateMontage(const TCHAR* PackageName, const TCHAR* AssetName,
	USkeleton* Skeleton)
{
	const FString ObjectPath = FString::Printf(TEXT("%s.%s"), PackageName, AssetName);
	UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *ObjectPath);
	if (!Montage)
	{
		UPackage* Package = CreatePackage(PackageName);
		Montage = Package ? NewObject<UAnimMontage>(Package, FName(AssetName),
			RF_Public | RF_Standalone) : nullptr;
	}
	if (Montage)
	{
		Montage->Modify();
		Montage->SetSkeleton(Skeleton);
	}
	return Montage;
}

bool IsValidSequence(const UAnimSequence* Sequence, const int32 MinimumKeys)
{
	return Sequence && Sequence->GetSkeleton()
		&& FMath::IsFinite(Sequence->GetPlayLength())
		&& Sequence->GetPlayLength() > KINDA_SMALL_NUMBER
		&& FMath::IsFinite(Sequence->RateScale)
		&& !FMath::IsNearlyZero(Sequence->RateScale)
		&& Sequence->GetNumberOfSampledKeys() > MinimumKeys;
}

float RateForRuntime(const UAnimSequence& Sequence, const float DesiredRuntime)
{
	if (!FMath::IsFinite(DesiredRuntime) || DesiredRuntime <= KINDA_SMALL_NUMBER
		|| !FMath::IsFinite(Sequence.RateScale)
		|| FMath::IsNearlyZero(Sequence.RateScale))
	{
		return 0.0f;
	}
	return Sequence.GetPlayLength()
		/ (DesiredRuntime * FMath::Abs(Sequence.RateScale));
}

float DoubleSpeedRuntime(const UAnimSequence& Sequence)
{
	return Sequence.GetPlayLength() / (2.0f * FMath::Abs(Sequence.RateScale));
}

void AddMontageSection(UAnimMontage& Montage, const FName Name, const float Time,
	const FName NextSection)
{
	FCompositeSection& Section = Montage.CompositeSections.AddDefaulted_GetRef();
	Section.SectionName = Name;
	Section.Link(&Montage, Time, 0);
	Section.SetTime(Time);
	Section.NextSectionName = NextSection;
}

bool FinalizeMontageLength(UAnimMontage& Montage)
{
	const float CalculatedLength = Montage.CalculateSequenceLength();
	if (!FMath::IsFinite(CalculatedLength) || CalculatedLength <= KINDA_SMALL_NUMBER)
	{
		return false;
	}
	// A newly-authored montage has no populated composite length yet. Notify
	// authoring APIs validate against GetPlayLength(), so persist the track-derived
	// length before adding notifies (RefreshCacheData alone does not set it).
	Montage.SetCompositeLength(CalculatedLength);
	Montage.UpdateLinkableElements();
	Montage.RefreshCacheData();
	return FMath::IsNearlyEqual(Montage.GetPlayLength(), CalculatedLength, 0.001f);
}

FSegmentInfo AddSegment(UAnimMontage& Montage, UAnimSequence& Sequence,
	const float Start, const float DesiredRuntime)
{
	FSegmentInfo Info;
	Info.Start = Start;
	Info.Duration = DesiredRuntime;
	Info.PlayRate = RateForRuntime(Sequence, DesiredRuntime);
	FSlotAnimationTrack& Track = Montage.SlotAnimTracks[0];
	FAnimSegment& Segment = Track.AnimTrack.AnimSegments.AddDefaulted_GetRef();
	Segment.SetAnimReference(&Sequence, true);
	Segment.StartPos = Start;
	Segment.AnimStartTime = 0.0f;
	Segment.AnimEndTime = Sequence.GetPlayLength();
	Segment.AnimPlayRate = Info.PlayRate;
	Segment.LoopingCount = 1;
	return Info;
}

bool EnsureNotifyTrack(UAnimMontage& Montage)
{
	if (!UAnimationBlueprintLibrary::IsValidAnimNotifyTrackName(&Montage,
		FName(NotifyTrackName)))
	{
		UAnimationBlueprintLibrary::AddAnimationNotifyTrack(&Montage,
			FName(NotifyTrackName), FLinearColor(0.1f, 0.8f, 1.0f));
	}
	return UAnimationBlueprintLibrary::IsValidAnimNotifyTrackName(&Montage,
		FName(NotifyTrackName));
}

void RemoveGeneratedNotifies(UAnimMontage& Montage)
{
	Montage.Notifies.RemoveAll([](const FAnimNotifyEvent& Event)
	{
		return Event.Notify && (Event.Notify->IsA(UAnimNotify_KinsectSlashEvent::StaticClass())
			|| Event.Notify->IsA(UAnimNotify_IG_AerialHandoff::StaticClass()));
	});
}

bool AddSlashEvent(UAnimMontage& Montage, const float Time,
	const EKinsectSlashMontageEvent EventType)
{
	UAnimNotify_KinsectSlashEvent* Event = Cast<UAnimNotify_KinsectSlashEvent>(
		UAnimationBlueprintLibrary::AddAnimationNotifyEvent(&Montage,
			FName(NotifyTrackName), Time,
			UAnimNotify_KinsectSlashEvent::StaticClass()));
	if (Event)
	{
		Event->Event = EventType;
	}
	return Event != nullptr;
}

bool AddAerialHandoff(UAnimMontage& Montage, const float Time)
{
	return Cast<UAnimNotify_IG_AerialHandoff>(
		UAnimationBlueprintLibrary::AddAnimationNotifyEvent(&Montage,
			FName(NotifyTrackName), Time,
			UAnimNotify_IG_AerialHandoff::StaticClass())) != nullptr;
}

bool BuildMainMontage(const FSequenceSet& Sequences, UAnimMontage*& OutMontage,
	FSegmentInfo& OutHover, FSegmentInfo& OutDash, FSegmentInfo& OutMiss)
{
	UAnimMontage* Montage = GetOrCreateMontage(MainMontagePackage,
		TEXT("AM_IG_CaoChongZhan"), Sequences.Hover->GetSkeleton());
	if (!Montage)
	{
		return false;
	}
	Montage->BlendIn.SetBlendTime(0.05f);
	Montage->BlendOut.SetBlendTime(DanceBlendTime);
	Montage->SlotAnimTracks.Reset();
	Montage->CompositeSections.Reset();
	RemoveGeneratedNotifies(*Montage);

	FSlotAnimationTrack& Slot = Montage->SlotAnimTracks.AddDefaulted_GetRef();
	Slot.SlotName = FName(DefaultSlotName);
	OutHover = AddSegment(*Montage, *Sequences.Hover, 0.0f, HoverDuration);
	OutDash = AddSegment(*Montage, *Sequences.Dash, OutHover.Start + OutHover.Duration,
		DashDuration);
	OutMiss = AddSegment(*Montage, *Sequences.Miss, OutDash.Start + OutDash.Duration,
		DoubleSpeedRuntime(*Sequences.Miss));

	AddMontageSection(*Montage, TEXT("Hover186"), OutHover.Start, TEXT("Dash187"));
	AddMontageSection(*Montage, TEXT("Dash187"), OutDash.Start, TEXT("Miss188"));
	AddMontageSection(*Montage, TEXT("Miss188"), OutMiss.Start, NAME_None);
	Montage->CompositeSections.Sort([](const FCompositeSection& A,
		const FCompositeSection& B) { return A.GetTime() < B.GetTime(); });
	if (!FinalizeMontageLength(*Montage))
	{
		return false;
	}
	if (!EnsureNotifyTrack(*Montage))
	{
		return false;
	}

	const int32 MissKeys = Sequences.Miss->GetNumberOfSampledKeys();
	const float MissCloseFraction = MissLateHitCloseSourceFrame
		/ static_cast<float>(MissKeys - 1);
	const float MissCloseTime = OutMiss.Start + OutMiss.Duration * MissCloseFraction;
	const float HoldTime = OutMiss.Start + OutMiss.Duration - 0.0001f;
	const bool bNotifiesAdded = AddSlashEvent(*Montage, 0.317f,
		EKinsectSlashMontageEvent::AimLock)
		&& AddSlashEvent(*Montage, OutDash.Start + 0.001f,
			EKinsectSlashMontageEvent::DashBegin)
		&& AddSlashEvent(*Montage, OutMiss.Start + 0.001f,
			EKinsectSlashMontageEvent::LateHitOpen)
		&& AddSlashEvent(*Montage, MissCloseTime,
			EKinsectSlashMontageEvent::LateHitClose)
		&& AddSlashEvent(*Montage, HoldTime,
			EKinsectSlashMontageEvent::HoldMissPose);
	Montage->RefreshCacheData();
	OutMontage = Montage;
	return bNotifiesAdded && Montage->IsValidSectionName(TEXT("Hover186"))
		&& Montage->IsValidSectionName(TEXT("Dash187"))
		&& Montage->IsValidSectionName(TEXT("Miss188"))
		&& FMath::IsNearlyEqual(OutHover.Duration, HoverDuration)
		&& FMath::IsNearlyEqual(OutDash.Duration, DashDuration)
		&& MissLateHitCloseSourceFrame < static_cast<float>(MissKeys - 1)
		&& Montage->Notifies.Num() >= 5 && SaveAsset(*Montage);
}

bool BuildDanceMontage(const UAnimSequence& Sequence, UAnimMontage*& OutMontage,
	float& OutHandoffTime)
{
	UAnimMontage* Montage = GetOrCreateMontage(DanceMontagePackage,
		TEXT("AM_IG_CaoChongZhan_Dance"), Sequence.GetSkeleton());
	if (!Montage)
	{
		return false;
	}
	Montage->BlendIn.SetBlendTime(DanceBlendTime);
	Montage->BlendOut.SetBlendTime(DanceBlendTime);
	Montage->SlotAnimTracks.Reset();
	Montage->CompositeSections.Reset();
	RemoveGeneratedNotifies(*Montage);

	const float Duration = DoubleSpeedRuntime(Sequence);
	FSlotAnimationTrack& Slot = Montage->SlotAnimTracks.AddDefaulted_GetRef();
	Slot.SlotName = FName(DefaultSlotName);
	const FSegmentInfo Segment = AddSegment(*Montage,
		*const_cast<UAnimSequence*>(&Sequence), 0.0f, Duration);
	AddMontageSection(*Montage, TEXT("HitDance189"), 0.0f, NAME_None);
	if (!FinalizeMontageLength(*Montage))
	{
		return false;
	}
	if (!EnsureNotifyTrack(*Montage))
	{
		return false;
	}

	const int32 Keys = Sequence.GetNumberOfSampledKeys();
	const float HandoffFraction = DanceHandoffSourceFrame
		/ static_cast<float>(Keys - 1);
	OutHandoffTime = Segment.Start + Segment.Duration * HandoffFraction;
	const bool bNotifiesAdded = AddSlashEvent(*Montage, 0.001f,
		EKinsectSlashMontageEvent::DanceBegin)
		&& AddAerialHandoff(*Montage, OutHandoffTime);
	Montage->RefreshCacheData();
	OutMontage = Montage;
	return bNotifiesAdded && Keys > static_cast<int32>(DanceHandoffSourceFrame)
		&& Montage->IsValidSectionName(TEXT("HitDance189"))
		&& FMath::IsNearlyEqual(Segment.Start, 0.0f)
		&& OutHandoffTime > 0.0f && OutHandoffTime < Duration
		&& SaveAsset(*Montage);
}

UBlueprint* GetOrCreateAbilityBlueprint()
{
	const FString ObjectPath = FString::Printf(TEXT("%s.GA_IG_CaoChongZhan"), AbilityPackage);
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *ObjectPath);
	if (Blueprint)
	{
		if (!Blueprint->GeneratedClass
			|| !Blueprint->GeneratedClass->IsChildOf(UMHGZKinsectSlashAbility::StaticClass()))
		{
			return nullptr;
		}
		return Blueprint;
	}

	UPackage* Package = CreatePackage(AbilityPackage);
	if (!Package)
	{
		return nullptr;
	}
	Blueprint = FKismetEditorUtilities::CreateBlueprint(
		UMHGZKinsectSlashAbility::StaticClass(), Package,
		FName(TEXT("GA_IG_CaoChongZhan")), BPTYPE_Normal,
		UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(),
		FName(TEXT("MHGZM5KinsectSlashAssetSetup")));
	return Blueprint && Blueprint->GeneratedClass ? Blueprint : nullptr;
}

bool ConfigureAbility(UBlueprint& Blueprint, UAnimMontage& MainMontage,
	UAnimMontage& DanceMontage)
{
	UMHGZKinsectSlashAbility* Ability = Blueprint.GeneratedClass
		? Cast<UMHGZKinsectSlashAbility>(Blueprint.GeneratedClass->GetDefaultObject())
		: nullptr;
	if (!Ability)
	{
		return false;
	}

	Blueprint.Modify();
	Ability->Modify();
	Ability->InputTag = FGameplayTag::RequestGameplayTag(TEXT("Input.Weapon.B"), false);
	Ability->AttackMontage = &MainMontage;
	Ability->DanceMontage = &DanceMontage;
	Ability->DefaultEntrySection = TEXT("Hover186");
	Ability->DanceSectionName = TEXT("HitDance189");
	Ability->DanceMontageBlendInTime = DanceBlendTime;
	Ability->EntrySectionByTransitionID.Reset();
	Ability->EntrySectionByTransitionID.Add(TEXT("Air_KinsectSlash_B"), TEXT("Hover186"));
	Ability->EntrySectionByTransitionID.Add(TEXT("Air_KinsectSlash_RTB"), TEXT("Hover186"));

	Ability->AttackSegments.Reset();
	for (int32 SegmentIndex = 0; SegmentIndex < 3; ++SegmentIndex)
	{
		FAttackSegmentConfig& Segment = Ability->AttackSegments.AddDefaulted_GetRef();
		Segment.Damage.DamageEffectClass = UMHGZDamageGameplayEffect::StaticClass();
		Segment.Damage.MotionValue = ProvisionalMotionValue;
		Segment.Damage.bUseHitzoneDefense = true;
		Segment.MultiHitCount = 1;
		Segment.MultiHitPolicy = EAttackMultiHitPolicy::ContactOnly;
		if (SegmentIndex > 0)
		{
			// The two blade hits are authored as separate AttackCollision windows.
			// Their exact montage times are deliberately left for visual alignment.
			FWeaponTraceRegion& Region = Segment.Collision.TraceRegions.AddDefaulted_GetRef();
			Region.StartSocketName = TEXT("Root");
			Region.EndSocketName = TEXT("IG_FrontTip");
		}
	}

	Ability->DanceFromDashTuning.ParameterMode =
		EBallisticParameterMode::ApexHeightAndDuration;
	Ability->DanceFromDashTuning.ApexHeight = 324.0f;
	Ability->DanceFromDashTuning.Duration = 0.75f;
	Ability->DanceFromDashTuning.HorizontalDistance = 392.0f;
	Ability->DanceFromDashTuning.ExplicitLaunchVelocity = FVector::ZeroVector;
	Ability->DanceFromMissTuning.ParameterMode =
		EBallisticParameterMode::ApexHeightAndDuration;
	Ability->DanceFromMissTuning.ApexHeight = 307.6f;
	Ability->DanceFromMissTuning.Duration = 0.75f;
	Ability->DanceFromMissTuning.HorizontalDistance = 488.0f;
	Ability->DanceFromMissTuning.ExplicitLaunchVelocity = FVector::ZeroVector;
	Blueprint.MarkPackageDirty();
	return true;
}

bool UpgradeExistingAbilityToThreeHits()
{
	// IsDataValid traverses DA_IG_Combat while the GA compiles. Resolve its GE
	// classes first so BlueprintCompilationManager never enqueues a second BP
	// compilation from inside this one.
	const UInsectGlaiveCombatConfig* CombatConfig =
		LoadObject<UInsectGlaiveCombatConfig>(nullptr, CombatConfigPath);
	if (!CombatConfig)
	{
		return false;
	}
	UClass* ExtractEffectClasses[] = {
		CombatConfig->RedEffectClass.Get(), CombatConfig->WhiteEffectClass.Get(),
		CombatConfig->OrangeEffectClass.Get() };
	for (UClass* EffectClass : ExtractEffectClasses)
	{
		if (EffectClass)
		{
			EffectClass->GetDefaultObject();
		}
	}

	const FString ObjectPath = FString::Printf(TEXT("%s.GA_IG_CaoChongZhan"), AbilityPackage);
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *ObjectPath);
	UMHGZKinsectSlashAbility* Ability = Blueprint && Blueprint->GeneratedClass
		? Cast<UMHGZKinsectSlashAbility>(Blueprint->GeneratedClass->GetDefaultObject())
		: nullptr;
	if (!Ability || (Ability->AttackSegments.Num() != 1
		&& Ability->AttackSegments.Num() != 3))
	{
		UE_LOG(LogTemp, Error, TEXT("[KinsectSlashAssets] Existing GA must have one legacy segment or three upgraded segments."));
		return false;
	}
	if (Ability->AttackSegments.Num() == 3)
	{
		if (Blueprint->Status == BS_Error
			|| !Ability->AttackSegments[0].Damage.DamageEffectClass
			|| !Ability->AttackSegments[1].Damage.DamageEffectClass
			|| !Ability->AttackSegments[2].Damage.DamageEffectClass
			|| Ability->AttackSegments[1].Collision.TraceRegions.IsEmpty()
			|| Ability->AttackSegments[2].Collision.TraceRegions.IsEmpty())
		{
			UE_LOG(LogTemp, Error, TEXT("[KinsectSlashAssets] Existing three-hit GA is incomplete."));
			return false;
		}
		UE_LOG(LogTemp, Display, TEXT("[KinsectSlashAssets] GA already has three segments; no asset changes."));
		return true;
	}

	const FAttackDamageConfig LegacyDamage = Ability->AttackSegments[0].Damage;
	Blueprint->Modify();
	Ability->Modify();
	for (int32 SegmentIndex = 1; SegmentIndex < 3; ++SegmentIndex)
	{
		FAttackSegmentConfig& Segment = Ability->AttackSegments.AddDefaulted_GetRef();
		Segment.Damage.DamageEffectClass = LegacyDamage.DamageEffectClass;
		Segment.Damage.MotionValue = LegacyDamage.MotionValue;
		Segment.Damage.bUseHitzoneDefense = LegacyDamage.bUseHitzoneDefense;
		Segment.MultiHitCount = 1;
		Segment.MultiHitPolicy = EAttackMultiHitPolicy::ContactOnly;
		FWeaponTraceRegion& Region = Segment.Collision.TraceRegions.AddDefaulted_GetRef();
		Region.StartSocketName = TEXT("Root");
		Region.EndSocketName = TEXT("IG_FrontTip");
	}
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	const UMHGZKinsectSlashAbility* CompiledAbility = Blueprint->GeneratedClass
		? Cast<UMHGZKinsectSlashAbility>(Blueprint->GeneratedClass->GetDefaultObject())
		: nullptr;
	if (Blueprint->Status == BS_Error || !CompiledAbility
		|| CompiledAbility->AttackSegments.Num() != 3)
	{
		UE_LOG(LogTemp, Error, TEXT("[KinsectSlashAssets] Three-hit GA failed to compile or retain its new defaults."));
		return false;
	}
	return SaveAsset(*Blueprint);
}

bool ParseRequiredTime(const FString& Params, const TCHAR* Key, float& OutTime)
{
	if (!FParse::Value(*Params, Key, OutTime) || !FMath::IsFinite(OutTime)
		|| OutTime < 0.0f)
	{
		UE_LOG(LogTemp, Error, TEXT("[KinsectSlashAssets] Missing or invalid required time argument '%s' (seconds)."), Key);
		return false;
	}
	return true;
}

bool GetMontageSectionStart(const UAnimMontage& Montage, const FName SectionName,
	float& OutStart)
{
	const FCompositeSection* Section = Montage.CompositeSections.FindByPredicate(
		[SectionName](const FCompositeSection& Candidate)
		{
			return Candidate.SectionName == SectionName;
		});
	if (!Section)
	{
		return false;
	}
	OutStart = Section->GetTime();
	return FMath::IsFinite(OutStart);
}

bool GetMissFrame14MontageTime(const UAnimMontage& Montage,
	const UAnimSequence& MissSequence, float& OutTime)
{
	float SectionStart = 0.0f;
	if (!GetMontageSectionStart(Montage, TEXT("Miss188"), SectionStart))
	{
		UE_LOG(LogTemp, Error, TEXT("[KinsectSlashAssets] Main montage has no Miss188 section."));
		return false;
	}

	const int32 KeyCount = MissSequence.GetNumberOfSampledKeys();
	if (KeyCount <= static_cast<int32>(MissLateHitCloseSourceFrame)
		|| FMath::IsNearlyZero(MissSequence.RateScale))
	{
		return false;
	}
	const float SourceFrameTime = MissSequence.GetPlayLength()
		* MissLateHitCloseSourceFrame / static_cast<float>(KeyCount - 1);
	for (const FSlotAnimationTrack& SlotTrack : Montage.SlotAnimTracks)
	{
		for (const FAnimSegment& Segment : SlotTrack.AnimTrack.AnimSegments)
		{
			if (Segment.GetAnimReference() != &MissSequence)
			{
				continue;
			}
			const float EffectiveRate = FMath::Abs(Segment.AnimPlayRate)
				* FMath::Abs(MissSequence.RateScale);
			if (EffectiveRate <= KINDA_SMALL_NUMBER
				|| SourceFrameTime < Segment.AnimStartTime
				|| SourceFrameTime > Segment.AnimEndTime
				|| !FMath::IsNearlyEqual(Segment.StartPos, SectionStart, 0.01f))
			{
				continue;
			}
			OutTime = Segment.StartPos
				+ (SourceFrameTime - Segment.AnimStartTime) / EffectiveRate;
			return FMath::IsFinite(OutTime)
				&& OutTime > SectionStart
				&& OutTime < Montage.GetPlayLength();
		}
	}
	UE_LOG(LogTemp, Error,
		TEXT("[KinsectSlashAssets] Could not map Miss188 source frame 14 to the montage segment; refusing to guess the insect window end."));
	return false;
}

bool EnsureCollisionNotifyTrack(UAnimMontage& Montage)
{
	if (!UAnimationBlueprintLibrary::IsValidAnimNotifyTrackName(&Montage,
		FName(CollisionNotifyTrackName)))
	{
		UAnimationBlueprintLibrary::AddAnimationNotifyTrack(&Montage,
			FName(CollisionNotifyTrackName), FLinearColor(1.0f, 0.25f, 0.1f));
	}
	return UAnimationBlueprintLibrary::IsValidAnimNotifyTrackName(&Montage,
		FName(CollisionNotifyTrackName));
}

void RemoveCollisionWindows(UAnimMontage& Montage, const int32 ConfigIndex)
{
	for (int32 Index = Montage.Notifies.Num() - 1; Index >= 0; --Index)
	{
		const FAnimNotifyEvent& Event = Montage.Notifies[Index];
		const UAnimNotifyState_AttackCollision* Collision =
			Cast<UAnimNotifyState_AttackCollision>(Event.NotifyStateClass);
		if (Collision && Collision->ConfigIndex == ConfigIndex)
		{
			Montage.Notifies.RemoveAt(Index);
		}
	}
}

void RemoveLegacyLateHitEvents(UAnimMontage& Montage)
{
	for (int32 Index = Montage.Notifies.Num() - 1; Index >= 0; --Index)
	{
		const UAnimNotify_KinsectSlashEvent* Event =
			Cast<UAnimNotify_KinsectSlashEvent>(Montage.Notifies[Index].Notify);
		if (Event && (Event->Event == EKinsectSlashMontageEvent::LateHitOpen
			|| Event->Event == EKinsectSlashMontageEvent::LateHitClose))
		{
			Montage.Notifies.RemoveAt(Index);
		}
	}
}

bool AddCollisionWindow(UAnimMontage& Montage, const int32 ConfigIndex,
	const float StartTime, const float EndTime)
{
	if (!FMath::IsFinite(StartTime) || !FMath::IsFinite(EndTime)
		|| StartTime < 0.0f || EndTime <= StartTime
		|| EndTime > Montage.GetPlayLength()
		|| !EnsureCollisionNotifyTrack(Montage))
	{
		return false;
	}

	UAnimNotifyState_AttackCollision* Notify =
		Cast<UAnimNotifyState_AttackCollision>(
			UAnimationBlueprintLibrary::AddAnimationNotifyStateEvent(&Montage,
				FName(CollisionNotifyTrackName), StartTime, EndTime - StartTime,
				UAnimNotifyState_AttackCollision::StaticClass()));
	if (!Notify)
	{
		return false;
	}
	Notify->ConfigIndex = ConfigIndex;
	return true;
}

bool HasExactCollisionWindow(const UAnimMontage& Montage, const int32 ConfigIndex,
	const float StartTime, const float EndTime)
{
	int32 MatchCount = 0;
	for (const FAnimNotifyEvent& Event : Montage.Notifies)
	{
		const UAnimNotifyState_AttackCollision* Collision =
			Cast<UAnimNotifyState_AttackCollision>(Event.NotifyStateClass);
		if (Collision && Collision->ConfigIndex == ConfigIndex
			&& FMath::IsNearlyEqual(Event.GetTriggerTime(), StartTime, 0.002f)
			&& FMath::IsNearlyEqual(Event.GetEndTriggerTime(), EndTime, 0.002f))
		{
			++MatchCount;
		}
	}
	return MatchCount == 1;
}

bool HasNoLegacyLateHitEvents(const UAnimMontage& Montage)
{
	for (const FAnimNotifyEvent& Event : Montage.Notifies)
	{
		if (const UAnimNotify_KinsectSlashEvent* SlashEvent =
			Cast<UAnimNotify_KinsectSlashEvent>(Event.Notify))
		{
			if (SlashEvent->Event == EKinsectSlashMontageEvent::LateHitOpen
				|| SlashEvent->Event == EKinsectSlashMontageEvent::LateHitClose)
			{
				return false;
			}
		}
	}
	return true;
}

bool HasMeshSocketOrBone(const USkeletalMesh& Mesh, const FName Name)
{
	return Mesh.FindSocket(Name) != nullptr
		|| Mesh.GetRefSkeleton().FindBoneIndex(Name) != INDEX_NONE;
}

bool InspectExistingCollisionWindows()
{
	UAnimMontage* MainMontage = LoadObject<UAnimMontage>(nullptr,
		TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_CaoChongZhan.AM_IG_CaoChongZhan"));
	UAnimMontage* DanceMontage = LoadObject<UAnimMontage>(nullptr,
		TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_CaoChongZhan_Dance.AM_IG_CaoChongZhan_Dance"));
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr,
		TEXT("/Game/Weapons/InsectGlaive/Abilities/GA_IG_CaoChongZhan.GA_IG_CaoChongZhan"));
	UInsectGlaiveCombatConfig* CombatConfig = LoadObject<UInsectGlaiveCombatConfig>(nullptr,
		CombatConfigPath);
	USkeletalMesh* GlaiveMesh = LoadObject<USkeletalMesh>(nullptr,
		GlaiveSkeletalMeshPath);
	UMHGZKinsectSlashAbility* Ability = Blueprint && Blueprint->GeneratedClass
		? Cast<UMHGZKinsectSlashAbility>(Blueprint->GeneratedClass->GetDefaultObject())
		: nullptr;
	if (!MainMontage || !DanceMontage || !Ability || !CombatConfig || !GlaiveMesh)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[KinsectSlashAssets][Inspect] Missing main/dance Montage, GA, DA_IG_Combat, or glaive mesh."));
		return false;
	}

	UE_LOG(LogTemp, Display,
		TEXT("[KinsectSlashAssets][Inspect] Current config socket='%s'; target socket='%s'; target found on SKM_IG_Glaive=%s."),
		*CombatConfig->KinsectSlashAttachSocket.ToString(), KinsectSlashSocketName,
		GlaiveMesh->FindSocket(FName(KinsectSlashSocketName)) ? TEXT("true") : TEXT("false"));
	auto InspectMontage = [](const TCHAR* Label, const UAnimMontage& Montage)
	{
		int32 WindowCount = 0;
		UE_LOG(LogTemp, Display,
			TEXT("[KinsectSlashAssets][Inspect] Montage=%s length=%.6f."),
			Label, Montage.GetPlayLength());
		for (const FAnimNotifyEvent& Event : Montage.Notifies)
		{
			if (const UAnimNotifyState_AttackCollision* Collision =
				Cast<UAnimNotifyState_AttackCollision>(Event.NotifyStateClass))
			{
				++WindowCount;
				UE_LOG(LogTemp, Display,
					TEXT("[KinsectSlashAssets][Inspect] Montage=%s AttackCollision ConfigIndex=%d start=%.6f end=%.6f track=%d."),
					Label, Collision->ConfigIndex, Event.GetTriggerTime(),
					Event.GetEndTriggerTime(), Event.TrackIndex);
			}
			else if (const UAnimNotify_KinsectSlashEvent* SlashEvent =
				Cast<UAnimNotify_KinsectSlashEvent>(Event.Notify))
			{
				UE_LOG(LogTemp, Display,
					TEXT("[KinsectSlashAssets][Inspect] Montage=%s KinsectSlashEvent=%d time=%.6f."),
					Label, static_cast<int32>(SlashEvent->Event), Event.GetTriggerTime());
			}
		}
		UE_LOG(LogTemp, Display,
			TEXT("[KinsectSlashAssets][Inspect] Montage=%s AttackCollisionCount=%d."),
			Label, WindowCount);
	};
	InspectMontage(TEXT("main"), *MainMontage);
	InspectMontage(TEXT("dance"), *DanceMontage);
	for (int32 Index = 0; Index < Ability->AttackSegments.Num(); ++Index)
	{
		const FAttackSegmentConfig& Segment = Ability->AttackSegments[Index];
		UE_LOG(LogTemp, Display,
			TEXT("[KinsectSlashAssets][Inspect] Segment[%d] TraceRegions=%d."),
			Index, Segment.Collision.TraceRegions.Num());
		for (int32 RegionIndex = 0; RegionIndex < Segment.Collision.TraceRegions.Num(); ++RegionIndex)
		{
			const FWeaponTraceRegion& Region = Segment.Collision.TraceRegions[RegionIndex];
			UE_LOG(LogTemp, Display,
				TEXT("[KinsectSlashAssets][Inspect] Segment[%d].Region[%d] Start='%s' End='%s' Radius=%.3f."),
				Index, RegionIndex, *Region.StartSocketName.ToString(),
				*Region.EndSocketName.ToString(), Region.Radius);
		}
	}
	return true;
}

bool MigrateCollisionWindows(const FString& Params, const bool bAuditOnly)
{
	const bool bSegment2InDance = Params.Contains(TEXT("-Segment2InDance"));
	float KinsectStart = 0.0f;
	float Weapon1Start = 0.0f;
	float Weapon1End = 0.0f;
	float Weapon2Start = 0.0f;
	float Weapon2End = 0.0f;
	if (!ParseRequiredTime(Params, TEXT("Weapon1WindowStart="), Weapon1Start)
		|| !ParseRequiredTime(Params, TEXT("Weapon1WindowEnd="), Weapon1End)
		|| !ParseRequiredTime(Params, TEXT("Weapon2WindowStart="), Weapon2Start)
		|| !ParseRequiredTime(Params, TEXT("Weapon2WindowEnd="), Weapon2End))
	{
		return false;
	}

	UAnimMontage* MainMontage = LoadObject<UAnimMontage>(nullptr,
		TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_CaoChongZhan.AM_IG_CaoChongZhan"));
	UAnimMontage* DanceMontage = LoadObject<UAnimMontage>(nullptr,
		TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_CaoChongZhan_Dance.AM_IG_CaoChongZhan_Dance"));
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr,
		TEXT("/Game/Weapons/InsectGlaive/Abilities/GA_IG_CaoChongZhan.GA_IG_CaoChongZhan"));
	UInsectGlaiveCombatConfig* CombatConfig = LoadObject<UInsectGlaiveCombatConfig>(nullptr,
		CombatConfigPath);
	USkeletalMesh* GlaiveMesh = LoadObject<USkeletalMesh>(nullptr,
		GlaiveSkeletalMeshPath);
	UMHGZKinsectSlashAbility* Ability = Blueprint && Blueprint->GeneratedClass
		? Cast<UMHGZKinsectSlashAbility>(Blueprint->GeneratedClass->GetDefaultObject())
		: nullptr;
	if (!MainMontage || !DanceMontage || !Ability
		|| Ability->AttackMontage != MainMontage || Ability->DanceMontage != DanceMontage
		|| !CombatConfig || !GlaiveMesh
		|| Ability->AttackSegments.Num() != 3)
	{
		UE_LOG(LogTemp, Error, TEXT("[KinsectSlashAssets] Migration preflight failed: expected main/dance montage, three-segment GA, DA_IG_Combat, and glaive mesh."));
		return false;
	}
	if (!GetMontageSectionStart(*MainMontage, TEXT("Dash187"), KinsectStart))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[KinsectSlashAssets] Main montage has no Dash187 section; refusing to guess the kinsect window start."));
		return false;
	}

	UAnimSequence* MissSequence = LoadObject<UAnimSequence>(nullptr, MissSequencePath);
	float KinsectEnd = 0.0f;
	if (!MissSequence || !GetMissFrame14MontageTime(*MainMontage,
		*MissSequence, KinsectEnd))
	{
		return false;
	}
	UAnimMontage* Weapon2Montage = bSegment2InDance ? DanceMontage : MainMontage;
	const bool bTimesValid = KinsectStart < KinsectEnd
		&& Weapon1Start < Weapon1End && Weapon1End <= MainMontage->GetPlayLength()
		&& Weapon2Start < Weapon2End && Weapon2End <= Weapon2Montage->GetPlayLength();
	if (!bTimesValid)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[KinsectSlashAssets] Weapon window times are invalid for the selected montage lengths. Times are montage seconds; kinsect window is mapped from Dash187 (%.6f s) to Miss188 source frame 14 (%.6f s)."),
			KinsectStart, KinsectEnd);
		return false;
	}

	const FName AnchorSocket(KinsectSlashSocketName);
	const FName SerializedSocket = CombatConfig->KinsectSlashAttachSocket;
	UE_LOG(LogTemp, Display,
		TEXT("[KinsectSlashAssets][Audit] SKM_IG_Glaive='%s' serializedAnchorSocket='%s' targetAnchorSocket='%s' targetFound=%s RearTip=%s Root=%s."),
		*GlaiveMesh->GetPathName(), *SerializedSocket.ToString(),
		*AnchorSocket.ToString(),
		GlaiveMesh->FindSocket(AnchorSocket) ? TEXT("true") : TEXT("false"),
		HasMeshSocketOrBone(*GlaiveMesh, TEXT("IG_RearTip")) ? TEXT("true") : TEXT("false"),
		HasMeshSocketOrBone(*GlaiveMesh, TEXT("Root")) ? TEXT("true") : TEXT("false"));

	UE_LOG(LogTemp, Display,
		TEXT("[KinsectSlashAssets][%s] Target windows: Segment0 main [%.6f, %.6f] (start from Dash187; end from Miss188 source frame 14); Segment1 main [%.6f, %.6f]; Segment2 %s [%.6f, %.6f]."),
		bAuditOnly ? TEXT("AuditOnly") : TEXT("Migrate"), KinsectStart, KinsectEnd,
		Weapon1Start, Weapon1End, bSegment2InDance ? TEXT("Dance") : TEXT("main"),
		Weapon2Start, Weapon2End);

	int32 LegacyEventCount = 0;
	auto AuditMontage = [&LegacyEventCount](const TCHAR* Label,
		const UAnimMontage& Montage)
	{
		int32 CollisionCount = 0;
		for (const FAnimNotifyEvent& Event : Montage.Notifies)
		{
			if (const UAnimNotifyState_AttackCollision* Collision =
				Cast<UAnimNotifyState_AttackCollision>(Event.NotifyStateClass))
			{
				++CollisionCount;
				UE_LOG(LogTemp, Display,
					TEXT("[KinsectSlashAssets][Audit] montage=%s AttackCollision index=%d start=%.6f end=%.6f trackIndex=%d."),
					Label, Collision->ConfigIndex, Event.GetTriggerTime(),
					Event.GetEndTriggerTime(), Event.TrackIndex);
			}
			if (const UAnimNotify_KinsectSlashEvent* SlashEvent =
				Cast<UAnimNotify_KinsectSlashEvent>(Event.Notify))
			{
				LegacyEventCount += SlashEvent->Event == EKinsectSlashMontageEvent::LateHitOpen
					|| SlashEvent->Event == EKinsectSlashMontageEvent::LateHitClose ? 1 : 0;
			}
		}
		UE_LOG(LogTemp, Display,
			TEXT("[KinsectSlashAssets][Audit] montage=%s has %d AttackCollision state(s)."),
			Label, CollisionCount);
	};
	AuditMontage(TEXT("main"), *MainMontage);
	AuditMontage(TEXT("dance"), *DanceMontage);
	UE_LOG(LogTemp, Display,
		TEXT("[KinsectSlashAssets][%s] Legacy LateHitOpen/Close point count=%d. Only ConfigIndex 0/1/2 windows and those two legacy point events are in migration scope; no unrelated notify or section will be changed."),
		bAuditOnly ? TEXT("AuditOnly") : TEXT("Migrate"), LegacyEventCount);
	for (int32 Index = 0; Index < Ability->AttackSegments.Num(); ++Index)
	{
		const FAttackSegmentConfig& Segment = Ability->AttackSegments[Index];
		UE_LOG(LogTemp, Display,
			TEXT("[KinsectSlashAssets][Audit] AttackSegments[%d]: GE=%s MotionValue=%.4f RequiresHit=%s MultiHitCount=%d Policy=%d MeshTag=%s HitzoneTag=%s TraceRegions=%d."),
			Index,
			Segment.Damage.DamageEffectClass
				? *Segment.Damage.DamageEffectClass->GetPathName() : TEXT("None"),
			Segment.Damage.MotionValue.GetValueAtLevel(1.0f),
			Segment.Damage.bRequiresHitToContinue ? TEXT("true") : TEXT("false"),
			Segment.MultiHitCount, static_cast<int32>(Segment.MultiHitPolicy),
			*Segment.Collision.TraceMeshComponentTag.ToString(),
			*Segment.Collision.HitzoneQueryTag.ToString(),
			Segment.Collision.TraceRegions.Num());
		for (int32 RegionIndex = 0; RegionIndex < Segment.Collision.TraceRegions.Num(); ++RegionIndex)
		{
			const FWeaponTraceRegion& Region = Segment.Collision.TraceRegions[RegionIndex];
			UE_LOG(LogTemp, Display,
				TEXT("[KinsectSlashAssets][Audit] AttackSegments[%d].TraceRegions[%d]: Start='%s' End='%s' Radius=%.3f Spacing=%.3f."),
				Index, RegionIndex, *Region.StartSocketName.ToString(),
				*Region.EndSocketName.ToString(), Region.Radius,
				Region.MaxSampleSpacing);
		}
	}

	bool bPreflightValid = true;
	if (AnchorSocket.IsNone() || !GlaiveMesh->FindSocket(AnchorSocket)
		|| (SerializedSocket != AnchorSocket
			&& SerializedSocket != FName(PreviousKinsectSlashSocketName))
		|| !HasMeshSocketOrBone(*GlaiveMesh, TEXT("IG_RearTip"))
		|| !HasMeshSocketOrBone(*GlaiveMesh, TEXT("Root")))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[KinsectSlashAssets][Audit] Required mesh socket/bone is missing or the serialized socket has an unexpected custom value. Verify '%s' on SKM_IG_Glaive and keep IG_RearTip/Root; only the known previous default '%s' may be migrated."),
			KinsectSlashSocketName, PreviousKinsectSlashSocketName);
		bPreflightValid = false;
	}
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const FAttackSegmentConfig& Segment = Ability->AttackSegments[Index];
		if (!Segment.Damage.DamageEffectClass
			|| Segment.Damage.MotionValue.GetValueAtLevel(1.0f) <= 0.0f
			|| Segment.Damage.bRequiresHitToContinue
			|| Segment.MultiHitCount != 1
			|| Segment.MultiHitPolicy != EAttackMultiHitPolicy::ContactOnly
			|| Segment.Collision.TraceMeshComponentTag != FName(TEXT("WeaponTrace")))
		{
			UE_LOG(LogTemp, Error, TEXT("[KinsectSlashAssets][Audit] AttackSegments[%d] damage policy or WeaponTrace component tag is incompatible; migration will not invent/overwrite data."), Index);
			bPreflightValid = false;
		}
	}
	if (Ability->AttackSegments[2].Collision.TraceRegions.IsEmpty())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[KinsectSlashAssets][Audit] AttackSegments[2] has no authored TraceRegion. Configure its real second weapon-hit path before migration."));
		bPreflightValid = false;
	}
	for (const FWeaponTraceRegion& Region : Ability->AttackSegments[2].Collision.TraceRegions)
	{
		if (Region.EndSocketName.IsNone()
			|| !HasMeshSocketOrBone(*GlaiveMesh, Region.EndSocketName)
			|| (!Region.StartSocketName.IsNone()
				&& !HasMeshSocketOrBone(*GlaiveMesh, Region.StartSocketName)))
		{
			UE_LOG(LogTemp, Error,
				TEXT("[KinsectSlashAssets][Audit] AttackSegments[2] contains a trace endpoint absent from SKM_IG_Glaive; preserving it would produce a non-functional window."));
			bPreflightValid = false;
		}
	}
	if (Ability->AttackSegments[0].Collision.TraceRegions.Num() > 1
		|| Ability->AttackSegments[1].Collision.TraceRegions.Num() > 1)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[KinsectSlashAssets][Audit] Segment 0/1 has multiple authored TraceRegions. Migration refuses to discard extra custom geometry; consolidate or explicitly review it first."));
		bPreflightValid = false;
	}
	const bool bNeedsLegacyRadiusSeed =
		Ability->AttackSegments[0].Collision.TraceRegions.IsEmpty()
		|| Ability->AttackSegments[0].Collision.TraceRegions[0].EndSocketName != AnchorSocket;
	if (bNeedsLegacyRadiusSeed
		&& (!FMath::IsFinite(CombatConfig->KinsectSlashProbeRadius)
			|| CombatConfig->KinsectSlashProbeRadius <= 0.0f))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[KinsectSlashAssets][Audit] Segment 0 has no calibrated Anchor radius and the deprecated ProbeRadius seed is invalid; refusing to invent a radius."));
		bPreflightValid = false;
	}
	if (!bPreflightValid)
	{
		return false;
	}

	if (bAuditOnly)
	{
		return true;
	}

	MainMontage->Modify();
	DanceMontage->Modify();
	Blueprint->Modify();
	Ability->Modify();
	CombatConfig->Modify();
	CombatConfig->KinsectSlashAttachSocket = AnchorSocket;
	RemoveCollisionWindows(*MainMontage, 0);
	RemoveCollisionWindows(*MainMontage, 1);
	RemoveCollisionWindows(*MainMontage, 2);
	RemoveCollisionWindows(*DanceMontage, 0);
	RemoveCollisionWindows(*DanceMontage, 1);
	RemoveCollisionWindows(*DanceMontage, 2);

	FAttackSegmentConfig& KinsectSegment = Ability->AttackSegments[0];
	FWeaponTraceRegion KinsectRegion = KinsectSegment.Collision.TraceRegions.IsEmpty()
		? FWeaponTraceRegion() : KinsectSegment.Collision.TraceRegions[0];
	if (bNeedsLegacyRadiusSeed)
	{
		KinsectRegion.Radius = CombatConfig->KinsectSlashProbeRadius;
	}
	KinsectSegment.Collision.TraceRegions.Reset();
	KinsectRegion.StartSocketName = NAME_None;
	KinsectRegion.EndSocketName = AnchorSocket;
	KinsectSegment.Collision.TraceRegions.Add(KinsectRegion);

	FAttackSegmentConfig& FirstWeaponSegment = Ability->AttackSegments[1];
	FWeaponTraceRegion FirstWeaponRegion = FirstWeaponSegment.Collision.TraceRegions.IsEmpty()
		? FWeaponTraceRegion() : FirstWeaponSegment.Collision.TraceRegions[0];
	FirstWeaponSegment.Collision.TraceRegions.Reset();
	FirstWeaponRegion.StartSocketName = TEXT("IG_RearTip");
	FirstWeaponRegion.EndSocketName = TEXT("Root");
	FirstWeaponSegment.Collision.TraceRegions.Add(FirstWeaponRegion);

	RemoveLegacyLateHitEvents(*MainMontage);
	RemoveLegacyLateHitEvents(*DanceMontage);
	RemoveCollisionWindows(*Weapon2Montage, 2);
	const bool bAdded = AddCollisionWindow(*MainMontage, 0, KinsectStart, KinsectEnd)
		&& AddCollisionWindow(*MainMontage, 1, Weapon1Start, Weapon1End)
		&& AddCollisionWindow(*Weapon2Montage, 2, Weapon2Start, Weapon2End);
	if (!bAdded)
	{
		UE_LOG(LogTemp, Error, TEXT("[KinsectSlashAssets] Failed adding one or more AttackCollision windows; no packages were saved."));
		return false;
	}

	MainMontage->RefreshCacheData();
	DanceMontage->RefreshCacheData();
	const bool bWindowsValid =
		HasExactCollisionWindow(*MainMontage, 0, KinsectStart, KinsectEnd)
		&& HasExactCollisionWindow(*MainMontage, 1, Weapon1Start, Weapon1End)
		&& HasExactCollisionWindow(*Weapon2Montage, 2, Weapon2Start, Weapon2End)
		&& HasNoLegacyLateHitEvents(*MainMontage)
		&& HasNoLegacyLateHitEvents(*DanceMontage);
	if (!bWindowsValid)
	{
		UE_LOG(LogTemp, Error, TEXT("[KinsectSlashAssets] Post-migration notify audit failed; no package saved."));
		return false;
	}
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	const UMHGZKinsectSlashAbility* CompiledAbility = Blueprint->GeneratedClass
		? Cast<UMHGZKinsectSlashAbility>(Blueprint->GeneratedClass->GetDefaultObject())
		: nullptr;
	if (Blueprint->Status == BS_Error || !CompiledAbility
		|| CompiledAbility->AttackMontage != MainMontage
		|| CompiledAbility->DanceMontage != DanceMontage
		|| CompiledAbility->AttackSegments.Num() != 3
		|| CompiledAbility->AttackSegments[0].Collision.TraceRegions.Num() != 1
		|| CompiledAbility->AttackSegments[0].Collision.TraceRegions[0].EndSocketName != AnchorSocket
		|| CompiledAbility->AttackSegments[1].Collision.TraceRegions.Num() != 1
		|| CompiledAbility->AttackSegments[1].Collision.TraceRegions[0].StartSocketName != FName(TEXT("IG_RearTip"))
		|| CompiledAbility->AttackSegments[1].Collision.TraceRegions[0].EndSocketName != FName(TEXT("Root")))
	{
		UE_LOG(LogTemp, Error, TEXT("[KinsectSlashAssets] Compiled GA no longer matches the three-window migration contract; no package saved."));
		return false;
	}
	return SaveAsset(*MainMontage) && SaveAsset(*DanceMontage)
		&& SaveAsset(*Blueprint) && SaveAsset(*CombatConfig);
}

FGameplayTag RequireTag(const TCHAR* Name)
{
	return FGameplayTag::RequestGameplayTag(FName(Name), false);
}

void ConfigureTransition(FComboTransition& Transition, const FName TransitionID,
	const FGameplayTag InputTag, UClass* AbilityClass,
	const FGameplayTagContainer& Required, const FGameplayTagContainer& Blocked)
{
	Transition.TransitionID = TransitionID;
	Transition.SourceState = NAME_None;
	Transition.bMatchAnyState = true;
	Transition.BlockedSourceStates.Reset();
	Transition.InputTag = InputTag;
	Transition.Direction = EDirectionalInput::None;
	Transition.ExecutionPolicy = EComboExecutionPolicy::ActivateAbility;
	Transition.AbilityClass = AbilityClass;
	Transition.MontageBlendInTime = -1.0f;
	Transition.MaxCorrectionAngle = -1.0f;
	Transition.TargetState = TEXT("IG.Aerial.KinsectSlash");
	Transition.StatePolicy = EComboStatePolicy::Replace;
	Transition.LandingPolicy = EComboLandingPolicy::AbilityOwned;
	Transition.RequiredTags = Required;
	Transition.BlockedTags = Blocked;
	Transition.StaminaRequired = 0.0f;
	Transition.bRequiresComboWindow = false;
	Transition.bRequiresDodgeAcceptWindow = false;
	Transition.GrantTiming = ETransitionGrantTiming::OnActivation;
	Transition.GrantedTags.Reset();
	Transition.bAutoTransition = false;
	Transition.Priority = 1000;
}

bool TransitionCanMatchAerialInput(const FComboTransition& Transition,
	const FGameplayTag InputTag, const FGameplayTagContainer& LiveTags)
{
	return !Transition.bAutoTransition && Transition.InputTag == InputTag
		&& (Transition.Direction == EDirectionalInput::None
			|| Transition.Direction == EDirectionalInput::Forward
			|| Transition.Direction == EDirectionalInput::Back
			|| Transition.Direction == EDirectionalInput::Left
			|| Transition.Direction == EDirectionalInput::Right)
		&& LiveTags.HasAll(Transition.RequiredTags)
		&& !LiveTags.HasAny(Transition.BlockedTags)
		&& !Transition.bRequiresComboWindow
		&& !Transition.bRequiresDodgeAcceptWindow;
}

bool AddOrUpdateRoutes(UMHGZWeaponComboData& ComboData, UClass* AbilityClass,
	FString& OutAudit)
{
	const FGameplayTag BTag = RequireTag(TEXT("Input.Weapon.B"));
	const FGameplayTag RTBTag = RequireTag(TEXT("Input.Weapon.RTB"));
	const FGameplayTag Falling = RequireTag(TEXT("Combat.State.Aerial.Falling"));
	const FGameplayTag Unsheathed = RequireTag(TEXT("Combat.State.Unsheathed"));
	const FGameplayTag CantAttack = RequireTag(TEXT("Combat.State.Aerial.CantAttack"));
	const FGameplayTag Grounded = RequireTag(TEXT("Combat.State.Grounded"));
	const FGameplayTag Dead = RequireTag(TEXT("Combat.State.Dead"));
	const FGameplayTag Hitstun = RequireTag(TEXT("Combat.State.Hitstun"));
	const FGameplayTag Knockdown = RequireTag(TEXT("Combat.State.Knockdown"));
	if (!BTag.IsValid() || !RTBTag.IsValid() || !Falling.IsValid()
		|| !Unsheathed.IsValid() || !CantAttack.IsValid()
		|| !Grounded.IsValid() || !Dead.IsValid() || !Hitstun.IsValid()
		|| !Knockdown.IsValid() || !AbilityClass)
	{
		return false;
	}

	FGameplayTagContainer Required;
	Required.AddTag(Falling);
	Required.AddTag(Unsheathed);
	FGameplayTagContainer Blocked;
	Blocked.AddTag(CantAttack);
	Blocked.AddTag(Grounded);
	Blocked.AddTag(Dead);
	Blocked.AddTag(Hitstun);
	Blocked.AddTag(Knockdown);
	FGameplayTagContainer AerialLiveTags = Required;

	const FName IDs[] = { TEXT("Air_KinsectSlash_B"), TEXT("Air_KinsectSlash_RTB") };
	const FGameplayTag Inputs[] = { BTag, RTBTag };
	TArray<FString> Conflicts;
	for (int32 RouteIndex = 0; RouteIndex < UE_ARRAY_COUNT(IDs); ++RouteIndex)
	{
		for (const FComboTransition& Existing : ComboData.Transitions)
		{
			if (Existing.TransitionID != IDs[RouteIndex]
				&& TransitionCanMatchAerialInput(Existing, Inputs[RouteIndex], AerialLiveTags))
			{
				Conflicts.Add(FString::Printf(TEXT("%s:%s->%s"),
					*Existing.TransitionID.ToString(), *Inputs[RouteIndex].ToString(),
					*Existing.TargetState.ToString()));
			}
		}
	}
	if (!Conflicts.IsEmpty())
	{
		OutAudit = FString::Join(Conflicts, TEXT("; "));
		return false;
	}
	// Remove only the route generated by the earlier LT+B mistake. The ground
	// LT+B recall route has a different TransitionID and remains untouched.
	ComboData.Transitions.RemoveAll([](const FComboTransition& Transition)
	{
		return Transition.TransitionID == FName(TEXT("Air_KinsectSlash_LTB"))
			&& Transition.AbilityClass
			&& Transition.AbilityClass->IsChildOf(UMHGZKinsectSlashAbility::StaticClass());
	});

	for (int32 RouteIndex = 0; RouteIndex < UE_ARRAY_COUNT(IDs); ++RouteIndex)
	{
		FComboTransition* Transition = ComboData.Transitions.FindByPredicate(
			[&IDs, RouteIndex](const FComboTransition& Candidate)
			{ return Candidate.TransitionID == IDs[RouteIndex]; });
		if (!Transition)
		{
			Transition = &ComboData.Transitions.AddDefaulted_GetRef();
		}
		ConfigureTransition(*Transition, IDs[RouteIndex], Inputs[RouteIndex],
			AbilityClass, Required, Blocked);
	}
	OutAudit = TEXT("B and RT+B transitions added/updated without an aerial-match conflict");
	return true;
}

bool EnsureRTBChord(UWeaponInputProfile& Profile, bool& bOutChanged)
{
	bOutChanged = false;
	const FGameplayTag B = RequireTag(TEXT("Input.Weapon.B"));
	const FGameplayTag RT = RequireTag(TEXT("Input.Modifier.RT"));
	const FGameplayTag RTB = RequireTag(TEXT("Input.Weapon.RTB"));
	if (!B.IsValid() || !RT.IsValid() || !RTB.IsValid())
	{
		return false;
	}
	for (const FWeaponChordDefinition& Chord : Profile.Chords)
	{
		if (Chord.OutputTag == RTB)
		{
			return Chord.TriggerControls.Num() == 1
				&& Chord.TriggerControls.Contains(B)
				&& Chord.RequiredHeldModifiers.Num() == 1
				&& Chord.RequiredHeldModifiers.Contains(RT)
				&& Chord.DispatchPolicy == EWeaponChordDispatchPolicy::OnPress
				&& Chord.bConsumeTriggerControls;
		}
	}
	Profile.Modify();
	FWeaponChordDefinition& Chord = Profile.Chords.AddDefaulted_GetRef();
	Chord.OutputTag = RTB;
	Chord.TriggerControls.Add(B);
	Chord.RequiredHeldModifiers.Add(RT);
	Chord.bModifiersMustPrecedeTriggers = true;
	Chord.bRequireExactModifiers = true;
	Chord.bConsumeTriggerControls = true;
	Chord.DispatchPolicy = EWeaponChordDispatchPolicy::OnPress;
	Chord.Priority = 100;
	Chord.AimSnapshotContext = EWeaponAimSnapshotContext::Kinsect;
	Profile.MarkPackageDirty();
	bOutChanged = true;
	return true;
}

bool HasExactRequiredContext(const FWeaponChordDefinition& Chord,
	const TArray<FGameplayTag>& Expected)
{
	if (Chord.RequiredContextTags.Num() != Expected.Num()
		|| !Chord.BlockedContextTags.IsEmpty())
	{
		return false;
	}
	for (const FGameplayTag& Tag : Expected)
	{
		if (!Chord.RequiredContextTags.HasTagExact(Tag))
		{
			return false;
		}
	}
	return true;
}

bool ConfigureRTInputOnly(const bool bAuditOnly)
{
	UWeaponRuntimeDefinition* Runtime = LoadObject<UWeaponRuntimeDefinition>(
		nullptr, WeaponRuntimeDefinitionPath);
	UWeaponInputProfile* Profile = Runtime ? Runtime->InputProfile.Get() : nullptr;
	if (bAuditOnly)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[KinsectSlashRTInput] RuntimeDefinition=%s InputProfile=%s ChordGracePeriod=%.3fs Chords=%d RawMappings=%d"),
			Runtime ? *Runtime->GetPathName() : TEXT("missing"),
			Profile ? *Profile->GetPathName() : TEXT("missing"),
			Profile ? Profile->ChordGracePeriod : 0.0f,
			Profile ? Profile->Chords.Num() : 0,
			Profile ? Profile->RawActionToPhysicalInputTag.Num() : 0);
		if (Profile)
		{
			for (const TPair<TObjectPtr<UInputAction>, FGameplayTag>& Mapping :
				Profile->RawActionToPhysicalInputTag)
			{
				UE_LOG(LogTemp, Display,
					TEXT("[KinsectSlashRTInput] RawAction %s -> %s"),
					Mapping.Key ? *Mapping.Key->GetPathName() : TEXT("null"),
					*Mapping.Value.ToString());
			}
			auto TagsToString = [](const TArray<FGameplayTag>& Tags)
			{
				TArray<FString> Values;
				for (const FGameplayTag& Tag : Tags)
				{
					Values.Add(Tag.ToString());
				}
				return FString::Join(Values, TEXT("+"));
			};
			for (int32 Index = 0; Index < Profile->Chords.Num(); ++Index)
			{
				const FWeaponChordDefinition& Chord = Profile->Chords[Index];
				UE_LOG(LogTemp, Display,
					TEXT("[KinsectSlashRTInput] Chord[%d] out=%s triggers=%s modifiers=%s requiredContext=%s blockedContext=%s policy=%d graceOverride=%.3f consume=%s release=%s maxPress=%.3f"),
					Index, *Chord.OutputTag.ToString(), *TagsToString(Chord.TriggerControls),
					*TagsToString(Chord.RequiredHeldModifiers),
					*Chord.RequiredContextTags.ToStringSimple(),
					*Chord.BlockedContextTags.ToStringSimple(),
					static_cast<int32>(Chord.DispatchPolicy),
					Chord.ChordGracePeriodOverrideSeconds,
					Chord.bConsumeTriggerControls ? TEXT("true") : TEXT("false"),
					*Chord.ReleaseControlTag.ToString(), Chord.MaxPressDurationSeconds);
			}
		}
	}
	const FGameplayTag RTInput = RequireTag(TEXT("Input.Modifier.RT"));
	const FGameplayTag RTOutput = RequireTag(TEXT("Input.Weapon.RT"));
	const FGameplayTag Sheathed = RequireTag(TEXT("Combat.State.Sheathed"));
	const FGameplayTag Unsheathed = RequireTag(TEXT("Combat.State.Unsheathed"));
	const FGameplayTag Grounded = RequireTag(TEXT("Combat.State.Grounded"));
	const FGameplayTag Aerial = RequireTag(TEXT("Combat.State.Aerial"));
	if (!Profile || !RTInput.IsValid() || !RTOutput.IsValid()
		|| !Sheathed.IsValid() || !Unsheathed.IsValid()
		|| !Grounded.IsValid() || !Aerial.IsValid())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[KinsectSlashRTInput] Runtime definition, InputProfile, or required GameplayTags are missing."));
		return false;
	}
	bool bMappedRTInput = false;
	for (const TPair<TObjectPtr<UInputAction>, FGameplayTag>& Mapping :
		Profile->RawActionToPhysicalInputTag)
	{
		bMappedRTInput |= Mapping.Value == RTInput;
	}
	if (!bMappedRTInput)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[KinsectSlashRTInput] InputProfile has no RawAction mapped to Input.Modifier.RT."));
		return false;
	}

	const TArray<FGameplayTag> UnsheathedGroundContext{ Unsheathed, Grounded };
	const TArray<FGameplayTag> SheathedGroundContext{ Sheathed, Grounded };
	const TArray<FGameplayTag> AerialContext{ Aerial };
	const TArray<TArray<FGameplayTag>> ExpectedContexts{
		UnsheathedGroundContext, SheathedGroundContext, AerialContext };
	const TCHAR* RouteLabels[] = { TEXT("Unsheathed+Grounded"), TEXT("Sheathed+Grounded"), TEXT("Aerial") };
	const EWeaponChordDispatchPolicy ExpectedPolicies[] = {
		EWeaponChordDispatchPolicy::OnReleaseWithinMaxDurationIfUnconsumed,
		EWeaponChordDispatchPolicy::OnPressAfterChordGrace,
		EWeaponChordDispatchPolicy::OnPressAfterChordGrace };
	TArray<int32> RouteIndices;
	RouteIndices.Init(INDEX_NONE, ExpectedContexts.Num());
	int32 LegacyContextFreeRouteIndex = INDEX_NONE;

	for (int32 ChordIndex = 0; ChordIndex < Profile->Chords.Num(); ++ChordIndex)
	{
		const FWeaponChordDefinition& Chord = Profile->Chords[ChordIndex];
		if (Chord.OutputTag != RTOutput)
		{
			continue;
		}
		if (Chord.TriggerControls.Num() != 1 || Chord.TriggerControls[0] != RTInput
			|| !Chord.RequiredHeldModifiers.IsEmpty())
		{
			UE_LOG(LogTemp, Error,
				TEXT("[KinsectSlashRTInput] Input.Weapon.RT chord %d is not a single RT trigger without modifiers."),
				ChordIndex);
			return false;
		}

		bool bMatchedExpectedContext = false;
		for (int32 ContextIndex = 0; ContextIndex < ExpectedContexts.Num(); ++ContextIndex)
		{
			if (!HasExactRequiredContext(Chord, ExpectedContexts[ContextIndex]))
			{
				continue;
			}
			if (RouteIndices[ContextIndex] != INDEX_NONE)
			{
				UE_LOG(LogTemp, Error,
					TEXT("[KinsectSlashRTInput] More than one Input.Weapon.RT route matches context %s."),
					RouteLabels[ContextIndex]);
				return false;
			}
			RouteIndices[ContextIndex] = ChordIndex;
			bMatchedExpectedContext = true;
			break;
		}
		if (bMatchedExpectedContext)
		{
			continue;
		}

		if (Chord.RequiredContextTags.IsEmpty() && Chord.BlockedContextTags.IsEmpty()
			&& LegacyContextFreeRouteIndex == INDEX_NONE)
		{
			LegacyContextFreeRouteIndex = ChordIndex;
			continue;
		}

		UE_LOG(LogTemp, Error,
			TEXT("[KinsectSlashRTInput] Input.Weapon.RT chord %d has an unsupported context %s (blocked %s)."),
			ChordIndex, *Chord.RequiredContextTags.ToStringSimple(),
			*Chord.BlockedContextTags.ToStringSimple());
		return false;
	}

	// Upgrade the existing context-free RT route in place as the unsheathed-ground
	// release route. Any other missing context route is appended without touching
	// unrelated chords in the profile.
	if (LegacyContextFreeRouteIndex != INDEX_NONE)
	{
		if (RouteIndices[0] != INDEX_NONE)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[KinsectSlashRTInput] A context-free RT route conflicts with the existing unsheathed-ground route."));
			return false;
		}
		RouteIndices[0] = LegacyContextFreeRouteIndex;
	}
	FWeaponChordDefinition RouteTemplate;
	bool bHasRouteTemplate = false;
	if (LegacyContextFreeRouteIndex != INDEX_NONE)
	{
		RouteTemplate = Profile->Chords[LegacyContextFreeRouteIndex];
		bHasRouteTemplate = true;
	}
	else
	{
		for (const int32 RouteIndex : RouteIndices)
		{
			if (RouteIndex != INDEX_NONE)
			{
				RouteTemplate = Profile->Chords[RouteIndex];
				bHasRouteTemplate = true;
				break;
			}
		}
	}
	for (int32 ContextIndex = 0; ContextIndex < RouteIndices.Num(); ++ContextIndex)
	{
		if (RouteIndices[ContextIndex] == INDEX_NONE)
		{
			UE_LOG(LogTemp, Display,
				TEXT("[KinsectSlashRTInput] Missing target route for context %s; %s will add it."),
				RouteLabels[ContextIndex], bAuditOnly ? TEXT("audit reports failure") : TEXT("apply"));
		}
	}
	if (bAuditOnly && RouteIndices.Contains(INDEX_NONE))
	{
		return false;
	}

	if (!bAuditOnly)
	{
		Profile->Modify();
		for (int32 ContextIndex = 0; ContextIndex < RouteIndices.Num(); ++ContextIndex)
		{
			if (RouteIndices[ContextIndex] == INDEX_NONE)
			{
				RouteIndices[ContextIndex] = bHasRouteTemplate
					? Profile->Chords.Add(RouteTemplate)
					: Profile->Chords.AddDefaulted();
			}
		}
	}

	bool bChanged = false;
	if (!FMath::IsNearlyEqual(Profile->ChordGracePeriod, 0.05f))
	{
		UE_LOG(LogTemp, Display,
			TEXT("[KinsectSlashRTInput] Preserving profile-wide grace %.3fs; applying 0.050s only to chords that contain RT."),
			Profile->ChordGracePeriod);
	}
	for (FWeaponChordDefinition& Chord : Profile->Chords)
	{
		const bool bContainsRT = Chord.TriggerControls.Contains(RTInput)
			|| Chord.RequiredHeldModifiers.Contains(RTInput);
		if (bContainsRT && !FMath::IsNearlyEqual(Chord.ChordGracePeriodOverrideSeconds, 0.05f))
		{
			bChanged = true;
			if (!bAuditOnly)
			{
				Chord.ChordGracePeriodOverrideSeconds = 0.05f;
			}
		}
	}

	auto SetField = [&bChanged](auto& Field, const auto& Value)
	{
		if (Field != Value)
		{
			Field = Value;
			bChanged = true;
		}
	};
	for (int32 ContextIndex = 0; ContextIndex < RouteIndices.Num(); ++ContextIndex)
	{
		if (RouteIndices[ContextIndex] == INDEX_NONE)
		{
			continue;
		}
		FWeaponChordDefinition& Chord = Profile->Chords[RouteIndices[ContextIndex]];
		FGameplayTagContainer ExpectedContextContainer;
		for (const FGameplayTag& ContextTag : ExpectedContexts[ContextIndex])
		{
			ExpectedContextContainer.AddTag(ContextTag);
		}
		const bool bContextMatches = Chord.RequiredContextTags.Num() == ExpectedContextContainer.Num()
			&& Chord.RequiredContextTags.HasAllExact(ExpectedContextContainer);
		if (!bContextMatches)
		{
			bChanged = true;
			if (!bAuditOnly)
			{
				Chord.RequiredContextTags = ExpectedContextContainer;
			}
		}
		SetField(Chord.OutputTag, RTOutput);
		SetField(Chord.TriggerControls, TArray<FGameplayTag>{ RTInput });
		SetField(Chord.RequiredHeldModifiers, TArray<FGameplayTag>());
		SetField(Chord.BlockedContextTags, FGameplayTagContainer());
		SetField(Chord.DispatchPolicy, ExpectedPolicies[ContextIndex]);
		SetField(Chord.ChordGracePeriodOverrideSeconds, 0.05f);
		SetField(Chord.bRequireExactModifiers, true);
		SetField(Chord.bConsumeTriggerControls, true);
		SetField(Chord.ReleaseControlTag, FGameplayTag());
		SetField(Chord.AdditionalReleaseControlTags, TArray<FGameplayTag>());
		if (ContextIndex == 0)
		{
			SetField(Chord.MaxPressDurationSeconds, 0.25f);
		}
	}

	const bool bMatches = !bChanged;
	if (bAuditOnly)
	{
		if (bMatches)
		{
			UE_LOG(LogTemp, Display,
				TEXT("[KinsectSlashRTInput][AuditOnly] profile=%s global grace=%.3fs; unsheathed-ground RT uses short-release max=%.3fs; sheathed-ground and aerial RT wait per-route grace=0.050s; status=match."),
				*Profile->GetPathName(), Profile->ChordGracePeriod,
				Profile->Chords[RouteIndices[0]].MaxPressDurationSeconds);
		}
		else
		{
			UE_LOG(LogTemp, Error,
				TEXT("[KinsectSlashRTInput][AuditOnly] profile=%s does not match target RT routing; run -ConfigureRTInputOnly -Apply."),
				*Profile->GetPathName());
		}
		return bMatches;
	}
	if (bChanged)
	{
		Profile->MarkPackageDirty();
		if (!SaveAsset(*Profile))
		{
			UE_LOG(LogTemp, Error, TEXT("[KinsectSlashRTInput] Failed to save InputProfile."));
			return false;
		}
	}
	UE_LOG(LogTemp, Display,
		TEXT("[KinsectSlashRTInput][Apply] Updated only %s: unsheathed-ground RT short-release max=0.250s; sheathed-ground/aerial RT grace=0.050s; RT chords keep 0.050s local arbitration."),
		*Profile->GetPathName());
	return true;
}

FString EscapeJson(const FString& Value)
{
	return Value.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\""));
}

namespace CapturedMotionCalibration
{
constexpr TCHAR ExistingAbilityPackage[] =
	TEXT("/Game/Blueprints/Ability/InsectGlaive/GA_IG_CaoChongZhan");
constexpr TCHAR Fall157SequencePath[] =
	TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_Fall_W_Jump.AS_Unsh_Fall_W_Jump");
constexpr float PrepDuration = 0.35f;
constexpr float FlightDuration = 1.1833333f;
constexpr float DesiredDanceMontageRuntimeLength = PrepDuration + FlightDuration;
constexpr float DanceBeginTime = 0.001f;
constexpr float AerialHandoffTime = 0.750f;
constexpr float MissGravityMagnitude = 3096.0f;
constexpr float TimeTolerance = 0.00001f;
constexpr double NormalizationTolerance = 0.000001;

// Keep the report local to this mode, without adding a module dependency on Json.
struct FCalibrationJson
{
	TArray<FString> Fields;

	static FString Quote(const FString& Value)
	{
		FString Result(TEXT("\""));
		for (const TCHAR Character : Value)
		{
			if (Character == TEXT('"') || Character == TEXT('\\'))
			{
				Result.AppendChar(TEXT('\\'));
				Result.AppendChar(Character);
			}
			else if (Character < 0x20)
			{
				Result += FString::Printf(TEXT("\\u%04x"), static_cast<uint32>(Character));
			}
			else
			{
				Result.AppendChar(Character);
			}
		}
		return Result + TEXT("\"");
	}

	void Raw(const TCHAR* Key, const FString& Value)
	{
		Fields.Add(Quote(Key) + TEXT(": ") + Value);
	}
	void String(const TCHAR* Key, const FString& Value) { Raw(Key, Quote(Value)); }
	void Number(const TCHAR* Key, const double Value)
	{
		Raw(Key, FMath::IsFinite(Value) ? FString::Printf(TEXT("%.9f"), Value) : TEXT("null"));
	}
	void Bool(const TCHAR* Key, const bool Value) { Raw(Key, Value ? TEXT("true") : TEXT("false")); }
	FString Object() const { return TEXT("{\n") + FString::Join(Fields, TEXT(",\n")) + TEXT("\n}"); }
};

FKinsectSlashDanceEntryTuning CalibratedTuning(FKinsectSlashDanceEntryTuning Tuning)
{
	// Retain all legacy serialized fields and any future unrelated fields.
	Tuning.bUseCapturedTrajectory = true;
	Tuning.PrepDuration = PrepDuration;
	Tuning.FlightDuration = FlightDuration;
	Tuning.LaunchUpSpeed = 1080.0f;
	Tuning.LaunchBackwardSpeed = 300.0f;
	Tuning.GravityMagnitude = 1800.0f;
	Tuning.BackwardAcceleration = 360.0f;
	return Tuning;
}

FString DescribeTuning(const FKinsectSlashDanceEntryTuning& Tuning)
{
	FCalibrationJson Json;
	Json.Bool(TEXT("bUseCapturedTrajectory"), Tuning.bUseCapturedTrajectory);
	Json.Number(TEXT("PrepDuration"), Tuning.PrepDuration);
	Json.Number(TEXT("FlightDuration"), Tuning.FlightDuration);
	Json.Number(TEXT("LaunchUpSpeed"), Tuning.LaunchUpSpeed);
	Json.Number(TEXT("LaunchBackwardSpeed"), Tuning.LaunchBackwardSpeed);
	Json.Number(TEXT("GravityMagnitude"), Tuning.GravityMagnitude);
	Json.Number(TEXT("BackwardAcceleration"), Tuning.BackwardAcceleration);
	FCalibrationJson Legacy;
	Legacy.Number(TEXT("ParameterMode"), static_cast<uint8>(Tuning.ParameterMode));
	Legacy.Number(TEXT("ApexHeight"), Tuning.ApexHeight);
	Legacy.Number(TEXT("Duration"), Tuning.Duration);
	Legacy.Number(TEXT("HorizontalDistance"), Tuning.HorizontalDistance);
	Legacy.String(TEXT("ExplicitLaunchVelocity"), Tuning.ExplicitLaunchVelocity.ToString());
	Json.Raw(TEXT("retained_legacy_fields"), Legacy.Object());
	return Json.Object();
}

FString DescribeAbility(const FKinsectSlashDanceEntryTuning& Dash,
	const FKinsectSlashDanceEntryTuning& Miss, const float MissGravity, const float BlendIn)
{
	FCalibrationJson Json;
	Json.Raw(TEXT("DanceFromDashTuning"), DescribeTuning(Dash));
	Json.Raw(TEXT("DanceFromMissTuning"), DescribeTuning(Miss));
	Json.Number(TEXT("MissGravityMagnitude"), MissGravity);
	Json.Number(TEXT("DanceMontageBlendInTime"), BlendIn);
	return Json.Object();
}

FString DescribeDance(const UAnimMontage& Montage, const FAnimSegment& Segment,
	const UAnimSequence& Source, const int32 SlotIndex, const float Length,
	const float BlendIn, const float BlendOut, const bool bAutoBlendOut)
{
	FCalibrationJson Clip;
	Clip.String(TEXT("path"), Source.GetPathName());
	Clip.Number(TEXT("length"), Source.GetPlayLength());
	Clip.Number(TEXT("RateScale"), Source.RateScale);
	Clip.Number(TEXT("sampled_keys"), Source.GetNumberOfSampledKeys());
	FCalibrationJson SegmentJson;
	SegmentJson.Number(TEXT("slot_index"), SlotIndex);
	SegmentJson.String(TEXT("slot_name"), Montage.SlotAnimTracks[SlotIndex].SlotName.ToString());
	SegmentJson.Number(TEXT("segment_index"), 0);
	SegmentJson.String(TEXT("source_clip"), Segment.GetAnimReference()->GetPathName());
	SegmentJson.Number(TEXT("StartPos"), Segment.StartPos);
	SegmentJson.Number(TEXT("AnimStartTime"), Segment.AnimStartTime);
	SegmentJson.Number(TEXT("AnimEndTime"), Segment.AnimEndTime);
	SegmentJson.Number(TEXT("AnimPlayRate"), Segment.AnimPlayRate);
	SegmentJson.Number(TEXT("effective_play_rate"), Segment.GetValidPlayRate());
	SegmentJson.Number(TEXT("LoopingCount"), Segment.LoopingCount);
	SegmentJson.Number(TEXT("GetLength"), Segment.GetLength());
	SegmentJson.Number(TEXT("CachedPlayLength"), Segment.CachedPlayLength);
	FCalibrationJson Json;
	Json.String(TEXT("path"), Montage.GetPathName());
	Json.Raw(TEXT("source_clip"), Clip.Object());
	Json.Raw(TEXT("segment"), SegmentJson.Object());
	Json.Number(TEXT("length"), Length);
	Json.Number(TEXT("montage_RateScale"), Montage.RateScale);
	Json.Number(TEXT("BlendIn"), BlendIn);
	Json.Number(TEXT("BlendOut"), BlendOut);
	Json.Bool(TEXT("bEnableAutoBlendOut"), bAutoBlendOut);
	Json.Number(TEXT("notify_count"), Montage.Notifies.Num());
	Json.Number(TEXT("notify_track_count"), Montage.AnimNotifyTracks.Num());
	Json.String(TEXT("section_name"), Montage.CompositeSections[0].SectionName.ToString());
	Json.Number(TEXT("section_time"), Montage.CompositeSections[0].GetTime());
	Json.String(TEXT("next_section"), Montage.CompositeSections[0].NextSectionName.ToString());
	return Json.Object();
}

double SourceFrame(const float Time, const FAnimSegment& Segment, const UAnimSequence& Source)
{
	const double SourceTime = Segment.AnimStartTime
		+ (static_cast<double>(Time) - Segment.StartPos) / Segment.GetLength()
			* (Segment.AnimEndTime - Segment.AnimStartTime);
	return SourceTime / Source.GetPlayLength() * (Source.GetNumberOfSampledKeys() - 1);
}

struct FNotifyPlan
{
	FAnimNotifyEvent Before;
	float Start = 0.0f;
	float End = 0.0f;
	float StartOffset = 0.0f;
	float EndOffset = 0.0f;
	bool bExplicitEventTime = false;
};

// Mirrors UE 5.6's offset prediction for the required single section at time zero.
// This calculation does not change the montage in -AuditOnly.
float PredictedOffset(const float Time, const float NewLength, const float OldOffset)
{
	EAnimEventTriggerOffsets::Type Type = EAnimEventTriggerOffsets::NoOffset;
	if (Time == 0.0f)
	{
		Type = EAnimEventTriggerOffsets::OffsetAfter;
	}
	else if (Time == NewLength || FMath::IsNearlyZero(Time))
	{
		Type = EAnimEventTriggerOffsets::OffsetBefore;
	}
	return Type == EAnimEventTriggerOffsets::NoOffset || OldOffset == 0.0f
		? GetTriggerTimeOffsetForType(Type) : OldOffset;
}

FString DescribeNotifyTime(const float Start, const float End, const float StartOffset,
	const float EndOffset, const bool bState, const FAnimSegment& Segment, const UAnimSequence& Source)
{
	FCalibrationJson Json;
	Json.Number(TEXT("time"), Start);
	Json.Number(TEXT("end_time"), End);
	Json.Number(TEXT("duration"), End - Start);
	Json.Number(TEXT("trigger_time"), Start + StartOffset);
	Json.Number(TEXT("end_trigger_time"), bState ? End + StartOffset + EndOffset : Start + StartOffset);
	Json.Number(TEXT("TriggerTimeOffset"), StartOffset);
	Json.Number(TEXT("EndTriggerTimeOffset"), EndOffset);
	Json.Number(TEXT("normalized_start"), (static_cast<double>(Start) - Segment.StartPos) / Segment.GetLength());
	Json.Number(TEXT("normalized_end"), (static_cast<double>(End) - Segment.StartPos) / Segment.GetLength());
	Json.Number(TEXT("source_start_frame"), SourceFrame(Start, Segment, Source));
	Json.Number(TEXT("source_end_frame"), SourceFrame(End, Segment, Source));
	return Json.Object();
}

bool PreservesSourcePositions(const FNotifyPlan& Plan, const float Start, const float End,
	const FAnimSegment& Before, const FAnimSegment& After)
{
	const double OldStart = (static_cast<double>(Plan.Before.GetTime()) - Before.StartPos) / Before.GetLength();
	const double OldEnd = (static_cast<double>(Plan.Before.GetTime() + Plan.Before.GetDuration())
		- Before.StartPos) / Before.GetLength();
	return FMath::Abs(OldStart - (static_cast<double>(Start) - After.StartPos) / After.GetLength())
			<= NormalizationTolerance
		&& FMath::Abs(OldEnd - (static_cast<double>(End) - After.StartPos) / After.GetLength())
			<= NormalizationTolerance;
}

FString DescribeNotifyChanges(const TArray<FNotifyPlan>& Plans, const UAnimMontage& Montage,
	const FAnimSegment& Before, const FAnimSegment& Expected, const FAnimSegment& Actual,
	const UAnimSequence& Source)
{
	TArray<FString> Rows;
	for (int32 Index = 0; Index < Plans.Num(); ++Index)
	{
		const FNotifyPlan& Plan = Plans[Index];
		const FAnimNotifyEvent& Old = Plan.Before;
		const UObject* Object = Old.NotifyStateClass ? static_cast<const UObject*>(Old.NotifyStateClass.Get())
			: static_cast<const UObject*>(Old.Notify.Get());
		const UAnimNotifyState_AttackCollision* Collision = Cast<UAnimNotifyState_AttackCollision>(Old.NotifyStateClass);
		FCalibrationJson Json;
		Json.Number(TEXT("index"), Index);
		Json.String(TEXT("type"), Object ? Object->GetClass()->GetPathName() : TEXT("NamedNotify"));
		Json.String(TEXT("object"), Object ? Object->GetPathName() : TEXT(""));
		Json.String(TEXT("name"), Old.NotifyName.ToString());
		Json.String(TEXT("guid"), Old.Guid.ToString());
		Json.Number(TEXT("TrackIndex"), Old.TrackIndex);
		Json.String(TEXT("track_name"), Montage.AnimNotifyTracks.IsValidIndex(Old.TrackIndex)
			? Montage.AnimNotifyTracks[Old.TrackIndex].TrackName.ToString() : TEXT("missing"));
		Json.Raw(TEXT("ConfigIndex"), Collision ? FString::FromInt(Collision->ConfigIndex) : TEXT("null"));
		Json.Number(TEXT("start_link_method"), Old.GetLinkMethod());
		Json.Number(TEXT("end_link_method"), Old.EndLink.GetLinkMethod());
		Json.Raw(TEXT("old"), DescribeNotifyTime(Old.GetTime(), Old.GetTime() + Old.GetDuration(),
			Old.TriggerTimeOffset, Old.EndTriggerTimeOffset, !!Old.NotifyStateClass, Before, Source));
		Json.Raw(TEXT("expected_new"), DescribeNotifyTime(Plan.Start, Plan.End, Plan.StartOffset,
			Plan.EndOffset, !!Old.NotifyStateClass, Expected, Source));
		Json.Bool(TEXT("source_position_preservation_required"), !Plan.bExplicitEventTime);
		Json.Bool(TEXT("expected_source_normalization_invariant"),
			PreservesSourcePositions(Plan, Plan.Start, Plan.End, Before, Expected));
		if (Montage.Notifies.IsValidIndex(Index))
		{
			const FAnimNotifyEvent& Event = Montage.Notifies[Index];
			Json.Number(TEXT("actual_TrackIndex"), Event.TrackIndex);
			const UAnimNotifyState_AttackCollision* ActualCollision = Cast<UAnimNotifyState_AttackCollision>(Event.NotifyStateClass);
			Json.Raw(TEXT("actual_ConfigIndex"), ActualCollision ? FString::FromInt(ActualCollision->ConfigIndex) : TEXT("null"));
			Json.Raw(TEXT("actual_new"), DescribeNotifyTime(Event.GetTime(), Event.GetTime() + Event.GetDuration(),
				Event.TriggerTimeOffset, Event.EndTriggerTimeOffset, !!Event.NotifyStateClass, Actual, Source));
			Json.Bool(TEXT("actual_source_normalization_invariant"), PreservesSourcePositions(Plan,
				Event.GetTime(), Event.GetTime() + Event.GetDuration(), Before, Actual));
		}
		Rows.Add(Json.Object());
	}
	return TEXT("[\n") + FString::Join(Rows, TEXT(",\n")) + TEXT("\n]");
}

TMap<FName, FString> UntouchedAbilityDefaults(const UMHGZKinsectSlashAbility& Ability)
{
	TMap<FName, FString> Values;
	for (TFieldIterator<FProperty> It(Ability.GetClass()); It; ++It)
	{
		const FProperty& Property = **It;
		const FName Name = Property.GetFName();
		if (Property.HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_NonPIEDuplicateTransient)
			|| Name == TEXT("DanceFromDashTuning") || Name == TEXT("DanceFromMissTuning")
			|| Name == TEXT("MissGravityMagnitude") || Name == TEXT("DanceMontageBlendInTime"))
		{
			continue;
		}
		FString Value;
		Property.ExportText_InContainer(0, Value, &Ability, nullptr,
			const_cast<UMHGZKinsectSlashAbility*>(&Ability), PPF_None);
		Values.Add(Name, MoveTemp(Value));
	}
	return Values;
}

FString DescribeValidation(const UObject& Asset, const EDataValidationResult Result,
	const FDataValidationContext& Context)
{
	FCalibrationJson Json;
	Json.String(TEXT("asset"), Asset.GetPathName());
	Json.String(TEXT("result"), Result == EDataValidationResult::Valid ? TEXT("Valid")
		: Result == EDataValidationResult::Invalid ? TEXT("Invalid") : TEXT("NotValidated"));
	Json.Number(TEXT("errors"), Context.GetNumErrors());
	Json.Number(TEXT("warnings"), Context.GetNumWarnings());
	TArray<FString> Issues;
	for (const FDataValidationContext::FIssue& Issue : Context.GetIssues())
	{
		FCalibrationJson Row;
		Row.Number(TEXT("severity"), Issue.Severity);
		Row.String(TEXT("message"), Issue.Message.ToString());
		Issues.Add(Row.Object());
		UE_LOG(LogTemp, Display, TEXT("[KinsectSlashCalibration] %s: %s"),
			*Asset.GetPathName(), *Issue.Message.ToString());
	}
	Json.Raw(TEXT("issues"), TEXT("[") + FString::Join(Issues, TEXT(",")) + TEXT("]"));
	return Json.Object();
}

FArrayProperty* BranchingMarkerProperty(const UAnimMontage& Montage)
{
	FArrayProperty* Property = FindFProperty<FArrayProperty>(Montage.GetClass(), TEXT("BranchingPointMarkers"));
	const FStructProperty* Inner = Property ? CastField<FStructProperty>(Property->Inner) : nullptr;
	return Inner && Inner->Struct == FBranchingPointMarker::StaticStruct() ? Property : nullptr;
}

void RefreshNotifyCachesPreservingIndices(UAnimMontage& Montage, const FArrayProperty& MarkerProperty)
{
	// RefreshCacheData sorts the owning Notifies array, can move notifies to new
	// tracks, and propagates to child assets. Rebuild only these derived caches.
	// The private marker array is reflected in UE 5.6; its type is checked first.
	TArray<FBranchingPointMarker>& Markers =
		*MarkerProperty.ContainerPtrToValuePtr<TArray<FBranchingPointMarker>>(&Montage);
	Markers.Reset();
	Montage.BranchingPointStateNotifyIndices.Reset();
	for (FAnimNotifyTrack& Track : Montage.AnimNotifyTracks)
	{
		Track.Notifies.Reset(); // Cached pointers only; never reset Montage.Notifies.
	}
	for (int32 Index = 0; Index < Montage.Notifies.Num(); ++Index)
	{
		FAnimNotifyEvent& Event = Montage.Notifies[Index];
		Montage.AnimNotifyTracks[Event.TrackIndex].Notifies.Add(&Event);
		if (Event.IsBranchingPoint())
		{
			Markers.Emplace(Index, Event.GetTriggerTime(), EAnimNotifyEventType::Begin);
			if (Event.NotifyStateClass)
			{
				Markers.Emplace(Index, Event.GetEndTriggerTime(), EAnimNotifyEventType::End);
				Montage.BranchingPointStateNotifyIndices.Add(Index);
			}
		}
	}
	Markers.Sort([](const FBranchingPointMarker& A, const FBranchingPointMarker& B)
	{
		return A.TriggerTime < B.TriggerTime;
	});
}

bool Run(const bool bAuditOnly)
{
	FCalibrationJson Report;
	Report.String(TEXT("mode"), TEXT("CalibrateCapturedMotionOnly"));
	Report.Bool(TEXT("audit_only"), bAuditOnly);
	Report.Number(TEXT("DesiredDanceMontageRuntimeLength"), DesiredDanceMontageRuntimeLength);
	Report.Number(TEXT("DanceBeginTime"), DanceBeginTime);
	Report.Number(TEXT("AerialHandoffTime"), AerialHandoffTime);
	Report.String(TEXT("handoff_provenance"), TEXT("Earliest 189 cancel: source frame 90 / 120 fps = 0.750 seconds; fixed runtime event, not a proportional source-frame window."));
	Report.String(TEXT("source_frame_definition"), TEXT("Display start/end times normalized by FAnimSegment::GetLength(), mapped to the unchanged source clip; trigger offsets reported separately."));
	Report.Number(TEXT("source_normalization_tolerance"), NormalizationTolerance);
	UBlueprint* Blueprint = nullptr;
	UMHGZKinsectSlashAbility* Ability = nullptr;
	UAnimMontage* Dance = nullptr;
	UAnimSequence* Source = nullptr;
	FAnimSegment* Segment = nullptr;
	FAnimSegment BeforeSegment;
	FAnimSegment ExpectedSegment;
	TArray<FNotifyPlan> Plans;
	TArray<FString> SavedPackages;
	int32 SlotIndex = INDEX_NONE;
	bool bWouldChange = false;
	bool bMutated = false;
	bool bCompileAttempted = false;
	bool bCompilePassed = false;
	bool bExpectedNormalizationPassed = false;
	bool bActualChecksPassed = false;
	FString Validation(TEXT("\"not_run\""));
	auto Finish = [&](const bool bSuccess, const FString& Error) -> bool
	{
		Report.String(TEXT("status"), !bSuccess ? TEXT("failed") : bAuditOnly ? TEXT("audit_only")
			: bWouldChange ? TEXT("calibrated") : TEXT("already_calibrated"));
		Report.String(TEXT("error"), Error);
		Report.Bool(TEXT("changed"), bMutated);
		Report.Bool(TEXT("noMutation"), !bMutated);
		Report.Bool(TEXT("would_change"), bWouldChange);
		Report.Bool(TEXT("blueprint_compile_attempted"), bCompileAttempted);
		Report.Bool(TEXT("blueprint_compile_passed"), bCompilePassed);
		Report.Bool(TEXT("expected_source_normalization_passed"), bExpectedNormalizationPassed);
		Report.Bool(TEXT("actual_checks_passed"), bActualChecksPassed);
		Report.Raw(TEXT("data_validation"), Validation);
		Report.Raw(TEXT("saved_packages"), TEXT("[") + FString::Join(SavedPackages, TEXT(",")) + TEXT("]"));
		if (Ability)
		{
			Report.Raw(TEXT("actual_ability"), DescribeAbility(Ability->DanceFromDashTuning,
				Ability->DanceFromMissTuning, Ability->MissGravityMagnitude, Ability->DanceMontageBlendInTime));
		}
		if (Dance && Source && Segment && SlotIndex != INDEX_NONE)
		{
			Report.Raw(TEXT("actual_dance"), DescribeDance(*Dance, *Segment, *Source, SlotIndex,
				Dance->GetPlayLength(), Dance->BlendIn.GetBlendTime(), Dance->BlendOut.GetBlendTime(), Dance->bEnableAutoBlendOut));
			Report.Raw(TEXT("notifies"), DescribeNotifyChanges(Plans, *Dance, BeforeSegment, ExpectedSegment, *Segment, *Source));
		}
		const FString ReportPath = FPaths::ProjectSavedDir() / TEXT("Analysis/KinsectSlashMotionCalibration.json");
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(ReportPath), true);
		const bool bReportSaved = FFileHelper::SaveStringToFile(Report.Object() + TEXT("\n"), *ReportPath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		if (!bSuccess || !bReportSaved)
		{
			UE_LOG(LogTemp, Error, TEXT("[KinsectSlashCalibration] %s; report=%s; report_saved=%d"),
				*Error, *ReportPath, bReportSaved ? 1 : 0);
		}
		else
		{
			UE_LOG(LogTemp, Display, TEXT("[KinsectSlashCalibration] %s; changed=%d; report=%s"),
				bAuditOnly ? TEXT("Read-only plan complete") : TEXT("Calibration verified"), bMutated ? 1 : 0, *ReportPath);
		}
		return bSuccess && bReportSaved;
	};

	// Prefer the moved asset. Never create a Blueprint or hide a wrong parent by
	// falling back to a different asset when the preferred package exists.
	const TCHAR* Package = FPackageName::DoesPackageExist(ExistingAbilityPackage) ? ExistingAbilityPackage
		: FPackageName::DoesPackageExist(AbilityPackage) ? AbilityPackage : nullptr;
	if (!Package)
	{
		return Finish(false, TEXT("Existing GA_IG_CaoChongZhan package is missing at both supported paths."));
	}
	Blueprint = LoadObject<UBlueprint>(nullptr, *FString::Printf(TEXT("%s.GA_IG_CaoChongZhan"), Package));
	Report.String(TEXT("ability_blueprint"), Blueprint ? Blueprint->GetPathName() : Package);
	Report.Bool(TEXT("used_legacy_path_fallback"), Package == AbilityPackage);
	if (!Blueprint || Blueprint->GetOutermost()->GetName() != Package || !Blueprint->ParentClass
		|| !Blueprint->ParentClass->IsChildOf(UMHGZKinsectSlashAbility::StaticClass())
		|| !Blueprint->GeneratedClass || !Blueprint->GeneratedClass->IsChildOf(UMHGZKinsectSlashAbility::StaticClass())
		|| Blueprint->Status == BS_Error)
	{
		return Finish(false, TEXT("Existing GA Blueprint is missing, has the wrong parent, or has a compile error."));
	}
	Ability = Cast<UMHGZKinsectSlashAbility>(Blueprint->GeneratedClass->GetDefaultObject());
	Dance = Ability ? Ability->DanceMontage.Get() : nullptr;
	const UInsectGlaiveCombatConfig* Config = LoadObject<UInsectGlaiveCombatConfig>(nullptr, CombatConfigPath);
	const UAnimMontage* Fall = Config ? Config->GetAerialDodgeFallMontage() : nullptr;
	if (!Ability || !Dance || Dance->GetOutermost()->GetName() != DanceMontagePackage
		|| !FPackageName::DoesPackageExist(Dance->GetOutermost()->GetName()) || !Config || !Fall)
	{
		return Finish(false, TEXT("Existing ability CDO, referenced DanceMontage, combat config, or GetAerialDodgeFallMontage() is missing."));
	}
	Report.String(TEXT("combat_config"), Config->GetPathName());
	Report.String(TEXT("fall_montage_157"), Fall->GetPathName());
	TArray<FString> FallSources;
	for (const FSlotAnimationTrack& Slot : Fall->SlotAnimTracks)
	{
		for (const FAnimSegment& FallSegment : Slot.AnimTrack.AnimSegments)
		{
			if (!FallSegment.GetAnimReference() || FallSegment.GetAnimReference()->GetPathName() != Fall157SequencePath)
			{
				return Finish(false, TEXT("Configured fall montage is not the existing 157 source clip."));
			}
			FallSources.Add(FCalibrationJson::Quote(FallSegment.GetAnimReference()->GetPathName()));
		}
	}
	Report.Raw(TEXT("fall_source_clips"), TEXT("[") + FString::Join(FallSources, TEXT(",")) + TEXT("]"));
	if (FallSources.IsEmpty() || Fall == Dance || Fall->GetSkeleton() != Dance->GetSkeleton()
		|| !FMath::IsFinite(Fall->GetPlayLength()) || Fall->GetPlayLength() <= 0.0f
		|| Dance->HasParentAsset() || Dance->CompositeSections.Num() != 1)
	{
		return Finish(false, TEXT("Fall montage is invalid, or DanceMontage is inherited or does not have exactly one section."));
	}
	int32 SegmentCount = 0;
	for (int32 Index = 0; Index < Dance->SlotAnimTracks.Num(); ++Index)
	{
		const TArray<FAnimSegment>& Segments = Dance->SlotAnimTracks[Index].AnimTrack.AnimSegments;
		SegmentCount += Segments.Num();
		if (Segments.Num() == 1)
		{
			SlotIndex = Index;
		}
	}
	if (SegmentCount != 1 || SlotIndex == INDEX_NONE)
	{
		return Finish(false, TEXT("DanceMontage must contain exactly one existing segment across all slots."));
	}
	FAnimSegment& ExistingSegment = Dance->SlotAnimTracks[SlotIndex].AnimTrack.AnimSegments[0];
	Source = Cast<UAnimSequence>(ExistingSegment.GetAnimReference());
	const FCompositeSection& Section = Dance->CompositeSections[0];
	if (!Source || Source->GetPathName() != DanceSequencePath
		|| !IsValidSequence(Source, static_cast<int32>(DanceHandoffSourceFrame))
		|| Source->GetSkeleton() != Dance->GetSkeleton()
		|| ExistingSegment.StartPos != 0.0f || ExistingSegment.AnimStartTime != 0.0f
		|| ExistingSegment.LoopingCount != 1 || !FMath::IsFinite(ExistingSegment.AnimEndTime)
		|| ExistingSegment.AnimEndTime <= 0.0f || ExistingSegment.AnimEndTime > Source->GetPlayLength() + TimeTolerance
		|| !FMath::IsFinite(ExistingSegment.AnimPlayRate) || !FMath::IsFinite(ExistingSegment.GetValidPlayRate())
		|| ExistingSegment.AnimPlayRate * Source->RateScale <= KINDA_SMALL_NUMBER
		|| !FMath::IsFinite(ExistingSegment.GetLength()) || ExistingSegment.GetLength() <= KINDA_SMALL_NUMBER
		|| !FMath::IsNearlyEqual(Dance->GetPlayLength(), ExistingSegment.GetLength(), TimeTolerance)
		|| Dance->RateScale != 1.0f || Section.GetTime() != 0.0f || Section.GetSlotIndex() != SlotIndex
		|| Section.SectionName != Ability->DanceSectionName || !Section.NextSectionName.IsNone())
	{
		Source = nullptr; // Do not describe an invalid source/section layout as a usable plan.
		return Finish(false, TEXT("DanceMontage must play source 189 forward from frame zero, once, with a terminal section at zero and montage RateScale=1; segment/source/length settings are invalid."));
	}
	Segment = &ExistingSegment;
	BeforeSegment = *Segment;
	ExpectedSegment = BeforeSegment;
	const float OldLength = Dance->GetPlayLength();
	const bool bRetime = !FMath::IsNearlyEqual(BeforeSegment.GetLength(), DesiredDanceMontageRuntimeLength, TimeTolerance);
	if (bRetime)
	{
		ExpectedSegment.AnimPlayRate = FMath::Sign(BeforeSegment.AnimPlayRate)
			* (BeforeSegment.AnimEndTime - BeforeSegment.AnimStartTime)
			/ (DesiredDanceMontageRuntimeLength * FMath::Abs(Source->RateScale));
	}
	const float NewLength = ExpectedSegment.GetLength();
	FArrayProperty* MarkerProperty = BranchingMarkerProperty(*Dance);
	if (!MarkerProperty || !FMath::IsFinite(NewLength) || !FMath::IsFinite(ExpectedSegment.AnimPlayRate)
		|| !FMath::IsNearlyEqual(NewLength, DesiredDanceMontageRuntimeLength, TimeTolerance)
		|| ExpectedSegment.GetValidPlayRate() <= KINDA_SMALL_NUMBER
		|| !(0.0f < DanceBeginTime && DanceBeginTime < AerialHandoffTime && AerialHandoffTime < NewLength))
	{
		return Finish(false, TEXT("UE branching-point cache API, predicted GetLength()/RateScale, or required event bounds failed preflight."));
	}

	const FKinsectSlashDanceEntryTuning ExpectedDash = CalibratedTuning(Ability->DanceFromDashTuning);
	const FKinsectSlashDanceEntryTuning ExpectedMiss = CalibratedTuning(Ability->DanceFromMissTuning);
	Report.Raw(TEXT("old_ability"), DescribeAbility(Ability->DanceFromDashTuning, Ability->DanceFromMissTuning,
		Ability->MissGravityMagnitude, Ability->DanceMontageBlendInTime));
	Report.Raw(TEXT("expected_ability"), DescribeAbility(ExpectedDash, ExpectedMiss, MissGravityMagnitude, DanceBlendTime));
	Report.Raw(TEXT("old_dance"), DescribeDance(*Dance, BeforeSegment, *Source, SlotIndex, OldLength,
		Dance->BlendIn.GetBlendTime(), Dance->BlendOut.GetBlendTime(), Dance->bEnableAutoBlendOut));
	Report.Raw(TEXT("expected_dance"), DescribeDance(*Dance, ExpectedSegment, *Source, SlotIndex, NewLength,
		DanceBlendTime, DanceBlendTime, false));
	Report.Number(TEXT("notify_time_scale"), bRetime ? NewLength / OldLength : 1.0f);
	Report.Bool(TEXT("segment_retime_required"), bRetime);
	int32 DanceBeginCount = 0;
	int32 HandoffCount = 0;
	bool bNotifyChange = false;
	TSet<float> BranchingTimes;
	TMap<int32, TSet<float>> TrackStartTimes;
	for (const FAnimNotifyEvent& Event : Dance->Notifies)
	{
		FNotifyPlan& Plan = Plans.AddDefaulted_GetRef();
		Plan.Before = Event;
		const float Start = Event.GetTime();
		const float End = Start + Event.GetDuration();
		if ((Event.Notify && Event.NotifyStateClass)
			|| !Dance->AnimNotifyTracks.IsValidIndex(Event.TrackIndex)
			|| Event.GetSlotIndex() != SlotIndex || Event.EndLink.GetSlotIndex() != SlotIndex
			|| !FMath::IsFinite(Start) || !FMath::IsFinite(End)
			|| !FMath::IsFinite(Event.TriggerTimeOffset) || !FMath::IsFinite(Event.EndTriggerTimeOffset)
			|| Start < 0.0f || End < Start || End > OldLength + TimeTolerance
			|| (Event.NotifyStateClass && End <= Start))
		{
			return Finish(false, TEXT("An existing Dance notify has invalid timing, duration, slot linkage, or TrackIndex."));
		}
		Plan.Start = bRetime ? Start / OldLength * NewLength : Start;
		Plan.End = bRetime ? End / OldLength * NewLength : End;
		const UAnimNotify_KinsectSlashEvent* SlashEvent = Cast<UAnimNotify_KinsectSlashEvent>(Event.Notify);
		if (SlashEvent && SlashEvent->Event == EKinsectSlashMontageEvent::DanceBegin)
		{
			++DanceBeginCount;
			Plan.Start = Plan.End = DanceBeginTime;
			Plan.bExplicitEventTime = true;
		}
		if (Event.Notify && Event.Notify->IsA<UAnimNotify_IG_AerialHandoff>())
		{
			++HandoffCount;
			Plan.Start = Plan.End = AerialHandoffTime;
			Plan.bExplicitEventTime = true;
		}
		Plan.StartOffset = PredictedOffset(Plan.Start, NewLength, Event.TriggerTimeOffset);
		Plan.EndOffset = PredictedOffset(Plan.End, NewLength, Event.EndTriggerTimeOffset);
		TSet<float>& TrackTimes = TrackStartTimes.FindOrAdd(Event.TrackIndex);
		if (TrackTimes.Contains(Plan.Start))
		{
			return Finish(false, TEXT("Projected notify starts overlap on the same track; refusing a layout that may be relocated on reload."));
		}
		TrackTimes.Add(Plan.Start);
		if (Plan.Start < 0.0f || Plan.End > NewLength + TimeTolerance || Plan.End < Plan.Start
			|| (!Plan.bExplicitEventTime && !PreservesSourcePositions(Plan, Plan.Start, Plan.End, BeforeSegment, ExpectedSegment)))
		{
			return Finish(false, TEXT("Projected notify timing exceeds the new length or changes an existing source-frame window."));
		}
		if (Event.IsBranchingPoint())
		{
			const float Trigger = Plan.Start + Plan.StartOffset;
			const float EndTrigger = Plan.End + Plan.StartOffset + Plan.EndOffset;
			if (BranchingTimes.Contains(Trigger) || (Event.NotifyStateClass
				&& (Trigger == EndTrigger || BranchingTimes.Contains(EndTrigger))))
			{
				return Finish(false, TEXT("Projected branching-point times overlap; UE cannot trigger both markers safely."));
			}
			BranchingTimes.Add(Trigger);
			if (Event.NotifyStateClass) { BranchingTimes.Add(EndTrigger); }
		}
		bNotifyChange |= Start != Plan.Start || End != Plan.End
			|| Event.TriggerTimeOffset != Plan.StartOffset || Event.EndTriggerTimeOffset != Plan.EndOffset;
	}
	Report.Number(TEXT("DanceBegin_count"), DanceBeginCount);
	Report.Number(TEXT("AerialHandoff_count"), HandoffCount);
	if (DanceBeginCount != 1 || HandoffCount != 1)
	{
		return Finish(false, TEXT("Exactly one existing DanceBegin and one existing AerialHandoff notify are required; none will be created or removed."));
	}
	bExpectedNormalizationPassed = true;
	const bool bAbilityChange = !FKinsectSlashDanceEntryTuning::StaticStruct()->CompareScriptStruct(
		&Ability->DanceFromDashTuning, &ExpectedDash, PPF_None)
		|| !FKinsectSlashDanceEntryTuning::StaticStruct()->CompareScriptStruct(&Ability->DanceFromMissTuning, &ExpectedMiss, PPF_None)
		|| Ability->MissGravityMagnitude != MissGravityMagnitude || Ability->DanceMontageBlendInTime != DanceBlendTime;
	const bool bDanceChange = bRetime || bNotifyChange || Dance->BlendIn.GetBlendTime() != DanceBlendTime
		|| Dance->BlendOut.GetBlendTime() != DanceBlendTime || Dance->bEnableAutoBlendOut;
	bWouldChange = bAbilityChange || bDanceChange;
	Report.Bool(TEXT("ability_change_required"), bAbilityChange);
	Report.Bool(TEXT("dance_change_required"), bDanceChange);
	if (bAuditOnly)
	{
		Validation = TEXT("\"not_run_audit_only; projected values only, no compile or asset save\"");
		return Finish(true, TEXT(""));
	}

	const TMap<FName, FString> UntouchedBefore = UntouchedAbilityDefaults(*Ability);
	const TArray<FAnimNotifyTrack> TracksBefore = Dance->AnimNotifyTracks;
	const FCompositeSection SectionBefore = Section;
	if (bDanceChange)
	{
		Dance->Modify();
		bMutated = true;
		Segment->AnimPlayRate = ExpectedSegment.AnimPlayRate; // Only the existing segment's rate changes.
		Dance->SetCompositeLength(Dance->CalculateSequenceLength());
		Dance->BlendIn.SetBlendTime(DanceBlendTime);
		Dance->BlendOut.SetBlendTime(DanceBlendTime);
		Dance->bEnableAutoBlendOut = false;
		Dance->CompositeSections[0].Link(Dance, SectionBefore.GetTime(), SlotIndex);
		for (int32 Index = 0; Index < Plans.Num(); ++Index)
		{
			const FNotifyPlan& Plan = Plans[Index];
			FAnimNotifyEvent& Event = Dance->Notifies[Index];
			Event.Link(Dance, Plan.Start, Plan.Before.GetSlotIndex());
			// Bypass FAnimNotifyEvent::SetTime's stale serialized Duration side effect.
			Event.FAnimLinkableElement::SetTime(Plan.Start, EAnimLinkMethod::Absolute);
			Event.EndLink.Link(Dance, Plan.End, Plan.Before.EndLink.GetSlotIndex());
			Event.EndLink.SetTime(Plan.End, EAnimLinkMethod::Absolute);
			Event.SetDuration(Plan.End - Plan.Start);
		}
		Dance->UpdateLinkableElements();
		RefreshNotifyCachesPreservingIndices(*Dance, *MarkerProperty);
	}
	if (bAbilityChange)
	{
		Blueprint->Modify();
		Ability->Modify();
		bMutated = true;
		Ability->DanceFromDashTuning = ExpectedDash;
		Ability->DanceFromMissTuning = ExpectedMiss;
		Ability->MissGravityMagnitude = MissGravityMagnitude;
		Ability->DanceMontageBlendInTime = DanceBlendTime;
		Blueprint->MarkPackageDirty();
	}
	if (bWouldChange)
	{
		// Resolve existing GE defaults before the scoped GA validation traverses its
		// config. Skip automatic compile-time validation/saves, then validate below.
		UClass* ExtractEffects[] = { Config->RedEffectClass.Get(), Config->WhiteEffectClass.Get(), Config->OrangeEffectClass.Get() };
		for (UClass* Effect : ExtractEffects) { if (Effect) { Effect->GetDefaultObject(); } }
		for (const FAttackSegmentConfig& AttackSegment : Ability->AttackSegments)
		{
			if (AttackSegment.Damage.DamageEffectClass) { AttackSegment.Damage.DamageEffectClass->GetDefaultObject(); }
		}
		bCompileAttempted = true;
		FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipSave
			| EBlueprintCompileOptions::SkipDefaultObjectValidation | EBlueprintCompileOptions::SkipGarbageCollection);
		Ability = Blueprint->GeneratedClass
			? Cast<UMHGZKinsectSlashAbility>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
		bCompilePassed = Blueprint->Status != BS_Error && Ability != nullptr;
		if (!bCompilePassed)
		{
			return Finish(false, TEXT("Scoped GA Blueprint compile failed; no asset packages saved."));
		}
	}

	// Re-fetching the CDO after compilation is essential: the old pointer may have
	// been reinstanced. Compare complete entry structs to preserve legacy fields.
	if (!FKinsectSlashDanceEntryTuning::StaticStruct()->CompareScriptStruct(&Ability->DanceFromDashTuning, &ExpectedDash, PPF_None)
		|| !FKinsectSlashDanceEntryTuning::StaticStruct()->CompareScriptStruct(&Ability->DanceFromMissTuning, &ExpectedMiss, PPF_None)
		|| Ability->MissGravityMagnitude != MissGravityMagnitude || Ability->DanceMontageBlendInTime != DanceBlendTime)
	{
		return Finish(false, TEXT("Compiled CDO did not retain captured tuning values or unchanged legacy tuning fields; no assets saved."));
	}
	const TMap<FName, FString> UntouchedAfter = UntouchedAbilityDefaults(*Ability);
	for (const TPair<FName, FString>& Value : UntouchedBefore)
	{
		const FString* After = UntouchedAfter.Find(Value.Key);
		if (!After || *After != Value.Value)
		{
			return Finish(false, FString::Printf(TEXT("Unrelated GA default changed during compile: %s; no assets saved."), *Value.Key.ToString()));
		}
	}
	if (!FMath::IsNearlyEqual(Dance->GetPlayLength(), NewLength, TimeTolerance)
		|| !FMath::IsNearlyEqual(Dance->CalculateSequenceLength(), NewLength, TimeTolerance)
		|| !FMath::IsNearlyEqual(Segment->GetLength(), DesiredDanceMontageRuntimeLength, TimeTolerance)
		|| Segment->GetAnimReference() != BeforeSegment.GetAnimReference()
		|| Segment->StartPos != BeforeSegment.StartPos || Segment->AnimStartTime != BeforeSegment.AnimStartTime
		|| Segment->AnimEndTime != BeforeSegment.AnimEndTime || Segment->LoopingCount != BeforeSegment.LoopingCount
		|| Segment->CachedPlayLength != BeforeSegment.CachedPlayLength
		|| Dance->Notifies.Num() != Plans.Num() || Dance->AnimNotifyTracks.Num() != TracksBefore.Num()
		|| Dance->BlendIn.GetBlendTime() != DanceBlendTime || Dance->BlendOut.GetBlendTime() != DanceBlendTime
		|| Dance->bEnableAutoBlendOut || Dance->CompositeSections.Num() != 1
		|| Dance->CompositeSections[0].SectionName != SectionBefore.SectionName
		|| Dance->CompositeSections[0].NextSectionName != SectionBefore.NextSectionName
		|| Dance->CompositeSections[0].GetTime() != SectionBefore.GetTime()
		|| Dance->CompositeSections[0].GetLinkMethod() != SectionBefore.GetLinkMethod())
	{
		return Finish(false, TEXT("Dance segment/section/settings read-back failed; no asset packages saved."));
	}
	for (int32 Index = 0; Index < TracksBefore.Num(); ++Index)
	{
		if (Dance->AnimNotifyTracks[Index].TrackName != TracksBefore[Index].TrackName
			|| Dance->AnimNotifyTracks[Index].TrackColor != TracksBefore[Index].TrackColor)
		{
			return Finish(false, TEXT("A manual notify track changed; no asset packages saved."));
		}
	}
	for (int32 Index = 0; Index < Plans.Num(); ++Index)
	{
		const FNotifyPlan& Plan = Plans[Index];
		const FAnimNotifyEvent& Event = Dance->Notifies[Index];
		if (Event.Notify != Plan.Before.Notify || Event.NotifyStateClass != Plan.Before.NotifyStateClass
			|| Event.NotifyName != Plan.Before.NotifyName || Event.Guid != Plan.Before.Guid || Event.TrackIndex != Plan.Before.TrackIndex
			|| Event.GetLinkMethod() != Plan.Before.GetLinkMethod() || Event.EndLink.GetLinkMethod() != Plan.Before.EndLink.GetLinkMethod()
			|| !FMath::IsNearlyEqual(Event.GetTime(), Plan.Start, TimeTolerance)
			|| !FMath::IsNearlyEqual(Event.GetDuration(), Plan.End - Plan.Start, TimeTolerance)
			|| !FMath::IsNearlyEqual(Event.EndLink.GetTime(), Plan.End, TimeTolerance)
			|| !FMath::IsNearlyEqual(Event.TriggerTimeOffset, Plan.StartOffset, TimeTolerance)
			|| !FMath::IsNearlyEqual(Event.EndTriggerTimeOffset, Plan.EndOffset, TimeTolerance)
			|| Event.GetSlotIndex() != SlotIndex || Event.EndLink.GetSlotIndex() != SlotIndex
			|| Event.GetLinkedMontage() != Dance || Event.EndLink.GetLinkedMontage() != Dance
			|| (!Plan.bExplicitEventTime && !PreservesSourcePositions(Plan, Event.GetTime(),
				Event.GetTime() + Event.GetDuration(), BeforeSegment, *Segment)))
		{
			return Finish(false, FString::Printf(TEXT("Notify %d identity, linkage, timing, offsets, or source normalization changed unexpectedly; no assets saved."), Index));
		}
	}
	bActualChecksPassed = true;
	FDataValidationContext BlueprintContext;
	FDataValidationContext DanceContext;
	const EDataValidationResult BlueprintResult = Blueprint->IsDataValid(BlueprintContext);
	const EDataValidationResult DanceResult = Dance->IsDataValid(DanceContext);
	Validation = TEXT("[") + DescribeValidation(*Blueprint, BlueprintResult, BlueprintContext)
		+ TEXT(",") + DescribeValidation(*Dance, DanceResult, DanceContext) + TEXT("]");
	if (BlueprintResult == EDataValidationResult::Invalid || DanceResult == EDataValidationResult::Invalid
		|| BlueprintContext.GetNumErrors() != 0 || DanceContext.GetNumErrors() != 0)
	{
		return Finish(false, TEXT("Scoped GA/Dance data validation failed; no asset packages saved."));
	}
	if (bDanceChange)
	{
		if (!SaveAsset(*Dance)) { return Finish(false, TEXT("DanceMontage package save failed.")); }
		SavedPackages.Add(FCalibrationJson::Quote(Dance->GetOutermost()->GetName()));
	}
	if (bAbilityChange)
	{
		if (!SaveAsset(*Blueprint)) { return Finish(false, TEXT("GA package save failed; see saved_packages for any successful Dance save.")); }
		SavedPackages.Add(FCalibrationJson::Quote(Blueprint->GetOutermost()->GetName()));
	}
	return Finish(true, TEXT(""));
}
} // namespace CapturedMotionCalibration

namespace FallingGateAndDanceRootRepair
{
// This repair never enters the legacy generators or motion calibration paths.
FGameplayTagContainer* RequiredTags(UMHGZKinsectSlashAbility* Ability)
{
	FStructProperty* Property = Ability
		? FindFProperty<FStructProperty>(Ability->GetClass(), TEXT("ActivationRequiredTags")) : nullptr;
	return Property && Property->Struct == FGameplayTagContainer::StaticStruct()
		? Property->ContainerPtrToValuePtr<FGameplayTagContainer>(Ability) : nullptr;
}

TMap<FName, FString> UnchangedDefaults(const UMHGZKinsectSlashAbility& Ability)
{
	TMap<FName, FString> Values;
	for (TFieldIterator<FProperty> It(Ability.GetClass()); It; ++It)
	{
		const FProperty& Property = **It;
		if (Property.GetFName() == TEXT("ActivationRequiredTags")
			|| Property.HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_NonPIEDuplicateTransient))
		{
			continue;
		}
		FString Value;
		Property.ExportText_InContainer(0, Value, &Ability, nullptr,
			const_cast<UMHGZKinsectSlashAbility*>(&Ability), PPF_None);
		Values.Add(Property.GetFName(), MoveTemp(Value));
	}
	return Values;
}

bool Run(const bool bAuditOnly)
{
	using CapturedMotionCalibration::FCalibrationJson;
	const FGameplayTag Actionable = RequireTag(TEXT("Combat.State.Aerial.Actionable"));
	const FGameplayTag Falling = RequireTag(TEXT("Combat.State.Aerial.Falling"));
	const FGameplayTag Unsheathed = RequireTag(TEXT("Combat.State.Unsheathed"));
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr,
		TEXT("/Game/Blueprints/Ability/InsectGlaive/GA_IG_CaoChongZhan.GA_IG_CaoChongZhan"));
	UMHGZKinsectSlashAbility* Ability = Blueprint && Blueprint->GeneratedClass
		? Cast<UMHGZKinsectSlashAbility>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
	UMHGZWeaponComboData* Combo = LoadObject<UMHGZWeaponComboData>(nullptr, ComboDataPath);
	UAnimSequence* Dance = LoadObject<UAnimSequence>(nullptr, DanceSequencePath);
	UAnimSequence* Fall = LoadObject<UAnimSequence>(nullptr,
		TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_Fall_W_Jump.AS_Unsh_Fall_W_Jump"));
	FCalibrationJson Report;
	Report.Bool(TEXT("audit_only"), bAuditOnly);
	const auto Finish = [&](const bool bSuccess, const FString& Diagnostic)
	{
		Report.Bool(TEXT("success"), bSuccess);
		Report.String(TEXT("diagnostic"), Diagnostic);
		const FString Path = FPaths::ProjectSavedDir() / (bAuditOnly
			? TEXT("AssetProbe/kinsect_slash_falling_gate_root_repair_audit.json")
			: TEXT("AssetProbe/kinsect_slash_falling_gate_root_repair.json"));
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		const bool bWroteReport = FFileHelper::SaveStringToFile(Report.Object(), *Path,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		UE_LOG(LogTemp, Display, TEXT("[KinsectSlashFallingRootRepair] success=%d %s report=%s"),
			bSuccess, *Diagnostic, *Path);
		return bSuccess && bWroteReport;
	};
	FGameplayTagContainer* AbilityRequired = RequiredTags(Ability);
	if (!Ability || !AbilityRequired || !Combo || !Dance || !Fall || !Actionable.IsValid()
		|| !Falling.IsValid() || !Unsheathed.IsValid() || Blueprint->Status == BS_Error)
	{
		return Finish(false, TEXT("Missing existing production assets/tags or invalid Blueprint; nothing saved."));
	}
	bool bDanceReferenced = false;
	if (Ability->DanceMontage)
	{
		for (const FSlotAnimationTrack& Slot : Ability->DanceMontage->SlotAnimTracks)
		{
			for (const FAnimSegment& Segment : Slot.AnimTrack.AnimSegments)
			{
				bDanceReferenced |= Segment.GetAnimReference() == Dance;
			}
		}
	}
	if (!bDanceReferenced || Dance->bEnableRootMotion || Fall->bEnableRootMotion
		|| !Fall->bForceRootLock || Dance->RootMotionRootLock != Fall->RootMotionRootLock
		|| Fall->RootMotionRootLock != ERootMotionRootLock::RefPose)
	{
		return Finish(false, TEXT("189/157 must be the existing presentation-only clips with matching RefPose root-lock mode; nothing saved."));
	}
	TArray<FComboTransition> ExpectedTransitions = Combo->Transitions;
	int32 BRouteCount = 0;
	int32 RTBRouteCount = 0;
	bool bComboChange = false;
	for (FComboTransition& Row : ExpectedTransitions)
	{
		if (Row.TransitionID != TEXT("Air_KinsectSlash_B")
			&& Row.TransitionID != TEXT("Air_KinsectSlash_RTB")) { continue; }
		if (Row.AbilityClass != Blueprint->GeneratedClass
			|| !Row.RequiredTags.HasTagExact(Falling) || !Row.RequiredTags.HasTagExact(Unsheathed))
		{
			return Finish(false, TEXT("Existing B/RT+B rows do not target the production GA with Falling/Unsheathed; nothing saved."));
		}
		BRouteCount += Row.TransitionID == TEXT("Air_KinsectSlash_B") ? 1 : 0;
		RTBRouteCount += Row.TransitionID == TEXT("Air_KinsectSlash_RTB") ? 1 : 0;
		bComboChange |= Row.RequiredTags.HasTagExact(Actionable);
		Row.RequiredTags.RemoveTag(Actionable);
	}
	if (BRouteCount != 1 || RTBRouteCount != 1 || !AbilityRequired->HasTagExact(Falling)
		|| !AbilityRequired->HasTagExact(Unsheathed))
	{
		return Finish(false, TEXT("Require exactly the two existing B/RT+B rows and native Falling/Unsheathed gates; nothing saved."));
	}
	const bool bAbilityChange = AbilityRequired->HasTagExact(Actionable);
	const bool bRootChange = !Dance->bForceRootLock;
	Report.Bool(TEXT("ga_required_actionable_before"), bAbilityChange);
	Report.Bool(TEXT("combo_required_actionable_before"), bComboChange);
	Report.Bool(TEXT("dance_root_locked_before"), !bRootChange);
	Report.Bool(TEXT("fall_root_locked"), Fall->bForceRootLock);
	Report.String(TEXT("dance_asset"), Dance->GetPathName());
	Report.String(TEXT("ga_asset"), Blueprint->GetPathName());
	if (bAuditOnly)
	{
		return Finish(!bAbilityChange && !bComboChange && !bRootChange,
			TEXT("Read-only audit; no asset packages saved."));
	}
	const TMap<FName, FString> Before = UnchangedDefaults(*Ability);
	FGameplayTagContainer ExpectedRequired = *AbilityRequired;
	ExpectedRequired.RemoveTag(Actionable);
	if (bAbilityChange)
	{
		Blueprint->Modify();
		Ability->Modify();
		*AbilityRequired = ExpectedRequired;
		FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipSave
			| EBlueprintCompileOptions::SkipDefaultObjectValidation | EBlueprintCompileOptions::SkipGarbageCollection);
		Ability = Blueprint->GeneratedClass
			? Cast<UMHGZKinsectSlashAbility>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
		AbilityRequired = RequiredTags(Ability);
	}
	if (!Ability || !AbilityRequired || Blueprint->Status == BS_Error || *AbilityRequired != ExpectedRequired)
	{
		return Finish(false, TEXT("Scoped Blueprint compile did not preserve the requested tag-only change; nothing saved."));
	}
	if (Before.OrderIndependentCompareEqual(UnchangedDefaults(*Ability)) == false)
	{
		return Finish(false, TEXT("An unrelated GA default changed; nothing saved."));
	}
	if (bComboChange)
	{
		Combo->Modify();
		for (FComboTransition& Row : Combo->Transitions)
		{
			if (Row.TransitionID == TEXT("Air_KinsectSlash_B")
				|| Row.TransitionID == TEXT("Air_KinsectSlash_RTB")) { Row.RequiredTags.RemoveTag(Actionable); }
		}
	}
	for (int32 Index = 0; Index < Combo->Transitions.Num(); ++Index)
	{
		if (!FComboTransition::StaticStruct()->CompareScriptStruct(
			&ExpectedTransitions[Index], &Combo->Transitions[Index], PPF_None))
		{
			return Finish(false, TEXT("A Combo field other than the two required tags changed; nothing saved."));
		}
	}
	if (bRootChange)
	{
		Dance->Modify();
		Dance->bForceRootLock = true;
	}
	FDataValidationContext ValidationContext;
	const EDataValidationResult ValidationResult = Ability->IsDataValid(ValidationContext);
	if (ValidationResult == EDataValidationResult::Invalid || ValidationContext.GetNumErrors() != 0)
	{
		return Finish(false, TEXT("Scoped GA validation failed; nothing saved."));
	}
	// Each save is explicit; report partial success if a later package cannot save.
	TArray<FString> Saved;
	const auto SaveIfChanged = [&](UObject& Asset, const bool bChanged)
	{
		if (!bChanged) { return true; }
		if (!SaveAsset(Asset)) { return false; }
		Saved.Add(FCalibrationJson::Quote(Asset.GetOutermost()->GetName()));
		return true;
	};
	const bool bSaved = SaveIfChanged(*Blueprint, bAbilityChange)
		&& SaveIfChanged(*Combo, bComboChange) && SaveIfChanged(*Dance, bRootChange);
	Report.Raw(TEXT("saved_packages"), TEXT("[") + FString::Join(Saved, TEXT(",")) + TEXT("]"));
	Report.Bool(TEXT("ga_required_actionable_after"), AbilityRequired->HasTagExact(Actionable));
	Report.Bool(TEXT("dance_root_locked_after"), Dance->bForceRootLock);
	return Finish(bSaved, bSaved ? TEXT("Tag/root-lock-only repair validated; motion tuning and montage assets untouched.")
		: TEXT("Package save failed; see saved_packages for partial writes."));
}
} // namespace FallingGateAndDanceRootRepair
} // namespace UE::MHGZ::KinsectSlashAssetSetup

#endif // WITH_EDITOR

UMHGZM5KinsectSlashAssetSetupCommandlet::UMHGZM5KinsectSlashAssetSetupCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMHGZM5KinsectSlashAssetSetupCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	using namespace UE::MHGZ::KinsectSlashAssetSetup;
	if (FParse::Param(*Params, TEXT("RepairFallingGateAndDanceRootOnly")))
	{
		return FallingGateAndDanceRootRepair::Run(FParse::Param(*Params, TEXT("AuditOnly"))) ? 0 : 6;
	}
	if (FParse::Param(*Params, TEXT("CalibrateCapturedMotionOnly")))
	{
		// This bounded mode returns before all legacy generation/migration paths.
		return CapturedMotionCalibration::Run(FParse::Param(*Params, TEXT("AuditOnly"))) ? 0 : 6;
	}
	if (FParse::Param(*Params, TEXT("ConfigureRTInputOnly")))
	{
		const bool bAuditOnly = FParse::Param(*Params, TEXT("AuditOnly"));
		const bool bApply = FParse::Param(*Params, TEXT("Apply"));
		if (bAuditOnly == bApply)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[KinsectSlashRTInput] Specify exactly one of -AuditOnly or -Apply."));
			return 2;
		}
		return ConfigureRTInputOnly(bAuditOnly) ? 0 : 6;
	}
	const bool bAuditOnly = Params.Contains(TEXT("-AuditOnly"));
	const bool bInspectExistingCollisionWindows =
		Params.Contains(TEXT("-InspectExistingCollisionWindows"));
	const bool bMigrateCollisionWindows = Params.Contains(TEXT("-MigrateCollisionWindowsOnly"));
	if (bInspectExistingCollisionWindows)
	{
		return InspectExistingCollisionWindows() ? 0 : 6;
	}
	if (bMigrateCollisionWindows)
	{
		return MigrateCollisionWindows(Params, bAuditOnly) ? 0 : 6;
	}
	if (!bAuditOnly && Params.Contains(TEXT("-UpgradeThreeHitsOnly")))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[KinsectSlashAssets] -UpgradeThreeHitsOnly is a legacy data-only upgrader and is disabled for production migration. Use -MigrateCollisionWindowsOnly with explicit window times."));
		return 6;
	}
	if (!bAuditOnly && !Params.Contains(TEXT("-ForceLegacyFullRegenerate")))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[KinsectSlashAssets] No write mode selected. The old full generator resets montage tracks/sections and does not author the approved three-window contract. Use -AuditOnly or -MigrateCollisionWindowsOnly; legacy full regeneration requires explicit -ForceLegacyFullRegenerate."));
		return 6;
	}
	if (!bAuditOnly && Params.Contains(TEXT("-ForceLegacyFullRegenerate")))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[KinsectSlashAssets] Running destructive legacy generation by explicit request; it may overwrite the approved collision-window setup."));
	}
	if (!bAuditOnly && Params.Contains(TEXT("-UpgradeThreeHitsOnly")))
	{
		return UpgradeExistingAbilityToThreeHits() ? 0 : 6;
	}

	FSequenceSet Sequences;
	Sequences.Hover = LoadObject<UAnimSequence>(nullptr, HoverSequencePath);
	Sequences.Dash = LoadObject<UAnimSequence>(nullptr, DashSequencePath);
	Sequences.Miss = LoadObject<UAnimSequence>(nullptr, MissSequencePath);
	Sequences.Dance = LoadObject<UAnimSequence>(nullptr, DanceSequencePath);
	if (!IsValidSequence(Sequences.Hover, 1)
		|| !IsValidSequence(Sequences.Dash, 1)
		|| !IsValidSequence(Sequences.Miss,
			static_cast<int32>(MissLateHitCloseSourceFrame))
		|| !IsValidSequence(Sequences.Dance,
			static_cast<int32>(DanceHandoffSourceFrame)))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[KinsectSlashAssets] One or more source sequences are missing, invalid, or have too few sampled keys."));
		return 1;
	}
	if (Sequences.Hover->GetSkeleton() != Sequences.Dash->GetSkeleton()
		|| Sequences.Hover->GetSkeleton() != Sequences.Miss->GetSkeleton()
		|| Sequences.Hover->GetSkeleton() != Sequences.Dance->GetSkeleton())
	{
		UE_LOG(LogTemp, Error, TEXT("[KinsectSlashAssets] Source sequence skeletons differ."));
		return 1;
	}

	UMHGZWeaponComboData* ComboData = LoadObject<UMHGZWeaponComboData>(nullptr, ComboDataPath);
	UInsectGlaiveCombatConfig* CombatConfig = LoadObject<UInsectGlaiveCombatConfig>(
		nullptr, CombatConfigPath);
	UWeaponRuntimeDefinition* RuntimeDefinition = LoadObject<UWeaponRuntimeDefinition>(
		nullptr, WeaponRuntimeDefinitionPath);
	if (!ComboData || !CombatConfig || CombatConfig->ComboData != ComboData)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[KinsectSlashAssets] DA_IG_Combo or DA_IG_Combat reference is missing/mismatched."));
		return 1;
	}

	int32 BRawActionCount = 0;
	int32 RTBChordCount = 0;
	bool bInputProfileChanged = false;
	if (RuntimeDefinition && RuntimeDefinition->InputProfile)
	{
		if (!bAuditOnly && !EnsureRTBChord(*RuntimeDefinition->InputProfile,
			bInputProfileChanged))
		{
			UE_LOG(LogTemp, Error, TEXT("[KinsectSlashAssets] RT+B chord conflicts with existing profile data."));
			return 3;
		}
		const FGameplayTag BTag = RequireTag(TEXT("Input.Weapon.B"));
		const FGameplayTag RTTag = RequireTag(TEXT("Input.Modifier.RT"));
		for (const TPair<TObjectPtr<UInputAction>, FGameplayTag>& Mapping :
			RuntimeDefinition->InputProfile->RawActionToPhysicalInputTag)
		{
			BRawActionCount += Mapping.Value == BTag ? 1 : 0;
		}
		for (const FWeaponChordDefinition& Chord : RuntimeDefinition->InputProfile->Chords)
		{
			RTBChordCount += Chord.OutputTag == RequireTag(TEXT("Input.Weapon.RTB"))
				&& Chord.TriggerControls.Contains(BTag)
				&& Chord.RequiredHeldModifiers.Contains(RTTag) ? 1 : 0;
		}
	}
	const bool bRuntimeWiringValid = RuntimeDefinition
		&& RuntimeDefinition->CombatConfig == CombatConfig
		&& RuntimeDefinition->InputProfile
		&& BRawActionCount > 0 && RTBChordCount > 0;

	FGameplayTagContainer Required;
	Required.AddTag(RequireTag(TEXT("Combat.State.Aerial.Falling")));
	Required.AddTag(RequireTag(TEXT("Combat.State.Unsheathed")));
	FGameplayTagContainer LiveTags = Required;
	TArray<FString> AuditRows;
	for (const FComboTransition& Transition : ComboData->Transitions)
	{
		if (Transition.InputTag == RequireTag(TEXT("Input.Weapon.B"))
			|| Transition.InputTag == RequireTag(TEXT("Input.Weapon.RTB"))
			|| Transition.TransitionID == FName(TEXT("Air_KinsectSlash_LTB")))
		{
			AuditRows.Add(FString::Printf(
				TEXT("{\"id\":\"%s\",\"input\":\"%s\",\"source\":\"%s\",\"any_state\":%s,\"priority\":%d,\"possible_aerial_match\":%s,\"ability\":\"%s\"}"),
				*EscapeJson(Transition.TransitionID.ToString()),
				*EscapeJson(Transition.InputTag.ToString()),
				*EscapeJson(Transition.SourceState.ToString()),
				Transition.bMatchAnyState ? TEXT("true") : TEXT("false"),
				Transition.Priority,
				TransitionCanMatchAerialInput(Transition, Transition.InputTag, LiveTags)
					? TEXT("true") : TEXT("false"),
				Transition.AbilityClass ? *Transition.AbilityClass->GetPathName() : TEXT("")));
		}
	}
	if (bAuditOnly)
	{
		if (!bRuntimeWiringValid)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[KinsectSlashAssets][AuditOnly] Runtime wiring failed: RuntimeDefinition=%s CombatConfigMatches=%s InputProfile=%s BRawActions=%d RTBChordsWithBAndRT=%d."),
				RuntimeDefinition ? *RuntimeDefinition->GetPathName() : TEXT("missing"),
				RuntimeDefinition && RuntimeDefinition->CombatConfig == CombatConfig
					? TEXT("true") : TEXT("false"),
				RuntimeDefinition && RuntimeDefinition->InputProfile
					? *RuntimeDefinition->InputProfile->GetPathName() : TEXT("missing"),
				BRawActionCount, RTBChordCount);
			return 3;
		}
		UE_LOG(LogTemp, Display,
			TEXT("[KinsectSlashAssets][AuditOnly] B/RT+B Combo rows: %s"),
			*FString::Join(AuditRows, TEXT(", ")));
		UE_LOG(LogTemp, Display,
			TEXT("[KinsectSlashAssets][AuditOnly] Runtime wiring: RuntimeDefinition=%s CombatConfig=%s InputProfile=%s BRawActions=%d RTBChordsWithBAndRT=%d."),
			*RuntimeDefinition->GetPathName(), *CombatConfig->GetPathName(),
			*RuntimeDefinition->InputProfile->GetPathName(), BRawActionCount, RTBChordCount);
		return 0;
	}

	FString ConflictAudit;
	for (int32 RouteIndex = 0; RouteIndex < 2; ++RouteIndex)
	{
		const FGameplayTag Input = RequireTag(RouteIndex == 0
			? TEXT("Input.Weapon.B") : TEXT("Input.Weapon.RTB"));
		for (const FComboTransition& Existing : ComboData->Transitions)
		{
			if (Existing.TransitionID != (RouteIndex == 0
				? FName(TEXT("Air_KinsectSlash_B")) : FName(TEXT("Air_KinsectSlash_RTB")))
				&& TransitionCanMatchAerialInput(Existing, Input, LiveTags))
			{
				ConflictAudit += FString::Printf(TEXT(" %s (%s)"),
					*Existing.TransitionID.ToString(), *Existing.TargetState.ToString());
			}
		}
	}
	if (!ConflictAudit.IsEmpty())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[KinsectSlashAssets] Refusing to add ambiguous aerial B/RT+B routes. Audit existing transitions:%s"),
			*ConflictAudit);
		return 2;
	}

	UAnimMontage* MainMontage = nullptr;
	UAnimMontage* DanceMontage = nullptr;
	FSegmentInfo HoverInfo;
	FSegmentInfo DashInfo;
	FSegmentInfo MissInfo;
	float HandoffTime = 0.0f;
	if (!BuildMainMontage(Sequences, MainMontage, HoverInfo, DashInfo, MissInfo)
		|| !BuildDanceMontage(*Sequences.Dance, DanceMontage, HandoffTime))
	{
		UE_LOG(LogTemp, Error, TEXT("[KinsectSlashAssets] Montage creation/validation/save failed."));
		return 3;
	}

	UBlueprint* AbilityBlueprint = GetOrCreateAbilityBlueprint();
	if (!AbilityBlueprint
		|| !ConfigureAbility(*AbilityBlueprint, *MainMontage, *DanceMontage))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[KinsectSlashAssets] GA_IG_CaoChongZhan Blueprint creation/configuration/save failed."));
		return 4;
	}

	FString RouteAudit;
	ComboData->Modify();
	if (!AddOrUpdateRoutes(*ComboData, AbilityBlueprint->GeneratedClass, RouteAudit)
	)
	{
		UE_LOG(LogTemp, Error, TEXT("[KinsectSlashAssets] Combo route setup failed: %s"),
			*RouteAudit);
		return 5;
	}

	// The ability's data validation cross-checks its transition-ID section map
	// against DA_IG_Combo. Install those rows in memory before compiling, then
	// refresh their class pointer in case Blueprint compilation reinstanced it.
	FKismetEditorUtilities::CompileBlueprint(AbilityBlueprint);
	if (AbilityBlueprint->Status == BS_Error || !AbilityBlueprint->GeneratedClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[KinsectSlashAssets] Ability Blueprint compile failed."));
		return 4;
	}
	const UMHGZKinsectSlashAbility* CompiledAbility = Cast<UMHGZKinsectSlashAbility>(
		AbilityBlueprint->GeneratedClass->GetDefaultObject());
	if (!CompiledAbility || CompiledAbility->AttackMontage != MainMontage
		|| CompiledAbility->DanceMontage != DanceMontage
		|| CompiledAbility->AttackSegments.Num() != 3
		|| CompiledAbility->EntrySectionByTransitionID.Num() != 2)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[KinsectSlashAssets] Compiled Blueprint did not retain required defaults."));
		return 4;
	}
	if (!AddOrUpdateRoutes(*ComboData, AbilityBlueprint->GeneratedClass, RouteAudit)
		|| !SaveAsset(*AbilityBlueprint) || !SaveAsset(*ComboData)
		|| (bInputProfileChanged && !SaveAsset(*RuntimeDefinition->InputProfile)))
	{
		UE_LOG(LogTemp, Error, TEXT("[KinsectSlashAssets] Combo route setup/save failed: %s"),
			*RouteAudit);
		return 5;
	}

	const FString Report = FString::Printf(
		TEXT("{\n  \"status\": \"generated\",\n  \"entry_source_frame_189\": 0,\n")
		TEXT("  \"primary_montage\": \"%s.AM_IG_CaoChongZhan\",\n")
		TEXT("  \"dance_montage\": \"%s.AM_IG_CaoChongZhan_Dance\",\n")
		TEXT("  \"ability_blueprint\": \"%s.GA_IG_CaoChongZhan\",\n")
		TEXT("  \"hover_duration\": %.6f,\n  \"dash_duration\": %.6f,\n")
		TEXT("  \"miss_duration_double_speed\": %.6f,\n")
		TEXT("  \"late_hit_close_source_frame\": 14,\n")
		TEXT("  \"miss_pose_hold_at_montage_time\": %.6f,\n")
		TEXT("  \"dance_entry_frame\": 0,\n  \"dance_handoff_source_frame\": 90,\n")
		TEXT("  \"dance_handoff_montage_time\": %.6f,\n")
		TEXT("  \"dance_crossfade_seconds\": 0.100000,\n")
		TEXT("  \"motion_value_provisional\": %.4f,\n  \"routes\": \"%s\"\n}\n"),
		MainMontagePackage, DanceMontagePackage, AbilityPackage,
		HoverInfo.Duration, DashInfo.Duration, MissInfo.Duration,
		MissInfo.Start + MissInfo.Duration - 0.0001f, HandoffTime,
		ProvisionalMotionValue, *EscapeJson(RouteAudit));
	const FString ReportPath = FPaths::ProjectSavedDir()
		/ TEXT("_kinsect_slash_asset_setup.json");
	const bool bReportSaved = FFileHelper::SaveStringToFile(Report, *ReportPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	UE_LOG(LogTemp, Display,
		TEXT("[KinsectSlashAssets] Saved montages, ability, Combo routes; report=%s; report_saved=%d"),
		*ReportPath, bReportSaved ? 1 : 0);
	return bReportSaved ? 0 : 6;
#else
	UE_LOG(LogTemp, Error,
		TEXT("[KinsectSlashAssets] This commandlet requires an editor build."));
	return 1;
#endif
}
