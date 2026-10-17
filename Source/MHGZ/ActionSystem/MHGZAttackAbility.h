// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CollisionShape.h"
#include "Engine/HitResult.h"
#include "ActionSystem/MHGZActionDirectionCorrectionTypes.h"
#include "MHGZDamageTypes.h"
#include "MHGZGameplayAbility.h"
#include "GameplayTagContainer.h"
#include "Misc/DataValidation.h"
#include "MHGZAttackAbility.generated.h"

class USoundBase;
class UCameraShakeBase;
class UAnimMontage;
class UAbilityTask_PlayMontageAndWait;
class UAbilityTask_MHGZActionDirectionCorrection;
class UMHGZMonsterHitzoneComponent;
class USkeletalMeshComponent;
struct FActionMoveInputFrameSnapshot;

/** 多段伤害默认只能来自真实接触；离散复击必须显式选择并逐跳重验。 */
UENUM(BlueprintType)
enum class EAttackMultiHitPolicy : uint8
{
	ContactOnly,
	LockedTargetTicks
};

/**
 * 一段武器轨迹区域。多个区域可以在同一碰撞窗口内同时生效，
 * 例如虫棍前端和后端同时横扫。
 */
USTRUCT(BlueprintType)
struct MHGZ_API FWeaponTraceRegion
{
	GENERATED_BODY()

	/** 可选起点 Socket/骨骼；留空表示本 TraceRegion 以 EndSocketName 做单点球 Sweep，不读取旧碰撞字段。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trace")
	FName StartSocketName;

	/** 有效攻击区域终点 Socket 或骨骼，必填。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trace")
	FName EndSocketName;

	/** 球形 Sweep 半径（厘米）。球体不受武器旋转朝向影响，适合高速长武器。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trace", meta = (ClampMin = "1.0"))
	float Radius = 14.0f;

	/** 可选水平横向半宽（cm）；0保留球形。大于Radius时横置胶囊，仅沿角色左右方向加宽，不改变竖向/前后厚度和Socket轨迹长度。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trace", meta = (ClampMin = "0.0", Units = "cm"))
	float HorizontalHalfWidth = 0.0f;

	FCollisionShape MakeQueryShape() const;

	/** 棍身相邻采样点的最大间距；运行时还会限制为不超过 2 × Radius。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trace", meta = (ClampMin = "1.0"))
	float MaxSampleSpacing = 20.0f;

	/** 单个区域最多使用的棍身采样点数。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trace", meta = (ClampMin = "1", ClampMax = "32"))
	int32 MaxSampleCount = 16;

	/** 每帧旋转超过该角度时增加时间子步，降低高速旋转沿弧线漏判。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trace", meta = (ClampMin = "1.0", ClampMax = "90.0"))
	float MaxAngularStepDegrees = 15.0f;
};

/**
 * FAttackCollisionConfig — 单段碰撞配置
 */
USTRUCT(BlueprintType)
struct FAttackCollisionConfig
{
	GENERATED_BODY()

	/**
	 * 用于定位武器 SkeletalMeshComponent 的组件 Tag。
	 * 默认 WeaponTrace；找不到时回退到角色主 Mesh，再回退到任意含目标 Socket 的骨骼网格。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName TraceMeshComponentTag = FName(TEXT("WeaponTrace"));

	/** ★ 本碰撞窗口内同时生效的武器轨迹区域；运行时只接受非空配置，为空则干净失败。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trace")
	TArray<FWeaponTraceRegion> TraceRegions;

	/** 碰撞通道 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TEnumAsByte<ECollisionChannel> CollisionChannel = ECC_GameTraceChannel1;

	/** 限定碰撞仅检测带此 Tag 的组件 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag HitzoneQueryTag;

	/** PIE 中绘制本段 Sweep，便于校准 Socket 和半径 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bDrawDebug = false;
};

/**
 * FAttackDamageConfig — 单段伤害配置
 */
USTRUCT(BlueprintType)
struct FAttackDamageConfig
{
	GENERATED_BODY()

