// Copyright MHGZ Project. All Rights Reserved.
#include "Commandlets/MHGZE64DamageNumberSetupCommandlet.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Engine/Blueprint.h"
#include "GameplayCues/MHGZDamageNumberCue.h"
#include "HAL/FileManager.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UI/MHGZDamageNumberActor.h"
#include "UI/MHGZDamageNumberWidget.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"

namespace
{
constexpr TCHAR WidgetPackage[] = TEXT("/Game/UI/Feedback/WBP_DamageNumber");
constexpr TCHAR ActorPackage[] = TEXT("/Game/Blueprints/Feedback/BP_DamageNumberActor");
constexpr TCHAR CuePackage[] = TEXT("/Game/GameplayCues/GCN_Hit_DamageNumber");

UBlueprint* ReadOrCreate(const TCHAR* PackageName, UClass* Parent, bool bWidget,
	bool bApply, TArray<FString>& Errors, TArray<UBlueprint*>& Created)
{
	const FString Name = FPackageName::GetLongPackageAssetName(PackageName);
	const FString ObjectPath = FString(PackageName) + TEXT(".") + Name;
	UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn);
	if (!BP && bApply && !FPackageName::DoesPackageExist(PackageName))
	{
		UPackage* Package = CreatePackage(PackageName);
		BP = FKismetEditorUtilities::CreateBlueprint(Parent, Package, FName(Name), BPTYPE_Normal,
			bWidget ? UWidgetBlueprint::StaticClass() : UBlueprint::StaticClass(),
			bWidget ? UWidgetBlueprintGeneratedClass::StaticClass() : UBlueprintGeneratedClass::StaticClass(),
			TEXT("E64DamageNumberSetup"));
		if (BP) { Created.Add(BP); FAssetRegistryModule::AssetCreated(BP); }
	}
	if (!BP || BP->ParentClass != Parent || (bWidget && !Cast<UWidgetBlueprint>(BP)))
	{
		Errors.Add(FString::Printf(TEXT("Missing or incompatible asset (not overwritten): %s"), *ObjectPath));
		return nullptr;
	}
	return BP;
}

UClass* ReadClass(UObject* Cdo, FName Name)
{
	const FClassProperty* Property = Cdo ? FindFProperty<FClassProperty>(Cdo->GetClass(), Name) : nullptr;
	return Property ? Cast<UClass>(Property->GetObjectPropertyValue_InContainer(Cdo)) : nullptr;
}

bool SetNumberClassProperty(UObject* Cdo, FName Name, UClass* Value)
{
	FClassProperty* Property = Cdo ? FindFProperty<FClassProperty>(Cdo->GetClass(), Name) : nullptr;
	if (!Property || !Value) { return false; }
	Property->SetObjectPropertyValue_InContainer(Cdo, Value);
	return true;
}

double ReadDisplayNumber(UObject* Cdo, FName Name)
{
	const FNumericProperty* Property = Cdo ? FindFProperty<FNumericProperty>(Cdo->GetClass(), Name) : nullptr;
	if (!Property) { return 0.0; }
	const void* Value = Property->ContainerPtrToValuePtr<void>(Cdo);
	return Property->IsFloatingPoint() ? Property->GetFloatingPointPropertyValue(Value)
		: static_cast<double>(Property->GetSignedIntPropertyValue(Value));
}

bool Save(UBlueprint* BP)
{
	BP->MarkPackageDirty();
	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	const FString Filename = FPackageName::LongPackageNameToFilename(
		BP->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
	return UPackage::SavePackage(BP->GetOutermost(), BP, *Filename, Args);
}
}
#endif

UMHGZE64DamageNumberSetupCommandlet::UMHGZE64DamageNumberSetupCommandlet()
{
	IsClient = false; IsServer = false; IsEditor = true; LogToConsole = true;
}

