// Copyright MHGZ Project. All Rights Reserved.

#include "Movement/MHGZInstrumentedCharacterMovementComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "EngineGlobals.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "MHGZ.h"
#include "MHGZCharacter.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

bool UMHGZInstrumentedCharacterMovementComponent::IsAerialMovementTelemetryEnabled()
{
	IConsoleVariable* EnableVariable = IConsoleManager::Get().FindConsoleVariable(
		TEXT("mhgz.Telemetry.Enable"));
	return EnableVariable && EnableVariable->GetInt() != 0;
}

void UMHGZInstrumentedCharacterMovementComponent::PerformMovement(const float DeltaTime)
{
	if (!IsAerialMovementTelemetryEnabled())
	{
		Super::PerformMovement(DeltaTime);
		ApplyPendingLandingSpeedReset();
		return;
	}

	const FVector PreMovementVelocity = Velocity;
	const uint8 PreMovementMode = static_cast<uint8>(MovementMode);
	const uint64 FirstSampleSerial = AerialMovementSampleSerial;
	bSawBlockingImpactDuringMovement = false;
	LastImpactActorName = NAME_None;
	LastImpactComponentName = NAME_None;
	LastImpactNormal = FVector::ZeroVector;
	CaptureMovementBoundary(TEXT("CMC.PerformMovement.Pre"), DeltaTime);
	Super::PerformMovement(DeltaTime);
	ApplyPendingLandingSpeedReset();
	CaptureMovementBoundary(TEXT("CMC.PerformMovement.Post"), DeltaTime);

	// A major airborne planar-speed loss is evidence worth surfacing, not a request to
	// restore velocity. Report whether CMC handled an actual blocking impact so the
	// recording can distinguish collision response from a silent non-collision write.
	const float PrePlanarSpeed = FVector(PreMovementVelocity.X, PreMovementVelocity.Y, 0.0f).Size();
	const float PostPlanarSpeed = FVector(Velocity.X, Velocity.Y, 0.0f).Size();
	if (PreMovementMode == MOVE_Falling && MovementMode == MOVE_Falling
		&& PrePlanarSpeed >= 300.0f
		&& PostPlanarSpeed <= FMath::Max(5.0f, PrePlanarSpeed * 0.1f))
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[AerialMovementDiscontinuity] Frame=%llu XY %.1f->%.1f cm/s; BlockingImpact=%d Actor=%s Component=%s Normal=%s. No automatic velocity recovery was applied."),
			static_cast<unsigned long long>(GFrameCounter), PrePlanarSpeed, PostPlanarSpeed,
			bSawBlockingImpactDuringMovement ? 1 : 0,
			*LastImpactActorName.ToString(),
			*LastImpactComponentName.ToString(), *LastImpactNormal.ToCompactString());
		LogDiscontinuityPhaseTrace(FirstSampleSerial);
	}
}

bool UMHGZInstrumentedCharacterMovementComponent::ShouldCaptureMovementSubPhase() const
{
	if (!IsAerialMovementTelemetryEnabled())
	{
		return false;
	}

	// Falling covers the reported seam; Flying covers the ballistic-vault phase that precedes it,
	// so the recording shows what the seam is a transition away from.
	if (IsFalling() || IsFlying())
	{
		return true;
	}

	// Landing presentation writes happen after ProcessLanded has already switched to Walking.
	if (const AMHGZCharacter* MHGZCharacter = Cast<AMHGZCharacter>(CharacterOwner))
	{
		if (const UMHGZWeaponRuntimeHostComponent* Host = MHGZCharacter->GetWeaponRuntimeHost())
		{
			return Host->IsAerialFalling() || Host->IsAerialLanding();
		}
	}

	return false;
}

