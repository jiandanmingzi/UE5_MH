// Copyright MHGZ Project. All Rights Reserved.

// 动画更新、MM路由与结果队列消费；CSV记录由独立遥测实现处理。
#include "Animation/MHGZMotionMatchingAnimInstance.h"
#include "AbilitySystemComponent.h"
#include "ActionSystem/MHGZDodgeAbility.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/MHGZMotionMatchingMath.h"
#include "Animation/MHGZMontageSegmentBlend.h"
#include "Animation/MHGZPoseSearchFeatureChannel_LoopPhase.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "MHGZCharacter.h"
#include "MHGZ.h"
#include "PoseSearch/MotionMatchingAnimNodeLibrary.h"
#include "PoseSearch/AnimNode_MotionMatching.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchResult.h"
#include "PoseSearch/PoseSearchLibrary.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"
#include "UObject/StructOnScope.h"

namespace
{
constexpr float PredictionTimes[] = { 0.0f, 0.2f, 0.5f, 0.8f, 1.0f };
constexpr float ActionExitMoveTailSeconds = 0.08f;
constexpr float ActionExitEndEpsilonSeconds = 0.01f;

constexpr TCHAR SheathedMoveDatabasePath[] =
	TEXT("/Game/Blueprints/Characters/Demo/Animation/MotionMatching/PSD_MH_Shth_Move.PSD_MH_Shth_Move");
constexpr TCHAR SheathedRunLoopOnlyDatabasePath[] =
	TEXT("/Game/Blueprints/Characters/Demo/Animation/MotionMatching/PSD_MH_Shth_Run_LoopOnly.PSD_MH_Shth_Run_LoopOnly");
constexpr TCHAR SheathedSprintLoopOnlyDatabasePath[] =
	TEXT("/Game/Blueprints/Characters/Demo/Animation/MotionMatching/PSD_MH_Shth_Sprint_LoopOnly.PSD_MH_Shth_Sprint_LoopOnly");
constexpr TCHAR SheathedRunLoopSequencePath[] =
	TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Run_Loop.AS_Shth_Run_Loop");
constexpr TCHAR SheathedSprintLoopSequencePath[] =
	TEXT("/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Sprint_Loop_125x.AS_Shth_Sprint_Loop_125x");
constexpr TCHAR UnsheathedMoveDatabasePath[] =
	TEXT("/Game/Blueprints/Characters/Demo/Animation/MotionMatching/PSD_MH_UnSh_Move.PSD_MH_UnSh_Move");
constexpr TCHAR SheathedActionIdleDatabasePath[] =
	TEXT("/Game/Blueprints/Characters/Demo/Animation/MotionMatching/PSD_MH_Shth_ActionIdle.PSD_MH_Shth_ActionIdle");
constexpr TCHAR UnsheathedActionIdleDatabasePath[] =
	TEXT("/Game/Blueprints/Characters/Demo/Animation/MotionMatching/PSD_MH_UnSh_ActionIdle.PSD_MH_UnSh_ActionIdle");
constexpr TCHAR SheathedSheatheExitDatabasePath[] =
	TEXT("/Game/Blueprints/Characters/Demo/Animation/MotionMatching/PSD_MH_Shth_SheatheExit.PSD_MH_Shth_SheatheExit");
constexpr TCHAR SheathedDodgeExitDatabasePath[] =
	TEXT("/Game/Blueprints/Characters/Demo/Animation/MotionMatching/PSD_MH_Shth_DodgeExit.PSD_MH_Shth_DodgeExit");
constexpr TCHAR UnsheathedDodgeExitDatabasePath[] =
	TEXT("/Game/Blueprints/Characters/Demo/Animation/MotionMatching/PSD_MH_UnSh_DodgeExit.PSD_MH_UnSh_DodgeExit");

TAutoConsoleVariable<int32> CVarMHGZMMPostActionIdleHold(
	TEXT("mhgz.MM.PostActionIdleHold"),
	0,
	TEXT("When non-zero, temporarily restore the post-action MM Idle hold using MMForceIdleReleaseHoldDuration. "
		"Default 0 disables only that output hold for telemetry diagnosis; the one-frame Root Motion measurement reset after release remains active."),
	ECVF_Default);
}

void UMHGZMotionMatchingAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	MontageSegmentBlendRequestSerial = 0;
	WeaponGripConstraint = {};
	WeaponGripConstraintAlpha = 0.f;
	WeaponGripConstraintSerial = 0;
	LastMontageSegmentBlendSamples.Reset();
	CachedCharacter = Cast<AMHGZCharacter>(TryGetPawnOwner());
	bHasPreviousActorLocation = false;
	bStartQueryActive = false;
	bStartQueryObservedStart = false;
	bHadMoveInput = false;
	bHasPreviousUnsheathedState = false;
	bPreviousUnsheathedState = false;
	bWasForceMMIdle = false;
	bStartInputSettling = false;
	PendingDirectionalDodgeStart = {};
	bMMForceDirectionalStartSearch = false;
	MMForceIdleReleaseHoldRemaining = 0.0f;
	MMStartInputSettleRemaining = 0.0f;
	MMStartQueryElapsed = 0.0f;
	MMStartQueryDuration = 0.0f;
	MMActualSpeed2D = 0.0f;
	MMQuerySpeed2D = MMExpectedWorldCruiseSpeed = 0.f;
	MMUnsheathedMoveSpeedMultiplier = MMUnsheathedAppliedSpeedMultiplier = 1.f;
	bMMCanApplyUnsheathedSpeed = false;
	MMBaseCruiseSpeed = 0.f;
	MMIntentQuery = 0.0f;
	MMDistanceToStopQuery = 0.0f;
	MMStopGaitQuery = 0.0f;
	MMMoveGaitQuery = 0.0f;
	bMMForceIdle = false;
	ResetActionVisualPitchLocalState();
	bMMHasLocomotionInput = false;
	bMMUnsheathedLocomotion = false;
	bMMStopRequestActive = false;
	bMMExitWantsMoveDatabase = false;
	bMMActionIdleContextActive = false;
	bMMActionIdleContextUnsheathed = false;
	MMActionIdleContextSerial = 0;
	bMMRouteSheathedActionIdle = false;
	bMMRouteUnsheathedActionIdle = false;
	NextMMActionIdleContextSerial = 1;
	MMLastNonZeroCruiseSpeed = 0.0f;
	ResetSheathedStopRequest();
	ResetLegacyStopRequest();
	ActiveSheathedGait = EMHGZStopGait::None;
	LoadActionExitDatabases();
	LoadSheathedLoopCandidateDatabases();
	ResetSheathedNormalMoveCandidateRouting();
	ResetActionExitRouting();
	ClearMMRuntimeSelections();
	DiscardMMSelectionEvents();
	DiscardMMHandoffConsumptions();
	DiscardMMActionIdleContextConsumptions();
	MMPredictedTrajectory.Samples.Reset();
}

void UMHGZMotionMatchingAnimInstance::NativeUninitializeAnimation()
{
	PendingDirectionalDodgeStart = {};
	bMMForceDirectionalStartSearch = false;
	WeaponGripConstraint = {};
	WeaponGripConstraintAlpha = 0.f;
	WeaponGripConstraintSerial = 0;
	LastMontageSegmentBlendSamples.Reset();
	if (CachedActionVisualPitchState.bValid)
	{
		if (AMHGZCharacter* Character = CachedCharacter.Get())
		{
			if (UMHGZWeaponRuntimeHostComponent* Host = Character->GetWeaponRuntimeHost())
			{
				Host->ReleaseActionVisualPitch(
					CachedActionVisualPitchState.OwnerAction,
					CachedActionVisualPitchState.SourceMontageInstanceID,
					EActionVisualPitchReleaseMode::ImmediateReset);
			}
		}
	}
	ResetActionVisualPitchLocalState();
	StopRuntimeTelemetry();
	ResetSheathedNormalMoveCandidateRouting();
	ResetActionExitRouting();
	ClearMMRuntimeSelections();
	DiscardMMSelectionEvents();
	DiscardMMHandoffConsumptions();
	DiscardMMActionIdleContextConsumptions();
	Super::NativeUninitializeAnimation();
}

void UMHGZMotionMatchingAnimInstance::Montage_Advance(const float DeltaSeconds)
{
	// PreUpdateAnimation has set NeedsUpdate for a real graph update. Do not leave
	// deferred requests behind when only off-screen Montages are being advanced.
	if (DeltaSeconds <= 0.0f || !NeedsUpdate())
	{
		WeaponGripConstraint.Clear();
		WeaponGripConstraintAlpha = 0.f;
		LastMontageSegmentBlendSamples.Reset();
		Super::Montage_Advance(DeltaSeconds);
		return;
	}
	Super::Montage_Advance(DeltaSeconds);
	TArray<FMHGZMontageSegmentBlendSample> After;
	FMHGZMontageSegmentBlend::Capture(MontageInstances, After);
	TMap<FName, float> Requests;
	FMHGZMontageSegmentBlend::BuildRequests(LastMontageSegmentBlendSamples, After, Requests);
	WeaponGripConstraint.Update(LastMontageSegmentBlendSamples, After, DeltaSeconds);
	WeaponGripConstraintAlpha = WeaponGripConstraint.Alpha;
	WeaponGripConstraintSerial = WeaponGripConstraint.Serial;
	LastMontageSegmentBlendSamples = MoveTemp(After);
	for (const TPair<FName, float>& Request : Requests)
	{
		// Must precede UpdateMontageEvaluationData: that method copies and clears
		// the instance map. Requesting in NativeUpdate would replay it next frame.
		RequestSlotGroupInertialization(Request.Key, Request.Value);
		++MontageSegmentBlendRequestSerial;
		UE_LOG(LogAnimation, Verbose, TEXT("[MontageSegmentBlend] serial=%lld group=%s duration=%.4f"),
			MontageSegmentBlendRequestSerial, *Request.Key.ToString(), Request.Value);
	}
}

void UMHGZMotionMatchingAnimInstance::QueueMotionMatchingSelection(
	const FAnimNodeReference& AnimNodeReference,
	const EMHGZMotionMatchingNode MotionMatchingNode)
{
	EAnimNodeReferenceConversionResult ConversionResult;
	const FMotionMatchingAnimNodeReference MotionMatchingReference =
		UMotionMatchingAnimNodeLibrary::ConvertToMotionMatchingNode(AnimNodeReference,
			ConversionResult);
	if (ConversionResult != EAnimNodeReferenceConversionResult::Succeeded)
	{
		return;
	}

	FPoseSearchBlueprintResult SearchResult;
	bool bIsResultValid = false;
	UMotionMatchingAnimNodeLibrary::GetMotionMatchingSearchResult(MotionMatchingReference,
		SearchResult, bIsResultValid);
	if (!bIsResultValid || !SearchResult.SelectedAnim)
	{
		return;
	}

	FMotionMatchingSelectionEvent Event;
	Event.Frame = GFrameCounter;
	Event.Node = MotionMatchingNode;
	Event.Database = SearchResult.SelectedDatabase;
	Event.Animation = SearchResult.SelectedAnim;
	Event.AnimationTime = SearchResult.SelectedTime;
	Event.SearchCost = SearchResult.SearchCost;
	Event.WantedPlayRate = SearchResult.WantedPlayRate;
	Event.bIsContinuing = SearchResult.bIsContinuingPoseSearch;
	Event.bIsLooping = SearchResult.bLoop;
	Event.bIsMirrored = SearchResult.bIsMirrored;
	const int32 NodeIndex = static_cast<int32>(MotionMatchingNode);
	Event.LoopPhaseQuery = NodeIndex == 0 ? MMLoopPhaseQuery : MMUnsheathedLoopPhaseQuery;
	Event.LoopQuerySource = MMLoopQuerySource[NodeIndex];
	Event.LoopHistoryEntriesRemoved = MMLoopHistoryEntriesRemoved[NodeIndex];
	if (const auto* MM = AnimNodeReference.GetAnimNodePtr<FAnimNode_MotionMatching>())
	{ Event.PreBlendActivePlayers = MM->AnimPlayers.Num(); }
	MMPendingSelectionEvents.Enqueue(MoveTemp(Event));
}

