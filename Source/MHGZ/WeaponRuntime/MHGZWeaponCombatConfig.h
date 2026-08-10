// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Misc/DataValidation.h"
#include "ActionSystem/MHGZWeaponComboData.h"
#include "MHGZWeaponCombatConfig.generated.h"

/**
 * UWeaponCombatConfigBase —— 通用武器战斗配置基类。
 * 每种武器派生自己的 CombatConfig 并由 UWeaponRuntimeDefinition 引用；
 * 虫棍使用 UInsectGlaiveCombatConfig。
 */
UCLASS(Abstract, BlueprintType)
class MHGZ_API UWeaponCombatConfigBase : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 该武器唯一的连招表（虫棍 ComboData 由其 CombatConfig 引用） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	TObjectPtr<UMHGZWeaponComboData> ComboData;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
