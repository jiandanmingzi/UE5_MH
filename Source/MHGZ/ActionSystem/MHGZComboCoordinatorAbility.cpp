// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZComboCoordinatorAbility.h"

#include "MHGZAbilitySystemComponent.h"
#include "MHGZAttackAbility.h"
#include "MHGZDodgeAbility.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "MHGZCharacter.h"
#include "HAL/PlatformTime.h"
#include "TimerManager.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"
#include "MHGZ.h"

namespace
{
	const FName IdleState(TEXT("Idle"));
	const FGameplayTag WeaponInputRoot = FGameplayTag::RequestGameplayTag(
		TEXT("Input.Weapon"));
	const FGameplayTag DodgeInputTag = FGameplayTag::RequestGameplayTag(
		TEXT("Input.Dodge"));
	const FGameplayTag BackVaultInputTag = FGameplayTag::RequestGameplayTag(
		TEXT("Input.Weapon.RTA"));
	const FGameplayTag GroundedTag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Grounded"));
	const FGameplayTag AerialTag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Aerial"));
	const FGameplayTag AerialFallingTag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Aerial.Falling"));
	const FGameplayTag SheathedTag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Sheathed"));
	const FGameplayTag UnsheathedTag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Unsheathed"));
	const FGameplayTag AttackingTag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Attacking"));
	const FGameplayTag DodgingTag = FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Dodging"));
	const FGameplayTag SheathingTag = FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Sheathing"));
	const FGameplayTag DeadTag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Dead"));
	const FGameplayTag HitstunTag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Hitstun"));
	const FGameplayTag KnockdownTag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Knockdown"));
	const FGameplayTag AirDodgeInputTag = FGameplayTag::RequestGameplayTag(
		TEXT("Input.AirDodge"));
	const FName EarlyDescendingThrustTransitionID(TEXT("Air_DescendingThrust_RT"));

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
	EarlyDescendingThrustSourceActionToken.Reset();
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

bool UGA_WeaponComboCoordinator::HasOpenDodgeAcceptWindowFor(
	const FWeaponActionToken& ActionToken) const
{
	const UMHGZAttackAbility* Attack = Cast<UMHGZAttackAbility>(
		ActionToken.AbilityInstance.Get());
	return Attack && Attack->HasOpenDodgeAcceptWindow(ActionToken);
}

bool UGA_WeaponComboCoordinator::TransitionRequirementsPass(
	const FComboTransition& Transition, const FWeaponInputSnapshot& Input,
	bool bIgnoreAcceptWindows, bool bAllowAerialHandoffPreInput,
	bool bAllowEarlyDescendingThrust, bool bAllowDodgePreInput) const
{
	const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	// Read-only candidate matching may ignore only this live Dodge's input lock.
	// No tags are removed and normal activation always rechecks the real gates.
	const bool bDodgeCandidate = bAllowDodgePreInput && FindLockedDodgePreInputSource();
	if (!ASC || (HasPlayerActionInputLock(ASC) && !bDodgeCandidate)
		|| !DoesTransitionMatchState(Transition)
		|| !IsSnapshotPostureCompatible(Transition, Input))
	{
		return false;
	}
	for (const FGameplayTag& RequiredTag : Transition.RequiredTags)
	{
		if (ASC->HasMatchingGameplayTag(RequiredTag))
		{
			continue;
		}

		// During an authored IG aerial lead, Falling is the one intentionally
		// withheld gate. A matching input may wait for its Handoff, but no other
		// missing requirement is forgiven.
		if (RequiredTag == AerialFallingTag && bAllowAerialHandoffPreInput
			&& CanBufferAerialHandoffInput(Input))
		{
			continue;
		}
		// AirDodge has one earlier, notify-authored release point for the
		// Descending Thrust edge only. It never opens the shared Falling gate.
		if (RequiredTag == AerialFallingTag && bAllowEarlyDescendingThrust
			&& Transition.TransitionID == EarlyDescendingThrustTransitionID
			&& IsEarlyDescendingThrustWindowOpen()
			&& CanBufferAerialHandoffInput(Input))
		{
			continue;
		}
		return false;
	}
	FGameplayTagContainer BlockedTags = Transition.BlockedTags;
	if (bDodgeCandidate) { BlockedTags.RemoveTag(DodgingTag); }
	if (!BlockedTags.IsEmpty() && ASC->HasAnyMatchingGameplayTags(BlockedTags))
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
	if (!bIgnoreAcceptWindows && Transition.bRequiresComboWindow
		&& (!ActiveTransition.IsSet()
			|| !HasOpenWindowFor(ActiveTransition->ActionToken)))
	{
		return false;
	}
	if (!bIgnoreAcceptWindows && Transition.bRequiresDodgeAcceptWindow
		&& (!ActiveTransition.IsSet()
			|| !HasOpenDodgeAcceptWindowFor(ActiveTransition->ActionToken)))
	{
		return false;
	}
	return Transition.Direction == EDirectionalInput::None
		|| Transition.Direction == Input.Direction;
}

const FComboTransition* UGA_WeaponComboCoordinator::FindBestMatch(
	const FWeaponInputSnapshot& Input, bool bIgnoreAcceptWindows,
	bool bAllowAerialHandoffPreInput, bool bAllowEarlyDescendingThrust,
	bool bAllowDodgePreInput) const
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
			|| !TransitionRequirementsPass(Candidate, Input, bIgnoreAcceptWindows,
				bAllowAerialHandoffPreInput, bAllowEarlyDescendingThrust, bAllowDodgePreInput))
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
	HasLiveBufferedCombatInput();
	if (Input.Phase != EWeaponInputPhase::Started || PendingTransition.IsSet())
	{
		return;
	}
	if (HasPlayerActionInputLock(GetAbilitySystemComponentFromActorInfo()))
	{
		if (const UMHGZDodgeAbility* Source = FindLockedDodgePreInputSource())
		{
			// Match the normal post-Dodge edge, not an imaginary attack accept window.
			if (SupportsPreInput(FindBestMatch(Input, false, false, false, true)))
			{
				CacheCombatInput(Input, &Source->GetActionToken());
			}
		}
		return;
	}
	if (const FComboTransition* Transition = FindBestMatch(Input))
	{
		if (Input.ResolvedInputTag == BackVaultInputTag)
		{
			UE_LOG(LogMHGZ, Warning,
				TEXT("[BackVault] Match id=%s State=%s Direction=%d Active=%s"),
				*Transition->TransitionID.ToString(), *CurrentState.ToString(),
				static_cast<int32>(Input.Direction),
				ActiveTransition.IsSet() ? TEXT("true") : TEXT("false"));
		}
		ExecuteTransition(*Transition, Input);
		return;
	}

	// The early point belongs only to the currently committed AirDodge token.
	// Pass that token through the regular transition transaction so successful
	// activation supersedes the source action using the existing handoff path.
	if (const FWeaponActionToken* EarlySource = GetLiveEarlyDescendingThrustSource())
	{
		if (const FComboTransition* Transition = FindBestMatch(
			Input, /* bIgnoreAcceptWindows = */ false,
			/* bAllowAerialHandoffPreInput = */ false,
			/* bAllowEarlyDescendingThrust = */ true))
		{
			ExecuteTransition(*Transition, Input, nullptr, EarlySource);
			return;
		}
	}
	if (Input.ResolvedInputTag == BackVaultInputTag)
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[BackVault] No match State=%s Direction=%d Active=%s InputTags=%s"),
			*CurrentState.ToString(), static_cast<int32>(Input.Direction),
			ActiveTransition.IsSet() ? TEXT("true") : TEXT("false"),
			*Input.ContextTags.ToStringSimple());
	}

	// Only cache an input which becomes valid when—and only when—the accept
	// gate is ignored.  State, posture, tags, stamina and direction still have
	// to pass at capture time, then are checked again on consumption.
	if (const FComboTransition* PendingMatch = FindBestMatch(Input, true))
	{
		if (SupportsPreInput(PendingMatch) && (PendingMatch->bRequiresComboWindow
			|| PendingMatch->bRequiresDodgeAcceptWindow))
		{
			CacheCombatInput(Input);
			return;
		}
	}

	// Also queue a weapon action edge pressed during an aerial lead whose only
	// missing transition requirement is the Falling tag restored by Handoff.
	// This reuses the same one-slot/latest-input-wins buffer and TTL as ComboWindow.
	if (CanBufferAerialHandoffInput(Input)
		&& SupportsPreInput(FindBestMatch(Input, true, true)))
	{
		CacheCombatInput(Input);
	}
}

