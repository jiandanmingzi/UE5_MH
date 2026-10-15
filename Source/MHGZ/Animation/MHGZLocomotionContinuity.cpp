// Copyright MHGZ Project. All Rights Reserved.
#include "Animation/MHGZMotionMatchingAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/MHGZPoseSearchFeatureChannel_LoopPhase.h"
#include "PoseSearch/AnimNode_MotionMatching.h"
#include "PoseSearch/MotionMatchingAnimNodeLibrary.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "UObject/UnrealType.h"
#include "UObject/StructOnScope.h"

const UPoseSearchDatabase* UMHGZMotionMatchingAnimInstance::GetLoopQueryDatabase(bool bUnsheathed) const
{ return MMLoopQueryDatabases[bUnsheathed ? 1 : 0]; }

bool UMHGZMotionMatchingAnimInstance::WantsLoopEntryMatch(bool bUnsheathed) const
{ return bMMLoopEntryMatch[bUnsheathed ? 1 : 0]; }

void UMHGZMotionMatchingAnimInstance::PublishLoopPhaseQuery(bool bUnsheathed,
	const FVector2D& Phase, FName Source)
{
	(bUnsheathed ? MMUnsheathedLoopPhaseQuery : MMLoopPhaseQuery) = Phase;
	MMLoopQuerySource[bUnsheathed ? 1 : 0] = Source;
}

void UMHGZMotionMatchingAnimInstance::PrepareLoopContinuity(const FAnimUpdateContext& Context,
	const FAnimNodeReference& Reference, EMHGZMotionMatchingNode Node)
{
	const int32 I = static_cast<int32>(Node);
	MMLoopQueryDatabases[I] = nullptr;
	bMMLoopEntryMatch[I] = false;
	MMLoopHistoryEntriesRemoved[I] = 0;
	PublishLoopPhaseQuery(I == 1, FVector2D::ZeroVector, NAME_None);
	const auto* Update = Context.GetContext();
	MMNodeUpdateDelta[I] = Update ? Update->GetDeltaTime() : 0.f;
	auto* MM = Reference.GetAnimNodePtr<FAnimNode_MotionMatching>();
	if (!MM) { return; }
	EAnimNodeReferenceConversionResult Conversion;
	const auto Ref = UMotionMatchingAnimNodeLibrary::ConvertToMotionMatchingNode(Reference, Conversion);
	bool bValid = false;
	if (!bMMBaseBlendCaptured[I])
	{
		// The UE5.6 blend-settings constructor is not DLL-exported. Initialize via
		// its exported UScriptStruct instead of defining a duplicate constructor.
		MMBaseBlend[I] = MakeShared<FStructOnScope>(FMotionMatchingBlueprintBlendSettings::StaticStruct());
		auto& Settings = *reinterpret_cast<FMotionMatchingBlueprintBlendSettings*>(MMBaseBlend[I]->GetStructMemory());
		UMotionMatchingAnimNodeLibrary::GetMotionMatchingBlendSettings(Ref, Settings, bValid);
		// UE5.6 GetMotionMatchingBlendSettings fills the settings but never assigns
		// its bIsResultValid out parameter. The typed node reference above is the
		// validity check; trusting the unchanged bool would silently disable this route.
		bMMBaseBlendCaptured[I] = true;
	}
	if (bMMBaseBlendCaptured[I])
	{
		// Restore the authored settings before routing. Stop/Start/Idle never inherit
		// the Loop-only inertial override from a preceding update.
		UMotionMatchingAnimNodeLibrary::OverrideMotionMatchingBlendSettings(Ref,
			*reinterpret_cast<FMotionMatchingBlueprintBlendSettings*>(MMBaseBlend[I]->GetStructMemory()), bValid);
	}
	if (I != 0) { return; }
	if (!bMMHasLocomotionInput || bMMForceIdle || bStartQueryActive || bMMStopRequestActive
		|| bMMUnsheathedLocomotion)
	{
		MMLoopClock.Reset();
		return;
	}
	if (MMLoopClockFrame != GFrameCounter)
	{
		MMLoopClockFrame = GFrameCounter;
		if (MMLoopClock.bValid) { MMLoopClock.Advance(); }
		else if (MM->GetAnimAsset() == MMSheathedRunLoopSequence
			|| MM->GetAnimAsset() == MMSheathedSprintLoopSequence)
		{
			// Seed from the live player's time, never last frame's search-result time.
			MMLoopClock.Seed(MM->GetAccumulatedTime(), MM->GetCurrentAssetLength());
		}
	}
	if (MMLoopClock.bValid)
	{
		PublishLoopPhaseQuery(false, UMHGZPoseSearchFeatureChannel_LoopPhase::EncodePhase(
			float(MMLoopClock.Cycle), 1.f), TEXT("CycleClock"));
	}
}

