// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MHGZInsectGlaiveAbility.h"
#include "MHGZBackVaultAbility.generated.h"

class UAbilityTask_MHGZPlayMontageAndWait;
class UAbilityTask_WaitDelay;
class UAbilityTask_MHGZWeaponMovement;
class UAnimMontage;
class UAnimSequenceBase;
class UCurveVector;

/** One point from the recorded local back-vault trajectory. */
USTRUCT(BlueprintType)
struct FBackVaultTrajectoryKey
{
	GENERATED_BODY()

	/** Normalized movement time, in the inclusive [0, 1] range. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Back Vault")
	float Time = 0.0f;

	/**
	 * X = travel progress, Y = lateral offset divided by total travel distance,
	 * Z = height divided by measured apex height.  The full recording ends at
	 * ground level; the ability samples only its action-owned prefix, then CMC
	 * continues from that non-zero-height handoff.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Back Vault")
	FVector NormalizedPosition = FVector::ZeroVector;
};

/**
 * 在 JumpOver 段内从 clip 自己的根骨骼轨道回收的**前向**位移。
 *
 * clip 的根骨骼在整段里偏离 ref pose，而该序列 `bEnableRootMotion` 与
 * `bForceRootLock` 都为假 —— 于是轨迹**既不提取也不重置**，那 23 cm 前向位移
 * 只活在姿势里，换坠落正片时被丢掉。现在给序列打开 `bForceRootLock` 把姿势
 * 前导消掉（否则它会以「一帧弹回」的形式重现），再由本表把同量的**前向**位移
 * 补回胶囊。
 *
 * 只补前向：Rise 实录用朝向分解后，侧向是「去—回—归零」的摆动，净位移为零，
 * 而曲线自己的横向已经就是这个形状；再叠姿势那份侧移，可见轨迹就回不了零。
 */
USTRUCT(BlueprintType)
struct FBackVaultClipDriftKey
{
	GENERATED_BODY()

	/** 在 CurvedVault 窗口内的归一化时间，闭区间 [0, 1]。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Back Vault")
	float CurveTime = 0.0f;

	/** 已回收的前向位移 ÷ TotalDistance。末端必须平（斜率为零）。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Back Vault")
	float ForwardFraction = 0.0f;
};

/**
 * 一条序列的根轨道该处于什么状态。
 *
 * 两个极端都是错，且后果不同：既不锁也不提取会让位移静默地留在姿势里（A9）；
 * 既锁又提取则让动画根运动抢占 CurvedVault 的 RootMotionSource
 * （`CharacterMovementComponent.cpp` 里 `HasAnimRootMotion()` 直接 return），
 * 整条解析轨迹连同交棒切线一起失效。
 */
enum class EBackVaultRootTrackPolicy : uint8
{
	/** 既不提取也不重置：位移只活在姿势里，换正片时弹回。 */
	Unaccounted,
	/** 锁在 ref pose、不提取 —— JumpOver 段唯一可接受的状态。 */
	LockedNotExtracted,
	/** 提取、不锁 —— Jump 段的状态（该段没有 RootMotionSource 在跑）。 */
	Extracted,
	/** 既锁又提取：提取会抢占 CurvedVault，比 Unaccounted 更糟。 */
	Conflicting,
};

/**
 * RT+A 后撑杆跳。
 *
 * 这是一个完整的地面动作取消：Combo 边只会在来源攻击的精确
 * DodgeAcceptWindow 内命中；Ability 根据白灯选择姿势序列和实录轨迹。动画
 * 的 Jump 段保留导入的 Montage Root Motion；到 Jump_Over_Back 的精确段边界才
 * 由 CurvedVault RootMotionSource 接管。随后保留末端速度并交由 CMC 自由下落，
 * 避免有根运动的 Jump_Back 与无可靠根运动的 Jump_Over_Back 产生位移所有权断层。
 */
UCLASS(BlueprintType, Blueprintable)
class MHGZ_API UMHGZBackVaultAbility : public UMHGZInsectGlaiveAbility
{
	GENERATED_BODY()

public:
	UMHGZBackVaultAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility, bool bWasCancelled) override;

