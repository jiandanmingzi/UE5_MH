// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MHGZMonsterBase.h"
#include "MHGZTrainingDummy.generated.h"

/**
 * AMHGZTrainingDummy — 训练木桩
 * 简化怪物——无 AI，不攻击，仅作为伤害接收方
 */
UCLASS()
class AMHGZTrainingDummy : public AMHGZMonsterBase
{
	GENERATED_BODY()

public:
	AMHGZTrainingDummy();

	/** 应用配置 */
	UFUNCTION(BlueprintCallable, Category = "MHGZ|Dummy")
	void ApplyConfig(class UMHGZDummyConfig* Config);

protected:
	virtual void BeginPlay() override;
};
