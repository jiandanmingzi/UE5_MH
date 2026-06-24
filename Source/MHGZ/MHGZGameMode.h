// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MHGZGameMode.generated.h"

/**
 * AMHGZGameMode — Demo GameMode
 * 支持 Seamless Travel（据点↔任务地图）
 */
UCLASS(abstract)
class AMHGZGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AMHGZGameMode();
};
