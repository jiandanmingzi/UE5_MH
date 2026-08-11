// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZComboCoordinatorAbility.h"

#include "MHGZAbilitySystemComponent.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "MHGZCharacter.h"
#include "TimerManager.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

namespace
{
	const FName IdleState(TEXT("Idle"));

	bool IsPostureTag(const FGameplayTag& Tag)
	{
		static const FGameplayTag Grounded = FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Grounded"));
		static const FGameplayTag Aerial = FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aerial"));
		static const FGameplayTag Sheathed = FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Sheathed"));
		static const FGameplayTag Unsheathed = FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Unsheathed"));
		return Tag.MatchesTagExact(Grounded) || Tag.MatchesTagExact(Aerial)
			|| Tag.MatchesTagExact(Sheathed) || Tag.MatchesTagExact(Unsheathed);
	}
}

UGA_WeaponComboCoordinator::UGA_WeaponComboCoordinator()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalOnly;
	StaminaCostPolicy = EAbilityStaminaCostPolicy::None;
	CurrentState = IdleState;
}

void UGA_WeaponComboCoordinator::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	UGameplayAbility::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (UMHGZAbilitySystemComponent* ASC = Cast<UMHGZAbilitySystemComponent>(
		ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr))
	{
		ASC->SetActiveComboCoordinator(this);
	}
}

void UGA_WeaponComboCoordinator::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	ResetCombo(EWeaponActionEndReason::RuntimeShutdown);
	if (UMHGZAbilitySystemComponent* ASC = Cast<UMHGZAbilitySystemComponent>(
		ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr))
	{
		if (ASC->GetActiveComboCoordinator() == this)
		{
			ASC->SetActiveComboCoordinator(nullptr);
		}
	}
	UGameplayAbility::EndAbility(Handle, ActorInfo, ActivationInfo,
		bReplicateEndAbility, bWasCancelled);
}

void UGA_WeaponComboCoordinator::InjectComboData(UMHGZWeaponComboData* Data)
{
	ResetCombo(EWeaponActionEndReason::WeaponChanged);
	ComboData = Data;
	BuildIndices();
}

void UGA_WeaponComboCoordinator::BuildIndices()
{
	StateIndex.Reset();
	AnyStateIndices.Reset();
	TransitionIndex.Reset();
	if (!ComboData) return;
	for (int32 Index = 0; Index < ComboData->Transitions.Num(); ++Index)
	{
		const FComboTransition& Transition = ComboData->Transitions[Index];
		TransitionIndex.Add(Transition.TransitionID, Index);
		if (Transition.bMatchAnyState)
		{
			AnyStateIndices.Add(Index);
		}
		else
		{
			StateIndex.FindOrAdd(Transition.SourceState).Add(Index);
		}
	}
}

const FComboTransition* UGA_WeaponComboCoordinator::FindTransition(FName TransitionID) const
{
	if (!ComboData) return nullptr;
	const int32* Index = TransitionIndex.Find(TransitionID);
	return Index && ComboData->Transitions.IsValidIndex(*Index)
		? &ComboData->Transitions[*Index] : nullptr;
}

bool UGA_WeaponComboCoordinator::DoesTransitionMatchState(
	const FComboTransition& Transition) const
{
	return Transition.bMatchAnyState
		? !Transition.BlockedSourceStates.Contains(CurrentState)
		: Transition.SourceState == CurrentState;
}

bool UGA_WeaponComboCoordinator::IsSnapshotPostureCompatible(
	const FComboTransition& Transition, const FWeaponInputSnapshot& Input) const
{
	for (const FGameplayTag& Required : Transition.RequiredTags)
	{
		if (IsPostureTag(Required) && !Input.ContextTags.HasTagExact(Required))
		{
			return false;
		}
	}
	for (const FGameplayTag& Blocked : Transition.BlockedTags)
	{
		if (IsPostureTag(Blocked) && Input.ContextTags.HasTagExact(Blocked))
		{
			return false;
		}
	}
	return true;
}

bool UGA_WeaponComboCoordinator::HasOpenWindowFor(
	const FWeaponActionToken& ActionToken) const
{
	for (const TPair<FName, FComboWindowEntry>& Pair : ComboWindows)
	{
		if (Pair.Value.ActionToken == ActionToken)
		{
			return true;
		}
	}
	return false;
}