void UMHGZInstrumentedCharacterMovementComponent::HandleImpact(
	const FHitResult& Hit, const float TimeSlice, const FVector& MoveDelta)
{
	if (IsAerialMovementTelemetryEnabled())
	{
		if (Hit.bBlockingHit)
		{
			bSawBlockingImpactDuringMovement = true;
			LastImpactActorName = Hit.GetActor()
				? FName(*Hit.GetActor()->GetName()) : NAME_None;
			LastImpactComponentName = Hit.GetComponent()
				? FName(*Hit.GetComponent()->GetName()) : NAME_None;
			LastImpactNormal = Hit.ImpactNormal;
		}
		CaptureImpactSample(TEXT("CMC.HandleImpact.Pre"), Hit, TimeSlice, MoveDelta);
	}

	Super::HandleImpact(Hit, TimeSlice, MoveDelta);

	if (IsAerialMovementTelemetryEnabled())
	{
		CaptureImpactSample(TEXT("CMC.HandleImpact.Post"), Hit, TimeSlice, MoveDelta);
	}
}

void UMHGZInstrumentedCharacterMovementComponent::RestorePreAdditiveRootMotionVelocity()
{
	if (!ShouldCaptureMovementSubPhase())
	{
		Super::RestorePreAdditiveRootMotionVelocity();
		return;
	}

	// This restore is the only writer that can put a stale additive pre-velocity back.
	const FVector EntryVelocity = Velocity;
	const FName PreviousSite = ActiveMovementSubPhase;
	ActiveMovementSubPhase = FName(TEXT("CMC.RestorePreAdditiveRootMotionVelocity"));
	CaptureSubPhaseSample(FName(TEXT("CMC.RestorePreAdditiveRootMotionVelocity.Pre")), 0.0f, EntryVelocity);
	Super::RestorePreAdditiveRootMotionVelocity();
	CaptureSubPhaseSample(FName(TEXT("CMC.RestorePreAdditiveRootMotionVelocity.Post")), 0.0f, EntryVelocity);
	ActiveMovementSubPhase = PreviousSite;
}

void UMHGZInstrumentedCharacterMovementComponent::ApplyRootMotionToVelocity(const float deltaTime)
{
	if (!ShouldCaptureMovementSubPhase())
	{
		Super::ApplyRootMotionToVelocity(deltaTime);
		return;
	}

	const FVector EntryVelocity = Velocity;
	const FName PreviousSite = ActiveMovementSubPhase;
	ActiveMovementSubPhase = FName(TEXT("CMC.ApplyRootMotionToVelocity"));
	CaptureSubPhaseSample(FName(TEXT("CMC.ApplyRootMotionToVelocity.Pre")), deltaTime, EntryVelocity);
	Super::ApplyRootMotionToVelocity(deltaTime);
	CaptureSubPhaseSample(FName(TEXT("CMC.ApplyRootMotionToVelocity.Post")), deltaTime, EntryVelocity);
	ActiveMovementSubPhase = PreviousSite;
}

void UMHGZInstrumentedCharacterMovementComponent::CalcVelocity(const float DeltaTime,
	const float Friction, const bool bFluid, const float BrakingDeceleration)
{
	if (!ShouldCaptureMovementSubPhase())
	{
		Super::CalcVelocity(DeltaTime, Friction, bFluid, BrakingDeceleration);
		return;
	}

	const FVector EntryVelocity = Velocity;
	const FName PreviousSite = ActiveMovementSubPhase;
	ActiveMovementSubPhase = FName(TEXT("CMC.CalcVelocity"));
	CaptureSubPhaseSample(FName(TEXT("CMC.CalcVelocity.Pre")), DeltaTime, EntryVelocity);
	Super::CalcVelocity(DeltaTime, Friction, bFluid, BrakingDeceleration);
	FMHGZAerialMovementPhaseSample& PostSample =
		CaptureSubPhaseSample(FName(TEXT("CMC.CalcVelocity.Post")), DeltaTime, EntryVelocity);
	PostSample.SubPhaseFriction = Friction;
	PostSample.SubPhaseBrakingDeceleration = BrakingDeceleration;
	ActiveMovementSubPhase = PreviousSite;
}

