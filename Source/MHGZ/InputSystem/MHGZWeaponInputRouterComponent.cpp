// Copyright MHGZ Project. All Rights Reserved.

#include "InputSystem/MHGZWeaponInputRouterComponent.h"

#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "InputAction.h"
#include "MHGZCharacter.h"
#include "UI/MHGZAimComponent.h"
#include "WeaponRuntime/MHGZWeaponInputProfile.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

namespace
{
constexpr int32 MaxCapturedSnapshots = 128;
constexpr int32 MaxInputCaptureEvents = 256;

FGameplayTag LTModifierTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Input.Modifier.LT"));
	return Tag;
}

FGameplayTag RTModifierTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Input.Modifier.RT"));
	return Tag;
}

FGameplayTag SheathedTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Sheathed"));
	return Tag;
}

FGameplayTag UnsheathedTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Unsheathed"));
	return Tag;
}

FGameplayTag GroundedTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Grounded"));
	return Tag;
}

FGameplayTag AerialTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aerial"));
	return Tag;
}

FGameplayTag HitstunTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Hitstun"));
	return Tag;
}

FGameplayTag KnockdownTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Knockdown"));
	return Tag;
}

FGameplayTag KinsectAimTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aiming.Kinsect"));
	return Tag;
}

FGameplayTag ActionAimTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aiming.Action"));
	return Tag;
}

FGameplayTag SlingerAimTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aiming.Slinger"));
	return Tag;
}
}

UMHGZWeaponInputRouterComponent::UMHGZWeaponInputRouterComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.0f;
}

void UMHGZWeaponInputRouterComponent::BeginPlay()
{
	Super::BeginPlay();

	OwnerPC = Cast<APlayerController>(GetOwner());
	if (OwnerPC.IsValid())
	{
		AttachToPawn(OwnerPC->GetPawn());
	}
}

void UMHGZWeaponInputRouterComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ShutdownRouter();
	Super::EndPlay(EndPlayReason);
}

void UMHGZWeaponInputRouterComponent::TickComponent(
	float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	FlushExpiredInputs(FPlatformTime::Seconds());
}

void UMHGZWeaponInputRouterComponent::AttachToPawn(APawn* InPawn)
{
	bAcceptingInput = true;
	if (!OwnerPC.IsValid())
	{
		OwnerPC = Cast<APlayerController>(GetOwner());
	}
	AMHGZCharacter* Character = Cast<AMHGZCharacter>(InPawn);

	if (CachedCharacter.Get() == Character)
	{
		if (!Character)
		{
			return;
		}
		if (CachedASC.IsValid())
		{
			return;
		}
		// Same pawn, ASC not available yet (possession ordering): refresh the ASC.
		CachedASC = Cast<UMHGZAbilitySystemComponent>(Character->GetAbilitySystemComponent());
		if (CachedASC.IsValid())
		{
			UnsubscribePoseTags();
			SubscribePoseTags();
			RecomputeAimChildTags();
		}
		return;
	}

	// Pawn changed (or detached): no held/pending/release state may leak across pawns.
	UnsubscribePoseTags();
	ReleaseAllAimChildTokens();
	HeldControls.Reset();
	DeferredChords.Reset();
	for (bool& bResolved : ResolvedChords)
	{
		bResolved = false;
	}
	ReleaseRegistry.Reset();
	CapturedSnapshots.Reset();
	InputCaptureEvents.Reset();
	NextInputCaptureEventSerial = 1;

	CachedCharacter = Character;
	CachedASC = nullptr;

	if (Character)
	{
		CachedASC = Cast<UMHGZAbilitySystemComponent>(Character->GetAbilitySystemComponent());
		if (CachedASC.IsValid())
		{
			SubscribePoseTags();
		}
		RecomputeAimChildTags();
	}
}

void UMHGZWeaponInputRouterComponent::ShutdownRouter()
{
	bAcceptingInput = false;
	UnsubscribePoseTags();
	ReleaseAllAimChildTokens();

	HeldControls.Reset();
	DeferredChords.Reset();
	for (bool& bResolved : ResolvedChords)
	{
		bResolved = false;
	}
	ReleaseRegistry.Reset();
	CapturedSnapshots.Reset();
	InputCaptureEvents.Reset();
	CachedCharacter.Reset();
	CachedASC.Reset();
	OwnerPC.Reset();
	NextSequenceID = 1;
	NextInputCaptureEventSerial = 1;
}

void UMHGZWeaponInputRouterComponent::SetInputProfile(UWeaponInputProfile* InProfile)
{
	if (CurrentProfile == InProfile)
	{
		RecomputeAimChildTags();
		return;
	}

	CurrentProfile = InProfile;
	RebuildChordCache();

	// Existing formations belong to the previous profile; keep held keys but reset
	// their single-obligation flags and refresh the frozen snapshots with the new
	// profile's modifier set (identity: same SequenceID/StartedTime).
	for (TPair<FGameplayTag, FPhysicalInputState>& Pair : HeldControls)
	{
		FPhysicalInputState& State = Pair.Value;
		State.bSingleEmitted = false;
		State.bSingleConsumed = false;
		State.FrozenSingleSnapshot = BuildSnapshot(
			Pair.Key, Pair.Key, State.SequenceID,
			EWeaponInputPhase::Started, State.StartedTime, EWeaponAimSnapshotContext::None);
	}

	RecomputeAimChildTags();
	OnInputProfileChanged.Broadcast();
}

