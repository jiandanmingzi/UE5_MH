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

void UMHGZInstrumentedCharacterMovementComponent::SetAerialPlanarAcceleration(
	const FVector InAcceleration)
{
	AerialPlanarAcceleration = FMath::IsFinite(InAcceleration.X)
		&& FMath::IsFinite(InAcceleration.Y) && FMath::IsFinite(InAcceleration.Z)
		? FVector(InAcceleration.X, InAcceleration.Y, 0.0) : FVector::ZeroVector;
}

uint64 UMHGZInstrumentedCharacterMovementComponent::BeginSegmentedDirectionalMovement(
	const FWeaponMovementRequest& Request)
{
	const AMHGZCharacter* Avatar = Cast<AMHGZCharacter>(CharacterOwner);
	const UMHGZWeaponRuntimeHostComponent* Host = Avatar ? Avatar->GetWeaponRuntimeHost() : nullptr;
	if (!HasValidData() || !Host || bSegmentedMovementActive
		|| Request.Mode != EWeaponMovementMode::SegmentedDirectional
		|| !Request.HasValidSegmentedDirectionalParameters()
		|| !Host->IsActionMovementOwnedBy(Request.OwnerAction))
	{
		return 0;
	}

	SegmentedMovement = Request;
	SegmentedMovement.DirectionSnapshot.Normalize();
	++SegmentedMovementLease;
	if (SegmentedMovementLease == 0) { ++SegmentedMovementLease; }
	SegmentedMovementElapsed = 0.0f;
	bSegmentedMovementCompleted = false;
	bSegmentedMovementActive = true;
	SegmentedContactNormals.Reset();
	SetMovementMode(MOVE_Flying);
	Velocity = SegmentedMovement.SegmentedProfile.GetVelocity(
		SegmentedMovement.DirectionSnapshot, 0.0f);
	RecordAerialMovementEvent(TEXT("SegmentedDash.Begin"));
	return SegmentedMovementLease;
}

bool UMHGZInstrumentedCharacterMovementComponent::IsSegmentedMovementActive(const uint64 Lease) const
{
	return Lease != 0 && Lease == SegmentedMovementLease && bSegmentedMovementActive;
}

bool UMHGZInstrumentedCharacterMovementComponent::HasSegmentedMovementCompleted(const uint64 Lease) const
{
	return Lease != 0 && Lease == SegmentedMovementLease && bSegmentedMovementCompleted;
}

void UMHGZInstrumentedCharacterMovementComponent::RetireSegmentedDirectionalMovement(const uint64 Lease)
{
	if (Lease != 0 && Lease == SegmentedMovementLease)
	{
		bSegmentedMovementActive = false;
		bSegmentedMovementCompleted = false;
		SegmentedContactNormals.Reset();
	}
}

bool UMHGZInstrumentedCharacterMovementComponent::IsSegmentedMovementOwnerCurrent(const uint64 Lease) const
{
	const AMHGZCharacter* Avatar = Cast<AMHGZCharacter>(CharacterOwner);
	const UMHGZWeaponRuntimeHostComponent* Host = Avatar ? Avatar->GetWeaponRuntimeHost() : nullptr;
	return IsSegmentedMovementActive(Lease) && HasValidData() && Host
		&& Host->IsActionMovementOwnedBy(SegmentedMovement.OwnerAction)
		&& Host->IsTokenCurrent(SegmentedMovement.OwnerAction.RuntimeToken);
}

