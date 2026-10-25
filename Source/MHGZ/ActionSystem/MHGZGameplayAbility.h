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
class USoundBase;
class UAnimNotify_SwingSound;
class UMHGZWeaponDefinition;
struct FStreamableHandle;

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

	/** 仅控制本GA能否由缓存输入触发；关闭不影响合法窗口内的即时输入，也不禁止本招期间缓存其它招式。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability|Input",
		meta = (DisplayName = "允许被预输入触发"))
	bool bAllowPreInputActivation = true;

	/** 耐力消耗策略：None / Instant（单次）/ PerSecond（Drain 任务按实际时间结算） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability|Cost")
	EAbilityStaminaCostPolicy StaminaCostPolicy = EAbilityStaminaCostPolicy::None;

	/** 单次耐力扣除量（Instant 策略；最终值 = 本值 × StaminaDeductionRate） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability|Cost")
	FScalableFloat StaminaCost;

	/** 发动所需最低耐力；-1沿用实际单次消耗，0无门槛。独立门槛不乘耗耐倍率；实际扣耐仍由StaminaCost决定，不足时扣到0。Combo边的门槛仍需同时满足。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability|Cost",
		meta = (ClampMin = "-1.0", DisplayName = "发动所需最低耐力"))
	float StaminaActivationRequirement = -1.f;

	/** 持续耐力消耗速率（每秒；最终值 = 本值 × StaminaConsumptionRate × 实际经过时间） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability|Cost")
	FScalableFloat StaminaCostRate;

	/** -1 使用地面/空中基础回复；>=0 为本动作每秒回复点数（再乘回复倍率）。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability|Cost", meta=(ClampMin="-1"))
	float StaminaRecoveryOverride = -1.f;

	/** Instant成本在动作内的已验证时刻支付，而非Commit；中途取消不扣未支付成本。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability|Cost")
	bool bDeferInstantStaminaCost = false;

	/** 当前动作的回复速率；负数表示使用环境基础值。 */
	virtual float GetStaminaRecoveryPerSecond() const;

	/** 武器专属资源成本规格（经 RuntimeHost 透传给 ResourceProvider 预留） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability|Cost")
	TArray<FWeaponResourceCostSpec> WeaponResourceCosts;

	/** 冷却时长（秒；Cooldown GE 的 Spec Duration） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability|Cooldown")
	FScalableFloat CooldownDuration;

	/** 冷却标签（作为 Cooldown GE 的 DynamicGrantedTag，UI 显示冷却） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability|Cooldown")
	FGameplayTag CooldownTag;

	/** 攻击方向修正的单次旋转上限（度）；超出上限但超出量不超过 90 度时，仍按此上限转向。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability|Correction")
	float MaxCorrectionAngle = 30.0f;

	/** 挥刀风声身份标签 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability|Audio")
	FGameplayTag AudioIdentityTag;

	/** 按实际Montage通知播放挥击声；迁移后的GA同时停用旧的激活即播三灯钩子。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ability|Audio")
	bool bUseAuthoredSwingAudio = false;

	/** 本动作明确允许交给MM的退出候选家族。空数组表示不允许，避免误放通知提前结束GA或改变根运动所有权；只路由数据库家族，不直接指定动画。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability|Motion Matching")
	TArray<EMHGZMotionMatchingHandoffType> AllowedMotionMatchingHandoffTypes;

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

	/**
	 * 提前释放本 Action 已获取的一个 Ability Tag Token。
	 * 仅匹配 OwnedActionTagTokens 中的精确 Token；不会按 Tag 名影响其他所有者。
	 */
	bool ReleaseActionTag(FWeaponOwnedTagToken& InOutToken);

	/** 释放本 Action 已获取的全部 Ability Tags。 */
	void ReleaseActionTags();

	bool IsActionActivationCommitted() const { return bIsActionActivationCommitted; }

	/** 自动化专用：把实例置入/移出「已 Commit」态（生产路径由 ActivateAbility 流水线写入）。 */
	void SetActionActivationCommittedForTest(bool bInCommitted) { bIsActionActivationCommitted = bInCommitted; }

	const FWeaponActionToken& GetActionToken() const { return CurrentActionToken; }

	const FWeaponAbilityActivationContext& GetWeaponActivationContext() const { return ActivationContext; }

	UMHGZWeaponRuntimeHostComponent* GetRuntimeHost() const;

	/** Only the registered Mesh+Montage instance and committed Action may dispatch. */
	bool HandleSwingSound(const FWeaponActionToken& Action, USkeletalMeshComponent* Mesh,
		int32 MontageInstanceID, const UAnimNotify_SwingSound& Recipe);

	// ------------------------------------------------------------------
	// M4.4 authored Root Motion Phase / Motion Matching Handoff contract
	// ------------------------------------------------------------------
	/** Called only by AnimNotifyState_ActionRootMotionPhase. */
	bool BeginActionRootMotionPhase(const FWeaponActionToken& ActionToken,
		bool bOwnsMontageRootMotion, bool bObserveRawMovementInput);

	/** Called by the same NotifyState each animation tick while observing input. */
	bool ObserveActionRootMotionPhase(const FWeaponActionToken& ActionToken);

	/** Called only by the matching NotifyState end. */
	bool EndActionRootMotionPhase(const FWeaponActionToken& ActionToken,
		bool bOwnedMontageRootMotion, bool bObservedRawMovementInput);

	/** Called only by AnimNotify_MotionMatchingHandoff after an authored safe frame. */
	bool HandleMotionMatchingHandoff(const FWeaponActionToken& ActionToken,
		EMHGZMotionMatchingHandoffType HandoffType);

