// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayTagContainer.h"
#include "MHGZGameplayAbility.generated.h"

/**
 * UMHGZGameplayAbility — 所有 Ability 的基类
 * 统一处理耐力消耗、冷却、输入标签、武器资源门控
 */
UCLASS(BlueprintType, Blueprintable, Abstract)
class UMHGZGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UMHGZGameplayAbility();

	// ═══════════════════════════════════════════
	// 配置（蓝图可编辑）
	// ═══════════════════════════════════════════

	/** 绑定的输入标签 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability|Input")
	FGameplayTag InputTag;

	/** 单次耐力扣除量（闪避/单次攻击） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability|Cost")
	FScalableFloat StaminaCost;

	/** 持续耐力消耗速率（每秒）——仅 bIsContinuous==true 的 Ability 使用 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability|Cost")
	FScalableFloat StaminaCostRate;

	/** 是否持续型 Ability（true=GA_Sprint/GA_Aim，false=单次型如 GA_Dodge） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability|Cost")
	bool bIsContinuous = false;

	/** 冷却时长 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability|Cooldown")
	FScalableFloat CooldownDuration;

	/** 冷却标签（用于 UI 显示冷却） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability|Cooldown")
	FGameplayTag CooldownTag;

	/** 是否需要武器专属资源 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability|Cost")
	bool bRequiresWeaponResource = false;

	/** 消耗的资源量 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability|Cost")
	FScalableFloat WeaponResourceCost;

	/** ★ 攻击激活瞬间最大方向修正角度（度） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability|Correction")
	float MaxCorrectionAngle = 30.0f;

	/** 挥刀风声身份标签 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability|Audio")
	FGameplayTag AudioIdentityTag;

	// ═══════════════════════════════════════════
	// 覆写
	// ═══════════════════════════════════════════

	virtual bool CanActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags,
		const FGameplayTagContainer* TargetTags,
		FGameplayTagContainer* OptionalRelevantTags) const override;

	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

	// ═══════════════════════════════════════════
	// 武器资源门控（蓝图可覆写）
	// ═══════════════════════════════════════════

	/** 检查当前武器资源是否满足此 Ability 的消耗要求 */
	UFUNCTION(BlueprintNativeEvent, Category = "MHGZ|Ability")
	bool CheckWeaponResourceForAbility() const;
	virtual bool CheckWeaponResourceForAbility_Implementation() const { return true; }

protected:
	/** 扣除单次耐力 */
	void DeductStaminaOnce();

	/** 开始持续耐力消耗 Tick */
	void StartContinuousStaminaDrain();

	/** 停止持续耐力消耗 Tick */
	void StopContinuousStaminaDrain();

	/** 每帧持续耗耐（由 Timer 驱动） */
	void OnContinuousStaminaTick();

private:
	FTimerHandle ContinuousStaminaTimer;
};
