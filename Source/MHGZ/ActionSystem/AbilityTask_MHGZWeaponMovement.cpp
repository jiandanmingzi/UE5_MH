// Copyright MHGZ Project. All Rights Reserved.

#include "ActionSystem/AbilityTask_MHGZWeaponMovement.h"

#include "ActionSystem/MHGZGameplayAbility.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/RootMotionSource.h"
#include "MHGZ.h"
#include "MHGZCharacter.h"
#include "MotionWarpingComponent.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

namespace
{
constexpr uint16 InvalidRootMotionSourceID = static_cast<uint16>(ERootMotionSourceID::Invalid);

FVector GetPlanarDirection(const FVector& Candidate, const ACharacter& Character)
{
	FVector Direction = Candidate;
	Direction.Z = 0.0f;
	if (Direction.Normalize())
	{
		return Direction;
	}

	Direction = Character.GetActorForwardVector();
	Direction.Z = 0.0f;
	return Direction.GetSafeNormal();
}

void ConfigureFinishVelocity(FRootMotionSource& Source,
	const EMovementCancelVelocityPolicy Policy)
{
	// A normal RootMotionSource completion leaves its authored final velocity
	// intact. Explicit cancellation is handled by the task below; this setting
	// makes the natural CMC handoff obey the same intent.
	if (Policy == EMovementCancelVelocityPolicy::ZeroVelocity)
	{
		Source.FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::SetVelocity;
		Source.FinishVelocityParams.SetVelocity = FVector::ZeroVector;
	}
	else
	{
		Source.FinishVelocityParams.Mode =
			ERootMotionFinishVelocityMode::MaintainLastRootMotionVelocity;
	}
}

void ConfigureCommonSource(FRootMotionSource& Source, const FName SourceName,
	const float Duration, const ERootMotionAccumulateMode AccumulateMode,
	const EMovementCancelVelocityPolicy FinishPolicy)
{
	Source.InstanceName = SourceName;
	Source.Priority = 700;
	Source.AccumulateMode = AccumulateMode;
	Source.Duration = Duration;
	ConfigureFinishVelocity(Source, FinishPolicy);
}
}

UAbilityTask_MHGZWeaponMovement::UAbilityTask_MHGZWeaponMovement(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bTickingTask = true;
}

UAbilityTask_MHGZWeaponMovement* UAbilityTask_MHGZWeaponMovement::StartWeaponMovement(
	UMHGZGameplayAbility* OwningAbility, const FName TaskInstanceName,
	const FWeaponMovementRequest& Request)
{
	if (!OwningAbility)
	{
		return nullptr;
	}

	UAbilityTask_MHGZWeaponMovement* Task =
		NewAbilityTask<UAbilityTask_MHGZWeaponMovement>(OwningAbility, TaskInstanceName);
	Task->MovementAbility = OwningAbility;
	Task->MovementRequest = Request;
	return Task;
}

void UAbilityTask_MHGZWeaponMovement::Activate()
{
	UMHGZGameplayAbility* OwningWeaponAbility = MovementAbility.Get();
	if (!OwningWeaponAbility || !OwningWeaponAbility->IsActionActivationCommitted()
		|| MovementRequest.OwnerAction != OwningWeaponAbility->GetActionToken())
	{
		FinishMovement(EWeaponMovementEndReason::Failed);
		return;
	}

	ACharacter* AvatarCharacter = Cast<ACharacter>(GetAvatarActor());
	UMHGZWeaponRuntimeHostComponent* Host = OwningWeaponAbility->GetRuntimeHost();
	UCharacterMovementComponent* CMC = AvatarCharacter
		? AvatarCharacter->GetCharacterMovement() : nullptr;
	if (!AvatarCharacter || !Host || !CMC || !ValidateRequest())
	{
		FinishMovement(EWeaponMovementEndReason::Failed);
		return;
	}

	Character = AvatarCharacter;
	MovementComponent = CMC;
	RuntimeHost = Host;
	StartLocation = AvatarCharacter->GetActorLocation();
	LastLocation = StartLocation;

	FName AllocatedWarpTargetName;
	if (!Host->AcquireActionMovement(MovementRequest.OwnerAction, AllocatedWarpTargetName))
	{
		FinishMovement(EWeaponMovementEndReason::Failed);
		return;
	}
	MovementRequest.WarpTargetName = AllocatedWarpTargetName;

	if (!ApplyMovementSource())
	{
		FinishMovement(EWeaponMovementEndReason::Failed);
		return;
	}

	if (AMHGZCharacter* MHGZCharacter = Cast<AMHGZCharacter>(AvatarCharacter))
	{
		if (UMotionWarpingComponent* MotionWarping =
			MHGZCharacter->GetMotionWarpingComponent())
		{
			MotionWarping->AddOrUpdateWarpTargetFromLocation(
				MovementRequest.WarpTargetName, ExpectedDestination);
		}
	}

	AvatarCharacter->LandedDelegate.AddDynamic(this,
		&UAbilityTask_MHGZWeaponMovement::HandleLanded);
	bBoundLandedDelegate = true;
	if (UCapsuleComponent* Capsule = AvatarCharacter->GetCapsuleComponent())
	{
		Capsule->OnComponentHit.AddDynamic(this,
			&UAbilityTask_MHGZWeaponMovement::HandleCapsuleHit);
		bBoundCapsuleHit = true;
	}

	bStarted = true;
	SetWaitingOnAvatar();
}