protected:
	/** Explicit legacy compatibility only; a sound event is not an attack segment. */
	virtual USoundBase* GetLegacySwingSound(int32 SegmentIndex) const { return nullptr; }

	/** Infrastructure/forced-reaction abilities override this explicitly. */
	virtual bool ShouldIgnorePlayerActionLocks() const { return false; }

	/** True while Sheathing, Dodging or the system-owned aerial landing is present on the ASC. */
	static bool HasPlayerActionInputLock(const UAbilitySystemComponent* ASC);

	/**
	 * Action 依赖预检（Commit 前、零副作用时调用）。
	 * 返回 false 时本次激活直接 Reject/End，不产生预留、成本、冷却或 Tag。
	 * 默认 true；子类（如 Dodge）在此校验角色、AnimInstance、方向蒙太奇等依赖。
	 */
	virtual bool ValidateActionDependencies() const { return true; }

	/**
	 * Builds this activation's weapon-resource costs after ActionToken creation and
	 * dependency validation, but before reservation/Commit. Most actions use the
	 * authored WeaponResourceCosts unchanged; conditional-cost actions may freeze
	 * their branch and return a different list here.
	 */
	virtual bool PrepareActivationWeaponResourceCosts(
		TArray<FWeaponResourceCostSpec>& OutCosts)
	{
		OutCosts = WeaponResourceCosts;
		return true;
	}

	/** Leaf actions with a gameplay Commit override this before opting into a handoff. */
	virtual bool IsMotionMatchingHandoffCommitComplete() const { return true; }

	/**
	 * Production reads AMHGZCharacter's physical-stick state. The virtual seam
	 * lets native automation use the existing lightweight ACharacter harness
	 * without changing movement semantics or injecting a fake player input path.
	 */
	virtual bool GetMotionMatchingRawMoveInput(FVector2D& OutRawMoveInput,
		bool& bOutHasRawMoveInput) const;

	/** 构造 Instant 耐力成本 GE Spec（SetByCaller Data.Cost.Stamina = CostMagnitude）。 */
	FGameplayEffectSpecHandle MakeStaminaCostSpec(float CostMagnitude) const;

	/** 构造冷却 GE Spec（HasDuration，Duration 写入 Spec）。 */
	FGameplayEffectSpecHandle MakeCooldownSpec(float Duration) const;

	/** 当前等级下 Instant 策略的实际耐力成本（StaminaCost × StaminaDeductionRate）。 */
	virtual float GetInstantStaminaCost(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo) const;

	/** 幂等支付延后成本；支付前再次CheckCost，失败由具体动作安全退出。 */
	bool TryPayDeferredStaminaCost();

private:
	friend class FMHGZSwingAudioTests;
	void PrepareSwingSoundOverrides();
	void RefreshLoadedSwingSounds(const FWeaponActionToken& ExpectedAction);
	void ResetSwingSoundOverrides();
	bool IsSwingSoundActionCurrent(const FWeaponActionToken& Action) const;
	USoundBase* ResolveSwingSound(const FWeaponActionToken& Action,
		USkeletalMeshComponent* Mesh, int32 MontageInstanceID,
		const UAnimNotify_SwingSound& Recipe) const;

	/** 本次动作已加载的挥击音源覆盖表，保持声音资源在播放期间有效。 */
	UPROPERTY(Transient)
	TMap<FGameplayTag, TObjectPtr<USoundBase>> LoadedSwingSoundOverrides;
	/** 建立本次挥击音效快照所使用的武器定义。 */
	UPROPERTY(Transient)
	TObjectPtr<UMHGZWeaponDefinition> PreparedSwingWeapon;
	TSharedPtr<FStreamableHandle> SwingSoundLoadHandle;
	TSet<FGameplayTag> MissingSwingSoundKeys;

	friend class FMHGZStaminaCostsTest;
	bool bDeferredStaminaPaid = false;

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

	struct FMotionMatchingPhaseState
	{
		int32 ActivePhaseDepth = 0;
		int32 OwningRootMotionPhaseDepth = 0;
		int32 InputObservationPhaseDepth = 0;
		bool bObservedAnyPhase = false;
		bool bObservedRawMoveInput = false;
		bool bReleasedRawMoveInput = false;
		bool bRawMoveInputWasActive = false;
		FVector2D LastRawMoveInput = FVector2D::ZeroVector;
		float LastActiveRawMoveCruiseSpeed = 0.0f;
	};

	FMotionMatchingPhaseState MotionMatchingPhaseState;

	bool IsCurrentMotionMatchingAction(const FWeaponActionToken& ActionToken) const;
	void SampleMotionMatchingPhaseInput();
};
