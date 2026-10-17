// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActionSystem/MHGZInsectGlaiveAbility.h"
#include "WeaponRuntime/MHGZWeaponRuntimeTypes.h"
#include "MHGZKinsectPiercerAbility.generated.h"

class UAbilityTask_MHGZPlayMontageAndWait;
class UAbilityTask_MHGZWeaponMovement;
class UAnimMontage;
class UAnimInstance;
class UCharacterMovementComponent;
class URes_InsectGlaive;
class USkeletalMeshComponent;

/**
 * 本招的运动相位。
 *
 * `202` **不在这里** —— 它是 201 的循环表现（主 Montage 的 Section 自循环），
 * 不是第四个游戏运动段。运动相位自始至终只有三个。
 */
UENUM(BlueprintType)
enum class EKinsectPiercerPhase : uint8
{
	Inactive,
	Start200,
	Dive201,
	Landing203,
	Ended
};

/** Per-sample diagnostics exposed to the existing opt-in RuntimeTelemetry writer. */
struct FMHGZKinsectPiercerTelemetrySnapshot
{
	bool bAbilityActive = false;
	uint32 ActivationSequenceID = 0;
	int32 Phase = static_cast<int32>(EKinsectPiercerPhase::Inactive);
	FVector LockedYawDirection = FVector::ForwardVector;
	float CurveElapsedSeconds = 0.0f;
	float CurveCoveredSeconds = 0.0f;
	uint64 MovementLease = 0;
	bool bRecordedTrajectoryActive = false;
	bool bActionMovementOwned = false;
	bool bAerialAttackBudgetSpent = false;
	int32 DanceSource = 0;
	bool bKinsectSlashSourceTagPresent = false;
	int32 MainMontageInstanceID = INDEX_NONE;
	int32 LandingMontageInstanceID = INDEX_NONE;
	float LandingMontageTimeSeconds = 0.0f;
	bool bLandingRootMotionOwned = false;
	bool bLandingRootMotionModeCaptured = false;
	int32 AnimRootMotionMode = INDEX_NONE;
	int32 SegmentConfigCount = 0;
	int32 LastSuccessfulConfigIndex = INDEX_NONE;
	int32 SuccessfulTargetCounts[3] = { 0, 0, 0 };
};

/**
 * 强化操虫穿刺（Enhanced Kinsect Piercer）—— M5 本招的生产 GA。
 *
 * 一次 Commit 的 Action 跨越**两条**常驻 Montage：主 Montage 跑
 * `Start200 → Dive201 → DiveLoop201`（自循环到真实触地），`_Over` 跑 `Landing203`。
 * ActionToken、攻击激活 GUID、激活级命中额度、动作锁全程只有一份。
 *
 * 三件事和项目其它动作不同，写在这里备查：
 *
 * 1. **200/201 用录制曲线，203 用 Montage Root Motion。** 真实触地后先归还
 *    RecordedLocalTrajectory 租约再取得 Montage 根位移所有权；绝不叠加两套驱动。
 * 2. **方向只来自冻结的准心水平朝向。** 激活时锁一次，之后镜头、目标、摇杆都不再
 *    参与。镜头俯仰不进入曲线、距离、升高或下落速度（§9.2 的固定合同）。
 * 3. **触地是事件，不是计时。** 201 的覆盖时长是被录制时的触地截断的，飞得更高就会
 *    走出覆盖；那一段按同模型续行，直到真实 `ProcessLanded` 或独立看门狗。
 *    周期性定时器**不是** 201→203 的出口。
 *
 * 两条 Montage 都是命令行写出的常驻资产（见实现计划 §3.2）。任一引用为空即拒绝激活，
 * 与操虫斩 `HasRequiredDanceMontage()`、空回 `AttackMontage` 为空直接拒绝同一条规则；
 * 这里**没有**运行时现拼兜底。
 */
UCLASS(BlueprintType, Blueprintable)
class MHGZ_API UMHGZKinsectPiercerAbility : public UMHGZInsectGlaiveAbility
{
	GENERATED_BODY()

public:
	UMHGZKinsectPiercerAbility();

