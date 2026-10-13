// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZMonsterBase.h"
#include "MHGZMonsterHitzoneComponent.h"
#include "MHGZDummyConfig.h"
#include "ActionSystem/MHGZHitFeedbackRouterComponent.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/MeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"

AMHGZMonsterBase::AMHGZMonsterBase()
{
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AttributeSet = CreateDefaultSubobject<UMHGZAttributeSet>(TEXT("AttributeSet"));
	AbilitySystemComponent->AddAttributeSetSubobject(AttributeSet.Get());

	// M2 parallel-domain dependency: this header is provided by the feedback writer.
	// The dummy routes AttributeSet -> Router through this component.
	HitFeedbackRouter = CreateDefaultSubobject<UMHGZHitFeedbackRouterComponent>(TEXT("HitFeedbackRouter"));

	// 实体 Body 只负责物理阻挡；Hitzone 独立承担武器、准心与肉质查询。
	GetCapsuleComponent()->SetCollisionProfileName(TEXT("MonsterBody"));
	GetMesh()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	GetMesh()->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Ignore);

	// 怪物不可站立/行走：玩家不能把怪物身体当地板踩。
	//
	// 这**不是碰撞预设能表达的** —— FWalkableSlopeOverride 是组件属性，
	// FCollisionProfileName 里没有它。而 UCharacterMovementComponent::IsWalkable
	// 正是通过它改写坡度阈值：
	//     TestWalkableZ = HitComponent->GetWalkableSlopeOverride()
	//                         .ModifyWalkableFloorZ(WalkableFloorZ);
	// WalkableSlope_Unwalkable 让任何角度都判为不可行走。
	//
	// 为什么必须有：木桩的碰撞面就是这条角色胶囊，而胶囊圆顶的法线 Z ≈ 0.72
	// 恰好高于 UE 的可行走坡度阈值（cos 44.765° ≈ 0.71），于是 PhysFalling 认定
	// 它能站、CMC 自己落地、HandleLanded 报 Landed；但胶囊在那么小的曲面上无法
	// 稳定接触，陷入每 ~0.5 s 一次的穿透极限环，MovementMode 一直是 Falling ——
	// 实测角色因此在木桩顶滞留 3.2 秒、全程无法操作，最后靠几何滑落才脱困。
	FWalkableSlopeOverride UnwalkableSlope;
	UnwalkableSlope.WalkableSlopeBehavior = WalkableSlope_Unwalkable;
	GetCapsuleComponent()->SetWalkableSlopeOverride(UnwalkableSlope);
}

UAbilitySystemComponent* AMHGZMonsterBase::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void AMHGZMonsterBase::BeginPlay()
{
	Super::BeginPlay();
	InitializeHitEffectVisualSurfaceCollision();
	AbilitySystemComponent->InitAbilityActorInfo(this, this);
}

void AMHGZMonsterBase::InitializeHitEffectVisualSurfaceCollision()
{
	// The body capsule and gameplay hitzones are not visual surfaces. Keep them
	// out of this presentation-only query even if a project collision profile is
	// later expanded with the custom channel.
	if (UCapsuleComponent* Body = GetCapsuleComponent())
	{
		Body->SetCollisionResponseToChannel(ECC_GameTraceChannel4, ECR_Ignore);
	}

	TArray<UMHGZMonsterHitzoneComponent*> Hitzones;
	GetComponents<UMHGZMonsterHitzoneComponent>(Hitzones);
	for (UMHGZMonsterHitzoneComponent* Hitzone : Hitzones)
	{
		if (Hitzone)
		{
			Hitzone->SetCollisionResponseToChannel(ECC_GameTraceChannel4, ECR_Ignore);
		}
	}

	TArray<UMeshComponent*> Meshes;
	GetComponents<UMeshComponent>(Meshes);
	for (UMeshComponent* VisualMesh : Meshes)
	{
		if (!IsValid(VisualMesh) || !VisualMesh->IsVisible())
		{
			continue;
		}

		const bool bWasCollisionDisabled =
			VisualMesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision;
		if (bWasCollisionDisabled)
		{
			// Visual-only meshes become queryable for this one channel and remain
			// ignored by every gameplay/physics channel.
			VisualMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
		}
		VisualMesh->SetCollisionResponseToChannel(ECC_GameTraceChannel4, ECR_Overlap);
		if (bWasCollisionDisabled)
		{
			VisualMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		}
	}
}

