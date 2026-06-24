// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "MHGZUserWidget.generated.h"

class UAbilitySystemComponent;

/**
 * UMHGZUserWidget — UI Widget 基类
 * 提供 ASC 绑定、Attribute 监听、Tag 订阅的通用功能
 */
UCLASS(Abstract)
class UMHGZUserWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 绑定到 ASC */
	UFUNCTION(BlueprintCallable, Category = "MHGZ|UI")
	void BindToASC(UAbilitySystemComponent* InASC);

	/** 当数值更新时调用（子类覆写） */
	UFUNCTION(BlueprintNativeEvent, Category = "MHGZ|UI")
	void OnValueUpdated(float Current, float Max);

protected:
	UPROPERTY(BlueprintReadOnly, Category = "MHGZ|UI")
	TObjectPtr<UAbilitySystemComponent> BoundASC;
};
