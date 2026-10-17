// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActionSystem/MHGZKinsectSlashAbility.h"
#include "InsectGlaive/Kinsect/Kinsect.h"
#include "MHGZInsectGlaiveAbility.h"
#include "MHGZAwakenedKinsectAttackAbility.generated.h"

class UAbilityTask_MHGZWeaponMovement;
class UAbilityTask_MHGZPlayMontageAndWait;
class UAnimInstance;
class UAnimMontage;
class UMHGZWeaponRuntimeHostComponent;

UENUM(BlueprintType)
enum class EAwakenedKinsectAttackEvent : uint8
{
	KinsectLaunch,
	HunterFlightStart,
	ReleaseFacingCorrection
};

enum class EAwakenedKinsectReleaseRoute : uint8
{
	None,
	QueueSendAfterStartup,
	JumpToSendNow
};

/**
 * M6 觉虫击 GA 的基础路由。
 *
 * 负责激活时选择普通/三灯强化分支、193 起手/保持/释放路由、猎虫飞行、199 猎人冲刺，
 * 以及冲刺结束后的 206 下落交接。对应正式 ComboData/GA 与动画资产接线仍需编辑器阶段完成。
 */
UCLASS(BlueprintType, Blueprintable)
class MHGZ_API UMHGZAwakenedKinsectAttackAbility : public UMHGZInsectGlaiveAbility
{
	GENERATED_BODY()

public:
	UMHGZAwakenedKinsectAttackAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility, bool bWasCancelled) override;

	virtual void HandleInputReleased(const FWeaponInputSnapshot& Snapshot) override;

	/** 当前激活是否已在 Commit 前选中并成功消费三灯强化分支。 */
	UFUNCTION(BlueprintPure, Category = "MHGZ|IG|Awakened Kinsect")
	bool IsEmpoweredKinsectBranch() const { return bEmpoweredKinsectBranch; }

	/** Exact Montage-instance event entry used by AnimNotify_IG_AwakenedKinsectEvent. */
	bool HandleAwakenedKinsectEvent(const FWeaponActionToken& ActionToken,
		EAwakenedKinsectAttackEvent Event);

	/** Exact Montage-instance boundary: atomically start the frozen kinsect request once. */
	bool CommitKinsectLaunch(const FWeaponActionToken& ActionToken);

	/** Projects the Action Aim onto separate yaw/pitch caps around the frozen ActorForward. */
	static bool ResolveLockedAimDirection(const FVector& ActorForward,
		const FVector& AimDirection, float MaxYawAngleDegrees,
		float MaxPitchAngleDegrees,
		FVector& OutLockedDirection);

	/** Compensates third-person parallax against the saved target, then applies the same caps. */
	static bool ResolveLockedTargetDirection(const FVector& ActorForward,
		const FVector& TargetPoint, const FVector& LaunchOrigin, const FVector& FallbackAimDirection,
		float MaxYawAngleDegrees, float MaxPitchAngleDegrees, FVector& OutLockedDirection);

	/** Reads the authored KinsectLaunch -> HunterFlightStart interval from the persistent Montage. */
	static bool GetAuthoredKinsectFlightDuration(const UAnimMontage& Montage,
		float& OutDurationSeconds);

	/** Uses fixed code speeds; the generic request distance is derived from speed * authored duration. */
	static bool BuildAwakenedKinsectFlightProfile(float FlightDurationSeconds,
		bool bEmpowered, float& OutMaxDistance, float& OutFlightSpeed);

	/** Builds the data-driven 199 CMC request from the release-locked Aim and tuning. */
	static bool BuildHunterFlightMovementRequest(const FVector& LockedDirection,
		float Speed, float Duration, float GravityMagnitude,
		FWeaponMovementRequest& OutRequest);
	/** Continuous target-distance reconstruction for a launch from floor toward a real ground hit. */
	static bool BuildGroundTargetHunterFlightMovementRequest(const FVector& CorrectedForward,
		const FVector& LaunchOrigin, const FVector& GroundTarget, float Speed, float Duration,
		float GravityMagnitude, float MinimumDistance, float VerticalLaunchSpeed,
		FWeaponMovementRequest& OutRequest);

	/** 起手段释放只排队在前摇播完后丢虫；保持循环释放才立即跳到 Send。 */
	static EAwakenedKinsectReleaseRoute ResolveReleaseRoute(FName CurrentSection,
		FName InStartupSection, FName InHoldLoopSection);

	/** Returns the next section after an entry into the hold loop; the third play exits to Send. */
	static FName ResolveHoldLoopNextSection(int32 EnteredLoopPlayCount,
		FName InHoldLoopSection, FName InSendSection);

	/** Pure gate shared by runtime collision filtering and its regression tests. */
	static bool IsHunterFlightDamageWindowOpen(bool bActionActive,
		bool bExactAttackMontageInstance, bool bHunterFlightStarted,
		bool bDanceTransitionPending, bool bDanceEntryCommitted,
		int32 SegmentIndex, FName CurrentSection, FName ExpectedSection);