void UMHGZInstrumentedCharacterMovementComponent::PhysFalling(const float deltaTime,
	const int32 Iterations)
{
	if (!ShouldCaptureMovementSubPhase())
	{
		Super::PhysFalling(deltaTime, Iterations);
		return;
	}

	const FVector EntryVelocity = Velocity;
	const FName PreviousSite = ActiveMovementSubPhase;
	ActiveMovementSubPhase = FName(TEXT("CMC.PhysFalling"));
	CaptureSubPhaseSample(FName(TEXT("CMC.PhysFalling.Pre")), deltaTime, EntryVelocity);
	Super::PhysFalling(deltaTime, Iterations);
	FMHGZAerialMovementPhaseSample& PostSample =
		CaptureSubPhaseSample(FName(TEXT("CMC.PhysFalling.Post")), deltaTime, EntryVelocity);
	PostSample.SubPhaseIterations = Iterations;
	ActiveMovementSubPhase = PreviousSite;
}

void UMHGZInstrumentedCharacterMovementComponent::ApplyAccumulatedForces(const float DeltaSeconds)
{
	if (!ShouldCaptureMovementSubPhase())
	{
		Super::ApplyAccumulatedForces(DeltaSeconds);
		return;
	}

	const FVector EntryVelocity = Velocity;
	const FName PreviousSite = ActiveMovementSubPhase;
	ActiveMovementSubPhase = FName(TEXT("CMC.ApplyAccumulatedForces"));
	CaptureSubPhaseSample(FName(TEXT("CMC.ApplyAccumulatedForces.Pre")), DeltaSeconds, EntryVelocity);
	Super::ApplyAccumulatedForces(DeltaSeconds);
	CaptureSubPhaseSample(FName(TEXT("CMC.ApplyAccumulatedForces.Post")), DeltaSeconds, EntryVelocity);
	ActiveMovementSubPhase = PreviousSite;
}

bool UMHGZInstrumentedCharacterMovementComponent::HandlePendingLaunch()
{
	if (!ShouldCaptureMovementSubPhase())
	{
		return Super::HandlePendingLaunch();
	}

	const FVector EntryVelocity = Velocity;
	const FName PreviousSite = ActiveMovementSubPhase;
	ActiveMovementSubPhase = FName(TEXT("CMC.HandlePendingLaunch"));
	CaptureSubPhaseSample(FName(TEXT("CMC.HandlePendingLaunch.Pre")), 0.0f, EntryVelocity);
	const bool bHandled = Super::HandlePendingLaunch();
	CaptureSubPhaseSample(FName(TEXT("CMC.HandlePendingLaunch.Post")), 0.0f, EntryVelocity);
	ActiveMovementSubPhase = PreviousSite;
	return bHandled;
}

void UMHGZInstrumentedCharacterMovementComponent::UpdateCharacterStateBeforeMovement(
	const float DeltaSeconds)
{
	if (!ShouldCaptureMovementSubPhase())
	{
		Super::UpdateCharacterStateBeforeMovement(DeltaSeconds);
		return;
	}

	const FVector EntryVelocity = Velocity;
	const FName PreviousSite = ActiveMovementSubPhase;
	ActiveMovementSubPhase = FName(TEXT("CMC.UpdateCharacterStateBeforeMovement"));
	CaptureSubPhaseSample(FName(TEXT("CMC.UpdateCharacterStateBeforeMovement.Pre")),
		DeltaSeconds, EntryVelocity);
	Super::UpdateCharacterStateBeforeMovement(DeltaSeconds);
	CaptureSubPhaseSample(FName(TEXT("CMC.UpdateCharacterStateBeforeMovement.Post")),
		DeltaSeconds, EntryVelocity);
	ActiveMovementSubPhase = PreviousSite;
}

