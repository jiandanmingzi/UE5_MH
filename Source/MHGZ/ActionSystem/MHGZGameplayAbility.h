// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Engine/HitResult.h"
#include "GameplayTagContainer.h"
#include "WeaponRuntime/MHGZWeaponRuntimeTypes.h"
#include "MHGZGameplayAbility.generated.h"

class UAbilityTask_MHGZStaminaDrain;
class UMHGZAbilitySystemComponent;
class UMHGZWeaponRuntimeHostComponent;
class USkeletalMeshComponent;

/**
 * UMHGZGameplayAbility — 所有 Action Ability 的 M1 基类
 * - 默认 InstancedPerExecution + LocalOnly；
 * - 耐力/冷却统一走原生 GE（SetByCaller / HasDuration + DynamicGrantedTags），无 Loose 冷却；
 * - 激活时构建 FWeaponActionToken：预留资源 → Commit → 结算 → 转移确认 → 注册 Active Action；
 * - 暴露 EndAbility 前的幂等清理（Drain 任务、预留、Montage/Action 注册、Ability Tags）。
 */
UCLASS(BlueprintType, Blueprintable, Abstract)
class MHGZ_API UMHGZGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UMHGZGameplayAbility();

	// ═══════════════════════════════════════════
	// 配置（蓝图可编辑）
	// ═══════════════════════════════════════════

	/** 绑定的输入标签（ASC 输入快照按此精确匹配） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability|Input")
	FGameplayTag InputTag;

	/** 耐力消耗策略：None / Instant（单次）/ PerSecond（Drain 任务按实际时间结算） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability|Cost")
	EAbilityStaminaCostPolicy StaminaCostPolicy = EAbilityStaminaCostPolicy::None;

	/** 单次耐力扣除量（Instant 策略；最终值 = 本值 × StaminaDeductionRate） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability|Cost")
	FScalableFloat StaminaCost;

	/** 持续耐力消耗速率（每秒；最终值 = 本值 × StaminaConsumptionRate × 实际经过时间） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability|Cost")
	FScalableFloat StaminaCostRate;

	/** 武器专属资源成本规格（经 RuntimeHost 透传给 ResourceProvider 预留） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability|Cost")
	TArray<FWeaponResourceCostSpec> WeaponResourceCosts;

	/** 冷却时长（秒；Cooldown GE 的 Spec Duration） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability|Cooldown")
	FScalableFloat CooldownDuration;

	/** 冷却标签（作为 Cooldown GE 的 DynamicGrantedTag，UI 显示冷却） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability|Cooldown")
	FGameplayTag CooldownTag;

	/** 攻击激活瞬间最大方向修正角度（度） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability|Correction")
	float MaxCorrectionAngle = 30.0f;

	/** 挥刀风声身份标签 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability|Audio")
	FGameplayTag AudioIdentityTag;

	// ═══════════════════════════════════════════
	// GAS 覆写（成本/冷却均走原生 GE）
	// ═══════════════════════════════════════════

	virtual bool CheckCost(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		OUT FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	virtual void ApplyCost(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) const override;

	virtual bool CheckCooldown(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		OUT FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	virtual void ApplyCooldown(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) const override;

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
	// M1 Action 运行时 API
	// ═══════════════════════════════════════════

	/** 请求结束本次 Action；首个 EWeaponActionEndReason 生效，重复请求忽略。 */
	void RequestEndAction(EWeaponActionEndReason Reason);

	/** 首个请求的结束原因；从未请求时返回 Normal。 */
	EWeaponActionEndReason GetActionEndReason() const
	{
		return bActionEndRequested ? ActionEndReason : EWeaponActionEndReason::Normal;
	}

	/**
	 * 输入释放回调（由 RuntimeHost 按激活输入身份精确分发）。
	 * 默认 No-Op：仅蓄力/按住型子类按需覆写并以自身策略结束 Action。
	 */
	virtual void HandleInputReleased(const FWeaponInputSnapshot& Snapshot);

	/** 落地回调（由协调器对精确 Active ActionToken 调用）；默认无操作。 */
	virtual void HandleLanded(const FHitResult& Hit);

	/** 注册本次 Action 的 Montage 实例（精确 Mesh+InstanceID → ActionToken）。返回 Host 注册结果。 */
	bool RegisterMontageInstance(USkeletalMeshComponent* Mesh, int32 MontageInstanceID);

	/** 以 Ability 所有者身份经 Ledger 获取 Action Tags；EndAbility 时自动释放。 */
	FWeaponOwnedTagToken AcquireActionTags(const FGameplayTagContainer& Tags, FName LocalID = NAME_None);

	/** 释放本 Action 已获取的全部 Ability Tags。 */
	void ReleaseActionTags();

	bool IsActionActivationCommitted() const { return bIsActionActivationCommitted; }

	const FWeaponActionToken& GetActionToken() const { return CurrentActionToken; }

	const FWeaponAbilityActivationContext& GetWeaponActivationContext() const { return ActivationContext; }

	UMHGZWeaponRuntimeHostComponent* GetRuntimeHost() const;

protected:
	/**
	 * Action 依赖预检（Commit 前、零副作用时调用）。
	 * 返回 false 时本次激活直接 Reject/End，不产生预留、成本、冷却或 Tag。
	 * 默认 true；子类（如 Dodge）在此校验角色、AnimInstance、方向蒙太奇等依赖。
	 */
	virtual bool ValidateActionDependencies() const { return true; }

	/** 构造 Instant 耐力成本 GE Spec（SetByCaller Data.Cost.Stamina = CostMagnitude）。 */
	FGameplayEffectSpecHandle MakeStaminaCostSpec(float CostMagnitude) const;

	/** 构造冷却 GE Spec（HasDuration，Duration 写入 Spec）。 */
	FGameplayEffectSpecHandle MakeCooldownSpec(float Duration) const;

private:
	/** 当前等级下 Instant 策略的实际耐力成本（StaminaCost × StaminaDeductionRate）。 */
	float GetInstantStaminaCost(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo) const;

	FWeaponActionToken CurrentActionToken;
	FWeaponAbilityActivationContext ActivationContext;
	FWeaponResourceCostReservation ActiveReservation;
	bool bReservationConsumed = false;
	bool bIsActionActivationCommitted = false;
	bool bActionEndRequested = false;
	EWeaponActionEndReason ActionEndReason = EWeaponActionEndReason::Normal;
	TArray<FWeaponOwnedTagToken> OwnedActionTagTokens;
	TWeakObjectPtr<UAbilityTask_MHGZStaminaDrain> StaminaDrainTask;
	bool bMHGZEndCleanupDone = false;
};