void UMHGZMotionMatchingAnimInstance::OnSheathedMotionMatchingPreUpdate(
	const FAnimUpdateContext& Context, const FAnimNodeReference& AnimNodeReference)
{
	PrepareLoopContinuity(Context, AnimNodeReference, EMHGZMotionMatchingNode::Sheathed);
	RouteMotionMatchingPreSearch(AnimNodeReference, EMHGZMotionMatchingNode::Sheathed);
}

void UMHGZMotionMatchingAnimInstance::OnUnsheathedMotionMatchingPreUpdate(
	const FAnimUpdateContext& Context, const FAnimNodeReference& AnimNodeReference)
{
	PrepareLoopContinuity(Context, AnimNodeReference, EMHGZMotionMatchingNode::Unsheathed);
	RouteMotionMatchingPreSearch(AnimNodeReference, EMHGZMotionMatchingNode::Unsheathed);
	ApplyUnsheathedLocomotionSpeed(AnimNodeReference);
}

void UMHGZMotionMatchingAnimInstance::OnSheathedMotionMatchingStateUpdated(
	const FAnimUpdateContext& Context, const FAnimNodeReference& AnimNodeReference)
{
	(void)Context;
	ObserveMotionMatchingPostSearch(AnimNodeReference, EMHGZMotionMatchingNode::Sheathed);
}

void UMHGZMotionMatchingAnimInstance::OnUnsheathedMotionMatchingStateUpdated(
	const FAnimUpdateContext& Context, const FAnimNodeReference& AnimNodeReference)
{
	(void)Context;
	ObserveMotionMatchingPostSearch(AnimNodeReference, EMHGZMotionMatchingNode::Unsheathed);
}

void UMHGZMotionMatchingAnimInstance::LoadActionExitDatabases()
{
	MMSheathedMoveDatabase = LoadObject<UPoseSearchDatabase>(nullptr, SheathedMoveDatabasePath);
	MMUnsheathedMoveDatabase = LoadObject<UPoseSearchDatabase>(nullptr, UnsheathedMoveDatabasePath);
	MMSheathedActionIdleDatabase = LoadObject<UPoseSearchDatabase>(nullptr,
		SheathedActionIdleDatabasePath);
	MMUnsheathedActionIdleDatabase = LoadObject<UPoseSearchDatabase>(nullptr,
		UnsheathedActionIdleDatabasePath);
	MMSheathedSheatheExitDatabase = LoadObject<UPoseSearchDatabase>(nullptr,
		SheathedSheatheExitDatabasePath);
	MMSheathedDodgeExitDatabase = LoadObject<UPoseSearchDatabase>(nullptr,
		SheathedDodgeExitDatabasePath);
	MMUnsheathedDodgeExitDatabase = LoadObject<UPoseSearchDatabase>(nullptr,
		UnsheathedDodgeExitDatabasePath);

	if (!MMSheathedMoveDatabase || !MMUnsheathedMoveDatabase
		|| !MMSheathedActionIdleDatabase || !MMUnsheathedActionIdleDatabase
		|| !MMSheathedSheatheExitDatabase || !MMSheathedDodgeExitDatabase
		|| !MMUnsheathedDodgeExitDatabase)
	{
		UE_LOG(LogMHGZ, Error,
			TEXT("[ActionExitRoute] Failed to load one or more audited Pose Search databases."));
	}
}

void UMHGZMotionMatchingAnimInstance::LoadSheathedLoopCandidateDatabases()
{
	MMSheathedWalkLoopOnlyDatabase = LoadObject<UPoseSearchDatabase>(nullptr,
		TEXT("/Game/Blueprints/Characters/Demo/Animation/MotionMatching/PSD_MH_Shth_Walk_LoopOnly.PSD_MH_Shth_Walk_LoopOnly"));
	MMUnsheathedLoopOnlyDatabase = LoadObject<UPoseSearchDatabase>(nullptr,
		TEXT("/Game/Blueprints/Characters/Demo/Animation/MotionMatching/PSD_MH_UnSh_Walk_LoopOnly.PSD_MH_UnSh_Walk_LoopOnly"));
	MMSheathedRunLoopOnlyDatabase = LoadObject<UPoseSearchDatabase>(nullptr,
		SheathedRunLoopOnlyDatabasePath);
	MMSheathedSprintLoopOnlyDatabase = LoadObject<UPoseSearchDatabase>(nullptr,
		SheathedSprintLoopOnlyDatabasePath);
	MMSheathedRunLoopSequence = LoadObject<UAnimSequenceBase>(nullptr,
		SheathedRunLoopSequencePath);
	MMSheathedSprintLoopSequence = LoadObject<UAnimSequenceBase>(nullptr,
		SheathedSprintLoopSequencePath);

	if (!MMSheathedRunLoopOnlyDatabase || !MMSheathedSprintLoopOnlyDatabase
		|| !MMSheathedRunLoopSequence || !MMSheathedSprintLoopSequence)
	{
		UE_LOG(LogMHGZ, Error,
			TEXT("[M421LoopRoute] Failed to load one or more audited sheathed LoopOnly assets."));
	}
}

void UMHGZMotionMatchingAnimInstance::ResetActionExitRouting()
{
	MMLoopClock.Reset();
	MMLoopClockFrame = MAX_uint64;
	MMLoopPhaseQuery = MMUnsheathedLoopPhaseQuery = FVector2D::ZeroVector;
	for (int32 I = 0; I < 2; ++I)
	{
		MMLoopQueryDatabases[I] = nullptr;
		bMMLoopEntryMatch[I] = false;
	}
	bMMMobileExitCruise[0] = bMMMobileExitCruise[1] = false;
	MMSheathedActionExitRoute = EMHGZMMActionExitRoute::Move;
	MMUnsheathedActionExitRoute = EMHGZMMActionExitRoute::Move;
	MMSheathedActionExitSerial = 0;
	MMUnsheathedActionExitSerial = 0;
	MMSheathedActionExitHandoffType = EMHGZMotionMatchingHandoffType::None;
	MMUnsheathedActionExitHandoffType = EMHGZMotionMatchingHandoffType::None;
	MMSheathedActionExitSelectedTime = 0.0f;
	MMUnsheathedActionExitSelectedTime = 0.0f;
	bMMActionIdleContextActive = false;
	bMMActionIdleContextUnsheathed = false;
	MMActionIdleContextSerial = 0;
	bMMRouteSheathedActionIdle = false;
	bMMRouteUnsheathedActionIdle = false;
}

UPoseSearchDatabase* UMHGZMotionMatchingAnimInstance::GetMobileExitLoopDatabase(
	const EMHGZMotionMatchingNode Node) const
{
	if (Node == EMHGZMotionMatchingNode::Unsheathed) { return MMUnsheathedLoopOnlyDatabase.Get(); }
	if (MMSheathedTargetMoveGait == MHGZMotionMatching::EMHGZNormalMoveGait::Sprint)
	{ return MMSheathedSprintLoopOnlyDatabase.Get(); }
	if (MMSheathedTargetMoveGait == MHGZMotionMatching::EMHGZNormalMoveGait::Run)
	{ return MMSheathedRunLoopOnlyDatabase.Get(); }
	return MMSheathedWalkLoopOnlyDatabase.Get();
}

void UMHGZMotionMatchingAnimInstance::ResetSheathedNormalMoveCandidateRouting()
{
	MMSheathedTargetMoveGait = MHGZMotionMatching::EMHGZNormalMoveGait::None;
	MMSheathedNormalMoveCandidateSet =
		MHGZMotionMatching::EMHGZNormalMoveCandidateSet::FullMove;
}

UPoseSearchDatabase* UMHGZMotionMatchingAnimInstance::GetMoveDatabase(
	const EMHGZMotionMatchingNode MotionMatchingNode) const
{
	return MotionMatchingNode == EMHGZMotionMatchingNode::Sheathed
		? MMSheathedMoveDatabase.Get() : MMUnsheathedMoveDatabase.Get();
}

UPoseSearchDatabase* UMHGZMotionMatchingAnimInstance::GetSheathedLoopCandidateDatabase(
	const MHGZMotionMatching::EMHGZNormalMoveCandidateSet CandidateSet) const
{
	switch (CandidateSet)
	{
	case MHGZMotionMatching::EMHGZNormalMoveCandidateSet::RunLoopOnly:
		return MMSheathedRunLoopOnlyDatabase.Get();
	case MHGZMotionMatching::EMHGZNormalMoveCandidateSet::SprintLoopOnly:
		return MMSheathedSprintLoopOnlyDatabase.Get();
	case MHGZMotionMatching::EMHGZNormalMoveCandidateSet::FullMove:
	default:
		return MMSheathedMoveDatabase.Get();
	}
}

void UMHGZMotionMatchingAnimInstance::RouteSheathedNormalMovePreSearch(
	const FMotionMatchingAnimNodeReference& MotionMatchingReference)
{
	using namespace MHGZMotionMatching;

	FMHGZNormalMoveCandidateRouteInput Input;
	Input.CurrentCandidateSet = MMSheathedNormalMoveCandidateSet;
	Input.TargetGait = MMSheathedTargetMoveGait;
	Input.bActionRouteIsMove = MMSheathedActionExitRoute == EMHGZMMActionExitRoute::Move;
	Input.bHasLocomotionInput = bMMHasLocomotionInput;
	Input.bStopRequestActive = bMMStopRequestActive;
	Input.bStartQueryActive = bStartQueryActive;
	const EMHGZNormalMoveGait SelectedLoopGait = GetExactSheathedLoopGait(
		GetLatestSelection(EMHGZMotionMatchingNode::Sheathed));
	Input.bLatestSelectionIsRunLoop = SelectedLoopGait == EMHGZNormalMoveGait::Run;
	Input.bLatestSelectionIsSprintLoop = SelectedLoopGait == EMHGZNormalMoveGait::Sprint;

	const EMHGZNormalMoveCandidateSet DesiredCandidateSet =
		ResolveSheathedNormalMoveCandidateSet(Input);
	if (DesiredCandidateSet == EMHGZNormalMoveCandidateSet::FullMove)
	{
		UMotionMatchingAnimNodeLibrary::ResetDatabasesToSearch(MotionMatchingReference,
			EPoseSearchInterruptMode::InterruptOnDatabaseChangeAndInvalidateContinuingPose);
		MMSheathedNormalMoveCandidateSet = DesiredCandidateSet;
		return;
	}

	if (UPoseSearchDatabase* CandidateDatabase =
		GetSheathedLoopCandidateDatabase(DesiredCandidateSet))
	{
		InstallLoopDatabase(MotionMatchingReference, EMHGZMotionMatchingNode::Sheathed,
			CandidateDatabase, false);
		MMSheathedNormalMoveCandidateSet = DesiredCandidateSet;
		return;
	}

	// Asset setup must have completed before this code can route a LoopOnly bank.
	// Keep the character controllable, but make a missing audited asset explicit.
	UE_LOG(LogMHGZ, Error,
		TEXT("[M421LoopRoute] Candidate database missing for set %d; reverting to full sheathed Move."),
		static_cast<int32>(DesiredCandidateSet));
	UMotionMatchingAnimNodeLibrary::ResetDatabasesToSearch(MotionMatchingReference,
		EPoseSearchInterruptMode::InterruptOnDatabaseChangeAndInvalidateContinuingPose);
	MMSheathedNormalMoveCandidateSet = EMHGZNormalMoveCandidateSet::FullMove;
}

UPoseSearchDatabase* UMHGZMotionMatchingAnimInstance::GetActionIdleDatabase(
	const EMHGZMotionMatchingNode MotionMatchingNode) const
{
	return MotionMatchingNode == EMHGZMotionMatchingNode::Sheathed
		? MMSheathedActionIdleDatabase.Get() : MMUnsheathedActionIdleDatabase.Get();
}

