// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MHGZQuickBarComponent.generated.h"

/**
 * UMHGZQuickBarComponent — 快捷栏组件（Demo 桩代码）
 * 挂载到 PlayerController
 */
UCLASS(ClassGroup = (MHGZ))
class UMHGZQuickBarComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMHGZQuickBarComponent();
};
