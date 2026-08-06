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

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FGuid InstanceID;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UMHGZEquipmentDefinition> Definition;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EEquipmentStatus Status = EEquipmentStatus::InStorage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FItemCustomization Customization;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TMap<FName, TObjectPtr<UMHGZEquipmentInstance>> SocketedAccessories;

	void SetStatus(EEquipmentStatus NewStatus) { Status = NewStatus; }
	EEquipmentStatus GetStatus() const { return Status; }

	void SocketAccessory(UMHGZEquipmentInstance* Accessory, FName SocketName);
	void RemoveAccessory(FName SocketName);
	bool CanSocketAccessory(UMHGZEquipmentInstance* Accessory, FName SocketName) const;
};
