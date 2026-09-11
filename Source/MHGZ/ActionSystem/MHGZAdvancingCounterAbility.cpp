// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZAdvancingCounterAbility.h"

#include "ActionSystem/MHGZIncomingHitResolverComponent.h"
#include "AttributeSystem/Res_InsectGlaive.h"
#include "MHGZCharacter.h"
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
}

void UMHGZAdvancingCounterAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	bCounterSucceeded = false;
	bIsEndingCounterAbility = false;
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
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility,
		bWasCancelled);
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
	OnAdvancingCounterSucceeded(Context);
	RequestEndAction(EWeaponActionEndReason::Superseded);
	return EIncomingHitInterceptResult::Consume;
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
