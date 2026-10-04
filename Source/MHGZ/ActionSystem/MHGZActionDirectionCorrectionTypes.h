// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MHGZActionDirectionCorrectionTypes.generated.h"

/** Normalized progress profile used by an authored in-action yaw correction. */
UENUM(BlueprintType)
enum class EActionDirectionCorrectionEasing : uint8
{
	Linear UMETA(DisplayName = "Linear"),
	EaseIn UMETA(DisplayName = "Ease In"),
	EaseOut UMETA(DisplayName = "Ease Out"),
	EaseInOut UMETA(DisplayName = "Ease In Out")
};
