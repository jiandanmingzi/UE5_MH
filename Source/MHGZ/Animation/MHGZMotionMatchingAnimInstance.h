// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "Animation/AnimExecutionContext.h"
#include "Animation/AnimNodeReference.h"
#include "Animation/AnimInstance.h"
#include "Animation/MHGZMotionMatchingMath.h"
#include "Animation/MHGZLocomotionPhase.h"
#include "Animation/MHGZMontageSegmentBlend.h"
#include "Animation/TrajectoryTypes.h"
#include "WeaponRuntime/MHGZWeaponRuntimeTypes.h"
#include "MHGZMotionMatchingAnimInstance.generated.h"

class AMHGZCharacter;
class UPoseSearchDatabase;
class UAnimSequenceBase;
class UMHGZInstrumentedCharacterMovementComponent;
struct FMotionMatchingAnimNodeReference;
class FStructOnScope;

/** Identifies the existing forward-only locomotion Motion Matching nodes in ABP_MH_Character. */
UENUM(BlueprintType)
enum class EMHGZMotionMatchingNode : uint8
{
	Sheathed UMETA(DisplayName="Sheathed Locomotion"),
	Unsheathed UMETA(DisplayName="Unsheathed Locomotion")
};

/**
 * Candidate-set state owned by the AnimInstance for an action-to-locomotion
 * transition. It is intentionally a routing state, not an output-pose hold:
 * every value selects which Pose Search databases the next MM search may use.
 */
UENUM(BlueprintType)
enum class EMHGZMMActionExitRoute : uint8
{
	Move UMETA(DisplayName="Normal Move"),
	ActionIdle UMETA(DisplayName="Action Idle"),
	AwaitExitEntry UMETA(DisplayName="Await Exit Entry"),
	ExitOnly UMETA(DisplayName="Exit Only"),
	ExitAndMove UMETA(DisplayName="Exit And Move")
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
	friend class FMHGZMobileExitRouteTest;
	friend class FMHGZLoopContinuityTest;
	friend class FMHGZLocomotionEntryTest;
	friend class FMHGZLocomotionSpeedTest;
	friend class FMHGZDirectionalDodgeStartTest;

public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUninitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	/** One-shot movement takeover from an exact directional IdleExit; not a mobile Exit/Loop handoff. */
	bool RequestDirectionalDodgeLocomotionStart(const FWeaponActionToken& SourceAction);

	/** 蒙太奇内部混合请求的诊断计数；每个实际请求的Slot组递增一次，不按混合帧递增。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Animation|Montage Segment Blend")
	int64 MontageSegmentBlendRequestSerial = 0;

	/** DefaultSlot惯性混合后的持械挂点校正，仅由显式接缝元数据授权。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Animation|Weapon Grip")
	float WeaponGripConstraintAlpha = 0.f;

	/** 持械接缝约束的更新序号，供诊断辨认新的约束请求。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Animation|Weapon Grip")
	int64 WeaponGripConstraintSerial = 0;

	/** Converts a world-facing correction into the current Mesh component basis. */
	static bool BuildActionVisualPitchComponentRotation(const FVector& FacingWorld,
		const FVector& TargetWorld, const FQuat& MeshWorldRotation,
		FQuat& OutComponentRotation);
	/**
	 * Captures the graph-proxy Root Motion immediately after AnimGraph evaluation.
	 * This runs before CMC consumes the generated transform, so it distinguishes
	 * a zero graph output from a later movement-consumption failure.
	 */
	virtual void NativePostEvaluateAnimation() override;

	/**
	 * Called from a Motion Matching node's On Update Motion Matching State function after its
	 * search has completed. It always enqueues the latest runtime result; optional CSV output is
	 * still written on the game thread in NativeUpdateAnimation.
	 */
	UFUNCTION(BlueprintCallable, Category="Movement|MM|Debug", meta=(BlueprintThreadSafe))
	void QueueMotionMatchingSelection(const FAnimNodeReference& AnimNodeReference,
		EMHGZMotionMatchingNode MotionMatchingNode);

	/** Runs before the sheathed MM node searches and installs its candidate set for this frame. */
	UFUNCTION(BlueprintCallable, Category="Movement|MM|Action Exit", meta=(BlueprintThreadSafe))
	void OnSheathedMotionMatchingPreUpdate(const FAnimUpdateContext& Context,
		const FAnimNodeReference& AnimNodeReference);