void UAbilityTask_MHGZWeaponMovement::TickTask(const float DeltaTime)
{
	Super::TickTask(DeltaTime);
	if (!bStarted || bFinished)
	{
		return;
	}

	UMHGZWeaponRuntimeHostComponent* Host = RuntimeHost.Get();
	if (!Host || !Host->IsActionMovementOwnedBy(MovementRequest.OwnerAction))
	{
		FinishMovement(EWeaponMovementEndReason::Interrupted);
		return;
	}

	UCharacterMovementComponent* CMC = MovementComponent.Get();
	ACharacter* AvatarCharacter = Character.Get();
	if (!CMC || !AvatarCharacter)
	{
		FinishMovement(EWeaponMovementEndReason::Interrupted);
		return;
	}

	UpdateActionRotation(DeltaTime);
	LastLocation = AvatarCharacter->GetActorLocation();

	const TSharedPtr<FRootMotionSource> Source =
		CMC->GetRootMotionSourceByID(RootMotionSourceID);
	if (!Source.IsValid() || Source->Status.HasFlag(ERootMotionSourceStatusFlags::Finished))
	{
		FinishMovement(EWeaponMovementEndReason::Completed);
	}
}

void UAbilityTask_MHGZWeaponMovement::OnDestroy(const bool AbilityIsEnding)
{
	if (ACharacter* AvatarCharacter = Character.Get())
	{
		if (bBoundLandedDelegate)
		{
			AvatarCharacter->LandedDelegate.RemoveDynamic(this,
				&UAbilityTask_MHGZWeaponMovement::HandleLanded);
		}
		if (bBoundCapsuleHit)
		{
			if (UCapsuleComponent* Capsule = AvatarCharacter->GetCapsuleComponent())
			{
				Capsule->OnComponentHit.RemoveDynamic(this,
					&UAbilityTask_MHGZWeaponMovement::HandleCapsuleHit);
			}
		}
	}
	bBoundLandedDelegate = false;
	bBoundCapsuleHit = false;

	if (!bFinished)
	{
		// Ability cancellation, weapon replacement and Runtime shutdown may end a
		// task without a gameplay result callback. They still obey the request's
		// explicit velocity policy before their RootMotionSource is removed.
		ApplyFinishVelocityPolicy();
	}
	RemoveRootMotionSource();
	ReleaseMovementOwnership();
	Super::OnDestroy(AbilityIsEnding);
}

void UAbilityTask_MHGZWeaponMovement::HandleLanded(const FHitResult& Hit)
{
	if (!bStarted || bFinished || MovementRequest.Mode != EWeaponMovementMode::BallisticVault)
	{
		return;
	}

	FinishMovement(EWeaponMovementEndReason::Landed, &Hit);
}

