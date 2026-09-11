// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZAdvancingChargeAbility.h"

#include "InputSystem/MHGZWeaponInputRouterComponent.h"
#include "MHGZCharacter.h"
#include "MHGZPlayerController.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "TimerManager.h"

UMHGZAdvancingChargeAbility::UMHGZAdvancingChargeAbility()
{
	ChargeReleaseControls.Add(FGameplayTag::RequestGameplayTag(TEXT("Input.Weapon.Y")));
	ChargeReleaseControls.Add(FGameplayTag::RequestGameplayTag(TEXT("Input.Weapon.B")));
}

void UMHGZAdvancingChargeAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	bChargeStarted = false;
	bChargeRouteConfigured = false;
	bReleaseStateCheckScheduled = false;
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}

void UMHGZAdvancingChargeAbility::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	bReleaseStateCheckScheduled = false;
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

bool UMHGZAdvancingChargeAbility::StartAttackMontage(
	ACharacter& Character, UAnimMontage* Montage, FName StartSection)
{
	return Super::StartAttackMontage(Character, Montage, StartSection)
		&& ConfigureChargeRoute();
}

void UMHGZAdvancingChargeAbility::HandleInputReleased(const FWeaponInputSnapshot& Snapshot)
{
	if (!IsActive() || !IsActionActivationCommitted()
		|| Snapshot.ResolvedInputTag != InputTag)
	{
		return;
	}

	// Router 在派发 Completed 后才将物理键从 HeldControls 移除；延至下一 Tick
	// 才能可靠地区分“松开一个组合键”和“所有蓄力键都已松开”。
	ScheduleChargeReleaseStateCheck();
}

void UMHGZAdvancingChargeAbility::BeginAdvancingCharge()
{
	if (!IsActive() || !IsActionActivationCommitted() || bChargeStarted)
	{
		return;
	}
	bChargeStarted = true;
	ProceedToAttackIfAllReleased();
}

bool UMHGZAdvancingChargeAbility::ConfigureChargeRoute()
{
	if (bChargeRouteConfigured)
	{
		return true;
	}
	UAnimInstance* AnimInstance = GetAttackAnimInstance();
	UAnimMontage* Montage = AttackMontage;
	if (!AnimInstance || !Montage || !Montage->IsValidSectionName(ChargeEntrySection)
		|| !Montage->IsValidSectionName(ChargeSection)
		|| !Montage->IsValidSectionName(AttackSection))
	{
		return false;
	}
	AnimInstance->Montage_SetNextSection(ChargeEntrySection, ChargeSection, Montage);
	bChargeRouteConfigured = true;
	return true;
}

bool UMHGZAdvancingChargeAbility::AreAllChargeReleaseControlsReleased() const
{
	const AMHGZCharacter* Character = Cast<AMHGZCharacter>(GetAvatarActorFromActorInfo());
	const AMHGZPlayerController* Controller = Character
		? Cast<AMHGZPlayerController>(Character->GetController()) : nullptr;
	const UMHGZWeaponInputRouterComponent* Router = Controller
		? Controller->GetWeaponInputRouter() : nullptr;
	if (!Router || ChargeReleaseControls.IsEmpty())
	{
		return true;
	}

	for (const FGameplayTag& Control : ChargeReleaseControls)
	{
		if (Router->IsPhysicalInputHeld(Control))
		{
			return false;
		}
	}
	return true;
}

UAnimInstance* UMHGZAdvancingChargeAbility::GetAttackAnimInstance() const
{
	const AMHGZCharacter* Character = Cast<AMHGZCharacter>(GetAvatarActorFromActorInfo());
	return Character && Character->GetMesh()
		? Character->GetMesh()->GetAnimInstance() : nullptr;
}

void UMHGZAdvancingChargeAbility::ScheduleChargeReleaseStateCheck()
{
	if (bReleaseStateCheckScheduled)
	{
		return;
	}
	bReleaseStateCheckScheduled = true;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateUObject(this,
				&UMHGZAdvancingChargeAbility::ResolveChargeReleaseStateCheck));
	}
	else
	{
		bReleaseStateCheckScheduled = false;
	}
}

void UMHGZAdvancingChargeAbility::ResolveChargeReleaseStateCheck()
{
	bReleaseStateCheckScheduled = false;
	ProceedToAttackIfAllReleased();
}

void UMHGZAdvancingChargeAbility::ProceedToAttackIfAllReleased()
{
	if (!IsActive() || !IsActionActivationCommitted()
		|| !AreAllChargeReleaseControlsReleased())
	{
		return;
	}

	if (UAnimInstance* AnimInstance = GetAttackAnimInstance())
	{
		const FName CurrentSection = AnimInstance->Montage_GetCurrentSection(AttackMontage);
		if (CurrentSection == ChargeEntrySection && bChargeRouteConfigured)
		{
			AnimInstance->Montage_SetNextSection(ChargeEntrySection, AttackSection, AttackMontage);
		}
		else if (CurrentSection == ChargeSection)
		{
			AnimInstance->Montage_JumpToSection(AttackSection, AttackMontage);
		}
	}
}
