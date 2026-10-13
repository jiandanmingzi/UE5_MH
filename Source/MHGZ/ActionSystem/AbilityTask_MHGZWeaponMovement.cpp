// Copyright MHGZ Project. All Rights Reserved.

#include "ActionSystem/AbilityTask_MHGZWeaponMovement.h"

#include "ActionSystem/MHGZGameplayAbility.h"
#include "ActionSystem/MHGZWeaponMovementCurves.h"
#include "Components/CapsuleComponent.h"
#include "Curves/CurveFloat.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/RootMotionSource.h"
#include "MHGZ.h"
#include "MHGZCharacter.h"
#include "Movement/MHGZInstrumentedCharacterMovementComponent.h"
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
	bOwnsActionMovement = true;

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
	if (MovementRequest.Mode == EWeaponMovementMode::StationaryHover)
	{
		// A superseded vault can leave a deferred finish velocity or a blending
		// montage root on the next movement frame. The 186 lead owns zero motion
		// continuously, not just on its activation frame.
		CMC->Velocity = FVector::ZeroVector;
		CMC->SetMovementMode(MOVE_Flying);
	}

	UpdateActionRotation(DeltaTime);
	LastLocation = AvatarCharacter->GetActorLocation();
	if (MovementRequest.Mode == EWeaponMovementMode::SegmentedDirectional)
	{
		const UMHGZInstrumentedCharacterMovementComponent* InstrumentedCMC =
			Cast<UMHGZInstrumentedCharacterMovementComponent>(CMC);
		if (InstrumentedCMC && InstrumentedCMC->HasSegmentedMovementCompleted(SegmentedMovementLease))
		{
			FinishMovement(EWeaponMovementEndReason::Completed);
		}
		else if (!InstrumentedCMC || !InstrumentedCMC->IsSegmentedMovementActive(SegmentedMovementLease)
			|| CMC->MovementMode != MOVE_Flying)
		{
			FinishMovement(CMC->IsMovingOnGround() ? EWeaponMovementEndReason::Landed
				: EWeaponMovementEndReason::Interrupted);
		}
		return;
	}
	if (MovementRequest.Mode == EWeaponMovementMode::RecordedLocalTrajectory)
	{
		const UMHGZInstrumentedCharacterMovementComponent* InstrumentedCMC =
			Cast<UMHGZInstrumentedCharacterMovementComponent>(CMC);
		// 每个接地策略都有自己的承载模式：空中两段是 MOVE_Flying，203 是 MOVE_Walking。
		// 两种都要查。CMC 只在模式对得上时才会走进录制轨迹那段物理；模式一歪，相位时钟
		// 就冻住、永远不报完成，而任务在这里不察觉的话整个动作会挂着锁不放。
		const EMovementMode ExpectedMode =
			MovementRequest.TrajectoryGroundPolicy == ERecordedTrajectoryGroundPolicy::Grounded
			? MOVE_Walking : MOVE_Flying;
		if (InstrumentedCMC
			&& InstrumentedCMC->HasRecordedTrajectoryMovementCompleted(RecordedTrajectoryLease))
		{
			FinishMovement(EWeaponMovementEndReason::Completed);
		}
		else if (!InstrumentedCMC
			|| !InstrumentedCMC->IsRecordedTrajectoryMovementActive(RecordedTrajectoryLease)
			|| CMC->MovementMode != ExpectedMode)
		{
			// 租约在任务观察之前就被拿掉，或者胶囊已经不在本相位该在的模式里：
			// 前者是 CMC 认领了真实触地（胶囊已经站住）或别的动作抢了位移所有权，
			// 后者是模式被第三方改掉（跳跃、击退、蓝图 SetMovementMode）。
			// 问地面，不拿模式当代理指标。
			FinishMovement(CMC->IsMovingOnGround() ? EWeaponMovementEndReason::Landed
				: EWeaponMovementEndReason::Interrupted);
		}
		return;
	}
	if (MovementRequest.Mode == EWeaponMovementMode::AuthoredAerialPhysics)
	{
		if (CMC->MovementMode != MOVE_Falling)
		{
			FinishMovement(CMC->IsMovingOnGround()
				? EWeaponMovementEndReason::Landed
				: EWeaponMovementEndReason::Interrupted);
			return;
		}

		ElapsedMovementTime += FMath::Max(0.0f, DeltaTime);
		if (ElapsedMovementTime >= ResolvedDuration)
		{
			FinishMovement(EWeaponMovementEndReason::Completed);
		}
		return;
	}

	const TSharedPtr<FRootMotionSource> Source =
		CMC->GetRootMotionSourceByID(RootMotionSourceID);

	if (!Source.IsValid() || Source->Status.HasFlag(ERootMotionSourceStatusFlags::Finished))
	{
		FinishMovement(EWeaponMovementEndReason::Completed);
	}
}

