// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZInsectGlaiveKinsectAbilities.h"

#include "AttributeSystem/Res_InsectGlaive.h"
#include "InsectGlaive/InsectGlaiveCombatConfig.h"
#include "InsectGlaive/Kinsect/InsectGlaiveKinsectData.h"
#include "InsectGlaive/Kinsect/Kinsect.h"
#include "MHGZCharacter.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

namespace
{
const FGameplayTag& GroundedTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Grounded"));
	return Tag;
}

const FGameplayTag& SheathedTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Sheathed"));
	return Tag;
}

const FGameplayTag& UnsheathedTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Unsheathed"));
	return Tag;
}

bool HasFrozenPose(const FWeaponInputSnapshot& Input, const FGameplayTag& WeaponPose)
{
	return Input.ContextTags.HasTagExact(GroundedTag())
		&& Input.ContextTags.HasTagExact(WeaponPose);
}

URes_InsectGlaive* ResolveResource(const UMHGZGameplayAbility* Ability)
{
	const UMHGZWeaponRuntimeHostComponent* Host = Ability ? Ability->GetRuntimeHost() : nullptr;
	return Host ? Cast<URes_InsectGlaive>(Host->GetResourceProvider()) : nullptr;
}

void FillStandardSingleHitRequest(FKinsectFlightRequest& Request,
	const FWeaponAbilityActivationContext& Activation,
	const UInsectGlaiveKinsectData& Data,
	const UInsectGlaiveCombatConfig& Config,
	float MotionValue)
{
	Request.RuntimeToken = Activation.RuntimeToken;
	Request.MaxDistance = Data.MaxFlightRange;
	Request.FlightSpeed = Data.FlightSpeed;
	Request.ArrivalRadius = Config.KinsectArrivalRadius;
	Request.DamageMode = EKinsectDamageMode::SingleHit;
	Request.ExtractMode = EKinsectExtractMode::FirstHitOnly;
	Request.PostFlightPolicy = EKinsectPostFlightPolicy::Hover;
	Request.MotionValue = MotionValue;
	Request.RehitInterval = 0.f;
	Request.FlightInstanceID = FGuid::NewGuid();
}
}

UMHGZSendKinsectAbility::UMHGZSendKinsectAbility()
{
	InputTag = FGameplayTag::RequestGameplayTag(TEXT("Input.Weapon.LTY"));
	StaminaCostPolicy = EAbilityStaminaCostPolicy::None;
}

URes_InsectGlaive* UMHGZSendKinsectAbility::GetIGResource() const
{
	return ResolveResource(this);
}

bool UMHGZSendKinsectAbility::BuildRequest(FKinsectFlightRequest& OutRequest) const
{
	URes_InsectGlaive* Resource = GetIGResource();
	const UInsectGlaiveKinsectData* Data = Resource ? Resource->GetKinsectData() : nullptr;
	const UInsectGlaiveCombatConfig* Config = Resource ? Resource->GetCombatConfig() : nullptr;
	AKinsect* Kinsect = Resource ? Resource->GetKinsectActor() : nullptr;
	const FWeaponAbilityActivationContext& Activation = GetWeaponActivationContext();
	const FWeaponInputSnapshot& Input = Activation.Input;
	if (!Resource || !Data || !Config || !Kinsect
		|| !HasFrozenPose(Input, UnsheathedTag())
		|| Input.Aim.Context != EWeaponAimSnapshotContext::Kinsect)
	{
		return false;
	}

	FillStandardSingleHitRequest(OutRequest, Activation, *Data, *Config,
		Config->SendKinsectMotionValue);
	if (Kinsect->GetState() == EKinsectState::Attached)
	{
		OutRequest.TrajectoryMode = EKinsectTrajectoryMode::AlongDirection;
		OutRequest.DirectionSnapshot = Input.Aim.Direction.GetSafeNormal();
	}
	else
	{
		OutRequest.TrajectoryMode = EKinsectTrajectoryMode::ToPoint;
		OutRequest.TargetPointSnapshot = Input.Aim.TargetPoint;
	}
	if ((OutRequest.TrajectoryMode == EKinsectTrajectoryMode::AlongDirection
			&& OutRequest.DirectionSnapshot.ContainsNaN())
		|| (OutRequest.TrajectoryMode == EKinsectTrajectoryMode::AlongDirection
			&& OutRequest.DirectionSnapshot.IsNearlyZero())
		|| (OutRequest.TrajectoryMode == EKinsectTrajectoryMode::ToPoint
			&& OutRequest.TargetPointSnapshot.ContainsNaN()))
	{
		return false;
	}
	return Resource->CanDeployKinsect(OutRequest);
}

bool UMHGZSendKinsectAbility::ValidateActionDependencies() const
{
	FKinsectFlightRequest Request;
	return BuildRequest(Request);
}

void UMHGZSendKinsectAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!IsActionActivationCommitted()) return;

	FKinsectFlightRequest Request;
	const bool bRequestReady = BuildRequest(Request);
	URes_InsectGlaive* Resource = GetIGResource();
	const bool bDeployed = bRequestReady && Resource && Resource->DeployKinsect(Request);
	RequestEndAction(bDeployed ? EWeaponActionEndReason::Normal : EWeaponActionEndReason::Cancelled);
}

UMHGZRecallKinsectAbility::UMHGZRecallKinsectAbility()
{
	InputTag = FGameplayTag::RequestGameplayTag(TEXT("Input.Weapon.LTB"));
	StaminaCostPolicy = EAbilityStaminaCostPolicy::None;
}

