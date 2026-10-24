// Copyright MHGZ Project. All Rights Reserved.

#include "Kinsect.h"
#include "KinsectCollisionComponent.h"
#include "InsectGlaiveKinsectData.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"
#include "AttributeSystem/Res_InsectGlaive.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/AudioComponent.h"
#include "InsectGlaive/InsectGlaiveCombatConfig.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Engine/World.h"

bool AKinsect::ResolveScreenCenterLaunchOrigin(const FVector& CameraOrigin,
	const FVector& CameraDirection, const FVector& OwnerLocation, FVector& OutOrigin)
{
	OutOrigin = FVector::ZeroVector;
	if (CameraOrigin.ContainsNaN() || CameraDirection.ContainsNaN() || OwnerLocation.ContainsNaN())
	{
		return false;
	}
	const FVector Direction = CameraDirection.GetSafeNormal();
	const FVector PlaneNormal = FVector(Direction.X, Direction.Y, 0).GetSafeNormal();
	const double Denominator = FVector::DotProduct(Direction, PlaneNormal);
	if (Denominator <= KINDA_SMALL_NUMBER) { return false; }
	const double Distance = FVector::DotProduct(OwnerLocation - CameraOrigin, PlaneNormal) / Denominator;
	if (!FMath::IsFinite(Distance) || Distance < 0) { return false; }
	OutOrigin = CameraOrigin + Direction * Distance;
	return !OutOrigin.ContainsNaN();
}

namespace
{
constexpr float FALLBACK_FLIGHT_SPEED = 2000.f;
constexpr float FALLBACK_RETURN_SPEED = 2500.f;
constexpr float FALLBACK_HOVER_DRAIN = 3.f;
constexpr float FALLBACK_FLIGHT_DRAIN = 8.f;
}

AKinsect::AKinsect()
{
	PrimaryActorTick.bCanEverTick = true;

	// Collision 为 Root；Mesh 纯视觉，附着在 Collision 上
	Collision = CreateDefaultSubobject<UKinsectCollisionComponent>(TEXT("Collision"));
	RootComponent = Collision;

	Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Collision);
	Mesh->SetCollisionProfileName(TEXT("NoCollision"));
	Mesh->SetGenerateOverlapEvents(false);

	Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
	Movement->UpdatedComponent = Collision;
	Movement->bAutoActivate = false;   // 仅 BeginFlight/StartReturn 显式激活
	Movement->InitialSpeed = 0.f;
	Movement->MaxSpeed = 0.f;
	Movement->ProjectileGravityScale = 0.f; // 无重力
	Movement->bShouldBounce = false;        // 不反弹
	Movement->bRotationFollowsVelocity = true;

	FlightAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("FlightAudio"));
	FlightAudio->SetupAttachment(RootComponent);
	FlightAudio->bAutoActivate = false;
}

USoundBase* AKinsect::ResolveFlightLoopSound() const
{
	const bool bFree = State == EKinsectState::Flying || State == EKinsectState::Hovering
		|| State == EKinsectState::Returning || State == EKinsectState::ActionReturning
		|| State == EKinsectState::ActionWaiting || State == EKinsectState::CoAttack;
	const FWeaponRuntimeToken& Runtime = IsSlashActionControlled() ? ActionAttachmentToken.RuntimeToken
		: State == EKinsectState::Returning
		? ReturnRuntimeToken : ActiveRequest.RuntimeToken;
	if (!bFree || !ResourceComponent.IsValid()
		|| !ResourceComponent->IsRuntimeRequestCurrent(Runtime)) return nullptr;
	const UInsectGlaiveCombatConfig* Config = ResourceComponent->GetCombatConfig();
	USoundBase* Sound = Config ? Config->KinsectFlightLoopSound.Get() : nullptr;
	return Sound && Sound->IsLooping() ? Sound : nullptr;
}

void AKinsect::RefreshFlightAudio(const bool bPlayLaunch)
{
	if (!FlightAudio) return;
	USoundBase* Loop = ResolveFlightLoopSound();
	if (FlightAudio->Sound != Loop)
	{
		FlightAudio->Stop();
		FlightAudio->SetSound(Loop);
	}
	if (!Loop)
	{
		FlightAudio->Stop();
	}
	else if (!FlightAudio->IsPlaying()) FlightAudio->Play();
	if (bPlayLaunch && State == EKinsectState::Flying && ResourceComponent.IsValid()
		&& ResourceComponent->IsRuntimeRequestCurrent(ActiveRequest.RuntimeToken))
	{
		const UInsectGlaiveCombatConfig* Config = ResourceComponent->GetCombatConfig();
		USoundBase* Launch = Config ? Config->KinsectLaunchSound.Get() : nullptr;
		if (Launch && !Launch->IsLooping())
		{
			UGameplayStatics::SpawnSoundAttached(Launch, RootComponent, NAME_None,
				FVector::ZeroVector, EAttachLocation::KeepRelativeOffset, true);
		}
	}
}

void AKinsect::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (FlightAudio)
	{
		FlightAudio->Stop();
		FlightAudio->SetSound(nullptr);
	}
	Super::EndPlay(EndPlayReason);
}

void AKinsect::BeginPlay()
{
	Super::BeginPlay();

	if (Collision)
	{
		Collision->OnComponentHit.AddDynamic(this, &AKinsect::OnWorldCollision);
	}

	if (Movement)
	{
		// 确保 Movement 先于 Actor Tick 推进，Previous→Current 扫描才能覆盖本帧位移
		AddTickPrerequisiteComponent(Movement);
	}
}

void AKinsect::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	RefreshFlightAudio();
	if (State == EKinsectState::CoAttack)
	{
		if (!ResourceComponent.IsValid() || !ResourceComponent->IsKinsectCommandCurrent(ActiveRequest))
		{
			Interrupt();
		}
		return; // the post-pose sampler is the only cooperative movement writer
	}
	if (State == EKinsectState::ActionReturning)
	{
		TickSlashPreparation(DeltaTime);
		return;
	}

	if (State != EKinsectState::Flying && State != EKinsectState::Returning)
	{
		return;
	}

	// 飞行沿用已提交请求身份；返回使用独立 Runtime 身份，因为动作租约
	// 可能已释放，而最初附着的猎虫也可能从未拥有 ActiveRequest。
	const FWeaponRuntimeToken& TickRuntimeToken = State == EKinsectState::Returning
		? ReturnRuntimeToken : ActiveRequest.RuntimeToken;
	if (!ResourceComponent.IsValid() ||
		!ResourceComponent->IsRuntimeRequestCurrent(TickRuntimeToken))
	{
		Interrupt();
		return;
	}
	if (State == EKinsectState::Flying && ActiveRequest.CommandSerial != 0
		&& !ResourceComponent->IsKinsectCommandCurrent(ActiveRequest))
	{
		Interrupt();
		return;
	}
	if (State == EKinsectState::Returning && ReturnCommandSerial != 0
		&& !ResourceComponent->IsKinsectCommandCurrent(ReturnCommandSerial,
			ReturnCommandSource, ReturnMarkSerial, ReturnRuntimeToken))
	{
		StopAndHover();
		return;
	}

	switch (State)
	{
	case EKinsectState::Flying:
	{
		if (ActiveRequest.TrajectoryMode == EKinsectTrajectoryMode::ToPoint
			&& ActiveRequest.bFollowTargetComponent)
		{
			UpdateMovingToPointTarget();
			if (State != EKinsectState::Flying)
			{
				break;
			}
		}
		// Terrain is an aim endpoint, never a physical hit or a flight interruption.
		if (ActiveRequest.CommandSource == EKinsectCommandSource::DescendingThrust
			&& ActiveRequest.TrajectoryMode == EKinsectTrajectoryMode::ToPoint)
		{
			const FVector Target = ActiveRequest.TargetPointSnapshot;
			if (FVector::DotProduct(Target - LastTickLocation, Target - GetActorLocation()) <= 0.f)
				SetActorLocation(Target, false, nullptr, ETeleportType::TeleportPhysics);
		}
		const FGuid SweptFlightID = ActiveRequest.FlightInstanceID;
		const uint64 SweptSerial = ActiveRequest.CommandSerial;
		SweepHitzones(DeltaTime);
		if (State != EKinsectState::Flying || ActiveRequest.FlightInstanceID != SweptFlightID
			|| ActiveRequest.CommandSerial != SweptSerial)
		{
			break; // ended or replaced inside a damage callback; do not advance the new flight
		}
		PendingWorldHit.Reset();
		CheckFlightProgress();
		break;
	}

	case EKinsectState::Returning:
		TickReturn();
		break;

	default:
		break;
	}
}

