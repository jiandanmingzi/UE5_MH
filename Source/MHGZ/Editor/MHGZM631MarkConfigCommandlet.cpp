// Copyright MHGZ Project. All Rights Reserved.

#include "Editor/MHGZM631MarkConfigCommandlet.h"

#if WITH_EDITOR

#include "ActionSystem/AnimNotifyState_AttackCollision.h"
#include "ActionSystem/MHGZDescendingThrustAbility.h"
#include "ActionSystem/MHGZAwakenedKinsectAttackAbility.h"
#include "ActionSystem/MHGZInsectGlaiveGroundAbilities.h"
#include "Animation/AnimMontage.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "InsectGlaive/InsectGlaiveCombatConfig.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

namespace UE::MHGZ::M631MarkConfig
{
constexpr TCHAR CombatConfigPath[] =
	TEXT("/Game/Weapons/InsectGlaive/Data/DA_IG_Combat.DA_IG_Combat");
constexpr TCHAR MarkSlashBlueprintPath[] =
	TEXT("/Game/Blueprints/Ability/InsectGlaive/GA_IG_ChongYin");
constexpr TCHAR DescendingThrustBlueprintPath[] =
	TEXT("/Game/Blueprints/Ability/InsectGlaive/GA_IG_JiXiTuCi");
constexpr TCHAR DescendingOverMontagePath[] =
	TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_JiXiTuCi_Over.AM_IG_JiXiTuCi_Over");
constexpr float MarkDurationSeconds = 90.0f;
constexpr float MarkPatchHalfExtentCm = 50.0f;
constexpr float MarkSurfaceNormalToleranceCm = 5.0f;

TSharedRef<FJsonObject> MakeWindowReport(const UAnimMontage* Montage,
	const int32 RequiredConfigIndex, TArray<int32>& OutWindowIndices)
{
	TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
	Report->SetStringField(TEXT("montage"), GetPathNameSafe(Montage));
	TArray<TSharedPtr<FJsonValue>> Windows;
	if (Montage)
	{
		for (const FAnimNotifyEvent& Event : Montage->Notifies)
		{
			const UAnimNotifyState_AttackCollision* Collision =
				Cast<UAnimNotifyState_AttackCollision>(Event.NotifyStateClass);
			if (!Collision)
			{
				continue;
			}
			OutWindowIndices.Add(Collision->ConfigIndex);
			TSharedRef<FJsonObject> Window = MakeShared<FJsonObject>();
			Window->SetNumberField(TEXT("config_index"), Collision->ConfigIndex);
			Window->SetNumberField(TEXT("start_seconds"), Event.GetTime());
			Window->SetNumberField(TEXT("duration_seconds"), Event.GetDuration());
			Window->SetNumberField(TEXT("end_seconds"), Event.GetTime() + Event.GetDuration());
			Window->SetNumberField(TEXT("track_index"), Event.TrackIndex);
			Windows.Add(MakeShared<FJsonValueObject>(Window));
		}
	}
	Report->SetArrayField(TEXT("attack_collision_windows"), Windows);
	Report->SetNumberField(TEXT("required_config_index"), RequiredConfigIndex);
	Report->SetBoolField(TEXT("required_index_has_window"),
		OutWindowIndices.Contains(RequiredConfigIndex));
	return Report;
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
	Package->MarkPackageDirty();
	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	return UPackage::SavePackage(Package, &Asset, *Filename, Args);
}

bool ReadBlueprintCdo(const TCHAR* BlueprintPath, UBlueprint*& OutBlueprint,
	UObject*& OutCdo, TSharedRef<FJsonObject> Report, TArray<FString>& Errors)
{
	OutBlueprint = LoadObject<UBlueprint>(nullptr, BlueprintPath, nullptr, LOAD_NoWarn);
	if (!OutBlueprint || !OutBlueprint->GeneratedClass)
	{
		Errors.Add(FString::Printf(TEXT("Could not load Blueprint/generated class: %s"), BlueprintPath));
		Report->SetStringField(TEXT("error"), TEXT("Blueprint or GeneratedClass missing"));
		return false;
	}
	OutCdo = OutBlueprint->GeneratedClass->GetDefaultObject();
	Report->SetStringField(TEXT("blueprint"), OutBlueprint->GetPathName());
	Report->SetStringField(TEXT("generated_class"), OutBlueprint->GeneratedClass->GetPathName());
	Report->SetStringField(TEXT("native_parent"),
		GetPathNameSafe(OutBlueprint->GeneratedClass->GetSuperClass()));
	return OutCdo != nullptr;
}

int32 ReadIntProperty(const UObject* Object, const TCHAR* PropertyName, TArray<FString>& Errors)
{
	const FIntProperty* Property = Object
		? FindFProperty<FIntProperty>(Object->GetClass(), PropertyName)
		: nullptr;
	if (!Property)
	{
		Errors.Add(FString::Printf(TEXT("Missing integer property %s on %s"),
			PropertyName, *GetPathNameSafe(Object)));
		return INDEX_NONE;
	}
	return Property->GetPropertyValue_InContainer(Object);
}
}