void UMHGZWeaponInputRouterComponent::RebuildChordCache()
{
	Chords.Reset();
	ConfiguredModifierTags.Reset();
	SingleChordOwnedTags.Reset();
	DelayedTags.Reset();
	DeferredChords.Reset();

	if (!CurrentProfile)
	{
		ResolvedChords.Reset();
		return;
	}

	const TArray<FWeaponChordDefinition>& Definitions = CurrentProfile->Chords;
	Chords.Reserve(Definitions.Num());
	for (const FWeaponChordDefinition& Definition : Definitions)
	{
		FChordInfo Info;
		Info.Definition = &Definition;
		Info.TriggerCount = Definition.TriggerControls.Num();
		Info.ModifierCount = Definition.RequiredHeldModifiers.Num();
		Info.bMultiMember = (Info.TriggerCount + Info.ModifierCount) > 1;

		for (const FGameplayTag& Trigger : Definition.TriggerControls)
		{
			Info.Members.Add(Trigger);
			Info.TriggerSet.Add(Trigger);
			if (Info.bMultiMember)
			{
				DelayedTags.Add(Trigger);
			}
			else if (Info.TriggerCount == 1)
			{
				SingleChordOwnedTags.Add(Trigger);
			}
		}
		for (const FGameplayTag& Modifier : Definition.RequiredHeldModifiers)
		{
			Info.Members.Add(Modifier);
			Info.ModifierSet.Add(Modifier);
			ConfiguredModifierTags.Add(Modifier);
			if (Info.bMultiMember)
			{
				DelayedTags.Add(Modifier);
			}
		}
		Chords.Add(MoveTemp(Info));
	}

	ResolvedChords.SetNum(Chords.Num());
	for (bool& bResolved : ResolvedChords)
	{
		bResolved = false;
	}
}

bool UMHGZWeaponInputRouterComponent::IsBetterChord(const FChordInfo& A, const FChordInfo& B)
{
	if (A.ModifierCount != B.ModifierCount)
	{
		return A.ModifierCount > B.ModifierCount;
	}
	if (A.TriggerCount != B.TriggerCount)
	{
		return A.TriggerCount > B.TriggerCount;
	}
	return A.Definition->Priority > B.Definition->Priority;
}

bool UMHGZWeaponInputRouterComponent::IsTriggerSubset(const FChordInfo& Sub, const FChordInfo& Super)
{
	for (const FGameplayTag& Trigger : Sub.TriggerSet)
	{
		if (!Super.TriggerSet.Contains(Trigger))
		{
			return false;
		}
	}
	return true;
}

double UMHGZWeaponInputRouterComponent::GetFirstTriggerTime(int32 ChordIndex) const
{
	const FChordInfo& Chord = Chords[ChordIndex];
	double First = DBL_MAX;
	for (const FGameplayTag& Trigger : Chord.Definition->TriggerControls)
	{
		if (const FPhysicalInputState* State = HeldControls.Find(Trigger))
		{
			First = FMath::Min(First, State->StartedTime);
		}
	}
	return First;
}

FGameplayTag UMHGZWeaponInputRouterComponent::GetLastMemberTag(int32 ChordIndex) const
{
	const FChordInfo& Chord = Chords[ChordIndex];
	FGameplayTag LastTag;
	double LastTime = -DBL_MAX;
	for (const FGameplayTag& Member : Chord.Members)
	{
		if (const FPhysicalInputState* State = HeldControls.Find(Member))
		{
			if (State->StartedTime >= LastTime)
			{
				LastTime = State->StartedTime;
				LastTag = Member;
			}
		}
	}
	return LastTag;
}

bool UMHGZWeaponInputRouterComponent::IsChordComplete(int32 ChordIndex) const
{
	if (!CurrentProfile || !Chords.IsValidIndex(ChordIndex))
	{
		return false;
	}

	const FChordInfo& Chord = Chords[ChordIndex];
	const double Grace = FMath::Max(0.0, static_cast<double>(CurrentProfile->ChordGracePeriod));

	double FirstTriggerTime = DBL_MAX;
	double LastTriggerTime = -DBL_MAX;
	double MaxMemberTime = -DBL_MAX;

	for (const FGameplayTag& Trigger : Chord.Definition->TriggerControls)
	{
		const FPhysicalInputState* State = HeldControls.Find(Trigger);
		if (!State)
		{
			return false;
		}
		FirstTriggerTime = FMath::Min(FirstTriggerTime, State->StartedTime);
		LastTriggerTime = FMath::Max(LastTriggerTime, State->StartedTime);
		MaxMemberTime = FMath::Max(MaxMemberTime, State->StartedTime);
	}

	for (const FGameplayTag& Modifier : Chord.Definition->RequiredHeldModifiers)
	{
		const FPhysicalInputState* State = HeldControls.Find(Modifier);
		if (!State)
		{
			return false;
		}
		MaxMemberTime = FMath::Max(MaxMemberTime, State->StartedTime);
	}

	// All TriggerControls must start within the grace period...
	if (LastTriggerTime - FirstTriggerTime > Grace)
	{
		return false;
	}
	// ...and a modifier filled last must arrive before the trigger deadline.
	if (MaxMemberTime - FirstTriggerTime > Grace)
	{
		return false;
	}

	// Exact modifiers: no extra configured modifier control may be held, except
	// ones this chord itself uses (as trigger or required modifier).
	if (Chord.Definition->bRequireExactModifiers)
	{
		for (const TPair<FGameplayTag, FPhysicalInputState>& Pair : HeldControls)
		{
			if (!ConfiguredModifierTags.Contains(Pair.Key))
			{
				continue;
			}
			if (!Chord.TriggerSet.Contains(Pair.Key) && !Chord.ModifierSet.Contains(Pair.Key))
			{
				return false;
			}
		}
	}
	if (!DoChordContextRequirementsPass(ChordIndex))
	{
		return false;
	}

	return true;
}