bool UGA_WeaponComboCoordinator::TryBufferDirectInput(
	const FWeaponInputSnapshot& Input)
{
	HasLiveBufferedCombatInput();

	if (Input.Phase != EWeaponInputPhase::Started || PendingTransition.IsSet()
		|| !SupportsDirectPreInput(Input))
	{
		return false;
	}
	if (const UMHGZDodgeAbility* Source = FindLockedDodgePreInputSource())
	{
		if (!Input.ContextTags.HasTagExact(GroundedTag)
			|| Input.ContextTags.HasTagExact(AerialTag)) { return false; }
		return CacheCombatInput(Input, &Source->GetActionToken());
	}
	// AirDodge defaults off on its GA, rather than a hard-coded tag exclusion.
	if (Input.ResolvedInputTag == AirDodgeInputTag && CanBufferAerialHandoffInput(Input))
	{
		return CacheCombatInput(Input);
	}

	if (Input.ResolvedInputTag != DodgeInputTag
		|| !ActiveTransition.IsSet()
		|| HasPlayerActionInputLock(GetAbilitySystemComponentFromActorInfo()))
	{
		return false;
	}

	const UMHGZAbilitySystemComponent* ASC = Cast<UMHGZAbilitySystemComponent>(
		GetAbilitySystemComponentFromActorInfo());
	const UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	const FWeaponActionToken& ActiveAction = ActiveTransition->ActionToken;
	if (!ASC || !Host || !Host->IsGrounded()
		|| !ASC->FindAbilityHandleByInputTag(DodgeInputTag).IsValid()
		|| !Cast<UMHGZAttackAbility>(ActiveAction.AbilityInstance.Get())
		|| !ASC->HasMatchingGameplayTag(AttackingTag)
		|| ASC->HasMatchingGameplayTag(AerialTag)
		|| ASC->HasMatchingGameplayTag(DeadTag)
		|| ASC->HasMatchingGameplayTag(HitstunTag)
		|| ASC->HasMatchingGameplayTag(KnockdownTag)
		|| !Input.ContextTags.HasTagExact(GroundedTag)
		|| Input.ContextTags.HasTagExact(AerialTag)
		|| (!Input.ContextTags.HasTagExact(SheathedTag)
			&& !Input.ContextTags.HasTagExact(UnsheathedTag))
		|| HasOpenDodgeAcceptWindowFor(ActiveAction))
	{
		return false;
	}

	return CacheCombatInput(Input);
}