void AMHGZMonsterBase::ForceRestoreAllChannels()
{
	TArray<UMHGZMonsterHitzoneComponent*> Hitzones;
	GetComponents<UMHGZMonsterHitzoneComponent>(Hitzones);
	for (UMHGZMonsterHitzoneComponent* HZ : Hitzones)
	{
		HZ->ForceRestoreAllChannels();
	}
}

void AMHGZMonsterBase::GenerateHitzonesFromConfig(UMHGZDummyConfig* Config)
{
	if (!Config) return;

	// ApplyConfig 可重复调用；重建前先移除旧的动态/蓝图部位，避免重复伤害判定。
	TArray<UMHGZMonsterHitzoneComponent*> ExistingHitzones;
	GetComponents<UMHGZMonsterHitzoneComponent>(ExistingHitzones);
	for (UMHGZMonsterHitzoneComponent* Existing : ExistingHitzones)
	{
		if (Existing)
		{
			Existing->DestroyComponent();
		}
	}

	for (const FDummyHitzoneConfig& HZConfig : Config->Hitzones)
	{
		const FName ComponentName = MakeUniqueObjectName(
			this, UMHGZMonsterHitzoneComponent::StaticClass(),
			FName(*FString::Printf(TEXT("Hitzone_%s"), *HZConfig.BoneName.ToString())));
		UMHGZMonsterHitzoneComponent* Hitzone =
			NewObject<UMHGZMonsterHitzoneComponent>(this, ComponentName);
		Hitzone->BoneName = HZConfig.BoneName;
		Hitzone->HitzoneTag = HZConfig.HitzoneTag;
		Hitzone->ExtractColorTag = HZConfig.ExtractColorTag;
		Hitzone->DefenseMultiplier = HZConfig.DefenseMultiplier;
		Hitzone->PhysicalHitzoneMultipliers = HZConfig.PhysicalHitzoneMultipliers;
		Hitzone->ElementHitzoneMultipliers = HZConfig.ElementHitzoneMultipliers;
		Hitzone->StaggerRate = HZConfig.StaggerRate;
		Hitzone->QueryShape = HZConfig.Shape;
		Hitzone->BoxHalfExtent = HZConfig.BoxHalfExtent;

		// 静态训练柱使用 None，直接挂到角色 Root；骨骼怪物仍跟随指定骨骼。
		USceneComponent* AttachmentParent = HZConfig.BoneName.IsNone()
			? GetRootComponent() : GetMesh();
		if (!AttachmentParent)
		{
			Hitzone->DestroyComponent();
			continue;
		}
		Hitzone->AttachToComponent(
			AttachmentParent,
			FAttachmentTransformRules::KeepRelativeTransform,
			HZConfig.BoneName);
		AddInstanceComponent(Hitzone);
		Hitzone->RegisterComponent();
		Hitzone->SetCollisionResponseToChannel(ECC_GameTraceChannel4, ECR_Ignore);
		Hitzone->SetRelativeLocation(HZConfig.RelativeLocation);
		Hitzone->Radius = FMath::Max(1.f, HZConfig.Radius);
		Hitzone->SetSphereRadius(Hitzone->Radius);
	}
}

bool AMHGZMonsterBase::ResolveHitEffectVisualSurface(
	const UMHGZMonsterHitzoneComponent* Hitzone,
	const FVector& CollisionPoint,
	const FVector& CollisionNormal,
	FVector& OutVisualPoint,
	FVector& OutVisualNormal) const
{
	return ResolveHitEffectVisualSurface(Hitzone, CollisionPoint,
		CollisionNormal, CollisionNormal, OutVisualPoint, OutVisualNormal);
}