void UAbilityTask_MHGZWeaponMovement::HandleCapsuleHit(UPrimitiveComponent* HitComponent,
	AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse,
	const FHitResult& Hit)
{
	if (!bStarted || bFinished || !Hit.bBlockingHit
		|| MovementRequest.CollisionPolicy != EMovementCollisionPolicy::StopOnBlockingHit)
	{
		return;
	}

	// Ground contact has a dedicated Landed result for a ballistic vault.
	if ((MovementRequest.Mode == EWeaponMovementMode::BallisticVault
		|| MovementRequest.Mode == EWeaponMovementMode::CurvedVault)
		&& Hit.ImpactNormal.Z > 0.5f)
	{
		return;
	}

	FinishMovement(EWeaponMovementEndReason::BlockingHit, &Hit);
}

bool UAbilityTask_MHGZWeaponMovement::ValidateRequest() const
{
	if (!MovementRequest.OwnerAction.IsValid())
	{
		return false;
	}

	switch (MovementRequest.Mode)
	{
	case EWeaponMovementMode::BoundedDirectional:
		return FMath::IsFinite(MovementRequest.Duration) && MovementRequest.Duration > 0.0f
			&& FMath::IsFinite(MovementRequest.MaxDistance) && MovementRequest.MaxDistance > 0.0f;

	case EWeaponMovementMode::BallisticVault:
		return MovementRequest.HasValidBallisticParameters();

	case EWeaponMovementMode::CurvedVault:
		return MovementRequest.HasValidCurvedVaultParameters();

	case EWeaponMovementMode::AdditiveInertia:
		return FMath::IsFinite(MovementRequest.Duration) && MovementRequest.Duration > 0.0f
			&& !MovementRequest.InheritedVelocity.ContainsNaN()
			&& !MovementRequest.InheritedVelocity.IsNearlyZero()
			&& FMath::IsFinite(MovementRequest.InheritedVelocityRatio)
			&& MovementRequest.InheritedVelocityRatio > 0.0f;

	default:
		return false;
	}
}

bool UAbilityTask_MHGZWeaponMovement::ApplyMovementSource()
{
	switch (MovementRequest.Mode)
	{
	case EWeaponMovementMode::BoundedDirectional:
		return ApplyBoundedDirectionalSource();
	case EWeaponMovementMode::BallisticVault:
		return ApplyBallisticVaultSource();
	case EWeaponMovementMode::CurvedVault:
		return ApplyCurvedVaultSource();
	case EWeaponMovementMode::AdditiveInertia:
		return ApplyAdditiveInertiaSource();
	default:
		return false;
	}
}

bool UAbilityTask_MHGZWeaponMovement::ApplyBoundedDirectionalSource()
{
	ACharacter* AvatarCharacter = Character.Get();
	UCharacterMovementComponent* CMC = MovementComponent.Get();
	if (!AvatarCharacter || !CMC)
	{
		return false;
	}

	const FVector Direction = GetPlanarDirection(MovementRequest.DirectionSnapshot, *AvatarCharacter);
	if (Direction.IsNearlyZero())
	{
		return false;
	}

	ResolvedDuration = MovementRequest.Duration;
	ExpectedDestination = StartLocation + Direction * MovementRequest.MaxDistance;
	TSharedPtr<FRootMotionSource_MoveToForce> Source = MakeShared<FRootMotionSource_MoveToForce>();
	ConfigureCommonSource(*Source, MovementRequest.WarpTargetName, ResolvedDuration,
		ERootMotionAccumulateMode::Override, MovementRequest.CancelVelocityPolicy);
	Source->StartLocation = StartLocation;
	Source->TargetLocation = ExpectedDestination;
	Source->bRestrictSpeedToExpected = true;
	RootMotionSourceID = CMC->ApplyRootMotionSource(Source);
	return RootMotionSourceID != InvalidRootMotionSourceID;
}

