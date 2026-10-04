// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZM5KinsectSlashTestTypes.h"

#include "ActionSystem/AbilityTask_MHGZWeaponMovement.h"
#include "Movement/MHGZInstrumentedCharacterMovementComponent.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

namespace
{
FVector GKinsectSlashTestDirection = FVector::ForwardVector;
float GKinsectSlashTestDistance = 0.0f;
float GKinsectSlashTestDuration = 0.0f;
UCurveFloat* GKinsectSlashTestDistanceCurve = nullptr;
EWeaponMovementMode GKinsectSlashTestMode = EWeaponMovementMode::CurvedDirectional;
TOptional<FWeaponMovementRequest> GKinsectSlashFullTestRequest;
}

AMHGZM5KinsectSlashMovementTestCharacter::AMHGZM5KinsectSlashMovementTestCharacter(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<
		UMHGZInstrumentedCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
}

UMHGZM5KinsectSlashMovementTestAbility::UMHGZM5KinsectSlashMovementTestAbility()
{
	StaminaCostPolicy = EAbilityStaminaCostPolicy::None;
}

void UMHGZM5KinsectSlashMovementTestAbility::ConfigureForTest(
	const FVector& Direction, const float Distance, const float Duration,
	UCurveFloat* DistanceCurve, const EWeaponMovementMode Mode)
{
	GKinsectSlashTestDirection = Direction;
	GKinsectSlashTestDistance = Distance;
	GKinsectSlashTestDuration = Duration;
	GKinsectSlashTestDistanceCurve = DistanceCurve;
	GKinsectSlashTestMode = Mode;
}

void UMHGZM5KinsectSlashMovementTestAbility::ResetTestConfiguration()
{
	GKinsectSlashFullTestRequest.Reset();
	GKinsectSlashTestDirection = FVector::ForwardVector;
	GKinsectSlashTestDistance = 0.0f;
	GKinsectSlashTestDuration = 0.0f;
	GKinsectSlashTestDistanceCurve = nullptr;
	GKinsectSlashTestMode = EWeaponMovementMode::CurvedDirectional;
}

void UMHGZM5KinsectSlashMovementTestAbility::ConfigureRequestForTest(
	const FWeaponMovementRequest& Request)
{
	GKinsectSlashFullTestRequest = Request;
}

void UMHGZM5KinsectSlashMovementTestAbility::ActivateAbility(
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

	FWeaponMovementRequest Request;
	Request.OwnerAction = GetActionToken();
	Request.Mode = GKinsectSlashTestMode;
	Request.DirectionSnapshot = GKinsectSlashTestDirection;
	Request.MaxDistance = GKinsectSlashTestDistance;
	Request.Duration = GKinsectSlashTestDuration;
	Request.DistanceCurve = GKinsectSlashTestDistanceCurve;
	Request.CancelVelocityPolicy = EMovementCancelVelocityPolicy::ZeroVelocity;
	if (GKinsectSlashFullTestRequest.IsSet())
	{
		Request = GKinsectSlashFullTestRequest.GetValue();
		Request.OwnerAction = GetActionToken();
	}

	// Match StartDanceMovement: the committed action installs the Host profile
	// before the physical task validates gravity and applies its launch velocity.
	if (Request.Mode == EWeaponMovementMode::AuthoredAerialPhysics)
	{
		UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
		if (!Host || !Host->ApplyAerialFallingPhysicsOverride(GetActionToken(),
			Request.GravityMagnitude, 0.0f, Request.PlanarAcceleration))
		{
			RequestEndAction(EWeaponActionEndReason::Interrupted);
			return;
		}
	}

	MovementTask = UAbilityTask_MHGZWeaponMovement::StartWeaponMovement(
		this, TEXT("M5KinsectSlashMovementTest"), Request);
	if (!MovementTask)
	{
		return;
	}
	MovementTask->OnFinished.AddDynamic(this,
		&UMHGZM5KinsectSlashMovementTestAbility::HandleMovementFinished);
	MovementTask->ReadyForActivation();
}

void UMHGZM5KinsectSlashMovementTestAbility::HandleMovementFinished(
	const FWeaponMovementResult& Result)
{
	MovementResult = Result;
	bHasMovementResult = true;
}