uint64 UMHGZInstrumentedCharacterMovementComponent::BeginRecordedTrajectoryMovement(
	const FWeaponMovementRequest& Request)
{
	const AMHGZCharacter* Avatar = Cast<AMHGZCharacter>(CharacterOwner);
	const UMHGZWeaponRuntimeHostComponent* Host = Avatar ? Avatar->GetWeaponRuntimeHost() : nullptr;
	if (!HasValidData() || !Host || bRecordedTrajectoryActive
		|| Request.Mode != EWeaponMovementMode::RecordedLocalTrajectory
		|| !Request.HasValidRecordedTrajectoryParameters()
		|| !Host->IsActionMovementOwnedBy(Request.OwnerAction))
	{
		return 0;
	}

	const bool bGrounded =
		Request.TrajectoryGroundPolicy == ERecordedTrajectoryGroundPolicy::Grounded;
	// 地面相位要求胶囊真的站在可行走地面上。半空启动一条地面曲线会让整段动作悬空滑行 ——
	// 那是拒绝配置，不是退化运行。
	//
	// **问地面，不问模式**：201→203 的切换发生在 `ProcessLanded` 内部，
	// 那一刻 `SetPostLandedPhysics` 还没跑，`MovementMode` 仍是 MOVE_Flying，
	// 但胶囊确实已经踩在可行走地面上。拿模式当代理指标会把每一次正常落地都拒掉。
	if (bGrounded)
	{
		FFindFloorResult Floor;
		FindFloor(UpdatedComponent ? UpdatedComponent->GetComponentLocation()
			: FVector::ZeroVector, Floor, false);
		if (!Floor.IsWalkableFloor() || Floor.FloorDist > MAX_FLOOR_DIST)
		{
			return 0;
		}
	}

	RecordedTrajectory = Request;
	RecordedTrajectory.DirectionSnapshot = FVector(Request.DirectionSnapshot.X,
		Request.DirectionSnapshot.Y, 0.0).GetSafeNormal();
	if (RecordedTrajectory.DirectionSnapshot.IsNearlyZero())
	{
		return 0;
	}
	++RecordedTrajectoryLease;
	if (RecordedTrajectoryLease == 0) { ++RecordedTrajectoryLease; }
	RecordedTrajectoryElapsed = 0.0f;
	bRecordedTrajectoryCompleted = false;
	bRecordedTrajectoryActive = true;
	RecordedContactNormals.Reset();

	if (bGrounded)
	{
		SetMovementMode(MOVE_Walking);
	}
	else
	{
		SetMovementMode(MOVE_Flying);
	}
	// 相位起点速度取自同一条曲线，而不是回读上一相位被源移除顺序影响的尾帧速度。
	FVector StartVelocity = MHGZRecordedLocalTrajectory::LocalToWorld(
		RecordedTrajectory.GetRecordedTrajectoryVelocity(0.0f),
		RecordedTrajectory.DirectionSnapshot);
	if (bGrounded)
	{
		StartVelocity.Z = 0.0;
	}
	Velocity = StartVelocity;
	RecordAerialMovementEvent(bGrounded
		? FName(TEXT("RecordedTrajectory.BeginGrounded"))
		: FName(TEXT("RecordedTrajectory.Begin")));
	return RecordedTrajectoryLease;
}

bool UMHGZInstrumentedCharacterMovementComponent::EnableRecordedTrajectoryLanding(
	const uint64 Lease)
{
	if (!IsRecordedTrajectoryOwnerCurrent(Lease))
	{
		return false;
	}
	if (RecordedTrajectory.TrajectoryGroundPolicy
		== ERecordedTrajectoryGroundPolicy::LandingAirborne)
	{
		return true;
	}
	if (RecordedTrajectory.TrajectoryGroundPolicy
		!= ERecordedTrajectoryGroundPolicy::ProtectedFlight
		|| MovementMode != MOVE_Flying)
	{
		return false;
	}

	// This changes only floor/ProcessLanded eligibility. The existing lease,
	// elapsed seconds, curve origin and terminal model stay untouched.
	RecordedTrajectory.TrajectoryGroundPolicy =
		ERecordedTrajectoryGroundPolicy::LandingAirborne;
	RecordAerialMovementEvent(TEXT("RecordedTrajectory.LandingEnabled"));
	return true;
}

bool UMHGZInstrumentedCharacterMovementComponent::IsRecordedTrajectoryMovementActive(
	const uint64 Lease) const
{
	return Lease != 0 && Lease == RecordedTrajectoryLease && bRecordedTrajectoryActive;
}

