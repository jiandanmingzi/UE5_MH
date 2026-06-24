// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MHGZUISubsystem.generated.h"

/**
 * UMHGZUISubsystem — UI 子系统
 * 管理武器资源 Widget 的创建/销毁
 */
UCLASS()
class UMHGZUISubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "MHGZ|UI")
	static UMHGZUISubsystem* Get(const UObject* WorldContext);
};
