// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Camera/PlayerCameraManager.h"
#include "MHGZPlayerCameraManager.generated.h"

/** Shared camera-pitch limits used by both free camera input and locked attack aim. */
UCLASS(Blueprintable)
class MHGZ_API AMHGZPlayerCameraManager : public APlayerCameraManager
{
	GENERATED_BODY()

public:
	AMHGZPlayerCameraManager();
};
