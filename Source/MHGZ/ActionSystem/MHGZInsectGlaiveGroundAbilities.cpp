// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZInsectGlaiveGroundAbilities.h"

#include "AttributeSystem/Res_InsectGlaive.h"
#include "MHGZAbilitySystemComponent.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

namespace
{
const FGameplayTag& GroundedTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Grounded"));
	return Tag;
}

const FGameplayTag& UnsheathedTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Unsheathed"));
	return Tag;
}

const FGameplayTag& AerialTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Aerial"));
	return Tag;
}
}

UMHGZMarkSlashAbility::UMHGZMarkSlashAbility()
{
	InputTag = FGameplayTag::RequestGameplayTag(TEXT("Input.Weapon.RT"));
	StaminaCostPolicy = EAbilityStaminaCostPolicy::None;
}

void UMHGZMarkSlashAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	bEstablishedMarkThisActivation = false;
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}

bool UMHGZMarkSlashAbility::ValidateActionDependencies() const
{
	if (!Super::ValidateActionDependencies())
	{
		return false;
	}

	const UMHGZAbilitySystemComponent* ASC = Cast<UMHGZAbilitySystemComponent>(
		GetAbilitySystemComponentFromActorInfo());
	const UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	const FWeaponInputSnapshot& Input = GetWeaponActivationContext().Input;
	return GetIGResourceComponent() && ASC && Host
		&& Host->IsGrounded() && !Host->IsSheathed()
		&& ASC->HasMatchingGameplayTag(GroundedTag())
		&& ASC->HasMatchingGameplayTag(UnsheathedTag())
		&& !ASC->HasMatchingGameplayTag(AerialTag())
		&& !HasPlayerActionInputLock(ASC)
		&& Input.ContextTags.HasTagExact(GroundedTag())
		&& Input.ContextTags.HasTagExact(UnsheathedTag())
		&& !Input.ContextTags.HasTagExact(AerialTag());
}

#if WITH_EDITOR
EDataValidationResult UMHGZMarkSlashAbility::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult ParentResult = Super::IsDataValid(Context);
	if (MarkSegmentIndex < 0 || !AttackSegments.IsValidIndex(MarkSegmentIndex))
	{
		Context.AddError(FText::FromString(FString::Printf(
			TEXT("MarkSegmentIndex %d must name an existing AttackSegments entry."),
			MarkSegmentIndex)));
		return EDataValidationResult::Invalid;
	}
	return ParentResult == EDataValidationResult::NotValidated
		? EDataValidationResult::Valid
		: ParentResult;
}
#endif

void UMHGZMarkSlashAbility::HandleSuccessfulAttackDamage(const FHitResult& Hit,
	int32 SegmentIndex)
{
	if (SegmentIndex != MarkSegmentIndex || bEstablishedMarkThisActivation)
	{
		return;
	}

	if (URes_InsectGlaive* Resource = GetIGResourceComponent();
		Resource && Resource->SetKinsectMarkFromMeleeHit(Hit))
	{
		bEstablishedMarkThisActivation = true;
	}
}
