// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZInsectGlaiveKinsectAbilities.h"

#include "AttributeSystem/Res_InsectGlaive.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "InsectGlaive/InsectGlaiveCombatConfig.h"
#include "InsectGlaive/Kinsect/InsectGlaiveKinsectData.h"
#include "InsectGlaive/Kinsect/Kinsect.h"
#include "MHGZAbilitySystemComponent.h"
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

bool IsLiveDrawStateValid(const UMHGZAbilitySystemComponent* ASC,
	const UMHGZWeaponRuntimeHostComponent* Host)
{
	return ASC && Host && Host->IsGrounded() && Host->IsSheathed()
		&& ASC->HasMatchingGameplayTag(GroundedTag())
		&& ASC->HasMatchingGameplayTag(SheathedTag())
		&& !ASC->HasMatchingGameplayTag(
			FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aerial")))
		&& !ASC->HasMatchingGameplayTag(UnsheathedTag())
		&& !ASC->HasMatchingGameplayTag(
			FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Attacking")))
		&& !ASC->HasMatchingGameplayTag(
			FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Dead")))
		&& !ASC->HasMatchingGameplayTag(
			FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Hitstun")))
		&& !ASC->HasMatchingGameplayTag(
			FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Knockdown")));
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
	return ValidateActionMontageDependencies() && BuildRequest(Request);
}

bool UMHGZSendKinsectAbility::ValidateActionMontageDependencies() const
{
	const ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	const USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	return ActionMontage && ActionMontagePlayRate > KINDA_SMALL_NUMBER
		&& Mesh && Mesh->GetAnimInstance();
}

void UMHGZSendKinsectAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	bCommandCommitted = false;
	PendingRequest = FKinsectFlightRequest();
	MontageTask = nullptr;
	ActiveActionMontage = nullptr;

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!IsActionActivationCommitted()) return;

	if (!BuildRequest(PendingRequest))
	{
		RequestEndAction(EWeaponActionEndReason::Cancelled);
		return;
	}

	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!Character || !StartActionMontage(*Character, ActionMontage))
	{
		RequestEndAction(EWeaponActionEndReason::Cancelled);
	}
}

bool UMHGZSendKinsectAbility::StartActionMontage(ACharacter& Character,
	UAnimMontage* Montage)
{
	USkeletalMeshComponent* Mesh = Character.GetMesh();
	UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (!Mesh || !AnimInstance || !Montage)
	{
		return false;
	}

	ActiveActionMontage = Montage;
	MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, FName(TEXT("SendKinsectMontage")), Montage, ActionMontagePlayRate);
	if (!MontageTask)
	{
		return false;
	}

	MontageTask->OnCompleted.AddDynamic(this,
		&UMHGZSendKinsectAbility::OnActionMontageCompleted);
	MontageTask->OnInterrupted.AddDynamic(this,
		&UMHGZSendKinsectAbility::OnActionMontageInterrupted);
	MontageTask->OnCancelled.AddDynamic(this,
		&UMHGZSendKinsectAbility::OnActionMontageInterrupted);
	MontageTask->ReadyForActivation();

	FAnimMontageInstance* MontageInstance = AnimInstance->GetActiveInstanceForMontage(Montage);
	return MontageInstance
		&& RegisterMontageInstance(Mesh, MontageInstance->GetInstanceID());
}

bool UMHGZSendKinsectAbility::CommitSendKinsect(const FWeaponActionToken& ActionToken)
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
	if (bCommandCommitted)
	{
		return true;
	}

	URes_InsectGlaive* Resource = GetIGResource();
	AKinsect* Kinsect = Resource ? Resource->GetKinsectActor() : nullptr;
	const bool bRequestStillValid = PendingRequest.RuntimeToken == ActionToken.RuntimeToken;
	if (!Resource || !Kinsect || !bRequestStillValid
		|| !Resource->CanDeployKinsect(PendingRequest)
		|| !Resource->DeployKinsect(PendingRequest))
	{
		RequestEndAction(EWeaponActionEndReason::Cancelled);
		return false;
	}

	bCommandCommitted = true;
	return true;
}

void UMHGZSendKinsectAbility::OnActionMontageCompleted()
{
	if (IsActive())
	{
		RequestEndAction(bCommandCommitted ? EWeaponActionEndReason::Normal
			: EWeaponActionEndReason::Cancelled);
	}
}

void UMHGZSendKinsectAbility::OnActionMontageInterrupted()
{
	if (IsActive())
	{
		RequestEndAction(EWeaponActionEndReason::Interrupted);
	}
}