bool UMHGZWeaponInputRouterComponent::DoChordContextRequirementsPass(int32 ChordIndex) const
{
	if (!Chords.IsValidIndex(ChordIndex) || !Chords[ChordIndex].Definition)
	{
		return false;
	}
	const FWeaponChordDefinition& Definition = *Chords[ChordIndex].Definition;
	if (Definition.RequiredContextTags.IsEmpty() && Definition.BlockedContextTags.IsEmpty())
	{
		return true;
	}
	const UMHGZAbilitySystemComponent* ASC = CachedASC.Get();
	return ASC
		&& (Definition.RequiredContextTags.IsEmpty()
			|| ASC->HasAllMatchingGameplayTags(Definition.RequiredContextTags))
		&& (Definition.BlockedContextTags.IsEmpty()
			|| !ASC->HasAnyMatchingGameplayTags(Definition.BlockedContextTags));
}

bool UMHGZWeaponInputRouterComponent::IsPossiblyCompletable(int32 ChordIndex, double Now) const
{
	if (!CurrentProfile || !Chords.IsValidIndex(ChordIndex))
	{
		return false;
	}

	const FChordInfo& Chord = Chords[ChordIndex];
	const double Grace = FMath::Max(0.0, static_cast<double>(CurrentProfile->ChordGracePeriod));

	double FirstTriggerTime = DBL_MAX;
	double LastTriggerTime = -DBL_MAX;
	bool bAnyTriggerStarted = false;

	for (const FGameplayTag& Trigger : Chord.Definition->TriggerControls)
	{
		if (const FPhysicalInputState* State = HeldControls.Find(Trigger))
		{
			bAnyTriggerStarted = true;
			FirstTriggerTime = FMath::Min(FirstTriggerTime, State->StartedTime);
			LastTriggerTime = FMath::Max(LastTriggerTime, State->StartedTime);
		}
	}
	if (!bAnyTriggerStarted)
	{
		return false;
	}
	if (LastTriggerTime - FirstTriggerTime > Grace)
	{
		return false;
	}
	if (Now > FirstTriggerTime + Grace)
	{
		return false; // its own trigger window has already closed
	}
	if (!DoChordContextRequirementsPass(ChordIndex))
	{
		return false;
	}

	// If exact modifiers are already violated by held keys, the chord can never complete.
	if (Chord.Definition->bRequireExactModifiers)
	{
		for (const TPair<FGameplayTag, FPhysicalInputState>& Pair : HeldControls)
		{
			if (!ConfiguredModifierTags.Contains(Pair.Key))
			{
				continue;
			}
			if (!Chord.TriggerSet.Contains(Pair.Key) && !Chord.ModifierSet.Contains(Pair.Key))
			{
				return false;
			}
		}
	}

	return true;
}

void UMHGZWeaponInputRouterComponent::EvaluateChords(double Now)
{
	if (!CurrentProfile || Chords.Num() == 0)
	{
		return;
	}

	// Best complete, unresolved chord.
	int32 BestIndex = INDEX_NONE;
	for (int32 Index = 0; Index < Chords.Num(); ++Index)
	{
		if (ResolvedChords[Index])
		{
			continue;
		}
		if (!IsChordComplete(Index))
		{
			continue;
		}
		if (BestIndex == INDEX_NONE || IsBetterChord(Chords[Index], Chords[BestIndex]))
		{
			BestIndex = Index;
		}
	}
	if (BestIndex == INDEX_NONE)
	{
		return;
	}

	// If a strictly better chord could still complete (its trigger set is a
	// superset of the best chord's and its window is open), defer the best chord
	// so arrival orders such as RT->Y->B and Y->B->RT converge on the same output.
	bool bDefer = false;
	for (int32 Index = 0; Index < Chords.Num(); ++Index)
	{
		if (Index == BestIndex || ResolvedChords[Index])
		{
			continue;
		}
		const FChordInfo& Other = Chords[Index];
		if (!IsBetterChord(Other, Chords[BestIndex]))
		{
			continue;
		}
		if (!IsTriggerSubset(Chords[BestIndex], Other))
		{
			continue;
		}
		if (!IsPossiblyCompletable(Index, Now))
		{
			continue;
		}
		bDefer = true;
		break;
	}

	if (bDefer)
	{
		if (!DeferredChords.Contains(BestIndex))
		{
			FDeferredChord Deferred;
			Deferred.ChordIndex = BestIndex;
			Deferred.CompletionTime = Now;
			Deferred.FirstTriggerTime = GetFirstTriggerTime(BestIndex);
			Deferred.FrozenSnapshot = BuildChordSnapshot(BestIndex, Now);
			DeferredChords.Add(BestIndex, Deferred);
		}
		return;
	}

	EmitChord(BestIndex, Now);

	// Other complete chords lost the sort at this exact evaluation moment: they
	// are discarded for this formation (a member release re-enables them).
	for (int32 Index = 0; Index < Chords.Num(); ++Index)
	{
		if (Index != BestIndex && !ResolvedChords[Index] && IsChordComplete(Index))
		{
			ResolvedChords[Index] = true;
			DeferredChords.Remove(Index);
		}
	}
}