bool UMHGZInstrumentedCharacterMovementComponent::HasRecordedTrajectoryMovementCompleted(
	const uint64 Lease) const
{
	return Lease != 0 && Lease == RecordedTrajectoryLease && bRecordedTrajectoryCompleted;
}

void UMHGZInstrumentedCharacterMovementComponent::GetRecordedTrajectoryTelemetry(
	const FWeaponActionToken& ExpectedOwner,
	FMHGZRecordedTrajectoryTelemetry& OutTelemetry) const
{
	OutTelemetry = FMHGZRecordedTrajectoryTelemetry();
	if (!ExpectedOwner.IsValid() || RecordedTrajectory.OwnerAction != ExpectedOwner)
	{
		return;
	}
	OutTelemetry.Lease = RecordedTrajectoryLease;
	OutTelemetry.ElapsedSeconds = RecordedTrajectoryElapsed;
	OutTelemetry.CoveredSeconds = RecordedTrajectory.RecordedTrajectory.GetDuration();
	OutTelemetry.bActive = bRecordedTrajectoryActive;
	OutTelemetry.bCompleted = bRecordedTrajectoryCompleted;
}

void UMHGZInstrumentedCharacterMovementComponent::RetireRecordedTrajectoryMovement(
	const uint64 Lease)
{
	if (Lease != 0 && Lease == RecordedTrajectoryLease)
	{
		bRecordedTrajectoryActive = false;
		bRecordedTrajectoryCompleted = false;
		RecordedContactNormals.Reset();
	}
}

bool UMHGZInstrumentedCharacterMovementComponent::IsRecordedTrajectoryOwnerCurrent(
	const uint64 Lease) const
{
	const AMHGZCharacter* Avatar = Cast<AMHGZCharacter>(CharacterOwner);
	const UMHGZWeaponRuntimeHostComponent* Host = Avatar ? Avatar->GetWeaponRuntimeHost() : nullptr;
	return IsRecordedTrajectoryMovementActive(Lease) && HasValidData() && Host
		&& Host->IsActionMovementOwnedBy(RecordedTrajectory.OwnerAction)
		&& Host->IsTokenCurrent(RecordedTrajectory.OwnerAction.RuntimeToken);
}

bool UMHGZInstrumentedCharacterMovementComponent::IsRecordedTrajectoryDurationElapsed() const
{
	const float Duration = RecordedTrajectory.RecordedTrajectory.GetDuration();
	if (Duration <= 0.0f)
	{
		return true;
	}
	// 覆盖外续行（201）**没有**曲线上限：它只由真实触地或独立看门狗结束，
	// 这里如果按时长收尾，飞得比录制更高时胶囊会在半空被冻住。
	if (RecordedTrajectory.TrajectoryTailPolicy
		== ERecordedTrajectoryTailPolicy::IntegrateTerminalModel
		&& RecordedTrajectory.TrajectoryGroundPolicy
			!= ERecordedTrajectoryGroundPolicy::Grounded)
	{
		return false;
	}
	return RecordedTrajectoryElapsed >= Duration - SMALL_NUMBER;
}

void UMHGZInstrumentedCharacterMovementComponent::PhysWalking(const float deltaTime,
	const int32 Iterations)
{
	if (!bRecordedTrajectoryActive
		|| RecordedTrajectory.TrajectoryGroundPolicy
			!= ERecordedTrajectoryGroundPolicy::Grounded)
	{
		Super::PhysWalking(deltaTime, Iterations);
		return;
	}

	PhysRecordedTrajectory(deltaTime, Iterations, true);
}