bool AKinsect::IsFlightRequestValid(const FKinsectFlightRequest& Request) const
{
	if (!Request.RuntimeToken.IsValid() || !ResourceComponent.IsValid())
	{
		return false;
	}
	if (!ResourceComponent->IsRuntimeRequestCurrent(Request.RuntimeToken))
	{
		return false;
	}
	if (!FMath::IsFinite(Request.FlightSpeed) || Request.FlightSpeed <= 0.f)
	{
		return false;
	}
	if (!FMath::IsFinite(Request.MaxDistance) || Request.MaxDistance <= 0.f)
	{
		return false;
	}
	if (!FMath::IsFinite(Request.ArrivalRadius) || Request.ArrivalRadius <= 0.f)
	{
		return false;
	}
	if (!FMath::IsFinite(Request.RehitInterval) || Request.RehitInterval < 0.f)
	{
		return false;
	}
	if (!FMath::IsFinite(Request.MotionValue) || Request.MotionValue < 0.f)
	{
		return false;
	}
	if (!Request.FlightInstanceID.IsValid())
	{
		return false;
	}
	if (Request.bProjectAttachedLaunchToAimRay && State == EKinsectState::Attached)
	{
		FVector ProjectedOrigin;
		if (!OwnerActor.IsValid() || !ResolveScreenCenterLaunchOrigin(Request.AimOriginSnapshot,
			Request.DirectionSnapshot, OwnerActor->GetActorLocation(), ProjectedOrigin))
		{
			return false;
		}
	}
	if (Request.PowderSpawnPolicy != EIGPowderSpawnPolicy::None
		&& !Request.SourceAttackInstanceID.IsValid())
	{
		return false;
	}
	if (Request.PowderSpawnPolicy == EIGPowderSpawnPolicy::AwakenedPiercing
		&& (Request.DamageMode != EKinsectDamageMode::Piercing
			|| Request.ExtractMode != EKinsectExtractMode::CollectPerValidHit
			|| Request.PostFlightPolicy != EKinsectPostFlightPolicy::ReturnToOwner))
	{
		return false;
	}
	if (Request.PowderSpawnPolicy == EIGPowderSpawnPolicy::AwakenedEachHit
		&& Request.DamageMode == EKinsectDamageMode::None)
	{
		return false;
	}
	if (Request.TrajectoryMode == EKinsectTrajectoryMode::AlongDirection)
	{
		if (Request.DirectionSnapshot.ContainsNaN() ||
			Request.DirectionSnapshot.GetSafeNormal().IsNearlyZero())
		{
			return false;
		}
	}
	else if (Request.TrajectoryMode == EKinsectTrajectoryMode::ToPoint)
	{
		FVector ResolvedTarget;
		if (!ResolveToPointTarget(Request, ResolvedTarget))
		{
			return false;
		}
	}
	else
	{
		return false;
	}
	return true;
}

bool AKinsect::CanBeginFlight(const FKinsectFlightRequest& Request) const
{
	return IsFlightRequestValid(Request) && Movement && Collision
		&& !ActionAttachmentToken.IsValid();
}

bool AKinsect::BeginCoAttack(const FWeaponRuntimeToken& RuntimeToken, const uint64 CommandSerial,
	const uint64 DescendingThrustHoverSerial)
{
	const bool bAuthorizedHover = State == EKinsectState::Hovering && DescendingThrustHoverSerial != 0
		&& ActiveRequest.CommandSource == EKinsectCommandSource::DescendingThrust
		&& ActiveRequest.CommandSerial == DescendingThrustHoverSerial;
	if ((State != EKinsectState::Attached && !bAuthorizedHover) || CommandSerial == 0 || !Movement || !Collision || !Mesh
		|| ActionAttachmentToken.IsValid() || !ResourceComponent.IsValid()
		|| !ResourceComponent->IsKinsectCommandCurrent(CommandSerial,
			EKinsectCommandSource::CoAttack, 0, RuntimeToken)
		|| !FMath::IsFinite(GetReturnSpeed()) || GetReturnSpeed() <= 0.f) return false;
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Movement->StopMovementImmediately();
	Movement->Deactivate();
	Collision->DisableKinsectCollision();
	PendingWorldHit.Reset();
	HitzoneHitTimers.Reset();
	bHasDealtDamage = false;
	ActiveRequest = FKinsectFlightRequest(); // damage/extraction explicitly None
	ActiveRequest.RuntimeToken = RuntimeToken;
	ActiveRequest.CommandSource = EKinsectCommandSource::CoAttack;
	ActiveRequest.CommandSerial = CommandSerial;
	State = EKinsectState::CoAttack;
	SetFlightVisualFacing(true);
	RefreshFlightAudio();
	return true;
}

bool AKinsect::BeginDescendingThrustWait(const FWeaponRuntimeToken& RuntimeToken,
	const uint64 Serial, const FVector& Position)
{
	if (State != EKinsectState::Attached || Serial == 0 || Position.ContainsNaN()
		|| !Movement || !Collision || !ResourceComponent.IsValid() || ActionAttachmentToken.IsValid()
		|| !ResourceComponent->IsKinsectCommandCurrent(Serial, EKinsectCommandSource::DescendingThrust, 0, RuntimeToken)) return false;
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Movement->StopMovementImmediately(); Movement->Deactivate();
	Collision->DisableKinsectCollision();
	PendingWorldHit.Reset(); HitzoneHitTimers.Reset(); bHasDealtDamage = false;
	ActiveRequest = FKinsectFlightRequest();
	ActiveRequest.RuntimeToken = RuntimeToken;
	ActiveRequest.CommandSource = EKinsectCommandSource::DescendingThrust;
	ActiveRequest.CommandSerial = Serial;
	SetActorLocation(Position, false, nullptr, ETeleportType::TeleportPhysics);
	State = EKinsectState::Hovering;
	SetFlightVisualFacing(true); RefreshFlightAudio();
	return true;
}

bool AKinsect::ApplyCoAttackPose(const uint64 CommandSerial, const FVector& Position,
	const FVector& Tangent)
{
	if (State != EKinsectState::CoAttack || ActiveRequest.CommandSerial != CommandSerial
		|| !ResourceComponent.IsValid() || !ResourceComponent->IsKinsectCommandCurrent(ActiveRequest)
		|| Position.ContainsNaN() || Tangent.ContainsNaN()) return false;
	// Collision is query-disabled; neither sweep nor ProjectileMovement can override this pose.
	SetActorLocation(Position, false, nullptr, ETeleportType::TeleportPhysics);
	if (!Tangent.IsNearlyZero()) SetActorRotation(Tangent.Rotation());
	return true;
}

bool AKinsect::BeginFlight(const FKinsectFlightRequest& Request)
{
	return BeginFlightInternal(Request, nullptr);
}

bool AKinsect::BeginFlightFromActionAttachment(
	const FWeaponActionToken& ActionToken,
	const FKinsectFlightRequest& Request)
{
	return BeginFlightInternal(Request, &ActionToken);
}