void UMHGZWeaponInputRouterComponent::EmitChord(int32 ChordIndex, double Now)
{
	if (!Chords.IsValidIndex(ChordIndex))
	{
		return;
	}

	const FChordInfo& Chord = Chords[ChordIndex];
	FWeaponInputSnapshot Snapshot;
	if (const FDeferredChord* Deferred = DeferredChords.Find(ChordIndex))
	{
		Snapshot = Deferred->FrozenSnapshot; // frozen at the completion moment, not now
		DeferredChords.Remove(ChordIndex);
	}
	else
	{
		Snapshot = BuildChordSnapshot(ChordIndex, Now);
	}
	ResolvedChords[ChordIndex] = true;

	// Consume TriggerControls only when configured; modifiers used by a chord are
	// always suppressed as single outputs.
	if (Chord.Definition->bConsumeTriggerControls)
	{
		for (const FGameplayTag& Trigger : Chord.Definition->TriggerControls)
		{
			if (FPhysicalInputState* State = HeldControls.Find(Trigger))
			{
				State->bSingleConsumed = true;
			}
		}
	}
	for (const FGameplayTag& Modifier : Chord.Definition->RequiredHeldModifiers)
	{
		if (FPhysicalInputState* State = HeldControls.Find(Modifier))
		{
			State->bSingleConsumed = true;
		}
	}

	// Every chord whose trigger set is a subset of the winner belongs to this
	// consumed formation, even if exact-modifier rules made it incomplete while
	// the winning modifier was held. Lock it until one of its own members is
	// released so dropping RT cannot retroactively emit Y+B after RT+Y+B won.
	for (int32 Index = 0; Index < Chords.Num(); ++Index)
	{
		if (Index != ChordIndex && IsTriggerSubset(Chords[Index], Chord))
		{
			ResolvedChords[Index] = true;
			DeferredChords.Remove(Index);
		}
	}

	if (Chord.Definition->ReleaseControlTag.IsValid())
	{
		RegisterRelease(Snapshot, Chord.Definition->ReleaseControlTag);
	}

	EmitSnapshot(Snapshot);
}

void UMHGZWeaponInputRouterComponent::FlushExpiredInputs(double Now)
{
	if (!bAcceptingInput || !CurrentProfile)
	{
		return;
	}
	const double Grace = FMath::Max(0.0, static_cast<double>(CurrentProfile->ChordGracePeriod));

	// 1. Deferred chords whose window closed: emit the best one; the others that
	// closed at this same moment lost the sort and are discarded.
	TSet<int32> Closed;
	for (const TPair<int32, FDeferredChord>& Pair : DeferredChords)
	{
		if (ResolvedChords[Pair.Key])
		{
			continue; // already emitted (or discarded) for this formation
		}
		const FDeferredChord& Deferred = Pair.Value;
		if (Now < Deferred.FirstTriggerTime + Grace)
		{
			continue;
		}
		if (!IsChordComplete(Pair.Key))
		{
			continue;
		}
		Closed.Add(Pair.Key);
	}
	if (Closed.Num() > 0)
	{
		int32 BestClosed = INDEX_NONE;
		for (const int32 Index : Closed)
		{
			if (BestClosed == INDEX_NONE || IsBetterChord(Chords[Index], Chords[BestClosed]))
			{
				BestClosed = Index;
			}
		}
		EmitChord(BestClosed, Now);
		for (const int32 Index : Closed)
		{
			if (Index != BestClosed)
			{
				DeferredChords.Remove(Index);
			}
		}
	}

	// Defensive: drop deferred chords that are no longer complete (a member
	// released; HandlePhysicalCompleted normally removes these already).
	TSet<int32> Invalid;
	for (const TPair<int32, FDeferredChord>& Pair : DeferredChords)
	{
		if (!IsChordComplete(Pair.Key))
		{
			Invalid.Add(Pair.Key);
		}
	}
	for (const int32 Index : Invalid)
	{
		DeferredChords.Remove(Index);
	}

	// 2. Pending fallback singles whose grace window expired. The snapshot was
	// frozen at the physical Started; timeout never re-samples direction/context.
	for (TPair<FGameplayTag, FPhysicalInputState>& Pair : HeldControls)
	{
		FPhysicalInputState& State = Pair.Value;
		if (State.bSingleEmitted || State.bSingleConsumed)
		{
			continue;
		}
		if (ConfiguredModifierTags.Contains(Pair.Key))
		{
			continue; // held modifiers are context, never fallback discrete actions
		}
		if (!DelayedTags.Contains(Pair.Key))
		{
			continue;
		}
		if (SingleChordOwnedTags.Contains(Pair.Key))
		{
			continue;
		}
		if (Now < State.StartedTime + Grace)
		{
			continue;
		}
		State.bSingleEmitted = true;
		RegisterRelease(State.FrozenSingleSnapshot, Pair.Key);
		EmitSnapshot(State.FrozenSingleSnapshot);
	}
}

