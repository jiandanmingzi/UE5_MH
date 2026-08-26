// Copyright MHGZ Project. All Rights Reserved.

#include "Animation/MHGZMotionMatchingAnimInstance.h"

#include "Animation/MHGZMotionMatchingMath.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MHGZCharacter.h"

namespace
{
constexpr float PredictionTimes[] = { 0.0f, 0.2f, 0.5f, 0.8f, 1.0f };
}

void UMHGZMotionMatchingAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	CachedCharacter = Cast<AMHGZCharacter>(TryGetPawnOwner());
	bHasPreviousActorLocation = false;
	bStartQueryActive = false;
	bHadMoveInput = false;
	bWasForceMMIdle = false;
	MMStartQueryElapsed = 0.0f;
	MMStartQueryDuration = 0.0f;
	MMActualSpeed2D = 0.0f;
	MMIntentQuery = 0.0f;
	MMDistanceToStopQuery = 0.0f;
	MMLastNonZeroCruiseSpeed = 0.0f;
	MMPredictedTrajectory.Samples.Reset();
}

void UMHGZMotionMatchingAnimInstance::NativeUpdateAnimation(const float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	AMHGZCharacter* Character = CachedCharacter.Get();
	if (!IsValid(Character))
	{
		Character = Cast<AMHGZCharacter>(TryGetPawnOwner());
		CachedCharacter = Character;
	}
	if (!IsValid(Character))
	{
		MMActualSpeed2D = 0.0f;
		MMIntentQuery = 0.0f;
		MMDistanceToStopQuery = 0.0f;
		MMLastNonZeroCruiseSpeed = 0.0f;
		MMPredictedTrajectory.Samples.Reset();
		bHasPreviousActorLocation = false;
		bStartQueryActive = false;
		bHadMoveInput = false;
		bWasForceMMIdle = false;
		return;
	}

	const FVector CurrentLocation = Character->GetActorLocation();
	const UCharacterMovementComponent* MovementComponent = Character->GetCharacterMovement();
	const bool bMovingOnGround = MovementComponent && MovementComponent->IsMovingOnGround();
	const bool bForceMMIdle = Character->bForceMMIdle;
	if (MHGZMotionMatching::ShouldResetMotionMeasurement(bForceMMIdle,
		bWasForceMMIdle, bMovingOnGround, bHasPreviousActorLocation, DeltaSeconds))
	{
		ResetMMTemporalState(CurrentLocation);
		bWasForceMMIdle = bForceMMIdle;
		return;
	}

	const float FrameDistance = FVector::Dist2D(CurrentLocation, PreviousActorLocation);
	PreviousActorLocation = CurrentLocation;
	if (FrameDistance > MMTeleportResetDistance)
	{
		ResetMMTemporalState(CurrentLocation);
		bWasForceMMIdle = bForceMMIdle;
		return;
	}

	MMActualSpeed2D = FrameDistance / DeltaSeconds;

	const bool bHasInput = Character->bHasInput;
	const bool bUnsheathed = Character->bUnsheathed;
	const float TargetSpeed = Character->TargetCruiseSpeed;
	if (bHasInput && TargetSpeed > 1.0f)
	{
		MMLastNonZeroCruiseSpeed = TargetSpeed;
	}

	if (!bHadMoveInput && bHasInput && MMActualSpeed2D <= MMStartEligibilitySpeed)
	{
		bStartQueryActive = true;
		MMStartQueryElapsed = 0.0f;
		MMStartQueryDuration = SelectStartQueryDuration(bUnsheathed, TargetSpeed);
	}
	if (!bHasInput)
	{
		bStartQueryActive = false;
		MMStartQueryElapsed = 0.0f;
		MMStartQueryDuration = 0.0f;
	}

	if (bStartQueryActive)
	{
		MMIntentQuery = 1.0f - FMath::Clamp(
			MMStartQueryElapsed / FMath::Max(MMStartQueryDuration, 0.01f), 0.0f, 1.0f);
		MMStartQueryElapsed += DeltaSeconds;
		if (MMStartQueryElapsed >= MMStartQueryDuration)
		{
			bStartQueryActive = false;
		}
	}
	else if (!bHasInput && MMActualSpeed2D > MMIdleSpeedThreshold)
	{
		MMIntentQuery = -FMath::Clamp(MMActualSpeed2D /
			FMath::Max(MMLastNonZeroCruiseSpeed, 1.0f), 0.0f, 1.0f);
	}
	else
	{
		MMIntentQuery = 0.0f;
	}

	const float DecelerationReferenceSpeed = FMath::Max(MMActualSpeed2D,
		MMLastNonZeroCruiseSpeed);
	const float StopDeceleration = SelectStopDeceleration(bUnsheathed,
		DecelerationReferenceSpeed);
	MMDistanceToStopQuery = !bHasInput && MMActualSpeed2D > MMIdleSpeedThreshold
		? MHGZMotionMatching::CalculateStopDistanceQuery(MMActualSpeed2D, StopDeceleration)
		: 0.0f;

	const float TrajectoryTargetSpeed = bHasInput ? TargetSpeed : 0.0f;
	const float TrajectoryAcceleration = TrajectoryTargetSpeed >= MMActualSpeed2D
		? SelectStartAcceleration(bUnsheathed, TrajectoryTargetSpeed)
		: StopDeceleration;
	BuildPredictedTrajectory(Character->GetActorTransform(), MMActualSpeed2D,
		TrajectoryTargetSpeed, TrajectoryAcceleration);

	bHadMoveInput = bHasInput;
	bWasForceMMIdle = false;
}

