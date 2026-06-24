// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "MHGZPlayerController.generated.h"

class UInputMappingContext;
class UUserWidget;
class UMHGZInputComponent;
class UMHGZQuickBarComponent;

/**
 * AMHGZPlayerController
 * 管理 IMC、输入组件、快捷栏
 */
UCLASS(abstract)
class AMHGZPlayerController : public APlayerController
{
	GENERATED_BODY()

protected:
	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category ="Input|Input Mappings")
	TArray<UInputMappingContext*> DefaultMappingContexts;

	/** Mobile excluded */
	UPROPERTY(EditAnywhere, Category="Input|Input Mappings")
	TArray<UInputMappingContext*> MobileExcludedMappingContexts;

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

public:
	AMHGZPlayerController();

	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

	UFUNCTION(BlueprintCallable, Category="MHGZ|Input")
	UMHGZInputComponent* GetMHGZInputComponent() const { return InputComponent_MHGZ; }

	UFUNCTION(BlueprintCallable, Category="MHGZ|QuickBar")
	UMHGZQuickBarComponent* GetQuickBarComponent() const { return QuickBarComponent; }
};