void UMHGZWeaponInputRouterComponent::HandlePhysicalStarted(FGameplayTag PhysicalTag, double Now)
{
	if (!bAcceptingInput || !PhysicalTag.IsValid())
	{
		return;
	}
	if (HeldControls.Contains(PhysicalTag))
	{
		return; // duplicate Started for an already-held control
	}

	FPhysicalInputState State;
	State.StartedTime = Now;
	State.LastTriggeredTime = Now;
	State.SequenceID = AllocateSequenceID();
	State.FrozenSingleSnapshot = BuildSnapshot(
		PhysicalTag, PhysicalTag, State.SequenceID,
		EWeaponInputPhase::Started, Now, EWeaponAimSnapshotContext::None);
	HeldControls.Add(PhysicalTag, MoveTemp(State));

	// 修饰键可能是最后补齐的成员；必须先建立 Aim 子标签，再冻结 Chord 的
	// ContextTags/AimSnapshot 并让 Coordinator 检查 RequiredTags。
	RecomputeAimChildTags();
	EvaluateChords(Now);

	// Keys that cannot participate in any multi-member chord dispatch immediately;
	// everything else waits for the grace window (FlushExpiredInputs) unless a
	// chord consumed it.
	FPhysicalInputState* Held = HeldControls.Find(PhysicalTag);
	if (Held && !Held->bSingleConsumed && !DelayedTags.Contains(PhysicalTag)
		&& !ConfiguredModifierTags.Contains(PhysicalTag)
		&& !SingleChordOwnedTags.Contains(PhysicalTag))
	{
		Held->bSingleEmitted = true;
		RegisterRelease(Held->FrozenSingleSnapshot, PhysicalTag);
		EmitSnapshot(Held->FrozenSingleSnapshot);
	}

}

void UMHGZWeaponInputRouterComponent::HandlePhysicalCompleted(FGameplayTag PhysicalTag, double Now)
{
	if (!bAcceptingInput || !PhysicalTag.IsValid())
	{
		return;
	}
	if (!HeldControls.Contains(PhysicalTag))
	{
		return; // idempotent / late release
	}

	// Releasing a member closes this formation immediately. Resolve the best
	// chord that is already complete before removing the member; otherwise a
	// quick Y+B tap released inside the grace period would disappear entirely.
	int32 BestComplete = INDEX_NONE;
	for (int32 Index = 0; Index < Chords.Num(); ++Index)
	{
		if (ResolvedChords[Index] || !Chords[Index].Members.Contains(PhysicalTag)
			|| !IsChordComplete(Index))
		{
			continue;
		}
		if (BestComplete == INDEX_NONE || IsBetterChord(Chords[Index], Chords[BestComplete]))
		{
			BestComplete = Index;
		}
	}
	if (BestComplete != INDEX_NONE)
	{
		EmitChord(BestComplete, Now);
		for (int32 Index = 0; Index < Chords.Num(); ++Index)
		{
			if (Index != BestComplete && !ResolvedChords[Index]
				&& IsChordComplete(Index))
			{
				ResolvedChords[Index] = true;
				DeferredChords.Remove(Index);
			}
		}
	}

	// If no consuming chord won, release also closes the single-key grace
	// period. Emit the frozen Started fact before its matching Completed fact.
	if (FPhysicalInputState* State = HeldControls.Find(PhysicalTag))
	{
		if (!State->bSingleEmitted && !State->bSingleConsumed
			&& !ConfiguredModifierTags.Contains(PhysicalTag)
			&& !SingleChordOwnedTags.Contains(PhysicalTag))
		{
			State->bSingleEmitted = true;
			RegisterRelease(State->FrozenSingleSnapshot, PhysicalTag);
			EmitSnapshot(State->FrozenSingleSnapshot);
		}
	}

	// Emit Completed only for identities registered under this control tag. The
	// snapshot is immutable except Phase (Completed) and Timestamp (now).
	if (TArray<FReleaseRegistration>* Registrations = ReleaseRegistry.Find(PhysicalTag))
	{
		TArray<FReleaseRegistration> Copy = MoveTemp(*Registrations);
		ReleaseRegistry.Remove(PhysicalTag);
		for (const FReleaseRegistration& Registration : Copy)
		{
			FWeaponInputSnapshot ReleaseSnapshot = Registration.Snapshot;
			ReleaseSnapshot.Phase = EWeaponInputPhase::Completed;
			ReleaseSnapshot.Timestamp = Now;
			EmitSnapshot(ReleaseSnapshot);
		}
	}

	HeldControls.Remove(PhysicalTag);

	// This control can no longer participate in any formation; resolved chords
	// using it may form again after a fresh press.
	for (int32 Index = 0; Index < Chords.Num(); ++Index)
	{
		if (Chords[Index].Members.Contains(PhysicalTag))
		{
			ResolvedChords[Index] = false;
			DeferredChords.Remove(Index);
		}
	}

	RecomputeAimChildTags();
}

