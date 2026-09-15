// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZAdvancingCounterAbility.h"

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "ActionSystem/AbilityTask_MHGZWeaponMovement.h"
#include "ActionSystem/MHGZIncomingHitResolverComponent.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "AttributeSystem/Res_InsectGlaive.h"
#include "InsectGlaive/InsectGlaiveCombatConfig.h"
#include "MHGZCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

namespace
{
const FGameplayTag& AdvancingCounterOpenTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.IG.AdvancingCounterOpen"));
	return Tag;
}
}

UMHGZAdvancingCounterAbility::UMHGZAdvancingCounterAbility()
{
	DanceVaultSequence = TSoftObjectPtr<UAnimSequenceBase>(FSoftObjectPath(
		TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_WuTa.AS_Unsh_WuTa")));
}

void UMHGZAdvancingCounterAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	bCounterSucceeded = false;
	bBeginFreeFallAfterEnd = false;
	bIsEndingCounterAbility = false;
	AdvancingCounterVaultTask = nullptr;
	AdvancingCounterVaultMontageTask = nullptr;
	CounterWindows.Reset();
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}

void UMHGZAdvancingCounterAbility::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	if (bIsEndingCounterAbility)
	{
		return;
	}
	bIsEndingCounterAbility = true;
	CloseAllAdvancingCounterWindows();
	if (AdvancingCounterVaultMontageTask)
	{
		AdvancingCounterVaultMontageTask->EndTask();
		AdvancingCounterVaultMontageTask = nullptr;
	}
	AdvancingCounterVaultTask = nullptr;
	ACharacter* EndingCharacter = ActorInfo && ActorInfo->AvatarActor.IsValid()
		? Cast<ACharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	const bool bStartFreeFall = bBeginFreeFallAfterEnd && !bWasCancelled
		&& EndingCharacter && EndingCharacter->GetCharacterMovement()
		&& EndingCharacter->GetCharacterMovement()->IsFalling();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility,
		bWasCancelled);
	if (bStartFreeFall)
	{
		if (UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost())
		{
			Host->BeginAerialFalling(true,
				FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aerial.Falling.IG_DanceVault")));
		}
	}
}

bool UMHGZAdvancingCounterAbility::ValidateActionDependencies() const
{
	if (!Super::ValidateActionDependencies() || !GetIGResourceComponent())
	{
		return false;
	}

	const AActor* Avatar = GetAvatarActorFromActorInfo();
	return Avatar && Avatar->FindComponentByClass<UMHGZIncomingHitResolverComponent>();
}

bool UMHGZAdvancingCounterAbility::BeginAdvancingCounterWindow(
	FName NotifyEventID, float TotalDuration)
{
	if (!IsActive() || bIsEndingCounterAbility || bCounterSucceeded
		|| !IsActionActivationCommitted() || NotifyEventID.IsNone()
		|| !FMath::IsFinite(TotalDuration) || TotalDuration <= 0.f)
	{
		return false;
	}
	if (CounterWindows.Contains(NotifyEventID))
	{
		return true;
	}

	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	const FWeaponActionToken& ActionToken = GetActionToken();
	AActor* Avatar = GetAvatarActorFromActorInfo();
	UMHGZIncomingHitResolverComponent* Resolver = Avatar
		? Avatar->FindComponentByClass<UMHGZIncomingHitResolverComponent>() : nullptr;
	if (!Host || !Resolver || !ActionToken.IsValid()
		|| !Host->IsTokenCurrent(ActionToken.RuntimeToken))
	{
		return false;
	}

	FGameplayTagContainer Tags;
	Tags.AddTag(AdvancingCounterOpenTag());
	FCounterWindowState State;
	State.TagToken = Host->AcquireTags(EWeaponTagOwnerKind::NotifyWindow,
		ActionToken.AbilityHandle, ActionToken.ActivationSequenceID, NotifyEventID, Tags);
	if (!State.TagToken.IsValid())
	{
		return false;
	}

	const TWeakObjectPtr<UMHGZAdvancingCounterAbility> WeakThis(this);
	const FWeaponActionToken ExpectedAction = ActionToken;
	State.ResolverTokenID = Resolver->RegisterInterceptorNative(ExpectedAction,
		CounterInterceptorPriority, TotalDuration + KINDA_SMALL_NUMBER,
		[WeakThis, ExpectedAction](const FIncomingHitContext& Context)
		{
			if (UMHGZAdvancingCounterAbility* Ability = WeakThis.Get())
			{
				return Ability->HandleIncomingHit(ExpectedAction, Context);
			}
			return EIncomingHitInterceptResult::Pass;
		});
	if (State.ResolverTokenID == 0)
	{
		Host->ReleaseTags(State.TagToken);
		return false;
	}

	CounterWindows.Add(NotifyEventID, MoveTemp(State));
	return true;
}