	/** 本段物理伤害类型；不从表现用 CueTag 猜测斩/打/弹。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EMHGZPhysicalDamageType PhysicalDamageType = EMHGZPhysicalDamageType::Sever;

	/** 属性动作倍率，默认1；与物理 MotionValue 独立。0可禁用本段属性伤害。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FScalableFloat ElementMotionValue = 1.0f;

	/** 本段额外物理修正，默认1；不重复填已由 AttackPower GE 处理的攻击力加成。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FScalableFloat PhysicalDamageMultiplier = 1.0f;

	/** 本段额外属性修正，默认1。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FScalableFloat ElementDamageMultiplier = 1.0f;

	/** 伤害 GE 蓝图 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSubclassOf<class UGameplayEffect> DamageEffectClass;

	/** ★ 原始动作值百分数：10 表示动作值10。伤害段须为正值；0 表示合法无伤害命中。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FScalableFloat MotionValue = 0.0f;

	/** ★ 基础破坏值 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FScalableFloat BaseStaggerValue = 0.f;

	/** 击退方向（相对攻击者朝向） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float KnockbackAngle = 0.f;

	/** 击退力度 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FScalableFloat KnockbackForce = 0.f;

	/** 硬直等级 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag HitStaggerTag;

	/** 是否按命中部位防御力修正伤害 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bUseHitzoneDefense = true;

	/** 招式内空挥截断 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bRequiresHitToContinue = false;

	/** 命中时对自身施加的 GE */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSubclassOf<UGameplayEffect> OnHitSelfEffect;

	/** 物理命中 GameplayCue Tag */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag HitCueTag;

	/** 元素附魔命中 GC 标签 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag ElementalCueTag;

	/** 震屏类 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSubclassOf<UCameraShakeBase> CameraShakeClass;

	/** 震屏强度倍率 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float CameraShakeScale = 0.f;

	/** 卡肉基础时长（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FScalableFloat HitStopBase = 0.f;

	/** 招式挥刀风声 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<USoundBase> SwingSound;
};

/**
 * FAttackSegmentConfig — 单段攻击配置（碰撞 + 伤害 + 多跳）
 */
USTRUCT(BlueprintType)
struct FAttackSegmentConfig
{
	GENERATED_BODY()

	/** 碰撞参数 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FAttackCollisionConfig Collision;

	/** 伤害参数 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FAttackDamageConfig Damage;

	/** ★ 单次碰撞产生的伤害跳数 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 MultiHitCount = 1;

	/** 默认只结算本次真实 Sweep 接触；LockedTargetTicks 必须逐跳重验距离。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EAttackMultiHitPolicy MultiHitPolicy = EAttackMultiHitPolicy::ContactOnly;

	/** 多次伤害之间的间隔（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float MultiHitInterval = 0.1f;

	/** LockedTargetTicks 每跳允许的最大目标距离。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1.0", EditCondition = "MultiHitPolicy == EAttackMultiHitPolicy::LockedTargetTicks"))
	float LockedTargetMaxDistance = 300.0f;

	/**
	 * 为特殊动作预留的段内 MotionWarping 旋转上限。通用攻击没有消费者；
	 * 默认 0，避免未实现 Warp 的攻击被误配为已启用。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float MaxWarpAngle = 0.0f;
};

/**
 * UMHGZAttackAbility — 攻击 Ability 中间层
 * 统一封装所有攻击类 Ability 的碰撞检测、命中过滤、伤害 GE 构造与 Apply
 */
UCLASS(BlueprintType, Blueprintable, Abstract)
class UMHGZAttackAbility : public UMHGZGameplayAbility
{
	GENERATED_BODY()

public:
	UMHGZAttackAbility();

	/** 本次 GA 激活生成的稳定攻击身份；一次激活内所有伤害 Spec 共享。 */
	const FGuid& GetActivationAttackInstanceID() const { return ActivationAttackInstanceID; }

	// ═══════════════════════════════════════════
	// 配置
	// ═══════════════════════════════════════════

	/** ★ 多段攻击配置 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack")
	TArray<FAttackSegmentConfig> AttackSegments;

	/** 攻击 Montage */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack")
	TObjectPtr<UAnimMontage> AttackMontage;

	/** Whether this attack suppresses locomotion input while its action is active. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability|Movement")
	bool bBlockCharacterMovementDuringAction = true;

	/**
	 * 没有更具体入口时的 Montage Section。None 表示从 Montage 开头播放。
	 * 入口 Section 属于目标攻击 Ability，而不是来源攻击的尾段。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Entry Section")
	FName DefaultEntrySection;

	/**
	 * 按 Combo TransitionID 选择入口。键存在即表示必须使用有效、非空的
	 * Montage Section；其优先级高于 SourceState 和 DefaultEntrySection。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Entry Section")
	TMap<FName, FName> EntrySectionByTransitionID;

	/**
	 * 按来源 Combo State 选择入口，仅在没有 TransitionID 专属映射时使用。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Entry Section")
	TMap<FName, FName> EntrySectionBySourceState;

	// ═══════════════════════════════════════════
	// 覆写
	// ═══════════════════════════════════════════

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

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

	// ═══════════════════════════════════════════
	// 碰撞窗口控制（由 AnimNotifyState 调用）
	// ═══════════════════════════════════════════

	/** 开启碰撞检测 */
	UFUNCTION(BlueprintCallable, Category = "MHGZ|Attack")
	void EnableCollision(int32 SegmentIndex = 0);

