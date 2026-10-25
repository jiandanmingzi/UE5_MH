// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Inventory/MHGZItemTypes.h"
#include "MHGZEquipmentInstance.generated.h"

class UMHGZEquipmentDefinition;

/**
 * UMHGZEquipmentInstance — 装备实例
 * 持有定义引用 + 客制化数据 + 状态
 */
UCLASS(BlueprintType)
class UMHGZEquipmentInstance : public UObject
{
	GENERATED_BODY()

public:
	/** 创建一个具有有效 ID 和定义引用的运行时装备实例。 */
	UFUNCTION(BlueprintCallable, Category = "MHGZ|Equipment",
		meta = (DefaultToSelf = "Outer"))
	static UMHGZEquipmentInstance* CreateEquipmentInstance(
		UObject* Outer, UMHGZEquipmentDefinition* InDefinition);

	/** 这件具体装备实例的唯一标识，与物品定义标识分开。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FGuid InstanceID;

	/** 该装备实例引用的静态定义资源。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UMHGZEquipmentDefinition> Definition;

	/** 该装备实例当前的状态信息。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EEquipmentStatus Status = EEquipmentStatus::InStorage;

	/** 该装备实例拥有的客制化数据。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FItemCustomization Customization;

	/** 已装入该装备插槽的装饰品实例。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TMap<FName, TObjectPtr<UMHGZEquipmentInstance>> SocketedAccessories;

	void SetStatus(EEquipmentStatus NewStatus) { Status = NewStatus; }
	EEquipmentStatus GetStatus() const { return Status; }

	void SocketAccessory(UMHGZEquipmentInstance* Accessory, FName SocketName);
	void RemoveAccessory(FName SocketName);
	bool CanSocketAccessory(UMHGZEquipmentInstance* Accessory, FName SocketName) const;
};