void UMHGZAdvancingCounterAbility::EndAdvancingCounterWindow(FName NotifyEventID)
{
	FCounterWindowState* State = CounterWindows.Find(NotifyEventID);
	if (!State)
	{
		return;
	}

	AActor* Avatar = GetAvatarActorFromActorInfo();
	if (UMHGZIncomingHitResolverComponent* Resolver = Avatar
		? Avatar->FindComponentByClass<UMHGZIncomingHitResolverComponent>() : nullptr)
	{
		Resolver->UnregisterInterceptor(State->ResolverTokenID);
	}
	if (UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost())
	{
		Host->ReleaseTags(State->TagToken);
	}
	CounterWindows.Remove(NotifyEventID);
}

bool UMHGZAdvancingCounterAbility::AddAdvancingCounterDanceStack()
{
	if (URes_InsectGlaive* Resource = GetIGResourceComponent())
	{
		return Resource->AddDanceStack(EIGDanceSource::AdvancingCounter);
	}
	return false;
}

EIncomingHitInterceptResult UMHGZAdvancingCounterAbility::HandleIncomingHit(
	const FWeaponActionToken& ExpectedAction, const FIncomingHitContext& Context)
{
	if (!Context.bCounterable || bCounterSucceeded || bIsEndingCounterAbility
		|| !IsActive() || !IsActionActivationCommitted()
		|| ExpectedAction != GetActionToken() || CounterWindows.IsEmpty())
	{
		return EIncomingHitInterceptResult::Pass;
	}

	// Only a successful Resource operation is allowed to consume the real incoming hit.
	// This keeps a broken/rebuilt runtime from accidentally turning into invulnerability.
	if (!AddAdvancingCounterDanceStack())
	{
		return EIncomingHitInterceptResult::Pass;
	}

	bCounterSucceeded = true;
	DisableCollision(); // Stop all later ground-attack segments before the montage is cancelled.
	CloseAllAdvancingCounterWindows();
	if (!SuspendAttackMontageForFollowup() || !StartAdvancingCounterVault())
	{
		// The hit has already been accepted and the dance stack was successfully
		// committed. A broken movement setup must end this exact action rather
		// than leaving a cancelled ground montage with a live ActionToken.
		RequestEndAction(EWeaponActionEndReason::Interrupted);
		return EIncomingHitInterceptResult::Consume;
	}
	OnAdvancingCounterSucceeded(Context);
	return EIncomingHitInterceptResult::Consume;
}