URes_InsectGlaive* UMHGZRecallKinsectAbility::GetIGResource() const
{
	return ResolveResource(this);
}

bool UMHGZRecallKinsectAbility::ValidateActionDependencies() const
{
	URes_InsectGlaive* Resource = GetIGResource();
	AKinsect* Kinsect = Resource ? Resource->GetKinsectActor() : nullptr;
	const FWeaponInputSnapshot& Input = GetWeaponActivationContext().Input;
	return Resource && Kinsect && HasFrozenPose(Input, UnsheathedTag())
		&& Kinsect->GetState() != EKinsectState::Attached;
}

void UMHGZRecallKinsectAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!IsActionActivationCommitted()) return;

	URes_InsectGlaive* Resource = GetIGResource();
	const bool bStarted = Resource && Resource->RecallKinsect();
	RequestEndAction(bStarted ? EWeaponActionEndReason::Normal : EWeaponActionEndReason::Cancelled);
}

UMHGZDrawAndSendKinsectAbility::UMHGZDrawAndSendKinsectAbility()
{
	InputTag = FGameplayTag::RequestGameplayTag(TEXT("Input.Weapon.RT"));
	StaminaCostPolicy = EAbilityStaminaCostPolicy::None;
}

URes_InsectGlaive* UMHGZDrawAndSendKinsectAbility::GetIGResource() const
{
	return ResolveResource(this);
}

bool UMHGZDrawAndSendKinsectAbility::BuildRequest(FKinsectFlightRequest& OutRequest) const
{
	URes_InsectGlaive* Resource = GetIGResource();
	const UInsectGlaiveKinsectData* Data = Resource ? Resource->GetKinsectData() : nullptr;
	const UInsectGlaiveCombatConfig* Config = Resource ? Resource->GetCombatConfig() : nullptr;
	const FWeaponAbilityActivationContext& Activation = GetWeaponActivationContext();
	const FWeaponInputSnapshot& Input = Activation.Input;
	if (!Resource || !Data || !Config || !HasFrozenPose(Input, SheathedTag()))
	{
		return false;
	}

	FillStandardSingleHitRequest(OutRequest, Activation, *Data, *Config,
		Config->DrawSendKinsectMotionValue);
	OutRequest.TrajectoryMode = EKinsectTrajectoryMode::AlongDirection;
	OutRequest.DirectionSnapshot = Input.ActorForward.GetSafeNormal();
	OutRequest.MaxDistance = Data->StraightFlightDistance;
	if (OutRequest.DirectionSnapshot.ContainsNaN()
		|| OutRequest.DirectionSnapshot.IsNearlyZero())
	{
		return false;
	}
	return Resource->CanDeployKinsect(OutRequest);
}

bool UMHGZDrawAndSendKinsectAbility::ValidateActionDependencies() const
{
	FKinsectFlightRequest Request;
	return BuildRequest(Request);
}

void UMHGZDrawAndSendKinsectAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!IsActionActivationCommitted()) return;

	FKinsectFlightRequest Request;
	const bool bRequestReady = BuildRequest(Request);
	URes_InsectGlaive* Resource = GetIGResource();
	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	bool bDeployed = false;
	if (bRequestReady && Resource && Host && Host->IsSheathed()
		&& Host->SetSheathed(false))
	{
		bDeployed = Resource->DeployKinsect(Request);
		if (!bDeployed)
		{
			Host->SetSheathed(true);
		}
	}
	if (bDeployed)
	{
		if (AMHGZCharacter* Character = Cast<AMHGZCharacter>(GetAvatarActorFromActorInfo()))
		{
			Character->ClearSprintHeld();
		}
	}
	RequestEndAction(bDeployed ? EWeaponActionEndReason::Normal : EWeaponActionEndReason::Cancelled);
}

UMHGZMarkKinsectTargetAbility::UMHGZMarkKinsectTargetAbility()
{
	InputTag = FGameplayTag::RequestGameplayTag(TEXT("Input.Weapon.LTRT"));
	StaminaCostPolicy = EAbilityStaminaCostPolicy::None;
}

URes_InsectGlaive* UMHGZMarkKinsectTargetAbility::GetIGResource() const
{
	return ResolveResource(this);
}

bool UMHGZMarkKinsectTargetAbility::ValidateActionDependencies() const
{
	const FWeaponInputSnapshot& Input = GetWeaponActivationContext().Input;
	const URes_InsectGlaive* Resource = GetIGResource();
	const UInsectGlaiveCombatConfig* Config = Resource ? Resource->GetCombatConfig() : nullptr;
	return Resource && Config && HasFrozenPose(Input, UnsheathedTag())
		&& Input.Aim.Context == EWeaponAimSnapshotContext::Kinsect
		&& !Input.Aim.Direction.ContainsNaN()
		&& !Input.Aim.Direction.GetSafeNormal().IsNearlyZero()
		&& Config->KinsectMarkProjectileSpeed > 0.f
		&& Config->KinsectMarkProjectileRadius > 0.f
		&& Config->KinsectMarkMaxDistance > 0.f
		&& Config->KinsectMarkProjectileLifetime > 0.f;
}

void UMHGZMarkKinsectTargetAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!IsActionActivationCommitted()) return;

	URes_InsectGlaive* Resource = GetIGResource();
	const bool bLaunched = Resource
		&& Resource->LaunchKinsectMark(GetWeaponActivationContext().Input.Aim);
	RequestEndAction(bLaunched ? EWeaponActionEndReason::Normal : EWeaponActionEndReason::Cancelled);
}