void UMHGZWeaponInputRouterComponent::HandleRawInputStarted(const FInputActionInstance& Instance)
{
	if (!CurrentProfile)
	{
		return;
	}
	const UInputAction* SourceAction = Instance.GetSourceAction();
	if (!SourceAction)
	{
		return;
	}
	const FGameplayTag* Tag = CurrentProfile->RawActionToPhysicalInputTag.Find(SourceAction);
	if (!Tag)
	{
		return;
	}
	HandlePhysicalStarted(*Tag, FPlatformTime::Seconds());
}

void UMHGZWeaponInputRouterComponent::HandleRawInputTriggered(const FInputActionInstance& Instance)
{
	if (!CurrentProfile)
	{
		return;
	}
	const UInputAction* SourceAction = Instance.GetSourceAction();
	if (!SourceAction)
	{
		return;
	}
	const FGameplayTag* Tag = CurrentProfile->RawActionToPhysicalInputTag.Find(SourceAction);
	if (!Tag)
	{
		return;
	}
	if (FPhysicalInputState* State = HeldControls.Find(*Tag))
	{
		State->LastTriggeredTime = FPlatformTime::Seconds();
	}
	if (*Tag == LTModifierTag() || *Tag == RTModifierTag())
	{
		RecomputeAimChildTags();
	}
}

void UMHGZWeaponInputRouterComponent::HandleRawInputCompleted(const FInputActionInstance& Instance)
{
	if (!CurrentProfile)
	{
		return;
	}
	const UInputAction* SourceAction = Instance.GetSourceAction();
	if (!SourceAction)
	{
		return;
	}
	const FGameplayTag* Tag = CurrentProfile->RawActionToPhysicalInputTag.Find(SourceAction);
	if (!Tag)
	{
		return;
	}
	HandlePhysicalCompleted(*Tag, FPlatformTime::Seconds());
}

uint32 UMHGZWeaponInputRouterComponent::AllocateSequenceID()
{
	if (NextSequenceID == 0)
	{
		NextSequenceID = 1; // skip the reserved 0 identity
	}
	return NextSequenceID++;
}

FWeaponInputSnapshot UMHGZWeaponInputRouterComponent::BuildChordSnapshot(int32 ChordIndex, double Now)
{
	const FChordInfo& Chord = Chords[ChordIndex];
	const FGameplayTag LastMemberTag = GetLastMemberTag(ChordIndex);
	const uint32 SequenceID = AllocateSequenceID();
	return BuildSnapshot(
		Chord.Definition->OutputTag, LastMemberTag, SequenceID,
		EWeaponInputPhase::Started, Now, Chord.Definition->AimSnapshotContext);
}

FWeaponInputSnapshot UMHGZWeaponInputRouterComponent::BuildSnapshot(
	const FGameplayTag& ResolvedTag,
	const FGameplayTag& SourceTag,
	uint32 SequenceID,
	EWeaponInputPhase Phase,
	double Timestamp,
	EWeaponAimSnapshotContext AimContext) const
{
	FWeaponInputSnapshot Snapshot;
	Snapshot.ResolvedInputTag = ResolvedTag;
	Snapshot.SourceControlTag = SourceTag;
	Snapshot.SequenceID = SequenceID;
	Snapshot.Phase = Phase;
	Snapshot.Timestamp = Timestamp;

	// Held modifiers: any currently held control that the profile declares as a
	// required modifier anywhere.
	for (const FGameplayTag& Modifier : ConfiguredModifierTags)
	{
		if (HeldControls.Contains(Modifier))
		{
			Snapshot.HeldModifierTags.AddTag(Modifier);
		}
	}

	// Context tags: pose + aiming children, frozen from the ASC at this moment.
	if (UMHGZAbilitySystemComponent* ASC = CachedASC.Get())
	{
		if (ASC->HasMatchingGameplayTag(GroundedTag()))
		{
			Snapshot.ContextTags.AddTag(GroundedTag());
		}
		else if (ASC->HasMatchingGameplayTag(AerialTag()))
		{
			Snapshot.ContextTags.AddTag(AerialTag());
		}
		if (ASC->HasMatchingGameplayTag(SheathedTag()))
		{
			Snapshot.ContextTags.AddTag(SheathedTag());
		}
		else if (ASC->HasMatchingGameplayTag(UnsheathedTag()))
		{
			Snapshot.ContextTags.AddTag(UnsheathedTag());
		}
		if (ASC->HasMatchingGameplayTag(KinsectAimTag()))
		{
			Snapshot.ContextTags.AddTag(KinsectAimTag());
		}
		if (ASC->HasMatchingGameplayTag(ActionAimTag()))
		{
			Snapshot.ContextTags.AddTag(ActionAimTag());
		}
		if (ASC->HasMatchingGameplayTag(SlingerAimTag()))
		{
			Snapshot.ContextTags.AddTag(SlingerAimTag());
		}
	}

	// Movement: world direction comes from the character's cached world input;
	// classification is relative to the character's forward/right.
	FVector CharacterForward = FVector::ForwardVector;
	FVector CharacterRight = FVector::RightVector;
	if (AMHGZCharacter* Character = CachedCharacter.Get())
	{
		Snapshot.RawMoveInput = Character->GetRawMoveInput();
		Snapshot.WorldDirection = Character->GetLastMovementInputDir();
		CharacterForward = Character->GetActorForwardVector();
		CharacterRight = Character->GetActorRightVector();
		Snapshot.ActorForward = CharacterForward;
	}

	const float Threshold = CurrentProfile ? CurrentProfile->DirectionInputThreshold : 0.0f;
	const float ConeHalfAngle = CurrentProfile ? CurrentProfile->ForwardConeHalfAngle : 45.0f;
	Snapshot.Direction = ClassifyDirection(
		Snapshot.WorldDirection, CharacterForward, CharacterRight,
		Snapshot.RawMoveInput.Size(), Threshold, ConeHalfAngle);

	// Aim: captured once at chord resolution; never re-read by consumers.
	if (AimContext != EWeaponAimSnapshotContext::None)
	{
		if (AMHGZCharacter* Character = CachedCharacter.Get())
		{
			if (UMHGZAimComponent* AimComponent = Character->GetAimComponent())
			{
				Snapshot.Aim = AimComponent->CaptureAimSnapshot(AimContext);
			}
		}
	}

	return Snapshot;
}