bool UGA_WeaponComboCoordinator::TransitionRequirementsPass(
	const FComboTransition& Transition, const FWeaponInputSnapshot& Input) const
{
	const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC || !DoesTransitionMatchState(Transition)
		|| !IsSnapshotPostureCompatible(Transition, Input))
	{
		return false;
	}
	if (!Transition.RequiredTags.IsEmpty()
		&& !ASC->HasAllMatchingGameplayTags(Transition.RequiredTags))
	{
		return false;
	}
	if (!Transition.BlockedTags.IsEmpty()
		&& ASC->HasAnyMatchingGameplayTags(Transition.BlockedTags))
	{
		return false;
	}
	if (Transition.StaminaRequired > 0.f)
	{
		const UMHGZAttributeSet* Attributes = ASC->GetSet<UMHGZAttributeSet>();
		if (!Attributes || Attributes->GetStamina() < Transition.StaminaRequired)
		{
			return false;
		}
	}
	if (Transition.bRequiresComboWindow
		&& (!ActiveTransition.IsSet()
			|| !HasOpenWindowFor(ActiveTransition->ActionToken)))
	{
		return false;
	}
	return Transition.Direction == EDirectionalInput::None
		|| Transition.Direction == Input.Direction;
}

const FComboTransition* UGA_WeaponComboCoordinator::FindBestMatch(
	const FWeaponInputSnapshot& Input) const
{
	if (!ComboData) return nullptr;
	TArray<int32> Candidates;
	if (const TArray<int32>* Exact = StateIndex.Find(CurrentState))
	{
		Candidates.Append(*Exact);
	}
	Candidates.Append(AnyStateIndices);

	const FComboTransition* Best = nullptr;
	for (int32 Index : Candidates)
	{
		if (!ComboData->Transitions.IsValidIndex(Index)) continue;
		const FComboTransition& Candidate = ComboData->Transitions[Index];
		if (Candidate.bAutoTransition || Candidate.InputTag != Input.ResolvedInputTag
			|| !TransitionRequirementsPass(Candidate, Input))
		{
			continue;
		}
		if (!Best)
		{
			Best = &Candidate;
			continue;
		}
		const int32 CandidateState = Candidate.bMatchAnyState ? 0 : 1;
		const int32 BestState = Best->bMatchAnyState ? 0 : 1;
		const int32 CandidateDirection = Candidate.Direction == EDirectionalInput::None ? 0 : 1;
		const int32 BestDirection = Best->Direction == EDirectionalInput::None ? 0 : 1;
		if (CandidateState > BestState
			|| (CandidateState == BestState && CandidateDirection > BestDirection)
			|| (CandidateState == BestState && CandidateDirection == BestDirection
				&& Candidate.Priority > Best->Priority))
		{
			Best = &Candidate;
		}
	}
	return Best;
}

void UGA_WeaponComboCoordinator::HandleWeaponInput(const FWeaponInputSnapshot& Input)
{
	if (Input.Phase != EWeaponInputPhase::Started || PendingTransition.IsSet()) return;
	if (const FComboTransition* Transition = FindBestMatch(Input))
	{
		ExecuteTransition(*Transition, Input);
	}
}

bool UGA_WeaponComboCoordinator::ExecuteTransition(
	const FComboTransition& Transition, const FWeaponInputSnapshot& Input,
	const FWeaponActionToken* SourceAction)
{
	if (Transition.ExecutionPolicy == EComboExecutionPolicy::StateOnly)
	{
		return SourceAction && ExecuteStateOnlyTransition(Transition, *SourceAction);
	}
	UMHGZAbilitySystemComponent* ASC = Cast<UMHGZAbilitySystemComponent>(
		GetAbilitySystemComponentFromActorInfo());
	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	if (!ASC || !Host || !Transition.AbilityClass || PendingTransition.IsSet()) return false;
	const UMHGZGameplayAbility* ActionCDO =
		Cast<UMHGZGameplayAbility>(Transition.AbilityClass->GetDefaultObject());
	if (!ActionCDO || ActionCDO->GetInstancingPolicy()
		!= EGameplayAbilityInstancingPolicy::InstancedPerExecution)
	{
		return false;
	}

	const FGameplayAbilitySpecHandle AbilityHandle =
		ASC->FindWeaponAbilityHandle(Transition.AbilityClass);
	if (!AbilityHandle.IsValid()) return false;

	FWeaponAbilityActivationContext Context;
	Context.RuntimeToken = Host->GetCurrentToken();
	Context.ActivationSequenceID = Host->AllocateActivationSequenceID();
	Context.TransitionID = Transition.TransitionID;
	Context.SourceState = CurrentState;
	Context.TargetState = Transition.TargetState;
	Context.Input = Input;

	FPendingComboTransition Pending;
	Pending.TransitionID = Transition.TransitionID;
	Pending.AbilityHandle = AbilityHandle;
	Pending.ActivationSequenceID = Context.ActivationSequenceID;
	Pending.ActivationContext = Context;
	if (ActiveTransition.IsSet())
	{
		Pending.PreviousActionToken = ActiveTransition->ActionToken;
	}
	PendingTransition = Pending;
	ASC->PrepareWeaponAbilityActivation(AbilityHandle, Context);

	const bool bStarted = ASC->TryActivateAbility(AbilityHandle);
	if (!bStarted && PendingTransition.IsSet()
		&& PendingTransition->ActivationSequenceID == Context.ActivationSequenceID)
	{
		PendingTransition.Reset();
		FWeaponAbilityActivationContext DiscardedContext;
		ASC->ConsumePendingActivationContext(AbilityHandle, DiscardedContext);
	}
	return bStarted;
}

