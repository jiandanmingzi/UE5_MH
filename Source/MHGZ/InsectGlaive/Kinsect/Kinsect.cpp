// Copyright MHGZ Project. All Rights Reserved.

#include "Kinsect.h"
#include "KinsectCollisionComponent.h"
#include "InsectGlaiveKinsectData.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"
#include "AttributeSystem/Res_InsectGlaive.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"

AKinsect::AKinsect()
{
	PrimaryActorTick.bCanEverTick = true;

	Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;

	Collision = CreateDefaultSubobject<UKinsectCollisionComponent>(TEXT("Collision"));
	Collision->SetupAttachment(Mesh);

	Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
	Movement->bAutoActivate = false;
	Movement->InitialSpeed = 0.f;
	Movement->MaxSpeed = 3000.f;
	Movement->ProjectileGravityScale = 0.f; // 无重力，可悬停
}

void AKinsect::BeginPlay()
{
	Super::BeginPlay();

	// 绑定 Overlap/Hit 事件
	if (Collision)
	{
		Collision->OnComponentBeginOverlap.AddDynamic(this, &AKinsect::OnHitMonsterHitzone);
		Collision->OnComponentHit.AddDynamic(this, &AKinsect::OnWorldCollision);
	}
}

void AKinsect::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	switch (State)
	{
	case EKinsectState::Flying:
	{
		TimeSinceLastDamage += DeltaTime;

		// 贯穿伤害
		if (DamageMode == EKinsectDamageMode::Piercing)
		{
			TryApplyKinsectDamage(DeltaTime);
		}

		// 检查是否应停止
		if (ShouldStopFlying())
		{
			OnFlightEnded();
		}

		// 检查是否超过最大距离
		if (bFollowRay)
		{
			const float Dist = FVector::Dist(GetActorLocation(),
				OwnerActor.IsValid() ? OwnerActor->GetActorLocation() : FVector::ZeroVector);
			if (Dist >= MaxFlightRange)
			{
				OnFlightEnded();
			}
		}
		break;
	}
	case EKinsectState::Returning:
	{
		// 每帧追踪玩家位置
		if (OwnerActor.IsValid() && Movement)
		{
			const FVector PlayerLoc = OwnerActor->GetActorLocation();
			const FVector ToPlayer = PlayerLoc - GetActorLocation();
			const float Dist = ToPlayer.Size();

			if (Dist < 50.f) // 到达
			{
				Movement->Velocity = FVector::ZeroVector;
				// AttachToPlayer 由 ResourceComponent 回调处理
				if (ResourceComponent.IsValid())
				{
					State = EKinsectState::Recalled;
					ResourceComponent->OnKinsectReachedPlayer(PendingExtractColor);
				}
			}
			else
			{
				Movement->Velocity = ToPlayer.GetSafeNormal() * GetFlightSpeed();
			}
		}
		break;
	}
	default:
		break;
	}
}

void AKinsect::StartFlightAlongRay(FVector InRayDirection, float MaxDistance)
{
	InRayDirection = InRayDirection.GetSafeNormal();
	MaxFlightRange = MaxDistance;

	State = EKinsectState::Flying;
	bFollowRay = true;
	FlyDestination = FVector::ZeroVector;

	Movement->Velocity = InRayDirection * GetFlightSpeed();
	Collision->EnableKinsectCollision();

	TimeSinceLastDamage = 999.f;
	bHasDealtDamage = false;

	FlyPlayRate = 1.5f;
}

void AKinsect::StartFlightToPoint(FVector Destination)
{
	FlyDestination = Destination;
	bFollowRay = false;

	State = EKinsectState::Flying;

	const FVector Dir = (Destination - GetActorLocation()).GetSafeNormal();
	Movement->Velocity = Dir * GetFlightSpeed();
	Collision->EnableKinsectCollision();

	TimeSinceLastDamage = 999.f;
	bHasDealtDamage = false;

	FlyPlayRate = 1.5f;
}

void AKinsect::SetDamageParams(EKinsectDamageMode InDamageMode, float InMotionValue,
	float InDamageInterval, EKinsectExtractMode InExtractMode)
{
	DamageMode = InDamageMode;
	CurrentMotionValue = InMotionValue;
	CurrentDamageInterval = InDamageInterval;
	ExtractMode = InExtractMode;

	TimeSinceLastDamage = 999.f;
	bHasDealtDamage = false;
}

void AKinsect::StopAndHover()
{
	Movement->Velocity = FVector::ZeroVector;
	State = EKinsectState::Hovering;
	FlyPlayRate = 0.3f;
}

void AKinsect::StartReturn()
{
	if (State == EKinsectState::Returning) return; // 已在返回中

	State = EKinsectState::Returning;
	FlyPlayRate = 1.0f;
}

void AKinsect::ForceRecall()
{
	StartReturn();
	// ★ 不清除 PendingExtractColor——耐力归零保留已萃取灯
}

void AKinsect::Interrupt()
{
	Movement->Velocity = FVector::ZeroVector;
	bFollowRay = false;
	FlyDestination = FVector::ZeroVector;
	// ★ 不修改 PendingExtractColor——若已萃取则保留
}

