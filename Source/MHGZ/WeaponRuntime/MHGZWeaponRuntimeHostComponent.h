// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MHGZWeaponRuntimeHostComponent.generated.h"

/**
 * 当前 Pawn 的武器运行时所有者。
 * M0 只建立稳定类型身份，完整生命周期、TagLedger 与注册表在 M1/M2 接入。
 */
UCLASS(ClassGroup = (MHGZ), BlueprintType, meta = (BlueprintSpawnableComponent))
class MHGZ_API UMHGZWeaponRuntimeHostComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMHGZWeaponRuntimeHostComponent();
};