	/** 关闭指定段；INDEX_NONE 表示关闭本 Ability 的全部碰撞窗口。 */
	UFUNCTION(BlueprintCallable, Category = "MHGZ|Attack")
	void DisableCollision(int32 SegmentIndex = -1);

	/** 碰撞窗口内由 AnimNotifyState::NotifyTick 每帧调用 */
	void TickCollision(int32 SegmentIndex, float DeltaSeconds);

	/** Attack-side exact window that permits this action to be superseded by Dodge. */
	bool BeginDodgeAcceptWindow(FName NotifyEventID);
	void EndDodgeAcceptWindow(FName NotifyEventID);
	bool HasOpenDodgeAcceptWindow(const FWeaponActionToken& ActionToken) const;
	bool PrepareDodgeSupersede(const FWeaponActionToken& DodgeActionToken);
	bool CommitDodgeSupersede(const FWeaponActionToken& DodgeActionToken);
	void CancelDodgeSupersede(const FWeaponActionToken& DodgeActionToken);

	// ═══════════════════════════════════════════
	// 伤害构造与 Apply
	// ═══════════════════════════════════════════

	/** 构造伤害 GE Spec：写满真实 HitResult/攻击身份/反馈参数。 */
	virtual FGameplayEffectSpecHandle MakeDamageSpec(
		const FHitResult& Hit, int32 SegmentIndex);

	/** Apply 伤害到目标；返回GE是否成功提交，真实 Sweep HitResult 全程不降级。 */
	bool ApplyDamage(const FHitResult& Hit, int32 SegmentIndex);

	// ═══════════════════════════════════════════
	// 命中/派生判断
	// ═══════════════════════════════════════════

	/** 当前碰撞窗口命中后，是否继续下一段 */
	UFUNCTION(BlueprintNativeEvent, Category = "MHGZ|Attack")
	bool ShouldContinueAfterHit() const;
	virtual bool ShouldContinueAfterHit_Implementation() const;

protected:
	virtual bool ValidateActionDependencies() const override;

	/**
	 * Per-action activation direction policy. Ordinary attacks correct to the
	 * frozen input direction; late-aim actions may override this without
	 * changing the shared attack activation path.
	 */
	virtual void ApplyActivationDirectionCorrection();

	/** Runs after ActionToken registration and before the attack montage starts. */
	virtual bool PrepareAttackMontage() { return true; }

	/**
	 * 根据 Coordinator 冻结的 ActivationContext 选择并验证播放入口。
	 * 返回 false 表示配置非法；调用方必须在播放 Montage 前取消，不能回退到错误开头。
	 */
	bool SelectAttackMontageStartSection(FName& OutStartSection) const;
	bool SelectAttackMontageStartSection(const FWeaponAbilityActivationContext& Context,
		FName& OutStartSection) const;

	/** Resolves a Combo edge's per-play Blend In override, or the asset default. */
	float ResolveAttackMontageBlendInTime(
		const FWeaponAbilityActivationContext& Context) const;

	/** Resolves a Combo edge's activation yaw cap, or this GA's default cap. */
	float ResolveActivationMaxCorrectionAngle(
		const FWeaponAbilityActivationContext& Context) const;
	/** Apply the shared capped, immediate activation yaw correction to an explicit world direction. */
	bool ApplyActivationDirectionCorrectionFromWorldDirection(
		const FVector& WorldDirection, float CorrectionAngleDegrees);

	/**
	 * 开始已选入口的攻击 Montage，并登记精确 MontageInstanceID。
	 * 测试子类可覆写此边界以验证 StartSection，而无需异步 AnimInstance。
	 */
	virtual bool StartAttackMontage(ACharacter& Character, UAnimMontage* Montage,
		FName StartSection);

	/**
	 * 在 Action Confirm 后、Montage 开始前，按冻结输入一次性修正 Actor Yaw。
	 * 普通攻击不创建 Motion Warping Target；特殊动作需要 Warping 时自行显式实现。
	 */
	void ApplyDirectionCorrection();

public:
	/**
	 * 精确 Notify 回调：只在当前已 Confirm 的 Action 上读取一次摇杆方向，
	 * 按角度上限决定目标修正量；Duration 为 0 时瞬时完成，正值时按指定速度/缓动插值。
	 * 返回 true 表示修正请求已被接受（插值模式尚未必在当前帧改变朝向）。
	 */
	bool ApplyInActionDirectionCorrection(const FWeaponActionToken& ActionToken,
		float MaxCorrectionAngleOverride = -1.0f,
		float CorrectionDuration = 0.0f,
		float MaxYawSpeedDegreesPerSecond = 0.0f,
		EActionDirectionCorrectionEasing Easing = EActionDirectionCorrectionEasing::Linear);