void UMHGZInstrumentedCharacterMovementComponent::StartNewPhysics(const float deltaTime,
	const int32 Iterations)
{
	if (!ShouldCaptureMovementSubPhase())
	{
		Super::StartNewPhysics(deltaTime, Iterations);
		return;
	}

	const FVector EntryVelocity = Velocity;
	const FName PreviousSite = ActiveMovementSubPhase;
	ActiveMovementSubPhase = FName(TEXT("CMC.StartNewPhysics"));
	CaptureSubPhaseSample(FName(TEXT("CMC.StartNewPhysics.Pre")), deltaTime, EntryVelocity);
	Super::StartNewPhysics(deltaTime, Iterations);
	FMHGZAerialMovementPhaseSample& PostSample =
		CaptureSubPhaseSample(FName(TEXT("CMC.StartNewPhysics.Post")), deltaTime, EntryVelocity);
	PostSample.SubPhaseIterations = Iterations;
	ActiveMovementSubPhase = PreviousSite;
}

void UMHGZInstrumentedCharacterMovementComponent::TickCharacterPose(const float DeltaTime)
{
	if (!ShouldCaptureMovementSubPhase())
	{
		Super::TickCharacterPose(DeltaTime);
		return;
	}

	// RootMotionParams is cleared at the end of every PerformMovement, so whatever is in it here is
	// a leftover; the reading that matters is what THIS call accumulates.
	const bool bHasBefore = RootMotionParams.bHasRootMotion;
	const FVector TranslationBefore = RootMotionParams.GetRootMotionTransform().GetTranslation();
	const FRotator RotationBefore = bHasBefore
		? RootMotionParams.GetRootMotionTransform().Rotator() : FRotator::ZeroRotator;
	const FName PreviousSite = ActiveMovementSubPhase;
	ActiveMovementSubPhase = FName(TEXT("CMC.TickCharacterPose"));
	CaptureSubPhaseSample(FName(TEXT("CMC.TickCharacterPose.Pre")), DeltaTime, Velocity);

	Super::TickCharacterPose(DeltaTime);

	const bool bHasAfter = RootMotionParams.bHasRootMotion;
	const FVector TranslationAfter = RootMotionParams.GetRootMotionTransform().GetTranslation();
	FMHGZAerialMovementPhaseSample& PostSample =
		CaptureSubPhaseSample(FName(TEXT("CMC.TickCharacterPose.Post")), DeltaTime, Velocity);
	PostSample.bTickCharacterPoseSample = true;
	PostSample.bRootMotionParamsBefore = bHasBefore;
	PostSample.RootMotionParamsBeforeTranslation = TranslationBefore;
	if (const USkeletalMeshComponent* Mesh = CharacterOwner ? CharacterOwner->GetMesh() : nullptr)
	{
		PostSample.bMeshShouldTickPose = Mesh->ShouldTickPose();
		PostSample.bPlayingRootMotionFromEverything = Mesh->IsPlayingRootMotionFromEverything();
		PostSample.bPlayingNetworkedRootMotionMontage = Mesh->IsPlayingNetworkedRootMotionMontage();
		if (const UAnimInstance* AnimInstance = Mesh->GetAnimInstance())
		{
			PostSample.MontageInstanceCount = AnimInstance->MontageInstances.Num();
		}
	}
	// Montage-side feeder: only a montage instance that owns root motion and is not disabled can
	// reach QueueRootMotionBlend (AnimMontage.cpp:2486/2580).
	if (USkeletalMeshComponent* Mesh = CharacterOwner ? CharacterOwner->GetMesh() : nullptr)
	{
		if (UAnimInstance* AnimInstance = Mesh->GetAnimInstance())
		{
			if (const FAnimMontageInstance* RootMotionMontage = AnimInstance->GetRootMotionMontageInstance())
			{
				PostSample.RootMotionMontageName =
					RootMotionMontage->Montage ? RootMotionMontage->Montage->GetFName() : NAME_None;
				PostSample.RootMotionMontagePosition = RootMotionMontage->GetPosition();
				PostSample.RootMotionMontageWeight = RootMotionMontage->GetWeight();
				PostSample.bRootMotionMontageDisabled = RootMotionMontage->IsRootMotionDisabled();
			}
		}
	}
	ActiveMovementSubPhase = PreviousSite;

	// A flip or a held flag is what matters; quiet frames stay out of the log.
	if (bHasAfter || bHasBefore)
	{
		LogAnimationRootMotionFeed(DeltaTime, bHasBefore, TranslationBefore, TranslationAfter,
			RotationBefore);
	}
}

