// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "InsectGlaiveKinsectData.generated.h"

class USkeletalMesh;
class UMaterialInstance;
class UAnimMontage;
class UAnimInstance;

/**
 * UInsectGlaiveKinsectData — 猎虫品种 DataAsset。
 * 策划为每种猎虫品种创建一个资产。
 */
UCLASS(BlueprintType)
class UInsectGlaiveKinsectData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 品种名称 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FText KinsectDisplayName;

	/** 猎虫模型 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSoftObjectPtr<USkeletalMesh> KinsectMesh;

	/** 按 SlotName 覆盖材质 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TMap<FName, TSoftObjectPtr<UMaterialInstance>> MaterialOverrides;

	/** 飞行动画 Montage（前进/返回/悬停共用，通过 PlayRate 控制） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSoftObjectPtr<UAnimMontage> FlyMontage;

	/** 猎虫外观 AnimBP；只负责飞行/悬停/附着表现，不拥有玩法状态。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSubclassOf<UAnimInstance> KinsectAnimClass;

	/** 基础飞行速度（cm/s） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float FlightSpeed = 2000.f;

	/** 返回飞行速度（cm/s） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float ReturnSpeed = 2500.f;

	/** 最大飞行距离（cm）——臂上放虫用 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float MaxFlightRange = 3000.f;

	/** 收刀 RT 直飞距离（cm） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float StraightFlightDistance = 1500.f;

	/** 基础耐力上限 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float StaminaPool = 100.f;

	/** 基础耐力回复速率 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float StaminaRegenRate = 15.f;

	/** 悬停耐力消耗速率 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float HoverDrainRate = 3.f;

	/** 飞行耐力消耗速率 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float FlightDrainRate = 8.f;

	/** 猎虫基础攻击力 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float KinsectAttackPower = 10.0f;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override
	{
		return FPrimaryAssetId(TEXT("KinsectData"), GetFName());
	}
};