bool UAbilityTask_MHGZWeaponMovement::EndHoverForImmediateMovementHandoff(
	const FWeaponActionToken& ActionToken)
{
	return MovementRequest.Mode == EWeaponMovementMode::StationaryHover
		&& EndForImmediateMovementHandoff(ActionToken);
}

bool UAbilityTask_MHGZWeaponMovement::EndForImmediateMovementHandoff(
	const FWeaponActionToken& ActionToken)
{
	if (!bStarted || bFinished || bPreserveFlyingForImmediateHandoff
		|| (MovementRequest.Mode != EWeaponMovementMode::StationaryHover
			&& MovementRequest.Mode != EWeaponMovementMode::CurvedDirectional
			&& MovementRequest.Mode != EWeaponMovementMode::SegmentedDirectional
			&& MovementRequest.Mode != EWeaponMovementMode::RecordedLocalTrajectory
			&& MovementRequest.Mode != EWeaponMovementMode::AuthoredAerialPhysics)
		|| MovementRequest.OwnerAction != ActionToken
		|| !RuntimeHost.IsValid()
		|| !RuntimeHost->IsActionMovementOwnedBy(ActionToken))
	{
		return false;
	}

	// RMS removal is deferred until the next CMC update. Neutralize this exact
	// retiring source's finish write before installing the new launch; otherwise
	// the hover's SetVelocity(0) wipes 189's freshly installed 1080 cm/s lift.
	if (UCharacterMovementComponent* CMC = MovementComponent.Get())
	{
		if (TSharedPtr<FRootMotionSource> Source = CMC->GetRootMotionSourceByID(RootMotionSourceID))
		{
			Source->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::MaintainLastRootMotionVelocity;
		}
	}
	// Never emit an intermediate Walking/fake landing while replacing this task.
	bPreserveFlyingForImmediateHandoff = true;
	EndTask();
	return true;
}

bool UAbilityTask_MHGZWeaponMovement::EnableRecordedTrajectoryLanding(
	const FWeaponActionToken& ActionToken)
{
	if (!bStarted || bFinished
		|| MovementRequest.Mode != EWeaponMovementMode::RecordedLocalTrajectory
		|| MovementRequest.OwnerAction != ActionToken
		|| MovementRequest.TrajectoryGroundPolicy
			!= ERecordedTrajectoryGroundPolicy::ProtectedFlight)
	{
		return false;
	}
	UMHGZInstrumentedCharacterMovementComponent* CMC =
		Cast<UMHGZInstrumentedCharacterMovementComponent>(MovementComponent.Get());
	UMHGZWeaponRuntimeHostComponent* Host = RuntimeHost.Get();
	if (!CMC || !Host || !Host->IsActionMovementOwnedBy(ActionToken)
		|| !CMC->EnableRecordedTrajectoryLanding(RecordedTrajectoryLease))
	{
		return false;
	}
	MovementRequest.TrajectoryGroundPolicy =
		ERecordedTrajectoryGroundPolicy::LandingAirborne;
	return true;
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

	const bool bStillOwnsMovement = OwnsMovement();
	if (!bFinished && bStillOwnsMovement && !bPreserveFlyingForImmediateHandoff)
	{
		// Ability cancellation, weapon replacement and Runtime shutdown may end a
		// task without a gameplay result callback. They still obey the request's
		// explicit velocity policy before their RootMotionSource is removed.
		ApplyFinishVelocityPolicy();
	}
	// 这条路上没有 FinishMovement，所以兜底要单独走一次：取消掉的舞踏若停在
	// MOVE_Flying，没有重力会把它带下来。MovementMode == MOVE_Flying 的判断让它
	// 天然幂等 —— FinishMovement 已经释放过时这里是 no-op。
	RetireSegmentedMovement();
	RetireRecordedTrajectory();
	if (bStillOwnsMovement && !bFinished && !bPreserveFlyingForImmediateHandoff)
	{
		bool bWasFlying = false;
		EnsureVaultFlightReleased(bWasFlying);
	}
	RemoveRootMotionSource();
	ReleaseMovementOwnership();
	Super::OnDestroy(AbilityIsEnding);
}

