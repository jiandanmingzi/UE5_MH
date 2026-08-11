// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZDodgeAbility.h"

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

UMHGZDodgeAbility::UMHGZDodgeAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerExecution;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalOnly;
	InputTag = FGameplayTag::RequestGameplayTag(TEXT("Input.Dodge"));
	StaminaCostPolicy = EAbilityStaminaCostPolicy::Instant;
	StaminaCost = FScalableFloat(25.f);
}

bool UMHGZDodgeAbility::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}
	const UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (!ASC) return false;
	if (ASC->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Hitstun")))
		|| ASC->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Knockdown"))))
	{
		return false;
	}
	return !ASC->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Attacking")))
		|| ASC->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.DodgeAcceptOpen")));
}

bool UMHGZDodgeAbility::ValidateActionDependencies() const
{
	const ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	return Character && Character->GetMesh() && Character->GetMesh()->GetAnimInstance()
		&& SelectDodgeMontage();
}

void UMHGZDodgeAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!IsActionActivationCommitted()) return;

	bEndingDodge = false;
	DodgeWindowTokens.Reset();
	CachedCollisionResponses.Reset();
	DodgeCapsule.Reset();

	FGameplayTagContainer BlockMovement;
	BlockMovement.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.BlockMovement")));
	AcquireActionTags(BlockMovement, FName(TEXT("DodgeMovement")));

	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	ActiveDodgeMontage = SelectDodgeMontage();
	UAnimInstance* AnimInstance = Character && Character->GetMesh()
		? Character->GetMesh()->GetAnimInstance() : nullptr;
	if (!Character || !ActiveDodgeMontage || !AnimInstance)
	{
		RequestEndAction(EWeaponActionEndReason::Cancelled);
		return;
	}

	MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, FName(TEXT("DodgeMontage")), ActiveDodgeMontage, 1.f);
	if (!MontageTask)
	{
		RequestEndAction(EWeaponActionEndReason::Cancelled);
		return;
	}
	MontageTask->OnCompleted.AddDynamic(this, &UMHGZDodgeAbility::OnMontageCompleted);
	MontageTask->OnInterrupted.AddDynamic(this, &UMHGZDodgeAbility::OnMontageInterrupted);
	MontageTask->OnCancelled.AddDynamic(this, &UMHGZDodgeAbility::OnMontageInterrupted);
	MontageTask->ReadyForActivation();

	FAnimMontageInstance* MontageInstance =
		AnimInstance->GetActiveInstanceForMontage(ActiveDodgeMontage);
	if (!MontageInstance
		|| !RegisterMontageInstance(Character->GetMesh(), MontageInstance->GetInstanceID()))
	{
		RequestEndAction(EWeaponActionEndReason::Interrupted);
	}
}

void UMHGZDodgeAbility::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	if (bEndingDodge) return;
	bEndingDodge = true;
	CloseAllDodgeWindows();
	MontageTask = nullptr;
	ActiveDodgeMontage = nullptr;
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

UAnimMontage* UMHGZDodgeAbility::SelectDodgeMontage() const
{
	const bool bSheathed = GetWeaponActivationContext().Input.ContextTags.HasTagExact(
		FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Sheathed")));
	const TMap<EDirectionalInput, TSoftObjectPtr<UAnimMontage>>& Montages =
		bSheathed ? SheathedDodgeMontages : UnsheathedDodgeMontages;
	const EDirectionalInput Direction = GetWeaponActivationContext().Input.Direction;
	if (const TSoftObjectPtr<UAnimMontage>* Found = Montages.Find(Direction))
	{
		return Found->LoadSynchronous();
	}
	if (const TSoftObjectPtr<UAnimMontage>* Fallback = Montages.Find(EDirectionalInput::None))
	{
		return Fallback->LoadSynchronous();
	}
	return nullptr;
}

bool UMHGZDodgeAbility::BeginDodgeWindow(FName NotifyEventID)
{
	if (!IsActionActivationCommitted() || DodgeWindowTokens.Contains(NotifyEventID)) return false;
	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	const FWeaponActionToken& Token = GetActionToken();
	if (!Capsule || !Host || !Token.IsValid()) return false;

	if (DodgeWindowTokens.IsEmpty())
	{
		DodgeCapsule = Capsule;
		for (const ECollisionChannel Channel : { ECC_GameTraceChannel1, ECC_GameTraceChannel2 })
		{
			CachedCollisionResponses.Add(Channel, Capsule->GetCollisionResponseToChannel(Channel));
			Capsule->SetCollisionResponseToChannel(Channel, ECR_Ignore);
		}
	}

	FGameplayTagContainer Tags;
	Tags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Invincible")));
	FWeaponOwnedTagToken WindowToken = Host->AcquireTags(EWeaponTagOwnerKind::NotifyWindow,
		Token.AbilityHandle, Token.ActivationSequenceID, NotifyEventID, Tags);
	if (!WindowToken.IsValid())
	{
		if (DodgeWindowTokens.IsEmpty()) RestoreDodgeCollisionResponses();
		return false;
	}
	DodgeWindowTokens.Add(NotifyEventID, WindowToken);
	return true;
}

void UMHGZDodgeAbility::EndDodgeWindow(FName NotifyEventID)
{
	FWeaponOwnedTagToken* WindowToken = DodgeWindowTokens.Find(NotifyEventID);
	if (!WindowToken) return;
	if (UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost())
	{
		Host->ReleaseTags(*WindowToken);
	}
	DodgeWindowTokens.Remove(NotifyEventID);
	if (DodgeWindowTokens.IsEmpty()) RestoreDodgeCollisionResponses();
}

void UMHGZDodgeAbility::CloseAllDodgeWindows()
{
	if (UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost())
	{
		for (TPair<FName, FWeaponOwnedTagToken>& Pair : DodgeWindowTokens)
		{
			Host->ReleaseTags(Pair.Value);
		}
	}
	DodgeWindowTokens.Reset();
	RestoreDodgeCollisionResponses();
}

void UMHGZDodgeAbility::RestoreDodgeCollisionResponses()
{
	if (UCapsuleComponent* Capsule = DodgeCapsule.Get())
	{
		for (const TPair<TEnumAsByte<ECollisionChannel>, ECollisionResponse>& Pair
			: CachedCollisionResponses)
		{
			Capsule->SetCollisionResponseToChannel(Pair.Key, Pair.Value);
		}
	}
	CachedCollisionResponses.Reset();
	DodgeCapsule.Reset();
}

void UMHGZDodgeAbility::OnMontageCompleted()
{
	if (IsActive() && !bEndingDodge)
	{
		RequestEndAction(EWeaponActionEndReason::Normal);
	}
}

void UMHGZDodgeAbility::OnMontageInterrupted()
{
	if (IsActive() && !bEndingDodge)
	{
		RequestEndAction(EWeaponActionEndReason::Interrupted);
	}
}