EDirectionalInput UMHGZWeaponInputRouterComponent::ClassifyDirection(
	const FVector& WorldDirection,
	const FVector& CharacterForward,
	const FVector& CharacterRight,
	float InputMagnitude,
	float Threshold,
	float ConeHalfAngleDegrees)
{
	if (InputMagnitude < Threshold || WorldDirection.IsNearlyZero())
	{
		return EDirectionalInput::None;
	}

	const FVector World2D = FVector(WorldDirection.X, WorldDirection.Y, 0.0f);
	const FVector Forward2D = FVector(CharacterForward.X, CharacterForward.Y, 0.0f);
	const FVector Right2D = FVector(CharacterRight.X, CharacterRight.Y, 0.0f);
	if (World2D.IsNearlyZero() || Forward2D.IsNearlyZero() || Right2D.IsNearlyZero())
	{
		return EDirectionalInput::None;
	}

	const FVector NormalizedWorld = World2D.GetSafeNormal();
	const FVector NormalizedForward = Forward2D.GetSafeNormal();
	const FVector NormalizedRight = Right2D.GetSafeNormal();
	const float HalfAngleCos = FMath::Cos(FMath::DegreesToRadians(
		FMath::Clamp(ConeHalfAngleDegrees, 0.0f, 180.0f)));

	const float ForwardDot = FVector::DotProduct(NormalizedWorld, NormalizedForward);
	if (ForwardDot >= HalfAngleCos)
	{
		return EDirectionalInput::Forward;
	}
	if (ForwardDot <= -HalfAngleCos)
	{
		return EDirectionalInput::Back;
	}
	return FVector::DotProduct(NormalizedWorld, NormalizedRight) >= 0.0f
		? EDirectionalInput::Right
		: EDirectionalInput::Left;
}

void UMHGZWeaponInputRouterComponent::RegisterRelease(
	const FWeaponInputSnapshot& Snapshot, const FGameplayTag& ControlTag)
{
	if (!ControlTag.IsValid())
	{
		return;
	}
	FReleaseRegistration Registration;
	Registration.ResolvedInputTag = Snapshot.ResolvedInputTag;
	Registration.SourceControlTag = Snapshot.SourceControlTag;
	Registration.SequenceID = Snapshot.SequenceID;
	Registration.Snapshot = Snapshot;
	ReleaseRegistry.FindOrAdd(ControlTag).Add(MoveTemp(Registration));
}

void UMHGZWeaponInputRouterComponent::EmitSnapshot(const FWeaponInputSnapshot& Snapshot)
{
	OnInputSnapshotResolved.Broadcast(Snapshot);

	CapturedSnapshots.Add(Snapshot);
	if (CapturedSnapshots.Num() > MaxCapturedSnapshots)
	{
		CapturedSnapshots.RemoveAt(0, CapturedSnapshots.Num() - MaxCapturedSnapshots);
	}

	FWeaponInputCaptureEvent& Event = InputCaptureEvents.AddDefaulted_GetRef();
	Event.Serial = NextInputCaptureEventSerial++;
	Event.Snapshot = Snapshot;
	if (InputCaptureEvents.Num() > MaxInputCaptureEvents)
	{
		InputCaptureEvents.RemoveAt(0, InputCaptureEvents.Num() - MaxInputCaptureEvents);
	}

	if (UMHGZAbilitySystemComponent* ASC = CachedASC.Get())
	{
		if (Snapshot.Phase == EWeaponInputPhase::Completed)
		{
			ASC->HandleResolvedInputRelease(Snapshot);
		}
		else
		{
			ASC->HandleResolvedInputSnapshot(Snapshot);
		}
	}
}

