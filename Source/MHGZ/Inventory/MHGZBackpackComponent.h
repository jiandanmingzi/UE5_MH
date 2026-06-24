// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MHGZBackpackComponent.generated.h"

/**
 * UMHGZBackpackComponent — 背包组件（Demo 桩代码）
 * 挂载到 PlayerState，跨关卡保留物品
 */
UCLASS(ClassGroup = (MHGZ))
class UMHGZBackpackComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMHGZBackpackComponent();
};