UPoseSearchDatabase* UMHGZMotionMatchingAnimInstance::GetExitDatabase(
	const EMHGZMotionMatchingNode MotionMatchingNode,
	const EMHGZMotionMatchingHandoffType HandoffType) const
{
	if (MotionMatchingNode == EMHGZMotionMatchingNode::Sheathed)
	{
		switch (HandoffType)
		{
		case EMHGZMotionMatchingHandoffType::SheatheMoveExit:
			return MMSheathedSheatheExitDatabase.Get();
		case EMHGZMotionMatchingHandoffType::DodgeMoveExit:
			return MMSheathedDodgeExitDatabase.Get();
		default:
			return nullptr;
		}
	}

	return HandoffType == EMHGZMotionMatchingHandoffType::DodgeMoveExit
		? MMUnsheathedDodgeExitDatabase.Get() : nullptr;
}

float UMHGZMotionMatchingAnimInstance::GetExitDuration(
	const EMHGZMotionMatchingNode MotionMatchingNode,
	const EMHGZMotionMatchingHandoffType HandoffType) const
{
	if (MotionMatchingNode == EMHGZMotionMatchingNode::Sheathed)
	{
		return HandoffType == EMHGZMotionMatchingHandoffType::SheatheMoveExit ? 0.3500f
			: HandoffType == EMHGZMotionMatchingHandoffType::DodgeMoveExit ? 0.2667f : 0.0f;
	}
	return HandoffType == EMHGZMotionMatchingHandoffType::DodgeMoveExit ? 0.5667f : 0.0f;
}

EMHGZMMActionExitRoute& UMHGZMotionMatchingAnimInstance::GetActionExitRoute(
	const EMHGZMotionMatchingNode MotionMatchingNode)
{
	return MotionMatchingNode == EMHGZMotionMatchingNode::Sheathed
		? MMSheathedActionExitRoute : MMUnsheathedActionExitRoute;
}

int64& UMHGZMotionMatchingAnimInstance::GetActionExitSerial(
	const EMHGZMotionMatchingNode MotionMatchingNode)
{
	return MotionMatchingNode == EMHGZMotionMatchingNode::Sheathed
		? MMSheathedActionExitSerial : MMUnsheathedActionExitSerial;
}

EMHGZMotionMatchingHandoffType& UMHGZMotionMatchingAnimInstance::GetActionExitHandoffType(
	const EMHGZMotionMatchingNode MotionMatchingNode)
{
	return MotionMatchingNode == EMHGZMotionMatchingNode::Sheathed
		? MMSheathedActionExitHandoffType : MMUnsheathedActionExitHandoffType;
}

float& UMHGZMotionMatchingAnimInstance::GetActionExitSelectedTime(
	const EMHGZMotionMatchingNode MotionMatchingNode)
{
	return MotionMatchingNode == EMHGZMotionMatchingNode::Sheathed
		? MMSheathedActionExitSelectedTime : MMUnsheathedActionExitSelectedTime;
}

void UMHGZMotionMatchingAnimInstance::EnterActionIdleRoute(
	const EMHGZMotionMatchingNode MotionMatchingNode)
{
	GetActionExitRoute(MotionMatchingNode) = EMHGZMMActionExitRoute::ActionIdle;
	GetActionExitSerial(MotionMatchingNode) = 0;
	GetActionExitHandoffType(MotionMatchingNode) = EMHGZMotionMatchingHandoffType::None;
	GetActionExitSelectedTime(MotionMatchingNode) = 0.0f;
	bMMActionIdleContextActive = true;
	bMMActionIdleContextUnsheathed = MotionMatchingNode == EMHGZMotionMatchingNode::Unsheathed;
	RefreshActionIdleRouteFlags();
}

void UMHGZMotionMatchingAnimInstance::ClearActionIdleRoute(
	const EMHGZMotionMatchingNode MotionMatchingNode)
{
	GetActionExitRoute(MotionMatchingNode) = EMHGZMMActionExitRoute::Move;
	GetActionExitSerial(MotionMatchingNode) = 0;
	GetActionExitHandoffType(MotionMatchingNode) = EMHGZMotionMatchingHandoffType::None;
	GetActionExitSelectedTime(MotionMatchingNode) = 0.0f;
	if (bMMActionIdleContextActive
		&& bMMActionIdleContextUnsheathed
			== (MotionMatchingNode == EMHGZMotionMatchingNode::Unsheathed))
	{
		bMMActionIdleContextActive = false;
	}
	RefreshActionIdleRouteFlags();
}

void UMHGZMotionMatchingAnimInstance::RouteMotionMatchingPreSearch(
	const FAnimNodeReference& AnimNodeReference,
	const EMHGZMotionMatchingNode MotionMatchingNode)
{
	EAnimNodeReferenceConversionResult ConversionResult;
	const FMotionMatchingAnimNodeReference MotionMatchingReference =
		UMotionMatchingAnimNodeLibrary::ConvertToMotionMatchingNode(AnimNodeReference,
			ConversionResult);
	if (ConversionResult != EAnimNodeReferenceConversionResult::Succeeded)
	{
		return;
	}

	EMHGZMMActionExitRoute& Route = GetActionExitRoute(MotionMatchingNode);
	int64& RouteSerial = GetActionExitSerial(MotionMatchingNode);
	EMHGZMotionMatchingHandoffType& RouteHandoffType =
		GetActionExitHandoffType(MotionMatchingNode);
	float& ExitSelectedTime = GetActionExitSelectedTime(MotionMatchingNode);
	const bool bNodeMatchesCurrentStance =
		(MotionMatchingNode == EMHGZMotionMatchingNode::Unsheathed) == bMMUnsheathedLocomotion;
	if (MotionMatchingNode == EMHGZMotionMatchingNode::Unsheathed
		&& bMMForceDirectionalStartSearch && bNodeMatchesCurrentStance
		&& bMMHasLocomotionInput && !bMMForceIdle && !bMMHandoffActive)
	{
		bMMForceDirectionalStartSearch = false;
		ClearActionIdleRoute(MotionMatchingNode);
		bMMMobileExitCruise[1] = false;
		if (bMMBaseBlendCaptured[1])
		{
			auto& Blend = *reinterpret_cast<FMotionMatchingBlueprintBlendSettings*>(MMBaseBlend[1]->GetStructMemory());
			const float AuthoredDuration = Blend.BlendTime;
			const bool bAuthoredInertialBlend = Blend.bUseInertialBlend;
			// The outgoing full-body montage supplies the requested 0.1s fade.
			// A second Idle->Start MM fade underneath would reintroduce an Idle hump.
			Blend.BlendTime = 0.f;
			Blend.bUseInertialBlend = false;
			bool bValid = false;
			UMotionMatchingAnimNodeLibrary::OverrideMotionMatchingBlendSettings(MotionMatchingReference, Blend, bValid);
			Blend.BlendTime = AuthoredDuration;
			Blend.bUseInertialBlend = bAuthoredInertialBlend;
		}
		// This update already has a fresh positive Start query. Do not continue an
		// old Idle/player or wait for the ordinary search throttle before using it.
		UMotionMatchingAnimNodeLibrary::ResetDatabasesToSearch(MotionMatchingReference,
			EPoseSearchInterruptMode::ForceInterruptAndInvalidateContinuingPose);
		return;
	}
	const UPoseSearchDatabase* PendingExitDatabase = bNodeMatchesCurrentStance
		? GetExitDatabase(MotionMatchingNode, MMHandoffType) : nullptr;
	const bool bNewCompatibleHandoff = bMMHandoffActive && bNodeMatchesCurrentStance
		&& MMHandoffSerial > 0 && PendingExitDatabase
		&& RouteSerial != MMHandoffSerial;

	if (bNewCompatibleHandoff)
	{
		bMMMobileExitCruise[static_cast<int32>(MotionMatchingNode)] = false;
		if (bMMActionIdleContextActive
			&& bMMActionIdleContextUnsheathed
				== (MotionMatchingNode == EMHGZMotionMatchingNode::Unsheathed))
		{
			bMMActionIdleContextActive = false;
			RefreshActionIdleRouteFlags();
		}
		Route = EMHGZMMActionExitRoute::AwaitExitEntry;
		RouteSerial = MMHandoffSerial;
		RouteHandoffType = MMHandoffType;
		ExitSelectedTime = 0.0f;
	}
	else if (Route == EMHGZMMActionExitRoute::Move && bMMActionIdleContextActive
		&& bMMActionIdleContextUnsheathed
			== (MotionMatchingNode == EMHGZMotionMatchingNode::Unsheathed))
	{
		EnterActionIdleRoute(MotionMatchingNode);
	}

	if (Route == EMHGZMMActionExitRoute::ActionIdle && bMMHasLocomotionInput)
	{
		ClearActionIdleRoute(MotionMatchingNode);
	}
	if (MotionMatchingNode == EMHGZMotionMatchingNode::Sheathed
		&& Route != EMHGZMMActionExitRoute::Move)
	{
		// Drop the ordinary bank's routing state, not the current input's gait.
		// The Exit tail needs that same gait to choose Walk/Run/Sprint Loop.
		MMSheathedNormalMoveCandidateSet = MHGZMotionMatching::EMHGZNormalMoveCandidateSet::FullMove;
	}

	switch (Route)
	{
	case EMHGZMMActionExitRoute::ActionIdle:
		if (UPoseSearchDatabase* IdleDatabase = GetActionIdleDatabase(MotionMatchingNode))
		{
			UMotionMatchingAnimNodeLibrary::SetDatabaseToSearch(MotionMatchingReference, IdleDatabase,
				EPoseSearchInterruptMode::InterruptOnDatabaseChangeAndInvalidateContinuingPose);
		}
		return;

	case EMHGZMMActionExitRoute::AwaitExitEntry:
	case EMHGZMMActionExitRoute::ExitOnly:
	case EMHGZMMActionExitRoute::ExitAndMove:
		break;

	case EMHGZMMActionExitRoute::Move:
	default:
		if (TryRouteStartToLoop(MotionMatchingReference, MotionMatchingNode)) { return; }
		if (bMMMobileExitCruise[static_cast<int32>(MotionMatchingNode)]
			&& bMMHasLocomotionInput && !bMMStopRequestActive && !bStartQueryActive)
		{
			if (UPoseSearchDatabase* LoopDatabase = GetMobileExitLoopDatabase(MotionMatchingNode))
			{
				InstallLoopDatabase(MotionMatchingReference, MotionMatchingNode, LoopDatabase, false);
				return;
			}
		}
		bMMMobileExitCruise[static_cast<int32>(MotionMatchingNode)] = false;
		if (MotionMatchingNode == EMHGZMotionMatchingNode::Sheathed)
		{
			RouteSheathedNormalMovePreSearch(MotionMatchingReference);
		}
		else
		{
			UMotionMatchingAnimNodeLibrary::ResetDatabasesToSearch(MotionMatchingReference,
				EPoseSearchInterruptMode::DoNotInterrupt);
		}
		return;
	}

	UPoseSearchDatabase* ExitDatabase = GetExitDatabase(MotionMatchingNode, RouteHandoffType);
	const float ExitDuration = GetExitDuration(MotionMatchingNode, RouteHandoffType);
	if (!ExitDatabase || ExitDuration <= KINDA_SMALL_NUMBER)
	{
		ClearActionIdleRoute(MotionMatchingNode);
		UMotionMatchingAnimNodeLibrary::ResetDatabasesToSearch(MotionMatchingReference,
			EPoseSearchInterruptMode::ForceInterruptAndInvalidateContinuingPose);
		return;
	}

	if (Route == EMHGZMMActionExitRoute::AwaitExitEntry)
	{
		// Fresh searches may select only the initial open window in the full Exit
		// PSD. Keeping that same database preserves its continuing pose through the
		// authored blocked body instead of creating a cross-database pose jump.
		UMotionMatchingAnimNodeLibrary::SetDatabaseToSearch(MotionMatchingReference, ExitDatabase,
			EPoseSearchInterruptMode::ForceInterruptAndInvalidateContinuingPose);
		return;
	}

	// A release that occurs during an approved MoveExit is a legal, immediate
	// Stop handoff. Its functional/root-motion phase has already ended before
	// the Handoff, so preserving the Exit continuing pose would only delay the
	// normal one-shot Stop until the authored tail. Force one ordinary Move PSD
	// search instead; its Stop query selects the normal Stop lifecycle without
	// creating an input deadzone or replaying the Exit tail.
	if (bMMExitWantsMoveDatabase)
	{
		Route = EMHGZMMActionExitRoute::ExitAndMove;
	}
	if (MHGZMotionMatching::ShouldInterruptMobileActionExitForStop(
		Route == EMHGZMMActionExitRoute::ExitAndMove, bMMStopRequestActive))
	{
		if (UPoseSearchDatabase* MoveDatabase = GetMoveDatabase(MotionMatchingNode))
		{
			ClearActionIdleRoute(MotionMatchingNode);
			UMotionMatchingAnimNodeLibrary::SetDatabaseToSearch(MotionMatchingReference,
				MoveDatabase, EPoseSearchInterruptMode::ForceInterruptAndInvalidateContinuingPose);
			return;
		}
	}

	if (Route == EMHGZMMActionExitRoute::ExitOnly
		&& ExitSelectedTime >= ExitDuration - ActionExitEndEpsilonSeconds)
	{
		EnterActionIdleRoute(MotionMatchingNode);
		if (UPoseSearchDatabase* IdleDatabase = GetActionIdleDatabase(MotionMatchingNode))
		{
			UMotionMatchingAnimNodeLibrary::SetDatabaseToSearch(MotionMatchingReference, IdleDatabase,
				EPoseSearchInterruptMode::InterruptOnDatabaseChangeAndInvalidateContinuingPose);
		}
		return;
	}

	if (Route == EMHGZMMActionExitRoute::ExitAndMove
		&& ExitSelectedTime >= ExitDuration - ActionExitEndEpsilonSeconds)
	{
		if (UPoseSearchDatabase* MoveDatabase = GetMobileExitLoopDatabase(MotionMatchingNode))
		{
			// A low-rate update can skip the short safe window. Even at the exact
			// end, keep the held-input handoff on Loop rather than re-opening Start.
			InstallLoopDatabase(MotionMatchingReference, MotionMatchingNode, MoveDatabase, true);
			return;
		}
	}

	if (Route == EMHGZMMActionExitRoute::ExitAndMove
		&& ExitSelectedTime >= ExitDuration - ActionExitMoveTailSeconds)
	{
		if (UPoseSearchDatabase* LoopDatabase = GetMobileExitLoopDatabase(MotionMatchingNode))
		{
			// One database-change edge searches immediately, despite the 0.12s
			// throttle. Entry blending is capped to the old player's remaining time.
			// Never expose Start/Idle to an already-moving action tail.
			InstallLoopDatabase(MotionMatchingReference, MotionMatchingNode, LoopDatabase, true);
			return;
		}
	}

	UMotionMatchingAnimNodeLibrary::SetDatabaseToSearch(MotionMatchingReference, ExitDatabase,
		EPoseSearchInterruptMode::DoNotInterrupt);
}

