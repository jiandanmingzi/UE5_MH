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

	/**
	 * Returns the presentation montage used while the weapon is in CMC-owned
	 * free fall.  The enhanced variant is weapon-defined (虫棍 uses White
	 * Extract).  A null result means this weapon has no custom aerial pose.
	 */
	virtual UAnimMontage* GetAerialFallingMontage(bool bEnhancedVariant) const
	{
		return nullptr;
	}

	/** A CMC landing may ask the current weapon for one non-root-motion landing visual. */
	virtual UAnimMontage* GetAerialLandingMontage() const
	{
		return nullptr;
	}

	/**
	 * Lets a weapon opt into a temporary CMC physics profile for its
	 * system-owned aerial fall.  Returning false deliberately leaves the
	 * character's ordinary CMC gravity and falling braking untouched.
	 */
	virtual bool ResolveAerialFallingPhysics(bool bEnhancedVariant,
		float& OutGravityScale, float& OutBrakingDecelerationFalling) const
	{
		return false;
	}

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
