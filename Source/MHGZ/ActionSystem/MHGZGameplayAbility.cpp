// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZGameplayAbility.h"

#include "AbilityTask_MHGZStaminaDrain.h"
#include "MHGZAbilityCostGameplayEffects.h"
#include "MHGZAbilitySystemComponent.h"
#include "MHGZComboCoordinatorAbility.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "MHGZCharacter.h"
#include "MHGZ.h"

UMHGZGameplayAbility::UMHGZGameplayAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerExecution;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalOnly;
}

bool UMHGZGameplayAbility::HasPlayerActionInputLock(
	const UAbilitySystemComponent* ASC)
{
	if (!ASC)
	{
		return false;
	}

	static const FGameplayTagContainer ActionInputLocks = []
	{
		FGameplayTagContainer Tags;
		Tags.AddTag(FGameplayTag::RequestGameplayTag(
			TEXT("Combat.State.Sheathing")));
		Tags.AddTag(FGameplayTag::RequestGameplayTag(
			TEXT("Combat.State.Dodging")));
		return Tags;
	}();
	return ASC->HasAnyMatchingGameplayTags(ActionInputLocks);
}

// ── 成本 / 冷却（原生 GE） ──────────────────────────────────────────────────

bool UMHGZGameplayAbility::CheckCost(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (StaminaCostPolicy != EAbilityStaminaCostPolicy::Instant)
	{
		return true; // PerSecond 由 Drain Task 按实际经过时间结算并在不足时自取消。
	}

	const UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (!ASC)
	{
		return true;
	}

	const float Cost = GetInstantStaminaCost(Handle, ActorInfo);
	if (Cost <= 0.f)
	{
		return true;
	}

	const UMHGZAttributeSet* AttrSet = ASC->GetSet<UMHGZAttributeSet>();
	return !AttrSet || AttrSet->GetStamina() >= Cost;
}

void UMHGZGameplayAbility::ApplyCost(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo) const
{
	if (StaminaCostPolicy != EAbilityStaminaCostPolicy::Instant)
	{
		return;
	}

	const float Cost = GetInstantStaminaCost(Handle, ActorInfo);
	if (Cost <= 0.f)
	{
		return;
	}

	FGameplayEffectSpecHandle Spec = MakeStaminaCostSpec(-Cost);
	UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (ASC && Spec.IsValid())
	{
		ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
	}
}

bool UMHGZGameplayAbility::CheckCooldown(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!CooldownTag.IsValid())
	{
		return true;
	}

	const float Duration = CooldownDuration.GetValueAtLevel(
		GetAbilityLevel(Handle, ActorInfo));
	if (Duration <= 0.f)
	{
		return true;
	}

	const UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (!ASC)
	{
		return true;
	}

	return !ASC->HasMatchingGameplayTag(CooldownTag);
}

void UMHGZGameplayAbility::ApplyCooldown(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo) const
{
	if (!CooldownTag.IsValid())
	{
		return;
	}

	const float Duration = CooldownDuration.GetValueAtLevel(
		GetAbilityLevel(Handle, ActorInfo));
	if (Duration <= 0.f)
	{
		return;
	}

	FGameplayEffectSpecHandle Spec = MakeCooldownSpec(Duration);
	UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (ASC && Spec.IsValid())
	{
		Spec.Data->DynamicGrantedTags.AddTag(CooldownTag);
		ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
	}
}

bool UMHGZGameplayAbility::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags,
		OptionalRelevantTags))
	{
		return false;
	}
	if (ShouldIgnorePlayerActionLocks())
	{
		return true;
	}

	const UAbilitySystemComponent* ASC = ActorInfo
		? ActorInfo->AbilitySystemComponent.Get()
		: nullptr;
	return ASC && !HasPlayerActionInputLock(ASC);
}

// ── 激活 / 结束 ─────────────────────────────────────────────────────────────

void UMHGZGameplayAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	MotionMatchingPhaseState = FMotionMatchingPhaseState();

	UMHGZAbilitySystemComponent* MHGZASC =
		Cast<UMHGZAbilitySystemComponent>(ActorInfo->AbilitySystemComponent.Get());
	UMHGZWeaponRuntimeHostComponent* Host = MHGZASC ? MHGZASC->GetRuntimeHost() : nullptr;

	// 1. 消费一次性激活上下文；无则从当前 Host 构建安全直连上下文。
	FWeaponAbilityActivationContext ConsumedContext;
	const bool bHasPendingContext =
		MHGZASC && MHGZASC->ConsumePendingActivationContext(Handle, ConsumedContext);
	ActivationContext = bHasPendingContext ? ConsumedContext : FWeaponAbilityActivationContext();
	if (!bHasPendingContext && Host)
	{
		ActivationContext.RuntimeToken = Host->GetCurrentToken();
		ActivationContext.ActivationSequenceID = Host->AllocateActivationSequenceID();
	}

	// 2. 构建 ActionToken 候选；缺少序列号时由 Host 分配。
	CurrentActionToken.RuntimeToken = ActivationContext.RuntimeToken;
	CurrentActionToken.AbilityHandle = Handle;
	CurrentActionToken.AbilityInstance = this;
	CurrentActionToken.ActivationSequenceID = ActivationContext.ActivationSequenceID;
	if (CurrentActionToken.ActivationSequenceID == 0 && Host)
	{
		CurrentActionToken.ActivationSequenceID = Host->AllocateActivationSequenceID();
		ActivationContext.ActivationSequenceID = CurrentActionToken.ActivationSequenceID;
	}

	const bool bHasTransition = !ActivationContext.TransitionID.IsNone();
	const auto RejectTransitionAndEnd =
		[this, Handle, ActorInfo, ActivationInfo, bHasTransition](bool bWasCancelled)
		{
			if (bHasTransition)
			{
				if (UMHGZAbilitySystemComponent* ASC =
					Cast<UMHGZAbilitySystemComponent>(ActorInfo->AbilitySystemComponent.Get()))
				{
					if (UGA_WeaponComboCoordinator* Coordinator = ASC->GetActiveComboCoordinator())
					{
						Coordinator->RejectTransitionActivation(CurrentActionToken);
					}
				}
			}
			EndAbility(Handle, ActorInfo, ActivationInfo, false, bWasCancelled);
		};

	// Every action is scoped to one live Pawn runtime. A structurally valid but
	// stale ActivationContext must fail before reservation, Commit, tags or FSM
	// confirmation; otherwise a delayed activation could mutate a rebuilt Host.
	if (!Host || !CurrentActionToken.IsValid()
		|| !Host->IsTokenCurrent(CurrentActionToken.RuntimeToken))
	{
		RejectTransitionAndEnd(true);
		return;
	}

	// 3. Action 依赖预检（Commit 前、零副作用；失败不产生预留/成本/冷却/Tag）。
	if (!ValidateActionDependencies())
	{
		RejectTransitionAndEnd(true);
		return;
	}

	// 4. 武器资源预留。
	bool bReservationOK = false;
	if (Host)
	{
		bReservationOK = Host->TryReserveCosts(CurrentActionToken, WeaponResourceCosts, ActiveReservation);
	}
	else
	{
		bReservationOK = WeaponResourceCosts.IsEmpty();
		ActiveReservation = FWeaponResourceCostReservation();
	}
	if (!bReservationOK)
	{
		RejectTransitionAndEnd(true);
		return;
	}

	// 5. Commit（CheckCost/ApplyCost/CheckCooldown/ApplyCooldown）。
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		if (Host)
		{
			Host->ReleaseReservation(ActiveReservation);
		}
		ActiveReservation = FWeaponResourceCostReservation();
		RejectTransitionAndEnd(true);
		return;
	}

	// 6. 结算预留。
	if (Host)
	{
		Host->ConsumeReservedCosts(ActiveReservation);
	}
	bReservationConsumed = true;
	bIsActionActivationCommitted = true;

	// 7. 转移确认（仅带 TransitionID 的上下文）。
	if (bHasTransition)
	{
		UGA_WeaponComboCoordinator* Coordinator =
			MHGZASC ? MHGZASC->GetActiveComboCoordinator() : nullptr;
		if (!Coordinator || !Coordinator->ConfirmTransitionActivation(CurrentActionToken))
		{
			if (Coordinator)
			{
				Coordinator->RejectTransitionActivation(CurrentActionToken);
			}
			EndAbility(Handle, ActorInfo, ActivationInfo, false, true);
			return;
		}
	}

	// 8. 注册 Active Action。
	if (Host && !Host->RegisterAction(CurrentActionToken))
	{
		RequestEndAction(EWeaponActionEndReason::Interrupted);
		return;
	}

	// 9. PerSecond 耐力消耗任务。
	if (StaminaCostPolicy == EAbilityStaminaCostPolicy::PerSecond)
	{
		StaminaDrainTask = UAbilityTask_MHGZStaminaDrain::StartStaminaDrain(this);
	}
}

