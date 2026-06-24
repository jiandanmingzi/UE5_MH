// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GameplayEffectTypes.h"
#include "Engine/DataAsset.h"
#include "MHGZItemTypes.generated.h"

/**
 * 物品稀有度
 */
UENUM(BlueprintType)
enum class EItemRarity : uint8
{
	Common    = 0 UMETA(DisplayName = "普通"),
	Uncommon  = 1 UMETA(DisplayName = "稀有"),
	Rare      = 2 UMETA(DisplayName = "珍贵"),
	Epic      = 3 UMETA(DisplayName = "史诗"),
	Legendary = 4 UMETA(DisplayName = "传说")
};

/**
 * 装备实例状态——由 EquipmentComponent 维护，O(1) 查询
 */
UENUM(BlueprintType)
enum class EEquipmentStatus : uint8
{
	InStorage  UMETA(DisplayName = "仓库中"),
	Equipped   UMETA(DisplayName = "已装备"),
	Socketed   UMETA(DisplayName = "已镶嵌")
};

/**
 * 镶嵌孔信息
 */
USTRUCT(BlueprintType)
struct FEquipmentSocket
{
	GENERATED_BODY()

	/** 孔位名称（如 "Slot_01"） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName SocketName;

	/** 孔位等级（1-4），饰品等级 ≤ 孔位等级才可镶入 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 SocketLevel = 1;
};

/**
 * 物品客制化数据——存于 EquipmentInstance
 */
USTRUCT(BlueprintType)
struct FItemCustomization
{
	GENERATED_BODY()

	/** 属性覆写（Key=AttributeTag, Value=覆写值） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TMap<FGameplayTag, float> StatOverrides;

	/** 修改的词条（EntryID -> 新等级） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TMap<FName, int32> ModifiedEntries;

	/** 移除的词条 ID 列表 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FName> RemovedEntryIDs;

	/** 新增的词条 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FName> AddedEntries;
};

/**
 * 词条引用——装备定义中引用词条目录中的词条
 */
USTRUCT(BlueprintType)
struct FEntryReference
{
	GENERATED_BODY()

	/** 词条 ID（查 DT_EntryCatalog） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName EntryID;

	/** 词条等级 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 EntryLevel = 1;
};

/**
 * 词条效果类型
 */
UENUM(BlueprintType)
enum class EEntryEffectType : uint8
{
	SimpleStat      UMETA(DisplayName = "简单属性（走 ExecCalc）"),
	Complex         UMETA(DisplayName = "复杂效果（自定义 GE 蓝图）"),
	WeaponResource  UMETA(DisplayName = "武器资源（走 ApplyEntryModifier）")
};

/**
 * 词条修饰符——定义词条对属性的修改
 */
USTRUCT(BlueprintType)
struct FEntryModifier
{
	GENERATED_BODY()

	/** 目标属性/参数 Tag（Attribute.Health / WeaponResource.IG.KinsectRegenRate 等） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag AttributeTag;

	/** 操作类型 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TEnumAsByte<EGameplayModOp::Type> Operation = EGameplayModOp::Multiplicitive;

	/** 数值曲线——X=EntryLevel，Y=修改值 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSoftObjectPtr<UCurveFloat> MagnitudeCurve;
};

/**
 * 词条定义——存于 DT_EntryCatalog
 */
USTRUCT(BlueprintType)
struct FEntryDefinition
{
	GENERATED_BODY()

	/** 词条 ID（主键） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName EntryID;

	/** 词条显示名 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FText DisplayName;

	/** 最大等级 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 MaxLevel = 5;

	/** 效果类型 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EEntryEffectType EffectType = EEntryEffectType::SimpleStat;

	/** 修饰符列表（SimpleStat/WeaponResource 用） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "EffectType != EEntryEffectType::Complex"))
	TArray<FEntryModifier> Modifiers;

	/** 自定义 GE 类（仅 Complex 类型用） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "EffectType == EEntryEffectType::Complex"))
	TSubclassOf<class UGameplayEffect> EffectClass;
};

/**
 * 武器连招表映射行
 */
USTRUCT(BlueprintType)
struct FWeaponComboConfigRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag WeaponTypeTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSoftObjectPtr<class UMHGZWeaponComboData> ComboDataAsset;
};

/**
 * 武器资源配置行
 */
USTRUCT(BlueprintType)
struct FWeaponResourceConfigRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag WeaponTypeTag;

	/** 资源组件 C++ 子类 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSubclassOf<class UMHGZWeaponResourceComponent> ResourceComponentClass;

	/** 资源 UI Widget */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSoftClassPtr<class UUserWidget> ResourceWidgetClass;
};