#endif // WITH_EDITOR

UMHGZM631MarkConfigCommandlet::UMHGZM631MarkConfigCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMHGZM631MarkConfigCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	using namespace UE::MHGZ::M631MarkConfig;
	const bool bApply = FParse::Param(*Params, TEXT("Apply"));
	TArray<FString> Errors;
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("task"), TEXT("M6.3.1 mark source/config audit"));
	Root->SetStringField(TEXT("mode"), bApply ? TEXT("Apply") : TEXT("AuditOnly"));
	Root->SetBoolField(TEXT("read_only"), !bApply);

	UInsectGlaiveCombatConfig* Config =
		LoadObject<UInsectGlaiveCombatConfig>(nullptr, CombatConfigPath, nullptr, LOAD_NoWarn);
	if (!Config)
	{
		Errors.Add(FString::Printf(TEXT("Could not load combat config: %s"), CombatConfigPath));
	}
	else
	{
		TSharedRef<FJsonObject> ConfigReport = MakeShared<FJsonObject>();
		ConfigReport->SetStringField(TEXT("asset"), Config->GetPathName());
		ConfigReport->SetNumberField(TEXT("duration_before_seconds"), Config->KinsectMarkDuration);
		ConfigReport->SetNumberField(TEXT("patch_half_extent_before_cm"), Config->KinsectMarkPatchHalfExtent);
		ConfigReport->SetNumberField(TEXT("surface_normal_tolerance_before_cm"),
			Config->KinsectMarkSurfaceNormalTolerance);

		bool bConfigChanged = false;
		if (bApply)
		{
			if (!FMath::IsNearlyEqual(Config->KinsectMarkDuration, MarkDurationSeconds))
			{
				Config->Modify();
				Config->KinsectMarkDuration = MarkDurationSeconds;
				bConfigChanged = true;
			}
			if (!FMath::IsNearlyEqual(Config->KinsectMarkPatchHalfExtent, MarkPatchHalfExtentCm))
			{
				Config->Modify();
				Config->KinsectMarkPatchHalfExtent = MarkPatchHalfExtentCm;
				bConfigChanged = true;
			}
			if (!FMath::IsNearlyEqual(Config->KinsectMarkSurfaceNormalTolerance,
				MarkSurfaceNormalToleranceCm))
			{
				Config->Modify();
				Config->KinsectMarkSurfaceNormalTolerance = MarkSurfaceNormalToleranceCm;
				bConfigChanged = true;
			}
			if (bConfigChanged && !SaveAsset(*Config))
			{
				Errors.Add(TEXT("Failed to save DA_IG_Combat mark fields."));
			}
		}
		ConfigReport->SetNumberField(TEXT("duration_after_seconds"), Config->KinsectMarkDuration);
		ConfigReport->SetNumberField(TEXT("patch_half_extent_after_cm"), Config->KinsectMarkPatchHalfExtent);
		ConfigReport->SetNumberField(TEXT("surface_normal_tolerance_after_cm"),
			Config->KinsectMarkSurfaceNormalTolerance);
		ConfigReport->SetBoolField(TEXT("changed"), bConfigChanged);
		Root->SetObjectField(TEXT("combat_config"), ConfigReport);
	}

	TSharedRef<FJsonObject> MarkSlashReport = MakeShared<FJsonObject>();
	UBlueprint* MarkSlashBlueprint = nullptr;
	UObject* MarkSlashCdoObject = nullptr;
	if (ReadBlueprintCdo(MarkSlashBlueprintPath, MarkSlashBlueprint,
		MarkSlashCdoObject, MarkSlashReport, Errors))
	{
		const UMHGZMarkSlashAbility* Ability = Cast<UMHGZMarkSlashAbility>(MarkSlashCdoObject);
		if (!Ability)
		{
			Errors.Add(TEXT("GA_IG_ChongYin is not a UMHGZMarkSlashAbility child."));
			MarkSlashReport->SetBoolField(TEXT("is_mark_slash_ability"), false);
		}
		else
		{
			MarkSlashReport->SetBoolField(TEXT("is_mark_slash_ability"), true);
			MarkSlashReport->SetNumberField(TEXT("mark_segment_index"), Ability->MarkSegmentIndex);
			MarkSlashReport->SetNumberField(TEXT("attack_segments_count"), Ability->AttackSegments.Num());
			MarkSlashReport->SetStringField(TEXT("attack_montage"), GetPathNameSafe(Ability->AttackMontage));
			TArray<int32> WindowIndices;
			TSharedRef<FJsonObject> MontageReport = MakeWindowReport(
				Ability->AttackMontage, Ability->MarkSegmentIndex, WindowIndices);
			const bool bValid = Ability->MarkSegmentIndex >= 0
				&& Ability->AttackSegments.IsValidIndex(Ability->MarkSegmentIndex)
				&& WindowIndices.Contains(Ability->MarkSegmentIndex);
			MontageReport->SetBoolField(TEXT("mark_segment_is_valid"), bValid);
			MarkSlashReport->SetObjectField(TEXT("montage_audit"), MontageReport);
			if (!bValid)
			{
				Errors.Add(TEXT("GA_IG_ChongYin mark index must exist in AttackSegments and have an AttackCollision window."));
			}
		}
	}
	Root->SetObjectField(TEXT("mark_slash_ability"), MarkSlashReport);

	TSharedRef<FJsonObject> DescendingReport = MakeShared<FJsonObject>();
	UBlueprint* DescendingBlueprint = nullptr;
	UObject* DescendingCdo = nullptr;
	if (ReadBlueprintCdo(DescendingThrustBlueprintPath, DescendingBlueprint,
		DescendingCdo, DescendingReport, Errors))
	{
		const UMHGZDescendingThrustAbility* Ability = Cast<UMHGZDescendingThrustAbility>(DescendingCdo);
		const int32 MarkIndex = ReadIntProperty(DescendingCdo, TEXT("MarkSegmentIndex"), Errors);
		DescendingReport->SetBoolField(TEXT("is_descending_thrust_ability"), Ability != nullptr);
		DescendingReport->SetNumberField(TEXT("mark_segment_index"), MarkIndex);
		DescendingReport->SetNumberField(TEXT("attack_segments_count"),
			Ability ? Ability->AttackSegments.Num() : 0);
		UAnimMontage* OverMontage = LoadObject<UAnimMontage>(
			nullptr, DescendingOverMontagePath, nullptr, LOAD_NoWarn);
		TArray<int32> WindowIndices;
		TSharedRef<FJsonObject> MontageReport = MakeWindowReport(
			OverMontage, MarkIndex, WindowIndices);
		const bool bValid = Ability && MarkIndex >= 0
			&& Ability->AttackSegments.IsValidIndex(MarkIndex)
			&& WindowIndices.Contains(MarkIndex);
		MontageReport->SetBoolField(TEXT("mark_segment_is_valid"), bValid);
		DescendingReport->SetObjectField(TEXT("ground_over_montage_audit"), MontageReport);
		if (!bValid)
		{
			Errors.Add(TEXT("GA_IG_JiXiTuCi mark index must exist in AttackSegments and have an AttackCollision window in AM_IG_JiXiTuCi_Over."));
		}
	}
	Root->SetObjectField(TEXT("descending_thrust_ability"), DescendingReport);

	// Read-only checks for the additional melee sources; -Apply never edits them.
	const TCHAR* AdditionalSourcePaths[] = {
		TEXT("/Game/Blueprints/Ability/InsectGlaive/GA_IG_SiLianYinZhan"),
		TEXT("/Game/Blueprints/Ability/InsectGlaive/GA_IG_JueChongJi") };
	TArray<TSharedPtr<FJsonValue>> AdditionalSources;
	for (int32 SourceIndex = 0; SourceIndex < UE_ARRAY_COUNT(AdditionalSourcePaths); ++SourceIndex)
	{
		TSharedRef<FJsonObject> SourceReport = MakeShared<FJsonObject>();
		UBlueprint* Blueprint = nullptr;
		UObject* Cdo = nullptr;
		if (ReadBlueprintCdo(AdditionalSourcePaths[SourceIndex], Blueprint, Cdo, SourceReport, Errors))
		{
			const UMHGZAttackAbility* Attack = Cast<UMHGZAttackAbility>(Cdo);
			const int32 ExpectedIndex = SourceIndex == 0 ? 1 : 0;
			const UMHGZMarkSlashAbility* FourHit = Cast<UMHGZMarkSlashAbility>(Cdo);
			const UMHGZAwakenedKinsectAttackAbility* Awakened = Cast<UMHGZAwakenedKinsectAttackAbility>(Cdo);
			const bool bCorrectClass = SourceIndex == 0
				? FourHit && FourHit->MarkSegmentIndex == ExpectedIndex : Awakened != nullptr;
			TArray<int32> WindowIndices;
			TSharedRef<FJsonObject> Windows = MakeWindowReport(
				Attack ? Attack->AttackMontage.Get() : nullptr, ExpectedIndex, WindowIndices);
			SourceReport->SetNumberField(TEXT("attack_segments_count"), Attack ? Attack->AttackSegments.Num() : 0);
			SourceReport->SetObjectField(TEXT("montage_audit"), Windows);
			SourceReport->SetBoolField(TEXT("source_is_valid"), bCorrectClass && Attack
				&& Attack->AttackSegments.IsValidIndex(ExpectedIndex) && WindowIndices.Contains(ExpectedIndex));
			if (!bCorrectClass || !Attack || !Attack->AttackSegments.IsValidIndex(ExpectedIndex)
				|| !WindowIndices.Contains(ExpectedIndex))
			{
				Errors.Add(FString::Printf(TEXT("Additional melee mark source has an invalid class/index/window: %s"),
					AdditionalSourcePaths[SourceIndex]));
			}
		}
		AdditionalSources.Add(MakeShared<FJsonValueObject>(SourceReport));
	}
	Root->SetArrayField(TEXT("additional_melee_mark_sources"), AdditionalSources);
	Root->SetArrayField(TEXT("errors"), [&Errors]()
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FString& Error : Errors)
		{
			Values.Add(MakeShared<FJsonValueString>(Error));
		}
		return Values;
	}());
	Root->SetBoolField(TEXT("success"), Errors.IsEmpty());

	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Root, Writer);
	const FString OutputDirectory = FPaths::ProjectSavedDir() / TEXT("M6Audit");
	const FString OutputPath = OutputDirectory / TEXT("m6_3_1_asset_audit.json");
	IFileManager::Get().MakeDirectory(*OutputDirectory, true);
	if (!FFileHelper::SaveStringToFile(Json, *OutputPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogTemp, Error, TEXT("[M6_3_1] Failed to write report: %s"), *OutputPath);
		return 1;
	}
	UE_LOG(LogTemp, Display, TEXT("[M6_3_1] Wrote %s (Apply=%d, Success=%d)"),
		*OutputPath, bApply ? 1 : 0, Errors.IsEmpty() ? 1 : 0);
	for (const FString& Error : Errors)
	{
		UE_LOG(LogTemp, Error, TEXT("[M6_3_1] %s"), *Error);
	}
	return Errors.IsEmpty() ? 0 : 1;
#else
	UE_LOG(LogTemp, Error, TEXT("This commandlet requires an editor build."));
	return 1;
#endif
}