void AKinsect::AttachToPlayer(USceneComponent* ArmSocket)
{
	AttachToComponent(ArmSocket, FAttachmentTransformRules::SnapToTargetIncludingScale);
	State = EKinsectState::Attached;
	OwnerActor = ArmSocket->GetOwner();
	Collision->DisableKinsectCollision();
	Movement->Velocity = FVector::ZeroVector;
}

void AKinsect::OnHitMonsterHitzone(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (State != EKinsectState::Flying) return;

	UMHGZMonsterHitzoneComponent* Hitzone = Cast<UMHGZMonsterHitzoneComponent>(OtherComp);
	if (!Hitzone) return;

	// 同一帧命中怪物+墙壁 → 怪物优先（先处理 Overlap，后续 Hit 中 State 已非 Flying 则忽略）

	// 记录萃取
	TryRecordExtract(Hitzone);

	// 单发模式 → Apply 伤害 + 标记
	if (DamageMode == EKinsectDamageMode::SingleHit && !bHasDealtDamage)
	{
		ApplyDamageOnce(Hitzone, CurrentMotionValue);
		bHasDealtDamage = true;
		// 下帧 Tick 中 ShouldStopFlying() 返回 true
	}
}

void AKinsect::OnWorldCollision(UPrimitiveComponent* HitComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (State != EKinsectState::Flying) return;

	// 撞墙 → 立即悬停
	Movement->Velocity = FVector::ZeroVector;
	State = EKinsectState::Hovering;
	FlyPlayRate = 0.3f;
}

void AKinsect::TryRecordExtract(UMHGZMonsterHitzoneComponent* Hitzone)
{
	if (!Hitzone || ExtractMode == EKinsectExtractMode::NoExtract) return;

	// 部位→萃取颜色映射（委托 ResourceComponent）
	FGameplayTag NewColor;
	if (ResourceComponent.IsValid())
	{
		NewColor = ResourceComponent->MapHitzoneToExtract(Hitzone->HitzoneTag);
	}

	if (!NewColor.IsValid()) return;

	switch (ExtractMode)
	{
	case EKinsectExtractMode::FirstHitOnly:
		if (!PendingExtractColor.IsValid())
		{
			PendingExtractColor = NewColor;
		}
		break;

	case EKinsectExtractMode::AlwaysOverwrite:
	{
		// 红(3) > 黄(2) > 白(1)
		static const TMap<FGameplayTag, int32> PriorityMap = {
			{FGameplayTag::RequestGameplayTag(TEXT("WeaponResource.IG.Extract.White")), 1},
			{FGameplayTag::RequestGameplayTag(TEXT("WeaponResource.IG.Extract.Yellow")), 2},
			{FGameplayTag::RequestGameplayTag(TEXT("WeaponResource.IG.Extract.Red")), 3}
		};

		const int32* NewPrio = PriorityMap.Find(NewColor);
		const int32* OldPrio = PriorityMap.Find(PendingExtractColor);
		if (!OldPrio || (NewPrio && *NewPrio > *OldPrio))
		{
			PendingExtractColor = NewColor;
		}
		break;
	}

	default:
		break;
	}
}

void AKinsect::TryApplyKinsectDamage(float DeltaTime)
{
	if (TimeSinceLastDamage < CurrentDamageInterval) return;

	UMHGZMonsterHitzoneComponent* Hitzone = GetOverlappingHitzone();
	if (!Hitzone) return;

	ApplyDamageOnce(Hitzone, CurrentMotionValue);
	TimeSinceLastDamage = 0.f;
}

void AKinsect::ApplyDamageOnce(UMHGZMonsterHitzoneComponent* Hitzone, float MotionValue)
{
	if (!Hitzone || !ResourceComponent.IsValid()) return;

	AActor* Monster = Hitzone->GetOwner();
	ResourceComponent->ApplyKinsectDamage(Hitzone, Monster, MotionValue);
}

UMHGZMonsterHitzoneComponent* AKinsect::GetOverlappingHitzone() const
{
	if (!Collision) return nullptr;

	TArray<AActor*> OverlappingActors;
	Collision->GetOverlappingActors(OverlappingActors);

	for (AActor* Actor : OverlappingActors)
	{
		TArray<UMHGZMonsterHitzoneComponent*> Hitzones;
		Actor->GetComponents<UMHGZMonsterHitzoneComponent>(Hitzones);
		if (Hitzones.Num() > 0)
		{
			return Hitzones[0];
		}
	}
	return nullptr;
}

bool AKinsect::ShouldStopFlying() const
{
	// 单发模式已伤害 → 停止
	if (DamageMode == EKinsectDamageMode::SingleHit && bHasDealtDamage)
	{
		return true;
	}
	return false;
}

void AKinsect::OnFlightEnded()
{
	if (PendingExtractColor.IsValid())
	{
		// 有萃取 → 自动返回
		StartReturn();
	}
	else
	{
		// 无萃取 → 悬停等待
		StopAndHover();
	}
}

float AKinsect::GetFlightSpeed() const
{
	if (KinsectData)
	{
		return KinsectData->FlightSpeed;
	}
	return 2000.f;
}

float AKinsect::GetHoverDrainRate() const
{
	if (KinsectData)
	{
		return KinsectData->HoverDrainRate;
	}
	return 3.f;
}

float AKinsect::GetFlightDrainRate() const
{
	if (KinsectData)
	{
		return KinsectData->FlightDrainRate;
	}
	return 8.f;
}
