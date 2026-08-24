// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZDrawAttackAbility.h"

#include "MHGZAbilitySystemComponent.h"
#include "MHGZCharacter.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

namespace
{
FGameplayTag Tag(const TCHAR* Name)
{
	return FGameplayTag::RequestGameplayTag(Name);
}

bool IsLiveDrawStateValid(const UMHGZAbilitySystemComponent* ASC,
	const UMHGZWeaponRuntimeHostComponent* Host)
{
	return ASC && Host && Host->IsGrounded() && Host->IsSheathed()
		&& ASC->HasMatchingGameplayTag(Tag(TEXT("Combat.State.Grounded")))
		&& ASC->HasMatchingGameplayTag(Tag(TEXT("Combat.State.Sheathed")))
		&& !ASC->HasMatchingGameplayTag(Tag(TEXT("Combat.State.Aerial")))
		&& !ASC->HasMatchingGameplayTag(Tag(TEXT("Combat.State.Unsheathed")))
		&& !ASC->HasMatchingGameplayTag(Tag(TEXT("Combat.State.Attacking")))
		&& !ASC->HasMatchingGameplayTag(Tag(TEXT("Combat.State.Dead")))
		&& !ASC->HasMatchingGameplayTag(Tag(TEXT("Combat.State.Hitstun")))
		&& !ASC->HasMatchingGameplayTag(Tag(TEXT("Combat.State.Knockdown")));
}

bool HasFrozenDrawPose(const FWeaponInputSnapshot& Input)
{
	return Input.ContextTags.HasTagExact(Tag(TEXT("Combat.State.Grounded")))
		&& Input.ContextTags.HasTagExact(Tag(TEXT("Combat.State.Sheathed")))
		&& !Input.ContextTags.HasTagExact(Tag(TEXT("Combat.State.Aerial")))
		&& !Input.ContextTags.HasTagExact(Tag(TEXT("Combat.State.Unsheathed")));
}
}

UMHGZDrawAttackAbility::UMHGZDrawAttackAbility()
{
	InputTag = FGameplayTag::RequestGameplayTag(TEXT("Input.Weapon.Y"));
	StaminaCostPolicy = EAbilityStaminaCostPolicy::None;
}

bool UMHGZDrawAttackAbility::CanActivateAbility(
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

	const UMHGZAbilitySystemComponent* ASC = ActorInfo
		? Cast<UMHGZAbilitySystemComponent>(ActorInfo->AbilitySystemComponent.Get())
		: nullptr;
	const UMHGZWeaponRuntimeHostComponent* Host = ASC ? ASC->GetRuntimeHost() : nullptr;
	return IsLiveDrawStateValid(ASC, Host);
}

bool UMHGZDrawAttackAbility::ValidateActionDependencies() const
{
	const UMHGZAbilitySystemComponent* ASC = Cast<UMHGZAbilitySystemComponent>(
		GetAbilitySystemComponentFromActorInfo());
	const UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	const FWeaponInputSnapshot& Input = GetWeaponActivationContext().Input;
	return Super::ValidateActionDependencies()
		&& IsLiveDrawStateValid(ASC, Host)
		&& !HasPlayerActionInputLock(ASC)
		&& HasFrozenDrawPose(Input);
}

bool UMHGZDrawAttackAbility::PrepareAttackMontage()
{
	return AcquireDrawMontageRootMotion();
}

bool UMHGZDrawAttackAbility::AcquireDrawMontageRootMotion()
{
	if (bOwnsMontageRootMotion)
	{
		return true;
	}

	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	if (!Host || !Host->AcquireMontageRootMotion(GetActionToken()))
	{
		return false;
	}
	bOwnsMontageRootMotion = true;
	return true;
}

void UMHGZDrawAttackAbility::ReleaseDrawMontageRootMotion()
{
	if (!bOwnsMontageRootMotion)
	{
		return;
	}

	bOwnsMontageRootMotion = false;
	if (UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost())
	{
		Host->ReleaseMontageRootMotion(GetActionToken());
	}
}

void UMHGZDrawAttackAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	bDrawCommitted = false;
	bOwnsMontageRootMotion = false;
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}

bool UMHGZDrawAttackAbility::CommitDraw(const FWeaponActionToken& ActionToken)
{
	if (!IsActive() || !IsActionActivationCommitted()
		|| ActionToken != GetActionToken())
	{
		return false;
	}

	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	if (!Host || !Host->IsTokenCurrent(ActionToken.RuntimeToken))
	{
		return false;
	}
	if (!Host->IsMontageRootMotionOwnedBy(ActionToken))
	{
		return false;
	}

	if (bDrawCommitted)
	{
		return !Host->IsSheathed();
	}

	const bool bPoseApplied = !Host->IsSheathed() || Host->SetSheathed(false);
	if (!bPoseApplied)
	{
		return false;
	}

	bDrawCommitted = true;
	if (AMHGZCharacter* Character = Cast<AMHGZCharacter>(
		GetAvatarActorFromActorInfo()))
	{
		Character->ClearSprintHeld();
	}
	OnDrawCommitted();
	return true;
}

void UMHGZDrawAttackAbility::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	ReleaseDrawMontageRootMotion();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility,
		bWasCancelled);
	bDrawCommitted = false;
}