bool UAbilityTask_MHGZWeaponMovement::ApplyBallisticVaultSource()
{
	ACharacter* AvatarCharacter = Character.Get();
	UCharacterMovementComponent* CMC = MovementComponent.Get();
	if (!AvatarCharacter || !CMC)
	{
		return false;
	}

	FVector Direction = GetPlanarDirection(MovementRequest.DirectionSnapshot, *AvatarCharacter);
	float Distance = MovementRequest.MaxDistance;
	float Height = MovementRequest.ApexHeight;
	ResolvedDuration = MovementRequest.Duration;
	if (MovementRequest.BallisticMode == EBallisticParameterMode::ExplicitLaunchVelocity)
	{
		const FVector LaunchVelocity = MovementRequest.LaunchVelocity;
		const FVector PlanarVelocity(LaunchVelocity.X, LaunchVelocity.Y, 0.0f);
		if (!PlanarVelocity.IsNearlyZero())
		{
			Direction = PlanarVelocity.GetSafeNormal();
		}

		const float GravityMagnitude = FMath::Abs(CMC->GetGravityZ());
		const float UpwardVelocity = LaunchVelocity.Z;
		if (GravityMagnitude <= KINDA_SMALL_NUMBER || UpwardVelocity <= KINDA_SMALL_NUMBER)
		{
			return false;
		}

		ResolvedDuration = (2.0f * UpwardVelocity) / GravityMagnitude;
		Height = (UpwardVelocity * UpwardVelocity) / (2.0f * GravityMagnitude);
		Distance = PlanarVelocity.Size() * ResolvedDuration;
	}

	if (Direction.IsNearlyZero() || ResolvedDuration <= KINDA_SMALL_NUMBER || Height <= 0.0f)
	{
		return false;
	}

	ExpectedDestination = StartLocation + Direction * Distance;
	TSharedPtr<FRootMotionSource_JumpForce> Source = MakeShared<FRootMotionSource_JumpForce>();
	ConfigureCommonSource(*Source, MovementRequest.WarpTargetName, ResolvedDuration,
		ERootMotionAccumulateMode::Override, MovementRequest.CancelVelocityPolicy);
	Source->Rotation = Direction.Rotation();
	Source->Distance = Distance;
	Source->Height = Height;
	Source->bDisableTimeout = false;
	RootMotionSourceID = CMC->ApplyRootMotionSource(Source);
	if (RootMotionSourceID == InvalidRootMotionSourceID)
	{
		return false;
	}

	CMC->SetMovementMode(MOVE_Falling);
	return true;
}

bool UAbilityTask_MHGZWeaponMovement::ApplyCurvedVaultSource()
{
	ACharacter* AvatarCharacter = Character.Get();
	UCharacterMovementComponent* CMC = MovementComponent.Get();
	if (!AvatarCharacter || !CMC || !MovementRequest.PathOffsetCurve)
	{
		return false;
	}

	const FVector Direction = GetPlanarDirection(MovementRequest.DirectionSnapshot,
		*AvatarCharacter);
	if (Direction.IsNearlyZero())
	{
		return false;
	}

	ResolvedDuration = MovementRequest.Duration;
	ExpectedDestination = StartLocation + Direction * MovementRequest.MaxDistance;
	TSharedPtr<FRootMotionSource_MoveToForce> Source =
		MakeShared<FRootMotionSource_MoveToForce>();
	ConfigureCommonSource(*Source, MovementRequest.WarpTargetName, ResolvedDuration,
		ERootMotionAccumulateMode::Override, MovementRequest.CancelVelocityPolicy);
	Source->StartLocation = StartLocation;
	Source->TargetLocation = ExpectedDestination;
	Source->bRestrictSpeedToExpected = true;
	Source->PathOffsetCurve = MovementRequest.PathOffsetCurve;

	// Install the authored terminal tangent BEFORE the source starts running.
	// MoveToForce targets a moving point and the CMC removes a finished source on
	// its own movement update, so mutating FinishVelocityParams after the task
	// notices completion is too late: the source is already gone and the CMC has
	// already derived the hand-off velocity from the final partial-frame
	// remainder.  Setting it here makes the natural removal carry the authored
	// velocity and removes the frame-order dependency entirely.
	//
	// The engine applies PathOffsetCurve through a yaw-only facing rotation, so
	// rotating the local tangent the same way reproduces the exact world tangent
	// regardless of where the character faces when this runs.
	const FRotator FacingRotation(0.0f, Direction.Rotation().Yaw, 0.0f);
	ResolvedHandoffVelocity =
		FacingRotation.RotateVector(MovementRequest.CurvedVaultHandoffVelocityLocal);
	bHasResolvedHandoffVelocity = !ResolvedHandoffVelocity.ContainsNaN()
		&& !ResolvedHandoffVelocity.IsNearlyZero();
	if (bHasResolvedHandoffVelocity)
	{
		Source->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::SetVelocity;
		Source->FinishVelocityParams.SetVelocity = ResolvedHandoffVelocity;
	}

	RootMotionSourceID = CMC->ApplyRootMotionSource(Source);
	if (RootMotionSourceID == InvalidRootMotionSourceID)
	{
		return false;
	}

	// The recorded path intentionally has a grounded crouch before lift-off.
	// Flying prevents CMC's floor / gravity code from treating that authored
	// phase as a landing; FinishMovement hands the terminal tangent to Falling.
	CMC->SetMovementMode(MOVE_Flying);
	return true;
}

