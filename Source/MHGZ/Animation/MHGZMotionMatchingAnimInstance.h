// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNodeReference.h"
#include "Animation/AnimInstance.h"
#include "Animation/TrajectoryTypes.h"
#include "MHGZMotionMatchingAnimInstance.generated.h"

class AMHGZCharacter;
class UPoseSearchDatabase;

/** Identifies the existing forward-only locomotion Motion Matching nodes in ABP_MH_Character. */
UENUM(BlueprintType)
enum class EMHGZMotionMatchingNode : uint8
{
	Sheathed UMETA(DisplayName="Sheathed Locomotion"),
	Unsheathed UMETA(DisplayName="Unsheathed Locomotion")
};

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
	virtual void NativeUninitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	/**
	 * Called from a Motion Matching node's On Update Motion Matching State function after its
	 * search has completed. It always enqueues the latest runtime result; optional CSV output is
	 * still written on the game thread in NativeUpdateAnimation.
	 */
	UFUNCTION(BlueprintCallable, Category="Movement|MM|Debug", meta=(BlueprintThreadSafe))
	void QueueMotionMatchingSelection(const FAnimNodeReference& AnimNodeReference,
		EMHGZMotionMatchingNode MotionMatchingNode);

	/** Actual XY displacement of the actor, measured from Root Motion. Unit: cm/s. */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Query")
	float MMActualSpeed2D = 0.0f;

	/** Start=positive, cruise/idle=zero, stop=negative. */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Query")
	float MMIntentQuery = 0.0f;

	/** Negative remaining stop distance while stopping; zero otherwise. Unit: cm. */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Query")
	float MMDistanceToStopQuery = 0.0f;

	/**
	 * Sheathed Stop family query. Zero outside a Stop search; Walk/Run/Sprint
	 * use distinct positive lanes while a generated Stop is selected.
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Query")
	float MMStopGaitQuery = 0.0f;

	/**
	 * Active locomotion family query. While movement input exists, Walk/Run/Sprint
	 * use the same 1/3, 2/3 and 1 lanes as the indexed MM_MoveGait curve.
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Query")
	float MMMoveGaitQuery = 0.0f;

	/**
	 * True while an action Montage or movement lock owns Root Motion. The AnimGraph must use this
	 * to bypass the regular Motion Matching nodes with the matching Idle pose before Pose History.
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Query")
	bool bMMForceIdle = false;

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

	/**
	 * Briefly holds the LocomotionBase at Idle after a deadzone-crossing input.
	 * This lets an analogue stick reach its final Walk/Run/Sprint lane before a
	 * Start candidate is first selected, preventing a one-frame Walk Start hop.
	 */
	UPROPERTY(EditDefaultsOnly, Category="Movement|MM|Query", meta=(ClampMin="0.0"))
	float MMStartInputSettleDuration = 1.0f / 30.0f;

	UPROPERTY(EditDefaultsOnly, Category="Movement|MM|Query", meta=(ClampMin="0.0"))
	float MMIdleSpeedThreshold = 8.0f;

	UPROPERTY(EditDefaultsOnly, Category="Movement|MM|Query", meta=(ClampMin="0.0"))
	float MMTeleportResetDistance = 200.0f;

	/**
	 * Keeps the AnimGraph on its matching Idle pose briefly after an action releases
	 * bForceMMIdle. This absorbs the final Montage Root Motion sample before regular
	 * Motion Matching is allowed to measure or output locomotion again.
	 */
	UPROPERTY(EditDefaultsOnly, Category="Movement|MM|Query", meta=(ClampMin="0.0"))
	float MMForceIdleReleaseHoldDuration = 0.05f;

