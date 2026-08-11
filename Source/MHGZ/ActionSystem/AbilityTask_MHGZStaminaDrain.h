// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/Tasks/AbilityTask.h"
#include "AbilityTask_MHGZStaminaDrain.generated.h"

class UMHGZGameplayAbility;

/**
 * PerSecond 耐力消耗任务。
 * 以 0.1s Timer 驱动，但按真实经过时间结算：
 * 每 Tick 幅度 = Rate × StaminaConsumptionRate × 实际经过时间，
 * 通过 UMHGZStaminaCostGameplayEffect（SetByCaller Data.Cost.Stamina）结算。
 * 下一笔付款不足时调用所属 GA 的 RequestEndAction(Cancelled)。
 */
UCLASS()
class MHGZ_API UAbilityTask_MHGZStaminaDrain : public UAbilityTask
{
	GENERATED_BODY()

public:
	UAbilityTask_MHGZStaminaDrain(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintCallable, Category = "Ability|Tasks",
		meta = (HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "true"))
	static UAbilityTask_MHGZStaminaDrain* StartStaminaDrain(UGameplayAbility* OwningAbility);

	virtual void Activate() override;

	virtual void OnDestroy(bool bInOwnerFinished) override;

private:
	void OnDrainTick();

	TWeakObjectPtr<UMHGZGameplayAbility> OwnerAbility;
	FTimerHandle DrainTimerHandle;
	double LastTickTimeSeconds = 0.0;
};
