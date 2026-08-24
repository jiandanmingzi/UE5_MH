// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Inventory/MHGZItemTypes.h"
#include "MHGZEquipmentDefinition.generated.h"

class UWeaponRuntimeDefinition;

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
	FName AttachSocket = "Weapon_L";

	/** 收刀时武器视觉组件所附着的角色 Socket。AttachSocket 始终表示拔刀后的手部 Socket。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon|Visual")
	FName SheathedAttachSocket = "Weapon_Back";

	/** 角色上代表此武器本体的 SkeletalMeshComponent 标签；默认沿用既有命中约定。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon|Visual")
	FName VisualComponentTag = "WeaponTrace";

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
	/** 武器运行时唯一接线入口；M2 后不再按 WeaponTypeTag 查询旧 DataTable。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Runtime")
	TObjectPtr<UWeaponRuntimeDefinition> RuntimeDefinition;

	/** 迁移期仍用于识别旧资产；运行时身份最终以 RuntimeDefinition 为准。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FGameplayTag WeaponTypeTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TMap<FGameplayTag, TSoftObjectPtr<USoundBase>> SwingSoundOverrides;
};