	/** Runs before the unsheathed MM node searches and installs its candidate set for this frame. */
	UFUNCTION(BlueprintCallable, Category="Movement|MM|Action Exit", meta=(BlueprintThreadSafe))
	void OnUnsheathedMotionMatchingPreUpdate(const FAnimUpdateContext& Context,
		const FAnimNodeReference& AnimNodeReference);

	/** Runs after the sheathed search. It records the real winner and acknowledges an entered Exit PSD. */
	UFUNCTION(BlueprintCallable, Category="Movement|MM|Action Exit", meta=(BlueprintThreadSafe))
	void OnSheathedMotionMatchingStateUpdated(const FAnimUpdateContext& Context,
		const FAnimNodeReference& AnimNodeReference);

	/** Runs after the unsheathed search. It records the real winner and acknowledges an entered Exit PSD. */
	UFUNCTION(BlueprintCallable, Category="Movement|MM|Action Exit", meta=(BlueprintThreadSafe))
	void OnUnsheathedMotionMatchingStateUpdated(const FAnimUpdateContext& Context,
		const FAnimNodeReference& AnimNodeReference);

	/**
	 * Called by E4.2's On Motion Matching State Updated graph after it has routed a
	 * compatible handoff serial into its Exit Pose Search Database. The callback may
	 * execute off the game thread, so it only queues the acknowledgement; the Host
	 * payload is cleared later from NativeUpdateAnimation.
	 */
	UFUNCTION(BlueprintCallable, Category="Movement|MM|Handoff", meta=(BlueprintThreadSafe))
	void QueueMotionMatchingHandoffConsumption(int64 HandoffSerial);

	/**
	 * Called by the AnimGraph after it has installed the one-shot idle-only
	 * candidate context published when a non-handoff action ends without input.
	 * The callback can run off the game thread, so acknowledgement is drained on
	 * the next NativeUpdateAnimation just like a normal Handoff acknowledgement.
	 */
	UFUNCTION(BlueprintCallable, Category="Movement|MM|Action Exit", meta=(BlueprintThreadSafe))
	void QueueMotionMatchingActionIdleContextConsumption(int64 ContextSerial);

	/** 根据角色实际根运动位移测得的水平速度，单位为厘米每秒。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Query")
	float MMActualSpeed2D = 0.0f;

	/** 平滑后的持刀有效移速倍率，包含三灯替换效果。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Speed")
	float MMUnsheathedMoveSpeedMultiplier = 1.0f;

	/** 真正应用到普通持刀MM的额外播放倍率；动作退出段和待机仍保持1。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Speed")
	float MMUnsheathedAppliedSpeedMultiplier = 1.0f;

	/** 按源动画及数据库原始时间尺度表示的查询速度，不是加速后的世界速度。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Speed")
	float MMQuerySpeed2D = 0.0f;

	/** 当前巡航挡位在移速倍率作用后的预期世界速度，单位为厘米每秒。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Speed")
	float MMExpectedWorldCruiseSpeed = 0.0f;

	/** 仅平滑移速增益获得或消失时的倍率变化，不缓存移动输入或重新启动动画播放器。 */
	UPROPERTY(EditDefaultsOnly, Category="Movement|MM|Speed", meta=(ClampMin="0.0"))
	float MMUnsheathedSpeedInterpSpeed = 20.0f;

	float GetLocomotionPlaybackRateScale(bool bUnsheathed) const
	{ return bUnsheathed ? MMUnsheathedAppliedSpeedMultiplier : 1.0f; }

	/** 移动意图查询值：起步为正、巡航或待机为零、停步为负。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Query")
	float MMIntentQuery = 0.0f;

	/** 停步期间的负剩余距离，单位为厘米；非停步时为零。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Query")
	float MMDistanceToStopQuery = 0.0f;

	/** 收刀停步搜索的挡位特征；非停步时为零，Walk、Run、Sprint使用不同正值通道。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Query")
	float MMStopGaitQuery = 0.0f;

	/** 有效移动输入对应的挡位特征；Walk、Run、Sprint分别与索引曲线的1/3、2/3、1通道一致。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Query")
	float MMMoveGaitQuery = 0.0f;

	/** 持续循环时钟或退出段一次性脚相匹配给出的精确循环相位查询。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Query")
	FVector2D MMLoopPhaseQuery = FVector2D::ZeroVector;
	/** 持刀循环节点使用的独立脚相查询值。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Query")
	FVector2D MMUnsheathedLoopPhaseQuery = FVector2D::ZeroVector;

	/** Worker-thread search context only. Never changes animation data or delays an input. */
	const UPoseSearchDatabase* GetLoopQueryDatabase(bool bUnsheathed) const;
	bool WantsLoopEntryMatch(bool bUnsheathed) const;
	void PublishLoopPhaseQuery(bool bUnsheathed, const FVector2D& Phase, FName Source);