void UMHGZMotionMatchingAnimInstance::ResetMMTemporalState(const FVector& CurrentLocation)
{
	PreviousActorLocation = CurrentLocation;
	bHasPreviousActorLocation = true;
	bStartQueryActive = false;
	bHadMoveInput = false;
	MMStartQueryElapsed = 0.0f;
	MMStartQueryDuration = 0.0f;
	MMActualSpeed2D = 0.0f;
	MMIntentQuery = 0.0f;
	MMDistanceToStopQuery = 0.0f;
	MMLastNonZeroCruiseSpeed = 0.0f;

	if (const AMHGZCharacter* Character = CachedCharacter.Get())
	{
		BuildFlatTrajectory(Character->GetActorTransform());
	}
	else
	{
		MMPredictedTrajectory.Samples.Reset();
	}
}

void UMHGZMotionMatchingAnimInstance::BuildFlatTrajectory(const FTransform& ActorTransform)
{
	MMPredictedTrajectory.Samples.Reset(UE_ARRAY_COUNT(PredictionTimes));
	for (const float Time : PredictionTimes)
	{
		FTransformTrajectorySample& Sample = MMPredictedTrajectory.Samples.AddDefaulted_GetRef();
		Sample.TimeInSeconds = Time;
		Sample.Facing = ActorTransform.GetRotation();
		Sample.Position = ActorTransform.GetLocation();
	}
}

void UMHGZMotionMatchingAnimInstance::BuildPredictedTrajectory(const FTransform& ActorTransform,
	const float InitialSpeed, const float TargetSpeed, const float Acceleration)
{
	MMPredictedTrajectory.Samples.Reset(UE_ARRAY_COUNT(PredictionTimes));
	const FVector CurrentLocation = ActorTransform.GetLocation();
	const FVector Forward = ActorTransform.GetUnitAxis(EAxis::X);
	for (const float Time : PredictionTimes)
	{
		FTransformTrajectorySample& Sample = MMPredictedTrajectory.Samples.AddDefaulted_GetRef();
		Sample.TimeInSeconds = Time;
		Sample.Facing = ActorTransform.GetRotation();
		Sample.Position = CurrentLocation + Forward *
			MHGZMotionMatching::CalculatePredictedDistance(InitialSpeed, TargetSpeed,
				Acceleration, Time);
	}
}

float UMHGZMotionMatchingAnimInstance::SelectStartAcceleration(const bool bUnsheathed,
	const float TargetSpeed) const
{
	if (bUnsheathed)
	{
		return MMStartAccelerationUnsheathed;
	}

	const float WalkRunBoundary = 310.0f;
	const float RunSprintBoundary = 520.0f;
	if (TargetSpeed <= WalkRunBoundary)
	{
		return MMStartAccelerationWalk;
	}
	return TargetSpeed < RunSprintBoundary
		? MMStartAccelerationRun
		: MMStartAccelerationSprint;
}

float UMHGZMotionMatchingAnimInstance::SelectStopDeceleration(const bool bUnsheathed,
	const float ReferenceSpeed) const
{
	if (bUnsheathed)
	{
		return MMStopDecelerationRun;
	}

	if (ReferenceSpeed <= 310.0f)
	{
		return MMStopDecelerationWalk;
	}
	return ReferenceSpeed < 520.0f
		? MMStopDecelerationRun
		: MMStopDecelerationSprint;
}

float UMHGZMotionMatchingAnimInstance::SelectStartQueryDuration(const bool bUnsheathed,
	const float TargetSpeed) const
{
	if (bUnsheathed)
	{
		return 0.60f;
	}
	return TargetSpeed <= 310.0f ? 1.25f : 0.80f;
}
