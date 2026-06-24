// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "MHGZDummyConfig.generated.h"

class USkeletalMesh;
class UAnimMontage;

/**
 * 木桩部位配置
 */
USTRUCT(BlueprintType)
struct FDummyHitzoneConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName BoneName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag HitzoneTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float DefenseMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float StaggerRate = 0.f;

	/** 碰撞形状参数 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector HalfExtent = FVector(30, 30, 30);
};

/**
 * UMHGZDummyConfig — 木桩配置 DataAsset
 */
UCLASS(BlueprintType)
class UMHGZDummyConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSoftObjectPtr<USkeletalMesh> DisplayMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSoftObjectPtr<UAnimMontage> LoopingMontage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FDummyHitzoneConfig> Hitzones;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override
	{
		return FPrimaryAssetId(TEXT("DummyConfig"), GetFName());
	}
};
