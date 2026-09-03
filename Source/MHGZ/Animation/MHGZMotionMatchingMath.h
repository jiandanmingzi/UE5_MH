// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * 纯 Motion Matching 普通移动的无状态查询数学。
 *
 * 这些函数不读取 World、Pawn、CharacterMovement 或 AnimInstance，因而既能由
 * Character/AnimInstance 使用，也能由自动化测试直接验证。普通移动的实际位移仍
 * 完全来自动画 Root Motion；这里仅构造 Pose Search 的查询意图和预测距离。
 */
namespace MHGZMotionMatching
{
/** Semantic gait families used only by the normal-move candidate-bank router. */
enum class EMHGZNormalMoveGait : uint8
{
	None,
	Walk,
	Run,
	Sprint
};

/** The candidate context installed on the sheathed normal locomotion MM node. */
enum class EMHGZNormalMoveCandidateSet : uint8
{
	FullMove,
	RunLoopOnly,
	SprintLoopOnly
};

/** Inputs to the pure M4.2.1 candidate-bank decision. */
struct FMHGZNormalMoveCandidateRouteInput
{
	EMHGZNormalMoveCandidateSet CurrentCandidateSet = EMHGZNormalMoveCandidateSet::FullMove;
	EMHGZNormalMoveGait TargetGait = EMHGZNormalMoveGait::None;
	bool bActionRouteIsMove = true;
	bool bHasLocomotionInput = false;
	bool bStopRequestActive = false;
	bool bStartQueryActive = false;
	bool bLatestSelectionIsRunLoop = false;
	bool bLatestSelectionIsSprintLoop = false;
};

/**
 * Narrows a full normal-move search only after a real Run/Sprint Loop result
 * has already been selected and the target family changes. This deliberately
 * leaves first Starts, Stops, Walk, ActionExit and unsheathed locomotion on
 * their existing candidate paths.
 */
FORCEINLINE EMHGZNormalMoveCandidateSet ResolveSheathedNormalMoveCandidateSet(
	const FMHGZNormalMoveCandidateRouteInput& Input)
{
	if (!Input.bActionRouteIsMove || !Input.bHasLocomotionInput
		|| Input.bStopRequestActive || Input.bStartQueryActive)
	{
		return EMHGZNormalMoveCandidateSet::FullMove;
	}

	if (Input.CurrentCandidateSet != EMHGZNormalMoveCandidateSet::FullMove)
	{
		return Input.TargetGait == EMHGZNormalMoveGait::Run
			? EMHGZNormalMoveCandidateSet::RunLoopOnly
			: Input.TargetGait == EMHGZNormalMoveGait::Sprint
				? EMHGZNormalMoveCandidateSet::SprintLoopOnly
				: EMHGZNormalMoveCandidateSet::FullMove;
	}

	if (Input.bLatestSelectionIsRunLoop && Input.TargetGait == EMHGZNormalMoveGait::Sprint)
	{
		return EMHGZNormalMoveCandidateSet::SprintLoopOnly;
	}
	if (Input.bLatestSelectionIsSprintLoop && Input.TargetGait == EMHGZNormalMoveGait::Run)
	{
		return EMHGZNormalMoveCandidateSet::RunLoopOnly;
	}
	return EMHGZNormalMoveCandidateSet::FullMove;
}
struct FMHGZMotionMatchingCruiseSpeedSettings
{
	/** Physical stick noise threshold. This does not decide whether sheathed locomotion starts. */
	float RawMoveDeadzone = 0.1f;
	/** Minimum stick magnitude that starts a sheathed Walk request. */
	float SheathedWalkInputThreshold = 0.5f;
	/** Minimum stick magnitude that promotes a sheathed Walk request to Run. */
	float SheathedRunInputThreshold = 0.75f;
	float SprintInputThreshold = 0.9f;
	float WalkCruiseSpeed = 160.0f;
	float RunCruiseSpeed = 460.0f;
	float SprintCruiseSpeed = 575.0f;
	float UnsheathedCruiseSpeed = 440.0f;
};

/** Maps input to an animation-backed speed lane; no unsupported in-between speed is emitted. */
FORCEINLINE float QuantizeCruiseSpeed(const FMHGZMotionMatchingCruiseSpeedSettings& Settings,
	const float StickMagnitude, const bool bUnsheathed, const bool bSprintHeld)
{
	if (StickMagnitude < Settings.RawMoveDeadzone)
	{
		return 0.0f;
	}
	if (bUnsheathed)
	{
		return Settings.UnsheathedCruiseSpeed;
	}
	if (StickMagnitude < Settings.SheathedWalkInputThreshold)
	{
		return 0.0f;
	}
	if (StickMagnitude > Settings.SprintInputThreshold && bSprintHeld)
	{
		return Settings.SprintCruiseSpeed;
	}
	return StickMagnitude < Settings.SheathedRunInputThreshold
		? Settings.WalkCruiseSpeed
		: Settings.RunCruiseSpeed;
}

/**
 * State for the short, one-directional Run/Sprint -> lower-lane confirmation.
 * It only delays committing a lower non-zero gait. Releasing below the
 * configured locomotion threshold remains immediate.
 */
struct FMHGZDownshiftProbeState
{
	float CommittedCruiseSpeed = 0.0f;
	float PreviousStickMagnitude = 0.0f;
	float ElapsedSeconds = 0.0f;
	bool bActive = false;

