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
	bPlayLandedPresentation = false;
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
	// 触地类收尾：JumpForce 的抛物线在 f=1 回到起跳高度，所以本动作没有、
	// 也不该有自由落体阶段（Rise 实录里舞踏是单一动作 id 覆盖升+降+触地，
	// 动画与移动同长同终）。落地姿势直接认领，不经过 AerialFalling。
	const bool bPlayLanding = bPlayLandedPresentation && !bWasCancelled;
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility,
		bWasCancelled);
	if (bPlayLanding)
	{
		if (UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost())
		{
			Host->PlayAerialLandingPresentation();
		}
	}
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
	// ApexHeightAndDuration 模式不会从别的字段推导水平位移；不设这一项，
	// JumpForce 的 Distance 为 0，落点会比 Rise 更贴脚下。
	Request.MaxDistance = CombatConfig->DanceVaultDistance;
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

UMHGZAdvancingCounterAbility::EVaultExit UMHGZAdvancingCounterAbility::ResolveVaultExit(
	const EWeaponMovementEndReason Reason, const bool bCmcIsFalling)
{
	switch (Reason)
	{
	case EWeaponMovementEndReason::Landed:
		// 弧线抵达地面。刻意不看 CMC 模式 —— 回调落在 ProcessLanded 内、
		// SetPostLandedPhysics 之前，此时 IsFalling() 仍为 true，用它当判据
		// 会把每一次触地都判成「还在空中」。
		return EVaultExit::LandedPresentation;
	case EWeaponMovementEndReason::Completed:
		// 弧线在空中跑完，本体交给 CMC 继续积分。若 CMC 已经落地，落地姿势
		// 同样归本次 Action —— 否则这次触地没有任何人认领。
		return bCmcIsFalling ? EVaultExit::FreeFall : EVaultExit::LandedPresentation;
	default:
		// BlockingHit / Interrupted / Cancelled / Failed / Death / WeaponChanged /
		// RuntimeShutdown / HitHitzone：没有抵达地面，不交接任何东西。
		return EVaultExit::None;
	}
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
	const ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	const UCharacterMovementComponent* Movement = Character
		? Character->GetCharacterMovement() : nullptr;
	const EVaultExit Exit = ResolveVaultExit(MovementResult.EndReason,
		Movement && Movement->IsFalling());
	bBeginFreeFallAfterEnd = Exit == EVaultExit::FreeFall;
	bPlayLandedPresentation = Exit == EVaultExit::LandedPresentation;
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