void UMHGZInstrumentedCharacterMovementComponent::LogAnimationRootMotionFeed(const float DeltaTime,
	const bool bRootMotionParamsBefore, const FVector& TranslationBefore,
	const FVector& TranslationAfter, const FRotator& RotationBefore) const
{
	// Root rotation is its own ownership question; report the accumulated transform's rotation too.
	const FRotator RotationAfter = RootMotionParams.bHasRootMotion
		? RootMotionParams.GetRootMotionTransform().Rotator()
		: FRotator::ZeroRotator;
	const USkeletalMeshComponent* Mesh = CharacterOwner ? CharacterOwner->GetMesh() : nullptr;
	UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
	const FAnimMontageInstance* RootMotionMontage =
		AnimInstance ? AnimInstance->GetRootMotionMontageInstance() : nullptr;

	FString ActionOwner;
	FString ActiveActions;
	if (const AMHGZCharacter* MHGZCharacter = Cast<AMHGZCharacter>(CharacterOwner))
	{
		if (const UMHGZWeaponRuntimeHostComponent* Host = MHGZCharacter->GetWeaponRuntimeHost())
		{
			ActionOwner = Host->GetActionMovementOwnerDebugString();
			ActiveActions = Host->GetActiveActionsDebugString();
		}
	}

	if (AerialRootMotionFeedLogCount >= AerialRootMotionFeedLogLimit)
	{
		if (AerialRootMotionFeedLogCount == AerialRootMotionFeedLogLimit)
		{
			++AerialRootMotionFeedLogCount;
			UE_LOG(LogMHGZ, Warning,
				TEXT("[AerialRootMotionFeed] further rows suppressed after %d; the probe is capped so the log stays readable. Raise AerialRootMotionFeedLogLimit if a longer window is needed."),
				AerialRootMotionFeedLogLimit);
		}
		return;
	}
	++AerialRootMotionFeedLogCount;

	UE_LOG(LogMHGZ, Warning,
		TEXT("[AerialRootMotionFeed] Frame=%llu dt=%.4f animRM %d->%d RMPtrans=(%.4f,%.4f,%.4f)->(%.4f,%.4f,%.4f) RMProtYaw=%.3f->%.3f RMProt=(%.3f,%.3f,%.3f) vXY=%.1f vZ=%.1f playRM=%d(fromEverything=%d,networkedMontage=%d) shouldTickPose=%d montages=%d RMImontage=%s pos=%.4f weight=%.3f rmDisabled=%d owner=[%s] actions=[%s]"),
		static_cast<unsigned long long>(GFrameCounter), DeltaTime,
		bRootMotionParamsBefore ? 1 : 0, RootMotionParams.bHasRootMotion ? 1 : 0,
		TranslationBefore.X, TranslationBefore.Y, TranslationBefore.Z,
		TranslationAfter.X, TranslationAfter.Y, TranslationAfter.Z,
		RotationBefore.Yaw, RotationAfter.Yaw,
		RotationAfter.Pitch, RotationAfter.Yaw, RotationAfter.Roll,
		FVector(Velocity.X, Velocity.Y, 0.0f).Size(), Velocity.Z,
		CharacterOwner && CharacterOwner->IsPlayingRootMotion() ? 1 : 0,
		Mesh && Mesh->IsPlayingRootMotionFromEverything() ? 1 : 0,
		Mesh && Mesh->IsPlayingNetworkedRootMotionMontage() ? 1 : 0,
		Mesh && Mesh->ShouldTickPose() ? 1 : 0,
		AnimInstance ? AnimInstance->MontageInstances.Num() : 0,
		RootMotionMontage && RootMotionMontage->Montage
			? *RootMotionMontage->Montage->GetName() : TEXT("None"),
		RootMotionMontage ? RootMotionMontage->GetPosition() : 0.0f,
		RootMotionMontage ? RootMotionMontage->GetWeight() : 0.0f,
		RootMotionMontage && RootMotionMontage->IsRootMotionDisabled() ? 1 : 0,
		*ActionOwner, *ActiveActions);
}