	FORCEINLINE void Reset()
	{
		CommittedCruiseSpeed = 0.0f;
		PreviousStickMagnitude = 0.0f;
		ElapsedSeconds = 0.0f;
		bActive = false;
	}
};

/**
 * Resolves the authoritative locomotion cruise speed for one input sample.
 * Up-shifts and RB-only changes commit immediately. A real downward stick
 * transition from Run/Sprint to a lower non-zero lane waits briefly so a
 * normal stick release cannot publish an intermediate Walk request.
 */
FORCEINLINE float ResolveCruiseSpeedWithDownshiftProbe(
	FMHGZDownshiftProbeState& State, const float RequestedCruiseSpeed,
	const float StickMagnitude, const float DeltaSeconds,
	const float ConfirmDurationSeconds, const bool bAllowDownshiftProbe)
{
	const float SafeStickMagnitude = FMath::Max(0.0f, StickMagnitude);
	const auto CommitRequested = [&State, SafeStickMagnitude](const float Speed)
	{
		State.CommittedCruiseSpeed = FMath::Max(0.0f, Speed);
		State.PreviousStickMagnitude = SafeStickMagnitude;
		State.ElapsedSeconds = 0.0f;
		State.bActive = false;
		return State.CommittedCruiseSpeed;
	};

	if (RequestedCruiseSpeed <= KINDA_SMALL_NUMBER)
	{
		return CommitRequested(0.0f);
	}

	if (!bAllowDownshiftProbe || State.CommittedCruiseSpeed <= KINDA_SMALL_NUMBER)
	{
		return CommitRequested(RequestedCruiseSpeed);
	}

	const bool bRequestedDownshift = RequestedCruiseSpeed
		< State.CommittedCruiseSpeed - KINDA_SMALL_NUMBER;
	if (!bRequestedDownshift)
	{
		return CommitRequested(RequestedCruiseSpeed);
	}

	if (!State.bActive)
	{
		const bool bStickMagnitudeFell = SafeStickMagnitude
			< State.PreviousStickMagnitude - KINDA_SMALL_NUMBER;
		if (!bStickMagnitudeFell || ConfirmDurationSeconds <= KINDA_SMALL_NUMBER)
		{
			return CommitRequested(RequestedCruiseSpeed);
		}

		State.ElapsedSeconds = 0.0f;
		State.bActive = true;
		State.PreviousStickMagnitude = SafeStickMagnitude;
		return State.CommittedCruiseSpeed;
	}

	State.ElapsedSeconds += FMath::Max(0.0f, DeltaSeconds);
	State.PreviousStickMagnitude = SafeStickMagnitude;
	if (State.ElapsedSeconds >= ConfirmDurationSeconds)
	{
		return CommitRequested(RequestedCruiseSpeed);
	}

	return State.CommittedCruiseSpeed;
}

/** Signed remaining distance convention used by the MM_DistanceToStop curve: negative -> zero. */
FORCEINLINE float CalculateStopDistanceQuery(const float ActualSpeed,
	const float StopDeceleration)
{
	const float Speed = FMath::Max(0.0f, ActualSpeed);
	const float Deceleration = FMath::Max(1.0f, FMath::Abs(StopDeceleration));
	return Speed > UE_KINDA_SMALL_NUMBER
		? -(Speed * Speed) / (2.0f * Deceleration)
		: 0.0f;
}

/**
 * Predicts forward distance for a constant acceleration/deceleration that clamps at TargetSpeed.
 * All distances are non-negative because this project has only forward locomotion assets.
 */
FORCEINLINE float CalculatePredictedDistance(const float InitialSpeed,
	const float TargetSpeed, const float AccelerationMagnitude, const float TimeSeconds)
{
	const float Time = FMath::Max(0.0f, TimeSeconds);
	const float V0 = FMath::Max(0.0f, InitialSpeed);
	const float V1 = FMath::Max(0.0f, TargetSpeed);
	const float DeltaSpeed = V1 - V0;
	if (FMath::IsNearlyZero(DeltaSpeed))
	{
		return V0 * Time;
	}

	const float Acceleration = FMath::Max(1.0f, FMath::Abs(AccelerationMagnitude));
	const float Sign = DeltaSpeed > 0.0f ? 1.0f : -1.0f;
	const float TimeToTarget = FMath::Abs(DeltaSpeed) / Acceleration;
	if (Time <= TimeToTarget)
	{
		return FMath::Max(0.0f, V0 * Time + 0.5f * Sign * Acceleration * Time * Time);
	}

	const float DistanceToTarget = V0 * TimeToTarget
		+ 0.5f * Sign * Acceleration * TimeToTarget * TimeToTarget;
	return FMath::Max(0.0f, DistanceToTarget + V1 * (Time - TimeToTarget));
}

/**
 * Converts the project's forward-only query distance into world space.
 *
 * The imported locomotion clips use an unusual root-bone basis: their forward
 * root motion is indexed on the Pose Search trajectory's local +Y axis. Keep
 * this convention in one place so query samples and the indexed animation data
 * use the same axis even when the Actor has turned in world space.
 */
FORCEINLINE FVector GetImportedLocomotionTrajectoryForward(const FTransform& ActorTransform)
{
	return ActorTransform.GetUnitAxis(EAxis::Y);
}

/** True when the actual Root Motion measurement must be discarded for this update. */
FORCEINLINE bool ShouldResetMotionMeasurement(const bool bForceMMIdle,
	const bool bWasForceMMIdle, const bool bMovingOnGround,
	const bool bHasPreviousLocation, const float DeltaSeconds)
{
	return bForceMMIdle || bWasForceMMIdle || !bMovingOnGround
		|| !bHasPreviousLocation || DeltaSeconds <= 0.0f;
}

/**
 * A mobile Action Handoff is the one case where a Root-Motion measurement reset
 * must retain a locomotion edge. A held stick must enter the Exit tail as an
 * existing cruise request, while a release observed during that mobile phase
 * must survive for one normal update so it becomes the legal Stop edge.
 *
 * A locked action never satisfies this predicate: its later stick state is a
 * new locomotion request and must therefore use the normal Start path.
 */
FORCEINLINE bool ShouldPreserveHandoffInputAcrossMeasurementReset(
	const bool bLeavingForcedMeasurement, const bool bHandoffActive,
	const bool bHadRawMoveInputInMobilePhase, const bool bHasRawMoveInputAtHandoff,
	const bool bPendingStopAtHandoff)
{
	return bLeavingForcedMeasurement && bHandoffActive
		&& bHadRawMoveInputInMobilePhase
		&& (bHasRawMoveInputAtHandoff || bPendingStopAtHandoff);
}

/**
 * A non-handoff action ended with no locomotion input. The next MM search must
 * use the isolated Idle candidate database rather than the unrestricted Move
 * database, whose feet pose can otherwise self-start a Loop.
 */
FORCEINLINE bool ShouldPublishActionIdleContextOnMeasurementRelease(
	const bool bLeavingForcedMeasurement, const bool bHandoffActive,
	const bool bHasLocomotionInput)
{
	return bLeavingForcedMeasurement && !bHandoffActive && !bHasLocomotionInput;
}

/**
 * An approved mobile Action Exit begins only after its functional/root-motion
 * phase has ended. A real release during that non-functional transition must
 * therefore hand the current pose to the ordinary Move PSD immediately: the
 * existing one-shot Stop query owns selection from that frame onward. Keeping
 * the Exit continuing pose would otherwise defer Stop until its tail.
 */
FORCEINLINE bool ShouldInterruptMobileActionExitForStop(
	const bool bRouteOwnsMobileExit, const bool bStopRequestActive)
{
	return bRouteOwnsMobileExit && bStopRequestActive;
}

/**
 * A Stop request is a one-shot input-edge semantic. Once it has accepted a
 * Pose Search result, only the same continuously-playing result may feed its
 * MM curves back into the next query. A newly searched Stop entry would start
 * its curve near -1 again and re-arm the same release indefinitely.
 */
FORCEINLINE bool ShouldContinueConsumedStopSelection(const bool bSameAnimation,
	const bool bHasNewSelectionEvent, const bool bIsContinuingPoseSearch,
	const float PreviousAnimationTime, const float CurrentAnimationTime)
{
	if (!bSameAnimation)
	{
		return false;
	}
	if (!bHasNewSelectionEvent)
	{
		return true;
	}
	return bIsContinuingPoseSearch
		&& CurrentAnimationTime + KINDA_SMALL_NUMBER >= PreviousAnimationTime;
}
}
