// Copyright MHGZ Project. All Rights Reserved.

#include "Kinsect.h"
#include "KinsectCollisionComponent.h"
#include "InsectGlaiveKinsectData.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"
#include "AttributeSystem/Res_InsectGlaive.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"

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

	if (State != EKinsectState::Flying && State != EKinsectState::Returning)
	{
		return;
	}

	// 飞行/返回期间每帧核对 Runtime Token；资源失效则安全停住
	if (!ResourceComponent.IsValid() ||
		!ResourceComponent->IsRuntimeRequestCurrent(ActiveRequest.RuntimeToken))
	{
		Interrupt();
		return;
	}

	switch (State)
	{
	case EKinsectState::Flying:
		// 先 Hitzone Sweep，再处理墙
		SweepHitzones(DeltaTime);
		if (State != EKinsectState::Flying)
		{
			break; // SingleHit 已结束飞行
		}
		if (PendingWorldHit.bBlockingHit)
		{
			HandleWorldHit();
			break;
		}
		CheckFlightProgress();
		break;

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
		if (Request.TargetPointSnapshot.ContainsNaN())
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

bool AKinsect::BeginFlight(const FKinsectFlightRequest& Request)
{
	// 完整校验，全部通过后才提交，保证原子性
	if (!IsFlightRequestValid(Request))
	{
		return false;
	}

	FVector FlightDirection;
	if (Request.TrajectoryMode == EKinsectTrajectoryMode::AlongDirection)
	{
		FlightDirection = Request.DirectionSnapshot.GetSafeNormal();
	}
	else
	{
		FlightDirection = (Request.TargetPointSnapshot - GetActorLocation()).GetSafeNormal();
	}
	if (FlightDirection.IsNearlyZero())
	{
		return false; // 目标点与当前位置重合
	}
	if (!Movement || !Collision)
	{
		return false;
	}

	// 原子提交
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	SetFlightVisualFacing(true);
	if (!ArmMovement(FlightDirection, Request.FlightSpeed))
	{
		// BeginFlight 的失败不能留下一个从主人脱离、却不可移动的猎虫。
		AttachToPlayer(AttachComponent.Get(), AttachSocketName);
		return false;
	}
	Collision->EnableKinsectCollision();

	ActiveRequest = Request;
	FlightStartLocation = GetActorLocation();
	LastTickLocation = GetActorLocation();
	PendingWorldHit.Reset();
	bHasDealtDamage = false;
	HitzoneHitTimers.Reset();
	State = EKinsectState::Flying;
	return true;
}

void AKinsect::Interrupt()
{
	if (State == EKinsectState::Attached)
	{
		return;
	}

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
}

void AKinsect::StartReturn()
{
	if (State == EKinsectState::Attached || State == EKinsectState::Returning)
	{
		return; // 幂等
	}

	const float ReturnSpeed = KinsectData ? KinsectData->ReturnSpeed : FALLBACK_RETURN_SPEED;
	if (ReturnSpeed <= 0.f)
	{
		StopAndHover();
		return;
	}

	State = EKinsectState::Returning;
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
	const float ReturnArrivalRadius = ActiveRequest.ArrivalRadius > 0.f
		? ActiveRequest.ArrivalRadius : RETURN_ARRIVAL_DISTANCE;
	if (ToTarget.Size() > ReturnArrivalRadius && !ArmMovement(ToTarget, ReturnSpeed))
	{
		StopAndHover();
	}
}

void AKinsect::AttachToPlayer(USceneComponent* InAttachComponent, FName InSocketName)
{
	if (Mesh && !bHasAttachedMeshRelativeRotation)
	{
		AttachedMeshRelativeRotation = Mesh->GetRelativeRotation();
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

	if (Collision)
	{
		Collision->DisableKinsectCollision();
	}
}

FGameplayTag AKinsect::TakePendingExtractColor()
{
	const FGameplayTag Extracted = PendingExtractColor;
	PendingExtractColor = FGameplayTag();
	return Extracted;
}

float AKinsect::GetFlightSpeed() const
{
	return KinsectData ? KinsectData->FlightSpeed : FALLBACK_FLIGHT_SPEED;
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

	for (const FHitResult& Hit : Hits)
	{
		UMHGZMonsterHitzoneComponent* Hitzone =
			Cast<UMHGZMonsterHitzoneComponent>(Hit.GetComponent());
		if (!Hitzone)
		{
			continue;
		}
		HandleHitzoneHit(Hit, DeltaTime);
		if (State != EKinsectState::Flying)
		{
			break; // 飞行已结束（SingleHit 立即结束）
		}
	}
}

void AKinsect::HandleHitzoneHit(const FHitResult& Hit, float DeltaTime)
{
	UMHGZMonsterHitzoneComponent* Hitzone =
		Cast<UMHGZMonsterHitzoneComponent>(Hit.GetComponent());
	if (!Hitzone || !ResourceComponent.IsValid())
	{
		return;
	}

	// 萃取：无颜色优先级、无部位名映射，直接用 Hitzone 的 ExtractColorTag
	if (ActiveRequest.ExtractMode == EKinsectExtractMode::FirstHitOnly)
	{
		if (!PendingExtractColor.IsValid() && Hitzone->ExtractColorTag.IsValid())
		{
			PendingExtractColor = Hitzone->ExtractColorTag;
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
		const bool bDamageApplied = ResourceComponent->ApplyKinsectDamage(
			Hit, ActiveRequest.MotionValue, FGuid::NewGuid());

		if (ActiveRequest.ExtractMode == EKinsectExtractMode::ApplyPerValidHit
			&& bDamageApplied && Hitzone->ExtractColorTag.IsValid())
		{
			ResourceComponent->ApplyExtract(Hitzone->ExtractColorTag);
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

	const bool bDamageApplied = ResourceComponent->ApplyKinsectDamage(
		Hit, ActiveRequest.MotionValue, FGuid::NewGuid());
	if (!bDamageApplied)
	{
		return;
	}
	HitzoneHitTimers.Add(Hitzone, Now);

	// 伤害成功且 ExtractMode=ApplyPerValidHit 后立即 Apply
	if (ActiveRequest.ExtractMode == EKinsectExtractMode::ApplyPerValidHit
		&& Hitzone->ExtractColorTag.IsValid())
	{
		ResourceComponent->ApplyExtract(Hitzone->ExtractColorTag);
	}
}

void AKinsect::HandleWorldHit()
{
	PendingWorldHit.Reset();
	EndFlight(EKinsectFlightEndReason::WorldHit);
}

void AKinsect::CheckFlightProgress()
{
	const FVector CurrentLocation = GetActorLocation();

	// 距离从 FlightStartLocation 计算
	const float Travelled = FVector::Dist(CurrentLocation, FlightStartLocation);
	if (Travelled >= ActiveRequest.MaxDistance)
	{
		EndFlight(EKinsectFlightEndReason::MaxDistance);
		return;
	}

	if (ActiveRequest.TrajectoryMode == EKinsectTrajectoryMode::ToPoint)
	{
		const FVector ToTarget = ActiveRequest.TargetPointSnapshot - CurrentLocation;
		const bool bWithinArrivalRadius =
			ToTarget.SizeSquared() <= FMath::Square(ActiveRequest.ArrivalRadius);
		const bool bPassedTarget =
			FVector::DotProduct(ToTarget, Movement->Velocity.GetSafeNormal()) < 0.f;
		if (bWithinArrivalRadius || bPassedTarget)
		{
			EndFlight(EKinsectFlightEndReason::Arrival);
		}
	}
}

void AKinsect::EndFlight(EKinsectFlightEndReason Reason)
{
	if (ActiveRequest.PostFlightPolicy == EKinsectPostFlightPolicy::ReturnToOwner)
	{
		StartReturn();
	}
	else
	{
		StopAndHover();
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
}

bool AKinsect::ArmMovement(const FVector& Direction, float Speed)
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
	Movement->Velocity = SafeDirection * Speed;
	Movement->UpdateComponentVelocity();
	SetActorRotation(SafeDirection.Rotation());
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
	FVector SocketLocation;
	if (!ResolveAttachSocketLocation(SocketLocation))
	{
		// 无有效附着目标：安全悬停
		StopAndHover();
		return;
	}

	const FVector ToTarget = SocketLocation - GetActorLocation();
	const float Distance = ToTarget.Size();

	const float ReturnArrivalRadius = ActiveRequest.ArrivalRadius > 0.f
		? ActiveRequest.ArrivalRadius : RETURN_ARRIVAL_DISTANCE;
	if (Distance <= ReturnArrivalRadius)
	{
		if (!ResourceComponent.IsValid())
		{
			StopAndHover();
			return;
		}
		// 先局部取出并清 Pending，再 Attach，再回调一次
		const FGameplayTag DeliveredColor = TakePendingExtractColor();
		AttachToPlayer(AttachComponent.Get(), AttachSocketName);
		ResourceComponent->OnKinsectReachedPlayer(DeliveredColor);
		return;
	}

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
	if (State != EKinsectState::Flying)
	{
		return;
	}

	// 只缓存真实 HitResult 并停 Movement；墙处理统一放在 Actor Tick（先 Hitzone Sweep 再处理墙）
	PendingWorldHit = Hit;
	Movement->StopMovementImmediately();
}