	/** 动作蒙太奇或移动锁拥有根运动时，普通MM旁路为对应待机姿势，再进入姿势历史。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Query")
	bool bMMForceIdle = false;

	/** 经过禁止移动过滤后，当前是否仍有可执行的普通移动输入。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Query")
	bool bMMHasLocomotionInput = false;

	/** 操虫动作的原始摇杆分层开关；为真时Spine_00以下保持普通移动。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Animation|Insect Glaive")
	bool bKinsectUpperBodyOnly = false;

	/** 普通移动当前是否持刀，用于让正确的MM节点消费交接。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Query")
	bool bMMUnsheathedLocomotion = false;

	/** 是否存在一次由玩家实际松开输入建立的有效停步请求。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Query")
	bool bMMStopRequestActive = false;

	/** 退出尾段是否允许正常移动候选；仅持续移动输入或有效停步请求可打开。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Query")
	bool bMMExitWantsMoveDatabase = false;

	/** 动作无有效移动输入结束时的一次性待机候选路由；只选待机数据库，不锁住输出姿势或制造输入死区。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Action Exit")
	bool bMMActionIdleContextActive = false;

	/** 本次动作结束待机路由属于持刀还是收刀状态。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Action Exit")
	bool bMMActionIdleContextUnsheathed = false;

	/** 动作结束待机上下文的发布序号，防止重复消费。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Action Exit")
	int64 MMActionIdleContextSerial = 0;

	/** 收刀MM回调本帧是否进入动作结束后的待机专用搜索。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Action Exit")
	bool bMMRouteSheathedActionIdle = false;

	/** 持刀MM回调本帧是否进入动作结束后的待机专用搜索。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Action Exit")
	bool bMMRouteUnsheathedActionIdle = false;

	/** 收刀普通移动节点当前使用的动作退出搜索路由。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Action Exit")
	EMHGZMMActionExitRoute MMSheathedActionExitRoute = EMHGZMMActionExitRoute::Move;

	/** 持刀普通移动节点当前使用的动作退出搜索路由。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Action Exit")
	EMHGZMMActionExitRoute MMUnsheathedActionExitRoute = EMHGZMMActionExitRoute::Move;

	/** 收刀退出路由当前持有的交接序号；零表示没有退出路由。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Action Exit")
	int64 MMSheathedActionExitSerial = 0;

	/** 持刀退出路由当前持有的交接序号；零表示没有退出路由。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Action Exit")
	int64 MMUnsheathedActionExitSerial = 0;

	/** 宿主是否发布了有效动作退出交接；路由只选择候选家族，不直接选择动画。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Handoff")
	bool bMMHandoffActive = false;

	/** 当前动作交给MM搜索的退出候选家族。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Handoff")
	EMHGZMotionMatchingHandoffType MMHandoffType = EMHGZMotionMatchingHandoffType::None;

	/** 当前MM交接序号，用于一次性消费与旧载荷隔离。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Handoff")
	int64 MMHandoffSerial = 0;

	/** 交接时锁定的原始物理摇杆轴值。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Handoff")
	FVector2D MMHandoffRawMoveInput = FVector2D::ZeroVector;

	/** 交接时是否存在有效物理移动输入。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Handoff")
	bool bMMHandoffHasRawMoveInput = false;

	/** 动作可移动尾段是否曾出现有效物理输入。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Handoff")
	bool bMMHandoffHadRawMoveInputInMobilePhase = false;

	/** 交接是否携带可移动尾段内真实松杆产生的待停步请求。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Handoff")
	bool bMMPendingStopAtHandoff = false;

	/** 交接前可移动尾段最后有效摇杆输入对应的巡航速度，单位为厘米每秒。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Handoff")
	float MMHandoffLastActiveRawMoveCruiseSpeed = 0.0f;

	/** 上次非零且具有动画支撑的目标巡航速度，单位为厘米每秒。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Query")
	float MMLastNonZeroCruiseSpeed = 0.0f;

	/** 当前样本及向前0.2、0.5、0.8、1.0秒的移动预测轨迹。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Movement|MM|Query")
	FTransformTrajectory MMPredictedTrajectory;

	/** 当前是否存在由精确动作身份拥有的视觉俯仰，包含只读淡出阶段。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Animation|Action Pitch")
	bool bActionVisualPitchActive = false;

	/** 锁定方向的仰角诊断值，单位为度；不可直接接入局部骨骼俯仰。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Animation|Action Pitch")
	float ActionVisualPitchDegrees = 0.0f;

	/** 动画图使用的平滑视觉俯仰权重，取值0到1。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Animation|Action Pitch")
	float ActionVisualPitchWeight = 0.0f;

	/** 最终根骨骼修改节点使用的带权组件空间附加旋转。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Animation|Action Pitch")
	FRotator ActionVisualPitchComponentRotation = FRotator::ZeroRotator;

	/** 收刀Walk起步查询使用的加速度，单位为厘米每平方秒。 */
	UPROPERTY(EditDefaultsOnly, Category="Movement|MM|Query|Acceleration", meta=(ClampMin="1.0"))
	float MMStartAccelerationWalk = 950.0f;

