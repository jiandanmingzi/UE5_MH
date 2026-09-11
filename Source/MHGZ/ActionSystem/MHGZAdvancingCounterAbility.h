// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActionSystem/MHGZIncomingHitResolverComponent.h"
#include "MHGZInsectGlaiveAbility.h"
#include "MHGZAdvancingCounterAbility.generated.h"

/**
 * 突进回旋斩的反击层。
 *
 * Notify 只负责用精确 Mesh + MontageInstanceID 找到本实例，再把窗口开关交给
 * 此 Ability。真正的伤害消费始终由目标侧 IncomingHitResolver 完成；因此反击
 * 不是把角色改成无敌，也不会吞掉没有 AttackInstanceID 的环境伤害。
 */
UCLASS(BlueprintType, Blueprintable)
class MHGZ_API UMHGZAdvancingCounterAbility : public UMHGZInsectGlaiveAbility
{
	GENERATED_BODY()

public:
	UMHGZAdvancingCounterAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility, bool bWasCancelled) override;

	/** 由精确的 Advancing Counter Notify Begin 调用。 */
	bool BeginAdvancingCounterWindow(FName NotifyEventID, float TotalDuration);

	/** 由精确的 Advancing Counter Notify End 及所有取消路径幂等调用。 */
	void EndAdvancingCounterWindow(FName NotifyEventID);

	/** 供调试/自动化确认本次动作最多只允许一次成功反击。 */
	bool HasCounterSucceeded() const { return bCounterSucceeded; }

	/** Resolver 的高优先级数值；同优先级按注册顺序稳定排序。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Counter")
	int32 CounterInterceptorPriority = 100;

protected:
	virtual bool ValidateActionDependencies() const override;

	/**
	 * 反击已由 Resolver 消费后调用。M4.7 只建立舞踏入口；M5 在这里的
	 * 派生实现中接入唯一 Movement Token / BallisticVault，不让地面 GA 偷写 CMC。
	 */
	virtual bool AddAdvancingCounterDanceStack();

	/** 表现或后续空中状态可在蓝图覆写；不承担伤害消费或层数结算。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Counter")
	void OnAdvancingCounterSucceeded(const FIncomingHitContext& Context);

private:
	struct FCounterWindowState
	{
		int64 ResolverTokenID = 0;
		FWeaponOwnedTagToken TagToken;
	};

	EIncomingHitInterceptResult HandleIncomingHit(const FWeaponActionToken& ExpectedAction,
		const FIncomingHitContext& Context);
	void CloseAllAdvancingCounterWindows();

	TMap<FName, FCounterWindowState> CounterWindows;
	bool bCounterSucceeded = false;
	bool bIsEndingCounterAbility = false;
};