void UMHGZMotionMatchingAnimInstance::InstallLoopDatabase(const FMotionMatchingAnimNodeReference& Reference,
	EMHGZMotionMatchingNode Node, UPoseSearchDatabase* Database, bool bEntryMatch)
{
	const int32 I = static_cast<int32>(Node);
	MMLoopQueryDatabases[I] = Database;
	bMMLoopEntryMatch[I] = bEntryMatch;
	auto* MM = Reference.GetAnimNodePtr<FAnimNode_MotionMatching>();
	const auto* Entry = Database ? Database->GetDatabaseAnimationAsset<FPoseSearchDatabaseSequence>(0) : nullptr;
	if (MM && Entry && Entry->Sequence)
	{
		if (bEntryMatch || MM->GetAnimAsset() != Entry->Sequence)
		{
			// UE5.6 exposes the state read-only in C++, but its reflected USTRUCT is
			// writable. Touch only history keys belonging to this explicit Loop bank.
			// FullMove Stop history and the node's authored history duration survive.
			static const FStructProperty* StateProperty = FindFProperty<FStructProperty>(
				FAnimNode_MotionMatching::StaticStruct(), TEXT("MotionMatchingState"));
			check(StateProperty && StateProperty->Struct == FMotionMatchingState::StaticStruct());
			auto* State = StateProperty->ContainerPtrToValuePtr<FMotionMatchingState>(MM);
			const FObjectKey TargetKey(Database);
			for (auto It = State->PoseIndicesHistory.IndexToTime.CreateIterator(); It; ++It)
			{
				if (It.Key().DatabaseKey == TargetKey)
				{ It.RemoveCurrent(); ++MMLoopHistoryEntriesRemoved[I]; }
			}
		}
		if (bMMBaseBlendCaptured[I])
		{
			auto& Blend = *reinterpret_cast<FMotionMatchingBlueprintBlendSettings*>(MMBaseBlend[I]->GetStructMemory());
			const bool bAuthoredInertialBlend = Blend.bUseInertialBlend;
			const float AuthoredDuration = Blend.BlendTime;
			const UAnimationAsset* Source = MM->GetAnimAsset();
			const bool bSourceGaitLoop = Source == MMSheathedRunLoopSequence || Source == MMSheathedSprintLoopSequence;
			const bool bTargetGaitLoop = Entry->Sequence == MMSheathedRunLoopSequence || Entry->Sequence == MMSheathedSprintLoopSequence;
			// Fast gait reversals need inertial poses, but an action/Start entrance
			// needs both live Root Motion players, not an immediate root-velocity switch.
			Blend.bUseInertialBlend = !bEntryMatch && bSourceGaitLoop && bTargetGaitLoop && Source != Entry->Sequence;
			if (bEntryMatch)
			{
				Blend.BlendTime = FMHGZLocomotionBlendPolicy::EntryDuration(AuthoredDuration,
					MM->GetCurrentAssetLength(), MM->GetAccumulatedTime(), MMNodeUpdateDelta[I]);
			}
			bool bValid = false;
			UMotionMatchingAnimNodeLibrary::OverrideMotionMatchingBlendSettings(Reference, Blend, bValid);
			Blend.bUseInertialBlend = bAuthoredInertialBlend;
			Blend.BlendTime = AuthoredDuration;
		}
		if (I == 0 && MMLoopClock.bValid)
		{ MMLoopClock.Schedule(MMNodeUpdateDelta[I], Entry->Sequence->GetPlayLength()); }
	}
	UMotionMatchingAnimNodeLibrary::SetDatabaseToSearch(Reference, Database,
		EPoseSearchInterruptMode::InterruptOnDatabaseChangeAndInvalidateContinuingPose);
}

bool UMHGZMotionMatchingAnimInstance::TryRouteStartToLoop(const FMotionMatchingAnimNodeReference& Reference,
	EMHGZMotionMatchingNode Node)
{
	if (!bMMHasLocomotionInput || bMMForceIdle || bMMStopRequestActive || bStartQueryActive
		|| (Node == EMHGZMotionMatchingNode::Unsheathed) != bMMUnsheathedLocomotion) { return false; }
	const auto* MM = Reference.GetAnimNodePtr<FAnimNode_MotionMatching>();
	const auto* Source = MM ? Cast<UAnimSequenceBase>(MM->GetAnimAsset()) : nullptr;
	if (!Source || !Source->GetName().Contains(TEXT("_Start"))) { return false; }
	// The existing Start Intent is the semantic gate. BlockTransition marks poses
	// that cannot be FRESH candidates; it is not a lock on leaving the source.
	if (UPoseSearchDatabase* Loop = GetMobileExitLoopDatabase(Node))
	{
		InstallLoopDatabase(Reference, Node, Loop, true);
		bMMMobileExitCruise[static_cast<int32>(Node)] = true;
		return true;
	}
	return false;
}

void UMHGZMotionMatchingAnimInstance::ObserveLoopContinuity(const FAnimNodeReference& Reference,
	EMHGZMotionMatchingNode Node)
{
	if (Node != EMHGZMotionMatchingNode::Sheathed) { return; }
	const auto* MM = Reference.GetAnimNodePtr<FAnimNode_MotionMatching>();
	if (!MM) { return; }
	const auto& Result = MM->GetMotionMatchingState().SearchResult;
	if (!Result.SelectedAnim) { return; } // A transiently invalid search cannot erase the independent clock.
	const auto* Sequence = Cast<UAnimSequenceBase>(Result.SelectedAnim);
	if (Sequence && (Sequence == MMSheathedRunLoopSequence || Sequence == MMSheathedSprintLoopSequence)
		&& bMMHasLocomotionInput && !bMMForceIdle && !bMMStopRequestActive)
	{
		if (!MMLoopClock.bValid) { MMLoopClock.Seed(Result.SelectedTime, Sequence->GetPlayLength()); }
		// A new loop advances during this same update, after the state callback.
		MMLoopClock.Schedule(MMNodeUpdateDelta[0], Sequence->GetPlayLength());
	}
	else { MMLoopClock.Reset(); }
}