bool AKinsect::BeginFlightInternal(
	const FKinsectFlightRequest& Request,
	const FWeaponActionToken* ActionAttachmentOwner)
{
	// 完整校验，全部通过后才提交，保证原子性
	const bool bFromActionAttachment = ActionAttachmentOwner != nullptr;
	if (!IsFlightRequestValid(Request)
		|| (bFromActionAttachment
			? !IsActionAttachmentOwnedBy(*ActionAttachmentOwner)
			: ActionAttachmentToken.IsValid()))
	{
		return false;
	}

	if (!Movement || !Collision) { return false; }
	FVector LaunchOrigin = GetActorLocation();
	const bool bProjectLaunch = Request.bProjectAttachedLaunchToAimRay && State == EKinsectState::Attached;
	if (bProjectLaunch && !ResolveScreenCenterLaunchOrigin(Request.AimOriginSnapshot,
		Request.DirectionSnapshot, OwnerActor->GetActorLocation(), LaunchOrigin))
	{
		return false;
	}
	if (bProjectLaunch && Request.TrajectoryMode == EKinsectTrajectoryMode::ToPoint)
	{
		// A close floor/wall can meet the center ray before the hunter's plane.
		// Stay on the visible ray, but do not teleport past that frozen target.
		const FVector AimDirection = Request.DirectionSnapshot.GetSafeNormal();
		const double TargetDistance = FVector::DotProduct(
			Request.TargetPointSnapshot - Request.AimOriginSnapshot, AimDirection);
		const double PlaneDistance = FVector::DotProduct(
			LaunchOrigin - Request.AimOriginSnapshot, AimDirection);
		double Clearance = Request.ArrivalRadius + Collision->GetScaledCapsuleRadius();
		const FVector Normal = Request.AimTargetSurfaceNormal.GetSafeNormal();
		const double ApproachCosine = -FVector::DotProduct(AimDirection, Normal);
		if (!Normal.IsNearlyZero() && ApproachCosine > KINDA_SMALL_NUMBER)
		{
			const double Support = Collision->GetScaledCapsuleRadius()
				+ (Collision->GetScaledCapsuleHalfHeight() - Collision->GetScaledCapsuleRadius()) * FMath::Abs(Normal.Z);
			Clearance = FMath::Max(Clearance, (Support + 2.0) / ApproachCosine);
		}
		if (TargetDistance <= 0) { return false; }
		if (PlaneDistance > TargetDistance - Clearance)
		{
			LaunchOrigin = Request.AimOriginSnapshot + AimDirection * FMath::Max(0.0, TargetDistance - Clearance);
		}
	}
	FVector FlightDirection;
	if (Request.TrajectoryMode == EKinsectTrajectoryMode::AlongDirection)
	{
		FlightDirection = Request.DirectionSnapshot.GetSafeNormal();
	}
	else
	{
		FlightDirection = (Request.TargetPointSnapshot - LaunchOrigin).GetSafeNormal();
	}
	if (FlightDirection.IsNearlyZero())
	{
		return false; // 目标点与当前位置重合
	}
	// 原子提交
	USceneComponent* PreviousAttachParent = bFromActionAttachment
		&& GetRootComponent() ? GetRootComponent()->GetAttachParent() : nullptr;
	const FName PreviousAttachSocket = bFromActionAttachment
		&& GetRootComponent() ? GetRootComponent()->GetAttachSocketName() : NAME_None;
	const FVector PreviousLocation = GetActorLocation();
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	// Flight heading belongs to the visual mesh; keep the physical capsule upright.
	SetActorRotation(FRotator(0.0f, GetActorRotation().Yaw, 0.0f));
	if (bProjectLaunch) { SetActorLocation(LaunchOrigin, false, nullptr, ETeleportType::TeleportPhysics); }
	SetFlightVisualFacing(true);
	if (!ArmMovement(FlightDirection, Request.FlightSpeed, true))
	{
		// BeginFlight 的失败不能留下一个从主人/武器脱离、却不可移动的猎虫。
		SetFlightVisualFacing(false);
		SetActorLocation(PreviousLocation, false, nullptr, ETeleportType::TeleportPhysics);
		if (PreviousAttachParent)
		{
			AttachToComponent(PreviousAttachParent,
				FAttachmentTransformRules::KeepWorldTransform, PreviousAttachSocket);
		}
		else
		{
			AttachToPlayer(AttachComponent.Get(), AttachSocketName);
		}
		return false;
	}
	Collision->EnableKinsectCollision();

	ActionAttachmentToken = FWeaponActionToken();
	ResetSlashPreparation();
	ActiveRequest = Request;
	ReturnRuntimeToken = FWeaponRuntimeToken();
	ReturnCommandSerial = 0;
	ReturnCommandSource = EKinsectCommandSource::None;
	ReturnMarkSerial = 0;
	FlightStartLocation = GetActorLocation();
	LastTickLocation = GetActorLocation();
	PendingWorldHit.Reset();
	bHasDealtDamage = false;
	HitzoneHitTimers.Reset();
	State = EKinsectState::Flying;
	RefreshFlightAudio(true);
	if (Request.bProjectAttachedLaunchToAimRay)
	{
		UE_LOG(LogTemp, Display, TEXT("[KinsectAim] centerLaunch=%d origin=%s target=%s direction=%s"),
			bProjectLaunch, *LaunchOrigin.ToCompactString(),
			*Request.TargetPointSnapshot.ToCompactString(), *FlightDirection.ToCompactString());
	}
	return true;
}

void AKinsect::Interrupt()
{
	if (State == EKinsectState::Attached || ActionAttachmentToken.IsValid())
	{
		return;
	}
	const FKinsectFlightRequest InterruptedRequest = ActiveRequest;
	const bool bWasFlying = State == EKinsectState::Flying;

	// 停旧飞行但不清除 Pending 萃取
	Movement->StopMovementImmediately();
	Movement->Deactivate();
	if (Collision)
	{
		Collision->DisableKinsectCollision();
	}
	PendingWorldHit.Reset();
	HitzoneHitTimers.Reset();
	bHasDealtDamage = false;
	State = EKinsectState::Hovering;
	if (bWasFlying && ResourceComponent.IsValid())
	{
		ResourceComponent->OnKinsectFlightEnded(InterruptedRequest,
			EKinsectFlightEndReason::Interrupted);
	}
}

bool AKinsect::StartReturn(const uint64 CommandSerial,
	const EKinsectCommandSource Source)
{
	if (State == EKinsectState::Attached || ActionAttachmentToken.IsValid()
		|| State == EKinsectState::Returning)
	{
		return State == EKinsectState::Returning; // 幂等
	}
	ReturnRuntimeToken = ActiveRequest.RuntimeToken;
	ReturnCommandSerial = CommandSerial != 0
		? CommandSerial : ActiveRequest.CommandSerial;
	ReturnCommandSource = Source != EKinsectCommandSource::None
		? Source : ActiveRequest.CommandSource;
	ReturnMarkSerial = ActiveRequest.MarkSerial;
	ReturnArrivalRadius = ActiveRequest.ArrivalRadius > 0.f
		? ActiveRequest.ArrivalRadius : RETURN_ARRIVAL_DISTANCE;
	bHasLastReturnRelativeLocation = false;
	StartReturnInternal();
	if (State != EKinsectState::Returning)
	{
		ReturnCommandSerial = 0;
		ReturnCommandSource = EKinsectCommandSource::None;
		ReturnMarkSerial = 0;
		return false;
	}
	return true;
}

