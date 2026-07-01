// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Inventory/MHGZItemTypes.h"
#include "MHGZEquipmentDefinition.generated.h"

/**
 * UMHGZEquipmentDefinition — 装备定义基类
 */
UCLASS(BlueprintType)
class UMHGZEquipmentDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName ItemID;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 RarityLevel = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FGameplayTag ItemTypeTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FGameplayTag EquipmentSlotTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float AttackPower = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float Defense = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float CriticalRate = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FEntryReference> Entries;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FEquipmentSocket> Sockets;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon|Visual")
	TSoftObjectPtr<USkeletalMesh> Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon|Visual")
	FName AttachSocket = "Weapon_R";

	virtual FPrimaryAssetId GetPrimaryAssetId() const override
	{
		return FPrimaryAssetId(TEXT("EquipmentDef"), ItemID);
	}
};

/**
 * UMHGZWeaponDefinition — 武器定义
 */
UCLASS(BlueprintType)
class UMHGZWeaponDefinition : public UMHGZEquipmentDefinition
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FGameplayTag WeaponTypeTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TMap<FGameplayTag, TSoftObjectPtr<USoundBase>> SwingSoundOverrides;
};