bool UGA_WeaponComboCoordinator::ConfirmTransitionActivation(
	const FWeaponActionToken& ActionToken)
{
	if (!PendingTransition.IsSet() || !ActionToken.IsValid()) return false;
	const FPendingComboTransition Pending = *PendingTransition;
	if (Pending.AbilityHandle != ActionToken.AbilityHandle
		|| Pending.ActivationSequenceID != ActionToken.ActivationSequenceID
		|| Pending.ActivationContext.RuntimeToken != ActionToken.RuntimeToken)
	{
		return false;
	}
	const FComboTransition* Transition = FindTransition(Pending.TransitionID);
	if (!Transition) return false;

	PendingTransition.Reset();
	if (Transition->StatePolicy == EComboStatePolicy::Preserve)
	{
		ResetComboTimeout();
		return true;
	}

	const FWeaponActionToken PreviousAction = Pending.PreviousActionToken;
	const FWeaponOwnedTagToken PreviousTransitionTagToken = ActiveTransitionTagToken;
	ActiveTransitionTagToken = FWeaponOwnedTagToken();

	FActiveComboTransition NewActive;
	NewActive.TransitionID = Transition->TransitionID;
	NewActive.ActionToken = ActionToken;
	NewActive.SourceState = Pending.ActivationContext.SourceState;
	NewActive.TargetState = Transition->TargetState;
	NewActive.OwnedTags = Transition->GrantedTags;
	ActiveTransition = NewActive;
	if (Transition->StatePolicy == EComboStatePolicy::Replace)
	{
		CurrentState = Transition->TargetState;
	}
	if (Transition->GrantTiming == ETransitionGrantTiming::OnActivation)
	{
		GrantActiveTransitionTags(*Transition);
	}
	ResetComboTimeout();

	if (PreviousAction.IsValid() && PreviousAction != ActionToken)
	{
		if (UMHGZGameplayAbility* PreviousAbility =
			Cast<UMHGZGameplayAbility>(PreviousAction.AbilityInstance.Get()))
		{
			PreviousAbility->RequestEndAction(EWeaponActionEndReason::Superseded);
		}
		CloseWindowsFor(PreviousAction);
	}
	if (UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost())
	{
		Host->ReleaseTags(PreviousTransitionTagToken);
	}
	return true;
}

void UGA_WeaponComboCoordinator::RejectTransitionActivation(
	const FWeaponActionToken& ActionToken)
{
	if (PendingTransition.IsSet()
		&& PendingTransition->AbilityHandle == ActionToken.AbilityHandle
		&& PendingTransition->ActivationSequenceID == ActionToken.ActivationSequenceID
		&& PendingTransition->ActivationContext.RuntimeToken == ActionToken.RuntimeToken)
	{
		PendingTransition.Reset();
	}
}

void UGA_WeaponComboCoordinator::OnAttackHit(const FWeaponActionToken& ActionToken)
{
	if (!ActiveTransition.IsSet() || ActiveTransition->ActionToken != ActionToken
		|| ActiveTransition->bFirstHitReceived)
	{
		return;
	}
	ActiveTransition->bFirstHitReceived = true;
	if (const FComboTransition* Transition = FindTransition(ActiveTransition->TransitionID))
	{
		if (Transition->GrantTiming == ETransitionGrantTiming::OnFirstHit)
		{
			GrantActiveTransitionTags(*Transition);
		}
	}
}

