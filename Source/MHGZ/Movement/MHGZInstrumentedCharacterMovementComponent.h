// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MHGZInstrumentedCharacterMovementComponent.generated.h"

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
 * Project character CMC. Movement is unchanged; while runtime telemetry is enabled,
 * it records the exact values immediately before and after each PerformMovement call.
 */
UCLASS()
class MHGZ_API UMHGZInstrumentedCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	virtual void PerformMovement(float DeltaTime) override;
	virtual void HandleImpact(const FHitResult& Hit, float TimeSlice = 0.0f,
		const FVector& MoveDelta = FVector::ZeroVector) override;

	// Intra-CMC sub-phases (P1-3). Each one is a pure passthrough unless telemetry is on and
	// the character is inside the airborne presentation window; movement behaviour is unchanged.
	virtual void RestorePreAdditiveRootMotionVelocity() override;
	virtual void ApplyRootMotionToVelocity(float deltaTime) override;
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid,
		float BrakingDeceleration) override;
	virtual void PhysFalling(float deltaTime, int32 Iterations) override;
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