FVector UMHGZInstrumentedCharacterMovementComponent::ConstrainAnimRootMotionVelocity(
	const FVector& RootMotionVelocity, const FVector& CurrentVelocity) const
{
	const FVector Result = Super::ConstrainAnimRootMotionVelocity(RootMotionVelocity, CurrentVelocity);

	// Capture every vote, not just airborne ones: a session with zero probe rows is itself
	// the evidence that the animation-root branch never ran (shield callbacks included).
	if (IsAerialMovementTelemetryEnabled())
	{
		FMHGZAerialMovementPhaseSample& Sample =
			CaptureSubPhaseSample(FName(TEXT("CMC.ConstrainAnimRootMotionVelocity")), 0.0f,
				CurrentVelocity);
		Sample.bConstrainProbe = true;
		Sample.ConstrainCallSite = ActiveMovementSubPhase;
		Sample.ConstrainRootMotionVelocity = RootMotionVelocity;
		Sample.ConstrainInputVelocity = CurrentVelocity;
		Sample.ConstrainOutputVelocity = Result;
	}

	return Result;
}

void UMHGZInstrumentedCharacterMovementComponent::QueuePostMovementLandingSpeedReset(
	const float Speed, const FVector& PlanarDirection)
{
	PendingLandingHorizontalSpeed = FMath::Max(Speed, 0.0f);
	PendingLandingPlanarDirection = FVector(PlanarDirection.X, PlanarDirection.Y, 0.0f)
		.GetSafeNormal();
	bHasPendingLandingSpeedReset = PendingLandingHorizontalSpeed > 0.0f
		&& !PendingLandingPlanarDirection.IsNearlyZero();
	RecordAerialMovementEvent(TEXT("CMC.LandingSpeedReset.Queued"));
}

void UMHGZInstrumentedCharacterMovementComponent::ApplyPendingLandingSpeedReset()
{
	if (!bHasPendingLandingSpeedReset)
	{
		return;
	}

	// A landing presentation is claimed inside CMC::ProcessLanded, during Super::PerformMovement.
	// The first write can therefore be followed by same-frame Walking braking. Re-apply exactly
	// once at the end of that same movement update so the post-update sample matches Rise's 148
	// first-frame horizontal speed. Later walking updates retain their normal braking/input.
	const FVector PlanarVelocity = PendingLandingPlanarDirection * PendingLandingHorizontalSpeed;
	Velocity.X = PlanarVelocity.X;
	Velocity.Y = PlanarVelocity.Y;
	bHasPendingLandingSpeedReset = false;
	// 帧外可核对的副本（自动化用）：原始那次写发生在 PerformMovement 内，测试只能这样区分
	// 「回调写值 / 同帧 CMC 后值 / 下一帧值」三个采样点（P1-1）。
	LastAppliedLandingSpeed = PendingLandingHorizontalSpeed;
	LastAppliedLandingDirection = PendingLandingPlanarDirection;
	++LandingSpeedResetApplyCount;
	RecordAerialMovementEvent(TEXT("CMC.LandingSpeedReset.Applied"));
}

void UMHGZInstrumentedCharacterMovementComponent::RecordAerialMovementEvent(
	const FName Phase, const FTransform* RootMotionCallbackTransform, const float DeltaSeconds)
{
	if (IsAerialMovementTelemetryEnabled())
	{
		CaptureSample(Phase, DeltaSeconds, RootMotionCallbackTransform);
	}
}

void UMHGZInstrumentedCharacterMovementComponent::CaptureMovementBoundary(
	const FName Phase, const float DeltaSeconds)
{
	CaptureSample(Phase, DeltaSeconds, nullptr);
}