void UMHGZInstrumentedCharacterMovementComponent::PhysRecordedTrajectory(
	const float DeltaTime, int32 Iterations, const bool bGrounded)
{
	const uint64 Lease = RecordedTrajectoryLease;
	if (!IsRecordedTrajectoryOwnerCurrent(Lease))
	{
		RetireRecordedTrajectoryMovement(Lease);
		const AMHGZCharacter* Avatar = Cast<AMHGZCharacter>(CharacterOwner);
		const UMHGZWeaponRuntimeHostComponent* Host = Avatar
			? Avatar->GetWeaponRuntimeHost() : nullptr;
		if (Host && (Host->IsActionMovementOwned() || Host->IsMontageRootMotionOwned()))
		{
			// 租约过期但胶囊已经有新主人：不改它的模式，也不替它起步。
			if (bGrounded) { Super::PhysWalking(DeltaTime, Iterations); }
			else { Super::PhysFlying(DeltaTime, Iterations); }
			return;
		}
		if (bGrounded)
		{
			Super::PhysWalking(DeltaTime, Iterations);
			return;
		}
		SetMovementMode(MOVE_Falling);
		StartNewPhysics(DeltaTime, Iterations);
		return;
	}

	const FRecordedLocalTrajectoryProfile& Profile = RecordedTrajectory.RecordedTrajectory;
	const bool bLandingAllowed =
		RecordedTrajectory.TrajectoryGroundPolicy == ERecordedTrajectoryGroundPolicy::LandingAirborne;

	float RemainingTime = DeltaTime;
	while (RemainingTime >= MIN_TICK_TIME && Iterations < MaxSimulationIterations
		&& IsRecordedTrajectoryOwnerCurrent(Lease))
	{
		const float Step = GetSimulationTimeStep(RemainingTime, Iterations);
		++Iterations;
		const float StepEnd = RecordedTrajectoryElapsed + Step;

		// 本子步位移 = Curve(t1) - Curve(t0)。绝不向「起点 + 累计目标位置」追赶：
		// 蹭墙掉下来的位移一旦在下一帧补回去，就是穿墙或蹭墙上升。
		const FVector LocalDelta = RecordedTrajectory.GetRecordedTrajectoryOffset(StepEnd)
			- RecordedTrajectory.GetRecordedTrajectoryOffset(RecordedTrajectoryElapsed);
		const FVector LocalEndVelocity = RecordedTrajectory.GetRecordedTrajectoryVelocity(StepEnd);
		FVector Delta = MHGZRecordedLocalTrajectory::LocalToWorld(LocalDelta,
			RecordedTrajectory.DirectionSnapshot);
		FVector EndVelocity = MHGZRecordedLocalTrajectory::LocalToWorld(LocalEndVelocity,
			RecordedTrajectory.DirectionSnapshot);
		if (bGrounded)
		{
			// §5.3：203 的竖直记录是胶囊跟随落地地形的抖动，不还原成升降。
			Delta.Z = 0.0;
			EndVelocity.Z = 0.0;
		}

		// 只去掉指向已接触面的分量；切向分量与相位时钟都保留。
		for (const FVector& Normal : RecordedContactNormals)
		{
			Delta -= Normal * FMath::Min(0.0, FVector::DotProduct(Delta, Normal));
			EndVelocity -= Normal * FMath::Min(0.0, FVector::DotProduct(EndVelocity, Normal));
		}
		Velocity = EndVelocity;
		bJustTeleported = false;

		FHitResult Hit(1.0f);
		const auto LandAtContact = [&](const float UsedStepFraction)
		{
			if (!bLandingAllowed || Delta.Z >= 0.0
				|| !IsValidLandingSpot(UpdatedComponent->GetComponentLocation(), Hit))
			{
				return false;
			}
			bRecordedTrajectoryActive = false;
			RecordAerialMovementEvent(TEXT("RecordedTrajectory.Landed"));
			ProcessLanded(Hit, RemainingTime - Step * UsedStepFraction, Iterations);
			return true;
		};

		SafeMoveUpdatedComponent(Delta, UpdatedComponent->GetComponentQuat(), true, Hit);
		// 碰撞回调可能取消本动作或装上另一条租约。
		if (!IsRecordedTrajectoryOwnerCurrent(Lease)) { return; }
		if (!bGrounded && MovementMode != MOVE_Flying) { return; }
		if (Hit.IsValidBlockingHit())
		{
			if (LandAtContact(Hit.Time)) { return; }
			const FVector Normal = Hit.Normal.GetSafeNormal();
			RecordedContactNormals.AddUnique(Normal);
			HandleImpact(Hit, Step, Delta);
			if (!IsRecordedTrajectoryOwnerCurrent(Lease)) { return; }
			if (!bGrounded && MovementMode != MOVE_Flying) { return; }
			float UsedFraction = Hit.Time;
			float SlideFraction = 1.0f - Hit.Time;
			FVector SlideDelta = ComputeSlideVector(Delta, SlideFraction, Normal, Hit);
			for (int32 SlideIndex = 0; SlideIndex < 2 && !SlideDelta.IsNearlyZero(1.e-3f)
				&& FVector::DotProduct(SlideDelta, Delta) > 0.0; ++SlideIndex)
			{
				SafeMoveUpdatedComponent(SlideDelta, UpdatedComponent->GetComponentQuat(),
					true, Hit);
				if (!IsRecordedTrajectoryOwnerCurrent(Lease)) { return; }
				if (!bGrounded && MovementMode != MOVE_Flying) { return; }
				UsedFraction += SlideFraction * Hit.Time;
				const float AppliedSlice = Step * SlideFraction * Hit.Time;
				SlideFraction *= 1.0f - Hit.Time;
				if (!Hit.IsValidBlockingHit()) { break; }
				if (LandAtContact(FMath::Clamp(UsedFraction, 0.0f, 1.0f))) { return; }
				RecordedContactNormals.AddUnique(Hit.Normal.GetSafeNormal());
				HandleImpact(Hit, AppliedSlice, SlideDelta);
				if (!IsRecordedTrajectoryOwnerCurrent(Lease)) { return; }
				if (!bGrounded && MovementMode != MOVE_Flying) { return; }
				if (SlideIndex == 0) { TwoWallAdjust(SlideDelta, Hit, Normal); }
			}
			for (const FVector& ContactNormal : RecordedContactNormals)
			{
				EndVelocity -= ContactNormal * FMath::Min(0.0,
					FVector::DotProduct(EndVelocity, ContactNormal));
			}
			Velocity = EndVelocity;
		}

		RecordedTrajectoryElapsed = StepEnd;
		RemainingTime -= Step;
	}

	if (IsRecordedTrajectoryDurationElapsed() && !bRecordedTrajectoryCompleted)
	{
		bRecordedTrajectoryCompleted = true;
		RecordAerialMovementEvent(TEXT("RecordedTrajectory.Completed"), nullptr, RemainingTime);
		if (bGrounded)
		{
			// 地面相位跑完就停在地上，剩余的 dt 交给引擎的 Walking 去跑摩擦。
			Velocity = FVector(0.0, 0.0, Velocity.Z);
		}
		// 空中相位跑完时**不动模式、也不撤租约**：胶囊停在 MOVE_Flying，一直等到租约的
		// 下一个持有者接手，或由任务收尾时真正归还。
		//
		// 速度在覆盖终点是**零**（HoldLastKey 的语义就是「路走完了」），不是曲线的终末切线；
		// 曲线上真正的终末切线由尾段模型另给，只在 IntegrateTerminalModel 下参与积分。
		//
		// 这两件事都不能在这里做。改回 Falling 会让起手段与下潜段的交界空出一帧自由
		// 落体 —— 任务观察到「已完成」必然晚于本次物理更新，那一帧就没有人驱动胶囊。
		// 撤掉租约同样糟：同一帧剩余的物理更新会落回 `Super::PhysFlying`，白送一帧重力。
	}
}