void UMHGZGameplayAbility::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	if (bMHGZEndCleanupDone)
	{
		return; // 幂等：单次清理。
	}
	bMHGZEndCleanupDone = true;
	bIsActionActivationCommitted = false;
	MotionMatchingPhaseState = FMotionMatchingPhaseState();

	// 1. 结束耐力消耗任务。
	if (StaminaDrainTask.IsValid())
	{
		StaminaDrainTask->EndTask();
		StaminaDrainTask = nullptr;
	}

	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();

	// 2. 释放未消耗的预留。
	if (!bReservationConsumed)
	{
		if (Host)
		{
			Host->ReleaseReservation(ActiveReservation);
		}
		ActiveReservation = FWeaponResourceCostReservation();
	}

	// 3. 注销 Montage 与 Active Action。
	if (Host && CurrentActionToken.IsValid())
	{
		Host->UnregisterMontages(CurrentActionToken);
		Host->UnregisterAction(CurrentActionToken);
	}

	// 4. 释放本 Action 拥有的 Ability Tags。
	ReleaseActionTags();

	// 5. 所有动作类型都以精确 ActionToken 通知 FSM。旧动作、Preserve
	// 命令与通用动作因 Token 不匹配而安全忽略；当前 Replace 动作正常
	// 结束时由同一入口关闭窗口、释放转移 Tag 并回到 Idle。
	if (UMHGZAbilitySystemComponent* ASC = Cast<UMHGZAbilitySystemComponent>(
		ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr))
	{
		if (UGA_WeaponComboCoordinator* Coordinator = ASC->GetActiveComboCoordinator())
		{
			Coordinator->OnActionFinished(CurrentActionToken, GetActionEndReason());
		}
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

// ── M1 Action 运行时 API ────────────────────────────────────────────────────

void UMHGZGameplayAbility::RequestEndAction(EWeaponActionEndReason Reason)
{
	if (bActionEndRequested)
	{
		return; // 首个原因生效。
	}

	bActionEndRequested = true;
	ActionEndReason = Reason;
	EndAbility(
		CurrentSpecHandle,
		CurrentActorInfo,
		CurrentActivationInfo,
		false,
		Reason != EWeaponActionEndReason::Normal);
}

void UMHGZGameplayAbility::HandleInputReleased(const FWeaponInputSnapshot& Snapshot)
{
	// 默认 No-Op：普通攻击/闪避不得因按键释放而结束。
	// 蓄力/按住型子类覆写此方法并按自身策略 RequestEndAction。
}

void UMHGZGameplayAbility::HandleLanded(const FHitResult& Hit)
{
	// 默认无操作；子类按需响应精确 Action 的落地。
}

bool UMHGZGameplayAbility::RegisterMontageInstance(
	USkeletalMeshComponent* Mesh,
	int32 MontageInstanceID)
{
	if (UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost())
	{
		return Host->RegisterMontage(CurrentActionToken, Mesh, MontageInstanceID);
	}
	return false;
}

FWeaponOwnedTagToken UMHGZGameplayAbility::AcquireActionTags(
	const FGameplayTagContainer& Tags,
	FName LocalID)
{
	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	if (!Host || !CurrentActionToken.IsValid() || Tags.IsEmpty())
	{
		return FWeaponOwnedTagToken();
	}

	FWeaponOwnedTagToken Token = Host->AcquireTags(
		EWeaponTagOwnerKind::Ability,
		CurrentActionToken.AbilityHandle,
		CurrentActionToken.ActivationSequenceID,
		LocalID,
		Tags);
	if (Token.IsValid())
	{
		OwnedActionTagTokens.Add(Token);
	}
	return Token;
}

bool UMHGZGameplayAbility::ReleaseActionTag(FWeaponOwnedTagToken& InOutToken)
{
	if (!InOutToken.IsValid())
	{
		return false;
	}

	const int32 OwnedIndex = OwnedActionTagTokens.IndexOfByPredicate(
		[&InOutToken](const FWeaponOwnedTagToken& OwnedToken)
		{
			return OwnedToken.RuntimeToken == InOutToken.RuntimeToken
				&& OwnedToken.TokenID == InOutToken.TokenID;
		});
	if (OwnedIndex == INDEX_NONE)
	{
		InOutToken = FWeaponOwnedTagToken();
		return false;
	}

	const FWeaponOwnedTagToken OwnedToken = OwnedActionTagTokens[OwnedIndex];
	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	const bool bReleased = Host && Host->ReleaseTags(OwnedToken);
	OwnedActionTagTokens.RemoveAtSwap(OwnedIndex, 1, EAllowShrinking::No);
	InOutToken = FWeaponOwnedTagToken();
	return bReleased;
}

void UMHGZGameplayAbility::ReleaseActionTags()
{
	if (UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost())
	{
		for (const FWeaponOwnedTagToken& Token : OwnedActionTagTokens)
		{
			Host->ReleaseTags(Token);
		}
	}
	OwnedActionTagTokens.Reset();
}

UMHGZWeaponRuntimeHostComponent* UMHGZGameplayAbility::GetRuntimeHost() const
{
	const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	const UMHGZAbilitySystemComponent* MHGZASC =
		Cast<const UMHGZAbilitySystemComponent>(ASC);
	return MHGZASC ? MHGZASC->GetRuntimeHost() : nullptr;
}

// ----------------------------------------------------------------------
// M4.4 Root Motion Phase / Motion Matching Handoff
// ----------------------------------------------------------------------

bool UMHGZGameplayAbility::IsCurrentMotionMatchingAction(
	const FWeaponActionToken& ActionToken) const
{
	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	return IsActive() && !bMHGZEndCleanupDone && bIsActionActivationCommitted
		&& ActionToken.IsValid() && ActionToken == CurrentActionToken
		&& Host && Host->IsTokenCurrent(ActionToken.RuntimeToken);
}

void UMHGZGameplayAbility::SampleMotionMatchingPhaseInput()
{
	if (MotionMatchingPhaseState.InputObservationPhaseDepth <= 0)
	{
		return;
	}

	FVector2D RawMoveInput = FVector2D::ZeroVector;
	bool bHasRawMoveInput = false;
	GetMotionMatchingRawMoveInput(RawMoveInput, bHasRawMoveInput);
	MotionMatchingPhaseState.LastRawMoveInput = RawMoveInput;
	if (bHasRawMoveInput)
	{
		MotionMatchingPhaseState.bObservedRawMoveInput = true;
		if (const AMHGZCharacter* Character = Cast<AMHGZCharacter>(
			GetAvatarActorFromActorInfo()))
		{
			MotionMatchingPhaseState.LastActiveRawMoveCruiseSpeed =
				Character->GetQuantizedCruiseSpeedForRawMoveInput(RawMoveInput.Size());
		}
	}
	else if (MotionMatchingPhaseState.bRawMoveInputWasActive
		&& MotionMatchingPhaseState.bObservedRawMoveInput)
	{
		// This uses raw physical input, not bHasInput/BlockMovement. A mobile
		// action tail can therefore remember a real release even while its
		// locomotion output is intentionally suppressed.
		MotionMatchingPhaseState.bReleasedRawMoveInput = true;
	}
	MotionMatchingPhaseState.bRawMoveInputWasActive = bHasRawMoveInput;
}

bool UMHGZGameplayAbility::BeginActionRootMotionPhase(
	const FWeaponActionToken& ActionToken, const bool bOwnsMontageRootMotion,
	const bool bObserveRawMovementInput)
{
	if (!IsCurrentMotionMatchingAction(ActionToken))
	{
		return false;
	}

	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	if (!Host)
	{
		UE_LOG(LogMHGZ, Warning, TEXT("[MotionMatchingHandoff] Rejected Ability=%s: missing RuntimeHost."),
			*GetClass()->GetName());
		return false;
	}
	if (Host->IsMontageRootMotionOwned() && !Host->IsMontageRootMotionOwnedBy(ActionToken))
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[MotionMatchingHandoff] Rejected Ability=%s: a competing Montage Root Motion owner exists."),
			*GetClass()->GetName());
		return false;
	}

	if (bOwnsMontageRootMotion && !Host->AcquireMontageRootMotion(ActionToken))
	{
		return false;
	}

	++MotionMatchingPhaseState.ActivePhaseDepth;
	if (bOwnsMontageRootMotion)
	{
		++MotionMatchingPhaseState.OwningRootMotionPhaseDepth;
	}
	if (bObserveRawMovementInput)
	{
		++MotionMatchingPhaseState.InputObservationPhaseDepth;
		SampleMotionMatchingPhaseInput();
	}
	MotionMatchingPhaseState.bObservedAnyPhase = true;
	return true;
}

