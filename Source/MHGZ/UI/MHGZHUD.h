// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "MHGZHUD.generated.h"

/**
 * AMHGZHUD — Demo HUD
 */
UCLASS()
class AMHGZHUD : public AHUD
{
	GENERATED_BODY()

public:
	AMHGZHUD();

protected:
	virtual void BeginPlay() override;
};