void UMHGZInstrumentedCharacterMovementComponent::PhysFlying(float DeltaTime, int32 Iterations)
{
	if (bRecordedTrajectoryActive
		&& RecordedTrajectory.TrajectoryGroundPolicy
			!= ERecordedTrajectoryGroundPolicy::Grounded)
	{
		PhysRecordedTrajectory(DeltaTime, Iterations, false);
		return;
	}

	if (!bSegmentedMovementActive)
	{
		Super::PhysFlying(DeltaTime, Iterations);
		return;
	}

	const uint64 Lease = SegmentedMovementLease;
	if (!IsSegmentedMovementOwnerCurrent(Lease))
	{
		RetireSegmentedDirectionalMovement(Lease);
		const AMHGZCharacter* Avatar = Cast<AMHGZCharacter>(CharacterOwner);
		const UMHGZWeaponRuntimeHostComponent* Host = Avatar ? Avatar->GetWeaponRuntimeHost() : nullptr;
		if (Host && (Host->IsActionMovementOwned() || Host->IsMontageRootMotionOwned()))
		{
			// The lease is stale but another action owns Flying now. Do not
			// change its mode or launch; its RMS/ordinary physics remains authority.
			Super::PhysFlying(DeltaTime, Iterations);
			return;
		}
		SetMovementMode(MOVE_Falling);
		StartNewPhysics(DeltaTime, Iterations);
		return;
	}

	float RemainingTime = DeltaTime;
	while (RemainingTime >= MIN_TICK_TIME && Iterations < MaxSimulationIterations
		&& IsSegmentedMovementOwnerCurrent(Lease) && MovementMode == MOVE_Flying)
	{
		const FSegmentedDirectionalMovementProfile& Profile = SegmentedMovement.SegmentedProfile;
		const float Boundary = Profile.GetNextBoundarySeconds(SegmentedMovementElapsed);
		const float Step = FMath::Min(GetSimulationTimeStep(RemainingTime, Iterations),
			Boundary - SegmentedMovementElapsed);
		if (Step <= SMALL_NUMBER)
		{
			SegmentedMovementElapsed = Boundary;
		}
		else
		{
			++Iterations;
			const float StepEnd = SegmentedMovementElapsed + Step;
			const FVector Direction = SegmentedMovement.DirectionSnapshot;
			FVector Delta = Profile.GetOffset(Direction, StepEnd)
				- Profile.GetOffset(Direction, SegmentedMovementElapsed);
			FVector EndVelocity = Profile.GetVelocity(Direction, StepEnd);
			// A blocking plane removes only the into-wall component. Do not reset
			// the phase clock, step up the wall, or relaunch into it at 188 entry.
			for (const FVector& Normal : SegmentedContactNormals)
			{
				Delta -= Normal * FMath::Min(0.0, FVector::DotProduct(Delta, Normal));
				EndVelocity -= Normal * FMath::Min(0.0, FVector::DotProduct(EndVelocity, Normal));
			}
			Velocity = EndVelocity;
			bJustTeleported = false;
			FHitResult Hit(1.0f);
			const auto LandAtContact = [&](const float UsedStepFraction)
			{
				if (Delta.Z >= 0.0 || !IsValidLandingSpot(UpdatedComponent->GetComponentLocation(), Hit))
				{
					return false;
				}
				bSegmentedMovementActive = false;
				RecordAerialMovementEvent(TEXT("SegmentedDash.Landed"));
				ProcessLanded(Hit, RemainingTime - Step * UsedStepFraction, Iterations);
				return true;
			};
			SafeMoveUpdatedComponent(Delta, UpdatedComponent->GetComponentQuat(), true, Hit);
			// Collision callbacks can cancel this action or install another task.
			if (!IsSegmentedMovementOwnerCurrent(Lease) || MovementMode != MOVE_Flying) { return; }
			if (Hit.IsValidBlockingHit())
			{
				if (LandAtContact(Hit.Time)) { return; }
				const FVector Normal = Hit.Normal.GetSafeNormal();
				SegmentedContactNormals.AddUnique(Normal);
				HandleImpact(Hit, Step, Delta);
				if (!IsSegmentedMovementOwnerCurrent(Lease) || MovementMode != MOVE_Flying) { return; }
				float UsedFraction = Hit.Time;
				float SlideFraction = 1.0f - Hit.Time;
				FVector SlideDelta = ComputeSlideVector(Delta, SlideFraction, Normal, Hit);
				// Same slide/two-wall geometry as the engine, but inspect EVERY
				// secondary sweep before it can overwrite a floor hit. The stock
				// Flying slide does not call ProcessLanded on these contacts.
				for (int32 SlideIndex = 0; SlideIndex < 2 && !SlideDelta.IsNearlyZero(1.e-3f)
					&& FVector::DotProduct(SlideDelta, Delta) > 0.0; ++SlideIndex)
				{
					SafeMoveUpdatedComponent(SlideDelta, UpdatedComponent->GetComponentQuat(), true, Hit);
					if (!IsSegmentedMovementOwnerCurrent(Lease) || MovementMode != MOVE_Flying) { return; }
					UsedFraction += SlideFraction * Hit.Time;
					const float AppliedSlice = Step * SlideFraction * Hit.Time;
					SlideFraction *= 1.0f - Hit.Time;
					if (!Hit.IsValidBlockingHit()) { break; }
					if (LandAtContact(FMath::Clamp(UsedFraction, 0.0f, 1.0f))) { return; }
					SegmentedContactNormals.AddUnique(Hit.Normal.GetSafeNormal());
					HandleImpact(Hit, AppliedSlice, SlideDelta);
					if (!IsSegmentedMovementOwnerCurrent(Lease) || MovementMode != MOVE_Flying) { return; }
					if (SlideIndex == 0) { TwoWallAdjust(SlideDelta, Hit, Normal); }
				}
				for (const FVector& ContactNormal : SegmentedContactNormals)
				{
					EndVelocity -= ContactNormal * FMath::Min(0.0,
						FVector::DotProduct(EndVelocity, ContactNormal));
				}
				Velocity = EndVelocity;
			}
			SegmentedMovementElapsed = StepEnd;
			RemainingTime -= Step;
		}

		if (SegmentedMovementElapsed >= Profile.GetDuration() - SMALL_NUMBER)
		{
			// Host installed 188's physical profile before 186. Keep the analytic
			// terminal tangent and consume any leftover dt as Falling RIGHT NOW.
			// Task ticks only observe completion; they never rewind this velocity.
			bSegmentedMovementActive = false;
			bSegmentedMovementCompleted = true;
			RecordAerialMovementEvent(TEXT("SegmentedDash.ToFalling"), nullptr, RemainingTime);
			SetMovementMode(MOVE_Falling);
			StartNewPhysics(RemainingTime, Iterations);
			return;
		}
	}
}