void UMHGZMotionMatchingAnimInstance::ObserveMotionMatchingPostSearch(
	const FAnimNodeReference& AnimNodeReference,
	const EMHGZMotionMatchingNode MotionMatchingNode)
{
	ObserveLoopContinuity(AnimNodeReference, MotionMatchingNode);
	QueueMotionMatchingSelection(AnimNodeReference, MotionMatchingNode);

	EAnimNodeReferenceConversionResult ConversionResult;
	const FMotionMatchingAnimNodeReference MotionMatchingReference =
		UMotionMatchingAnimNodeLibrary::ConvertToMotionMatchingNode(AnimNodeReference,
			ConversionResult);
	if (ConversionResult != EAnimNodeReferenceConversionResult::Succeeded)
	{
		return;
	}

	FPoseSearchBlueprintResult SearchResult;
	bool bIsResultValid = false;
	UMotionMatchingAnimNodeLibrary::GetMotionMatchingSearchResult(MotionMatchingReference,
		SearchResult, bIsResultValid);
	if (!bIsResultValid || !SearchResult.SelectedAnim)
	{
		return;
	}

	EMHGZMMActionExitRoute& Route = GetActionExitRoute(MotionMatchingNode);
	const EMHGZMotionMatchingHandoffType RouteHandoffType =
		GetActionExitHandoffType(MotionMatchingNode);
	UPoseSearchDatabase* ExitDatabase = GetExitDatabase(MotionMatchingNode, RouteHandoffType);
	if (!ExitDatabase)
	{
		return;
	}

	if (SearchResult.SelectedDatabase == ExitDatabase)
	{
		GetActionExitSelectedTime(MotionMatchingNode) = SearchResult.SelectedTime;
		if (Route == EMHGZMMActionExitRoute::AwaitExitEntry)
		{
			Route = bMMExitWantsMoveDatabase
				? EMHGZMMActionExitRoute::ExitAndMove : EMHGZMMActionExitRoute::ExitOnly;
			QueueMotionMatchingHandoffConsumption(GetActionExitSerial(MotionMatchingNode));
		}
		return;
	}

	// Held-input exits finish only after a real Loop candidate wins. Release is
	// handled before this branch by the unchanged one-shot Stop path.
	if (Route == EMHGZMMActionExitRoute::ExitAndMove
		&& SearchResult.SelectedDatabase == GetMobileExitLoopDatabase(MotionMatchingNode))
	{
		bMMMobileExitCruise[static_cast<int32>(MotionMatchingNode)] = true;
		Route = EMHGZMMActionExitRoute::Move;
		GetActionExitSerial(MotionMatchingNode) = 0;
		GetActionExitHandoffType(MotionMatchingNode) = EMHGZMotionMatchingHandoffType::None;
		GetActionExitSelectedTime(MotionMatchingNode) = 0.0f;
	}
}

void UMHGZMotionMatchingAnimInstance::QueueMotionMatchingHandoffConsumption(
	const int64 HandoffSerial)
{
	if (HandoffSerial > 0)
	{
		MMPendingHandoffConsumptionSerials.Enqueue(HandoffSerial);
	}
}

void UMHGZMotionMatchingAnimInstance::QueueMotionMatchingActionIdleContextConsumption(
	const int64 ContextSerial)
{
	if (ContextSerial > 0)
	{
		MMPendingActionIdleContextConsumptionSerials.Enqueue(ContextSerial);
	}
}

bool UMHGZMotionMatchingAnimInstance::BuildActionVisualPitchComponentRotation(
	const FVector& FacingWorld, const FVector& TargetWorld,
	const FQuat& MeshWorldRotation, FQuat& OutComponentRotation)
{
	OutComponentRotation = FQuat::Identity;
	if (FacingWorld.ContainsNaN() || TargetWorld.ContainsNaN()
		|| MeshWorldRotation.ContainsNaN() || FacingWorld.IsNearlyZero()
		|| TargetWorld.IsNearlyZero())
	{
		return false;
	}

	const FQuat MeshRotation = MeshWorldRotation.GetNormalized();
	const FVector FacingInComponent = MeshRotation.UnrotateVector(
		FacingWorld.GetSafeNormal()).GetSafeNormal();
	const FVector TargetInComponent = MeshRotation.UnrotateVector(
		TargetWorld.GetSafeNormal()).GetSafeNormal();
	if (FacingInComponent.IsNearlyZero() || TargetInComponent.IsNearlyZero())
	{
		return false;
	}

	OutComponentRotation = FQuat::FindBetweenNormals(
		FacingInComponent, TargetInComponent).GetNormalized();
	return !OutComponentRotation.ContainsNaN();
}

void UMHGZMotionMatchingAnimInstance::ResetActionVisualPitchLocalState()
{
	CachedActionVisualPitchState = FWeaponActionVisualPitchState();
	LastActionVisualPitchRevision = 0;
	ActionVisualPitchTransitionStart = FQuat::Identity;
	ActionVisualPitchTransitionTarget = FQuat::Identity;
	ActionVisualPitchTransitionElapsed = 0.0f;
	ActionVisualPitchTransitionDuration = 0.0f;
	ActionVisualPitchTransitionStartWeight = 0.0f;
	ActionVisualPitchTransitionTargetWeight = 0.0f;
	bActionVisualPitchTransitionActive = false;
	bActionVisualPitchActive = false;
	ActionVisualPitchDegrees = 0.0f;
	ActionVisualPitchWeight = 0.0f;
	ActionVisualPitchComponentRotation = FRotator::ZeroRotator;
}

void UMHGZMotionMatchingAnimInstance::UpdateActionVisualPitch(
	const AMHGZCharacter* Character, const float DeltaSeconds)
{
	FWeaponActionVisualPitchState State;
	UMHGZWeaponRuntimeHostComponent* Host = Character
		? Character->GetWeaponRuntimeHost() : nullptr;
	if (Host)
	{
		Host->GetActionVisualPitchState(State);
	}

	if (!Host)
	{
		ResetActionVisualPitchLocalState();
		return;
	}

	const bool bNewRevision = State.StateRevision != LastActionVisualPitchRevision;
	if (bNewRevision)
	{
		CachedActionVisualPitchState = State;
		LastActionVisualPitchRevision = State.StateRevision;
		ActionVisualPitchTransitionStart = ActionVisualPitchComponentRotation.Quaternion();
		ActionVisualPitchTransitionStartWeight = ActionVisualPitchWeight;
		ActionVisualPitchTransitionTargetWeight = State.bValid
			? FMath::Clamp(State.TargetWeight, 0.0f, 1.0f) : 0.0f;
		ActionVisualPitchTransitionTarget = FQuat::Identity;
		ActionVisualPitchDegrees = 0.0f;

		if (State.bValid && State.TargetWeight > 0.0f)
		{
			ActionVisualPitchDegrees = FMath::RadiansToDegrees(FMath::Atan2(
				State.LockedDirection.Z,
				FMath::Sqrt(FMath::Square(State.LockedDirection.X)
					+ FMath::Square(State.LockedDirection.Y))));

			const USkeletalMeshComponent* Mesh = Character->GetMesh();
			FVector HorizontalFacing = Character->GetActorForwardVector();
			HorizontalFacing.Z = 0.0f;
			HorizontalFacing = HorizontalFacing.GetSafeNormal();
			if (Mesh && !HorizontalFacing.IsNearlyZero())
			{
				FQuat ComponentRotation = FQuat::Identity;
				if (BuildActionVisualPitchComponentRotation(HorizontalFacing,
					State.LockedDirection, Mesh->GetComponentQuat(), ComponentRotation))
				{
					ActionVisualPitchTransitionTarget = ComponentRotation;
				}
			}
		}

		ActionVisualPitchTransitionElapsed = 0.0f;
		ActionVisualPitchTransitionDuration = State.bValid
			? (State.bReleasing ? State.BlendOutSeconds : State.BlendInSeconds)
			: 0.0f;
		bActionVisualPitchTransitionActive =
			ActionVisualPitchTransitionDuration > KINDA_SMALL_NUMBER
			&& (!ActionVisualPitchTransitionStart.Equals(
				ActionVisualPitchTransitionTarget, 1.0e-5f)
				|| !FMath::IsNearlyEqual(ActionVisualPitchTransitionStartWeight,
					ActionVisualPitchTransitionTargetWeight, 1.0e-4f));
		if (!bActionVisualPitchTransitionActive)
		{
			ActionVisualPitchComponentRotation =
				ActionVisualPitchTransitionTarget.Rotator();
			ActionVisualPitchWeight = ActionVisualPitchTransitionTargetWeight;
		}
	}

	if (bActionVisualPitchTransitionActive)
	{
		ActionVisualPitchTransitionElapsed += FMath::Max(0.0f, DeltaSeconds);
		const float Alpha = FMath::Clamp(
			ActionVisualPitchTransitionElapsed / ActionVisualPitchTransitionDuration,
			0.0f, 1.0f);
		const FQuat AppliedRotation = FQuat::Slerp(
			ActionVisualPitchTransitionStart,
			ActionVisualPitchTransitionTarget, Alpha).GetNormalized();
		ActionVisualPitchComponentRotation = AppliedRotation.Rotator();
		ActionVisualPitchWeight = FMath::Lerp(
			ActionVisualPitchTransitionStartWeight,
			ActionVisualPitchTransitionTargetWeight, Alpha);
		if (Alpha >= 1.0f)
		{
			bActionVisualPitchTransitionActive = false;
		}
	}

	bActionVisualPitchActive = CachedActionVisualPitchState.bValid
		|| !ActionVisualPitchComponentRotation.IsNearlyZero(1.0e-3f)
		|| ActionVisualPitchWeight > KINDA_SMALL_NUMBER;
	if (!bActionVisualPitchActive)
	{
		ActionVisualPitchComponentRotation = FRotator::ZeroRotator;
		ActionVisualPitchWeight = 0.0f;
		ActionVisualPitchDegrees = 0.0f;
	}

	if (CachedActionVisualPitchState.bValid
		&& CachedActionVisualPitchState.bReleasing
		&& !bActionVisualPitchTransitionActive
		&& ActionVisualPitchWeight <= KINDA_SMALL_NUMBER)
	{
		Host->AcknowledgeActionVisualPitchRetirement(
			CachedActionVisualPitchState.StateRevision);
	}
}

