// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MHGZUserWidget.h"
#include "MHGZCrosshairWidget.generated.h"

/**
 * UMHGZCrosshairWidget — 准心 Widget
 * 订阅 UMHGZAimComponent::OnAimTargetChanged
 */
UCLASS(Abstract)
class UMHGZCrosshairWidget : public UMHGZUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, Category = "MHGZ|UI")
	void OnAimTargetUpdated(AActor* Target, FGameplayTag HitzoneTag, FGameplayTag ExtractColor);
	virtual void OnAimTargetUpdated_Implementation(AActor* Target, FGameplayTag HitzoneTag, FGameplayTag ExtractColor);
};
