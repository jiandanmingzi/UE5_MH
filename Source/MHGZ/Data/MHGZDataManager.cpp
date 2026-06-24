// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZDataManager.h"
#include "Engine/DataTable.h"
#include "Engine/CurveTable.h"
#include "Engine/GameInstance.h"

void UMHGZDataManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UMHGZDataManager::Deinitialize()
{
	Super::Deinitialize();
}

UMHGZDataManager* UMHGZDataManager::Get(const UObject* WorldContext)
{
	if (const UGameInstance* GI = WorldContext->GetWorld()->GetGameInstance())
	{
		return GI->GetSubsystem<UMHGZDataManager>();
	}
	return nullptr;
}

UDataTable* UMHGZDataManager::GetEntryCatalog() const
{
	return EntryCatalog.LoadSynchronous();
}

UDataTable* UMHGZDataManager::GetWeaponComboConfig() const
{
	return WeaponComboConfig.LoadSynchronous();
}

UDataTable* UMHGZDataManager::GetWeaponResourceConfig() const
{
	return WeaponResourceConfig.LoadSynchronous();
}

UCurveTable* UMHGZDataManager::GetAbilityScalars() const
{
	return AbilityScalars.LoadSynchronous();
}
