// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "WeaponRuntime/MHGZWeaponRuntimeTypes.h"
#include "MHGZHUD.generated.h"

class UMHGZAttributeBarWidget;
class UMHGZCrosshairWidget;
class UMHGZMainHUDWidget;
class UMHGZWeaponResourceWidget;
class UAbilitySystemComponent;
class UMHGZWeaponRuntimeHostComponent;
struct FWeaponRuntimeToken;

/**
 * AMHGZHUD — 本地玩家HUD唯一的Widget生命周期所有者。
 */
UCLASS()
class MHGZ_API AMHGZHUD : public AHUD
{
	GENERATED_BODY()

public:
	AMHGZHUD();
	void RefreshPawnContext();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
	/** 玩家主HUD根控件类。 */
	UPROPERTY(EditDefaultsOnly, Category = "MHGZ|HUD")
	TSubclassOf<UMHGZMainHUDWidget> MainHUDClass;

	/** 主HUD使用的生命条控件类。 */
	UPROPERTY(EditDefaultsOnly, Category = "MHGZ|HUD")
	TSubclassOf<UMHGZAttributeBarWidget> HealthBarClass;

	/** 主HUD使用的耐力条控件类。 */
	UPROPERTY(EditDefaultsOnly, Category = "MHGZ|HUD")
	TSubclassOf<UMHGZAttributeBarWidget> StaminaBarClass;

	/** 主HUD使用的瞄准准心控件类。 */
	UPROPERTY(EditDefaultsOnly, Category = "MHGZ|HUD")
	TSubclassOf<UMHGZCrosshairWidget> CrosshairClass;

	/** 当前玩家已创建的HUD根控件实例。 */
	UPROPERTY(Transient)
	TObjectPtr<UMHGZMainHUDWidget> MainHUDWidget;

	/** 当前HUD中的生命条实例。 */
	UPROPERTY(Transient)
	TObjectPtr<UMHGZAttributeBarWidget> HealthBarWidget;

	/** 当前HUD中的耐力条实例。 */
	UPROPERTY(Transient)
	TObjectPtr<UMHGZAttributeBarWidget> StaminaBarWidget;

	/** 当前HUD中的准心实例。 */
	UPROPERTY(Transient)
	TObjectPtr<UMHGZCrosshairWidget> CrosshairWidget;

	/** 当前武器对应的资源面板实例。 */
	UPROPERTY(Transient)
	TObjectPtr<UMHGZWeaponResourceWidget> WeaponResourceWidget;

private:
	friend struct FMHGZM6HUDTestAccess;
	UFUNCTION()
	void HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn);
	void UnbindPawnContext();
	void RemoveWeaponResourceWidget(FName Reason = NAME_None);
	void HandleWeaponRuntimeReady(const FWeaponRuntimeToken& Token,
		class UMHGZWeaponResourceComponent* Resource);
	void HandleWeaponRuntimeInvalidated(const FWeaponRuntimeToken& Token);

	TWeakObjectPtr<class AMHGZCharacter> BoundCharacter;
	TWeakObjectPtr<UAbilitySystemComponent> BoundASC;
	TWeakObjectPtr<class UMHGZAimComponent> BoundAimComponent;
	TWeakObjectPtr<UMHGZWeaponRuntimeHostComponent> BoundRuntimeHost;
	FDelegateHandle RuntimeReadyHandle;
	FDelegateHandle RuntimeInvalidatedHandle;
	FWeaponRuntimeToken BoundRuntimeToken;
	// Possess/Ready may arrive before BeginPlay has created the widget tree.
	bool bWidgetTreeReady = false;
	bool bPawnContextInitialized = false;
};