	virtual bool CanActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags,
		const FGameplayTagContainer* TargetTags,
		FGameplayTagContainer* OptionalRelevantTags) const override;

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility, bool bWasCancelled) override;

	UFUNCTION(BlueprintPure, Category = "MHGZ|KinsectPiercer")
	EKinsectPiercerPhase GetPiercerPhase() const { return Phase; }

	/** 激活时冻结的准心水平朝向；镜头俯仰从不进入它。 */
	UFUNCTION(BlueprintPure, Category = "MHGZ|KinsectPiercer")
	FVector GetLockedYawDirection() const { return LockedYawDirection; }

	/** Read-only snapshot; called only by the opt-in AnimInstance telemetry sampler. */
	void GetRuntimeTelemetrySnapshot(FMHGZKinsectPiercerTelemetrySnapshot& OutSnapshot) const;

	/** 该段在**本次激活内**是否已经成功伤害过这个目标（跨部位、跨两条 Montage 都算）。 */
	bool HasDamagedTarget(int32 SegmentIndex, const AActor* Target) const;

	/** 自动化专用：不建资产也能测「同一目标每段只结算一次」。 */
	void SetPhaseForTest(EKinsectPiercerPhase InPhase) { Phase = InPhase; }
	void SetLockedYawDirectionForTest(const FVector& InDirection)
	{
		LockedYawDirection = FVector(InDirection.X, InDirection.Y, 0.0).GetSafeNormal();
	}
	void RecordDamagedTargetForTest(int32 SegmentIndex, const AActor* Target)
	{
		RecordDamagedTarget(SegmentIndex, Target);
	}

	// ── 主 Montage 的 Section 名 ─────────────────────────────────────────
	//
	// 最后一个 Section 必须**自接**（`NextSectionName == DiveLoop201`），循环次数由真实
	// 触地时刻决定，不烘成有限循环数。这是项目里第一次用 Section 自循环，M3 生成后
	// 必须回读验证，E1 目视确认它真的在循环。

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect Piercer|Montage Sections")
	FName StartSectionName = TEXT("Start200");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect Piercer|Montage Sections")
	FName DiveSectionName = TEXT("Dive201");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect Piercer|Montage Sections")
	FName DiveLoopSectionName = TEXT("DiveLoop201");

	// ── 落地 Montage ────────────────────────────────────────────────────

	/** 203 的独立落地表现；与主 Montage 是两个常驻资产。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect Piercer|Montage")
	TObjectPtr<UAnimMontage> LandingMontage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect Piercer|Montage Sections")
	FName LandingSectionName = TEXT("Landing203");

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

protected:
	virtual bool ValidateActionDependencies() const override;

	/** 整个 GA 的所有武器伤害段共用出招时的舞踏倍率，包括203地面段。 */
	virtual FGameplayEffectSpecHandle MakeDamageSpec(
		const FHitResult& Hit, int32 SegmentIndex) override;

	virtual bool PrepareAttackMontage() override;

	/**
	 * 自建两条 MontageTask，**不追加**基类默认终止绑定。
	 *
	 * 基类的 `StartAttackMontage` 把 `OnInterrupted/OnCancelled` 接到
	 * `UMHGZAttackAbility::OnMontageInterrupted()`，而它默认会
	 * `RequestEndAction(Interrupted)` —— 201→203 的换段会同步触发旧中断，
	 * 那条默认绑定会在落地那一刻把整个 GA 收掉。详见实现计划 §6.4。
	 */
	virtual bool StartAttackMontage(ACharacter& Character, UAnimMontage* Montage,
		FName StartSection) override;

	/**
	 * 方向来自冻结的准心射线而不是摇杆，所以不在基类的入口再做一次摇杆修正；
	 * 真正的转向与锁定在 `ActivateAbility` 里一次做完。
	 */
	virtual void ApplyActivationDirectionCorrection() override;

	virtual void HandleSuccessfulAttackDamage(const FHitResult& Hit,
		int32 SegmentIndex) override;

	virtual bool CanApplyCollisionHit(const FHitResult& Hit,
		int32 SegmentIndex) const override;

	/** 基类的完成绑定不会被本招创建；保留一个明确的空覆写，防止将来被误接。 */
	virtual void OnMontageCompleted() override;

	virtual void HandleLanded(const FHitResult& Hit) override;