void UMHGZSendKinsectAbility::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	// The GAS ability lifetime owns the task. Do not issue another montage stop
	// here; its completion/interruption delegate may already be ending this action.
	MontageTask = nullptr;
	ActiveActionMontage = nullptr;
	PendingRequest = FKinsectFlightRequest();
	bCommandCommitted = false;
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility,
		bWasCancelled);
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
	return ValidateActionMontageDependencies()
		&& Resource && Kinsect && HasFrozenPose(Input, UnsheathedTag())
		&& Kinsect->GetState() != EKinsectState::Attached;
}

bool UMHGZRecallKinsectAbility::ValidateActionMontageDependencies() const
{
	const ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	const USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	return ActionMontage && ActionMontagePlayRate > KINDA_SMALL_NUMBER
		&& Mesh && Mesh->GetAnimInstance();
}

void UMHGZRecallKinsectAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	bCommandCommitted = false;
	MontageTask = nullptr;
	ActiveActionMontage = nullptr;

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!IsActionActivationCommitted()) return;

	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!Character || !StartActionMontage(*Character, ActionMontage))
	{
		RequestEndAction(EWeaponActionEndReason::Cancelled);
	}
}

bool UMHGZRecallKinsectAbility::StartActionMontage(ACharacter& Character,
	UAnimMontage* Montage)
{
	USkeletalMeshComponent* Mesh = Character.GetMesh();
	UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (!Mesh || !AnimInstance || !Montage)
	{
		return false;
	}

	ActiveActionMontage = Montage;
	MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, FName(TEXT("RecallKinsectMontage")), Montage, ActionMontagePlayRate);
	if (!MontageTask)
	{
		return false;
	}

	MontageTask->OnCompleted.AddDynamic(this,
		&UMHGZRecallKinsectAbility::OnActionMontageCompleted);
	MontageTask->OnInterrupted.AddDynamic(this,
		&UMHGZRecallKinsectAbility::OnActionMontageInterrupted);
	MontageTask->OnCancelled.AddDynamic(this,
		&UMHGZRecallKinsectAbility::OnActionMontageInterrupted);
	MontageTask->ReadyForActivation();

	FAnimMontageInstance* MontageInstance = AnimInstance->GetActiveInstanceForMontage(Montage);
	return MontageInstance
		&& RegisterMontageInstance(Mesh, MontageInstance->GetInstanceID());
}

bool UMHGZRecallKinsectAbility::CommitRecallKinsect(const FWeaponActionToken& ActionToken)
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
	if (bCommandCommitted)
	{
		return true;
	}

	URes_InsectGlaive* Resource = GetIGResource();
	AKinsect* Kinsect = Resource ? Resource->GetKinsectActor() : nullptr;
	if (!Resource || !Kinsect || Kinsect->GetState() == EKinsectState::Attached
		|| !Resource->RecallKinsect())
	{
		RequestEndAction(EWeaponActionEndReason::Cancelled);
		return false;
	}

	bCommandCommitted = true;
	return true;
}

void UMHGZRecallKinsectAbility::OnActionMontageCompleted()
{
	if (IsActive())
	{
		RequestEndAction(bCommandCommitted ? EWeaponActionEndReason::Normal
			: EWeaponActionEndReason::Cancelled);
	}
}

void UMHGZRecallKinsectAbility::OnActionMontageInterrupted()
{
	if (IsActive())
	{
		RequestEndAction(EWeaponActionEndReason::Interrupted);
	}
}

void UMHGZRecallKinsectAbility::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	MontageTask = nullptr;
	ActiveActionMontage = nullptr;
	bCommandCommitted = false;
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility,
		bWasCancelled);
}

UMHGZDrawAndSendKinsectAbility::UMHGZDrawAndSendKinsectAbility()
{
	InputTag = FGameplayTag::RequestGameplayTag(TEXT("Input.Weapon.RT"));
	StaminaCostPolicy = EAbilityStaminaCostPolicy::None;
}

bool UMHGZDrawAndSendKinsectAbility::CanActivateAbility(
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
	return IsLiveDrawStateValid(ASC, ASC ? ASC->GetRuntimeHost() : nullptr);
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
	const UMHGZAbilitySystemComponent* ASC = Cast<UMHGZAbilitySystemComponent>(
		GetAbilitySystemComponentFromActorInfo());
	const FWeaponInputSnapshot& Input = GetWeaponActivationContext().Input;
	return ValidateActionMontageDependencies()
		&& IsLiveDrawStateValid(ASC, ASC ? ASC->GetRuntimeHost() : nullptr)
		&& !HasPlayerActionInputLock(ASC)
		&& HasFrozenPose(Input, SheathedTag())
		&& BuildRequest(Request);
}