	/**
	 * Analytic tangent of a recorded back-vault trajectory at HandoffProgress,
	 * expressed in the path's facing frame (X forward, Y right, Z up) in cm/s.
	 *
	 * This is the value the free-fall hand-off must carry.  Reading it back from
	 * CMC->Velocity instead is what silently cost the fall two thirds of its
	 * vertical speed, because the CMC removes a finished MoveToForce source on
	 * its own movement update and the velocity has already collapsed to the
	 * source's final partial-frame remainder by then.
	 *
	 * Static and free of editor state so the hand-off contract is directly
	 * testable without a world or a live ability.
	 */
	static bool ComputeTrajectoryTangent(const TArray<FBackVaultTrajectoryKey>& Keys,
		float StartProgress, float HandoffProgress, float TotalDistance,
		float ApexHeight, float CurveDuration, FVector& OutVelocityLocal);

	/**
	 * 由序列的两个根运动开关判定它处于哪种状态。纯函数，便于直接测试。
	 */
	static EBackVaultRootTrackPolicy ResolveRootTrackPolicy(
		bool bEnableRootMotion, bool bForceRootLock);

	/**
	 * 在 CurvedVault 窗口的归一化时间处采样已回收的前向位移（÷ TotalDistance）。
	 *
	 * 表为空时返回 0（合法：没有漂移要补）。形状非法 —— 首键非零、末键不为 1、
	 * 时间不单调、含 NaN —— 返回 false，让调用方像其他退化输入一样拒绝这次移动。
	 */
	static bool SampleClipDriftForwardFraction(
		const TArray<FBackVaultClipDriftKey>& DriftKeys, float CurveTime,
		float& OutFraction);

