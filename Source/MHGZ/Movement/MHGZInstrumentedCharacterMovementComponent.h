// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "WeaponRuntime/MHGZWeaponRuntimeTypes.h"
#include "MHGZInstrumentedCharacterMovementComponent.generated.h"

/** Read-only snapshot of the current/last recorded-trajectory movement lease. */
struct FMHGZRecordedTrajectoryTelemetry
{
	uint64 Lease = 0;
	float ElapsedSeconds = 0.0f;
	float CoveredSeconds = 0.0f;
	bool bActive = false;
	bool bCompleted = false;
};

/** One frame-boundary or movement-handoff sample, buffered until runtime telemetry drains it. */
struct FMHGZAerialMovementPhaseSample
{
	uint64 Serial = 0;
	uint64 Frame = 0;
	double WorldTimeSeconds = 0.0;
	FName Phase = NAME_None;
	float DeltaSeconds = 0.0f;
	FVector Location = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	FVector Acceleration = FVector::ZeroVector;
	uint8 MovementMode = MOVE_None;
	uint8 CustomMovementMode = 0;
	float GravityScale = 1.0f;
	float BrakingDecelerationFalling = 0.0f;
	float FallingLateralFriction = 0.0f;
	uint64 RuntimeGeneration = 0;
	FString ActionMovementOwner;
	FString MontageRootMotionOwner;
	FString ActiveActions;
	int32 ActiveRootMotionSourceCount = 0;
	int32 PendingRootMotionSourceCount = 0;
	bool bHasOverrideRootMotionVelocity = false;
	bool bHasAdditiveRootMotionVelocity = false;
	bool bHasRootMotionCallbackTransform = false;
	FTransform RootMotionCallbackTransform = FTransform::Identity;
	bool bHasMovementImpact = false;
	bool bImpactBlockingHit = false;
	bool bImpactStartPenetrating = false;
	FName ImpactActorName = NAME_None;
	FName ImpactComponentName = NAME_None;
	FVector ImpactPoint = FVector::ZeroVector;
	FVector ImpactNormal = FVector::ZeroVector;
	FVector ImpactMoveDelta = FVector::ZeroVector;
	float ImpactTimeSlice = 0.0f;

	// --- Intra-CMC sub-phase probe (P1-3, 2026-09-23) ---------------------------------
	// Aerial XY speed loss happens inside a single Super::PerformMovement call while the
	// 137 montage fades out. Frame boundaries alone cannot name the writing sub-phase, so
	// the same CMC records its overridable sub-phases and the anim-root feeding state.
	/** Bracketing override that produced this row (NAME_None = frame boundary or GA/Host callback). */
	FName SubPhaseSite = NAME_None;
	/** Velocity when the bracketed sub-phase was entered. */
	FVector SubPhaseEntryVelocity = FVector::ZeroVector;
	/** CalcVelocity arguments as actually used by the caller. */
	float SubPhaseFriction = 0.0f;
	float SubPhaseBrakingDeceleration = 0.0f;
	int32 SubPhaseIterations = 0;
	/** RootMotionParams.bHasRootMotion (HasAnimRootMotion()); only meaningful inside PerformMovement. */
	bool bHasAnimRootMotion = false;
	FVector RootMotionParamsTranslation = FVector::ZeroVector;
	/** CMC::AnimRootMotionVelocity, the value ConstrainAnimRootMotionVelocity would impose on XY. */
	FVector AnimRootMotionVelocity = FVector::ZeroVector;
	/** CharacterOwner->IsPlayingRootMotion(): root motion mode gate, not proof that this frame produced any. */
	bool bIsPlayingRootMotion = false;
	/** Whether the Host shield is currently bound to ProcessRootMotionPostConvertToWorld. */
	bool bShieldDelegateBound = false;
	/** Sticky additive-restore state; survives after the additive source is gone. */
	bool bAdditiveVelocityApplied = false;
	FVector LastPreAdditiveVelocity = FVector::ZeroVector;
	/** CMC.TickCharacterPose rows: what the animation side fed into RootMotionParams (P1-3 step 1). */
	bool bTickCharacterPoseSample = false;
	bool bRootMotionParamsBefore = false;
	FVector RootMotionParamsBeforeTranslation = FVector::ZeroVector;
	bool bMeshShouldTickPose = false;
	bool bPlayingRootMotionFromEverything = false;
	bool bPlayingNetworkedRootMotionMontage = false;
	int32 MontageInstanceCount = 0;
	FName RootMotionMontageName = NAME_None;
	float RootMotionMontagePosition = 0.0f;
	float RootMotionMontageWeight = 0.0f;
	bool bRootMotionMontageDisabled = false;

	/** CMC.ConstrainAnimRootMotionVelocity rows: the exact inputs and result of the vote. */
	bool bConstrainProbe = false;
	FName ConstrainCallSite = NAME_None;
	FVector ConstrainRootMotionVelocity = FVector::ZeroVector;
	FVector ConstrainInputVelocity = FVector::ZeroVector;
	FVector ConstrainOutputVelocity = FVector::ZeroVector;
};

