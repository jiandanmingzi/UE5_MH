// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZPowderVortexAbility.h"

#include "ActionSystem/AnimNotify_IG_PowderGatherBegin.h"
#include "AttributeSystem/Res_InsectGlaive.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimMontage.h"
#include "InsectGlaive/InsectGlaiveCombatConfig.h"
#include "InsectGlaive/Kinsect/Kinsect.h"
#include "Misc/DataValidation.h"

namespace
{
const FGameplayTag& PowderVortexInputTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Input.Weapon.LTYB"));
	return Tag;
}

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

bool IsFiniteVector(const FVector& Value)
{
	return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y)
		&& FMath::IsFinite(Value.Z);
}
}

UMHGZPowderVortexAbility::UMHGZPowderVortexAbility()
{
	InputTag = PowderVortexInputTag();
	MaxCorrectionAngle = 0.0f;
	bBlockCharacterMovementDuringAction = false;
}

bool UMHGZPowderVortexAbility::ValidateActionDependencies() const
{
	if (!Super::ValidateActionDependencies() || !AttackSegments.IsEmpty()
		|| !InputTag.MatchesTagExact(PowderVortexInputTag())
		|| !GetWeaponActivationContext().Input.ResolvedInputTag.MatchesTagExact(
			PowderVortexInputTag())
		|| !ValidatePowderVortexRuntimeDependencies()
		|| !AttackMontage || !AttackMontage->GetSkeleton()
		|| AttackMontage->GetPlayLength() <= 0.0f)
	{
		return false;
	}

	int32 GatherNotifyCount = 0;
	for (const FAnimNotifyEvent& Event : AttackMontage->Notifies)
	{
		if (Event.Notify
			&& Event.Notify->IsA(UAnimNotify_IG_PowderGatherBegin::StaticClass()))
		{
			++GatherNotifyCount;
		}
	}
	return GatherNotifyCount == 1;
}

bool UMHGZPowderVortexAbility::ValidatePowderVortexRuntimeDependencies() const
{
	const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	const URes_InsectGlaive* Resource = GetIGResourceComponent();
	const UInsectGlaiveCombatConfig* Config = Resource
		? Resource->GetCombatConfig() : nullptr;
	const AKinsect* Kinsect = Resource ? Resource->GetKinsectActor() : nullptr;
	const FWeaponAimSnapshot& Aim = GetWeaponActivationContext().Input.Aim;
	return ASC && ASC->HasMatchingGameplayTag(GroundedTag())
		&& ASC->HasMatchingGameplayTag(UnsheathedTag())
		&& Resource && Config && Kinsect
		&& Kinsect->GetState() != EKinsectState::ActionAttached
		&& Aim.Context == EWeaponAimSnapshotContext::Kinsect
		&& IsFiniteVector(Aim.TargetPoint)
		&& FMath::IsFinite(Config->PowderGatherRadius)
		&& Config->PowderGatherRadius > 0.0f
		&& FMath::IsFinite(Config->PowderVortexDelay)
		&& Config->PowderVortexDelay > 0.0f
		&& FMath::IsFinite(Config->PowderVortexDamageAt1)
		&& Config->PowderVortexDamageAt1 > 0.0f
		&& FMath::IsFinite(Config->PowderVortexDamageAt5)
		&& Config->PowderVortexDamageAt5 > 0.0f
		&& FMath::IsFinite(Config->PowderVortexDamageAt10)
		&& Config->PowderVortexDamageAt10 > 0.0f
		&& Config->PowderVortexHitCueTag.IsValid()
		&& Config->PowderVortexExplosionCueTag.IsValid();
}

bool UMHGZPowderVortexAbility::PrepareAttackMontage()
{
	if (!Super::PrepareAttackMontage())
	{
		return false;
	}
	bGatherNotifyHandled = false;
	bKinsectCommandSubmitted = false;
	return true;
}

bool UMHGZPowderVortexAbility::HandlePowderGatherBegin(
	const FWeaponActionToken& ActionToken)
{
	if (!IsActive() || !IsActionActivationCommitted()
		|| ActionToken != GetActionToken() || bGatherNotifyHandled
		|| bKinsectCommandSubmitted || !ValidatePowderVortexRuntimeDependencies())
	{
		return false;
	}
	bGatherNotifyHandled = true;
	URes_InsectGlaive* Resource = GetIGResourceComponent();
	if (!Resource || !Resource->StartPowderVortex(ActionToken,
		GetWeaponActivationContext().Input.Aim))
	{
		return false;
	}
	bKinsectCommandSubmitted = true;
	return true;
}

void UMHGZPowderVortexAbility::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const bool bReplicateEndAbility, const bool bWasCancelled)
{
	// A submitted flight/gather transaction intentionally outlives this GA and Montage.
	bGatherNotifyHandled = false;
	bKinsectCommandSubmitted = false;
	Super::EndAbility(Handle, ActorInfo, ActivationInfo,
		bReplicateEndAbility, bWasCancelled);
}

#if WITH_EDITOR
EDataValidationResult UMHGZPowderVortexAbility::IsDataValid(
	FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	auto AddError = [&Context, &Result](const TCHAR* Message)
	{
		Context.AddError(FText::FromString(Message));
		Result = EDataValidationResult::Invalid;
	};
	if (!AttackSegments.IsEmpty())
	{
		AddError(TEXT("Powder Vortex must use no AttackSegments; its two damage stages are Resource-owned."));
	}
	if (!InputTag.MatchesTagExact(PowderVortexInputTag()))
	{
		AddError(TEXT("Powder Vortex InputTag must be Input.Weapon.LTYB."));
	}
	if (!FMath::IsNearlyZero(MaxCorrectionAngle))
	{
		AddError(TEXT("Powder Vortex must not apply activation direction correction."));
	}
	if (bBlockCharacterMovementDuringAction)
	{
		AddError(TEXT("Powder Vortex must allow character locomotion while the action montage is active."));
	}
	if (!AttackMontage)
	{
		AddError(TEXT("Powder Vortex requires its persistent AttackMontage asset."));
		return EDataValidationResult::Invalid;
	}
	int32 GatherNotifyCount = 0;
	for (const FAnimNotifyEvent& Event : AttackMontage->Notifies)
	{
		if (Event.Notify
			&& Event.Notify->IsA(UAnimNotify_IG_PowderGatherBegin::StaticClass()))
		{
			++GatherNotifyCount;
		}
	}
	if (GatherNotifyCount != 1)
	{
		AddError(TEXT("Powder Vortex Montage must contain exactly one IG Powder Gather Begin point notify."));
	}
	return Result;
}
#endif