bool UMHGZDrawAndSendKinsectAbility::ValidateActionMontageDependencies() const
{
	const ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	const USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	return ActionMontage && ActionMontagePlayRate > KINDA_SMALL_NUMBER
		&& Mesh && Mesh->GetAnimInstance();
}

void UMHGZDrawAndSendKinsectAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	bDrawCommitted = false;
	bCommandCommitted = false;
	PendingRequest = FKinsectFlightRequest();
	MontageTask = nullptr;
	ActiveActionMontage = nullptr;

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!IsActionActivationCommitted())
	{
		return;
	}

	if (!BuildRequest(PendingRequest))
	{
		RequestEndAction(EWeaponActionEndReason::Cancelled);
		return;
	}

	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!Character || !StartActionMontage(*Character, ActionMontage))
	{
		RequestEndAction(EWeaponActionEndReason::Cancelled);
	}
}

bool UMHGZDrawAndSendKinsectAbility::StartActionMontage(ACharacter& Character,
	UAnimMontage* Montage)
{
	USkeletalMeshComponent* Mesh = Character.GetMesh();
	UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (!Mesh || !AnimInstance || !Montage)
	{
		return false;
	}

	ActiveActionMontage = Montage;
	MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, FName(TEXT("DrawAndSendKinsectMontage")), Montage, ActionMontagePlayRate);
	if (!MontageTask)
	{
		return false;
	}

	MontageTask->OnCompleted.AddDynamic(this,
		&UMHGZDrawAndSendKinsectAbility::OnActionMontageCompleted);
	MontageTask->OnInterrupted.AddDynamic(this,
		&UMHGZDrawAndSendKinsectAbility::OnActionMontageInterrupted);
	MontageTask->OnCancelled.AddDynamic(this,
		&UMHGZDrawAndSendKinsectAbility::OnActionMontageInterrupted);
	MontageTask->ReadyForActivation();

	FAnimMontageInstance* MontageInstance = AnimInstance->GetActiveInstanceForMontage(Montage);
	return MontageInstance
		&& RegisterMontageInstance(Mesh, MontageInstance->GetInstanceID());
}

bool UMHGZDrawAndSendKinsectAbility::CommitDraw(const FWeaponActionToken& ActionToken)
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
	if (bDrawCommitted)
	{
		return !Host->IsSheathed();
	}

	if (Host->IsSheathed() && !Host->SetSheathed(false))
	{
		RequestEndAction(EWeaponActionEndReason::Cancelled);
		return false;
	}

	bDrawCommitted = true;
	if (AMHGZCharacter* Character = Cast<AMHGZCharacter>(GetAvatarActorFromActorInfo()))
	{
		Character->ClearSprintHeld();
	}
	return true;
}

bool UMHGZDrawAndSendKinsectAbility::CommitSendKinsect(
	const FWeaponActionToken& ActionToken)
{
	if (!IsActive() || !IsActionActivationCommitted()
		|| ActionToken != GetActionToken())
	{
		return false;
	}

	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	if (!Host || !Host->IsTokenCurrent(ActionToken.RuntimeToken) || !bDrawCommitted)
	{
		return false;
	}
	if (bCommandCommitted)
	{
		return true;
	}

	URes_InsectGlaive* Resource = GetIGResource();
	AKinsect* Kinsect = Resource ? Resource->GetKinsectActor() : nullptr;
	const bool bRequestStillValid = PendingRequest.RuntimeToken == ActionToken.RuntimeToken;
	if (!Resource || !Kinsect || !bRequestStillValid
		|| !Resource->CanDeployKinsect(PendingRequest)
		|| !Resource->DeployKinsect(PendingRequest))
	{
		RequestEndAction(EWeaponActionEndReason::Cancelled);
		return false;
	}

	bCommandCommitted = true;
	return true;
}

void UMHGZDrawAndSendKinsectAbility::OnActionMontageCompleted()
{
	if (IsActive())
	{
		RequestEndAction(bCommandCommitted ? EWeaponActionEndReason::Normal
			: EWeaponActionEndReason::Cancelled);
	}
}

void UMHGZDrawAndSendKinsectAbility::OnActionMontageInterrupted()
{
	if (IsActive())
	{
		RequestEndAction(EWeaponActionEndReason::Interrupted);
	}
}

void UMHGZDrawAndSendKinsectAbility::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	MontageTask = nullptr;
	ActiveActionMontage = nullptr;
	PendingRequest = FKinsectFlightRequest();
	bDrawCommitted = false;
	bCommandCommitted = false;
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility,
		bWasCancelled);
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