	/**
	 * Same correction task/ownership path, but the target comes from the exact
	 * immediately preceding game-frame snapshot (N-1). Missing, stale-frame,
	 * neutral, stale-runtime, or other-Pawn snapshots are rejected; this never
	 * falls back to live/last-nonzero input.
	 */
	bool ApplyInActionDirectionCorrectionFromSnapshot(
		const FWeaponActionToken& ActionToken,
		const FActionMoveInputFrameSnapshot& InputSnapshot,
		float MaxCorrectionAngleOverride = -1.0f,
		float CorrectionDuration = 0.0f,
		float MaxYawSpeedDegreesPerSecond = 0.0f,
		EActionDirectionCorrectionEasing Easing = EActionDirectionCorrectionEasing::Linear);

protected:
	/** 通知协调器首次命中 */
	void NotifyCoordinatorFirstHit();

	/**
	 * 翻滚已准备接管、尚未提交：其 Montage_Play 会同步打断旧 Montage。
	 * 自建 MontageTask 的子类也必须暂缓该中断，并保留任务供提交/回滚清理。
	 * 这不是新的取消窗口；许可仍由精确 DodgeAcceptWindow 和 ActionToken 决定。
	 */
	bool HasPreparedDodgeSupersede() const;

	/** 当前招内修正可读取的实时世界方向；生产实现来自 Character 的原始摇杆缓存。 */
	virtual FVector GetInActionCorrectionDirection() const;

	/**
	 * 强制反应转入后续 MovementTask 前停止本攻击的 Montage，但保留当前 Action
	 * 身份与已持有的锁，直到后续任务自行结束。仅限精确拥有本 ActionToken 的派生
	 * Ability 使用；它不发布 MM Handoff，也不允许来源攻击恢复播放。
	 */
	bool SuspendAttackMontageForFollowup();
	bool ApplyInActionDirectionCorrectionWithDirection(
		const FWeaponActionToken& ActionToken, const FVector& WorldDirection,
		float MaxCorrectionAngleOverride, float CorrectionDuration,
		float MaxYawSpeedDegreesPerSecond, EActionDirectionCorrectionEasing Easing);
	/**
	 * Ease toward an already validated absolute target yaw. Special actions use this
	 * when their target was clamped against a frozen reference yaw and must not be
	 * clamped a second time against a later Montage-root-rotated actor yaw.
	 */
	bool ApplyInActionDirectionCorrectionToYaw(
		const FWeaponActionToken& ActionToken, float TargetYaw,
		float CorrectionDuration,
		float MaxYawSpeedDegreesPerSecond = 0.0f,
		EActionDirectionCorrectionEasing Easing = EActionDirectionCorrectionEasing::Linear);

#if WITH_DEV_AUTOMATION_TESTS
	UAbilityTask_MHGZActionDirectionCorrection* GetActiveDirectionCorrectionTaskForTest() const
	{
		return ActiveDirectionCorrectionTask.Get();
	}
#endif

	/**
	 * 每一次 Damage GE 成功提交后的扩展点。真实 HitResult 与段索引保持原样；
	 * 派生招式可以在这里接入命中后的专属资源效果，不能在 Sweep 发现接触时提前结算。
	 */
	virtual void HandleSuccessfulAttackDamage(const FHitResult& Hit, int32 SegmentIndex) {}

	/** Special attacks may gate a configured collision segment by action ownership/phase. */
	virtual bool CanApplyCollisionHit(const FHitResult& Hit, int32 SegmentIndex) const
	{
		return true;
	}

	/** Preserve legacy count-on-contact behavior unless an action opts into retrying failed GEs. */
	virtual bool ShouldConsumeCollisionOnDamageFailure(int32 SegmentIndex) const
	{
		return true;
	}

	/** Notifies a derived action after its exact AttackCollision window is removed. */
	virtual void OnAttackCollisionWindowClosed(int32 SegmentIndex) {}

	/** Shared resolver for derived-action audits; use exactly the gameplay trace mesh. */
	USkeletalMeshComponent* FindTraceMeshComponent(const FAttackCollisionConfig& Collision) const;

	/** Montage task completion hook for attacks with multi-phase lifetimes. */
	UFUNCTION()
	virtual void OnMontageCompleted();

