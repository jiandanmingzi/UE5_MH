// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MHGZMonsterBase.h"
#include "MHGZTrainingDummy.generated.h"

struct FOnAttributeChangeData;
class UMHGZDummyConfig;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDummyHealthChanged,
	float, CurrentHealth, float, MaxHealth);

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

	/** 放到关卡后直接配置；BeginPlay 自动生成 Hitzone。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MHGZ|Dummy")
	TObjectPtr<UMHGZDummyConfig> DummyConfig;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MHGZ|Dummy", meta = (ClampMin = "1.0"))
	float DummyMaxHealth = 1000.f;

	UPROPERTY(BlueprintAssignable, Category = "MHGZ|Dummy")
	FOnDummyHealthChanged OnHealthChanged;

	/** 应用配置 */
	UFUNCTION(BlueprintCallable, Category = "MHGZ|Dummy")
	void ApplyConfig(UMHGZDummyConfig* Config);

	UFUNCTION(BlueprintPure, Category = "MHGZ|Dummy")
	float GetCurrentHealth() const;

	UFUNCTION(BlueprintPure, Category = "MHGZ|Dummy")
	float GetMaxHealth() const;

protected:
	virtual void BeginPlay() override;

private:
	void HandleHealthChanged(const FOnAttributeChangeData& ChangeData);
};