bool UMHGZGameplayAbility::ObserveActionRootMotionPhase(
	const FWeaponActionToken& ActionToken)
{
	if (!IsCurrentMotionMatchingAction(ActionToken)
		|| MotionMatchingPhaseState.ActivePhaseDepth <= 0)
	{
		return false;
	}

	SampleMotionMatchingPhaseInput();
	return true;
}

bool UMHGZGameplayAbility::EndActionRootMotionPhase(
	const FWeaponActionToken& ActionToken, const bool bOwnedMontageRootMotion,
	const bool bObservedRawMovementInput)
{
	if (!IsCurrentMotionMatchingAction(ActionToken)
		|| MotionMatchingPhaseState.ActivePhaseDepth <= 0
		|| (bOwnedMontageRootMotion
			&& MotionMatchingPhaseState.OwningRootMotionPhaseDepth <= 0)
		|| (bObservedRawMovementInput
			&& MotionMatchingPhaseState.InputObservationPhaseDepth <= 0))
	{
		return false;
	}

	if (bObservedRawMovementInput)
	{
		SampleMotionMatchingPhaseInput();
		--MotionMatchingPhaseState.InputObservationPhaseDepth;
	}
	if (bOwnedMontageRootMotion)
	{
		--MotionMatchingPhaseState.OwningRootMotionPhaseDepth;
	}
	--MotionMatchingPhaseState.ActivePhaseDepth;

	// Ending the phase proves an authored no-root-motion safety segment begins,
	// but deliberately does not release yet.  MMHandoff validates Commit/type/
	// ownership and publishes atomically; a missing or malformed Handoff thus
	// leaves the old Montage on its compatibility owner instead of exposing MM
	// to its functional tail.
	return true;
}