void UMHGZInstrumentedCharacterMovementComponent::CaptureImpactSample(
	const FName Phase, const FHitResult& Hit, const float TimeSlice, const FVector& MoveDelta)
{
	CaptureSample(Phase, TimeSlice, nullptr);
	FMHGZAerialMovementPhaseSample& Sample = AerialMovementSamples.Last();
	Sample.bHasMovementImpact = true;
	Sample.bImpactBlockingHit = Hit.bBlockingHit;
	Sample.bImpactStartPenetrating = Hit.bStartPenetrating;
	Sample.ImpactActorName = Hit.GetActor() ? FName(*Hit.GetActor()->GetName()) : NAME_None;
	Sample.ImpactComponentName = Hit.GetComponent()
		? FName(*Hit.GetComponent()->GetName()) : NAME_None;
	Sample.ImpactPoint = Hit.ImpactPoint;
	Sample.ImpactNormal = Hit.ImpactNormal;
	Sample.ImpactMoveDelta = MoveDelta;
	Sample.ImpactTimeSlice = TimeSlice;
}

FMHGZAerialMovementPhaseSample& UMHGZInstrumentedCharacterMovementComponent::CaptureSubPhaseSample(
	const FName Phase, const float DeltaSeconds, const FVector& EntryVelocity) const
{
	CaptureSample(Phase, DeltaSeconds, nullptr);
	FMHGZAerialMovementPhaseSample& Sample = AerialMovementSamples.Last();
	Sample.SubPhaseSite = ActiveMovementSubPhase;
	Sample.SubPhaseEntryVelocity = EntryVelocity;
	Sample.bHasAnimRootMotion = RootMotionParams.bHasRootMotion;
	Sample.RootMotionParamsTranslation = RootMotionParams.GetRootMotionTransform().GetTranslation();
	Sample.AnimRootMotionVelocity = AnimRootMotionVelocity;
	Sample.bIsPlayingRootMotion = CharacterOwner && CharacterOwner->IsPlayingRootMotion();
	Sample.bShieldDelegateBound = ProcessRootMotionPostConvertToWorld.IsBound();
	Sample.bAdditiveVelocityApplied = CurrentRootMotion.bIsAdditiveVelocityApplied != 0;
	Sample.LastPreAdditiveVelocity = CurrentRootMotion.LastPreAdditiveVelocity;
	return Sample;
}

void UMHGZInstrumentedCharacterMovementComponent::LogDiscontinuityPhaseTrace(
	const uint64 FirstSampleSerial) const
{
	// One line per recorded sub-phase of the failing CMC update: the first line whose planar
	// speed collapses names the writer, and the anim-root columns say whether it came from
	// the animation root branch at all.
	for (const FMHGZAerialMovementPhaseSample& Sample : AerialMovementSamples)
	{
		if (Sample.Serial <= FirstSampleSerial)
		{
			continue;
		}

		const float PlanarSpeed = FVector(Sample.Velocity.X, Sample.Velocity.Y, 0.0f).Size();
		UE_LOG(LogMHGZ, Warning,
			TEXT("[AerialMovementDiscontinuity]   #%llu %s site=%s v=(%.2f,%.2f,%.2f) vXY=%.2f entryVXY=%.2f animRM=%d playingRM=%d animRMv=(%.1f,%.1f,%.1f) override=%d additive=%d additiveApplied=%d lastPreAdditive=(%.1f,%.1f,%.1f) shieldBound=%d friction=%.2f brake=%.1f iter=%d"),
			static_cast<unsigned long long>(Sample.Serial), *Sample.Phase.ToString(),
			*Sample.SubPhaseSite.ToString(),
			Sample.Velocity.X, Sample.Velocity.Y, Sample.Velocity.Z, PlanarSpeed,
			FVector(Sample.SubPhaseEntryVelocity.X, Sample.SubPhaseEntryVelocity.Y, 0.0f).Size(),
			Sample.bHasAnimRootMotion ? 1 : 0, Sample.bIsPlayingRootMotion ? 1 : 0,
			Sample.AnimRootMotionVelocity.X, Sample.AnimRootMotionVelocity.Y,
			Sample.AnimRootMotionVelocity.Z,
			Sample.bHasOverrideRootMotionVelocity ? 1 : 0,
			Sample.bHasAdditiveRootMotionVelocity ? 1 : 0,
			Sample.bAdditiveVelocityApplied ? 1 : 0,
			Sample.LastPreAdditiveVelocity.X, Sample.LastPreAdditiveVelocity.Y,
			Sample.LastPreAdditiveVelocity.Z,
			Sample.bShieldDelegateBound ? 1 : 0,
			Sample.SubPhaseFriction, Sample.SubPhaseBrakingDeceleration, Sample.SubPhaseIterations);
	}
}

