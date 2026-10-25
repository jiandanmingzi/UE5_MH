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
	/** 装备定义的稳定物品标识。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName ItemID;

	/** 界面展示的装备名称。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FText DisplayName;

	/** 装备的稀有度等级。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 RarityLevel = 1;

	/** 装备所属的物品类型标签。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FGameplayTag ItemTypeTag;

	/** 该装备允许占用的装备栏位标签。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FGameplayTag EquipmentSlotTag;

	/** 装备提供的基础攻击力。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float AttackPower = 0.f;

	/** 防御数值，参与对应的受伤结算。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float Defense = 0.f;

	/** 会心率，供攻击快照与伤害结算读取。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float CriticalRate = 0.f;

	/** 该装备自带的词条配置列表。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FEntryReference> Entries;

	/** 该装备提供的装饰品插槽配置。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FEquipmentSocket> Sockets;

	/** 装备显示使用的骨骼网格资源。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon|Visual")
	TSoftObjectPtr<USkeletalMesh> Mesh;

	/** 将装备模型挂到角色时使用的插槽名。 */
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
	/** 实际属性值，非《世界》显示值；面板300在这里填30。属性类型由CombatConfig.DamageProfile决定。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Damage", meta = (ClampMin = "0.0"))
	float ElementPower = 0.0f;

	/** 武器运行时唯一接线入口；M2 后不再按 WeaponTypeTag 查询旧 DataTable。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Runtime")
	TObjectPtr<UWeaponRuntimeDefinition> RuntimeDefinition;

	/** 迁移期仍用于识别旧资产；运行时身份最终以 RuntimeDefinition 为准。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FGameplayTag WeaponTypeTag;

	/** 按挥击声音身份标签覆盖该武器的具体挥击音源。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TMap<FGameplayTag, TSoftObjectPtr<USoundBase>> SwingSoundOverrides;
};