private:
	#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
	friend class FMHGZKM5KinsectPiercerTest;
	friend class FMHGZIGDanceDamageTest;
	friend class FMHGZM5LandingDodgeHandoffTestAccess;
	#endif

	// ── 身份 ──
	bool IsCurrentAction(const FWeaponActionToken& ActionToken) const;

	// ── 配置复核 ──
	bool HasRequiredSections(const UAnimMontage* Montage) const;
	bool HasRequiredLandingMontage() const;
	/** 空中两份 profile 必须有效；203 的旧 profile 仅保留为录制对照。 */
	bool HasUsableTrajectoryProfiles() const;
	const URes_InsectGlaive* GetUsableResource() const;

	// ── 锁向 ──
	/** §5.1：取输入快照里冻结的准心射线，只保留水平分量；零向量回退 ActorForward。 */
	FVector ResolveLockedYawDirection() const;
	bool ApplyLockedYaw();

	// ── 相位 ──
	bool StartPhaseMovement(EKinsectPiercerPhase InPhase);
	FWeaponMovementRequest MakePhaseMovementRequest(EKinsectPiercerPhase InPhase) const;
	bool AdvanceToDive();
	bool BeginLandingSwitch(const FHitResult& Hit);
	bool PlayLandingMontage();
	bool AcquireLandingRootMotion();
	void ReleaseLandingRootMotion();
	void FinalizeHostLanding();
	void StartDiveWatchdog();
	void ClearDiveWatchdog();

	// ── 命中额度 ──
	void RecordDamagedTarget(int32 SegmentIndex, const AActor* Target);

	// ── 收尾 ──
	void CompletePiercerAction();
	void CancelPiercerAction(EWeaponActionEndReason EndReason, const TCHAR* Diagnostic);
	void ResetActivationState();
	void TryCompleteLandingPhase();

	// ── 生命周期护栏 ──
	void BindLifecycleGuards();
	void UnbindLifecycleGuards();
	bool IsLiveContinuationStateValid() const;
	void HandleRuntimeInvalidated(const FWeaponRuntimeToken& InvalidatedToken);
	void HandleDisqualifyingStateTagChanged(FGameplayTag Tag, int32 NewCount);
	void HandleDiveWatchdogExpired();

	UFUNCTION()
	void HandleMainMontageCompleted();

	UFUNCTION()
	void HandleMainMontageInterrupted();

	UFUNCTION()
	void HandleLandingMontageCompleted();

	UFUNCTION()
	void HandleLandingMontageInterrupted();

	UFUNCTION()
	void HandleMovementFinished(const FWeaponMovementResult& Result);

	// ── 状态 ──
	UPROPERTY(Transient)
	TObjectPtr<UAbilityTask_MHGZPlayMontageAndWait> MainMontageTask;

	UPROPERTY(Transient)
	TObjectPtr<UAbilityTask_MHGZPlayMontageAndWait> LandingMontageTask;

	UPROPERTY(Transient)
	TObjectPtr<UAbilityTask_MHGZWeaponMovement> MovementTask;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveMainMontage;

	FWeaponActionToken PiercerActionToken;
	FWeaponActionToken MovementActionToken;
	int32 MainMontageInstanceID = INDEX_NONE;
	int32 LandingMontageInstanceID = INDEX_NONE;

	EKinsectPiercerPhase Phase = EKinsectPiercerPhase::Inactive;
	FVector LockedYawDirection = FVector::ForwardVector;

	/** `SegmentIndex → 本次激活内已成功伤害的目标`。三段各自独立，允许窗口重叠。 */
	TArray<TSet<TWeakObjectPtr<const AActor>>> DamagedTargetsPerSegment;

	/** Commit 时快照；触地清层不改变本次 GA 的任何伤害段。 */
	float FrozenDanceDamageMultiplier = 1.0f;

	FTimerHandle DiveWatchdogTimer;
	FDelegateHandle RuntimeInvalidationDelegateHandle;
	TMap<FGameplayTag, FDelegateHandle> DisqualifyingStateTagDelegateHandles;

	bool bLandingLatched = false;
	/** 201→203 的切换事务：任何 `EndTask/Montage_Stop` 之前必须先立起来。 */
	bool bSwitchingToLanding = false;
	bool bLandingMontageActive = false;
	bool bLandingMontageFinished = false;
	TWeakObjectPtr<UAnimInstance> LandingRootMotionAnimInstance;
	uint8 SavedLandingRootMotionMode = 0;
	bool bLandingRootMotionModeCaptured = false;
	bool bEndingPiercer = false;
	int32 LastSuccessfulConfigIndex = INDEX_NONE;
};