#if WITH_DEV_AUTOMATION_TESTS
	/** Seeds a release-frame camera sample without requiring a live player controller. */
	void SetReleaseAimSnapshotForTest(const FVector& ActorForward,
		const FVector& AimDirection, const FVector& TargetPoint, const FHitResult* BlockingHit = nullptr)
	{
		ReleaseActorForward = ActorForward.GetSafeNormal();
		ReleaseAimSnapshot.Context = EWeaponAimSnapshotContext::Action;
		ReleaseAimSnapshot.Direction = AimDirection;
		ReleaseAimSnapshot.TargetPoint = TargetPoint;
		ReleaseAimSnapshot.bHasBlockingHit = BlockingHit != nullptr;
		ReleaseAimSnapshot.BlockingHit = BlockingHit ? *BlockingHit : FHitResult();
		bHasReleaseAimSnapshot = true;
		bReleaseAimLocked = false;
	}
	bool LockReleaseAimForTest() { return LockReleaseAim(); }
	void SetKinsectFlightDurationForTest(float Seconds)
	{
		TestKinsectFlightDurationSeconds = Seconds;
	}
#endif

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Awakened Kinsect|Montage")
	FName StartupSection = TEXT("Startup193");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Awakened Kinsect|Montage")
	FName HoldLoopSection = TEXT("Hold193Loop");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Awakened Kinsect|Montage")
	FName SendSection = TEXT("Send195");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Awakened Kinsect|Montage")
	FName PreFlightSection = TEXT("PreFlight196");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Awakened Kinsect|Montage")
	FName HunterFlightSection = TEXT("HunterFlight199");

	/** Shared persistent M5 189 montage; never generated at runtime. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Awakened Kinsect|Dance")
	TObjectPtr<UAnimMontage> DanceMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Awakened Kinsect|Dance")
	FName DanceSectionName = TEXT("HitDance189");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Awakened Kinsect|Dance",
		meta = (ClampMin = "0.0", ForceUnits = "s"))
	float DanceMontageBlendInTime = 0.1f;

	/** Copied from M5 DanceFromDashTuning so both attacks share the same 189 arc. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Awakened Kinsect|Dance")
	FKinsectSlashDanceEntryTuning DanceFromDashTuning;

	/** 全部松开才离开 193；RT 是 Chord modifier，不属于释放控制。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Awakened Kinsect|Input")
	TArray<FGameplayTag> HoldReleaseControls;

protected:
	virtual bool ValidateActionDependencies() const override;
	virtual bool PrepareActivationWeaponResourceCosts(
		TArray<FWeaponResourceCostSpec>& OutCosts) override;
	virtual bool PrepareAttackMontage() override;
	virtual void ApplyActivationDirectionCorrection() override;
	virtual bool StartAttackMontage(ACharacter& Character, UAnimMontage* Montage,
		FName StartSection) override;
	virtual bool CanApplyCollisionHit(const FHitResult& Hit,
		int32 SegmentIndex) const override;
	virtual bool ShouldConsumeCollisionOnDamageFailure(int32 SegmentIndex) const override;
	virtual void OnAttackCollisionWindowClosed(int32 SegmentIndex) override;
	virtual void HandleSuccessfulAttackDamage(const FHitResult& Hit,
		int32 SegmentIndex) override;
	virtual void OnMontageCompleted() override;
	virtual void HandleLanded(const FHitResult& Hit) override;

#if WITH_DEV_AUTOMATION_TESTS
	/** Test-only access to drive the real movement task's completion callback deterministically. */
	UAbilityTask_MHGZWeaponMovement* GetHunterFlightMovementTaskForTest() const
	{
		return HunterFlightMovementTask.Get();
	}
#endif