void AKinsect::StartReturnInternal()
{
	if (State == EKinsectState::Returning)
	{
		return;
	}

	const float ReturnSpeed = KinsectData ? KinsectData->ReturnSpeed : FALLBACK_RETURN_SPEED;
	if (ReturnSpeed <= 0.f)
	{
		StopAndHover();
		return;
	}

	State = EKinsectState::Returning;
	SetFlightVisualFacing(true);
	// 返回不再参与攻击或世界阻挡；否则被墙截停后将永远无法交付 Pending 精华。
	if (Collision)
	{
		Collision->DisableKinsectCollision();
	}
	PendingWorldHit.Reset();
	HitzoneHitTimers.Reset();
	bHasDealtDamage = false;

	// 仅在尚未到达时立刻写入首帧返回速度。这样既能重新绑定撞墙后被
	// ProjectileMovement 清空的 UpdatedComponent，又保留“刚送出即召回”至少
	// 经过一帧 Returning 的状态契约；真正 Attach 仍统一在 TickReturn 中发生。
	FVector SocketLocation;
	if (!ResolveAttachSocketLocation(SocketLocation))
	{
		StopAndHover();
		return;
	}
	const FVector ToTarget = SocketLocation - GetActorLocation();
	LastReturnRelativeLocation = GetActorLocation() - SocketLocation;
	bHasLastReturnRelativeLocation = true;
	if (ToTarget.Size() > ReturnArrivalRadius && !ArmMovement(ToTarget, ReturnSpeed))
	{
		StopAndHover();
	}
	RefreshFlightAudio();
}

void AKinsect::ForceRecall(const uint64 CommandSerial,
	const EKinsectCommandSource Source)
{
	if (ActionAttachmentToken.IsValid())
	{
		if (ResourceComponent.IsValid())
		{
			ResourceComponent->EndKinsectActionAttachmentAndReturn(
				ActionAttachmentToken, Source);
		}
		return;
	}
	StartReturn(CommandSerial, Source);
}

bool AKinsect::UpdateAutomaticMarkTarget(const FVector& TargetPoint,
	const uint64 ExpectedCommandSerial, const uint64 ExpectedMarkSerial)
{
	if (State != EKinsectState::Flying || TargetPoint.ContainsNaN()
		|| ActiveRequest.CommandSource != EKinsectCommandSource::AutoMark
		|| ActiveRequest.CommandSerial != ExpectedCommandSerial
		|| ActiveRequest.MarkSerial != ExpectedMarkSerial
		|| !ResourceComponent.IsValid()
		|| !ResourceComponent->IsKinsectCommandCurrent(ActiveRequest))
	{
		return false;
	}

	ActiveRequest.TargetPointSnapshot = TargetPoint;
	const FVector ToTarget = TargetPoint - GetActorLocation();
	if (ToTarget.IsNearlyZero())
	{
		return true;
	}
	return ArmMovement(ToTarget, ActiveRequest.FlightSpeed, true);
}

bool AKinsect::BeginAutomaticMarkContact(const FKinsectFlightRequest& Request)
{
	if (State != EKinsectState::Hovering
		|| Request.CommandSource != EKinsectCommandSource::AutoMark
		|| Request.CommandSerial == 0 || Request.MarkSerial == 0
		|| !IsFlightRequestValid(Request)
		|| !ResourceComponent.IsValid()
		|| !ResourceComponent->IsKinsectCommandCurrent(Request))
	{
		return false;
	}
	ActiveRequest = Request;
	ReturnRuntimeToken = FWeaponRuntimeToken();
	ReturnCommandSerial = 0;
	ReturnCommandSource = EKinsectCommandSource::None;
	ReturnMarkSerial = 0;
	LastTickLocation = GetActorLocation();
	bHasDealtDamage = false;
	return FaceAutomaticMark(Request.CommandSerial, Request.MarkSerial);
}

FVector AKinsect::GetAttackForwardVector() const
{
	return Mesh ? Mesh->GetRightVector().GetSafeNormal() : FVector::ZeroVector;
}

bool AKinsect::IsAttackContactInFront(const FHitResult& Hit) const
{
	return IsContactInFront(Hit, GetAttackForwardVector());
}

bool AKinsect::IsContactInFront(const FHitResult& Hit, const FVector& Forward)
{
	// Initial-overlap sweeps do not have a surface ImpactPoint. Their separating
	// normal points out of the target; its opposite points toward the contact.
	const FVector ToContact = Hit.bStartPenetrating
		? -Hit.ImpactNormal : Hit.ImpactPoint - Hit.Location;
	return !Forward.IsNearlyZero() && !ToContact.ContainsNaN()
		&& FVector::DotProduct(Forward, ToContact) > KINDA_SMALL_NUMBER;
}

bool AKinsect::FaceAutomaticMark(const uint64 ExpectedCommandSerial,
	const uint64 ExpectedMarkSerial)
{
	if (State != EKinsectState::Hovering || !Mesh
		|| ActiveRequest.CommandSource != EKinsectCommandSource::AutoMark
		|| ActiveRequest.CommandSerial != ExpectedCommandSerial
		|| ActiveRequest.MarkSerial != ExpectedMarkSerial
		|| !ResourceComponent.IsValid()
		|| !ResourceComponent->IsKinsectCommandCurrent(ActiveRequest))
	{
		return false;
	}
	FVector MarkLocation;
	FVector MarkNormal;
	if (!ResourceComponent->GetKinsectMarkSurfaceFrame(ExpectedMarkSerial,
		MarkLocation, MarkNormal))
	{
		return false;
	}
	const FVector Direction = (MarkLocation - GetActorLocation()).GetSafeNormal();
	if (Direction.IsNearlyZero()) { return false; }
	const FQuat MeshFacingOffset(FVector::UpVector, -UE_HALF_PI);
	Mesh->SetWorldRotation(Direction.ToOrientationQuat() * MeshFacingOffset);
	return true;
}

