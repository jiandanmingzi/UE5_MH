// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZInsectGlaiveGroundAbilities.h"
#include "MHGZGameplayTags.h"

#include "AttributeSystem/Res_InsectGlaive.h"
#include "MHGZAbilitySystemComponent.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"
#include "Components/PrimitiveComponent.h"
#include "InsectGlaive/Kinsect/InsectGlaiveKinsectData.h"
#include "InsectGlaive/Kinsect/Kinsect.h"

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
		&& ASC->HasMatchingGameplayTag(MHGZGameplayTags::Grounded())
		&& ASC->HasMatchingGameplayTag(MHGZGameplayTags::Unsheathed())
		&& !ASC->HasMatchingGameplayTag(MHGZGameplayTags::Aerial())
		&& !HasPlayerActionInputLock(ASC)
		&& Input.ContextTags.HasTagExact(MHGZGameplayTags::Grounded())
		&& Input.ContextTags.HasTagExact(MHGZGameplayTags::Unsheathed())
		&& !Input.ContextTags.HasTagExact(MHGZGameplayTags::Aerial());
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
		const UInsectGlaiveKinsectData* Data = Resource->GetKinsectData();
		const UInsectGlaiveCombatConfig* Config = Resource->GetCombatConfig();
		UPrimitiveComponent* Part = Hit.GetComponent();
		const UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
		if (!bSendKinsectOnMarkHit || !Data || !Config || !IsValid(Part)
			|| !IsActive() || !IsActionActivationCommitted()
			|| !Host || !Host->IsActionRegistered(GetActionToken())) return;

		FKinsectFlightRequest Request;
		Request.RuntimeToken = GetActionToken().RuntimeToken;
		Request.TrajectoryMode = EKinsectTrajectoryMode::ToPoint;
		Request.TargetPointSnapshot = Hit.ImpactPoint;
		Request.TargetComponent = Part;
		Request.TargetBoneName = Hit.BoneName;
		Request.TargetLocalPoint = Part->GetSocketTransform(Hit.BoneName).InverseTransformPosition(Hit.ImpactPoint);
		Request.bFollowTargetComponent = true;
		Request.bRequireHitzoneContactForArrival = true;
		Request.MaxDistance = Data->MaxFlightRange;
		Request.FlightSpeed = Data->FlightSpeed;
		Request.ArrivalRadius = Config->KinsectArrivalRadius;
		Request.MotionValue = Config->SendKinsectMotionValue;
		Request.DamageMode = EKinsectDamageMode::SingleHit;
		Request.ExtractMode = EKinsectExtractMode::FirstHitOnly;
		Request.PostFlightPolicy = EKinsectPostFlightPolicy::Hover;
		Request.FlightInstanceID = FGuid::NewGuid();
		Request.SourceAttackInstanceID = GetActivationAttackInstanceID();
		Request.ActionActivationSequenceID = GetActionToken().ActivationSequenceID;
		// 共用自动寻印的真实命中产粉策略；单发只结算一次，绕过且不占用普通CD。
		Request.PowderSpawnPolicy = EIGPowderSpawnPolicy::AutoMarkContact;
		Resource->DeployKinsect(Request); // 猎虫不可出动时，不撤销已成功的武器伤害和虫印。
	}
}
