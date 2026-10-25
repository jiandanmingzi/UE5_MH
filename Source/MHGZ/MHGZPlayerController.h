// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "UI/MHGZUITelemetry.h"
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
	/** 触屏设备使用的移动控制界面类。 */
	UPROPERTY(EditAnywhere, Category="Input|Touch Controls")
	TSubclassOf<UUserWidget> MobileControlsWidgetClass;

	/** 当前已创建的触屏控制界面实例。 */
	UPROPERTY()
	TObjectPtr<UUserWidget> MobileControlsWidget;

	// ── MHGZ 组件 ──
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MHGZ|Components")
	TObjectPtr<UMHGZInputComponent> InputComponent_MHGZ;

	/** 负责玩家快捷栏使用输入与当前选择的组件。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MHGZ|Components")
	TObjectPtr<UMHGZQuickBarComponent> QuickBarComponent;

	/** 当前玩家的武器组合键输入路由组件。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MHGZ|Components")
	TObjectPtr<UMHGZWeaponInputRouterComponent> WeaponInputRouterComponent;

	FMHGZUIEventTelemetryQueue UITelemetryEvents;

public:
	AMHGZPlayerController();

	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	void RefreshMHGZHUDContext();
	void RecordUITelemetryEvent(FName EventName, const UObject* Subject = nullptr,
		FName Reason = NAME_None, uint64 RuntimeGeneration = 0);
	void GetUITelemetryEventsSince(uint64& InOutLastSerial,
		TArray<FMHGZUIEventTelemetry>& OutEvents) const;
	uint64 GetLatestUITelemetrySerial() const { return UITelemetryEvents.GetLatestSerial(); }

	UFUNCTION(BlueprintCallable, Category="MHGZ|Input")
	UMHGZInputComponent* GetMHGZInputComponent() const { return InputComponent_MHGZ; }

	UFUNCTION(BlueprintCallable, Category="MHGZ|Input")
	UMHGZWeaponInputRouterComponent* GetWeaponInputRouter() const { return WeaponInputRouterComponent; }

	UFUNCTION(BlueprintCallable, Category="MHGZ|QuickBar")
	UMHGZQuickBarComponent* GetQuickBarComponent() const { return QuickBarComponent; }
};
