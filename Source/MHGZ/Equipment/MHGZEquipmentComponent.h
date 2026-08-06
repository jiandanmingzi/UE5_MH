// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Inventory/MHGZItemTypes.h"
#include "MHGZEquipmentComponent.generated.h"

class UMHGZEquipmentInstance;
class UMHGZWeaponComboData;
class UMHGZWeaponResourceComponent;
class UAbilitySystemComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnEquipmentChanged);

/**
 * UMHGZEquipmentComponent — 装备 GE 管理组件
 * 挂载到 PlayerState。管理装备槽位、GE 的创建/Apply/移除。
 */
UCLASS(ClassGroup = (MHGZ), BlueprintType)
class UMHGZEquipmentComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMHGZEquipmentComponent();

	// ═══════════════════════════════════════════
	// 装备槽位管理
	// ═══════════════════════════════════════════

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

	/** 拆除饰品 */
	UFUNCTION(BlueprintCallable, Category = "MHGZ|Equipment")
	void RemoveAccessory(UMHGZEquipmentInstance* HostItem, FName SocketName);

	// ═══════════════════════════════════════════
	// Delegate
	// ═══════════════════════════════════════════

	UPROPERTY(BlueprintAssignable, Category = "MHGZ|Equipment")
	FOnEquipmentChanged OnEquipmentChanged;

protected:
	virtual void BeginPlay() override;

	/** 装备变更的统一入口——全量重算 */
	void OnEquipmentChangedInternal();

	/** Apply 装备的 GE */
	void ApplyItemEffects(UMHGZEquipmentInstance* Item);

	/** 将所有已装备物品的基础数值写入 GAS Attribute Base。 */
	void RecalculateEquipmentBaseAttributes(UAbilitySystemComponent* ASC);

	/** Apply 词条 GE */
	void ApplyEntryGEs(UAbilitySystemComponent* ASC, const TArray<FEntryReference>& Entries);

	/** 获取玩家 ASC */
	UAbilitySystemComponent* GetPlayerASC() const;
};