bool UAbilityTask_MHGZWeaponMovement::ApplyAdditiveInertiaSource()
{
	UCharacterMovementComponent* CMC = MovementComponent.Get();
	ACharacter* AvatarCharacter = Character.Get();
	if (!CMC || !AvatarCharacter)
	{
		return false;
	}

	ResolvedDuration = MovementRequest.Duration;
	const FVector Force = MovementRequest.InheritedVelocity
		* MovementRequest.InheritedVelocityRatio;
	ExpectedDestination = StartLocation + Force * ResolvedDuration;
	TSharedPtr<FRootMotionSource_ConstantForce> Source = MakeShared<FRootMotionSource_ConstantForce>();
	ConfigureCommonSource(*Source, MovementRequest.WarpTargetName, ResolvedDuration,
		ERootMotionAccumulateMode::Additive, MovementRequest.CancelVelocityPolicy);
	Source->Force = Force;
	RootMotionSourceID = CMC->ApplyRootMotionSource(Source);
	return RootMotionSourceID != InvalidRootMotionSourceID;
}

void UAbilityTask_MHGZWeaponMovement::UpdateActionRotation(const float DeltaTime)
{
	if (MovementRequest.RotationPolicy == EActionRotationPolicy::Locked)
	{
		return;
	}

	ACharacter* AvatarCharacter = Character.Get();
	if (!AvatarCharacter)
	{
		return;
	}

	FVector DesiredDirection = GetPlanarDirection(MovementRequest.DirectionSnapshot, *AvatarCharacter);
	if (MovementRequest.RotationPolicy == EActionRotationPolicy::SteerWithinCone)
	{
		if (const AMHGZCharacter* MHGZCharacter = Cast<AMHGZCharacter>(AvatarCharacter))
		{
			const FVector InputDirection = MHGZCharacter->GetLastMovementInputDir().GetSafeNormal2D();
			if (!InputDirection.IsNearlyZero()
				&& FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
					FVector::DotProduct(DesiredDirection, InputDirection), -1.0f, 1.0f)))
					<= MovementRequest.SteeringConeHalfAngle)
			{
				DesiredDirection = InputDirection;
			}
		}
	}

	if (DesiredDirection.IsNearlyZero())
	{
		return;
	}

	const float CurrentYaw = AvatarCharacter->GetActorRotation().Yaw;
	const float TargetYaw = DesiredDirection.Rotation().Yaw;
	const float DeltaYaw = FMath::FindDeltaAngleDegrees(CurrentYaw, TargetYaw);
	const float MaxStep = MovementRequest.MaxTurnRateDegrees <= 0.0f
		? FMath::Abs(DeltaYaw)
		: MovementRequest.MaxTurnRateDegrees * DeltaTime;
	AvatarCharacter->SetActorRotation(FRotator(0.0f,
		FMath::UnwindDegrees(CurrentYaw + FMath::Clamp(DeltaYaw, -MaxStep, MaxStep)), 0.0f));
}

