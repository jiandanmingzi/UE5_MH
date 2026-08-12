// Copyright MHGZ Project. All Rights Reserved.

#include "IGMarkProjectile.h"
#include "AttributeSystem/Res_InsectGlaive.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"

AIGMarkProjectile::AIGMarkProjectile()
{
	PrimaryActorTick.bCanEverTick = true;

	Sphere = CreateDefaultSubobject<USphereComponent>(TEXT("Sphere"));
	RootComponent = Sphere;
	Sphere->InitSphereRadius(12.f);
	Sphere->SetGenerateOverlapEvents(false);

	Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
	Movement->UpdatedComponent = Sphere;
	Movement->bAutoActivate = false;   // 仅 Initialize 后显式激活
	Movement->InitialSpeed = 0.f;
	Movement->MaxSpeed = 0.f;
	Movement->ProjectileGravityScale = 0.f; // 无重力
	Movement->bShouldBounce = false;        // 不反弹
	Movement->bRotationFollowsVelocity = true;
}

void AIGMarkProjectile::BeginPlay()
{
	Super::BeginPlay();

	if (Sphere)
	{
		Sphere->OnComponentHit.AddDynamic(this, &AIGMarkProjectile::OnSphereHit);
	}

	if (Movement)
	{
		// 确保 Movement 先于 Actor Tick 推进，Previous→Current 扫描才能覆盖本帧位移
		AddTickPrerequisiteComponent(Movement);
	}
}

void AIGMarkProjectile::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bInitialized)
	{
		return;
	}

	// 每帧核对 Runtime Token；失效即销毁（覆盖持续虫印与飞行两种状态）
	if (!Resource.IsValid() || !Resource->IsRuntimeRequestCurrent(RuntimeToken))
	{
		Destroy();
		return;
	}

	// 持续虫印：保持在世界中，不再移动/扫描
	if (bIsAttachedMark)
	{
		return;
	}

	// 与猎虫飞行一致：先扫描 Previous→Current 的 Hitzone，再处理本帧墙阻挡。
	// ProjectileMovement 已把 Current 截在墙面，因此不会选中墙后的部位。
	SweepHitzones();
	if (bIsAttachedMark)
	{
		return;
	}
	if (bBlockedByWorld)
	{
		Destroy();
		return;
	}

	// 超时
	ElapsedTime += DeltaTime;
	if (Lifetime > 0.f && ElapsedTime >= Lifetime)
	{
		Destroy();
		return;
	}

	// 超距（自 FlightStartLocation 起算）
	const float Travelled = FVector::Dist(GetActorLocation(), FlightStartLocation);
	if (MaxDistance > 0.f && Travelled >= MaxDistance)
	{
		Destroy();
		return;
	}

}

void AIGMarkProjectile::Initialize(URes_InsectGlaive* InResource,
	const FWeaponRuntimeToken& InRuntimeToken, const FWeaponAimSnapshot& AimSnapshot,
	float InSpeed, float InRadius, float InMaxDistance, float InLifetime)
{
	InitializeInternal(InResource, InRuntimeToken, AimSnapshot.Direction.GetSafeNormal(),
		InSpeed, InRadius, InMaxDistance, InLifetime);
}

void AIGMarkProjectile::Initialize(URes_InsectGlaive* InResource,
	const FWeaponRuntimeToken& InRuntimeToken, const FVector& StartDirection,
	float InSpeed, float InRadius, float InMaxDistance, float InLifetime)
{
	InitializeInternal(InResource, InRuntimeToken, StartDirection.GetSafeNormal(),
		InSpeed, InRadius, InMaxDistance, InLifetime);
}

void AIGMarkProjectile::InitializeInternal(URes_InsectGlaive* InResource,
	const FWeaponRuntimeToken& InRuntimeToken, const FVector& Direction,
	float InSpeed, float InRadius, float InMaxDistance, float InLifetime)
{
	Resource = InResource;
	RuntimeToken = InRuntimeToken;

	if (Sphere)
	{
		Sphere->InitSphereRadius(FMath::Max(InRadius, 1.f));
		// 物理只 Block WorldStatic；不 Overlap Hitzone
		Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Sphere->SetCollisionResponseToAllChannels(ECR_Ignore);
		Sphere->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
		Sphere->SetCollisionResponseToChannel(ECC_GameTraceChannel3, ECR_Ignore); // Hitzone
		Sphere->SetGenerateOverlapEvents(false);
	}

	MaxDistance = FMath::Max(InMaxDistance, 0.f); // <=0 视为不限制
	Lifetime = FMath::Max(InLifetime, 0.f);       // <=0 视为不限制
	ElapsedTime = 0.f;
	FlightStartLocation = GetActorLocation();
	LastTickLocation = GetActorLocation();
	bBlockedByWorld = false;
	bIsAttachedMark = false;
	bInitialized = true;

	if (Movement && !Direction.IsNearlyZero() && InSpeed > 0.f)
	{
		Movement->MaxSpeed = InSpeed;
		Movement->Activate();
		Movement->Velocity = Direction * InSpeed;
	}
	else
	{
		// 无效参数：不飞行，直接自毁
		Destroy();
	}
}

void AIGMarkProjectile::SweepHitzones()
{
	if (!Sphere || !GetWorld())
	{
		return;
	}

	const FVector CurrentLocation = GetActorLocation();

	// 仅 Hitzone Object Channel（ECC_GameTraceChannel3）
	FCollisionObjectQueryParams ObjectQueryParams;
	ObjectQueryParams.AddObjectTypesToQuery(ECC_GameTraceChannel3);

	FCollisionQueryParams QueryParams(FName(TEXT("IGMarkHitzoneSweep")), false);
	QueryParams.AddIgnoredActor(this);

	const FCollisionShape SphereShape =
		FCollisionShape::MakeSphere(Sphere->GetUnscaledSphereRadius());

	TArray<FHitResult> Hits;
	GetWorld()->SweepMultiByObjectType(Hits, LastTickLocation, CurrentLocation,
		FQuat::Identity, ObjectQueryParams, SphereShape, QueryParams);
	LastTickLocation = CurrentLocation;

	if (Hits.Num() == 0)
	{
		return;
	}

	// 按 Time 取最早命中
	Hits.Sort([](const FHitResult& A, const FHitResult& B)
	{
		return A.Time < B.Time;
	});

	const FHitResult& Earliest = Hits[0];
	UMHGZMonsterHitzoneComponent* Hitzone =
		Cast<UMHGZMonsterHitzoneComponent>(Earliest.GetComponent());
	if (!Hitzone)
	{
		return;
	}

	// 收编失败：销毁
	if (!Resource.IsValid() ||
		!Resource->SetKinsectMark(Hitzone, Earliest.ImpactPoint, this))
	{
		Destroy();
		return;
	}

	// 成功后停止并 AttachToComponent 保持世界位置，成为持续虫印
	Movement->StopMovementImmediately();
	Movement->Deactivate();
	AttachToComponent(Hitzone, FAttachmentTransformRules::KeepWorldTransform);
	bIsAttachedMark = true;
}

void AIGMarkProjectile::OnSphereHit(UPrimitiveComponent* HitComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (!bInitialized || bIsAttachedMark)
	{
		return;
	}

	// 物理仅 Block WorldStatic：缓存撞墙，由 Tick 销毁
	Movement->StopMovementImmediately();
	bBlockedByWorld = true;
}