FString UMHGZWeaponInputRouterComponent::GetHeldPhysicalInputTagsDebugString() const
{
	TArray<FString> Tags;
	Tags.Reserve(HeldControls.Num());
	for (const TPair<FGameplayTag, FPhysicalInputState>& Pair : HeldControls)
	{
		Tags.Add(Pair.Key.ToString());
	}
	Tags.Sort();
	return FString::Join(Tags, TEXT("|"));
}

void UMHGZWeaponInputRouterComponent::RecomputeAimChildTags()
{
	TArray<FGameplayTag> Desired;
	UMHGZWeaponRuntimeHostComponent* Host =
		CachedCharacter.IsValid() ? CachedCharacter->GetWeaponRuntimeHost() : nullptr;

	if (Host && CachedASC.IsValid())
	{
		const bool bLTHeld = HeldControls.Contains(LTModifierTag());
		const bool bRTHeld = HeldControls.Contains(RTModifierTag());
		const bool bPaused = CachedASC->HasMatchingGameplayTag(HitstunTag())
			|| CachedASC->HasMatchingGameplayTag(KnockdownTag());

		if (!bPaused)
		{
			if (bLTHeld)
			{
				if (CachedASC->HasMatchingGameplayTag(SheathedTag()))
				{
					Desired.Add(SlingerAimTag());
				}
				else if (CachedASC->HasMatchingGameplayTag(UnsheathedTag()))
				{
					Desired.Add(KinsectAimTag());
				}
			}
			if (bRTHeld)
			{
				Desired.Add(ActionAimTag());
			}
		}
	}

	// Release tokens no longer desired.
	for (int32 Index = AimChildTokens.Num() - 1; Index >= 0; --Index)
	{
		const bool bCurrentToken = Host
			&& AimChildTokens[Index].RuntimeToken == Host->GetCurrentToken();
		if (Desired.Contains(AimChildTags[Index]) && bCurrentToken)
		{
			continue;
		}
		if (UMHGZWeaponRuntimeHostComponent* TokenHost = AimChildTokens[Index].RuntimeToken.Host.Get())
		{
			TokenHost->ReleaseTags(AimChildTokens[Index]);
		}
		AimChildTokens.RemoveAt(Index);
		AimChildTags.RemoveAt(Index);
	}

	// Acquire missing tokens through the current RuntimeHost (owned by the router).
	for (const FGameplayTag& Tag : Desired)
	{
		if (AimChildTags.Contains(Tag))
		{
			continue;
		}
		FGameplayTagContainer Tags;
		Tags.AddTag(Tag);
		const FName LocalID = FName(*FString::Printf(TEXT("WeaponInputRouter.%s"), *Tag.ToString()));
		const FWeaponOwnedTagToken Token = Host->AcquireTags(
			EWeaponTagOwnerKind::Input, FGameplayAbilitySpecHandle(), 0, LocalID, Tags);
		if (Token.IsValid())
		{
			AimChildTags.Add(Tag);
			AimChildTokens.Add(Token);
		}
	}
}

void UMHGZWeaponInputRouterComponent::ReleaseAllAimChildTokens()
{
	for (FWeaponOwnedTagToken& Token : AimChildTokens)
	{
		if (UMHGZWeaponRuntimeHostComponent* Host = Token.RuntimeToken.Host.Get())
		{
			Host->ReleaseTags(Token);
		}
	}
	AimChildTokens.Reset();
	AimChildTags.Reset();
}

void UMHGZWeaponInputRouterComponent::SubscribePoseTags()
{
	if (!CachedASC.IsValid())
	{
		return;
	}

	static const TArray<FGameplayTag> PoseTags = []()
	{
		TArray<FGameplayTag> Tags;
		Tags.Add(SheathedTag());
		Tags.Add(UnsheathedTag());
		Tags.Add(HitstunTag());
		Tags.Add(KnockdownTag());
		return Tags;
	}();

	for (const FGameplayTag& Tag : PoseTags)
	{
		if (!Tag.IsValid())
		{
			continue;
		}
		const FDelegateHandle Handle = CachedASC->RegisterGameplayTagEvent(
			Tag, EGameplayTagEventType::NewOrRemoved)
			.AddUObject(this, &UMHGZWeaponInputRouterComponent::OnPoseTagChanged);
		PoseTagEventHandles.Add(Tag, Handle);
	}
}

void UMHGZWeaponInputRouterComponent::UnsubscribePoseTags()
{
	if (!CachedASC.IsValid())
	{
		PoseTagEventHandles.Reset();
		return;
	}
	for (const TPair<FGameplayTag, FDelegateHandle>& Pair : PoseTagEventHandles)
	{
		CachedASC->UnregisterGameplayTagEvent(Pair.Value, Pair.Key, EGameplayTagEventType::NewOrRemoved);
	}
	PoseTagEventHandles.Reset();
}

void UMHGZWeaponInputRouterComponent::OnPoseTagChanged(const FGameplayTag /*Tag*/, int32 /*NewCount*/)
{
	RecomputeAimChildTags();
}