bool UMHGZMotionMatchingAnimInstance::RequestDirectionalDodgeLocomotionStart(
	const FWeaponActionToken& SourceAction)
{
	const AMHGZCharacter* Character = Cast<AMHGZCharacter>(TryGetPawnOwner());
	const UMHGZWeaponRuntimeHostComponent* Host = Character ? Character->GetWeaponRuntimeHost() : nullptr;
	const UMHGZDodgeAbility* Source = Cast<UMHGZDodgeAbility>(SourceAction.AbilityInstance.Get());
	if (!SourceAction.IsValid() || !Host || !Host->IsActionRegistered(SourceAction)
		|| !Source || Source->GetActionToken() != SourceAction || !Source->IsDirectionalRecoveryOpen())
	{ return false; }
	PendingDirectionalDodgeStart = SourceAction;
	return true;
}

void UMHGZMotionMatchingAnimInstance::NativeUpdateAnimation(const float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	bMMForceDirectionalStartSearch = false;

	// Motion Matching state callbacks may run on the animation thread. Consume their
	// latest results here, on the AnimInstance update thread, before producing the
	// next query. This is deliberately independent of CSV capture.
	DrainMMSelectionEvents();

	AMHGZCharacter* Character = CachedCharacter.Get();
	if (!IsValid(Character))
	{
		Character = Cast<AMHGZCharacter>(TryGetPawnOwner());
		CachedCharacter = Character;
	}
	if (!IsValid(Character))
	{
		PendingDirectionalDodgeStart = {};
		ResetActionVisualPitchLocalState();
		MMActualSpeed2D = 0.0f;
		MMQuerySpeed2D = MMExpectedWorldCruiseSpeed = 0.f;
		MMUnsheathedMoveSpeedMultiplier = MMUnsheathedAppliedSpeedMultiplier = 1.f;
		bMMCanApplyUnsheathedSpeed = false;
		MMIntentQuery = 0.0f;
		MMDistanceToStopQuery = 0.0f;
		MMStopGaitQuery = 0.0f;
		MMMoveGaitQuery = 0.0f;
		bMMForceIdle = false;
		bMMHasLocomotionInput = false;
		bKinsectUpperBodyOnly = false;
		bMMUnsheathedLocomotion = false;
		bMMStopRequestActive = false;
		bMMExitWantsMoveDatabase = false;
		bMMActionIdleContextActive = false;
		bMMActionIdleContextUnsheathed = false;
		MMActionIdleContextSerial = 0;
		bMMRouteSheathedActionIdle = false;
		bMMRouteUnsheathedActionIdle = false;
		bMMHandoffActive = false;
		MMHandoffType = EMHGZMotionMatchingHandoffType::None;
		MMHandoffSerial = 0;
		MMHandoffRawMoveInput = FVector2D::ZeroVector;
		bMMHandoffHasRawMoveInput = false;
		bMMHandoffHadRawMoveInputInMobilePhase = false;
		bMMPendingStopAtHandoff = false;
		MMHandoffLastActiveRawMoveCruiseSpeed = 0.0f;
		MMLastNonZeroCruiseSpeed = 0.0f;
		MMPredictedTrajectory.Samples.Reset();
		bHasPreviousActorLocation = false;
		bStartQueryActive = false;
		bStartQueryObservedStart = false;
		bHadMoveInput = false;
		bHasPreviousUnsheathedState = false;
		bPreviousUnsheathedState = false;
		bWasForceMMIdle = false;
		bStartInputSettling = false;
		MMForceIdleReleaseHoldRemaining = 0.0f;
		MMStartInputSettleRemaining = 0.0f;
		ResetSheathedStopRequest();
		ResetLegacyStopRequest();
		ActiveSheathedGait = EMHGZStopGait::None;
		ResetActionExitRouting();
		ClearMMRuntimeSelections();
		DiscardMMHandoffConsumptions();
		DiscardMMActionIdleContextConsumptions();
		UpdateRuntimeTelemetry(nullptr, DeltaSeconds);
		return;
	}
	UpdateActionVisualPitch(Character, DeltaSeconds);
	// The kinsect GA remains in-place and lets locomotion run. Use the physical
	// movement stick (with the Character's authored deadzone), not speed or the
	// post-BlockMovement MM input flag, to decide whether its montage is upper-body-only.
	bKinsectUpperBodyOnly = Character->HasRawMovementInput();

	UpdateMotionMatchingHandoff(Character);
	// Update the public snapshot before draining acknowledgements. A graph callback
	// queues an acknowledgement after it has consumed this snapshot; consuming it on
	// the next NativeUpdate keeps the payload visible for the complete routing frame.
	DrainMMHandoffConsumptions(Character);
	DrainMMActionIdleContextConsumptions();
	bMMHasLocomotionInput = Character->bHasInput;
	bMMUnsheathedLocomotion = Character->bUnsheathed;

	const FVector CurrentLocation = Character->GetActorLocation();
	const UCharacterMovementComponent* MovementComponent = Character->GetCharacterMovement();
	const bool bMovingOnGround = MovementComponent && MovementComponent->IsMovingOnGround();
	UpdateLocomotionSpeed(*Character, DeltaSeconds);
	const bool bExternalForceMMIdle = Character->bForceMMIdle;
	const float PostActionIdleHoldDuration = CVarMHGZMMPostActionIdleHold.GetValueOnGameThread() != 0
		? MMForceIdleReleaseHoldDuration
		: 0.0f;
	if (bExternalForceMMIdle)
	{
		MMForceIdleReleaseHoldRemaining = PostActionIdleHoldDuration;
	}
	else
	{
		MMForceIdleReleaseHoldRemaining = FMath::Max(0.0f,
			MMForceIdleReleaseHoldRemaining - FMath::Max(DeltaSeconds, 0.0f));
	}
	bool bStartFromDirectionalDodge = false;
	if (PendingDirectionalDodgeStart.ActivationSequenceID != 0 && DeltaSeconds > 0.f)
	{
		const UMHGZWeaponRuntimeHostComponent* Host = Character->GetWeaponRuntimeHost();
		// The source has now retired. Any new action, stance/runtime change or real
		// input release cancels this one-shot request rather than resurrecting it later.
		// Do not require the retired AbilityInstance to survive a GC to retain its identity.
		bStartFromDirectionalDodge = Host
			&& Host->IsTokenCurrent(PendingDirectionalDodgeStart.RuntimeToken)
			&& !Host->HasRegisteredAction() && bMovingOnGround
			&& !Host->IsSheathed() && Character->bUnsheathed && Character->bHasInput
			&& Character->HasRawMovementInput()
			&& !bExternalForceMMIdle && !bMMHandoffActive
			&& MMForceIdleReleaseHoldRemaining <= KINDA_SMALL_NUMBER;
		PendingDirectionalDodgeStart = {};
	}

	// The post-action Idle hold is a legacy comparison aid and defaults to disabled.
	// Even with it disabled, bWasForceMMIdle causes one measurement-only reset when
	// the external lock releases, so the last Montage Root Motion sample is not
	// counted as locomotion. That reset does not keep the AnimGraph in forced Idle.
	//
	// A separate two-frame input settle is graph-only. It prevents the first analogue
	// sample after the deadzone from selecting Walk Start before the stick reaches its
	// final Run/Sprint lane, but deliberately does not reset Root Motion measurement.
	if (!Character->bHasInput)
	{
		MMStartInputSettleRemaining = 0.0f;
	}
	const bool bBeginStartInputSettle = !bExternalForceMMIdle
		&& !bStartFromDirectionalDodge
		// The first released Root-Motion measurement frame is an action boundary,
		// not a fresh physical stick edge. Mobile Handoff recovery below decides
		// whether it remains cruise or becomes exactly one Stop.
		&& !bWasForceMMIdle
		&& !bHadMoveInput
		&& Character->bHasInput
		&& MMQuerySpeed2D <= MMStartEligibilitySpeed
		&& MMStartInputSettleRemaining <= KINDA_SMALL_NUMBER
		&& MMStartInputSettleDuration > KINDA_SMALL_NUMBER;
	if (bBeginStartInputSettle)
	{
		MMStartInputSettleRemaining = MMStartInputSettleDuration;
	}
	bStartInputSettling = MMStartInputSettleRemaining > KINDA_SMALL_NUMBER;
	if (bStartInputSettling)
	{
		MMStartInputSettleRemaining = FMath::Max(0.0f,
			MMStartInputSettleRemaining - FMath::Max(DeltaSeconds, 0.0f));
	}

	const bool bMeasurementForceMMIdle = bExternalForceMMIdle
		|| MMForceIdleReleaseHoldRemaining > KINDA_SMALL_NUMBER;
	const bool bForceMMIdle = bMeasurementForceMMIdle || bStartInputSettling;
	bMMForceIdle = bForceMMIdle;
	if (bMMForceIdle)
	{
		bMMMobileExitCruise[0] = bMMMobileExitCruise[1] = false;
		MMLoopPhaseQuery = FVector2D::ZeroVector;
		MMLoopClock.Reset();
	}
	const bool bLeavingForcedMeasurement = !bMeasurementForceMMIdle && bWasForceMMIdle;
	const bool bPreserveHandoffInput =
		MHGZMotionMatching::ShouldPreserveHandoffInputAcrossMeasurementReset(
			bLeavingForcedMeasurement, bMMHandoffActive,
			bMMHandoffHadRawMoveInputInMobilePhase, bMMHandoffHasRawMoveInput,
			bMMPendingStopAtHandoff);
	const bool bPublishActionIdleContext =
		MHGZMotionMatching::ShouldPublishActionIdleContextOnMeasurementRelease(
			bLeavingForcedMeasurement, bMMHandoffActive, Character->bHasInput);
	if (bStartFromDirectionalDodge || MHGZMotionMatching::ShouldResetMotionMeasurement(bMeasurementForceMMIdle,
		bWasForceMMIdle, bMovingOnGround, bHasPreviousActorLocation, DeltaSeconds))
	{
		ResetMMTemporalState(CurrentLocation, bPreserveHandoffInput);
		bMMHasLocomotionInput = Character->bHasInput;
		bMMStopRequestActive = false;
		// A release sampled by the authored mobile phase has not reached the
		// normal input-edge update yet, but it must be allowed to open the Exit
		// tail so that update can select its one legitimate Stop.
		bMMExitWantsMoveDatabase = bMMHasLocomotionInput
			|| (bPreserveHandoffInput && bMMPendingStopAtHandoff);
		if (bPublishActionIdleContext)
		{
			PublishActionIdleContext(Character->bUnsheathed);
		}
		else
		{
			RefreshActionIdleRouteFlags();
		}
		if (bStartFromDirectionalDodge)
		{
			// Discard the final action displacement, not the new locomotion intent.
			// Prime Start before this update returns; otherwise a zero/flat query
			// searches Idle, then falsely starts the analogue-settle timer next frame.
			bMMForceIdle = false;
			bHadMoveInput = true;
			bHasPreviousUnsheathedState = true;
			bPreviousUnsheathedState = true;
			MMMoveGaitQuery = GetStopGaitCurveValue(GetGaitFromTargetSpeed(Character->TargetCruiseSpeed));
			MMLastNonZeroCruiseSpeed = Character->TargetCruiseSpeed;
			UpdateLegacyIntentQueries(true, true, false, 0.f);
			BuildPredictedTrajectory(Character->GetActorTransform(), 0.f,
				Character->TargetCruiseSpeed, SelectStartAcceleration(true, Character->TargetCruiseSpeed));
			ClearActionIdleRoute(EMHGZMotionMatchingNode::Unsheathed);
			bMMForceDirectionalStartSearch = true;
		}
		bWasForceMMIdle = bMeasurementForceMMIdle;
		UpdateRuntimeTelemetry(Character, DeltaSeconds);
		return;
	}

	const float FrameDistance = FVector::Dist2D(CurrentLocation, PreviousActorLocation);
	PreviousActorLocation = CurrentLocation;
	if (FrameDistance > MMTeleportResetDistance)
	{
		ResetMMTemporalState(CurrentLocation);
		bMMHasLocomotionInput = Character->bHasInput;
		bMMStopRequestActive = false;
		bMMExitWantsMoveDatabase = bMMHasLocomotionInput;
		bWasForceMMIdle = bMeasurementForceMMIdle;
		UpdateRuntimeTelemetry(Character, DeltaSeconds);
		return;
	}

	MMActualSpeed2D = FrameDistance / FMath::Max(DeltaSeconds, KINDA_SMALL_NUMBER);
	// The measured displacement came from the previous graph update. Divide by
	// that update's applied rate, never by the new target buff. PSD speeds and
	// prediction horizons stay in their original source-animation units.
	MMQuerySpeed2D = MMActualSpeed2D / GetLocomotionPlaybackRateScale(Character->bUnsheathed);

	const bool bHasInput = Character->bHasInput;
	bMMHasLocomotionInput = bHasInput;
	const bool bUnsheathed = Character->bUnsheathed;
	const float TargetSpeed = Character->TargetCruiseSpeed;
	MMSheathedTargetMoveGait = !bUnsheathed && bHasInput
		? ToNormalMoveGait(GetGaitFromTargetSpeed(TargetSpeed))
		: MHGZMotionMatching::EMHGZNormalMoveGait::None;
	if (bHasPreviousUnsheathedState && bPreviousUnsheathedState != bUnsheathed)
	{
		// Do not let a result from the other locomotion database drive this path.
		ClearMMRuntimeSelections();
		ResetSheathedStopRequest();
		ResetLegacyStopRequest();
		ActiveSheathedGait = EMHGZStopGait::None;
		ResetSheathedNormalMoveCandidateRouting();
		MMStopGaitQuery = 0.0f;
	}
	bHasPreviousUnsheathedState = true;
	bPreviousUnsheathedState = bUnsheathed;

	const bool bInputStarted = !bHadMoveInput && bHasInput
		&& MMQuerySpeed2D <= MMStartEligibilitySpeed;
	const bool bInputReleased = bHadMoveInput && !bHasInput;
	MMMoveGaitQuery = bHasInput
		? GetStopGaitCurveValue(GetGaitFromTargetSpeed(TargetSpeed)) : 0.0f;
	if (bHasInput && TargetSpeed > 1.0f)
	{
		MMLastNonZeroCruiseSpeed = TargetSpeed;
	}

	if (bInputStarted)
	{
		bStartQueryActive = true;
		bStartQueryObservedStart = false;
		MMStartQueryElapsed = 0.0f;
		MMStartQueryDuration = 0.0f;
	}
	if (!bUnsheathed)
	{
		UpdateSheathedIntentQueries(bHasInput, bInputStarted, bInputReleased, DeltaSeconds,
			TargetSpeed);
	}
	else
	{
		UpdateLegacyIntentQueries(bHasInput, bInputStarted, bInputReleased, DeltaSeconds);
	}

	const float DecelerationReferenceSpeed = FMath::Max(MMQuerySpeed2D,
		MMLastNonZeroCruiseSpeed);
	const float StopDeceleration = SelectStopDeceleration(bUnsheathed,
		DecelerationReferenceSpeed);
	if (bUnsheathed && LegacyStopMode == EMHGZLegacyStopMode::AwaitingStopCandidate)
	{
		MMDistanceToStopQuery = !bHasInput && MMQuerySpeed2D > MMIdleSpeedThreshold
			? MHGZMotionMatching::CalculateStopDistanceQuery(MMQuerySpeed2D, StopDeceleration)
			: 0.0f;
	}
	else if (bUnsheathed && LegacyStopMode != EMHGZLegacyStopMode::FollowingStopCurve)
	{
		// No player release has armed a legacy Stop. In particular, do not turn
		// residual action displacement into a generic Stop-distance query.
		MMDistanceToStopQuery = 0.0f;
	}

	const float TrajectoryTargetSpeed = bHasInput ? TargetSpeed : 0.0f;
	const float TrajectoryAcceleration = TrajectoryTargetSpeed >= MMQuerySpeed2D
		? SelectStartAcceleration(bUnsheathed, TrajectoryTargetSpeed)
		: StopDeceleration;
	BuildPredictedTrajectory(Character->GetActorTransform(), MMQuerySpeed2D,
		TrajectoryTargetSpeed, TrajectoryAcceleration);

	bMMStopRequestActive = SheathedStopMode != EMHGZSheathedStopMode::None
		|| LegacyStopMode != EMHGZLegacyStopMode::None;
	bMMExitWantsMoveDatabase = bMMHasLocomotionInput || bMMStopRequestActive;
	bHadMoveInput = bHasInput;
	bWasForceMMIdle = false;
	UpdateRuntimeTelemetry(Character, DeltaSeconds);
}

