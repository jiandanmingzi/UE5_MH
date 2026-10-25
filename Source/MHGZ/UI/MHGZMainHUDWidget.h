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
	/** 主HUD中放置生命条的容器插槽。 */
	UPROPERTY(meta = (BindWidget), BlueprintReadOnly, Category = "MHGZ|UI")
	TObjectPtr<UOverlay> HealthBarSlot;

	/** 主HUD中放置耐力条的容器插槽。 */
	UPROPERTY(meta = (BindWidget), BlueprintReadOnly, Category = "MHGZ|UI")
	TObjectPtr<UOverlay> StaminaBarSlot;

	/** 主HUD中放置准心的容器插槽。 */
	UPROPERTY(meta = (BindWidget), BlueprintReadOnly, Category = "MHGZ|UI")
	TObjectPtr<UOverlay> CrosshairSlot;

	/** 主HUD中放置武器资源面板的容器插槽。 */
	UPROPERTY(meta = (BindWidget), BlueprintReadOnly, Category = "MHGZ|UI")
	TObjectPtr<UOverlay> WeaponResourceSlot;
};