bool UAbilityTask_MHGZWeaponMovement::EnsureVaultFlightReleased(bool& bOutWasFlying)
{
	bOutWasFlying = false;
	UCharacterMovementComponent* CMC = MovementComponent.Get();
	if (!CMC)
	{
		return false;
	}
	if (CMC->MovementMode == MOVE_Flying)
	{
		bOutWasFlying = true;
		// Decide from a real floor BEFORE changing mode. SetDefaultMovementMode
		// first enters Walking then falls back, which emits a false grounded event
		// and restores the Host profile (the recorded Miss188 1g/80 regression).
		FFindFloorResult Floor;
		CMC->FindFloor(Character.IsValid() ? Character->GetActorLocation() : LastLocation,
			Floor, false);
		const bool bOnFloor = Floor.IsWalkableFloor()
			&& Floor.FloorDist <= UCharacterMovementComponent::MAX_FLOOR_DIST;
		CMC->SetMovementMode(bOnFloor ? MOVE_Walking : MOVE_Falling);
	}
	// 直接问地面，不拿模式当代理指标 —— 这个项目已经在这上面栽过两次。
	//
	// SetDefaultMovementMode 只在 `MOVE_Walking && GetMovementBase() == NULL` 时才回退
	// 到 Falling（CharacterMovementComponent.cpp:1308-1312）。也就是说：只要胶囊身上
	// 还挂着**旧的 MovementBase**，它就会停在 Walking，于是 IsMovingOnGround() 对一个
	// 悬空胶囊读作 true —— 而本函数的返回值决定 FinishMovement 把 `Completed` 翻成
	// `Landed`（⇒ 认领落地姿势、不给自由落体），一次误判就能让悬空的弧尾错认触地。
	//
	// `CurrentFloor` 正是 SetMovementMode 内部那次 FindFloor 留下的结果，
	// `IsWalkableFloor()` = `bBlockingHit && bWalkableFloor`，问它才是问对了问题。
	return CMC->IsMovingOnGround() && CMC->CurrentFloor.IsWalkableFloor();
}

void UAbilityTask_MHGZWeaponMovement::HandleLanded(const FHitResult& Hit)
{
	// 两类 vault 都认。这是「弧走完时胶囊在半空吗？」这个问题的**直接**回答 ——
	// CMC 说它落地了，那就是落地了。此前只有 BallisticVault 收，于是后撑杆跳
	// 的触地被丢掉，只能靠 BackVaultResidualFallSeconds 这个几何代理指标去猜。
	//
	// CurvedVault 在弧中段仍由源驱动；此时若发生触地，说明 authored 的路径提前
	// 够到了地面，如实上报 Landed 比让它继续跑更接近事实。
	// 录制轨迹只有「空中下潜」那一种接地策略认领触地；起手段的保护飞行段
	// 明确不接 —— 它脚下的地面不是它的事，等曲线跑完由下一相位接手。
	const bool bRecordedLanding = MovementRequest.Mode == EWeaponMovementMode::RecordedLocalTrajectory
		&& MovementRequest.TrajectoryGroundPolicy == ERecordedTrajectoryGroundPolicy::LandingAirborne;
	if (!bStarted || bFinished
		|| (!bRecordedLanding
			&& MovementRequest.Mode != EWeaponMovementMode::BallisticVault
			&& MovementRequest.Mode != EWeaponMovementMode::CurvedVault
			&& MovementRequest.Mode != EWeaponMovementMode::CurvedDirectional
			&& MovementRequest.Mode != EWeaponMovementMode::SegmentedDirectional
			&& MovementRequest.Mode != EWeaponMovementMode::AuthoredAerialPhysics))
	{
		return;
	}

	FinishMovement(EWeaponMovementEndReason::Landed, &Hit);
}