void UMHGZInstrumentedCharacterMovementComponent::CaptureSample(
	const FName Phase, const float DeltaSeconds,
	const FTransform* RootMotionCallbackTransform) const
{
	FMHGZAerialMovementPhaseSample& Sample = AerialMovementSamples.Emplace_GetRef();
	Sample.Serial = ++AerialMovementSampleSerial;
	Sample.Frame = GFrameCounter;
	Sample.WorldTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	Sample.Phase = Phase;
	Sample.DeltaSeconds = FMath::Max(DeltaSeconds, 0.0f);
	Sample.Location = UpdatedComponent ? UpdatedComponent->GetComponentLocation() : FVector::ZeroVector;
	Sample.Velocity = Velocity;
	Sample.Acceleration = Acceleration;
	Sample.MovementMode = static_cast<uint8>(MovementMode);
	Sample.CustomMovementMode = CustomMovementMode;
	Sample.GravityScale = GravityScale;
	Sample.BrakingDecelerationFalling = BrakingDecelerationFalling;
	Sample.FallingLateralFriction = FallingLateralFriction;
	if (const AMHGZCharacter* MHGZCharacter = Cast<AMHGZCharacter>(CharacterOwner))
	{
		if (const UMHGZWeaponRuntimeHostComponent* Host = MHGZCharacter->GetWeaponRuntimeHost())
		{
			Sample.RuntimeGeneration = Host->GetCurrentToken().Generation;
			Sample.ActionMovementOwner = Host->GetActionMovementOwnerDebugString();
			Sample.MontageRootMotionOwner = Host->GetMontageRootMotionOwnerDebugString();
			Sample.ActiveActions = Host->GetActiveActionsDebugString();
		}
	}
	Sample.ActiveRootMotionSourceCount = CurrentRootMotion.RootMotionSources.Num();
	Sample.PendingRootMotionSourceCount = CurrentRootMotion.PendingAddRootMotionSources.Num();
	Sample.bHasOverrideRootMotionVelocity = CurrentRootMotion.HasOverrideVelocity();
	Sample.bHasAdditiveRootMotionVelocity = CurrentRootMotion.HasAdditiveVelocity();
	Sample.bHasRootMotionCallbackTransform = RootMotionCallbackTransform != nullptr;
	if (RootMotionCallbackTransform)
	{
		Sample.RootMotionCallbackTransform = *RootMotionCallbackTransform;
	}

	const int32 OverflowCount = AerialMovementSamples.Num() - AerialMovementSampleHistoryLimit;
	if (OverflowCount > 0)
	{
		AerialMovementSamples.RemoveAt(0, OverflowCount, EAllowShrinking::No);
	}
}

void UMHGZInstrumentedCharacterMovementComponent::GetAerialMovementSamplesSince(
	const uint64 LastObservedSerial, TArray<FMHGZAerialMovementPhaseSample>& OutSamples) const
{
	OutSamples.Reset();
	for (const FMHGZAerialMovementPhaseSample& Sample : AerialMovementSamples)
	{
		if (Sample.Serial > LastObservedSerial)
		{
			OutSamples.Add(Sample);
		}
	}
}
