// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "MHGZPlayerController.generated.h"

class UUserWidget;
class UMHGZInputComponent;
class UMHGZQuickBarComponent;
class UMHGZWeaponInputRouterComponent;

/**
 * AMHGZPlayerController
 * 管理 IMC、输入组件、快捷栏
 */
UCLASS(abstract)
class AMHGZPlayerController : public APlayerController
{
	GENERATED_BODY()

protected:
	/** Mobile controls widget class */
	UPROPERTY(EditAnywhere, Category="Input|Touch Controls")
	TSubclassOf<UUserWidget> MobileControlsWidgetClass;

	UPROPERTY()
	TObjectPtr<UUserWidget> MobileControlsWidget;

	// ── MHGZ 组件 ──
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MHGZ|Components")
	TObjectPtr<UMHGZInputComponent> InputComponent_MHGZ;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MHGZ|Components")
	TObjectPtr<UMHGZQuickBarComponent> QuickBarComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MHGZ|Components")
	TObjectPtr<UMHGZWeaponInputRouterComponent> WeaponInputRouterComponent;

public:
	AMHGZPlayerController();

	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category="MHGZ|Input")
	UMHGZInputComponent* GetMHGZInputComponent() const { return InputComponent_MHGZ; }

	UFUNCTION(BlueprintCallable, Category="MHGZ|Input")
	UMHGZWeaponInputRouterComponent* GetWeaponInputRouter() const { return WeaponInputRouterComponent; }

	UFUNCTION(BlueprintCallable, Category="MHGZ|QuickBar")
	UMHGZQuickBarComponent* GetQuickBarComponent() const { return QuickBarComponent; }
};