void UAbilityTask_MHGZWeaponMovement::HandleCapsuleHit(UPrimitiveComponent* HitComponent,
	AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse,
	const FHitResult& Hit)
{
	if (MovementRequest.Mode == EWeaponMovementMode::CurvedDirectional
		&& bStarted && !bFinished && Hit.bBlockingHit)
	{
		// Keep the 187 source and probe alive through their authored duration;
		// the CMC resolves wall contact by sliding, like the air-dodge vault.
		// Ground contact is owned separately by the Landed delegate.
		UCharacterMovementComponent* CMC = MovementComponent.Get();
		if (CMC && !CMC->IsWalkable(Hit))
		{
			bDirectionalHitWall = true;
			DirectionalWallHit = Hit;
			// A natural RMS expiry can apply FinishVelocity before TickTask sees it.
			// Do not re-apply the authored into-wall terminal tangent on that frame.
			if (TSharedPtr<FRootMotionSource> Source =
				CMC->GetRootMotionSourceByID(RootMotionSourceID))
			{
				Source->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::SetVelocity;
				Source->FinishVelocityParams.SetVelocity = FVector::ZeroVector;
			}
		}
		return;
	}

	// Deliberately does nothing.  Sliding along whatever the capsule meets is the
	// CMC's job, not this task's, and it already does it for every source mode:
	// PhysFlying and PhysFalling both run
	//     SafeMoveUpdatedComponent(...) -> HandleImpact(...) -> SlideAlongSurface(...)
	// on any blocking hit, and no FRootMotionSource moves the component itself --
	// JumpForce/MoveToForce only compute a Force and hand it to the CMC.
	//
	// This used to FinishMovement(BlockingHit) here, which pre-empted that slide by
	// removing the source.  The body was then left with whatever velocity it had at
	// the instant of the hit (CancelVelocityPolicy::PreserveVelocity makes
	// ApplyFinishVelocityPolicy a no-op, and the engine only rewrites Velocity for
	// ClampVelocity/SetVelocity), in a MovementMode nothing restored, with
	// ResolveVaultExit returning None -- so it flew on unowned.  Measured: a dance
	// vault that grazed the training dummy 1-3 frames after launch kept ~1309 cm/s
	// of climb and topped out at 971 cm instead of the configured 564.
	//
	// Ground contact still ends a BallisticVault, via the Landed delegate
	// (HandleLanded) rather than here.
	(void)HitComponent; (void)OtherActor; (void)OtherComp; (void)NormalImpulse; (void)Hit;
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

	case EWeaponMovementMode::CurvedDirectional:
	{
		float TerminalSlope = 0.0f;
		return FMath::IsFinite(MovementRequest.Duration) && MovementRequest.Duration > 0.0f
			&& FMath::IsFinite(MovementRequest.MaxDistance) && MovementRequest.MaxDistance > 0.0f
			&& !MovementRequest.DirectionSnapshot.ContainsNaN()
			&& !MovementRequest.DirectionSnapshot.IsNearlyZero()
			&& MHGZWeaponMovementCurves::ValidateNormalizedProgressCurve(
				MovementRequest.DistanceCurve, TerminalSlope);
	}
	case EWeaponMovementMode::StationaryHover:
		return FMath::IsFinite(MovementRequest.Duration)
			&& MovementRequest.Duration > 0.0f;

	case EWeaponMovementMode::BallisticVault:
		return MovementRequest.HasValidBallisticParameters();

	case EWeaponMovementMode::CurvedVault:
		return MovementRequest.HasValidCurvedVaultParameters();

	case EWeaponMovementMode::AuthoredAerialPhysics:
		return MovementRequest.HasValidAuthoredAerialPhysicsParameters();
	case EWeaponMovementMode::SegmentedDirectional:
		return MovementRequest.HasValidSegmentedDirectionalParameters();
	case EWeaponMovementMode::RecordedLocalTrajectory:
		return MovementRequest.HasValidRecordedTrajectoryParameters();

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
	case EWeaponMovementMode::CurvedDirectional:
		return ApplyCurvedDirectionalSource();
	case EWeaponMovementMode::StationaryHover:
		return ApplyStationaryHoverSource();
	case EWeaponMovementMode::BallisticVault:
		return ApplyBallisticVaultSource();
	case EWeaponMovementMode::CurvedVault:
		return ApplyCurvedVaultSource();
	case EWeaponMovementMode::AdditiveInertia:
		return ApplyAdditiveInertiaSource();
	case EWeaponMovementMode::AuthoredAerialPhysics:
		return ApplyAuthoredAerialPhysics();
	case EWeaponMovementMode::SegmentedDirectional:
		return ApplySegmentedDirectionalMovement();
	case EWeaponMovementMode::RecordedLocalTrajectory:
		return ApplyRecordedLocalTrajectoryMovement();
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

bool UAbilityTask_MHGZWeaponMovement::ApplyCurvedDirectionalSource()
{
	ACharacter* AvatarCharacter = Character.Get();
	UCharacterMovementComponent* CMC = MovementComponent.Get();
	UCurveFloat* DistanceCurve = MovementRequest.DistanceCurve;
	if (!AvatarCharacter || !CMC || !DistanceCurve)
	{
		return false;
	}

	FVector Direction = MovementRequest.DirectionSnapshot.GetSafeNormal();
	if (Direction.IsNearlyZero() || Direction.ContainsNaN())
	{
		return false;
	}

	ResolvedDuration = MovementRequest.Duration;
	TSharedPtr<FRootMotionSource_MoveToDynamicForce> Source =
	MHGZWeaponMovementCurves::MakeCurvedDirectionalSource(
		StartLocation, Direction, MovementRequest.MaxDistance, ResolvedDuration,
		DistanceCurve, MovementRequest.WarpTargetName,
		ResolvedDirectionalHandoffVelocity);
	if (!Source)
	{
		return false;
	}
	ExpectedDestination = Source->TargetLocation;
	bHasResolvedDirectionalHandoffVelocity = true;

	RootMotionSourceID = CMC->ApplyRootMotionSource(Source);
	if (RootMotionSourceID == InvalidRootMotionSourceID)
	{
		return false;
	}

	// The curve owns the full 3D trajectory. Flying prevents gravity from being
	// accumulated on top of it; FinishMovement hands the authored terminal
	// tangent back to the CMC before this task broadcasts completion.
	CMC->SetMovementMode(MOVE_Flying);
	return true;
}

bool UAbilityTask_MHGZWeaponMovement::ApplyStationaryHoverSource()
{
	UCharacterMovementComponent* CMC = MovementComponent.Get();
	if (!CMC)
	{
		return false;
	}
	ResolvedDuration = MovementRequest.Duration;
	ExpectedDestination = StartLocation;
	TSharedPtr<FRootMotionSource_ConstantForce> Source = MakeShared<FRootMotionSource_ConstantForce>();
	ConfigureCommonSource(*Source, MovementRequest.WarpTargetName, ResolvedDuration,
		ERootMotionAccumulateMode::Override, EMovementCancelVelocityPolicy::ZeroVelocity);
	Source->Force = FVector::ZeroVector;
	RootMotionSourceID = CMC->ApplyRootMotionSource(Source);
	if (RootMotionSourceID == InvalidRootMotionSourceID)
	{
		return false;
	}
	CMC->SetMovementMode(MOVE_Flying);
	CMC->Velocity = FVector::ZeroVector;
	return true;
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

	// Flying，不是 Falling。
	//
	// PhysFlying 不跑重力、不 FindFloor、不 ProcessLanded，override 源活跃时它的
	// CalcVelocity 也被跳过（所以 MaxFlySpeed 不会钳住这条弧）。这正是后撑杆跳
	// 一直在用的手法 —— ApplyCurvedVaultSource 的 Flying 是为了让 authored 的
	// 落地蹲姿不被读成落地；舞踏此前用 Falling，代价是实测到的一次误判：
	// 起飞前胶囊已在 Falling，于是这次 SetMovementMode 成了 no-op，CMC 立刻找到
	// 脚下地面，ProcessLanded → LandedDelegate → 动作 3 帧后被 Landed 收尾，
	// 源算出的 force（1373.75，完全正确）根本没进 Velocity。
	//
	// 胶囊由 UAnimNotify_IG_AerialHandoff 在最早可操作帧交给 CMC；
	// 没走到那一步的由 EnsureVaultFlightReleased() 兜底。
	CMC->SetMovementMode(MOVE_Flying);

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
	ExpectedDestination = StartLocation + Direction * MovementRequest.MaxDistance
		+ FVector(0.0f, 0.0f, MovementRequest.CurvedVaultEndHeight);
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

bool UAbilityTask_MHGZWeaponMovement::ApplySegmentedDirectionalMovement()
{
	UMHGZInstrumentedCharacterMovementComponent* CMC =
		Cast<UMHGZInstrumentedCharacterMovementComponent>(MovementComponent.Get());
	if (!CMC || !MovementRequest.HasValidSegmentedDirectionalParameters()) { return false; }
	ResolvedDuration = MovementRequest.SegmentedProfile.GetDuration();
	RootMotionSourceID = InvalidRootMotionSourceID;
	ExpectedDestination = StartLocation + MovementRequest.SegmentedProfile.GetOffset(
		MovementRequest.DirectionSnapshot, ResolvedDuration);
	SegmentedMovementLease = CMC->BeginSegmentedDirectionalMovement(MovementRequest);
	return SegmentedMovementLease != 0;
}

bool UAbilityTask_MHGZWeaponMovement::ApplyRecordedLocalTrajectoryMovement()
{
	UMHGZInstrumentedCharacterMovementComponent* CMC =
		Cast<UMHGZInstrumentedCharacterMovementComponent>(MovementComponent.Get());
	if (!CMC || !MovementRequest.HasValidRecordedTrajectoryParameters()) { return false; }
	ResolvedDuration = MovementRequest.RecordedTrajectory.GetDuration();
	RootMotionSourceID = InvalidRootMotionSourceID;
	// 预告落点只是给 MotionWarping 的调试目标；曲线模式的位移完全由 CMC 逐子步施加，
	// 不经过任何 RMS，所以这里读的是同一份 authored 数据的终值。
	ExpectedDestination = StartLocation + MHGZRecordedLocalTrajectory::LocalToWorld(
		MovementRequest.GetRecordedTrajectoryOffset(ResolvedDuration),
		MovementRequest.DirectionSnapshot);
	RecordedTrajectoryLease = CMC->BeginRecordedTrajectoryMovement(MovementRequest);
	return RecordedTrajectoryLease != 0;
}

bool UAbilityTask_MHGZWeaponMovement::ApplyAuthoredAerialPhysics()
{
	UCharacterMovementComponent* CMC = MovementComponent.Get();
	if (!CMC || !MovementRequest.HasValidAuthoredAerialPhysicsParameters())
	{
		return false;
	}

	ResolvedDuration = MovementRequest.Duration;
	ElapsedMovementTime = 0.0f;
	RootMotionSourceID = InvalidRootMotionSourceID;
	const FVector Acceleration = MovementRequest.PlanarAcceleration
		+ FVector(0.0, 0.0, -MovementRequest.GravityMagnitude);
	const double Time = ResolvedDuration;
	ExpectedDestination = StartLocation + MovementRequest.LaunchVelocity * Time
		+ 0.5 * Acceleration * Time * Time;

	// Dance is an authored physical arc, not an RMS path. Install Falling first
	// (the preceding stationary-hold task left CMC in Flying), then its captured
	// launch tangent. From here CMC owns gravity, planar acceleration, sweeps,
	// wall sliding, and the real Landed callback without an RMS partial-end tick.
	if (!FMath::IsNearlyEqual(FMath::Abs(CMC->GetGravityZ()),
		MovementRequest.GravityMagnitude, 1.0f))
	{
		return false;
	}
	CMC->SetMovementMode(MOVE_Falling);
	CMC->Velocity = MovementRequest.LaunchVelocity;
	return true;
}

void UAbilityTask_MHGZWeaponMovement::UpdateActionRotation(const float DeltaTime)
{
	if (MovementRequest.RotationPolicy == EActionRotationPolicy::Locked)
	{
		return;
	}
	if (UMHGZWeaponRuntimeHostComponent* Host = RuntimeHost.Get();
		Host && Host->IsActionRotationOverrideOwnedBy(MovementRequest.OwnerAction))
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
			const FVector InputDirection = MHGZCharacter
				->GetCurrentMoveInputWorldDirection().GetSafeNormal2D();
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

	// 弧尾仍在 MOVE_Flying 时，是**任务**在替 CMC 决定这次收尾算哪种 ——
	// 因为没有通知把胶囊交还给 CMC（蒙太奇没挂通知，或蒙太奇在最早可操作帧
	// 之前就被取消了）。此时唯一可靠的问法是让 CMC 重查地面，而不是猜模式。
	//
	// 若胶囊早已离开 Flying（通知切过），那条路上 CMC 自己的 LandedDelegate 才是
	// 事实来源，这里不去二次猜测 —— 那会重新引入「用代理指标覆盖直接观测」的老毛病。
	EWeaponMovementEndReason EffectiveReason = Reason;
	const bool bOwnedMovementAtFinish = OwnsMovement();
	const bool bRecordedTrajectory =
		MovementRequest.Mode == EWeaponMovementMode::RecordedLocalTrajectory;
	// Retire before broadcasting, not later in OnDestroy: callbacks can install
	// another same-action task synchronously. That replacement owns its mode.
	RetireSegmentedMovement();
	RetireRecordedTrajectory();
	if (bOwnedMovementAtFinish && bRecordedTrajectory)
	{
		// 曲线模式的触地**只**由 CMC 的真实 `ProcessLanded` 上报 —— 它同步走到
		// HandleLanded，带着真实 HitResult。这里绝不能把 Completed 翻成 Landed：
		// 起手段跑完时胶囊本来就在半空，一次 FindFloor 命中地面会把「起手结束」
		// 误报成「落地」，于是 201 还没开始就被 203 顶掉。
		//
		// 取消/中断仍然要放开 MOVE_Flying，否则被取消的动作会把胶囊永久悬停。
		if (Reason != EWeaponMovementEndReason::Completed
			&& Reason != EWeaponMovementEndReason::Landed)
		{
			bool bWasFlying = false;
			EnsureVaultFlightReleased(bWasFlying);
		}
	}
	else if (bOwnedMovementAtFinish)
	{
		bool bWasFlying = false;
		const bool bGrounded = EnsureVaultFlightReleased(bWasFlying);
		if (Reason == EWeaponMovementEndReason::Completed && bWasFlying && bGrounded)
		{
			// 弧跑完了、胶囊站在可行走地面上 ⇒ 这就是一次触地，如实上报。
			EffectiveReason = EWeaponMovementEndReason::Landed;
		}
	}

	Result.EndReason = EffectiveReason;
	Result.TravelledDistance = FVector::Dist2D(StartLocation,
		Character.IsValid() ? Character->GetActorLocation() : LastLocation);
	Result.TravelledDistance3D = FVector::Dist(StartLocation,
		Character.IsValid() ? Character->GetActorLocation() : LastLocation);
	if (UCharacterMovementComponent* CMC = MovementComponent.Get())
	{
		Result.FinalVelocity = CMC->Velocity;
	}
	if (BlockingHit)
	{
		Result.BlockingHit = *BlockingHit;
	}
	else if (bDirectionalHitWall)
	{
		Result.BlockingHit = DirectionalWallHit;
	}

	if (bOwnedMovementAtFinish && Reason != EWeaponMovementEndReason::Completed
		&& Reason != EWeaponMovementEndReason::Landed)
	{
		ApplyFinishVelocityPolicy();
		if (UCharacterMovementComponent* CMC = MovementComponent.Get())
		{
			Result.FinalVelocity = CMC->Velocity;
		}
	}

	// 用**原始** Reason 而不是 EffectiveReason：这里的语义是「录下来的那条路径
	// 跑到了它的终点」，与胶囊最后站在哪里无关 —— 白灯后撑杆跳的路径终点就在
	// 地面上，它仍然需要把 authored 的弧尾切线装上去，否则落地的最后几帧会脱节。
	const bool bCompletedCurvedVault = bOwnedMovementAtFinish && Reason == EWeaponMovementEndReason::Completed
		&& MovementRequest.Mode == EWeaponMovementMode::CurvedVault;
	const bool bCompletedCurvedDirectional = bOwnedMovementAtFinish && Reason == EWeaponMovementEndReason::Completed
		&& EffectiveReason == EWeaponMovementEndReason::Completed
		&& MovementRequest.Mode == EWeaponMovementMode::CurvedDirectional;
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
		// 模式不在这里切了 —— 归 UAnimNotify_IG_AerialHandoff 所有，兜底走
		// EnsureVaultFlightReleased()（已在函数开头调用）。这里要做的只是把
		// authored 的弧尾切线装上。
		//
		// RemoveRootMotionSource 不保证源的天然 FinishVelocity 在本回调之前装上。
		// 所以把 CurvedVault 仍拥有胶囊时采样到的精确终末切线抄上去，否则自由落体
		// 的第一帧会丢掉大部分水平与垂直速度，表现为可见的 Jump_Over → Fall 顿挫。
		if (UCharacterMovementComponent* CMC = MovementComponent.Get())
		{
			CMC->Velocity = CurvedVaultHandoffVelocity;
			Result.FinalVelocity = CurvedVaultHandoffVelocity;
		}
	}
	if (bCompletedCurvedDirectional && bHasResolvedDirectionalHandoffVelocity)
	{
		// A wall hit changes only the terminal handoff, not the dash duration.
		// Otherwise preserve the authored 187 end speed for MissFall.
		if (UCharacterMovementComponent* CMC = MovementComponent.Get())
		{
			const FVector HandoffVelocity = bDirectionalHitWall
				? FVector::ZeroVector : ResolvedDirectionalHandoffVelocity;
			CMC->Velocity = HandoffVelocity;
			Result.FinalVelocity = HandoffVelocity;
		}
	}
	RetireSegmentedMovement();
	ReleaseMovementOwnership();
	if (ShouldBroadcastAbilityTaskDelegates())
	{
		OnFinished.Broadcast(Result);
	}
	EndTask();
}

void UAbilityTask_MHGZWeaponMovement::ReleaseMovementOwnership()
{
	if (!bOwnsActionMovement) { return; }
	// Clear before callbacks; old OnDestroy must not release a replacement task
	// started synchronously by OnFinished using the same action token.
	bOwnsActionMovement = false;
	if (UMHGZWeaponRuntimeHostComponent* Host = RuntimeHost.Get())
	{
		Host->ReleaseActionMovement(MovementRequest.OwnerAction);
	}
}

bool UAbilityTask_MHGZWeaponMovement::OwnsMovement() const
{
	const UMHGZWeaponRuntimeHostComponent* Host = RuntimeHost.Get();
	return bOwnsActionMovement && Host && Host->IsActionMovementOwnedBy(MovementRequest.OwnerAction);
}

void UAbilityTask_MHGZWeaponMovement::RetireSegmentedMovement()
{
	if (SegmentedMovementLease != 0)
	{
		if (UMHGZInstrumentedCharacterMovementComponent* CMC =
			Cast<UMHGZInstrumentedCharacterMovementComponent>(MovementComponent.Get()))
		{
			CMC->RetireSegmentedDirectionalMovement(SegmentedMovementLease);
		}
		SegmentedMovementLease = 0;
	}
}

void UAbilityTask_MHGZWeaponMovement::RetireRecordedTrajectory()
{
	if (RecordedTrajectoryLease != 0)
	{
		if (UMHGZInstrumentedCharacterMovementComponent* CMC =
			Cast<UMHGZInstrumentedCharacterMovementComponent>(MovementComponent.Get()))
		{
			CMC->RetireRecordedTrajectoryMovement(RecordedTrajectoryLease);
		}
		RecordedTrajectoryLease = 0;
	}
}

void UAbilityTask_MHGZWeaponMovement::RemoveRootMotionSource()
{
	if (RootMotionSourceID != InvalidRootMotionSourceID)
	{
		if (UCharacterMovementComponent* CMC = MovementComponent.Get())
		{
			// Removal is deferred until CMC's next movement update. A retired
			// action must not apply its old SetVelocity/Clamp finish policy to
			// the replacement owner's launch on that update.
			if (!OwnsMovement())
			{
				if (TSharedPtr<FRootMotionSource> Source = CMC->GetRootMotionSourceByID(RootMotionSourceID))
				{
					Source->FinishVelocityParams.Mode =
						ERootMotionFinishVelocityMode::MaintainLastRootMotionVelocity;
				}
			}
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