void UMHGZMotionMatchingAnimInstance::UpdateSheathedIntentQueries(
	const bool bHasInput, const bool bInputStarted, const bool bInputReleased,
	const float DeltaSeconds, const float TargetSpeed)
{
	if (bInputStarted)
	{
		// A new locomotion request invalidates the previous stop selection. The next
		// Motion Matching callback will publish the new Start/Loop result.
		ClearMMRuntimeSelections();
		ResetSheathedStopRequest();
	}

	if (bHasInput)
	{
		// This is intentionally derived from the actual Motion Matching result,
		// rather than the most recent analogue magnitude. A player can ease the
		// stick down from Run before releasing it; the selected Run animation is
		// still the correct gait family for its Stop.
		if (const FMotionMatchingSelectionEvent* Selection = GetLatestSelection(
			EMHGZMotionMatchingNode::Sheathed))
		{
			const EMHGZStopGait SelectionGait = GetGaitFromSelection(Selection);
			if (SelectionGait != EMHGZStopGait::None)
			{
				ActiveSheathedGait = SelectionGait;
			}
		}
		if (ActiveSheathedGait == EMHGZStopGait::None)
		{
			ActiveSheathedGait = GetGaitFromTargetSpeed(TargetSpeed);
		}

		// Re-pressing the stick before the stop animation finishes cancels the stop
		// request. It does not reuse the old stop animation as the new result.
		if (SheathedStopMode != EMHGZSheathedStopMode::None)
		{
			ResetSheathedStopRequest();
			ClearMMRuntimeSelections();
		}

		if (bStartQueryActive)
		{
			if (bStartInputSettling)
			{
				// bMMForceIdle keeps the graph on Idle during this tiny handoff.
				// Preserve the +Start semantic without advancing its timer so the
				// first real search sees the settled analogue lane at time zero.
				MMIntentQuery = 1.0f;
				MMDistanceToStopQuery = 0.0f;
				MMStopGaitQuery = 0.0f;
				return;
			}

			UpdateStartIntentQuery(EMHGZMotionMatchingNode::Sheathed, DeltaSeconds);
		}
		else
		{
			MMIntentQuery = 0.0f;
		}
		MMDistanceToStopQuery = 0.0f;
		MMStopGaitQuery = 0.0f;
		return;
	}

	if (bInputReleased)
	{
		// The falling edge is the only source of the initial stop request. It is
		// intentionally independent of measured speed and does not predict a
		// stop-distance value before a stop asset has actually been selected.
		ResetSheathedStopRequest();
		SheathedStopMode = EMHGZSheathedStopMode::AwaitingExtendedStopCandidate;
		// A mobile action-exit owns the gait at its release edge. The ordinary
		// locomotion gait can be stale (for example, Walk before a full-stick
		// MoveExit), so it is intentionally only a fallback for non-handoff stops.
		LatchedSheathedStopGait = bMMPendingStopAtHandoff
			? GetGaitFromTargetSpeed(MMHandoffLastActiveRawMoveCruiseSpeed)
			: EMHGZStopGait::None;
		if (LatchedSheathedStopGait == EMHGZStopGait::None)
		{
			LatchedSheathedStopGait = ActiveSheathedGait;
		}
		if (LatchedSheathedStopGait == EMHGZStopGait::None)
		{
			LatchedSheathedStopGait = GetGaitFromSelection(GetLatestSelection(
				EMHGZMotionMatchingNode::Sheathed));
		}
		if (LatchedSheathedStopGait == EMHGZStopGait::None)
		{
			LatchedSheathedStopGait = GetGaitFromTargetSpeed(TargetSpeed);
		}
	}
	bStartQueryActive = false;
	bStartQueryObservedStart = false;
	MMStartQueryElapsed = 0.0f;
	MMStartQueryDuration = 0.0f;

	const FMotionMatchingSelectionEvent* Selection = GetLatestSelection(
		EMHGZMotionMatchingNode::Sheathed);
	if (SheathedStopMode == EMHGZSheathedStopMode::AwaitingExtendedStopCandidate
		&& Selection && IsExtendedStopAnimation(*Selection))
	{
		BeginFollowingSheathedStop(*Selection);
	}

	if (SheathedStopMode == EMHGZSheathedStopMode::FollowingExtendedStopCurve
		&& Selection && IsExtendedStopAnimation(*Selection))
	{
		if (!IsContinuingAcceptedSheathedStop(*Selection))
		{
			// The release edge has already consumed one Stop. A non-continuing
			// re-search into a Stop entry must not restart its -1 -> 0 curve.
			ResetSheathedStopRequest();
		}
		else
		{
			const float SampledIntent = EvaluateSelectedCurve(*Selection, FName(TEXT("MM_Intent")));
			const float SampledDistance = EvaluateSelectedCurve(*Selection,
				FName(TEXT("MM_DistanceToStop")));
			MMIntentQuery = FMath::Clamp(SampledIntent, -1.0f, 0.0f);
			MMDistanceToStopQuery = FMath::Min(SampledDistance, 0.0f);
			// The release edge owns the Stop family. Never adopt the lane of a
			// candidate that happened to win one search: doing so makes a Run Stop
			// selected as Walk self-confirm on the next frame. Generated Stop assets
			// keep their indexed MM_StopGait lane constant for the same reason.
			MMStopGaitQuery = GetStopGaitCurveValue(LatchedSheathedStopGait);

			// A generated stop asset ends with both curves at zero. Clear the mode on
			// that terminal sample so the following idle/loop search is not held back.
			if (FMath::IsNearlyZero(MMIntentQuery, 0.02f)
				&& FMath::IsNearlyZero(MMDistanceToStopQuery, 0.5f))
			{
				ResetSheathedStopRequest();
				MMIntentQuery = 0.0f;
				MMDistanceToStopQuery = 0.0f;
				MMStopGaitQuery = 0.0f;
			}
			return;
		}
	}
	if (SheathedStopMode == EMHGZSheathedStopMode::FollowingExtendedStopCurve)
	{
		// The selected result has already left the stop asset. Do not leave the
		// internal mode latched if a database transition reaches Idle directly.
		ResetSheathedStopRequest();
	}

	if (SheathedStopMode == EMHGZSheathedStopMode::AwaitingExtendedStopCandidate)
	{
		MMIntentQuery = -1.0f;
		MMDistanceToStopQuery = 0.0f;
		MMStopGaitQuery = GetStopGaitCurveValue(LatchedSheathedStopGait);
	}
	else
	{
		MMIntentQuery = 0.0f;
		MMDistanceToStopQuery = 0.0f;
		MMStopGaitQuery = 0.0f;
	}
}

