// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZSheatheAbility.h"

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "AbilitySystemComponent.h"
#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "GameFramework/Character.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

namespace
{
const FGameplayTag& AttackingTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Attacking"));
	return Tag;
}

const FGameplayTag& HitstunTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Hitstun"));
	return Tag;
}

const FGameplayTag& KnockdownTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Knockdown"));
	return Tag;
}

const FGameplayTag& UnsheathedTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Unsheathed"));
	return Tag;
}

const FGameplayTag& SheathedTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Sheathed"));
	return Tag;
}

const FGameplayTag& GroundedTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Grounded"));
	return Tag;
}

const FGameplayTag& AerialTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Aerial"));
	return Tag;
}

const FGameplayTag& DeadTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Dead"));
	return Tag;
}

const FGameplayTag& SheathingTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Sheathing"));
	return Tag;
}

bool IsLiveSheatheStateValid(const UMHGZAbilitySystemComponent* ASC,
	const UMHGZWeaponRuntimeHostComponent* Host)
{
	return ASC && Host && Host->IsGrounded() && !Host->IsSheathed()
		&& ASC->HasMatchingGameplayTag(GroundedTag())
		&& !ASC->HasMatchingGameplayTag(AerialTag())
		&& !ASC->HasMatchingGameplayTag(DeadTag())
		&& !ASC->HasMatchingGameplayTag(AttackingTag())
		&& !ASC->HasMatchingGameplayTag(HitstunTag())
		&& !ASC->HasMatchingGameplayTag(KnockdownTag());
}
}

UMHGZSheatheAbility::UMHGZSheatheAbility()
{
	InputTag = FGameplayTag::RequestGameplayTag(TEXT("Input.Sheathe"));
	StaminaCostPolicy = EAbilityStaminaCostPolicy::None;
	AllowedMotionMatchingHandoffTypes.Add(EMHGZMotionMatchingHandoffType::SheatheMoveExit);
}

FName UMHGZSheatheAbility::SelectSectionName(const FWeaponInputSnapshot& Input) const
{
	return Input.Direction == EDirectionalInput::None
		? IdleSectionName
		: WalkSectionName;
}

bool UMHGZSheatheAbility::CanActivateAbility(
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
	return IsLiveSheatheStateValid(ASC, Host);
}

bool UMHGZSheatheAbility::ValidateActionDependencies() const
{
	const UMHGZAbilitySystemComponent* ASC = Cast<UMHGZAbilitySystemComponent>(
		GetAbilitySystemComponentFromActorInfo());
	const UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	const FWeaponInputSnapshot& Input = GetWeaponActivationContext().Input;
	return IsLiveSheatheStateValid(ASC, Host)
		&& !HasPlayerActionInputLock(ASC)
		&& Input.ContextTags.HasTagExact(GroundedTag())
		&& Input.ContextTags.HasTagExact(UnsheathedTag())
		&& !Input.ContextTags.HasTagExact(AerialTag())
		&& !Input.ContextTags.HasTagExact(SheathedTag())
		&& ValidateSheatheMontageDependencies();
}

bool UMHGZSheatheAbility::ValidateSheatheMontageDependencies() const
{
	const ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	const FName SectionName = SelectSectionName(GetWeaponActivationContext().Input);
	return Character && Character->GetMesh() && Character->GetMesh()->GetAnimInstance()
		&& SheatheMontage && !SectionName.IsNone()
		&& SheatheMontage->IsValidSectionName(SectionName);
}

void UMHGZSheatheAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!IsActionActivationCommitted())
	{
		return;
	}

	bEndingSheathe = false;
	bSheatheCommitted = false;
	bOwnsMontageRootMotion = false;
	IdleBlockMovementToken = FWeaponOwnedTagToken();
	SheathingToken = FWeaponOwnedTagToken();
	ActiveSheatheMontage = SheatheMontage;
	const FWeaponInputSnapshot& Input = GetWeaponActivationContext().Input;
	ActiveSectionName = SelectSectionName(Input);
	bUsingWalkSection = Input.Direction != EDirectionalInput::None;

	FGameplayTagContainer SheathingTags;
	SheathingTags.AddTag(SheathingTag());
	SheathingToken = AcquireActionTags(
		SheathingTags, FName(TEXT("SheatheActionLock")));
	if (!SheathingToken.IsValid())
	{
		RequestEndAction(EWeaponActionEndReason::Cancelled);
		return;
	}

	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	const bool bUseLegacyMontageRootMotionOwner = !bUsingWalkSection
		|| !bWalkUsesActionRootMotionPhase;
	if (!Host || (bUseLegacyMontageRootMotionOwner
		&& !Host->AcquireMontageRootMotion(GetActionToken())))
	{
		RequestEndAction(EWeaponActionEndReason::Cancelled);
		return;
	}
	bOwnsMontageRootMotion = bUseLegacyMontageRootMotionOwner;

	if (!bUsingWalkSection)
	{
		FGameplayTagContainer BlockMovement;
		BlockMovement.AddTag(FGameplayTag::RequestGameplayTag(
			TEXT("Combat.State.BlockMovement")));
		IdleBlockMovementToken = AcquireActionTags(
			BlockMovement, FName(TEXT("SheatheIdleMovement")));
		if (!IdleBlockMovementToken.IsValid())
		{
			RequestEndAction(EWeaponActionEndReason::Cancelled);
			return;
		}
	}

	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!Character
		|| !StartSheatheMontage(*Character, ActiveSheatheMontage, ActiveSectionName))
	{
		RequestEndAction(EWeaponActionEndReason::Cancelled);
	}
}

bool UMHGZSheatheAbility::StartSheatheMontage(ACharacter& Character,
	UAnimMontage* Montage, FName StartSection)
{
	UAnimInstance* AnimInstance = Character.GetMesh()
		? Character.GetMesh()->GetAnimInstance()
		: nullptr;
	if (!AnimInstance || !Montage)
	{
		return false;
	}

	MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, FName(TEXT("SheatheMontage")), Montage, 1.f, StartSection);
	if (!MontageTask)
	{
		return false;
	}

	MontageTask->OnCompleted.AddDynamic(this, &UMHGZSheatheAbility::OnMontageCompleted);
	MontageTask->OnInterrupted.AddDynamic(this, &UMHGZSheatheAbility::OnMontageInterrupted);
	MontageTask->OnCancelled.AddDynamic(this, &UMHGZSheatheAbility::OnMontageInterrupted);
	MontageTask->ReadyForActivation();

	FAnimMontageInstance* MontageInstance = AnimInstance->GetActiveInstanceForMontage(Montage);
	return MontageInstance
		&& RegisterMontageInstance(Character.GetMesh(), MontageInstance->GetInstanceID());
}

bool UMHGZSheatheAbility::CommitSheathe(const FWeaponActionToken& ActionToken)
{
	if (!IsActive() || bEndingSheathe || !IsActionActivationCommitted()
		|| ActionToken != GetActionToken())
	{
		return false;
	}

	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	if (!Host || !Host->IsTokenCurrent(ActionToken.RuntimeToken)
		|| !Host->IsMontageRootMotionOwnedBy(ActionToken))
	{
		return false;
	}

	if (bSheatheCommitted)
	{
		return Host->IsSheathed();
	}

	const bool bPoseApplied = Host->IsSheathed() || Host->SetSheathed(true);
	if (bPoseApplied)
	{
		bSheatheCommitted = true;
	}
	return bPoseApplied;
}

void UMHGZSheatheAbility::ReleaseMontageRootMotionOwnership()
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

void UMHGZSheatheAbility::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	if (bEndingSheathe)
	{
		return;
	}
	bEndingSheathe = true;
	ReleaseMontageRootMotionOwnership();
	MontageTask = nullptr;
	ActiveSheatheMontage = nullptr;
	ActiveSectionName = NAME_None;
	IdleBlockMovementToken = FWeaponOwnedTagToken();
	bUsingWalkSection = false;
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility,
		bWasCancelled);
	SheathingToken = FWeaponOwnedTagToken();
	bSheatheCommitted = false;
}

void UMHGZSheatheAbility::OnMontageCompleted()
{
	if (!IsActive() || bEndingSheathe)
	{
		return;
	}

	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	const bool bCommittedPoseCurrent = bSheatheCommitted && Host
		&& Host->IsTokenCurrent(GetActionToken().RuntimeToken)
		&& Host->IsSheathed();
	ReleaseMontageRootMotionOwnership();
	RequestEndAction(bCommittedPoseCurrent
		? EWeaponActionEndReason::Normal
		: EWeaponActionEndReason::Interrupted);
}

void UMHGZSheatheAbility::OnMontageInterrupted()
{
	if (IsActive() && !bEndingSheathe)
	{
		RequestEndAction(EWeaponActionEndReason::Interrupted);
	}
}
