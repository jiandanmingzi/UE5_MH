// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Inventory/MHGZItemTypes.h"
#include "MHGZEquipmentComponent.generated.h"

class UMHGZEquipmentInstance;
class UMHGZWeaponDefinition;
class UWeaponRuntimeDefinition;
class UAbilitySystemComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnEquipmentChanged);

/**
 * 当前武器槽的一次快照。
 * EquipmentInstance 使用弱引用（运行时实例不延长生命周期）；
 * WeaponDefinition / RuntimeDefinition 使用强引用（DataAsset 随快照保活）。
 * WeaponRevision 仅在武器槽 EquipmentInstance 或 RuntimeDefinition 身份真实变化时递增。
 */
USTRUCT(BlueprintType)
struct FEquippedWeaponSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	TWeakObjectPtr<UMHGZEquipmentInstance> EquipmentInstance;

	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<UMHGZWeaponDefinition> WeaponDefinition;

	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<UWeaponRuntimeDefinition> RuntimeDefinition;

	UPROPERTY(BlueprintReadOnly)
	int64 WeaponRevision = 0;
};

/** 任意装备变化（护甲/饰品/镶嵌/武器）后的属性与词条重算通知。 */
DECLARE_MULTICAST_DELEGATE(FOnEquipmentStatsChanged);

/** 仅武器槽身份真实变化后广播；携带完整快照。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnEquippedWeaponChanged, const FEquippedWeaponSnapshot&);

/**
 * UMHGZEquipmentComponent —— 装备 GE 管理组件
 * 挂在 PlayerState。管理装备槽位、GE 的创建/Apply/移除。
 * M2 起仅负责持久装备效果与两条拆分事件；不再创建/销毁 Resource、
 * 不授予/移除武器 Ability、不读取旧 DataTable。
 */
UCLASS(ClassGroup = (MHGZ), BlueprintType)
class UMHGZEquipmentComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMHGZEquipmentComponent();

	// ----------------------------------------------------------------------
	// 装备槽位管理
	// ----------------------------------------------------------------------
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Equipment|State")
	TMap<FGameplayTag, TObjectPtr<UMHGZEquipmentInstance>> EquippedItems;

	UFUNCTION(BlueprintCallable, Category = "MHGZ|Equipment")
	void EquipItem(FGameplayTag SlotTag, UMHGZEquipmentInstance* Item);

	UFUNCTION(BlueprintCallable, Category = "MHGZ|Equipment")
	void UnequipItem(FGameplayTag SlotTag);

	UFUNCTION(BlueprintPure, Category = "MHGZ|Equipment")
	UMHGZEquipmentInstance* GetEquippedItem(FGameplayTag SlotTag) const;

	/** 镶嵌饰品 */
	UFUNCTION(BlueprintCallable, Category = "MHGZ|Equipment")
	void SocketAccessory(UMHGZEquipmentInstance* HostItem, UMHGZEquipmentInstance* Accessory, FName SocketName);

	/** 拆下饰品 */
	UFUNCTION(BlueprintCallable, Category = "MHGZ|Equipment")
	void RemoveAccessory(UMHGZEquipmentInstance* HostItem, FName SocketName);

	/** 当前武器槽快照（含单调递增 WeaponRevision）。 */
	UFUNCTION(BlueprintPure, Category = "MHGZ|Equipment")
	const FEquippedWeaponSnapshot& GetEquippedWeaponSnapshot() const { return CurrentWeaponSnapshot; }

	// ----------------------------------------------------------------------
	// Delegate
	// ----------------------------------------------------------------------
	/** 属性/词条重算通知：护甲、饰品、镶嵌、武器均触发；不负责武器运行时重建。 */
	FOnEquipmentStatsChanged OnEquipmentStatsChanged;

	/** 武器槽身份变化通知：仅 EquipmentInstance / RuntimeDefinition 身份真实变化时广播。 */
	FOnEquippedWeaponChanged OnEquippedWeaponChanged;

	/** M1 兼容遗留委托：与 OnEquipmentStatsChanged 同步广播，不再作为武器运行时重建入口。 */
	UPROPERTY(BlueprintAssignable, Category = "MHGZ|Equipment")
	FOnEquipmentChanged OnEquipmentChanged;

protected:
	virtual void BeginPlay() override;

	/** 装备变更的统一入口：重算 GE/基础值 + 拆分广播 StatsChanged / WeaponChanged。 */
	void OnEquipmentChangedInternal();

	/** Apply 持久装备效果（词条 GE 与镶嵌饰品）；不再创建/销毁武器运行时对象。 */
	void ApplyItemEffects(UMHGZEquipmentInstance* Item);

	/** 将所有已装备物品的基础数值写入 GAS Attribute Base。 */
	void RecalculateEquipmentBaseAttributes(UAbilitySystemComponent* ASC);

	/** Apply 词条 GE */
	void ApplyEntryGEs(UAbilitySystemComponent* ASC, const TArray<FEntryReference>& Entries);

	/** 获取玩家 ASC */
	UAbilitySystemComponent* GetPlayerASC() const;

private:
	/** 当前武器槽快照缓存；初始为空快照（WeaponRevision=0）。 */
	FEquippedWeaponSnapshot CurrentWeaponSnapshot;
};