void UMHGZMotionMatchingAnimInstance::ResetSheathedStopRequest()
{
	SheathedStopMode = EMHGZSheathedStopMode::None;
	LatchedSheathedStopGait = EMHGZStopGait::None;
	ActiveSheathedStopAnimation.Reset();
	LastSheathedStopSelectionFrame = 0;
	LastSheathedStopAnimationTime = 0.0f;
}

void UMHGZMotionMatchingAnimInstance::BeginFollowingSheathedStop(
	const FMotionMatchingSelectionEvent& Selection)
{
	check(IsExtendedStopAnimation(Selection));
	SheathedStopMode = EMHGZSheathedStopMode::FollowingExtendedStopCurve;
	ActiveSheathedStopAnimation = Selection.Animation;
	LastSheathedStopSelectionFrame = Selection.Frame;
	LastSheathedStopAnimationTime = Selection.AnimationTime;
}

bool UMHGZMotionMatchingAnimInstance::IsContinuingAcceptedSheathedStop(
	const FMotionMatchingSelectionEvent& Selection)
{
	const bool bSameAnimation = ActiveSheathedStopAnimation.IsValid()
		&& ActiveSheathedStopAnimation == Selection.Animation;
	const bool bHasNewSelectionEvent = Selection.Frame > LastSheathedStopSelectionFrame;
	if (!MHGZMotionMatching::ShouldContinueConsumedStopSelection(bSameAnimation,
		bHasNewSelectionEvent, Selection.bIsContinuing, LastSheathedStopAnimationTime,
		Selection.AnimationTime))
	{
		return false;
	}

	if (bHasNewSelectionEvent)
	{
		LastSheathedStopSelectionFrame = Selection.Frame;
		LastSheathedStopAnimationTime = Selection.AnimationTime;
	}
	return true;
}

void UMHGZMotionMatchingAnimInstance::ResetLegacyStopRequest()
{
	LegacyStopMode = EMHGZLegacyStopMode::None;
	ActiveLegacyStopAnimation.Reset();
	LastLegacyStopSelectionFrame = 0;
	LastLegacyStopAnimationTime = 0.0f;
}

void UMHGZMotionMatchingAnimInstance::BeginFollowingLegacyStop(
	const FMotionMatchingSelectionEvent& Selection)
{
	check(IsLegacyStopAnimation(Selection));
	LegacyStopMode = EMHGZLegacyStopMode::FollowingStopCurve;
	ActiveLegacyStopAnimation = Selection.Animation;
	LastLegacyStopSelectionFrame = Selection.Frame;
	LastLegacyStopAnimationTime = Selection.AnimationTime;
}

bool UMHGZMotionMatchingAnimInstance::IsContinuingAcceptedLegacyStop(
	const FMotionMatchingSelectionEvent& Selection)
{
	const bool bSameAnimation = ActiveLegacyStopAnimation.IsValid()
		&& ActiveLegacyStopAnimation == Selection.Animation;
	const bool bHasNewSelectionEvent = Selection.Frame > LastLegacyStopSelectionFrame;
	if (!MHGZMotionMatching::ShouldContinueConsumedStopSelection(bSameAnimation,
		bHasNewSelectionEvent, Selection.bIsContinuing, LastLegacyStopAnimationTime,
		Selection.AnimationTime))
	{
		return false;
	}

	if (bHasNewSelectionEvent)
	{
		LastLegacyStopSelectionFrame = Selection.Frame;
		LastLegacyStopAnimationTime = Selection.AnimationTime;
	}
	return true;
}

void UMHGZMotionMatchingAnimInstance::UpdateLegacyIntentQueries(
	const bool bHasInput, const bool bInputStarted, const bool bInputReleased, const float DeltaSeconds)
{
	const FMotionMatchingSelectionEvent* Selection = GetLatestSelection(
		EMHGZMotionMatchingNode::Unsheathed);
	if (bHasInput)
	{
		ResetLegacyStopRequest();
	}
	else if (bInputReleased)
	{
		// A locomotion input falling edge is the only legal source of a Stop
		// request. Residual action displacement has no such edge.
		ResetLegacyStopRequest();
		LegacyStopMode = EMHGZLegacyStopMode::AwaitingStopCandidate;
	}

	if (LegacyStopMode == EMHGZLegacyStopMode::AwaitingStopCandidate
		&& Selection && IsLegacyStopAnimation(*Selection))
	{
		BeginFollowingLegacyStop(*Selection);
	}
	if (LegacyStopMode == EMHGZLegacyStopMode::FollowingStopCurve
		&& Selection && IsLegacyStopAnimation(*Selection))
	{
		if (!IsContinuingAcceptedLegacyStop(*Selection))
		{
			// A release may consume only one Stop. A fresh search into the same
			// asset cannot restart it from an earlier frame.
			ResetLegacyStopRequest();
			MMIntentQuery = 0.0f;
			MMDistanceToStopQuery = 0.0f;
		}
		else
		{
			MMIntentQuery = FMath::Clamp(EvaluateSelectedCurve(*Selection,
				FName(TEXT("MM_Intent"))), -1.0f, 0.0f);
			MMDistanceToStopQuery = FMath::Min(EvaluateSelectedCurve(*Selection,
				FName(TEXT("MM_DistanceToStop"))), 0.0f);
			if (FMath::IsNearlyZero(MMIntentQuery, 0.02f)
				&& FMath::IsNearlyZero(MMDistanceToStopQuery, 0.5f))
			{
				ResetLegacyStopRequest();
				MMIntentQuery = 0.0f;
				MMDistanceToStopQuery = 0.0f;
			}
			return;
		}
	}
	if (LegacyStopMode == EMHGZLegacyStopMode::FollowingStopCurve)
	{
		// The Stop has left the current selection before its terminal sample.
		// Do not hold a stale Stop semantic over the next global search.
		ResetLegacyStopRequest();
	}

	if (!bHasInput)
	{
		bStartQueryActive = false;
		bStartQueryObservedStart = false;
		MMStartQueryElapsed = 0.0f;
		MMStartQueryDuration = 0.0f;
	}

	if (bInputStarted)
	{
		ClearMMRuntimeSelections();
		Selection = nullptr;
		bStartQueryActive = true;
		bStartQueryObservedStart = false;
		MMStartQueryElapsed = 0.0f;
		MMStartQueryDuration = 0.0f;
	}

	if (bStartQueryActive)
	{
		UpdateStartIntentQuery(EMHGZMotionMatchingNode::Unsheathed, DeltaSeconds);
	}
	else if (LegacyStopMode == EMHGZLegacyStopMode::AwaitingStopCandidate)
	{
		MMIntentQuery = -1.0f;
	}
	else
	{
		MMIntentQuery = 0.0f;
	}
}

void UMHGZMotionMatchingAnimInstance::UpdateStartIntentQuery(
	const EMHGZMotionMatchingNode MotionMatchingNode, const float DeltaSeconds)
{
	const FMotionMatchingSelectionEvent* Selection = GetLatestSelection(MotionMatchingNode);
	if (Selection && IsStartAnimation(*Selection))
	{
		bStartQueryObservedStart = true;
		const UAnimSequenceBase* Sequence = Cast<UAnimSequenceBase>(Selection->Animation.Get());
		MMStartQueryElapsed = FMath::Max(0.0f, Selection->AnimationTime);
		MMStartQueryDuration = Sequence ? Sequence->GetPlayLength() : 0.0f;
		MMIntentQuery = FMath::Clamp(EvaluateSelectedCurve(*Selection, FName(TEXT("MM_Intent"))),
			0.0f, 1.0f);
		if (MMIntentQuery <= KINDA_SMALL_NUMBER)
		{
			bStartQueryActive = false;
		}
		return;
	}

	if (bStartQueryObservedStart)
	{
		// The selected Start has handed over to a Loop/other valid candidate. Do
		// not retain a stale positive semantic after that natural handoff.
		bStartQueryActive = false;
		MMStartQueryElapsed = 0.0f;
		MMStartQueryDuration = 0.0f;
		MMIntentQuery = 0.0f;
		return;
	}

	// Before the first Start result returns from the Motion Matching node, keep
	// the request at +1. The two-frame analogue settle occurs before this path,
	// so this cannot preserve the former deadzone Walk sample.
	MMIntentQuery = 1.0f;
	MMStartQueryElapsed += FMath::Max(DeltaSeconds, 0.0f);
}

bool UMHGZMotionMatchingAnimInstance::IsStartAnimation(
	const FMotionMatchingSelectionEvent& Selection) const
{
	const UObject* Animation = Selection.Animation.Get();
	return IsValid(Animation)
		&& Animation->GetName().Contains(TEXT("_Start"), ESearchCase::IgnoreCase);
}

bool UMHGZMotionMatchingAnimInstance::IsExtendedStopAnimation(
	const FMotionMatchingSelectionEvent& Selection) const
{
	const UObject* Animation = Selection.Animation.Get();
	if (!IsValid(Animation))
	{
		return false;
	}

	const FString AnimationName = Animation->GetName();
	return AnimationName.Contains(TEXT("FirstStepCommitStop"), ESearchCase::IgnoreCase)
		|| AnimationName.Contains(TEXT("_Stop_Left_Extended"), ESearchCase::IgnoreCase)
		|| AnimationName.Contains(TEXT("_Stop_Right_Extended"), ESearchCase::IgnoreCase);
}

bool UMHGZMotionMatchingAnimInstance::IsLegacyStopAnimation(
	const FMotionMatchingSelectionEvent& Selection) const
{
	const UObject* Animation = Selection.Animation.Get();
	return IsValid(Animation)
		&& Animation->GetName().Equals(TEXT("AS_UnSh_Walk_Stop"), ESearchCase::IgnoreCase);
}

UMHGZMotionMatchingAnimInstance::EMHGZStopGait
UMHGZMotionMatchingAnimInstance::GetGaitFromSelection(
	const FMotionMatchingSelectionEvent* Selection) const
{
	const UObject* Animation = Selection ? Selection->Animation.Get() : nullptr;
	if (!IsValid(Animation))
	{
		return EMHGZStopGait::None;
	}

	const FString AnimationName = Animation->GetName();
	if (AnimationName.Contains(TEXT("Sprint"), ESearchCase::IgnoreCase))
	{
		return EMHGZStopGait::Sprint;
	}
	if (AnimationName.Contains(TEXT("Run"), ESearchCase::IgnoreCase))
	{
		return EMHGZStopGait::Run;
	}
	if (AnimationName.Contains(TEXT("Walk"), ESearchCase::IgnoreCase))
	{
		return EMHGZStopGait::Walk;
	}
	return EMHGZStopGait::None;
}