void UGA_WeaponComboCoordinator::OnActionFinished(
	const FWeaponActionToken& ActionToken, EWeaponActionEndReason Reason)
{
	if (!ActiveTransition.IsSet() || ActiveTransition->ActionToken != ActionToken) return;
	CloseWindowsFor(ActionToken);
	ReleaseActiveTransitionTags();
	ActiveTransition.Reset();
	CurrentState = IdleState;
	if (AActor* Owner = GetOwningActorFromActorInfo())
	{
		Owner->GetWorldTimerManager().ClearTimer(ComboTimeoutTimer);
	}
	(void)Reason;
}

bool UGA_WeaponComboCoordinator::OnAutoTransition(
	FName TransitionID, const FWeaponActionToken& SourceAction)
{
	if (!ActiveTransition.IsSet() || ActiveTransition->ActionToken != SourceAction) return false;
	const FComboTransition* Transition = FindTransition(TransitionID);
	if (!Transition || !Transition->bAutoTransition
		|| !DoesTransitionMatchState(*Transition))
	{
		return false;
	}
	FWeaponInputSnapshot Input;
	if (const UMHGZGameplayAbility* SourceAbility =
		Cast<UMHGZGameplayAbility>(SourceAction.AbilityInstance.Get()))
	{
		Input = SourceAbility->GetWeaponActivationContext().Input;
	}
	Input.ResolvedInputTag = FGameplayTag();
	Input.Phase = EWeaponInputPhase::Triggered;
	if (!TransitionRequirementsPass(*Transition, Input))
	{
		return false;
	}
	return ExecuteTransition(*Transition, Input, &SourceAction);
}

bool UGA_WeaponComboCoordinator::ExecuteStateOnlyTransition(
	const FComboTransition& Transition, const FWeaponActionToken& SourceAction)
{
	if (!Transition.bAutoTransition || Transition.AbilityClass
		|| !ActiveTransition.IsSet() || ActiveTransition->ActionToken != SourceAction)
	{
		return false;
	}
	ReleaseActiveTransitionTags();
	ActiveTransition->TransitionID = Transition.TransitionID;
	ActiveTransition->SourceState = CurrentState;
	ActiveTransition->TargetState = Transition.TargetState;
	ActiveTransition->OwnedTags = Transition.GrantedTags;
	ActiveTransition->bFirstHitReceived = false;
	if (Transition.StatePolicy == EComboStatePolicy::Replace)
	{
		CurrentState = Transition.TargetState;
	}
	if (Transition.GrantTiming == ETransitionGrantTiming::OnActivation)
	{
		GrantActiveTransitionTags(Transition);
	}
	ResetComboTimeout();
	return true;
}

void UGA_WeaponComboCoordinator::OnLanded(const FHitResult& Hit)
{
	if (ActiveTransition.IsSet())
	{
		if (const FComboTransition* Transition = FindTransition(ActiveTransition->TransitionID))
		{
			if (Transition->LandingPolicy == EComboLandingPolicy::AbilityOwned)
			{
				if (UMHGZGameplayAbility* Ability = Cast<UMHGZGameplayAbility>(
					ActiveTransition->ActionToken.AbilityInstance.Get()))
				{
					Ability->HandleLanded(Hit);
					return;
				}
			}
		}
	}
	ResetCombo(EWeaponActionEndReason::Landed);
}

void UGA_WeaponComboCoordinator::ResetCombo(EWeaponActionEndReason Reason)
{
	PendingTransition.Reset();
	FWeaponActionToken PreviousAction;
	if (ActiveTransition.IsSet())
	{
		PreviousAction = ActiveTransition->ActionToken;
		CloseWindowsFor(PreviousAction);
		ReleaseActiveTransitionTags();
		ActiveTransition.Reset();
	}
	CurrentState = IdleState;
	if (AActor* Owner = GetOwningActorFromActorInfo())
	{
		Owner->GetWorldTimerManager().ClearTimer(ComboTimeoutTimer);
	}
	if (PreviousAction.IsValid())
	{
		if (UMHGZGameplayAbility* Ability =
			Cast<UMHGZGameplayAbility>(PreviousAction.AbilityInstance.Get()))
		{
			Ability->RequestEndAction(Reason);
		}
	}
}

