// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MHGZEdgeVaultComponent.generated.h"

/**
 * UMHGZEdgeVaultComponent — 边缘跳越组件（挂载到 Character）
 * Demo 阶段桩实现——后续实现 CMC 边缘检测 + 自动触发 GA_EdgeVault
 */
UCLASS(ClassGroup = (MHGZ))
class UMHGZEdgeVaultComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMHGZEdgeVaultComponent();
};