UMHGZMotionMatchingAnimInstance::EMHGZStopGait
UMHGZMotionMatchingAnimInstance::GetGaitFromTargetSpeed(const float TargetSpeed) const
{
	if (TargetSpeed <= KINDA_SMALL_NUMBER)
	{
		return EMHGZStopGait::None;
	}

	// This is only a fail-safe for a release that occurs before the first
	// Motion Matching callback. Normal releases always use the selected
	// animation's family above, so Blueprint-tuned cruise speeds do not enter
	// this decision.
	return TargetSpeed >= 520.0f ? EMHGZStopGait::Sprint
		: (TargetSpeed >= 300.0f ? EMHGZStopGait::Run : EMHGZStopGait::Walk);
}

MHGZMotionMatching::EMHGZNormalMoveGait
UMHGZMotionMatchingAnimInstance::ToNormalMoveGait(const EMHGZStopGait Gait)
{
	switch (Gait)
	{
	case EMHGZStopGait::Walk:
		return MHGZMotionMatching::EMHGZNormalMoveGait::Walk;
	case EMHGZStopGait::Run:
		return MHGZMotionMatching::EMHGZNormalMoveGait::Run;
	case EMHGZStopGait::Sprint:
		return MHGZMotionMatching::EMHGZNormalMoveGait::Sprint;
	case EMHGZStopGait::None:
	default:
		return MHGZMotionMatching::EMHGZNormalMoveGait::None;
	}
}

MHGZMotionMatching::EMHGZNormalMoveGait
UMHGZMotionMatchingAnimInstance::GetExactSheathedLoopGait(
	const FMotionMatchingSelectionEvent* Selection) const
{
	const UObject* Animation = Selection ? Selection->Animation.Get() : nullptr;
	if (Animation && Animation == MMSheathedRunLoopSequence.Get())
	{
		return MHGZMotionMatching::EMHGZNormalMoveGait::Run;
	}
	if (Animation && Animation == MMSheathedSprintLoopSequence.Get())
	{
		return MHGZMotionMatching::EMHGZNormalMoveGait::Sprint;
	}
	return MHGZMotionMatching::EMHGZNormalMoveGait::None;
}

float UMHGZMotionMatchingAnimInstance::GetStopGaitCurveValue(const EMHGZStopGait Gait)
{
	switch (Gait)
	{
	case EMHGZStopGait::Walk:
		return 1.0f / 3.0f;
	case EMHGZStopGait::Run:
		return 2.0f / 3.0f;
	case EMHGZStopGait::Sprint:
		return 1.0f;
	default:
		return 0.0f;
	}
}

float UMHGZMotionMatchingAnimInstance::EvaluateSelectedCurve(
	const FMotionMatchingSelectionEvent& Selection, const FName CurveName) const
{
	const UAnimSequenceBase* Sequence = Cast<UAnimSequenceBase>(Selection.Animation.Get());
	if (!Sequence)
	{
		return 0.0f;
	}

	const float SampleTime = FMath::Clamp(Selection.AnimationTime, 0.0f,
		Sequence->GetPlayLength());
	return Sequence->EvaluateCurveData(CurveName,
		FAnimExtractContext(static_cast<double>(SampleTime)), true);
}

void UMHGZMotionMatchingAnimInstance::UpdateMotionMatchingHandoff(
	const AMHGZCharacter* Character)
{
	bMMHandoffActive = false;
	MMHandoffType = EMHGZMotionMatchingHandoffType::None;
	MMHandoffSerial = 0;
	MMHandoffRawMoveInput = FVector2D::ZeroVector;
	bMMHandoffHasRawMoveInput = false;
	bMMHandoffHadRawMoveInputInMobilePhase = false;
	bMMPendingStopAtHandoff = false;
	MMHandoffLastActiveRawMoveCruiseSpeed = 0.0f;

	const UMHGZWeaponRuntimeHostComponent* Host = Character
		? Character->GetWeaponRuntimeHost() : nullptr;
	FWeaponMotionMatchingHandoff Handoff;
	if (!Host || !Host->GetPendingMotionMatchingHandoff(Handoff))
	{
		return;
	}

	bMMHandoffActive = true;
	MMHandoffType = Handoff.Type;
	MMHandoffSerial = Handoff.Serial;
	MMHandoffRawMoveInput = Handoff.RawMoveInput;
	bMMHandoffHasRawMoveInput = Handoff.bHasRawMoveInputAtHandoff;
	bMMHandoffHadRawMoveInputInMobilePhase = Handoff.bHadRawMoveInputInMobilePhase;
	bMMPendingStopAtHandoff = Handoff.bPendingStopAtHandoff;
	MMHandoffLastActiveRawMoveCruiseSpeed = Handoff.LastActiveRawMoveCruiseSpeedInMobilePhase;
}

void UMHGZMotionMatchingAnimInstance::DrainMMHandoffConsumptions(
	AMHGZCharacter* Character)
{
	TSet<int64> PendingSerials;
	int64 HandoffSerial = 0;
	while (MMPendingHandoffConsumptionSerials.Dequeue(HandoffSerial))
	{
		if (HandoffSerial > 0)
		{
			PendingSerials.Add(HandoffSerial);
		}
	}

	UMHGZWeaponRuntimeHostComponent* Host = Character
		? Character->GetWeaponRuntimeHost() : nullptr;
	if (!Host)
	{
		return;
	}

	for (const int64 PendingSerial : PendingSerials)
	{
		// The Host verifies the serial, so a stale graph acknowledgement can never
		// clear a newer action's handoff payload.
		Host->ClearPendingMotionMatchingHandoff(PendingSerial);
	}
}

void UMHGZMotionMatchingAnimInstance::DiscardMMHandoffConsumptions()
{
	int64 DiscardedSerial = 0;
	while (MMPendingHandoffConsumptionSerials.Dequeue(DiscardedSerial))
	{
	}
}

void UMHGZMotionMatchingAnimInstance::PublishActionIdleContext(const bool bUnsheathed)
{
	bMMActionIdleContextActive = true;
	bMMActionIdleContextUnsheathed = bUnsheathed;
	MMActionIdleContextSerial = NextMMActionIdleContextSerial++;
	EnterActionIdleRoute(bUnsheathed ? EMHGZMotionMatchingNode::Unsheathed
		: EMHGZMotionMatchingNode::Sheathed);
	if (NextMMActionIdleContextSerial <= 0)
	{
		NextMMActionIdleContextSerial = 1;
	}
}

void UMHGZMotionMatchingAnimInstance::RefreshActionIdleRouteFlags()
{
	bMMRouteSheathedActionIdle = bMMActionIdleContextActive
		&& !bMMActionIdleContextUnsheathed;
	bMMRouteUnsheathedActionIdle = bMMActionIdleContextActive
		&& bMMActionIdleContextUnsheathed;
}

void UMHGZMotionMatchingAnimInstance::DrainMMActionIdleContextConsumptions()
{
	TSet<int64> PendingSerials;
	int64 ContextSerial = 0;
	while (MMPendingActionIdleContextConsumptionSerials.Dequeue(ContextSerial))
	{
		if (ContextSerial > 0)
		{
			PendingSerials.Add(ContextSerial);
		}
	}

	// Since E4.2 pre-search routing owns the persistent ActionIdle candidate set,
	// an old post-search Blueprint acknowledgement must never clear it. Keep this
	// queue drained for backwards compatibility with already-open editor sessions.
	(void)PendingSerials;
}

void UMHGZMotionMatchingAnimInstance::DiscardMMActionIdleContextConsumptions()
{
	int64 DiscardedSerial = 0;
	while (MMPendingActionIdleContextConsumptionSerials.Dequeue(DiscardedSerial))
	{
	}
}

void UMHGZMotionMatchingAnimInstance::DrainMMSelectionEvents()
{
	FMotionMatchingSelectionEvent Event;
	while (MMPendingSelectionEvents.Dequeue(Event))
	{
		const int32 NodeIndex = static_cast<int32>(Event.Node);
		if (NodeIndex >= 0 && NodeIndex < UE_ARRAY_COUNT(MMLatestSelection)
			&& (!bHasLatestSelection[NodeIndex] || Event.Frame >= MMLatestSelection[NodeIndex].Frame))
		{
			MMLatestSelection[NodeIndex] = Event;
			bHasLatestSelection[NodeIndex] = true;
		}

		if (!bRuntimeTelemetryActive)
		{
			continue;
		}

		AppendMMSelectionTelemetry(Event);
	}
}

void UMHGZMotionMatchingAnimInstance::DiscardMMSelectionEvents()
{
	FMotionMatchingSelectionEvent DiscardedEvent;
	while (MMPendingSelectionEvents.Dequeue(DiscardedEvent))
	{
	}
}

void UMHGZMotionMatchingAnimInstance::ClearMMRuntimeSelections()
{
	for (bool& bHasSelection : bHasLatestSelection)
	{
		bHasSelection = false;
	}
	for (FMotionMatchingSelectionEvent& Selection : MMLatestSelection)
	{
		Selection = FMotionMatchingSelectionEvent();
	}
}

const UMHGZMotionMatchingAnimInstance::FMotionMatchingSelectionEvent*
UMHGZMotionMatchingAnimInstance::GetLatestSelection(
	const EMHGZMotionMatchingNode MotionMatchingNode) const
{
	const int32 NodeIndex = static_cast<int32>(MotionMatchingNode);
	return NodeIndex >= 0 && NodeIndex < UE_ARRAY_COUNT(MMLatestSelection)
		&& bHasLatestSelection[NodeIndex]
		? &MMLatestSelection[NodeIndex]
		: nullptr;
}

float UMHGZMotionMatchingAnimInstance::GetPredictedForwardDistance(const float TimeInSeconds,
	const FVector& Origin, const FVector& TrajectoryForward) const
{
	for (const FTransformTrajectorySample& Sample : MMPredictedTrajectory.Samples)
	{
		if (FMath::IsNearlyEqual(Sample.TimeInSeconds, TimeInSeconds, KINDA_SMALL_NUMBER))
		{
			return FVector::DotProduct(Sample.Position - Origin, TrajectoryForward);
		}
	}
	return 0.0f;
}

void UMHGZMotionMatchingAnimInstance::ResetMMTemporalState(const FVector& CurrentLocation,
	const bool bPreserveMoveInput)
{
	PreviousActorLocation = CurrentLocation;
	bHasPreviousActorLocation = true;
	bStartQueryActive = false;
	bStartQueryObservedStart = false;
	bHadMoveInput = bPreserveMoveInput;
	bHasPreviousUnsheathedState = false;
	bPreviousUnsheathedState = false;
	MMStartQueryElapsed = 0.0f;
	MMStartQueryDuration = 0.0f;
	MMActualSpeed2D = 0.0f;
	MMIntentQuery = 0.0f;
	MMQuerySpeed2D = 0.f;
	MMDistanceToStopQuery = 0.0f;
	MMStopGaitQuery = 0.0f;
	MMMoveGaitQuery = 0.0f;
	bMMStopRequestActive = false;
	bMMExitWantsMoveDatabase = false;
	MMLastNonZeroCruiseSpeed = 0.0f;
	ResetSheathedStopRequest();
	ResetLegacyStopRequest();
	ActiveSheathedGait = EMHGZStopGait::None;
	ResetSheathedNormalMoveCandidateRouting();
	bStartInputSettling = false;
	MMStartInputSettleRemaining = 0.0f;
	ClearMMRuntimeSelections();

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
	const FVector TrajectoryForward = MHGZMotionMatching::GetImportedLocomotionTrajectoryForward(
		ActorTransform);
	for (const float Time : PredictionTimes)
	{
		FTransformTrajectorySample& Sample = MMPredictedTrajectory.Samples.AddDefaulted_GetRef();
		Sample.TimeInSeconds = Time;
		Sample.Facing = ActorTransform.GetRotation();
		Sample.Position = CurrentLocation + TrajectoryForward *
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