int32 UMHGZE64DamageNumberSetupCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	const bool bApply = FParse::Param(*Params, TEXT("Apply"));
	TArray<FString> Errors;
	TArray<UBlueprint*> Created;
	UWidgetBlueprint* Widget = Cast<UWidgetBlueprint>(ReadOrCreate(WidgetPackage,
		UMHGZDamageNumberWidget::StaticClass(), true, bApply, Errors, Created));
	UBlueprint* Actor = ReadOrCreate(ActorPackage, AMHGZDamageNumberActor::StaticClass(),
		false, bApply, Errors, Created);
	UBlueprint* Cue = ReadOrCreate(CuePackage, UMHGZDamageNumberCue::StaticClass(),
		false, bApply, Errors, Created);
	if (bApply && Errors.IsEmpty())
	{
		if (!Widget->WidgetTree->RootWidget && Created.Contains(Widget))
		{
			UCanvasPanel* Root = Widget->WidgetTree->ConstructWidget<UCanvasPanel>(
				UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
			Widget->WidgetTree->RootWidget = Root;
			UTextBlock* Text = Widget->WidgetTree->ConstructWidget<UTextBlock>(
				UTextBlock::StaticClass(), TEXT("DamageText"));
			Text->bIsVariable = true;
			Text->SetText(FText::FromString(TEXT("123")));
			Text->SetJustification(ETextJustify::Center);
			Text->SetVisibility(ESlateVisibility::HitTestInvisible);
			UCanvasPanelSlot* Slot = Root->AddChildToCanvas(Text);
			Slot->SetPosition(FVector2D(0, 12)); Slot->SetSize(FVector2D(180, 48));
		}
		if (!Cast<UTextBlock>(Widget->WidgetTree->FindWidget(TEXT("DamageText"))))
		{
			Errors.Add(TEXT("Existing widget lacks DamageText; refusing to replace its authored tree."));
		}
		if (Errors.IsEmpty())
		{
			FKismetEditorUtilities::CompileBlueprint(Widget);
			FKismetEditorUtilities::CompileBlueprint(Actor);
			FKismetEditorUtilities::CompileBlueprint(Cue);
			if (!Widget->GeneratedClass || !Actor->GeneratedClass || !Cue->GeneratedClass)
			{
				Errors.Add(TEXT("A generated class is missing after compile."));
			}
			else
			{
				UObject* ActorCdo = Actor->GeneratedClass->GetDefaultObject();
				UMHGZDamageNumberCue* CueCdo = Cast<UMHGZDamageNumberCue>(Cue->GeneratedClass->GetDefaultObject());
				if (!SetNumberClassProperty(ActorCdo, TEXT("DamageWidgetClass"), Widget->GeneratedClass)
					|| !SetNumberClassProperty(CueCdo, TEXT("DamageNumberActorClass"), Actor->GeneratedClass))
				{
					Errors.Add(TEXT("Native class-link properties could not be set."));
				}
				else
				{
					CueCdo->GameplayCueTag = FGameplayTag::RequestGameplayTag(TEXT("GameplayCue.Hit.DamageNumber"));
					CueCdo->GameplayCueName = CueCdo->GameplayCueTag.GetTagName();
					for (UBlueprint* BP : {static_cast<UBlueprint*>(Widget), Actor, Cue})
					{
						if (BP->Status == BS_Error || !Save(BP)) { Errors.Add(TEXT("Compile/save failed: ") + BP->GetPathName()); }
					}
				}
			}
		}
	}
	const bool bText = Widget && Widget->WidgetTree
		&& Cast<UTextBlock>(Widget->WidgetTree->FindWidget(TEXT("DamageText")));
	UObject* ActorCdo = Actor && Actor->GeneratedClass ? Actor->GeneratedClass->GetDefaultObject() : nullptr;
	UMHGZDamageNumberCue* CueCdo = Cue && Cue->GeneratedClass
		? Cast<UMHGZDamageNumberCue>(Cue->GeneratedClass->GetDefaultObject()) : nullptr;
	const bool bLinks = ActorCdo && CueCdo && Widget && Widget->GeneratedClass
		&& ReadClass(ActorCdo, TEXT("DamageWidgetClass")) == Widget->GeneratedClass
		&& ReadClass(CueCdo, TEXT("DamageNumberActorClass")) == Actor->GeneratedClass
		&& CueCdo->GameplayCueTag == FGameplayTag::RequestGameplayTag(TEXT("GameplayCue.Hit.DamageNumber"));
	if (!bText || !bLinks) { Errors.Add(TEXT("Damage text tree or persistent class/tag links are invalid.")); }
	FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	Registry.Get().SearchAllAssets(true);
	TArray<FAssetData> AllAssets;
	Registry.Get().GetAllAssets(AllAssets);
	TArray<TSharedPtr<FJsonValue>> RegisteredCues;
	for (const FAssetData& Asset : AllAssets)
	{
		FString Tag;
		if (Asset.GetTagValue(TEXT("GameplayCueName"), Tag) && Tag == TEXT("GameplayCue.Hit.DamageNumber"))
		{
			RegisteredCues.Add(MakeShared<FJsonValueString>(Asset.GetObjectPathString()));
		}
	}
	if (RegisteredCues.Num() != 1) { Errors.Add(TEXT("Expected exactly one registered DamageNumber Cue asset.")); }
	TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
	Report->SetStringField(TEXT("mode"), bApply ? TEXT("Apply") : TEXT("AuditOnly"));
	Report->SetBoolField(TEXT("success"), Errors.IsEmpty());
	Report->SetBoolField(TEXT("has_damage_text"), bText);
	Report->SetBoolField(TEXT("class_and_tag_links_valid"), bLinks);
	Report->SetStringField(TEXT("widget"), WidgetPackage);
	Report->SetStringField(TEXT("actor"), ActorPackage);
	Report->SetStringField(TEXT("cue"), CuePackage);
	UObject* WidgetCdo = Widget && Widget->GeneratedClass ? Widget->GeneratedClass->GetDefaultObject() : nullptr;
	Report->SetNumberField(TEXT("normal_font_size"), ReadDisplayNumber(WidgetCdo, TEXT("NormalFontSize")));
	Report->SetNumberField(TEXT("critical_font_size"), ReadDisplayNumber(WidgetCdo, TEXT("CriticalFontSize")));
	Report->SetNumberField(TEXT("display_lifetime"), ReadDisplayNumber(ActorCdo, TEXT("DisplayLifetime")));
	Report->SetNumberField(TEXT("fade_out_duration"), ReadDisplayNumber(ActorCdo, TEXT("FadeOutDuration")));
	Report->SetNumberField(TEXT("fade_rise_speed"), ReadDisplayNumber(ActorCdo, TEXT("RiseSpeed")));
	Report->SetArrayField(TEXT("registered_number_cues"), RegisteredCues);
	TArray<TSharedPtr<FJsonValue>> ErrorValues;
	for (const FString& Error : Errors) { ErrorValues.Add(MakeShared<FJsonValueString>(Error)); UE_LOG(LogTemp, Error, TEXT("[E6.4Numbers] %s"), *Error); }
	Report->SetArrayField(TEXT("errors"), ErrorValues);
	FString Json;
	FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("M6Audit");
	IFileManager::Get().MakeDirectory(*Directory, true);
	const bool bReportSaved = FFileHelper::SaveStringToFile(Json,
		*(Directory / TEXT("e64_damage_number_assets.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	UE_LOG(LogTemp, Display, TEXT("[E6.4Numbers] Mode=%s Success=%d"), bApply ? TEXT("Apply") : TEXT("AuditOnly"), Errors.IsEmpty());
	return Errors.IsEmpty() && bReportSaved ? 0 : 1;
#else
	return 1;
#endif
}