void UAbilityTask_MHGZWeaponMovement::FinishMovement(const EWeaponMovementEndReason Reason,
	const FHitResult* BlockingHit)
{
	if (bFinished)
	{
		return;
	}
	bFinished = true;

	Result.EndReason = Reason;
	Result.TravelledDistance = FVector::Dist2D(StartLocation,
		Character.IsValid() ? Character->GetActorLocation() : LastLocation);
	if (UCharacterMovementComponent* CMC = MovementComponent.Get())
	{
		Result.FinalVelocity = CMC->Velocity;
	}
	if (BlockingHit)
	{
		Result.BlockingHit = *BlockingHit;
	}

	if (Reason != EWeaponMovementEndReason::Completed
		&& Reason != EWeaponMovementEndReason::Landed)
	{
		ApplyFinishVelocityPolicy();
		if (UCharacterMovementComponent* CMC = MovementComponent.Get())
		{
			Result.FinalVelocity = CMC->Velocity;
		}
	}

	const bool bCompletedCurvedVault = Reason == EWeaponMovementEndReason::Completed
		&& MovementRequest.Mode == EWeaponMovementMode::CurvedVault;
	// The authored tangent was resolved when the source was created.  Reading
	// CMC->Velocity here instead would return the source's final partial-frame
	// remainder, which is what silently cost the free fall two thirds of its
	// vertical speed at every hand-off.
	FVector CurvedVaultHandoffVelocity = Result.FinalVelocity;
	if (bCompletedCurvedVault)
	{
		if (bHasResolvedHandoffVelocity)
		{
			CurvedVaultHandoffVelocity = ResolvedHandoffVelocity;
		}
		else
		{
			UE_LOG(LogMHGZ, Warning,
				TEXT("[WeaponMovement] CurvedVault completed without an authored hand-off ")
				TEXT("tangent; falling back to the collapsed CMC velocity (%s)."),
				*Result.FinalVelocity.ToCompactString());
		}

		if (UCharacterMovementComponent* CMC = MovementComponent.Get())
		{
			if (TSharedPtr<FRootMotionSource> Source =
				CMC->GetRootMotionSourceByID(RootMotionSourceID))
			{
				// The source is normally already gone by now, and its natural
				// removal applied the same value.  This only covers the case where
				// the task observes completion while the source is still live, so
				// the deferred removal installs the identical tangent.
				Source->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::SetVelocity;
				Source->FinishVelocityParams.SetVelocity = CurvedVaultHandoffVelocity;
			}
		}
	}
	RemoveRootMotionSource();
	if (bCompletedCurvedVault)
	{
		// The source has reached the recorded hand-off point.  From this exact
		// frame onward gravity and collision, rather than the source curve, own
		// the capsule.  Do this before notifying the owning GA so it can start
		// the fall presentation without an ownership gap.
		if (UCharacterMovementComponent* CMC = MovementComponent.Get())
		{
			CMC->SetMovementMode(MOVE_Falling);
			// RemoveRootMotionSource does not guarantee that a source's natural
			// FinishVelocity is installed before this task's callback.  Copy the
			// exact final tangent sampled while CurvedVault still owned the capsule;
			// otherwise the first Fall frame loses most of its horizontal and vertical
			// velocity, producing a visible Jump_Over -> Fall hitch.
			CMC->Velocity = CurvedVaultHandoffVelocity;
			Result.FinalVelocity = CurvedVaultHandoffVelocity;
		}
	}
	ReleaseMovementOwnership();
	if (ShouldBroadcastAbilityTaskDelegates())
	{
		OnFinished.Broadcast(Result);
	}
	EndTask();
}

void UAbilityTask_MHGZWeaponMovement::ReleaseMovementOwnership()
{
	if (UMHGZWeaponRuntimeHostComponent* Host = RuntimeHost.Get())
	{
		Host->ReleaseActionMovement(MovementRequest.OwnerAction);
	}
}

void UAbilityTask_MHGZWeaponMovement::RemoveRootMotionSource()
{
	if (RootMotionSourceID != InvalidRootMotionSourceID)
	{
		if (UCharacterMovementComponent* CMC = MovementComponent.Get())
		{
			CMC->RemoveRootMotionSourceByID(RootMotionSourceID);
		}
		RootMotionSourceID = InvalidRootMotionSourceID;
	}
}

void UAbilityTask_MHGZWeaponMovement::ApplyFinishVelocityPolicy()
{
	if (MovementRequest.CancelVelocityPolicy == EMovementCancelVelocityPolicy::ZeroVelocity)
	{
		if (UCharacterMovementComponent* CMC = MovementComponent.Get())
		{
			CMC->Velocity = FVector::ZeroVector;
		}
	}
}
