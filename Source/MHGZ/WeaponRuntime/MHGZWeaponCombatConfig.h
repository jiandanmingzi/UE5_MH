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
	 * 空回（空中回避）后坠的下落表现 clip。真值 = id `157`（回避后**任何灯态**都是它）。
	 * ⚠ 与 `GetAerialFallingMontage(bEnhancedVariant)` 解耦：那个旗标一手挑 clip 一手挑
	 * 重力，而空回要 157 的 clip、重力却仍是非白灯档。
	 */
	virtual UAnimMontage* GetAerialDodgeFallMontage() const
	{
		return GetAerialFallingMontage(false);
	}

	/** 落地水平重设值（cm/s）：真值 `148` 首帧恒 337±6、入速无关（下落物理.md §六）。 */
	virtual float GetAerialLandingHorizontalSpeed() const
	{
		return 337.0f;
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

	/**
	 * Longest a single system-owned free fall may last before the Host's watchdog
	 * ends it.  Free fall had no lifetime management at all: the visual montage has
	 * no end delegate, no loop counter and no timeout, so a long fall silently loops
	 * it forever (measured: a 0.85 s clip wrapping by exact modulo at weight 1.0),
	 * and a character that leaves the level simply keeps falling (measured: still
	 * descending at Z = -1939 cm when the capture ended, tag still held).
	 *
	 * The watchdog only restores physics, releases the pose tags and returns the CMC
	 * to its default mode, and logs; it never repositions anything.
	 */
	virtual float GetAerialFallMaxSeconds() const { return 4.0f; }

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