	/** 收刀Run起步查询使用的加速度，单位为厘米每平方秒。 */
	UPROPERTY(EditDefaultsOnly, Category="Movement|MM|Query|Acceleration", meta=(ClampMin="1.0"))
	float MMStartAccelerationRun = 1000.0f;

	/** 收刀Sprint起步查询使用的加速度，单位为厘米每平方秒。 */
	UPROPERTY(EditDefaultsOnly, Category="Movement|MM|Query|Acceleration", meta=(ClampMin="1.0"))
	float MMStartAccelerationSprint = 700.0f;

	/** 持刀起步查询使用的加速度，单位为厘米每平方秒。 */
	UPROPERTY(EditDefaultsOnly, Category="Movement|MM|Query|Acceleration", meta=(ClampMin="1.0"))
	float MMStartAccelerationUnsheathed = 1750.0f;

	/** Walk停步距离估计使用的减速度幅值，单位为厘米每平方秒。 */
	UPROPERTY(EditDefaultsOnly, Category="Movement|MM|Query|Deceleration", meta=(ClampMin="1.0"))
	float MMStopDecelerationWalk = 250.0f;

	/** Run停步距离估计使用的减速度幅值，单位为厘米每平方秒。 */
	UPROPERTY(EditDefaultsOnly, Category="Movement|MM|Query|Deceleration", meta=(ClampMin="1.0"))
	float MMStopDecelerationRun = 1900.0f;

	/** Sprint停步距离估计使用的减速度幅值，单位为厘米每平方秒。 */
	UPROPERTY(EditDefaultsOnly, Category="Movement|MM|Query|Deceleration", meta=(ClampMin="1.0"))
	float MMStopDecelerationSprint = 3000.0f;

	/** 允许进入起步候选的速度阈值，单位为厘米每秒。 */
	UPROPERTY(EditDefaultsOnly, Category="Movement|MM|Query", meta=(ClampMin="0.0"))
	float MMStartEligibilitySpeed = 60.0f;

	/** 摇杆刚越过死区后的短暂挡位稳定等待，单位为秒；避免先选一帧Walk起步再改挡。 */
	UPROPERTY(EditDefaultsOnly, Category="Movement|MM|Query", meta=(ClampMin="0.0"))
	float MMStartInputSettleDuration = 1.0f / 30.0f;

	/** 进入待机判断使用的水平速度阈值，单位为厘米每秒。 */
	UPROPERTY(EditDefaultsOnly, Category="Movement|MM|Query", meta=(ClampMin="0.0"))
	float MMIdleSpeedThreshold = 8.0f;

	/** 超过该帧位移时按传送处理并重置移动历史，单位为厘米。 */
	UPROPERTY(EditDefaultsOnly, Category="Movement|MM|Query", meta=(ClampMin="0.0"))
	float MMTeleportResetDistance = 200.0f;