void UGA_WeaponComboCoordinator::GrantActiveTransitionTags(
	const FComboTransition& Transition)
{
	if (!ActiveTransition.IsSet() || Transition.GrantedTags.IsEmpty()
		|| ActiveTransitionTagToken.IsValid()) return;
	if (UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost())
	{
		ActiveTransitionTagToken = Host->AcquireTags(
			EWeaponTagOwnerKind::Transition,
			ActiveTransition->ActionToken.AbilityHandle,
			ActiveTransition->ActionToken.ActivationSequenceID,
			Transition.TransitionID, Transition.GrantedTags);
	}
}

void UGA_WeaponComboCoordinator::ReleaseActiveTransitionTags()
{
	if (UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost())
	{
		Host->ReleaseTags(ActiveTransitionTagToken);
	}
	ActiveTransitionTagToken = FWeaponOwnedTagToken();
}

FName UGA_WeaponComboCoordinator::MakeWindowKey(
	const FWeaponActionToken& ActionToken, FName NotifyEventID)
{
	return FName(*FString::Printf(TEXT("%u:%s"),
		ActionToken.ActivationSequenceID, *NotifyEventID.ToString()));
}

bool UGA_WeaponComboCoordinator::OpenComboWindow(
	const FWeaponActionToken& ActionToken, FName NotifyEventID)
{
	if (!ActiveTransition.IsSet() || ActiveTransition->ActionToken != ActionToken) return false;
	const FName Key = MakeWindowKey(ActionToken, NotifyEventID);
	if (ComboWindows.Contains(Key)) return true;
	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	if (!Host) return false;
	FGameplayTagContainer Tags;
	Tags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.ComboWindowOpen")));
	FComboWindowEntry Entry;
	Entry.ActionToken = ActionToken;
	Entry.TagToken = Host->AcquireTags(EWeaponTagOwnerKind::NotifyWindow,
		ActionToken.AbilityHandle, ActionToken.ActivationSequenceID, NotifyEventID, Tags);
	if (!Entry.TagToken.IsValid()) return false;
	ComboWindows.Add(Key, Entry);
	return true;
}

void UGA_WeaponComboCoordinator::CloseComboWindow(
	const FWeaponActionToken& ActionToken, FName NotifyEventID)
{
	const FName Key = MakeWindowKey(ActionToken, NotifyEventID);
	FComboWindowEntry* Entry = ComboWindows.Find(Key);
	if (!Entry || Entry->ActionToken != ActionToken) return;
	if (UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost())
	{
		Host->ReleaseTags(Entry->TagToken);
	}
	ComboWindows.Remove(Key);
}

void UGA_WeaponComboCoordinator::CloseWindowsFor(const FWeaponActionToken& ActionToken)
{
	TArray<FName> Keys;
	for (const TPair<FName, FComboWindowEntry>& Pair : ComboWindows)
	{
		if (Pair.Value.ActionToken == ActionToken) Keys.Add(Pair.Key);
	}
	for (const FName Key : Keys)
	{
		if (FComboWindowEntry* Entry = ComboWindows.Find(Key))
		{
			if (UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost())
			{
				Host->ReleaseTags(Entry->TagToken);
			}
		}
		ComboWindows.Remove(Key);
	}
}

void UGA_WeaponComboCoordinator::ResetComboTimeout()
{
	AActor* Owner = GetOwningActorFromActorInfo();
	if (!Owner) return;
	Owner->GetWorldTimerManager().ClearTimer(ComboTimeoutTimer);
	if (ActiveTransition.IsSet() && ComboData && ComboData->GlobalComboTimeout > 0.f)
	{
		Owner->GetWorldTimerManager().SetTimer(ComboTimeoutTimer, this,
			&UGA_WeaponComboCoordinator::OnComboTimeout,
			ComboData->GlobalComboTimeout, false);
	}
}

void UGA_WeaponComboCoordinator::OnComboTimeout()
{
	ResetCombo(EWeaponActionEndReason::Cancelled);
}

UMHGZWeaponRuntimeHostComponent* UGA_WeaponComboCoordinator::GetRuntimeHost() const
{
	const UMHGZAbilitySystemComponent* ASC = Cast<UMHGZAbilitySystemComponent>(
		GetAbilitySystemComponentFromActorInfo());
	return ASC ? ASC->GetRuntimeHost() : nullptr;
}
