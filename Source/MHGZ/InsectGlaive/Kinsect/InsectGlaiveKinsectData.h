// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "InsectGlaiveKinsectData.generated.h"

class USkeletalMesh;
class UMaterialInstance;
class UAnimMontage;

/**
 * UInsectGlaiveKinsectData — 猎虫品种 DataAsset
 * 策划为每种猎虫品种创建一个资产
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

	/** 按 SlotName 覆写材质 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TMap<FName, TSoftObjectPtr<UMaterialInstance>> MaterialOverrides;

	/** 飞行动画 Montage（前进/返回/悬停共用，通过 PlayRate 控制） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSoftObjectPtr<UAnimMontage> FlyMontage;

	/** 基础飞行速度（cm/s） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float FlightSpeed = 2000.f;

	/** 最大飞行距离（cm）——臂上放虫用 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float MaxFlightRange = 3000.f;

	/** 收刀 RT 直飞距离（cm） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float StraightFlightDistance = 1500.f;

	/** 基础耐力上限 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float StaminaPool = 100.f;

	/** 基础耐力回复速率 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float StaminaRegenRate = 15.f;

	/** 悬停耐力消耗速率 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float HoverDrainRate = 3.f;

	/** 飞行耐力消耗速率 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float FlightDrainRate = 8.f;

	/** ★ 猎虫基础攻击力 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float KinsectAttackPower = 10.0f;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override
	{
		return FPrimaryAssetId(TEXT("KinsectData"), GetFName());
	}
};