/**
 * Project character CMC. Explicit aerial planar acceleration is integrated during
 * CMC-owned falling. A leased segmented dash owns Flying only for its measured
 * phase interval; other movement follows the engine implementation. Runtime
 * telemetry records the values around movement without changing that behavior.
 */
UCLASS()
class MHGZ_API UMHGZInstrumentedCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	virtual void PerformMovement(float DeltaTime) override;
	virtual void HandleImpact(const FHitResult& Hit, float TimeSlice = 0.0f,
		const FVector& MoveDelta = FVector::ZeroVector) override;

	// Intra-CMC sub-phases (P1-3). Telemetry brackets the same movement calculation
	// used when capture is disabled, including the explicit falling acceleration.
	virtual void RestorePreAdditiveRootMotionVelocity() override;
	virtual void ApplyRootMotionToVelocity(float deltaTime) override;
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid,
		float BrakingDeceleration) override;
	virtual void PhysFalling(float deltaTime, int32 Iterations) override;
	virtual void PhysFlying(float DeltaTime, int32 Iterations) override;
	/**
	 * 只在 203 那样的**地面**录制相位生效时接管，其余一律原样交给引擎。
	 * 接管期间曲线是唯一的水平位移来源，胶囊停在 MOVE_Walking 上 —— 它确实站在地上，
	 * 不是被悬空的 MOVE_Flying 假装成落地。
	 */
	virtual void PhysWalking(float deltaTime, int32 Iterations) override;
	virtual void ApplyAccumulatedForces(float DeltaSeconds) override;
	virtual bool HandlePendingLaunch() override;
	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;
	virtual void StartNewPhysics(float deltaTime, int32 Iterations) override;
	/**
	 * Brackets the animation tick that feeds RootMotionParams (PerformMovement:2824) and records what
	 * the animation side fed in (P1-3 step 1).
	 *
	 * ⚠ 这里曾经还挂一条 P0-1 的窄门控（`|Δroot| < ε` 就 `RootMotionParams.Clear()`），**已于
	 * 2026-09-23 22:50 按用户要求连同两个 CVar 一并删除** —— 现在本函数只记录、不改行为。
	 */
	virtual void TickCharacterPose(float DeltaTime) override;
	/** Records every vote, including the ones taken from PerformMovement itself. */
	virtual FVector ConstrainAnimRootMotionVelocity(const FVector& RootMotionVelocity,
		const FVector& CurrentVelocity) const override;

	/** Finite world-space XY acceleration for CMC-owned falling; Z is ignored. */
	void SetAerialPlanarAcceleration(FVector InAcceleration);
	FVector GetAerialPlanarAcceleration() const { return AerialPlanarAcceleration; }

	/** Returns a task-specific lease; action identity alone cannot distinguish replacements. */
	uint64 BeginSegmentedDirectionalMovement(const FWeaponMovementRequest& Request);
	bool IsSegmentedMovementActive(uint64 Lease) const;
	bool HasSegmentedMovementCompleted(uint64 Lease) const;
	void RetireSegmentedDirectionalMovement(uint64 Lease);

	/**
	 * 录制局部轨迹的同类租约。曲线模式生效期间它是该相位**唯一**的位移来源：
	 * 每个物理子步施加 `Curve(t1)-Curve(t0)`，不叠加惯性、蒙太奇根运动、摇杆加速度，
	 * 也不额外积分一次重力。返回 0 表示请求被拒（没有拿到租约）。
	 */
	uint64 BeginRecordedTrajectoryMovement(const FWeaponMovementRequest& Request);
	/** Monotonic ProtectedFlight -> LandingAirborne change for one exact trajectory lease. */
	bool EnableRecordedTrajectoryLanding(uint64 Lease);
	bool IsRecordedTrajectoryMovementActive(uint64 Lease) const;
	bool HasRecordedTrajectoryMovementCompleted(uint64 Lease) const;
	void RetireRecordedTrajectoryMovement(uint64 Lease);
	void GetRecordedTrajectoryTelemetry(const FWeaponActionToken& ExpectedOwner,
		FMHGZRecordedTrajectoryTelemetry& OutTelemetry) const;

	/** Re-apply a real landing's authored horizontal speed after the landing CMC tick. */
	void QueuePostMovementLandingSpeedReset(float Speed, const FVector& PlanarDirection);

	/** Records a sparse GA/Host/root-motion callback at its actual execution point. */
	void RecordAerialMovementEvent(FName Phase,
		const FTransform* RootMotionCallbackTransform = nullptr,
		float DeltaSeconds = 0.0f);

	void GetAerialMovementSamplesSince(uint64 LastObservedSerial,
		TArray<FMHGZAerialMovementPhaseSample>& OutSamples) const;
	uint64 GetLatestAerialMovementSampleSerial() const { return AerialMovementSampleSerial; }
	int32 GetCurrentActiveRootMotionSourceCount() const
	{
		return CurrentRootMotion.RootMotionSources.Num();
	}

	/**
	 * 自动化专用（只读）：上一次落地重设的载荷与已应用次数 —— 「Landed 回调写值」在帧外的可核对副本。
	 *
	 * P1-1 要求把「`Landed` 回调写值 / 同帧 CMC 后值 / 下一帧值」三个采样点分开断言，而回调那一次
	 * 直接写发生在 `PerformMovement` 内（帧外读不到），所以在这里留一份副本。
	 */
	float GetLastAppliedLandingSpeedForTest() const { return LastAppliedLandingSpeed; }
	FVector GetLastAppliedLandingDirectionForTest() const { return LastAppliedLandingDirection; }
	int32 GetLandingSpeedResetApplyCountForTest() const { return LandingSpeedResetApplyCount; }