bool UMHGZGameplayAbility::HandleMotionMatchingHandoff(
	const FWeaponActionToken& ActionToken,
	const EMHGZMotionMatchingHandoffType HandoffType)
{
	const TCHAR* RejectionReason = nullptr;
	if (!IsCurrentMotionMatchingAction(ActionToken))
	{
		RejectionReason = TEXT("stale or foreign action token");
	}
	else if (HandoffType == EMHGZMotionMatchingHandoffType::None)
	{
		RejectionReason = TEXT("notify has no HandoffType");
	}
	else if (!AllowedMotionMatchingHandoffTypes.Contains(HandoffType))
	{
		RejectionReason = TEXT("ability does not declare this HandoffType");
	}
	else if (!MotionMatchingPhaseState.bObservedAnyPhase)
	{
		RejectionReason = TEXT("no ActionRootMotionPhase was observed");
	}
	else if (MotionMatchingPhaseState.ActivePhaseDepth != 0)
	{
		RejectionReason = TEXT("ActionRootMotionPhase is still active");
	}
	else if (MotionMatchingPhaseState.OwningRootMotionPhaseDepth != 0)
	{
		RejectionReason = TEXT("Root Motion owner phase is still active");
	}
	else if (!IsMotionMatchingHandoffCommitComplete())
	{
		RejectionReason = TEXT("required ability Commit has not completed");
	}
	if (RejectionReason)
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[MotionMatchingHandoff] Rejected Ability=%s Type=%d Sequence=%d: %s."),
			*GetClass()->GetName(), static_cast<int32>(HandoffType), ActionToken.ActivationSequenceID,
			RejectionReason);
		return false;
	}

	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	// Handoff is deliberately after the final Root Motion phase. A competing
	// owner proves that the asset's safety frame was misplaced; do not end the
	// GA or release anybody else's owner as a fallback. An in-place phase may
	// legitimately have no owner at all.
	if (!Host)
	{
		UE_LOG(LogMHGZ, Warning, TEXT("[MotionMatchingHandoff] Rejected Ability=%s: missing RuntimeHost."),
			*GetClass()->GetName());
		return false;
	}
	if (Host->IsMontageRootMotionOwned() && !Host->IsMontageRootMotionOwnedBy(ActionToken))
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[MotionMatchingHandoff] Rejected Ability=%s: a competing Montage Root Motion owner exists."),
			*GetClass()->GetName());
		return false;
	}

	FVector2D RawMoveInput = FVector2D::ZeroVector;
	bool bHasRawMoveInput = false;
	GetMotionMatchingRawMoveInput(RawMoveInput, bHasRawMoveInput);
	FWeaponMotionMatchingHandoff Handoff;
	Handoff.Type = HandoffType;
	Handoff.RawMoveInput = RawMoveInput;
	Handoff.bHasRawMoveInputAtHandoff = bHasRawMoveInput;
	Handoff.bHadRawMoveInputInMobilePhase =
		MotionMatchingPhaseState.bObservedRawMoveInput;
	// The ActionRootMotionPhase can deliberately close several authored frames
	// before the Handoff Notify so UE always dispatches NotifyEnd first. The
	// Handoff still samples the physical stick at its exact authored frame: if
	// the player moved earlier in the mobile phase and is now neutral, that is
	// a real release even when no later NotifyTick had a chance to observe it.
	Handoff.bReleasedRawMoveInputInMobilePhase =
		MotionMatchingPhaseState.bReleasedRawMoveInput
		|| (Handoff.bHadRawMoveInputInMobilePhase && !bHasRawMoveInput);
	Handoff.bPendingStopAtHandoff = Handoff.bHadRawMoveInputInMobilePhase
		&& Handoff.bReleasedRawMoveInputInMobilePhase
		&& !Handoff.bHasRawMoveInputAtHandoff;
	Handoff.LastActiveRawMoveCruiseSpeedInMobilePhase =
		MotionMatchingPhaseState.LastActiveRawMoveCruiseSpeed;

	const bool bReleasesExistingOwner = Host->IsMontageRootMotionOwnedBy(ActionToken);
	if (bReleasesExistingOwner && !Host->ReleaseMontageRootMotion(ActionToken))
	{
		return false;
	}

	if (!Host->PublishMotionMatchingHandoff(ActionToken, MoveTemp(Handoff)))
	{
		// The Host can reject only before source-Action teardown. Restore the
		// exact owner so an invalid handoff never creates a root-motion gap.
		if (bReleasesExistingOwner)
		{
			Host->AcquireMontageRootMotion(ActionToken);
		}
		return false;
	}

	// The Handoff Notify is authored only in a no-function safety frame. Ending
	// the Action lets its AbilityTask blend out the old Montage after this exact
	// release; the published payload remains Host-owned until the AnimBP/Chooser
	// consumes its serial.
	RequestEndAction(EWeaponActionEndReason::Normal);
	return true;
}

