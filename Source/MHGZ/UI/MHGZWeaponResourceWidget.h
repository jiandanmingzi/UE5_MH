// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MHGZUserWidget.h"
#include "MHGZWeaponResourceWidget.generated.h"

class UMHGZWeaponResourceComponent;

/**
 * UMHGZWeaponResourceWidget — 武器资源 UI 基类
 */
UCLASS(Abstract)
class UMHGZWeaponResourceWidget : public UMHGZUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "MHGZ|UI")
	void BindToResourceComponent(UMHGZWeaponResourceComponent* InRC);

protected:
	UPROPERTY(BlueprintReadOnly, Category = "MHGZ|UI")
	TObjectPtr<UMHGZWeaponResourceComponent> ResourceComponent;
};