void UMHGZInstrumentedCharacterMovementComponent::CalcVelocity(const float DeltaTime,
	const float Friction, const bool bFluid, const float BrakingDeceleration)
{
	const auto CalculateVelocity = [&]()
	{
		if (MovementMode == MOVE_Falling && !AerialPlanarAcceleration.IsZero()
			&& !CurrentRootMotion.HasOverrideVelocity() && DeltaTime >= MIN_TICK_TIME
			&& HasValidData()
			&& !(CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy
				&& !bWasSimulatingRootMotion))
		{
			// This authored acceleration replaces planar input/drag and is not
			// constrained by locomotion's speed cap. PhysFalling owns gravity and
			// midpoint position integration. Its wall/no-air-control recomputation
			// restores OldVelocity first, so this same branch must run there too:
			// it computes a second baseline rather than adding twice to live velocity.
			Velocity += AerialPlanarAcceleration * DeltaTime;
			return;
		}
		Super::CalcVelocity(DeltaTime, Friction, bFluid, BrakingDeceleration);
	};
	if (!ShouldCaptureMovementSubPhase())
	{
		CalculateVelocity();
		return;
	}

	const FVector EntryVelocity = Velocity;
	const FName PreviousSite = ActiveMovementSubPhase;
	ActiveMovementSubPhase = FName(TEXT("CMC.CalcVelocity"));
	CaptureSubPhaseSample(FName(TEXT("CMC.CalcVelocity.Pre")), DeltaTime, EntryVelocity);
	CalculateVelocity();
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