	// ═══════════════════════════════════════════
	// 运行时状态
	// ═══════════════════════════════════════════

	/** 最近开始/结束判定的段索引，仅供兼容 ShouldContinueAfterHit。 */
	int32 CurrentSegmentIndex = 0;

	/** 本次 GA 激活后是否已有命中 */
	bool bHasHitThisActivation = false;

	/** 本次激活的稳定攻击身份（ActivateAbility 时生成）。 */
	FGuid ActivationAttackInstanceID;

	/** 是否有活跃的 RootMotion Task */
	bool bHasActiveRootMotionTask = false;

	/** 当前播放的攻击 Montage */
	UPROPERTY()
	TObjectPtr<UAnimMontage> ActiveAttackMontage;

private:
	struct FTraceRegionRuntimeState
	{
		FName StartSocketName;
		FName EndSocketName;
		float Radius = 14.0f;
		FCollisionShape QueryShape;
		float MaxSampleSpacing = 20.0f;
		float MaxAngularStepDegrees = 15.0f;
		int32 MaxSampleCount = 16;
		FVector PreviousStart = FVector::ZeroVector;
		FVector PreviousEnd = FVector::ZeroVector;
	};

	/** 单目标多跳运行时状态：ContactOnly 与 LockedTargetTicks 共用。 */
	struct FHitTargetRuntimeState
	{
		TWeakObjectPtr<AActor> TargetActor;
		TWeakObjectPtr<UMHGZMonsterHitzoneComponent> Hitzone;
		FHitResult LastHit;
		int32 HitCount = 0;
		float LastHitTime = -1.f;
		FTimerHandle TickTimer;
	};

	struct FCollisionWindowRuntimeState
	{
		TArray<FTraceRegionRuntimeState> Regions;
		TMap<TWeakObjectPtr<AActor>, FHitTargetRuntimeState> HitTargets;
	};

	/** GAS Montage 任务——统一处理完成、取消和被其他 Montage 打断 */
	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> MontageTask;
	TWeakObjectPtr<UAbilityTask_MHGZActionDirectionCorrection> ActiveDirectionCorrectionTask;
	bool StartInActionDirectionCorrection(const FWeaponActionToken& ActionToken,
		ACharacter& Character, float DeltaYaw, float CorrectionDuration,
		float MaxYawSpeedDegreesPerSecond, EActionDirectionCorrectionEasing Easing);

	/** 每个 ConfigIndex 独立保存轨迹、命中和多跳状态，允许窗口重叠。 */
	TMap<int32, FCollisionWindowRuntimeState> ActiveCollisionWindows;

	/** NotifyEventID -> exact ledger token; the notify asset owns no mutable state. */
	TMap<FName, FWeaponOwnedTagToken> DodgeAcceptWindowTokens;
	FWeaponActionToken PendingDodgeSuperseder;
	bool bMontageSuspendedForFollowup = false;

	/** 执行指定段的一次 Socket Sweep 检测。 */
	void PerformSweepCheck(int32 SegmentIndex);

	/** 处理本帧为某个怪物选出的最早 Sweep 命中。 */
	void ProcessSweepHit(const FHitResult& Hit, int32 SegmentIndex);

	/** LockedTargetTicks：首次命中后为该目标启动独立跳伤 Timer。 */
	void StartLockedTargetTickTimer(int32 SegmentIndex, const TWeakObjectPtr<AActor>& TargetKey);

	/** LockedTargetTicks：单目标逐跳重验并结算一次伤害。 */
	void OnLockedTargetTick(int32 SegmentIndex, TWeakObjectPtr<AActor> TargetKey);

	/** 次数耗尽：只停该目标 Timer，保留状态条目用于去重。 */
	void FinishLockedTarget(int32 SegmentIndex, const TWeakObjectPtr<AActor>& TargetKey);

	/** 停止并移除该目标（离区/失效），同时清其 Timer；再次接触按新目标重新计数。 */
	void StopLockedTarget(int32 SegmentIndex, const TWeakObjectPtr<AActor>& TargetKey);

	/** 目标/部位存活校验（ASC 死亡 Tag 或 Health<=0 视为不存活）。 */
	bool IsTargetAlive(AActor* Target) const;

	/** 检查 Mesh 是否包含当前配置要求的所有轨迹 Socket/骨骼。 */
	bool HasRequiredTraceSockets(const USkeletalMeshComponent* Mesh,
		const FAttackCollisionConfig& Collision) const;

	UFUNCTION()
	void OnMontageInterrupted();

	void CloseAllDodgeAcceptWindows();

	bool bIsEndingAbility = false;
};