bool AKinsect::SweepAutomaticMarkContact(const uint64 ExpectedCommandSerial,
	const uint64 ExpectedMarkSerial, const FWeaponRuntimeToken& RuntimeToken,
	FHitResult& OutHit) const
{
	if (State != EKinsectState::Hovering || !Collision || !GetWorld()
		|| ActiveRequest.CommandSource != EKinsectCommandSource::AutoMark
		|| ActiveRequest.CommandSerial != ExpectedCommandSerial
		|| ActiveRequest.MarkSerial != ExpectedMarkSerial
		|| ActiveRequest.RuntimeToken != RuntimeToken
		|| !ResourceComponent.IsValid()
		|| !ResourceComponent->IsKinsectCommandCurrent(ExpectedCommandSerial,
			EKinsectCommandSource::AutoMark, ExpectedMarkSerial, RuntimeToken))
	{
		return false;
	}

	FVector MarkLocation;
	FVector MarkNormal;
	if (!ResourceComponent->GetKinsectMarkSurfaceFrame(ExpectedMarkSerial,
		MarkLocation, MarkNormal))
	{
		return false;
	}
	// The radial proxy trace below resolves a surface hit; it is NOT an unlimited
	// attack ray. The real mark must first be within melee reach and in front of
	// the Kinsect's visible head, including while hovering with zero velocity.
	const FVector ToMark = MarkLocation - GetActorLocation();
	// Reuse the committed approach endpoint: a low wall mark can require extra
	// vertical clearance from the floor. A fixed distance to the surface would
	// reject that valid approach. Five cm is the resource's existing arrival gate.
	if (FVector::DistSquared(GetActorLocation(), ActiveRequest.TargetPointSnapshot) > 25.0f
		|| FVector::DotProduct(ToMark, GetAttackForwardVector()) <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	FCollisionObjectQueryParams ObjectQueryParams;
	ObjectQueryParams.AddObjectTypesToQuery(ECC_GameTraceChannel3);
	FCollisionQueryParams QueryParams(FName(TEXT("KinsectAutoMarkContact")), false);
	QueryParams.AddIgnoredActor(this);
	if (OwnerActor.IsValid())
	{
		QueryParams.AddIgnoredActor(OwnerActor.Get());
	}

	const UMHGZMonsterHitzoneComponent* MarkHitzone =
		ResourceComponent->GetKinsectMarkHitzone();
	if (!MarkHitzone)
	{
		return false;
	}
	// The visible surface can be outside its spherical gameplay proxy. Trace
	// radially through that proxy, then verify the projected surface patch; a broad
	// capsule sweep hits an off-centre point that can miss the small 20 cm patch.
	const FVector ProxyCenter = MarkHitzone->GetComponentLocation();
	const FVector ContactDirection = (MarkLocation - ProxyCenter).GetSafeNormal(SMALL_NUMBER, MarkNormal);
	const float ContactDepth = FVector::Distance(MarkLocation, ProxyCenter)
		+ MarkHitzone->GetQueryBoundingRadius() + 2.0f;
	const FVector Start = MarkLocation + ContactDirection * ContactDepth;
	const FVector End = ProxyCenter - ContactDirection * ContactDepth;
	TArray<FHitResult> Hits;
	GetWorld()->LineTraceMultiByObjectType(Hits, Start, End,
		ObjectQueryParams, QueryParams);
	Hits.Sort([](const FHitResult& A, const FHitResult& B)
	{
		return A.Time < B.Time;
	});

	for (const FHitResult& Hit : Hits)
	{
		if (ResourceComponent->IsHitOnKinsectMarkedPart(Hit))
		{
			OutHit = Hit;
			return true;
		}
	}
	return false;
}

void AKinsect::AttachToPlayer(USceneComponent* InAttachComponent, FName InSocketName)
{
	if (Mesh && !bHasAttachedMeshRelativeRotation)
	{
		AttachedMeshRelativeRotation = Mesh->GetRelativeRotation();
		AttachedMeshRelativeScale = Mesh->GetRelativeScale3D();
		bHasAttachedMeshRelativeRotation = true;
	}

	AttachComponent = InAttachComponent;
	AttachSocketName = InSocketName;

	if (InAttachComponent)
	{
		AttachToComponent(InAttachComponent,
			FAttachmentTransformRules::SnapToTargetIncludingScale, InSocketName);
		OwnerActor = InAttachComponent->GetOwner();
	}
	SetFlightVisualFacing(false);

	Movement->StopMovementImmediately();
	Movement->Deactivate();
	PendingWorldHit.Reset();
	State = EKinsectState::Attached;
	RefreshFlightAudio();
	bHasLastReturnRelativeLocation = false;
	ActionAttachmentToken = FWeaponActionToken();
	ResetSlashPreparation();
	ReturnRuntimeToken = FWeaponRuntimeToken();
	ReturnCommandSerial = 0;
	ReturnCommandSource = EKinsectCommandSource::None;
	ReturnMarkSerial = 0;
	ReturnArrivalRadius = RETURN_ARRIVAL_DISTANCE;

	if (Collision)
	{
		Collision->DisableKinsectCollision();
	}
}

bool AKinsect::BeginActionAttachment(const FWeaponActionToken& ActionToken,
	USkeletalMeshComponent* WeaponMesh, const FName SocketName)
{
	if (!ActionToken.IsValid() || !ResourceComponent.IsValid()
		|| !ResourceComponent->IsRuntimeRequestCurrent(ActionToken.RuntimeToken)
		|| !WeaponMesh || SocketName.IsNone() || !WeaponMesh->DoesSocketExist(SocketName)
		|| !Collision || !Movement || State == EKinsectState::ActionAttached
		|| (ActionAttachmentToken.IsValid() && ActionAttachmentToken != ActionToken))
	{
		return false;
	}

	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Movement->StopMovementImmediately();
	Movement->Deactivate();
	if (Movement->UpdatedComponent != Collision)
	{
		Movement->SetUpdatedComponent(Collision);
	}
	Collision->DisableKinsectCollision();
	PendingWorldHit.Reset();
	HitzoneHitTimers.Reset();
	bHasDealtDamage = false;
	SetFlightVisualFacing(false);

	if (!bHasAttachedMeshRelativeRotation && Mesh)
	{
		AttachedMeshRelativeRotation = Mesh->GetRelativeRotation();
		AttachedMeshRelativeScale = Mesh->GetRelativeScale3D();
		bHasAttachedMeshRelativeRotation = true;
	}
	AttachToComponent(WeaponMesh,
		FAttachmentTransformRules::SnapToTargetIncludingScale, SocketName);
	ActionAttachmentToken = ActionToken;
	ReturnRuntimeToken = FWeaponRuntimeToken();
	State = EKinsectState::ActionAttached;
	RefreshFlightAudio();
	return true;
}

bool AKinsect::BeginSlashPreparation(const FWeaponActionToken& ActionToken,
	USkeletalMeshComponent* WeaponMesh, const FName SocketName,
	const float AvailableReturnTime, const bool bHasRequiredStamina)
{
	if (!ActionToken.IsValid() || !ResourceComponent.IsValid()
		|| !ResourceComponent->IsRuntimeRequestCurrent(ActionToken.RuntimeToken)
		|| ActionAttachmentToken.IsValid() || !WeaponMesh || SocketName.IsNone()
		|| !WeaponMesh->DoesSocketExist(SocketName) || !Collision || !Movement
		|| !FMath::IsFinite(AvailableReturnTime) || AvailableReturnTime < 0.f)
	{
		return false;
	}
	const bool bWasOnArm = State == EKinsectState::Attached;
	const FVector Target = WeaponMesh->GetSocketLocation(SocketName);
	const float Speed = GetReturnSpeed();
	const float Distance = FVector::Distance(GetActorLocation(), Target);
	if (Target.ContainsNaN() || !FMath::IsFinite(Distance)) { return false; }
	if (bWasOnArm && bHasRequiredStamina)
	{
		if (!BeginActionAttachment(ActionToken, WeaponMesh, SocketName)) { return false; }
	}
	else
	{
		// Freeze the old flight/return before claiming this action. No stale attack
		// sweep, auto-mark request or world collision may move the waiting insect.
		Movement->StopMovementImmediately();
		Movement->Deactivate();
		Collision->DisableKinsectCollision();
		PendingWorldHit.Reset();
		HitzoneHitTimers.Reset();
		bHasDealtDamage = false;
		ActiveRequest = FKinsectFlightRequest();
		ActiveRequest.RuntimeToken = ActionToken.RuntimeToken;
		ReturnRuntimeToken = FWeaponRuntimeToken();
		ReturnCommandSerial = 0;
		if (!bWasOnArm)
		{
			DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
			const bool bCanArrive = bHasRequiredStamina && FMath::IsFinite(Speed)
				&& Speed > 0.f && Distance <= Speed * AvailableReturnTime;
			State = bCanArrive ? EKinsectState::ActionReturning : EKinsectState::ActionWaiting;
			SetFlightVisualFacing(true);
		}
		ActionAttachmentToken = ActionToken;
	}
	bSlashPreparation = true;
	SlashAttachMesh = WeaponMesh;
	SlashAttachSocket = SocketName;
	SlashReturnTimeRemaining = AvailableReturnTime;
	RefreshFlightAudio();
	return true;
}

void AKinsect::ResetSlashPreparation()
{
	bSlashPreparation = false;
	SlashAttachMesh.Reset();
	SlashAttachSocket = NAME_None;
	SlashReturnTimeRemaining = 0.f;
}

void AKinsect::FinalizeSlashPreparation(const FWeaponActionToken& ActionToken)
{
	if (IsActionControlOwnedBy(ActionToken) && bSlashPreparation)
	{
		SlashReturnTimeRemaining = 0.f;
		if (State == EKinsectState::ActionReturning) { State = EKinsectState::ActionWaiting; }
	}
}

void AKinsect::TickSlashPreparation(const float DeltaTime)
{
	if (!ResourceComponent.IsValid()
		|| !ResourceComponent->IsKinsectActionControlCurrent(ActionAttachmentToken)
		|| !SlashAttachMesh.IsValid() || !SlashAttachMesh->DoesSocketExist(SlashAttachSocket))
	{
		State = EKinsectState::ActionWaiting;
		return;
	}
	const float StepTime = FMath::Min(FMath::Max(0.f, DeltaTime), SlashReturnTimeRemaining);
	const FVector Target = SlashAttachMesh->GetSocketLocation(SlashAttachSocket);
	const FVector ToTarget = Target - GetActorLocation();
	const float Step = GetReturnSpeed() * StepTime;
	if (Target.ContainsNaN() || !FMath::IsFinite(Step) || Step < 0.f)
	{
		State = EKinsectState::ActionWaiting;
		return;
	}
	SlashReturnTimeRemaining = FMath::Max(0.f, SlashReturnTimeRemaining - StepTime);
	if (ToTarget.Size() <= Step)
	{
		// Attach only on actual arrival. This bypasses world collision, not speed.
		BeginActionAttachment(ActionAttachmentToken, SlashAttachMesh.Get(), SlashAttachSocket);
	}
	else
	{
		SetActorLocation(GetActorLocation() + ToTarget.GetSafeNormal() * Step,
			false, nullptr, ETeleportType::TeleportPhysics);
		SetActorRotation(ToTarget.Rotation());
		if (SlashReturnTimeRemaining <= 0.f) { State = EKinsectState::ActionWaiting; }
	}
}

bool AKinsect::IsActionControlOwnedBy(const FWeaponActionToken& ActionToken) const
{
	return ActionToken.IsValid() && ActionAttachmentToken == ActionToken
		&& (State == EKinsectState::ActionAttached || State == EKinsectState::ActionReturning
			|| State == EKinsectState::ActionWaiting || (bSlashPreparation && State == EKinsectState::Attached));
}

bool AKinsect::EndActionAttachmentAndReturn(
	const FWeaponActionToken& ActionToken,
	const FWeaponRuntimeToken& RuntimeToken,
	const float ArrivalRadius,
	const uint64 CommandSerial,
	const EKinsectCommandSource Source)
{
	if (!IsActionControlOwnedBy(ActionToken) || !ResourceComponent.IsValid()
		|| !RuntimeToken.IsValid()
		|| RuntimeToken != ActionToken.RuntimeToken
		|| !ResourceComponent->IsRuntimeRequestCurrent(RuntimeToken)
		|| !FMath::IsFinite(ArrivalRadius) || ArrivalRadius <= 0.0f)
	{
		return false;
	}
	const bool bLeaveInPlace = bSlashPreparation && State != EKinsectState::ActionAttached
		&& (Source == EKinsectCommandSource::Action || State == EKinsectState::Attached);
	ResetSlashPreparation();
	if (bLeaveInPlace)
	{
		ActionAttachmentToken = FWeaponActionToken();
		if (State != EKinsectState::Attached) { State = EKinsectState::Hovering; }
		return true;
	}

	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	ActionAttachmentToken = FWeaponActionToken();
	ReturnRuntimeToken = RuntimeToken;
	ReturnCommandSerial = CommandSerial;
	ReturnCommandSource = Source;
	ReturnMarkSerial = 0;
	ReturnArrivalRadius = ArrivalRadius;
	State = EKinsectState::Hovering;
	StartReturnInternal();
	return State == EKinsectState::Returning || State == EKinsectState::Attached;
}

bool AKinsect::IsActionAttachmentOwnedBy(
	const FWeaponActionToken& ActionToken) const
{
	return State == EKinsectState::ActionAttached
		&& ActionAttachmentToken.IsValid() && ActionAttachmentToken == ActionToken
		&& GetRootComponent() && GetRootComponent()->GetAttachParent()
		&& !GetRootComponent()->GetAttachSocketName().IsNone();
}

FGameplayTag AKinsect::GetPendingExtractColor() const
{
	return PendingExtractColors.IsEmpty() ? FGameplayTag() : PendingExtractColors.GetByIndex(0);
}

FGameplayTag AKinsect::TakePendingExtractColor()
{
	const FGameplayTag Extracted = GetPendingExtractColor();
	PendingExtractColors.RemoveTag(Extracted);
	return Extracted;
}

FGameplayTagContainer AKinsect::TakePendingExtractColors()
{
	FGameplayTagContainer Extracted = MoveTemp(PendingExtractColors);
	PendingExtractColors.Reset();
	return Extracted;
}

bool AKinsect::CapturePendingExtractColor(const FGameplayTag ExtractColor,
	const bool bAllowMultipleColors)
{
	const FString Name = ExtractColor.ToString();
	if ((!bAllowMultipleColors && HasPendingExtract())
		|| PendingExtractColors.HasTagExact(ExtractColor)
		|| (Name != TEXT("WeaponResource.IG.Extract.Red")
		&& Name != TEXT("WeaponResource.IG.Extract.White")
		&& Name != TEXT("WeaponResource.IG.Extract.Orange")))
	{
		return false;
	}
	PendingExtractColors.AddTag(ExtractColor);
	return true;
}

float AKinsect::GetFlightSpeed() const
{
	return KinsectData ? KinsectData->FlightSpeed : FALLBACK_FLIGHT_SPEED;
}

float AKinsect::GetReturnSpeed() const
{
	return KinsectData ? KinsectData->ReturnSpeed : FALLBACK_RETURN_SPEED;
}

float AKinsect::GetHoverDrainRate() const
{
	return KinsectData ? KinsectData->HoverDrainRate : FALLBACK_HOVER_DRAIN;
}

float AKinsect::GetFlightDrainRate() const
{
	return KinsectData ? KinsectData->FlightDrainRate : FALLBACK_FLIGHT_DRAIN;
}

void AKinsect::SweepHitzones(float DeltaTime)
{
	if (!Collision || !GetWorld())
	{
		return;
	}

	const FVector CurrentLocation = GetActorLocation();

	// 仅 Hitzone Object Channel（ECC_GameTraceChannel3）
	FCollisionObjectQueryParams ObjectQueryParams;
	ObjectQueryParams.AddObjectTypesToQuery(ECC_GameTraceChannel3);

	FCollisionQueryParams QueryParams(FName(TEXT("KinsectHitzoneSweep")), false);
	QueryParams.AddIgnoredActor(this);
	if (OwnerActor.IsValid())
	{
		QueryParams.AddIgnoredActor(OwnerActor.Get());
	}

	// 与 Collision 同尺寸的 Capsule
	const FCollisionShape CapsuleShape = FCollisionShape::MakeCapsule(
		Collision->GetUnscaledCapsuleRadius(),
		Collision->GetUnscaledCapsuleHalfHeight());

	TArray<FHitResult> Hits;
	GetWorld()->SweepMultiByObjectType(Hits, LastTickLocation, CurrentLocation,
		Collision->GetComponentQuat(), ObjectQueryParams, CapsuleShape, QueryParams);
	LastTickLocation = CurrentLocation;

	if (Hits.Num() == 0)
	{
		return;
	}

	// 按 Hit.Time 排序
	Hits.Sort([](const FHitResult& A, const FHitResult& B)
	{
		return A.Time < B.Time;
	});

	// 清理已失效组件的计时
	for (auto It = HitzoneHitTimers.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}

	const FGuid SweptFlightID = ActiveRequest.FlightInstanceID;
	const uint64 SweptSerial = ActiveRequest.CommandSerial;
	for (const FHitResult& Hit : Hits)
	{
		UMHGZMonsterHitzoneComponent* Hitzone =
			UMHGZMonsterHitzoneComponent::Resolve(Hit.GetComponent());
		if (!Hitzone || !IsAttackContactInFront(Hit))
		{
			continue;
		}
		HandleHitzoneHit(Hit, DeltaTime);
		if (State != EKinsectState::Flying || ActiveRequest.FlightInstanceID != SweptFlightID
			|| ActiveRequest.CommandSerial != SweptSerial)
		{
			break; // completion or a reentrant command invalidates this old sweep
		}
	}
}

void AKinsect::HandleHitzoneHit(const FHitResult& Hit, float DeltaTime)
{
	UMHGZMonsterHitzoneComponent* Hitzone =
		UMHGZMonsterHitzoneComponent::Resolve(Hit.GetComponent());
	if (!Hitzone || !ResourceComponent.IsValid())
	{
		return;
	}

	// 萃取：无颜色优先级、无部位名映射，直接用 Hitzone 的 ExtractColorTag
	if (ActiveRequest.ExtractMode == EKinsectExtractMode::FirstHitOnly)
	{
		if (!HasPendingExtract() && Hitzone->ExtractColorTag.IsValid())
		{
			CapturePendingExtractColor(Hitzone->ExtractColorTag);
		}
	}
	if (ActiveRequest.DamageMode == EKinsectDamageMode::None)
	{
		return;
	}

	if (ActiveRequest.DamageMode == EKinsectDamageMode::SingleHit)
	{
		if (bHasDealtDamage)
		{
			return;
		}
		bHasDealtDamage = true;

		// 真实 FHitResult 全程保留并透传给 Resource
		const FGuid HitInstanceID = FGuid::NewGuid();
		const bool bDamageApplied = ResourceComponent->ApplyKinsectDamage(
			Hit, ActiveRequest.MotionValue, HitInstanceID,
			ActiveRequest.SourceAttackInstanceID);
		if (bDamageApplied && ActiveRequest.PowderSpawnPolicy != EIGPowderSpawnPolicy::None)
		{
			ResourceComponent->HandleSuccessfulKinsectDamage(
				ActiveRequest.RuntimeToken, ActiveRequest.SourceAttackInstanceID,
				HitInstanceID, Hit, ActiveRequest.PowderSpawnPolicy);
		}

		if (ActiveRequest.ExtractMode == EKinsectExtractMode::CollectPerValidHit
			&& bDamageApplied && Hitzone->ExtractColorTag.IsValid())
		{
			ResourceComponent->CaptureExtractFromKinsectHit(ActiveRequest, Hit, HitInstanceID);
		}

		// 单发命中：立即结束飞行；结束策略由 PostFlightPolicy 决定
		EndFlight(EKinsectFlightEndReason::HitzoneHit);
		return;
	}

	// Piercing：首碰立即命中；之后按绝对世界时间对每个 Hitzone 独立限频。
	// 不能只在重叠帧累计 DeltaTime，否则高速穿过部位时首击会被错误吞掉。
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	if (const float* LastHitTime = HitzoneHitTimers.Find(Hitzone);
		LastHitTime && Now - *LastHitTime < ActiveRequest.RehitInterval)
	{
		return;
	}

	const FKinsectFlightRequest HitRequest = ActiveRequest;
	const auto IsHitFlightCurrent = [&]()
	{
		return State == EKinsectState::Flying && ResourceComponent.IsValid()
			&& ActiveRequest.FlightInstanceID == HitRequest.FlightInstanceID
			&& ActiveRequest.CommandSerial == HitRequest.CommandSerial
			&& ResourceComponent->IsKinsectCommandCurrent(HitRequest);
	};
	const FGuid HitInstanceID = FGuid::NewGuid();
	const bool bDamageApplied = ResourceComponent->ApplyKinsectDamage(
		Hit, HitRequest.MotionValue, HitInstanceID, HitRequest.SourceAttackInstanceID);
	if (!bDamageApplied || !IsHitFlightCurrent())
	{
		return;
	}
	HitzoneHitTimers.Add(Hitzone, Now);
	// 强化贯通成功伤害只写入猎虫携带；角色点灯统一等回手交付。
	if (HitRequest.ExtractMode == EKinsectExtractMode::CollectPerValidHit
		&& Hitzone->ExtractColorTag.IsValid())
	{
		ResourceComponent->CaptureExtractFromKinsectHit(HitRequest, Hit, HitInstanceID);
	}
	if (!IsHitFlightCurrent()) return;

	// 产粉策略统一在真实伤害 GE 成功后评估；失败命中不会启动冷却、占用ID或留下粉尘。
	if (ActiveRequest.PowderSpawnPolicy != EIGPowderSpawnPolicy::None
		&& !ResourceComponent->HandleSuccessfulKinsectDamage(
			ActiveRequest.RuntimeToken, ActiveRequest.SourceAttackInstanceID,
			HitInstanceID, Hit, ActiveRequest.PowderSpawnPolicy))
	{
		UE_LOG(LogTemp, VeryVerbose,
			TEXT("[Kinsect] Successful hit did not produce powder under policy %d (attack=%s hit=%s)."),
			static_cast<int32>(ActiveRequest.PowderSpawnPolicy),
			*ActiveRequest.SourceAttackInstanceID.ToString(), *HitInstanceID.ToString());
	}
}

void AKinsect::CheckFlightProgress()
{
	const FVector CurrentLocation = GetActorLocation();
	const float Travelled = FVector::Dist(CurrentLocation, FlightStartLocation);
	const bool bPreciseToPoint =
		ActiveRequest.TrajectoryMode == EKinsectTrajectoryMode::ToPoint
		&& (ActiveRequest.CommandSource == EKinsectCommandSource::PowderVortex
			|| ActiveRequest.CommandSource == EKinsectCommandSource::DescendingThrust);

	// Preserve distance-first behavior except for precise vortex/dive endpoints,
	// which must resolve arrival before their distance cap.
	if (!bPreciseToPoint && Travelled >= ActiveRequest.MaxDistance)
	{
		EndFlight(EKinsectFlightEndReason::MaxDistance);
		return;
	}

	if (ActiveRequest.TrajectoryMode == EKinsectTrajectoryMode::ToPoint)
	{
		FVector TargetPoint;
		if (!ResolveToPointTarget(ActiveRequest, TargetPoint))
		{
			EndFlight(EKinsectFlightEndReason::Interrupted);
			return;
		}
		ActiveRequest.TargetPointSnapshot = TargetPoint;
		const FVector ToTarget = TargetPoint - CurrentLocation;
		const bool bWithinArrivalRadius =
			ToTarget.SizeSquared() <= FMath::Square(ActiveRequest.ArrivalRadius);
		const bool bPassedTarget =
			FVector::DotProduct(ToTarget, Movement->Velocity.GetSafeNormal()) < 0.f;
		if ((!ActiveRequest.bRequireHitzoneContactForArrival && bWithinArrivalRadius) || bPassedTarget)
		{
			// Snap auto-mark approaches to their moving surface point to avoid
			// repeated overshoot/reapproach. The damaging dive is already clamped
			// before its contact sweep; keep its final arrival at the exact snapshot.
			if (bPassedTarget
				&& (ActiveRequest.CommandSource == EKinsectCommandSource::AutoMark
					|| ActiveRequest.CommandSource == EKinsectCommandSource::DescendingThrust))
			{
				SetActorLocation(ActiveRequest.TargetPointSnapshot, false, nullptr,
					ETeleportType::TeleportPhysics);
				LastTickLocation = ActiveRequest.TargetPointSnapshot;
			}
			EndFlight(EKinsectFlightEndReason::Arrival);
			return;
		}

		if (bPreciseToPoint && Travelled >= ActiveRequest.MaxDistance)
		{
			EndFlight(EKinsectFlightEndReason::MaxDistance);
		}
		return;
	}

	if (Travelled >= ActiveRequest.MaxDistance)
	{
		EndFlight(EKinsectFlightEndReason::MaxDistance);
	}
}

bool AKinsect::ResolveToPointTarget(const FKinsectFlightRequest& Request,
	FVector& OutLocation) const
{
	if (Request.bFollowTargetComponent)
	{
		UPrimitiveComponent* Component = Request.TargetComponent.Get();
		if (!IsValid(Component) || Request.TargetLocalPoint.ContainsNaN())
		{
			return false;
		}
		if (!Request.TargetBoneName.IsNone())
		{
			if (const USkeletalMeshComponent* SkeletalMesh =
				Cast<USkeletalMeshComponent>(Component))
			{
				OutLocation = SkeletalMesh->GetSocketTransform(
					Request.TargetBoneName, RTS_World).TransformPosition(
					Request.TargetLocalPoint);
				return !OutLocation.ContainsNaN();
			}
		}
		OutLocation = Component->GetComponentTransform().TransformPosition(
			Request.TargetLocalPoint);
		return !OutLocation.ContainsNaN();
	}
	OutLocation = Request.TargetPointSnapshot;
	return !OutLocation.ContainsNaN();
}

void AKinsect::UpdateMovingToPointTarget()
{
	FVector TargetPoint;
	if (!ResolveToPointTarget(ActiveRequest, TargetPoint))
	{
		EndFlight(EKinsectFlightEndReason::Interrupted);
		return;
	}
	ActiveRequest.TargetPointSnapshot = TargetPoint;
	const FVector ToTarget = TargetPoint - GetActorLocation();
	if (!ToTarget.IsNearlyZero())
	{
		ArmMovement(ToTarget, ActiveRequest.FlightSpeed, true);
	}
}

void AKinsect::EndFlight(EKinsectFlightEndReason Reason)
{
	const FKinsectFlightRequest EndedRequest = ActiveRequest;
	if (ActiveRequest.PostFlightPolicy == EKinsectPostFlightPolicy::ReturnToOwner)
	{
		StartReturn(ActiveRequest.CommandSerial, ActiveRequest.CommandSource);
	}
	else
	{
		StopAndHover();
		if (Reason == EKinsectFlightEndReason::Arrival
			&& EndedRequest.CommandSource == EKinsectCommandSource::AutoMark)
		{
			FaceAutomaticMark(EndedRequest.CommandSerial, EndedRequest.MarkSerial);
		}
	}
	if (ResourceComponent.IsValid())
	{
		ResourceComponent->OnKinsectFlightEnded(EndedRequest, Reason);
	}
}

void AKinsect::StopAndHover()
{
	Movement->StopMovementImmediately();
	Movement->Deactivate();
	if (Collision)
	{
		Collision->DisableKinsectCollision();
	}
	State = EKinsectState::Hovering;
	bHasLastReturnRelativeLocation = false;
}

bool AKinsect::ArmMovement(const FVector& Direction, float Speed,
	const bool bKeepCollisionOrientation)
{
	const FVector SafeDirection = Direction.GetSafeNormal();
	if (!Movement || !Collision || SafeDirection.IsNearlyZero() ||
		!FMath::IsFinite(Speed) || Speed <= 0.f)
	{
		return false;
	}

	// UProjectileMovementComponent::StopSimulating（非反弹 WorldStatic 命中）会
	// Deactivate 并可能 SetUpdatedComponent(nullptr)。重发/召回都必须完整复位。
	if (Movement->UpdatedComponent != Collision)
	{
		Movement->SetUpdatedComponent(Collision);
	}
	Movement->SetComponentTickEnabled(true);
	if (!Movement->IsActive())
	{
		Movement->Activate(true);
	}
	Movement->MaxSpeed = Speed;
	Movement->bSweepCollision = false; // damage still uses the explicit Hitzone sweep
	Movement->Velocity = SafeDirection * Speed;
	Movement->UpdateComponentVelocity();
	Movement->bRotationFollowsVelocity = !bKeepCollisionOrientation;
	if (bKeepCollisionOrientation && Mesh)
	{
		// A hovering capsule resting against a floor must not rotate into that
		// floor when auto-seek starts. Only the visual mesh follows the new heading.
		const FQuat MeshFacingOffset(FVector::UpVector, -UE_HALF_PI);
		Mesh->SetWorldRotation(SafeDirection.ToOrientationQuat() * MeshFacingOffset);
	}
	else
	{
		SetActorRotation(SafeDirection.Rotation());
	}
	return true;
}

void AKinsect::SetFlightVisualFacing(bool bInFlight)
{
	if (!Mesh)
	{
		return;
	}

	if (!bInFlight)
	{
		if (bHasAttachedMeshRelativeRotation)
		{
			Mesh->SetRelativeRotation(AttachedMeshRelativeRotation);
			Mesh->SetRelativeScale3D(AttachedMeshRelativeScale);
		}
		return;
	}

	// 该 Mesh 的 root +Y 是虫子的实际面朝方向，+Z 为背部上方，+X 为左。
	// Projectile/Actor 的 +X 为飞行前方、+Y 为右、+Z 为上，因此只需 Yaw -90°：
	// root Y -> actor X，root Z -> actor Z，root X -> actor -Y。
	const FQuat FlightVisualRotation(FVector::UpVector, -UE_HALF_PI);
	Mesh->SetRelativeRotation(FlightVisualRotation.Rotator());
}

void AKinsect::TickReturn()
{
	if (ReturnCommandSerial != 0
		&& (!ResourceComponent.IsValid()
			|| !ResourceComponent->IsKinsectCommandCurrent(ReturnCommandSerial,
				ReturnCommandSource, ReturnMarkSerial, ReturnRuntimeToken)))
	{
		StopAndHover();
		return;
	}

	FVector SocketLocation;
	if (!ResolveAttachSocketLocation(SocketLocation))
	{
		// 无有效附着目标：安全悬停
		StopAndHover();
		return;
	}

	const FVector ToTarget = SocketLocation - GetActorLocation();
	const float Distance = ToTarget.Size();
	const FVector CurrentRelativeLocation = GetActorLocation() - SocketLocation;
	const bool bCrossedSocketBetweenTicks = bHasLastReturnRelativeLocation
		&& FMath::PointDistToSegment(FVector::ZeroVector,
			LastReturnRelativeLocation, CurrentRelativeLocation) <= RETURN_SWEEP_TOLERANCE;

	if (Distance <= ReturnArrivalRadius || bCrossedSocketBetweenTicks)
	{
		if (!ResourceComponent.IsValid())
		{
			StopAndHover();
			return;
		}
		// 先局部取出并清 Pending，再 Attach，再回调一次
		const FGameplayTagContainer DeliveredColors = TakePendingExtractColors();
		const uint64 CompletedCommandSerial = ReturnCommandSerial;
		const EKinsectCommandSource CompletedCommandSource = ReturnCommandSource;
		SetActorLocation(SocketLocation, false, nullptr, ETeleportType::TeleportPhysics);
		bHasLastReturnRelativeLocation = false;
		AttachToPlayer(AttachComponent.Get(), AttachSocketName);
		ResourceComponent->OnKinsectReachedPlayer(DeliveredColors,
			CompletedCommandSerial, CompletedCommandSource);
		return;
	}
	LastReturnRelativeLocation = CurrentRelativeLocation;
	bHasLastReturnRelativeLocation = true;

	const float ReturnSpeed = KinsectData ? KinsectData->ReturnSpeed : FALLBACK_RETURN_SPEED;
	if (!ArmMovement(ToTarget, ReturnSpeed))
	{
		StopAndHover();
	}
}

bool AKinsect::ResolveAttachSocketLocation(FVector& OutLocation) const
{
	if (AttachComponent.IsValid())
	{
		if (AttachSocketName != NAME_None && AttachComponent->DoesSocketExist(AttachSocketName))
		{
			OutLocation = AttachComponent->GetSocketLocation(AttachSocketName);
		}
		else
		{
			OutLocation = AttachComponent->GetComponentLocation();
		}
		return true;
	}

	if (OwnerActor.IsValid())
	{
		OutLocation = OwnerActor->GetActorLocation();
		return true;
	}
	return false;
}

void AKinsect::OnWorldCollision(UPrimitiveComponent* HitComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	// Keep the reflected callback for old assets, but ignore late terrain events.
	// Neither extraction, damage, stop-simulation nor arrival is driven by this.
	(void)HitComp; (void)OtherActor; (void)OtherComp; (void)NormalImpulse; (void)Hit;
}
