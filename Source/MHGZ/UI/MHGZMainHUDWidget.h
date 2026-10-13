// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MHGZMainHUDWidget.generated.h"

class UOverlay;

/** 主HUD Designer只提供插槽；子Widget的创建和生命周期由AMHGZHUD所有。 */
UCLASS(Abstract)
class MHGZ_API UMHGZMainHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(meta = (BindWidget), BlueprintReadOnly, Category = "MHGZ|UI")
	TObjectPtr<UOverlay> HealthBarSlot;

	UPROPERTY(meta = (BindWidget), BlueprintReadOnly, Category = "MHGZ|UI")
	TObjectPtr<UOverlay> StaminaBarSlot;

	UPROPERTY(meta = (BindWidget), BlueprintReadOnly, Category = "MHGZ|UI")
	TObjectPtr<UOverlay> CrosshairSlot;

	UPROPERTY(meta = (BindWidget), BlueprintReadOnly, Category = "MHGZ|UI")
	TObjectPtr<UOverlay> WeaponResourceSlot;
};
