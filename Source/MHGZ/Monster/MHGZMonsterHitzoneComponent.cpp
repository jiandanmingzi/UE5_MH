// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZMonsterHitzoneComponent.h"
#include "GameFramework/Actor.h"

UMHGZMonsterHitzoneComponent::UMHGZMonsterHitzoneComponent()
{
	// 构造器中不做碰撞设置——BlueprintSpawnableComponent 的 CDO 构造期间
	// SetCollisionEnabled 等 API 会触发 NewObject 空名崩溃
}

void UMHGZMonsterHitzoneComponent::OnRegister()
{
	Super::OnRegister();

	// MonsterHitzone 预设使用独立 Hitzone Object Channel；实体阻挡由 Body 承担。
	ConfigureQueryShape(QueryShape, BoxHalfExtent);
}

void UMHGZMonsterHitzoneComponent::ConfigureQueryShape(
	const EMHGZHitzoneShape Shape, const FVector& HalfExtent)
{
	QueryShape = Shape;
	BoxHalfExtent = HalfExtent.ComponentMax(FVector(1.0f));
	SetCollisionProfileName(TEXT("MonsterHitzone"));
	SetCollisionResponseToChannel(ECC_GameTraceChannel4, ECR_Ignore);
	SetGenerateOverlapEvents(false);
	if (Shape == EMHGZHitzoneShape::Sphere)
	{
		if (BoxQuery) { BoxQuery->DestroyComponent(); BoxQuery = nullptr; }
		SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		return;
	}
	// Keep metadata and attachments stable while replacing the active query shape.
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (!BoxQuery && GetOwner() && !IsTemplate())
	{
		BoxQuery = NewObject<UMHGZMonsterBoxHitzoneQueryComponent>(GetOwner());
		BoxQuery->HitzoneOwner = this;
		BoxQuery->SetupAttachment(this);
		GetOwner()->AddInstanceComponent(BoxQuery);
		BoxQuery->SetBoxExtent(BoxHalfExtent);
		BoxQuery->SetCollisionProfileName(TEXT("MonsterHitzone"));
		BoxQuery->SetCollisionResponseToChannel(ECC_GameTraceChannel4, ECR_Ignore);
		BoxQuery->SetGenerateOverlapEvents(false);
		BoxQuery->RegisterComponent();
	}
	else if (BoxQuery)
	{
		BoxQuery->SetBoxExtent(BoxHalfExtent);
	}
}

UPrimitiveComponent* UMHGZMonsterHitzoneComponent::GetQueryCollisionComponent() const
{
	return QueryShape == EMHGZHitzoneShape::Box ? static_cast<UPrimitiveComponent*>(BoxQuery.Get())
		: const_cast<UMHGZMonsterHitzoneComponent*>(this);
}

UMHGZMonsterHitzoneComponent* UMHGZMonsterHitzoneComponent::Resolve(
	UPrimitiveComponent* HitComponent)
{
	if (UMHGZMonsterHitzoneComponent* Sphere = Cast<UMHGZMonsterHitzoneComponent>(HitComponent))
	{
		return Sphere;
	}
	if (const UMHGZMonsterBoxHitzoneQueryComponent* Box =
		Cast<UMHGZMonsterBoxHitzoneQueryComponent>(HitComponent))
	{
		UMHGZMonsterHitzoneComponent* Owner = Box->HitzoneOwner.Get();
		return Owner && Owner->GetOwner() == Box->GetOwner()
			&& Owner->GetQueryCollisionComponent() == Box ? Owner : nullptr;
	}
	return nullptr;
}

float UMHGZMonsterHitzoneComponent::GetQueryBoundingRadius() const
{
	return QueryShape == EMHGZHitzoneShape::Box
		? (BoxHalfExtent * GetComponentScale()).Size() : GetScaledSphereRadius();
}

float UMHGZMonsterHitzoneComponent::GetClosestPointOnQueryCollision(
	const FVector& Point, FVector& OutPoint) const
{
	UPrimitiveComponent* Query = GetQueryCollisionComponent();
	return Query ? Query->GetClosestPointOnCollision(Point, OutPoint) : -1.0f;
}

void UMHGZMonsterHitzoneComponent::OnComponentDestroyed(const bool bDestroyingHierarchy)
{
	if (IsValid(BoxQuery)) { BoxQuery->DestroyComponent(); }
	BoxQuery = nullptr;
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void UMHGZMonsterHitzoneComponent::ForceRestoreAllChannels()
{
	ConfigureQueryShape(QueryShape, BoxHalfExtent);
	if (BoxQuery)
	{
		BoxQuery->SetCollisionProfileName(TEXT("MonsterHitzone"));
		BoxQuery->SetCollisionResponseToChannel(ECC_GameTraceChannel4, ECR_Ignore);
		BoxQuery->SetGenerateOverlapEvents(false);
	}
}