	/** The normal (no-white-extract) visual chain used by the fallback montage builder. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Back Vault|Animation")
	TSoftObjectPtr<UAnimSequenceBase> BackJumpSequence;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Back Vault|Animation")
	TSoftObjectPtr<UAnimSequenceBase> BackJumpOverSequence;

	/** White-extract visual chain; its air phase is longer and higher. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Back Vault|Animation")
	TSoftObjectPtr<UAnimSequenceBase> WhiteBackJumpSequence;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Back Vault|Animation")
	TSoftObjectPtr<UAnimSequenceBase> WhiteBackJumpOverSequence;

	/** Authored White Extract counterpart of AttackMontage. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Back Vault|Animation")
	TObjectPtr<UAnimMontage> WhiteAttackMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Back Vault|Animation")
	FName BackVaultMontageSlot = FName(TEXT("DefaultSlot"));

	/** Natural effective durations of the ordinary Jmp and Jump_Over source sequences. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Back Vault|Animation", meta = (ForceUnits = "s"))
	float BackJumpDuration = 0.650f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Back Vault|Animation", meta = (ForceUnits = "s"))
	float BackJumpOverDuration = 1.092f;

	/** Natural effective durations of the White Extract Jmp and Jump_Over source sequences. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Back Vault|Animation", meta = (ForceUnits = "s"))
	float WhiteBackJumpDuration = 0.650f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Back Vault|Animation", meta = (ForceUnits = "s"))
	float WhiteBackJumpOverDuration = 1.467f;

	/** Directly sampled from the no-white MHR capture; not an assumed parabola. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Back Vault|Recorded Path")
	TArray<FBackVaultTrajectoryKey> BackVaultTrajectory;

	/** Directly sampled from the white-extract MHR capture. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Back Vault|Recorded Path")
	TArray<FBackVaultTrajectoryKey> WhiteBackVaultTrajectory;

	/** 非白灯 JumpOver clip 的根轨道前向漂移回收表。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Back Vault|Recorded Path")
	TArray<FBackVaultClipDriftKey> BackVaultClipDrift;

	/** 白灯是另一条 clip（3.1667 s vs 2.1667 s），漂移量必须单独测。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Back Vault|Recorded Path")
	TArray<FBackVaultClipDriftKey> WhiteBackVaultClipDrift;

protected:
	virtual bool ValidateActionDependencies() const override;
	virtual bool PrepareAttackMontage() override;
	virtual bool StartAttackMontage(ACharacter& Character, UAnimMontage* Montage,
		FName StartSection) override;

private:
	bool BuildBackVaultMontage();
	/**
	 * Makes the first (Montage-root-motion-owned) Jump segment airborne before
	 * its first root-motion delta is extracted.  The matching restore path is
	 * deliberately owned by this GA rather than by the later CurvedVault task:
	 * the ability can be interrupted before that task exists.
	 */
	bool BeginBackVaultInitialFlight(ACharacter& Character);
	/** Restore the pre-vault locomotion mode after an interrupted flight phase. */
	void RestoreBackVaultInitialFlight(ACharacter& Character);
	/** Schedules the exact Jump -> JumpOver ownership boundary. */
	bool ScheduleJumpOverMovementHandoff();
	/** Runs at the JumpOver section boundary and gives its physical path to CurvedVault. */
	bool StartBackVaultMovement();
	/**
	 * Rebuilds the action-owned prefix of the recorded path as a MoveToForce
	 * offset curve.
	 *
	 * OutHandoffVelocityLocal receives the analytic tangent of that same path at
	 * FreeFallHandoffProgress, in the path's facing frame (X forward, Y right,
	 * Z up) and in cm/s.  Sampling it from the recorded keys is what makes the
	 * free-fall hand-off independent of the frame on which the movement task
	 * happens to observe the root-motion source completing.
	 */
	UCurveVector* BuildTrajectoryCurve(const TArray<FBackVaultTrajectoryKey>& Keys,
		const TArray<FBackVaultClipDriftKey>& DriftKeys,
		float StartProgress,
		float FreeFallHandoffProgress, float TotalDistance, float ApexHeight,
		float CurveDuration, float& OutHandoffDistance,
		FVector& OutHandoffVelocityLocal) const;
	void TryFinishBackVault();

	UFUNCTION()
	void HandleBackVaultVisualCompleted();

	UFUNCTION()
	void HandleBackVaultVisualInterrupted();

	UFUNCTION()
	void HandleJumpOverMovementHandoff();

	UFUNCTION()
	void HandleBackVaultMovementFinished(const FWeaponMovementResult& MovementResult);

	UPROPERTY()
	TObjectPtr<UAbilityTask_MHGZPlayMontageAndWait> BackVaultVisualTask;

	/** Keeps the section-boundary delay inside the ability lifecycle. */
	UPROPERTY()
	TObjectPtr<UAbilityTask_WaitDelay> JumpOverHandoffTask;

	UPROPERTY()
	TObjectPtr<UAbilityTask_MHGZWeaponMovement> BackVaultMovementTask;

	/** Keeps the runtime curve alive for the exact lifetime of the movement task. */
	UPROPERTY(Transient)
	TObjectPtr<UCurveVector> ActiveTrajectoryCurve;

	bool bUseWhiteBackVault = false;
	/** Balanced explicitly after Jump has handed movement ownership to CurvedVault. */
	bool bBackVaultVisualRootMotionDisabled = false;
	bool bVisualFinished = false;
	bool bMovementFinished = false;
	/** Set only after an unobstructed action phase has released movement while still airborne. */
	bool bBeginFreeFallAfterEnd = false;
	/** True from the first Jump frame until CurvedVault hands the action to Falling, or cleanup restores it. */
	bool bBackVaultInitialFlightOwned = false;
	/** Movement state to restore if the GA ends before its intentional Falling hand-off. */
	uint8 PreBackVaultMovementMode = 0;
	uint8 PreBackVaultCustomMovementMode = 0;
	bool bIsEndingBackVault = false;
};