private:
	enum class EMHGZSheathedStopMode : uint8
	{
		None,
		AwaitingExtendedStopCandidate,
		FollowingExtendedStopCurve
	};

	enum class EMHGZLegacyStopMode : uint8
	{
		None,
		AwaitingStopCandidate,
		FollowingStopCurve
	};

	enum class EMHGZStopGait : uint8
	{
		None,
		Walk,
		Run,
		Sprint
	};

	TWeakObjectPtr<AMHGZCharacter> CachedCharacter;
	FVector PreviousActorLocation = FVector::ZeroVector;
	bool bHasPreviousActorLocation = false;
	bool bStartQueryActive = false;
	bool bStartQueryObservedStart = false;
	bool bHadMoveInput = false;
	bool bHasPreviousUnsheathedState = false;
	bool bPreviousUnsheathedState = false;
	bool bWasForceMMIdle = false;
	bool bStartInputSettling = false;
	float MMForceIdleReleaseHoldRemaining = 0.0f;
	float MMStartInputSettleRemaining = 0.0f;
	float MMStartQueryElapsed = 0.0f;
	float MMStartQueryDuration = 0.0f;
	EMHGZSheathedStopMode SheathedStopMode = EMHGZSheathedStopMode::None;
	EMHGZLegacyStopMode LegacyStopMode = EMHGZLegacyStopMode::None;
	EMHGZStopGait ActiveSheathedGait = EMHGZStopGait::None;
	EMHGZStopGait LatchedSheathedStopGait = EMHGZStopGait::None;
	/** The one Stop selection accepted by the current release edge. */
	TWeakObjectPtr<UObject> ActiveSheathedStopAnimation;
	uint64 LastSheathedStopSelectionFrame = 0;
	float LastSheathedStopAnimationTime = 0.0f;

	/** Runtime-only state for the opt-in CSV telemetry controlled by mhgz.Telemetry.Enable. */
	bool bRuntimeTelemetryActive = false;
	bool bRuntimeTelemetryFailed = false;
	float RuntimeTelemetryElapsed = 0.0f;
	FString RuntimeTelemetrySessionDirectory;
	FString RuntimeTelemetryRawInputFilePath;
	FString RuntimeTelemetryParsedInputFilePath;
	FString RuntimeTelemetryCharacterStateFilePath;
	FString RuntimeTelemetryCharacterSpatialFilePath;
	FString RuntimeTelemetryMMQueryFilePath;
	FString RuntimeTelemetryMMSelectionFilePath;
	FString RuntimeTelemetryAnimationFilePath;
	FString RuntimeTelemetryPoseSearchTraceFilePath;
	FString RuntimeTelemetryPoseSearchDetailDirectory;
	bool bRuntimeTelemetryPoseSearchTraceActive = false;
	bool bRuntimeTelemetryPoseSearchDetailRequested = false;
	int32 RuntimeTelemetryPoseSearchTopN = 0;
	TArray<FString> RuntimeTelemetryRawInputPendingRows;
	TArray<FString> RuntimeTelemetryParsedInputPendingRows;
	TArray<FString> RuntimeTelemetryCharacterStatePendingRows;
	TArray<FString> RuntimeTelemetryCharacterSpatialPendingRows;
	TArray<FString> RuntimeTelemetryMMQueryPendingRows;
	TArray<FString> RuntimeTelemetryMMSelectionPendingRows;
	TArray<FString> RuntimeTelemetryAnimationPendingRows;
	uint64 RuntimeTelemetryLastObservedInputEventSerial = 0;

	struct FMotionMatchingSelectionEvent
	{
		uint64 Frame = 0;
		EMHGZMotionMatchingNode Node = EMHGZMotionMatchingNode::Sheathed;
		TWeakObjectPtr<const UPoseSearchDatabase> Database;
		TWeakObjectPtr<UObject> Animation;
		float AnimationTime = 0.0f;
		float SearchCost = 0.0f;
		float WantedPlayRate = 1.0f;
		bool bIsContinuing = false;
		bool bIsLooping = false;
		bool bIsMirrored = false;
	};
	TQueue<FMotionMatchingSelectionEvent, EQueueMode::Mpsc> MMPendingSelectionEvents;
	FMotionMatchingSelectionEvent MMLatestSelection[2];
	bool bHasLatestSelection[2] = { false, false };

	void ResetMMTemporalState(const FVector& CurrentLocation);
	void BuildFlatTrajectory(const FTransform& ActorTransform);
	void BuildPredictedTrajectory(const FTransform& ActorTransform, float InitialSpeed,
		float TargetSpeed, float Acceleration);
	void UpdateRuntimeTelemetry(const AMHGZCharacter* Character, float DeltaSeconds);
	bool StartRuntimeTelemetry(const AMHGZCharacter* Character);
	void StopRuntimeTelemetry();
	void FlushRuntimeTelemetry();
	void CaptureParsedInputEvents(const AMHGZCharacter* Character, float WorldTimeSeconds);
	void DrainMMSelectionEvents();
	void DiscardMMSelectionEvents();
	void ClearMMRuntimeSelections();
	const FMotionMatchingSelectionEvent* GetLatestSelection(EMHGZMotionMatchingNode MotionMatchingNode) const;
	void UpdateStartIntentQuery(EMHGZMotionMatchingNode MotionMatchingNode, float DeltaSeconds);
	void UpdateSheathedIntentQueries(bool bHasInput, bool bInputStarted, bool bInputReleased,
		float DeltaSeconds, float TargetSpeed);
	void UpdateLegacyIntentQueries(bool bHasInput, bool bInputStarted, bool bInputReleased,
		float DeltaSeconds);
	void ResetSheathedStopRequest();
	void BeginFollowingSheathedStop(const FMotionMatchingSelectionEvent& Selection);
	bool IsContinuingAcceptedSheathedStop(const FMotionMatchingSelectionEvent& Selection);
	bool IsStartAnimation(const FMotionMatchingSelectionEvent& Selection) const;
	bool IsExtendedStopAnimation(const FMotionMatchingSelectionEvent& Selection) const;
	bool IsLegacyStopAnimation(const FMotionMatchingSelectionEvent& Selection) const;
	EMHGZStopGait GetGaitFromSelection(const FMotionMatchingSelectionEvent* Selection) const;
	EMHGZStopGait GetGaitFromTargetSpeed(float TargetSpeed) const;
	static float GetStopGaitCurveValue(EMHGZStopGait Gait);
	float EvaluateSelectedCurve(const FMotionMatchingSelectionEvent& Selection, FName CurveName) const;
	float GetPredictedForwardDistance(float TimeInSeconds, const FVector& Origin,
		const FVector& TrajectoryForward) const;
	float SelectStartAcceleration(bool bUnsheathed, float TargetSpeed) const;
	float SelectStopDeceleration(bool bUnsheathed, float ReferenceSpeed) const;
};