bool UGA_WeaponComboCoordinator::ExecuteTransition(
	const FComboTransition& Transition, const FWeaponInputSnapshot& Input,
	const FWeaponActionToken* SourceAction,
	const FWeaponActionToken* AerialHandoffSourceAction)
{
	if (Transition.ExecutionPolicy == EComboExecutionPolicy::StateOnly)
	{
		return SourceAction && ExecuteStateOnlyTransition(Transition, *SourceAction);
	}
	UMHGZAbilitySystemComponent* ASC = Cast<UMHGZAbilitySystemComponent>(
		GetAbilitySystemComponentFromActorInfo());
	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	if (!ASC || !Host || !Transition.AbilityClass || PendingTransition.IsSet())
	{
		return false;
	}
	const UMHGZGameplayAbility* ActionCDO =
		Cast<UMHGZGameplayAbility>(Transition.AbilityClass->GetDefaultObject());
	if (!ActionCDO || ActionCDO->GetInstancingPolicy()
		!= EGameplayAbilityInstancingPolicy::InstancedPerExecution)
	{
		return false;
	}

	const FGameplayAbilitySpecHandle AbilityHandle =
		ASC->FindWeaponAbilityHandle(Transition.AbilityClass);
	if (!AbilityHandle.IsValid())
	{
		return false;
	}

	FWeaponAbilityActivationContext Context;
	Context.RuntimeToken = Host->GetCurrentToken();
	Context.ActivationSequenceID = Host->AllocateActivationSequenceID();
	Context.TransitionID = Transition.TransitionID;
	Context.SourceState = CurrentState;
	Context.TargetState = Transition.TargetState;
	Context.MontageBlendInTime = Transition.MontageBlendInTime;
	Context.MaxCorrectionAngle = Transition.MaxCorrectionAngle;
	Context.Input = Input;

	FPendingComboTransition Pending;
	Pending.TransitionID = Transition.TransitionID;
	Pending.AbilityHandle = AbilityHandle;
	Pending.ActivationSequenceID = Context.ActivationSequenceID;
	Pending.ActivationContext = Context;
	if (AerialHandoffSourceAction && AerialHandoffSourceAction->IsValid()
		&& AerialHandoffSourceAction->RuntimeToken == Context.RuntimeToken)
	{
		const UMHGZGameplayAbility* HandoffSourceAbility =
			Cast<UMHGZGameplayAbility>(AerialHandoffSourceAction->AbilityInstance.Get());
		if (HandoffSourceAbility && HandoffSourceAbility->IsActive()
			&& HandoffSourceAbility->GetActionToken() == *AerialHandoffSourceAction)
		{
			Pending.AerialHandoffSourceActionToken = *AerialHandoffSourceAction;
		}
	}
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

	ClearBufferedCombatInput();
	PendingTransition.Reset();
	const FWeaponActionToken AerialHandoffSource =
		Pending.AerialHandoffSourceActionToken;
	const auto EndAerialHandoffSource = [this, &AerialHandoffSource, &ActionToken]()
	{
		if (!AerialHandoffSource.IsValid() || AerialHandoffSource == ActionToken)
		{
			return;
		}
		if (UMHGZGameplayAbility* SourceAbility = Cast<UMHGZGameplayAbility>(
			AerialHandoffSource.AbilityInstance.Get()))
		{
			SourceAbility->RequestEndAction(EWeaponActionEndReason::Superseded);
		}
		CloseWindowsFor(AerialHandoffSource);
	};
	if (Transition->StatePolicy == EComboStatePolicy::Preserve)
	{
		// Preserve keeps the combo FSM predecessor, not the aerial action which
		// yielded its movement ownership at this authored handoff.
		EndAerialHandoffSource();
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
	if (AerialHandoffSource != PreviousAction)
	{
		EndAerialHandoffSource();
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
	if (BufferedCombatInput.IsSet() && BufferedCombatInput->DodgeSourceAction == ActionToken)
	{
		const auto* Source = Cast<UMHGZDodgeAbility>(ActionToken.AbilityInstance.Get());
		if (Reason == EWeaponActionEndReason::Normal && Source && Source->GetActionToken() == ActionToken)
		{
			BufferedCombatInput->bDodgeSourceCompletedNormally = true;
		}
		else { ClearBufferedCombatInput(); }
	}
	if (!ActiveTransition.IsSet() || ActiveTransition->ActionToken != ActionToken) return;
	ClearBufferedCombatInput();
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
	ClearBufferedCombatInput();
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
	TryConsumeBufferedCombatInput(/* bAllowDirectInput = */ false);
	return true;
}

void UGA_WeaponComboCoordinator::OnDodgeAcceptWindowOpened(
	const FWeaponActionToken& ActionToken)
{
	if (!ActiveTransition.IsSet() || ActiveTransition->ActionToken != ActionToken)
	{
		return;
	}
	TryConsumeBufferedCombatInput(/* bAllowDirectInput = */ true);
}

void UGA_WeaponComboCoordinator::OnAerialHandoffWindowOpened(
	const FWeaponActionToken* SourceActionToken)
{
	const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC || !ASC->HasMatchingGameplayTag(AerialFallingTag))
	{
		return;
	}
	TryConsumeBufferedCombatInput(/* bAllowDirectInput = */ true,
		SourceActionToken);
}

bool UGA_WeaponComboCoordinator::OpenEarlyDescendingThrustWindow(
	const FWeaponActionToken& SourceActionToken)
{
	if (!SourceActionToken.IsValid())
	{
		return false;
	}
	EarlyDescendingThrustSourceActionToken = SourceActionToken;
	if (!IsEarlyDescendingThrustWindowOpen())
	{
		EarlyDescendingThrustSourceActionToken.Reset();
		return false;
	}

	// A valid RT snapshot may already be waiting in the ordinary aerial
	// pre-input slot. Re-match it now with the narrowly-scoped early exception.
	TryConsumeBufferedCombatInput(/* bAllowDirectInput = */ false,
		&SourceActionToken, /* bAllowEarlyDescendingThrust = */ true);
	return true;
}

void UGA_WeaponComboCoordinator::CloseEarlyDescendingThrustWindow(
	const FWeaponActionToken& SourceActionToken)
{
	if (EarlyDescendingThrustSourceActionToken.IsSet()
		&& EarlyDescendingThrustSourceActionToken.GetValue() == SourceActionToken)
	{
		EarlyDescendingThrustSourceActionToken.Reset();
	}
}

bool UGA_WeaponComboCoordinator::IsEarlyDescendingThrustWindowOpen() const
{
	return GetLiveEarlyDescendingThrustSource() != nullptr;
}

void UGA_WeaponComboCoordinator::OnDirectActionConfirmed()
{
	ClearBufferedCombatInput();
}

void UGA_WeaponComboCoordinator::DiscardDodgePreInput(const FWeaponActionToken& SourceActionToken)
{
	if (BufferedCombatInput.IsSet() && BufferedCombatInput->DodgeSourceAction == SourceActionToken)
	{
		ClearBufferedCombatInput();
	}
}

void UGA_WeaponComboCoordinator::OnDodgePreInputWindowOpened(
	const FWeaponActionToken& SourceActionToken)
{
	if (!HasLiveBufferedCombatInput()
		|| BufferedCombatInput->DodgeSourceAction != SourceActionToken) { return; }
	const auto* Source = Cast<UMHGZDodgeAbility>(SourceActionToken.AbilityInstance.Get());
	const auto* ASC = GetAbilitySystemComponentFromActorInfo();
	const auto* Host = GetRuntimeHost();
	if (!Host || !ASC
		|| !Host->IsTokenCurrent(SourceActionToken.RuntimeToken) || !Host->IsGrounded()
		|| !ASC->HasMatchingGameplayTag(GroundedTag) || ASC->HasMatchingGameplayTag(AerialTag)
		|| ASC->HasMatchingGameplayTag(DeadTag) || ASC->HasMatchingGameplayTag(HitstunTag)
		|| ASC->HasMatchingGameplayTag(KnockdownTag) || HasPlayerActionInputLock(ASC)
		|| (!BufferedCombatInput->bDodgeSourceCompletedNormally
			&& (!Source || Source->GetActionToken() != SourceActionToken || !Source->IsActionRecoveryOpen())))
	{
		return;
	}
	TryConsumeBufferedCombatInput(true, nullptr, false, &SourceActionToken);
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

bool UGA_WeaponComboCoordinator::CanDodgeSupersedeActiveAction() const
{
	if (!ActiveTransition.IsSet())
	{
		return false;
	}

	const FWeaponActionToken& ActiveAction = ActiveTransition->ActionToken;
	return HasOpenDodgeAcceptWindowFor(ActiveAction);
}

bool UGA_WeaponComboCoordinator::PrepareActiveActionForDodge(
	const FWeaponActionToken& DodgeActionToken)
{
	UMHGZDodgeAbility* Dodge = Cast<UMHGZDodgeAbility>(
		DodgeActionToken.AbilityInstance.Get());
	if (!Dodge || DodgeActionToken != Dodge->GetActionToken()
		|| !Dodge->IsActionActivationCommitted()
		|| !CanDodgeSupersedeActiveAction())
	{
		return false;
	}

	const FWeaponActionToken PreviousAction = ActiveTransition->ActionToken;
	UMHGZAttackAbility* PreviousAttack = Cast<UMHGZAttackAbility>(
		PreviousAction.AbilityInstance.Get());
	if (!PreviousAttack)
	{
		return false;
	}

	return PreviousAttack->PrepareDodgeSupersede(DodgeActionToken);
}

bool UGA_WeaponComboCoordinator::CommitActiveActionDodgeSupersede(
	const FWeaponActionToken& DodgeActionToken)
{
	if (!ActiveTransition.IsSet())
	{
		return false;
	}
	UMHGZAttackAbility* PreviousAttack = Cast<UMHGZAttackAbility>(
		ActiveTransition->ActionToken.AbilityInstance.Get());
	return PreviousAttack
		&& PreviousAttack->CommitDodgeSupersede(DodgeActionToken);
}

void UGA_WeaponComboCoordinator::CancelActiveActionDodgeSupersede(
	const FWeaponActionToken& DodgeActionToken)
{
	if (!ActiveTransition.IsSet())
	{
		return;
	}
	if (UMHGZAttackAbility* PreviousAttack = Cast<UMHGZAttackAbility>(
		ActiveTransition->ActionToken.AbilityInstance.Get()))
	{
		PreviousAttack->CancelDodgeSupersede(DodgeActionToken);
	}
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

// ── M5 空中回避：可操作帧闸门 / 独立预输入槽 / 让位 ──────────────────────────

int32 UGA_WeaponComboCoordinator::SupersedeOtherIGAerialActions(
	const FWeaponActionToken& Superseder)
{
	UMHGZAbilitySystemComponent* ASC = Cast<UMHGZAbilitySystemComponent>(
		GetAbilitySystemComponentFromActorInfo());
	if (!ASC)
	{
		return 0;
	}

	// 先收集再结束（照 MHGZHitReactionAbility::CancelActivePlayerActions 的两段式）：
	// RequestEndAction 同步走 EndAbility → UnregisterAction / TaskOwnerEnded，迭代中容器会变。
	TArray<TObjectPtr<UMHGZGameplayAbility>> ActionsToEnd;
	for (FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		for (UGameplayAbility* Instance : Spec.GetAbilityInstances())
		{
			UMHGZInsectGlaiveAbility* Action = Cast<UMHGZInsectGlaiveAbility>(Instance);
			if (Action && Action->IsActionActivationCommitted() && Action->IsActive()
				&& Action != Superseder.AbilityInstance.Get())
			{
				ActionsToEnd.Add(Action);
			}
		}
	}
	for (UMHGZGameplayAbility* Action : ActionsToEnd)
	{
		if (Action && Action->IsActive())
		{
			// 与 ConfirmTransitionActivation 的让位同一行语义：Superseded ⇒ bWasCancelled，
			// 被让位者不会抢落地/自由落体交接（两招 EndAbility 的旗标都带 !bWasCancelled 守卫）。
			// RequestEndAction 返回时位移所有权已释放（UnregisterAction 清槽先于任务拆除）。
			Action->RequestEndAction(EWeaponActionEndReason::Superseded);
		}
	}
	return ActionsToEnd.Num();
}

bool UGA_WeaponComboCoordinator::SupportsPreInput(const FComboTransition* Transition) const
{
	const auto* Defaults = Transition && Transition->AbilityClass
		? Cast<UMHGZGameplayAbility>(Transition->AbilityClass->GetDefaultObject()) : nullptr;
	return Defaults && Defaults->bAllowPreInputActivation;
}

bool UGA_WeaponComboCoordinator::SupportsDirectPreInput(const FWeaponInputSnapshot& Input) const
{
	auto* ASC = Cast<UMHGZAbilitySystemComponent>(GetAbilitySystemComponentFromActorInfo());
	const FGameplayAbilitySpec* Spec = ASC
		? ASC->FindAbilitySpecFromHandle(ASC->FindAbilityHandleByInputTag(Input.ResolvedInputTag)) : nullptr;
	const auto* Defaults = Spec ? Cast<UMHGZGameplayAbility>(Spec->Ability) : nullptr;
	return Defaults && Defaults->bAllowPreInputActivation;
}

UMHGZDodgeAbility* UGA_WeaponComboCoordinator::FindLockedDodgePreInputSource() const
{
	auto* ASC = Cast<UMHGZAbilitySystemComponent>(GetAbilitySystemComponentFromActorInfo());
	const auto* Host = GetRuntimeHost();
	if (!ASC || !Host || !Host->IsGrounded() || !ASC->HasMatchingGameplayTag(GroundedTag)
		|| !ASC->HasMatchingGameplayTag(DodgingTag) || ASC->HasMatchingGameplayTag(SheathingTag)
		|| ASC->HasMatchingGameplayTag(AerialTag) || ASC->HasMatchingGameplayTag(DeadTag)
		|| ASC->HasMatchingGameplayTag(HitstunTag) || ASC->HasMatchingGameplayTag(KnockdownTag)) { return nullptr; }
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		for (UGameplayAbility* Instance : Spec.GetAbilityInstances())
		{
			auto* Dodge = Cast<UMHGZDodgeAbility>(Instance);
			if (Dodge && Dodge->IsActive() && Dodge->IsActionActivationCommitted()
				&& Host->IsActionRegistered(Dodge->GetActionToken())) { return Dodge; }
		}
	}
	return nullptr;
}

bool UGA_WeaponComboCoordinator::CacheCombatInput(const FWeaponInputSnapshot& Input,
	const FWeaponActionToken* DodgeSource)
{
	if (!ComboData || !FMath::IsFinite(ComboData->PreInputLifetime)
		|| ComboData->PreInputLifetime <= 0.f)
	{
		return false;
	}

	FBufferedCombatInput Buffered;
	Buffered.Snapshot = Input;
	Buffered.ExpireAt = FPlatformTime::Seconds() + ComboData->PreInputLifetime;
	if (DodgeSource) { Buffered.DodgeSourceAction = *DodgeSource; }
	BufferedCombatInput = MoveTemp(Buffered);
	return true;
}

bool UGA_WeaponComboCoordinator::CanBufferAerialHandoffInput(
	const FWeaponInputSnapshot& Input) const
{
	if (Input.Phase != EWeaponInputPhase::Started
		|| (!Input.ResolvedInputTag.MatchesTag(WeaponInputRoot)
			&& Input.ResolvedInputTag != AirDodgeInputTag)
		|| !Input.ContextTags.HasTagExact(AerialTag)
		|| Input.ContextTags.HasTagExact(GroundedTag))
	{
		return false;
	}

	const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	const UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	return ASC && Host
		&& ASC->HasMatchingGameplayTag(AerialTag)
		&& !ASC->HasMatchingGameplayTag(GroundedTag)
		&& !ASC->HasMatchingGameplayTag(AerialFallingTag)
		&& !Host->IsGrounded();
}

const FWeaponActionToken*
UGA_WeaponComboCoordinator::GetLiveEarlyDescendingThrustSource() const
{
	if (!EarlyDescendingThrustSourceActionToken.IsSet())
	{
		return nullptr;
	}

	const FWeaponActionToken& SourceAction =
		EarlyDescendingThrustSourceActionToken.GetValue();
	const UMHGZAirDodgeAbility* AirDodge = Cast<UMHGZAirDodgeAbility>(
		SourceAction.AbilityInstance.Get());
	const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	const UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	return AirDodge && AirDodge->IsActive()
		&& AirDodge->IsActionActivationCommitted()
		&& AirDodge->GetActionToken() == SourceAction
		&& ASC && ASC->HasMatchingGameplayTag(AerialTag)
		&& !ASC->HasMatchingGameplayTag(GroundedTag)
		&& !ASC->HasMatchingGameplayTag(AerialFallingTag)
		&& Host && !Host->IsGrounded()
		&& Host->IsTokenCurrent(SourceAction.RuntimeToken)
		? &SourceAction : nullptr;
}

bool UGA_WeaponComboCoordinator::HasLiveBufferedCombatInput()
{
	if (!BufferedCombatInput.IsSet())
	{
		return false;
	}
	if (FPlatformTime::Seconds() > BufferedCombatInput->ExpireAt)
	{
		ClearBufferedCombatInput();
		return false;
	}
	const auto& Source = BufferedCombatInput->DodgeSourceAction;
	if (Source.ActivationSequenceID != 0 && ((!Source.IsValid() && !BufferedCombatInput->bDodgeSourceCompletedNormally)
		|| !GetRuntimeHost()
		|| !GetRuntimeHost()->IsTokenCurrent(Source.RuntimeToken)))
	{
		ClearBufferedCombatInput();
		return false;
	}
	return true;
}

void UGA_WeaponComboCoordinator::ClearBufferedCombatInput()
{
	BufferedCombatInput.Reset();
}

void UGA_WeaponComboCoordinator::TryConsumeBufferedCombatInput(
	bool bAllowDirectInput,
	const FWeaponActionToken* AerialHandoffSourceAction,
	bool bAllowEarlyDescendingThrust, const FWeaponActionToken* DodgeSource)
{
	if (!HasLiveBufferedCombatInput() || PendingTransition.IsSet())
	{
		return;
	}
	if (BufferedCombatInput->DodgeSourceAction.ActivationSequenceID != 0
		&& (!DodgeSource || BufferedCombatInput->DodgeSourceAction != *DodgeSource)) { return; }

	const FWeaponInputSnapshot Input = BufferedCombatInput->Snapshot;
	if (Input.ResolvedInputTag.MatchesTag(WeaponInputRoot))
	{
		if (HasPlayerActionInputLock(GetAbilitySystemComponentFromActorInfo()))
		{
			ClearBufferedCombatInput();
			return;
		}
		const FComboTransition* Transition = bAllowEarlyDescendingThrust
			? FindBestMatch(Input, /* bIgnoreAcceptWindows = */ false,
				/* bAllowAerialHandoffPreInput = */ false,
				/* bAllowEarlyDescendingThrust = */ true)
			: FindBestMatch(Input);
		if (Transition)
		{
			ClearBufferedCombatInput();
			if (!SupportsPreInput(Transition)) { return; }
			const FWeaponActionToken* SourceAction = bAllowEarlyDescendingThrust
				? GetLiveEarlyDescendingThrustSource() : AerialHandoffSourceAction;
			ExecuteTransition(*Transition, Input, nullptr, SourceAction);
			return;
		}
		if (DodgeSource)
		{
			// This is the one Dodge release point, not a later attack-window wait.
			ClearBufferedCombatInput();
			return;
		}

		// A weapon edge may require both accept gates.  Keep it while the
		// virtual match is still valid, so the other gate may consume it.
		const bool bStillMatchesAcceptGate = FindBestMatch(Input, true)
			|| (bAllowEarlyDescendingThrust
				&& (FindBestMatch(Input, true, true)
					|| FindBestMatch(Input, true, false, true)));
		if (!bStillMatchesAcceptGate)
		{
			ClearBufferedCombatInput();
		}
		return;
	}

	if (!bAllowDirectInput)
	{
		return;
	}

	// The caller has opened the exact attack/Dodge/aerial release point.
	// Clear first so an activation failure cannot recursively re-buffer itself.
	ClearBufferedCombatInput();
	if (!SupportsDirectPreInput(Input)) { return; }
	if (UMHGZAbilitySystemComponent* ASC = Cast<UMHGZAbilitySystemComponent>(
		GetAbilitySystemComponentFromActorInfo()))
	{
		ASC->TryActivateDirectInput(Input);
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
