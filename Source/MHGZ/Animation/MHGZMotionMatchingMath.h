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
struct FMHGZMotionMatchingCruiseSpeedSettings
{
	float MoveDeadzone = 0.1f;
	float WalkInputThreshold = 0.5f;
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
	if (StickMagnitude < Settings.MoveDeadzone)
	{
		return 0.0f;
	}
	if (bUnsheathed)
	{
		return Settings.UnsheathedCruiseSpeed;
	}
	if (StickMagnitude > Settings.SprintInputThreshold && bSprintHeld)
	{
		return Settings.SprintCruiseSpeed;
	}
	return StickMagnitude < Settings.WalkInputThreshold
		? Settings.WalkCruiseSpeed
		: Settings.RunCruiseSpeed;
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

/** True when the actual Root Motion measurement must be discarded for this update. */
FORCEINLINE bool ShouldResetMotionMeasurement(const bool bForceMMIdle,
	const bool bWasForceMMIdle, const bool bMovingOnGround,
	const bool bHasPreviousLocation, const float DeltaSeconds)
{
	return bForceMMIdle || bWasForceMMIdle || !bMovingOnGround
		|| !bHasPreviousLocation || DeltaSeconds <= 0.0f;
}
}
