// Copyright MHGZ Project. All Rights Reserved.
//
// M5 空中闸门 / 让位测试用的最小 IG 动作双。
//
// 生产流水线（资源预留 / 蒙太奇 / 位移）整体跳过：`ActivateAbility` 直接进
// 「已 Commit、锁存可摆」的可观测状态，让闸门谓词与让位语义可以脱离资产做单测。
// 锁存的生产驱动方是 DetectAerialHandoffNotify / NotifyAerialHandoff，这里用
// `SetAerialHandoffStateForTest` 直接摆。

#pragma once

#include "CoreMinimal.h"
#include "ActionSystem/MHGZInsectGlaiveAbility.h"
#include "MHGZM5TestTypes.generated.h"

UCLASS()
class UMHGZM5TestIGAction : public UMHGZInsectGlaiveAbility
{
	GENERATED_BODY()

public:
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override
	{
		// 走 Token/Commit/RegisterAction 的生产流水线（注册表状态要真），
		// 但跳过攻击层的蒙太奇流水线（Validate 直接过、不起播）。
		UMHGZGameplayAbility::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	}

	virtual bool ValidateActionDependencies() const override { return true; }

	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility, bool bWasCancelled) override
	{
		++EndCount;
		LastEndWasCancelled = bWasCancelled;
		// 全链收尾：中止滞空的「空中可操作」领取 + 预输入重放 poke 都在里面。
		UMHGZInsectGlaiveAbility::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
	}

	/** 让测试驱动生产的 lead 放行路径（DetectAerialHandoffNotify 是 protected）。 */
	void RunDetectForTest(const UAnimMontage* Montage) { DetectAerialHandoffNotify(Montage); }

	int32 EndCount = 0;
	bool LastEndWasCancelled = false;
};
