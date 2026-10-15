// Copyright MHGZ Project. All Rights Reserved.

#include "Commandlets/MHGZM64UISubsystemAuditCommandlet.h"

#include "AssetRegistry/AssetIdentifier.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/Blueprint.h"
#include "Modules/ModuleManager.h"

UMHGZM64UISubsystemAuditCommandlet::UMHGZM64UISubsystemAuditCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UMHGZM64UISubsystemAuditCommandlet::Main(const FString& Params)
{
	(void)Params;
	FAssetRegistryModule& AssetRegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	AssetRegistry.SearchAllAssets(true);
	AssetRegistry.WaitForCompletion();

	const FAssetIdentifier ClassIdentifier(
		FName(TEXT("/Script/MHGZ")), FName(TEXT("MHGZUISubsystem")));
	const FAssetIdentifier FunctionIdentifier(
		FName(TEXT("/Script/MHGZ")), FName(TEXT("MHGZUISubsystem")), FName(TEXT("Get")));
	TArray<FAssetIdentifier> ClassReferencers;
	TArray<FAssetIdentifier> FunctionReferencers;
	AssetRegistry.GetReferencers(ClassIdentifier, ClassReferencers);
	AssetRegistry.GetReferencers(FunctionIdentifier, FunctionReferencers);

	UE_LOG(LogTemp, Display,
		TEXT("[M6.4UISubsystemAudit] Class=%s ReferencerCount=%d"),
		*ClassIdentifier.ToString(), ClassReferencers.Num());
	for (const FAssetIdentifier& Referencer : ClassReferencers)
	{
		UE_LOG(LogTemp, Display, TEXT("[M6.4UISubsystemAudit] ClassReferencer=%s"),
			*Referencer.ToString());
	}
	UE_LOG(LogTemp, Display,
		TEXT("[M6.4UISubsystemAudit] Function=%s ReferencerCount=%d"),
		*FunctionIdentifier.ToString(), FunctionReferencers.Num());
	for (const FAssetIdentifier& Referencer : FunctionReferencers)
	{
		UE_LOG(LogTemp, Display, TEXT("[M6.4UISubsystemAudit] FunctionReferencer=%s"),
			*Referencer.ToString());
	}

	const bool bHasReferences = !ClassReferencers.IsEmpty() || !FunctionReferencers.IsEmpty();
	UE_LOG(LogTemp, Display,
		TEXT("[M6.4UISubsystemAudit] Result=%s (on-disk AssetRegistry dependencies only)"),
		bHasReferences ? TEXT("REFERENCED_KEEP_CLASS") : TEXT("NO_ASSET_REFERENCERS_FOUND"));

	if (UBlueprint* ExistingHUD = LoadObject<UBlueprint>(nullptr,
		TEXT("/Game/Blueprints/HUD/BP_MHGZHUD.BP_MHGZHUD")))
	{
		UE_LOG(LogTemp, Display,
			TEXT("[M6.4UISubsystemAudit] ExistingHUD=%s ParentClass=%s GeneratedClass=%s"),
			*ExistingHUD->GetPathName(),
			*GetPathNameSafe(ExistingHUD->ParentClass),
			*GetPathNameSafe(ExistingHUD->GeneratedClass));
	}
	else
	{
		UE_LOG(LogTemp, Display,
			TEXT("[M6.4UISubsystemAudit] ExistingHUD=/Game/Blueprints/HUD/BP_MHGZHUD NotFound"));
	}

	FARFilter WidgetFilter;
	WidgetFilter.ClassPaths.Add(FTopLevelAssetPath(
		FName(TEXT("/Script/UMGEditor")), FName(TEXT("WidgetBlueprint"))));
	WidgetFilter.bRecursiveClasses = true;
	TArray<FAssetData> WidgetBlueprints;
	AssetRegistry.GetAssets(WidgetFilter, WidgetBlueprints);
	UE_LOG(LogTemp, Display, TEXT("[M6.4UISubsystemAudit] WidgetBlueprintCount=%d"),
		WidgetBlueprints.Num());
	for (const FAssetData& Widget : WidgetBlueprints)
	{
		UE_LOG(LogTemp, Display, TEXT("[M6.4UISubsystemAudit] WidgetBlueprint=%s"),
			*Widget.GetObjectPathString());
	}
	return bHasReferences ? 1 : 0;
}
