// Copyright MHGZ Project. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MHGZGroundDustTestReceiver.generated.h"

class USkeletalMeshComponent;

UCLASS()
class UMHGZGroundDustTestReceiver : public UObject
{
	GENERATED_BODY()
public:
	int32 Count = 0;
	int32 LandingCount = 0;
	int32 ImpactCount = 0;
	FVector LastNormal = FVector::ZeroVector;
	UFUNCTION()
	void ReceiveDust(USkeletalMeshComponent* SourceMesh, FVector Location, FVector SurfaceNormal)
	{
		++Count;
		LastNormal = SurfaceNormal;
	}
	UFUNCTION()
	void ReceiveLanding(USkeletalMeshComponent* SourceMesh, FVector Location, FVector SurfaceNormal)
	{
		++LandingCount;
		LastNormal = SurfaceNormal;
	}
	UFUNCTION()
	void ReceiveImpact(USkeletalMeshComponent* SourceMesh, FVector Location, FVector SurfaceNormal)
	{
		++ImpactCount;
		LastNormal = SurfaceNormal;
	}
};
