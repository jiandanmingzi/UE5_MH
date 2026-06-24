// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MHGZInputComponent.generated.h"

class UInputMappingContext;

/**
 * UMHGZInputComponent — 输入组件（挂载到 PlayerController）
 * 管理 IMC 生命周期
 */
UCLASS(ClassGroup = (MHGZ))
class UMHGZInputComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMHGZInputComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TArray<UInputMappingContext*> DefaultIMCs;

	void PushIMC(UInputMappingContext* IMC, int32 Priority);
	void PopIMC(UInputMappingContext* IMC);

protected:
	virtual void BeginPlay() override;
};