bool AMHGZMonsterBase::ResolveHitEffectVisualSurface(
	const UMHGZMonsterHitzoneComponent* Hitzone,
	const FVector& CollisionPoint,
	const FVector& CollisionNormal,
	const FVector& PreferredOutwardDirection,
	FVector& OutVisualPoint,
	FVector& OutVisualNormal) const
{
	if (!Hitzone || Hitzone->GetOwner() != this
		|| CollisionPoint.ContainsNaN() || CollisionNormal.ContainsNaN()
		|| PreferredOutwardDirection.ContainsNaN())
	{
		return false;
	}

	FVector OutwardDirection = PreferredOutwardDirection.GetSafeNormal();
	if (OutwardDirection.IsNearlyZero())
	{
		OutwardDirection = CollisionNormal.GetSafeNormal();
	}
	if (OutwardDirection.IsNearlyZero())
	{
		OutwardDirection = (CollisionPoint - Hitzone->GetComponentLocation()).GetSafeNormal();
	}
	if (OutwardDirection.IsNearlyZero())
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const FBox Bounds = GetComponentsBoundingBox(true);
	const float TraceHalfLength = FMath::Clamp(
		(FVector::Distance(Bounds.GetCenter(), CollisionPoint)
			+ Bounds.GetExtent().Size() + 100.0f),
		100.0f, 50000.0f);
	const FVector TraceStart = CollisionPoint + OutwardDirection * TraceHalfLength;
	const FVector TraceEnd = CollisionPoint - OutwardDirection * TraceHalfLength;

	// Do not pass this actor as FCollisionQueryParams' ignore actor: that would
	// silently filter out the very mesh whose surface this query must resolve.
	FCollisionQueryParams QueryParams(
		SCENE_QUERY_STAT(MonsterMarkVisualSurface), true);
	QueryParams.AddIgnoredComponent(GetCapsuleComponent());
	TArray<UMHGZMonsterHitzoneComponent*> QueryHitzones;
	GetComponents<UMHGZMonsterHitzoneComponent>(QueryHitzones);
	for (const UMHGZMonsterHitzoneComponent* QueryHitzone : QueryHitzones)
	{
		QueryParams.AddIgnoredComponent(QueryHitzone);
	}

	TArray<FHitResult> SurfaceHits;
	World->LineTraceMultiByChannel(SurfaceHits, TraceStart, TraceEnd,
		ECC_GameTraceChannel4, QueryParams);

	float BestOutwardDistance = -TNumericLimits<float>::Max();
	float BestNormalAlignment = 0.0f;
	bool bFoundSurface = false;
	for (const FHitResult& SurfaceHit : SurfaceHits)
	{
		if (SurfaceHit.GetActor() != this
			|| !Cast<UMeshComponent>(SurfaceHit.GetComponent()))
		{
			continue;
		}

		const FVector SurfaceNormal = SurfaceHit.ImpactNormal.GetSafeNormal();
		const float NormalAlignment = FVector::DotProduct(SurfaceNormal, OutwardDirection);
		if (SurfaceNormal.IsNearlyZero() || NormalAlignment <= 0.0f)
		{
			continue;
		}

		// A hitzone can be much larger than the visible model and several visual
		// meshes can overlap. Choose the outermost visible intersection toward the
		// attack source, not the intersection nearest the proxy's collision point.
		const float OutwardDistance = FVector::DotProduct(
			SurfaceHit.ImpactPoint - CollisionPoint, OutwardDirection);
		if (OutwardDistance > BestOutwardDistance
			|| (FMath::IsNearlyEqual(OutwardDistance, BestOutwardDistance)
				&& NormalAlignment > BestNormalAlignment))
		{
			BestOutwardDistance = OutwardDistance;
			BestNormalAlignment = NormalAlignment;
			OutVisualPoint = SurfaceHit.ImpactPoint;
			OutVisualNormal = SurfaceNormal;
			bFoundSurface = true;
		}
	}

	return bFoundSurface;
}