bool UMHGZGameplayAbility::GetMotionMatchingRawMoveInput(
	FVector2D& OutRawMoveInput, bool& bOutHasRawMoveInput) const
{
	OutRawMoveInput = FVector2D::ZeroVector;
	bOutHasRawMoveInput = false;
	const AMHGZCharacter* Character = Cast<AMHGZCharacter>(
		GetAvatarActorFromActorInfo());
	if (!Character)
	{
		return false;
	}

	OutRawMoveInput = Character->GetRawMoveInput();
	bOutHasRawMoveInput = Character->HasRawMovementInput();
	return true;
}

// ── Spec 构造 ───────────────────────────────────────────────────────────────

FGameplayEffectSpecHandle UMHGZGameplayAbility::MakeStaminaCostSpec(float CostMagnitude) const
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC)
	{
		return FGameplayEffectSpecHandle();
	}

	FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(
		UMHGZStaminaCostGameplayEffect::StaticClass(),
		GetAbilityLevel(),
		ASC->MakeEffectContext());
	if (Spec.IsValid())
	{
		Spec.Data->SetSetByCallerMagnitude(
			UMHGZStaminaCostGameplayEffect::GetStaminaCostSetByCallerTag(),
			CostMagnitude);
	}
	return Spec;
}

FGameplayEffectSpecHandle UMHGZGameplayAbility::MakeCooldownSpec(float Duration) const
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC)
	{
		return FGameplayEffectSpecHandle();
	}

	FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(
		UMHGZCooldownGameplayEffect::StaticClass(),
		GetAbilityLevel(),
		ASC->MakeEffectContext());
	if (Spec.IsValid())
	{
		Spec.Data->SetDuration(Duration, true);
	}
	return Spec;
}

float UMHGZGameplayAbility::GetInstantStaminaCost(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo) const
{
	const UAbilitySystemComponent* ASC =
		ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (!ASC)
	{
		return 0.f;
	}

	const UMHGZAttributeSet* AttrSet = ASC->GetSet<UMHGZAttributeSet>();
	const float DeductionRate = AttrSet ? AttrSet->GetStaminaDeductionRate() : 1.f;
	return StaminaCost.GetValueAtLevel(GetAbilityLevel(Handle, ActorInfo)) * DeductionRate;
}
