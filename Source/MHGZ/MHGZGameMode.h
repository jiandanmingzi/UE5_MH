// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameMode.h"
#include "MHGZGameMode.generated.h"

/**
 * AMHGZGameMode — Demo GameMode
 * 继承 AGameMode（非 AGameModeBase），支持 GameState、PlayerState 等完整功能
 * 支持 Seamless Travel（据点↔任务地图）
 */
UCLASS(abstract)
class AMHGZGameMode : public AGameMode
{
	GENERATED_BODY()

public:
	AMHGZGameMode();
};
