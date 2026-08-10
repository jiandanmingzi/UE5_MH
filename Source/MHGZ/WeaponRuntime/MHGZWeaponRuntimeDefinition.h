// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Misc/DataValidation.h"
#include "WeaponRuntime/MHGZWeaponCombatConfig.h"
#include "WeaponRuntime/MHGZWeaponInputProfile.h"
#include "MHGZWeaponRuntimeDefinition.generated.h"

class UMHGZWeaponResourceComponent;
class UMHGZWeaponResourceWidget;

/**
 * UWeaponRuntimeDefinition —— 每种武器类型的运行时接线唯一入口。
 * 具体 UMHGZWeaponDefinition（物品攻击力/外观/词条）只引用对应的 RuntimeDefinition。
 */
UCLASS(BlueprintType)
class MHGZ_API UWeaponRuntimeDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 武器种类 Tag（例如 Weapon.IG） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Runtime")
	FGameplayTag WeaponTypeTag;

	/** 当前武器资源组件基类（虫棍为 URes_InsectGlaive 派生类） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Runtime")
	TSubclassOf<UMHGZWeaponResourceComponent> ResourceComponentClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Runtime")
	TObjectPtr<UWeaponInputProfile> InputProfile;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Runtime")
	TObjectPtr<UWeaponCombatConfigBase> CombatConfig;

	/** 本地 HUD 资源插槽中创建的资源 Widget 类 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Runtime")
	TSubclassOf<UMHGZWeaponResourceWidget> ResourceWidgetClass;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