private:
	static bool IsAerialMovementTelemetryEnabled();
	/** Telemetry on AND (falling OR inside a Host aerial presentation window). */
	bool ShouldCaptureMovementSubPhase() const;
	void CaptureMovementBoundary(FName Phase, float DeltaSeconds);
	void CaptureSample(FName Phase, float DeltaSeconds,
		const FTransform* RootMotionCallbackTransform) const;
	FMHGZAerialMovementPhaseSample& CaptureSubPhaseSample(FName Phase, float DeltaSeconds,
		const FVector& EntryVelocity) const;
	void CaptureImpactSample(FName Phase, const FHitResult& Hit, float TimeSlice,
		const FVector& MoveDelta);
	/** Names the animation-side feeder: which montage instance owns root motion, and its weight. */
	void LogAnimationRootMotionFeed(float DeltaTime, bool bRootMotionParamsBefore,
		const FVector& TranslationBefore, const FVector& TranslationAfter,
		const FRotator& RotationBefore) const;
	/** Dumps every recorded sub-phase of the PerformMovement call that just lost planar speed. */
	void LogDiscontinuityPhaseTrace(uint64 FirstSampleSerial) const;
	void ApplyPendingLandingSpeedReset();

	FVector AerialPlanarAcceleration = FVector::ZeroVector;
	bool IsSegmentedMovementOwnerCurrent(uint64 Lease) const;
	FWeaponMovementRequest SegmentedMovement;
	uint64 SegmentedMovementLease = 0;
	float SegmentedMovementElapsed = 0.0f;
	bool bSegmentedMovementActive = false;
	bool bSegmentedMovementCompleted = false;
	/** Remove only into-contact velocity; keep the phase clock and tangential slide. */
	TArray<FVector, TInlineAllocator<4>> SegmentedContactNormals;

	bool IsRecordedTrajectoryOwnerCurrent(uint64 Lease) const;
	/**
	 * 录制轨迹的物理子步循环。`PhysFlying` 与 `PhysWalking` 都归它，
	 * 因为同一套曲线模型既跑空中相位也跑 203 的地面相位。
	 */
	void PhysRecordedTrajectory(float DeltaTime, int32 Iterations, bool bGrounded);
	/** 相位是否该在此刻结束（只看曲线本身，不看真实触地）。 */
	bool IsRecordedTrajectoryDurationElapsed() const;

	FWeaponMovementRequest RecordedTrajectory;
	uint64 RecordedTrajectoryLease = 0;
	float RecordedTrajectoryElapsed = 0.0f;
	bool bRecordedTrajectoryActive = false;
	bool bRecordedTrajectoryCompleted = false;

	/** Mutable so the const ConstrainAnimRootMotionVelocity probe can append its vote. */
	mutable TArray<FMHGZAerialMovementPhaseSample> AerialMovementSamples;
	mutable uint64 AerialMovementSampleSerial = 0;
	/** Enclosing sub-phase while its Super runs; the Constrain probe reports it as the call site. */
	mutable FName ActiveMovementSubPhase = NAME_None;
	/** Feed narration is per-frame on grounded root-motion clips too, so it is capped and says so. */
	mutable int32 AerialRootMotionFeedLogCount = 0;
	static constexpr int32 AerialRootMotionFeedLogLimit = 400;
	static constexpr int32 AerialMovementSampleHistoryLimit = 512;
	bool bHasPendingLandingSpeedReset = false;
	float PendingLandingHorizontalSpeed = 0.0f;
	FVector PendingLandingPlanarDirection = FVector::ZeroVector;
	/**
	 * 「Landed 回调写值」的可核对副本 + 应用次数（自动化专用，见 .cpp 的 ApplyPendingLandingSpeedReset）。
	 * 存在的理由：回调那一次直接写 `Velocity.XY` 发生在 `PerformMovement` 内，帧外读不到；
	 * 而 P1-1 要求把「回调写值 / 同帧 CMC 后值 / 下一帧值」三个采样点分开断言。
	 */
	float LastAppliedLandingSpeed = 0.0f;
	FVector LastAppliedLandingDirection = FVector::ZeroVector;
	int32 LandingSpeedResetApplyCount = 0;
	bool bSawBlockingImpactDuringMovement = false;
	FName LastImpactActorName = NAME_None;
	FName LastImpactComponentName = NAME_None;
	FVector LastImpactNormal = FVector::ZeroVector;
};
