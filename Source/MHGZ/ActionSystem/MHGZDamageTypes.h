// Copyright MHGZ Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MHGZDamageTypes.generated.h"

UENUM(BlueprintType)
enum class EMHGZPhysicalDamageType : uint8
{
	Sever UMETA(DisplayName = "斩击"),
	Blunt UMETA(DisplayName = "打击"),
	Shot UMETA(DisplayName = "弹射")
};

UENUM(BlueprintType)
enum class EMHGZElementType : uint8
{
	None UMETA(DisplayName = "无属性"),
	Fire UMETA(DisplayName = "火"),
	Water UMETA(DisplayName = "水"),
	Thunder UMETA(DisplayName = "雷"),
	Ice UMETA(DisplayName = "冰"),
	Dragon UMETA(DisplayName = "龙")
};

/** 武器伤害参数；创建 Spec 时快照，不在结算时查询已切换的武器。 */
USTRUCT(BlueprintType)
struct FMHGZWeaponDamageProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
	EMHGZElementType ElementType = EMHGZElementType::None;

	/** 物理斩味倍率。白斩 1.32；默认 1 保持现有无斩味 Demo 数值。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage", meta = (ClampMin = "0.0"))
	float PhysicalSharpnessMultiplier = 1.0f;

	/** 属性斩味倍率。白斩 1.15；与物理斩味独立。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage", meta = (ClampMin = "0.0"))
	float ElementSharpnessMultiplier = 1.0f;

	/** 正会心的物理倍率，基础 1.25；超会心等可改为 1.30/1.35/1.40。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage", meta = (ClampMin = "1.0"))
	float CriticalDamageMultiplier = 1.25f;

	/** 默认为 1，即普通会心不增幅属性；启用会心属性时配置对应武器倍率。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage", meta = (ClampMin = "1.0"))
	float CriticalElementMultiplier = 1.0f;

	bool IsValid() const
	{
		return static_cast<uint8>(ElementType) <= static_cast<uint8>(EMHGZElementType::Dragon)
			&& FMath::IsFinite(PhysicalSharpnessMultiplier) && PhysicalSharpnessMultiplier >= 0.0f
			&& FMath::IsFinite(ElementSharpnessMultiplier) && ElementSharpnessMultiplier >= 0.0f
			&& FMath::IsFinite(CriticalDamageMultiplier) && CriticalDamageMultiplier >= 1.0f
			&& FMath::IsFinite(CriticalElementMultiplier) && CriticalElementMultiplier >= 1.0f;
	}
};