private:
	bool BuildPendingKinsectRequest(const FWeaponInputSnapshot& Input);
	void ApplyConsumedExtractEffects(const class UInsectGlaiveCombatConfig& Config);
	void RecallKinsectForThisAction();
	bool StartHunterFlight(const FWeaponActionToken& ActionToken);
	UFUNCTION()
	void HandleHunterFlightMovementFinished(const FWeaponMovementResult& Result);
	void HandleQueuedDanceTransition(FWeaponActionToken ActionToken,
		int32 SourceMontageInstanceID);
	bool StartDancePreparationMovement();
	bool StartDanceMontage();
	bool StartDanceMovement();
	void HandleDancePreparationFinished(FWeaponActionToken ActionToken,
		int32 DanceMontageInstanceID);
	UFUNCTION()
	void HandleDanceMovementFinished(const FWeaponMovementResult& Result);
	UFUNCTION()
	void HandleDanceMontageCompleted();
	UFUNCTION()
	void HandleDanceMontageInterrupted();
	void TryCompleteDanceAction();
	void ClearDanceTimers();
	void CleanupDanceTasks();
	void StartFallbackFallingAfterActionEnd(UMHGZWeaponRuntimeHostComponent* Host,
		bool bShouldStartFallback);
	bool Configure193HoldLoop();
	bool AreAllHoldReleaseControlsUp() const;
	bool CaptureReleaseAimSnapshot();
	bool LockReleaseAim();
	bool ApplyReleaseFacingCorrection();
	bool UpdatePendingKinsectFlightProfile();
	bool GetKinsectFlightDuration(float& OutSeconds) const;
	void OnAttackMontageSectionChanged(UAnimMontage* Montage,
		FName SectionName, bool bLooped);
	void UnbindAttackMontageSectionChanged();
	UAnimInstance* GetAttackAnimInstance() const;
	bool DisableAttackMontageRootMotionForHunterFlight(
		const FWeaponActionToken& ActionToken);
	void ScheduleHoldReleaseCheck();
	void ResolveHoldReleaseCheck();
	void ApplyHoldReleaseRouteIfReady();

	bool bEmpoweredKinsectBranch = false;
	/** 本次已消耗颜色的快照；三灯GE没有单色Tag，原子消费时显式记录三色。 */
	FGameplayTagContainer ConsumedExtractSnapshot;
	bool bKinsectLaunchCommitted = false;
	bool bKinsectActionAttached = false;
	bool bHoldReleaseCheckScheduled = false;
	bool bHoldReleasePending = false;
	bool bHoldReleaseRouteApplied = false;
	bool bHasReleaseAimSnapshot = false;
	bool bReleaseAimLocked = false;
	bool bReleaseFacingCorrectionStarted = false;
	bool bHunterFlightStarted = false;
	bool bHunterFallTransitionCommitted = false;
	bool bDanceTransitionPending = false;
	bool bDanceEntryCommitted = false;
	bool bDanceMontageFinished = false;
	bool bDanceMovementFinished = false;
	bool bDanceLanded = false;
	bool bEndingHunterFlightForDance = false;
	bool bEndingDanceRuntime = false;
	int32 HoldLoopPlayCount = 0;
	int32 AttackMontageInstanceID = INDEX_NONE;
	int32 DanceMontageInstanceID = INDEX_NONE;
	int32 SuccessfulHitSourceMontageInstanceID = INDEX_NONE;
	float SuccessfulHitSourcePosition = 0.0f;
	FHitResult SuccessfulHunterHit;
	UPROPERTY(Transient)
	TObjectPtr<UAbilityTask_MHGZWeaponMovement> HunterFlightMovementTask;
	UPROPERTY(Transient)
	TObjectPtr<UAbilityTask_MHGZWeaponMovement> DancePreparationMovementTask;
	UPROPERTY(Transient)
	TObjectPtr<UAbilityTask_MHGZWeaponMovement> DanceMovementTask;
	UPROPERTY(Transient)
	TObjectPtr<UAbilityTask_MHGZPlayMontageAndWait> DanceMontageTask;
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveDanceMontage;
	FTimerHandle QueuedDanceTransitionTimer;
	FTimerHandle DancePreparationTimer;
	FVector LockedKinsectDirection = FVector::ForwardVector;
	FVector LockedHunterFlightDirection = FVector::ForwardVector;
	FVector ReleaseActorForward = FVector::ForwardVector;
	FWeaponAimSnapshot ReleaseAimSnapshot;
	FVector LockedHunterTargetPoint = FVector::ZeroVector;
	FGuid AwakenedAttackInstanceID;
	FKinsectFlightRequest PendingKinsectRequest;
#if WITH_DEV_AUTOMATION_TESTS
	float TestKinsectFlightDurationSeconds = -1.0f;
#endif
};