bool UMHGZAdvancingCounterAbility::StartAdvancingCounterVault()
{
	if (!IsActive() || bIsEndingCounterAbility || AdvancingCounterVaultTask
		|| !IsActionActivationCommitted())
	{
		return false;
	}

	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	const UInsectGlaiveCombatConfig* CombatConfig = Host
		? Cast<UInsectGlaiveCombatConfig>(Host->GetCurrentContext().CombatConfig) : nullptr;
	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!Host || !CombatConfig || !Character
		|| Host->IsMontageRootMotionOwnedBy(GetActionToken()))
	{
		return false;
	}

	FWeaponMovementRequest Request;
	Request.OwnerAction = GetActionToken();
	Request.Mode = EWeaponMovementMode::BallisticVault;
	Request.DirectionSnapshot = Character->GetActorForwardVector();
	Request.BallisticMode = CombatConfig->DanceVaultBallisticMode;
	Request.ApexHeight = CombatConfig->DanceVaultApexHeight;
	Request.Duration = CombatConfig->DanceVaultDuration;
	Request.LaunchVelocity = CombatConfig->DanceVaultLaunchVelocity;
	Request.RotationPolicy = EActionRotationPolicy::Locked;
	Request.CollisionPolicy = EMovementCollisionPolicy::StopOnBlockingHit;
	Request.CancelVelocityPolicy = EMovementCancelVelocityPolicy::PreserveVelocity;
	if (!Request.HasValidBallisticParameters())
	{
		return false;
	}

	UAbilityTask_MHGZWeaponMovement* Task =
		UAbilityTask_MHGZWeaponMovement::StartWeaponMovement(this,
			TEXT("AdvancingCounterVault"), Request);
	if (!Task)
	{
		return false;
	}

	AdvancingCounterVaultTask = Task;
	Task->OnFinished.AddDynamic(this,
		&UMHGZAdvancingCounterAbility::HandleAdvancingCounterVaultFinished);
	Task->ReadyForActivation();
	if (!Task->DidStartMovement())
	{
		AdvancingCounterVaultTask = nullptr;
		return false;
	}

	// AS_Unsh_WuTa is intentionally in-place.  Its visual playback may end
	// before or after the editable ballistic duration, but never owns CMC
	// movement or this Action's completion; the vault task/landing does.
	StartAdvancingCounterVaultVisual();
	return true;
}

bool UMHGZAdvancingCounterAbility::StartAdvancingCounterVaultVisual()
{
	if (!IsActive() || bIsEndingCounterAbility || AdvancingCounterVaultMontageTask
		|| DanceVaultSequence.IsNull())
	{
		return false;
	}

	UAnimSequenceBase* Sequence = DanceVaultSequence.LoadSynchronous();
	if (!Sequence)
	{
		return false;
	}

	UAnimMontage* DynamicMontage = UAnimMontage::CreateSlotAnimationAsDynamicMontage(
		Sequence, DanceVaultMontageSlot, DanceVaultBlendInTime, DanceVaultBlendOutTime,
		DanceVaultAnimationPlayRate, 1);
	if (!DynamicMontage)
	{
		return false;
	}

	AdvancingCounterVaultMontageTask =
		UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this, TEXT("AdvancingCounterVaultVisual"), DynamicMontage, 1.0f,
			NAME_None, true, 1.0f, 0.0f, true);
	if (!AdvancingCounterVaultMontageTask)
	{
		return false;
	}

	// Intentionally no completion delegates: a presentation-only sequence must
	// not end the live Action while BallisticVault is still airborne.
	AdvancingCounterVaultMontageTask->ReadyForActivation();
	return true;
}

void UMHGZAdvancingCounterAbility::HandleAdvancingCounterVaultFinished(
	const FWeaponMovementResult& MovementResult)
{
	AdvancingCounterVaultTask = nullptr;
	if (!IsActive() || bIsEndingCounterAbility)
	{
		return;
	}

	const EWeaponActionEndReason EndReason =
		(MovementResult.EndReason == EWeaponMovementEndReason::Completed
			|| MovementResult.EndReason == EWeaponMovementEndReason::Landed)
		? EWeaponActionEndReason::Normal
		: EWeaponActionEndReason::Interrupted;
	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	bBeginFreeFallAfterEnd = MovementResult.EndReason == EWeaponMovementEndReason::Completed
		&& Character && Character->GetCharacterMovement()
		&& Character->GetCharacterMovement()->IsFalling();
	RequestEndAction(EndReason);
}

void UMHGZAdvancingCounterAbility::CloseAllAdvancingCounterWindows()
{
	TArray<FName> WindowIDs;
	CounterWindows.GetKeys(WindowIDs);
	for (const FName WindowID : WindowIDs)
	{
		EndAdvancingCounterWindow(WindowID);
	}
}
