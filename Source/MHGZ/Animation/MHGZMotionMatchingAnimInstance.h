// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/TrajectoryTypes.h"
#include "MHGZMotionMatchingAnimInstance.generated.h"

class AMHGZCharacter;

/**
 * Query producer for the forward-only, Root-Motion Motion Matching locomotion path.
 *
 * This class never selects or plays an animation. It only exposes values consumed by the
 * Pose History trajectory and the two Pose Search Curve Channels after PMM-2.
 */
UCLASS(Blueprintable, Transient)
class MHGZ_API UMHGZMotionMatchingAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	/** Actual XY displacement of the actor, measured from Root Motion. Unit: cm/s. */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Query")
	float MMActualSpeed2D = 0.0f;

	/** Start=positive, cruise/idle=zero, stop=negative. */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Query")
	float MMIntentQuery = 0.0f;

	/** Negative remaining stop distance while stopping; zero otherwise. Unit: cm. */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Query")
	float MMDistanceToStopQuery = 0.0f;

	/** Last non-zero animation-backed target cruise speed. Unit: cm/s. */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Query")
	float MMLastNonZeroCruiseSpeed = 0.0f;

	/** Current sample plus the 0.2/0.5/0.8/1.0 second forward-only predictions. */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Query")
	FTransformTrajectory MMPredictedTrajectory;

	UPROPERTY(EditDefaultsOnly, Category="Movement|MM|Query|Acceleration", meta=(ClampMin="1.0"))
	float MMStartAccelerationWalk = 950.0f;

	UPROPERTY(EditDefaultsOnly, Category="Movement|MM|Query|Acceleration", meta=(ClampMin="1.0"))
	float MMStartAccelerationRun = 1000.0f;

	UPROPERTY(EditDefaultsOnly, Category="Movement|MM|Query|Acceleration", meta=(ClampMin="1.0"))
	float MMStartAccelerationSprint = 700.0f;

	UPROPERTY(EditDefaultsOnly, Category="Movement|MM|Query|Acceleration", meta=(ClampMin="1.0"))
	float MMStartAccelerationUnsheathed = 1750.0f;

	UPROPERTY(EditDefaultsOnly, Category="Movement|MM|Query|Deceleration", meta=(ClampMin="1.0"))
	float MMStopDecelerationWalk = 250.0f;

	UPROPERTY(EditDefaultsOnly, Category="Movement|MM|Query|Deceleration", meta=(ClampMin="1.0"))
	float MMStopDecelerationRun = 1900.0f;

	UPROPERTY(EditDefaultsOnly, Category="Movement|MM|Query|Deceleration", meta=(ClampMin="1.0"))
	float MMStopDecelerationSprint = 3000.0f;

	UPROPERTY(EditDefaultsOnly, Category="Movement|MM|Query", meta=(ClampMin="0.0"))
	float MMStartEligibilitySpeed = 60.0f;

	UPROPERTY(EditDefaultsOnly, Category="Movement|MM|Query", meta=(ClampMin="0.0"))
	float MMIdleSpeedThreshold = 8.0f;

	UPROPERTY(EditDefaultsOnly, Category="Movement|MM|Query", meta=(ClampMin="0.0"))
	float MMTeleportResetDistance = 200.0f;

private:
	TWeakObjectPtr<AMHGZCharacter> CachedCharacter;
	FVector PreviousActorLocation = FVector::ZeroVector;
	bool bHasPreviousActorLocation = false;
	bool bStartQueryActive = false;
	bool bHadMoveInput = false;
	bool bWasForceMMIdle = false;
	float MMStartQueryElapsed = 0.0f;
	float MMStartQueryDuration = 0.0f;

	void ResetMMTemporalState(const FVector& CurrentLocation);
	void BuildFlatTrajectory(const FTransform& ActorTransform);
	void BuildPredictedTrajectory(const FTransform& ActorTransform, float InitialSpeed,
		float TargetSpeed, float Acceleration);
	float SelectStartAcceleration(bool bUnsheathed, float TargetSpeed) const;
	float SelectStopDeceleration(bool bUnsheathed, float ReferenceSpeed) const;
	float SelectStartQueryDuration(bool bUnsheathed, float TargetSpeed) const;
};