	/** 旧版动作后待机保持的诊断时长，单位为秒；仅开启mhgz.MM.PostActionIdleHold时读取，默认不启用。 */
	UPROPERTY(EditDefaultsOnly, Category="Movement|MM|Query", meta=(ClampMin="0.0"))
	float MMForceIdleReleaseHoldDuration = 0.05f;

protected:
	/** After real Montage advancement, before UE copies/clears requests for the graph. */
	virtual void Montage_Advance(float DeltaSeconds) override;

private:
	/** Previous graph-update sample, also detects section seeks performed between updates. */
	TArray<FMHGZMontageSegmentBlendSample> LastMontageSegmentBlendSamples;
	FMHGZWeaponGripConstraint WeaponGripConstraint;

	void UpdateActionVisualPitch(const AMHGZCharacter* Character, float DeltaSeconds);
	void ResetActionVisualPitchLocalState();

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
	FWeaponActionVisualPitchState CachedActionVisualPitchState;
	uint64 LastActionVisualPitchRevision = 0;
	FQuat ActionVisualPitchTransitionStart = FQuat::Identity;
	FQuat ActionVisualPitchTransitionTarget = FQuat::Identity;
	float ActionVisualPitchTransitionElapsed = 0.0f;
	float ActionVisualPitchTransitionDuration = 0.0f;
	float ActionVisualPitchTransitionStartWeight = 0.0f;
	float ActionVisualPitchTransitionTargetWeight = 0.0f;
	bool bActionVisualPitchTransitionActive = false;
	FVector PreviousActorLocation = FVector::ZeroVector;
	bool bHasPreviousActorLocation = false;
	bool bStartQueryActive = false;
	bool bStartQueryObservedStart = false;
	bool bHadMoveInput = false;
	bool bHasPreviousUnsheathedState = false;
	bool bPreviousUnsheathedState = false;
	bool bWasForceMMIdle = false;
	bool bStartInputSettling = false;
	FWeaponActionToken PendingDirectionalDodgeStart;
	/** Consumed only by the unsheathed pre-search callback, never a persistent forced-search mode. */
	bool bMMForceDirectionalStartSearch = false;
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
	/** The one legacy Unsheathed Stop selection accepted by the current release edge. */
	TWeakObjectPtr<UObject> ActiveLegacyStopAnimation;
	uint64 LastLegacyStopSelectionFrame = 0;
	float LastLegacyStopAnimationTime = 0.0f;

	/** Runtime-only state for the opt-in CSV telemetry controlled by mhgz.Telemetry.Enable. */
	bool bRuntimeTelemetryActive = false;
	bool bRuntimeTelemetryFailed = false;
	float RuntimeTelemetryElapsed = 0.0f;
	FString RuntimeTelemetrySessionDirectory;
	FString RuntimeTelemetryRawInputFilePath;
	FString RuntimeTelemetryParsedInputFilePath;
	FString RuntimeTelemetryCharacterStateFilePath;
	FString RuntimeTelemetryCharacterSpatialFilePath;
	FString RuntimeTelemetryMovementPhasesFilePath;
	FString RuntimeTelemetryRootMotionSourcesFilePath;
	FString RuntimeTelemetryCapsuleHitsFilePath;
	FString RuntimeTelemetryCombatEventsFilePath;
	FString RuntimeTelemetryUIEventsFilePath;
	FString RuntimeTelemetryMMQueryFilePath;
	FString RuntimeTelemetryMMSelectionFilePath;
	FString RuntimeTelemetryAnimationFilePath;
	FString RuntimeTelemetryActionVisualPitchEventsFilePath;
	FString RuntimeTelemetryAnimGraphRootMotionFilePath;
	FString RuntimeTelemetryMontageInstancesFilePath;
	FString RuntimeTelemetryPoseSearchTraceFilePath;
	FString RuntimeTelemetryPoseSearchDetailDirectory;
	bool bRuntimeTelemetryPoseSearchTraceActive = false;
	bool bRuntimeTelemetryPoseSearchDetailRequested = false;
	int32 RuntimeTelemetryPoseSearchTopN = 0;
	TArray<FString> RuntimeTelemetryRawInputPendingRows;
	TArray<FString> RuntimeTelemetryParsedInputPendingRows;
	TArray<FString> RuntimeTelemetryCharacterStatePendingRows;
	TArray<FString> RuntimeTelemetryCharacterSpatialPendingRows;
	TArray<FString> RuntimeTelemetryMovementPhasePendingRows;
	TArray<FString> RuntimeTelemetryRootMotionSourcesPendingRows;
	TArray<FString> RuntimeTelemetryCapsuleHitsPendingRows;
	TArray<FString> RuntimeTelemetryCombatEventsPendingRows;
	TArray<FString> RuntimeTelemetryUIEventsPendingRows;
	TArray<FString> RuntimeTelemetryMMQueryPendingRows;
	TArray<FString> RuntimeTelemetryMMSelectionPendingRows;
	TArray<FString> RuntimeTelemetryAnimationPendingRows;
	TArray<FString> RuntimeTelemetryActionVisualPitchEventsPendingRows;
	TArray<FString> RuntimeTelemetryAnimGraphRootMotionPendingRows;
	TArray<FString> RuntimeTelemetryMontageInstancesPendingRows;
	uint64 RuntimeTelemetryLastObservedInputEventSerial = 0;
	uint64 RuntimeTelemetryLastObservedCapsuleHitSerial = 0;
	uint64 RuntimeTelemetryLastObservedCombatEventSerial = 0;
	uint64 RuntimeTelemetryLastObservedUIEventSerial = 0;
	TWeakObjectPtr<class AMHGZPlayerController> RuntimeTelemetryUIEventSource;
	uint64 RuntimeTelemetryLastObservedMovementPhaseSerial = 0;
	uint64 RuntimeTelemetryLastObservedActionVisualPitchEventSerial = 0;
	/**
	 * Previous frame's CMC velocity, used to derive the Spatial row's acceleration.
	 *
	 * `UCharacterMovementComponent::Acceleration` is only ever written from player
	 * input (ControlledCharacterMove -> ScaleInputAcceleration(ConsumeInputVector)),
	 * and this project drives the character entirely through root motion sources --
	 * AddMovementInput/AddInputVector/ConsumeInputVector have zero call sites.  That
	 * makes GetCurrentAcceleration() identically zero for this character, not racy:
	 * rows in Walking with Velocity2D up to 339 cm/s still log zero.  Differencing
	 * the velocity the telemetry already trusts is the only reading that moves.
	 */
	FVector RuntimeTelemetryPrevVelocity = FVector::ZeroVector;
	bool bRuntimeTelemetryHasPrevVelocity = false;

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
		FVector2D LoopPhaseQuery = FVector2D::ZeroVector;
		FName LoopQuerySource;
		int32 LoopHistoryEntriesRemoved = 0;
		int32 PreBlendActivePlayers = 0;
	};
	TQueue<FMotionMatchingSelectionEvent, EQueueMode::Mpsc> MMPendingSelectionEvents;
	FMotionMatchingSelectionEvent MMLatestSelection[2];
	bool bHasLatestSelection[2] = { false, false };
	/** Serials accepted by the AnimGraph, awaiting game-thread Host acknowledgement. */
	TQueue<int64, EQueueMode::Mpsc> MMPendingHandoffConsumptionSerials;
	/** Serials consumed by the AnimGraph, awaiting game-thread acknowledgement. */
	TQueue<int64, EQueueMode::Mpsc> MMPendingActionIdleContextConsumptionSerials;
	int64 NextMMActionIdleContextSerial = 1;

	/** The last target gait and installed bank for the narrow Run/Sprint Loop route. */
	MHGZMotionMatching::EMHGZNormalMoveGait MMSheathedTargetMoveGait =
		MHGZMotionMatching::EMHGZNormalMoveGait::None;
	MHGZMotionMatching::EMHGZNormalMoveCandidateSet MMSheathedNormalMoveCandidateSet =
		MHGZMotionMatching::EMHGZNormalMoveCandidateSet::FullMove;

	/** 收刀普通移动数据库；在游戏线程加载，工作线程回调只读。 */
	UPROPERTY(Transient)
	TObjectPtr<UPoseSearchDatabase> MMSheathedMoveDatabase;
	/** 收刀Run专用循环候选数据库。 */
	UPROPERTY(Transient)
	TObjectPtr<UPoseSearchDatabase> MMSheathedRunLoopOnlyDatabase;
	/** 收刀Sprint专用循环候选数据库。 */
	UPROPERTY(Transient)
	TObjectPtr<UPoseSearchDatabase> MMSheathedSprintLoopOnlyDatabase;
	/** 收刀Walk专用循环候选数据库。 */
	UPROPERTY(Transient)
	TObjectPtr<UPoseSearchDatabase> MMSheathedWalkLoopOnlyDatabase;
	/** 持刀巡航专用循环候选数据库。 */
	UPROPERTY(Transient)
	TObjectPtr<UPoseSearchDatabase> MMUnsheathedLoopOnlyDatabase;
	/** 收刀Run脚相对齐使用的原循环序列引用。 */
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequenceBase> MMSheathedRunLoopSequence;
	/** 收刀Sprint脚相对齐使用的原循环序列引用。 */
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequenceBase> MMSheathedSprintLoopSequence;
	/** 持刀普通移动数据库的已加载引用。 */
	UPROPERTY(Transient)
	TObjectPtr<UPoseSearchDatabase> MMUnsheathedMoveDatabase;
	/** 收刀动作结束后仅包含待机候选的数据库。 */
	UPROPERTY(Transient)
	TObjectPtr<UPoseSearchDatabase> MMSheathedActionIdleDatabase;
	/** 持刀动作结束后仅包含待机候选的数据库。 */
	UPROPERTY(Transient)
	TObjectPtr<UPoseSearchDatabase> MMUnsheathedActionIdleDatabase;
	/** 移动收刀退出段对应的收刀候选数据库。 */
	UPROPERTY(Transient)
	TObjectPtr<UPoseSearchDatabase> MMSheathedSheatheExitDatabase;
	/** 收刀翻滚退出段对应的候选数据库。 */
	UPROPERTY(Transient)
	TObjectPtr<UPoseSearchDatabase> MMSheathedDodgeExitDatabase;
	/** 持刀翻滚退出段对应的候选数据库。 */
	UPROPERTY(Transient)
	TObjectPtr<UPoseSearchDatabase> MMUnsheathedDodgeExitDatabase;

	EMHGZMotionMatchingHandoffType MMSheathedActionExitHandoffType =
		EMHGZMotionMatchingHandoffType::None;
	EMHGZMotionMatchingHandoffType MMUnsheathedActionExitHandoffType =
		EMHGZMotionMatchingHandoffType::None;
	float MMSheathedActionExitSelectedTime = 0.0f;
	float MMUnsheathedActionExitSelectedTime = 0.0f;
	/** Cruise context after Start or action Exit, cleared by release/new Start/action; never holds input. */
	bool bMMMobileExitCruise[2] = { false, false };
	UPoseSearchDatabase* GetMobileExitLoopDatabase(EMHGZMotionMatchingNode Node) const;
	FMHGZLocomotionPhaseClock MMLoopClock;
	uint64 MMLoopClockFrame = MAX_uint64;
	float MMNodeUpdateDelta[2] = { 0.f, 0.f };
	bool bMMBaseBlendCaptured[2] = { false, false };
	TSharedPtr<FStructOnScope> MMBaseBlend[2];
	/** 循环脚相查询需要保持加载的数据库引用集合。 */
	UPROPERTY(Transient)
	TObjectPtr<UPoseSearchDatabase> MMLoopQueryDatabases[2];
	bool bMMLoopEntryMatch[2] = { false, false };
	FName MMLoopQuerySource[2];
	int32 MMLoopHistoryEntriesRemoved[2] = { 0, 0 };
	bool bMMCanApplyUnsheathedSpeed = false;
	float MMBaseCruiseSpeed = 0.0f;
	void UpdateLocomotionSpeed(const AMHGZCharacter& Character, float DeltaSeconds);
	void ApplyUnsheathedLocomotionSpeed(const FAnimNodeReference& Reference);
	void PrepareLoopContinuity(const FAnimUpdateContext& Context, const FAnimNodeReference& Reference,
		EMHGZMotionMatchingNode Node);
	void InstallLoopDatabase(const FMotionMatchingAnimNodeReference& Reference,
		EMHGZMotionMatchingNode Node, UPoseSearchDatabase* Database, bool bEntryMatch);
	bool TryRouteStartToLoop(const FMotionMatchingAnimNodeReference& Reference, EMHGZMotionMatchingNode Node);
	void ObserveLoopContinuity(const FAnimNodeReference& Reference, EMHGZMotionMatchingNode Node);

	void ResetMMTemporalState(const FVector& CurrentLocation, bool bPreserveMoveInput = false);
	void BuildFlatTrajectory(const FTransform& ActorTransform);
	void BuildPredictedTrajectory(const FTransform& ActorTransform, float InitialSpeed,
		float TargetSpeed, float Acceleration);
	void UpdateMotionMatchingHandoff(const AMHGZCharacter* Character);
	void DrainMMHandoffConsumptions(AMHGZCharacter* Character);
	void DiscardMMHandoffConsumptions();
	void PublishActionIdleContext(bool bUnsheathed);
	void RefreshActionIdleRouteFlags();
	void DrainMMActionIdleContextConsumptions();
	void DiscardMMActionIdleContextConsumptions();
	void LoadActionExitDatabases();
	void LoadSheathedLoopCandidateDatabases();
	void ResetSheathedNormalMoveCandidateRouting();
	void ResetActionExitRouting();
	void RouteMotionMatchingPreSearch(const FAnimNodeReference& AnimNodeReference,
		EMHGZMotionMatchingNode MotionMatchingNode);
	void ObserveMotionMatchingPostSearch(const FAnimNodeReference& AnimNodeReference,
		EMHGZMotionMatchingNode MotionMatchingNode);
	UPoseSearchDatabase* GetMoveDatabase(EMHGZMotionMatchingNode MotionMatchingNode) const;
	UPoseSearchDatabase* GetSheathedLoopCandidateDatabase(
		MHGZMotionMatching::EMHGZNormalMoveCandidateSet CandidateSet) const;
	void RouteSheathedNormalMovePreSearch(const FMotionMatchingAnimNodeReference& MotionMatchingReference);
	UPoseSearchDatabase* GetActionIdleDatabase(EMHGZMotionMatchingNode MotionMatchingNode) const;
	UPoseSearchDatabase* GetExitDatabase(EMHGZMotionMatchingNode MotionMatchingNode,
		EMHGZMotionMatchingHandoffType HandoffType) const;
	float GetExitDuration(EMHGZMotionMatchingNode MotionMatchingNode,
		EMHGZMotionMatchingHandoffType HandoffType) const;
	EMHGZMMActionExitRoute& GetActionExitRoute(EMHGZMotionMatchingNode MotionMatchingNode);
	int64& GetActionExitSerial(EMHGZMotionMatchingNode MotionMatchingNode);
	EMHGZMotionMatchingHandoffType& GetActionExitHandoffType(
		EMHGZMotionMatchingNode MotionMatchingNode);
	float& GetActionExitSelectedTime(EMHGZMotionMatchingNode MotionMatchingNode);
	void EnterActionIdleRoute(EMHGZMotionMatchingNode MotionMatchingNode);
	void ClearActionIdleRoute(EMHGZMotionMatchingNode MotionMatchingNode);
	void UpdateRuntimeTelemetry(const AMHGZCharacter* Character, float DeltaSeconds);
	void CaptureCombatTelemetryEvents(const AMHGZCharacter* Character);
	void CaptureUITelemetryEvents(const AMHGZCharacter* Character);
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
	void ResetLegacyStopRequest();
	void BeginFollowingLegacyStop(const FMotionMatchingSelectionEvent& Selection);
	bool IsContinuingAcceptedLegacyStop(const FMotionMatchingSelectionEvent& Selection);
	bool IsStartAnimation(const FMotionMatchingSelectionEvent& Selection) const;
	bool IsExtendedStopAnimation(const FMotionMatchingSelectionEvent& Selection) const;
	bool IsLegacyStopAnimation(const FMotionMatchingSelectionEvent& Selection) const;
	EMHGZStopGait GetGaitFromSelection(const FMotionMatchingSelectionEvent* Selection) const;
	EMHGZStopGait GetGaitFromTargetSpeed(float TargetSpeed) const;
	static MHGZMotionMatching::EMHGZNormalMoveGait ToNormalMoveGait(EMHGZStopGait Gait);
	MHGZMotionMatching::EMHGZNormalMoveGait GetExactSheathedLoopGait(
		const FMotionMatchingSelectionEvent* Selection) const;
	static float GetStopGaitCurveValue(EMHGZStopGait Gait);
	float EvaluateSelectedCurve(const FMotionMatchingSelectionEvent& Selection, FName CurveName) const;
	float GetPredictedForwardDistance(float TimeInSeconds, const FVector& Origin,
		const FVector& TrajectoryForward) const;
	float SelectStartAcceleration(bool bUnsheathed, float TargetSpeed) const;
	float SelectStopDeceleration(bool bUnsheathed, float ReferenceSpeed) const;
};
