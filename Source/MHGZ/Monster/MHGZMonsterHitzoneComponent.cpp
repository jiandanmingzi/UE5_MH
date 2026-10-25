// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZMonsterHitzoneComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

void UMHGZMonsterHitzoneComponent::ApplyKinsectMarkDebuff(
	UObject* Source, const FGuid& MarkID, const double ExpireAt)
{
	if (!IsValid(Source) || !MarkID.IsValid() || !GetWorld()
		|| Source->GetWorld() != GetWorld() || !FMath::IsFinite(ExpireAt)
		|| ExpireAt <= GetWorld()->GetTimeSeconds()) return;
	// Dead sources/expired entries need no extra timer or component tick.
	for (auto It = KinsectMarkDebuffs.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || It.Value().ExpireAt <= GetWorld()->GetTimeSeconds()) It.RemoveCurrent();
	}
	KinsectMarkDebuffs.Add(Source, {MarkID, ExpireAt});
}

void UMHGZMonsterHitzoneComponent::RemoveKinsectMarkDebuff(
	const UObject* Source, const FGuid& MarkID)
{
	const FKinsectMarkDebuff* Debuff = KinsectMarkDebuffs.Find(Source);
	if (Debuff && Debuff->MarkID == MarkID) KinsectMarkDebuffs.Remove(Source);
}

bool UMHGZMonsterHitzoneComponent::HasKinsectMarkDebuffFrom(
	const UObject* Source, const FGuid& MarkID) const
{
	const FKinsectMarkDebuff* Debuff = KinsectMarkDebuffs.Find(Source);
	return IsValid(Source) && MarkID.IsValid() && GetWorld() && Debuff
		&& Debuff->MarkID == MarkID && GetWorld()->GetTimeSeconds() < Debuff->ExpireAt;
}

bool UMHGZMonsterHitzoneComponent::HasKinsectMarkDebuff() const
{
	for (const auto& Entry : KinsectMarkDebuffs)
	{
		if (HasKinsectMarkDebuffFrom(Entry.Key.Get(), Entry.Value.MarkID)) return true;
	}
	return false;
}

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
		if (MeshQuery) { MeshQuery->DestroyComponent(); MeshQuery = nullptr; }
		SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		return;
	}
	// Keep metadata and attachments stable while replacing the active query shape.
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (Shape == EMHGZHitzoneShape::Mesh)
	{
		if (BoxQuery) { BoxQuery->DestroyComponent(); BoxQuery = nullptr; }
		if (!QueryMesh || !GetOwner() || IsTemplate())
		{
			if (MeshQuery) { MeshQuery->DestroyComponent(); MeshQuery = nullptr; }
			return;
		}
		if (!MeshQuery)
		{
			MeshQuery = NewObject<UMHGZMonsterMeshHitzoneQueryComponent>(GetOwner());
			MeshQuery->HitzoneOwner = this;
			MeshQuery->SetupAttachment(this);
			GetOwner()->AddInstanceComponent(MeshQuery);
			MeshQuery->SetVisibility(false);
			MeshQuery->SetHiddenInGame(true);
			MeshQuery->SetCastShadow(false);
			MeshQuery->SetCanEverAffectNavigation(false);
			MeshQuery->SetStaticMesh(QueryMesh);
			MeshQuery->SetRelativeScale3D(QueryMeshScale);
			MeshQuery->SetCollisionProfileName(TEXT("MonsterHitzone"));
			MeshQuery->SetCollisionResponseToChannel(ECC_GameTraceChannel4, ECR_Ignore);
			MeshQuery->SetGenerateOverlapEvents(false);
			MeshQuery->RegisterComponent();
		}
		else
		{
			MeshQuery->SetStaticMesh(QueryMesh);
			MeshQuery->SetRelativeScale3D(QueryMeshScale);
		}
		return;
	}
	if (MeshQuery) { MeshQuery->DestroyComponent(); MeshQuery = nullptr; }
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
	if (QueryShape == EMHGZHitzoneShape::Mesh) return MeshQuery;
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
	if (const auto* Mesh = Cast<UMHGZMonsterMeshHitzoneQueryComponent>(HitComponent))
	{
		UMHGZMonsterHitzoneComponent* Owner = Mesh->HitzoneOwner.Get();
		return Owner && Owner->GetOwner() == Mesh->GetOwner()
			&& Owner->GetQueryCollisionComponent() == Mesh ? Owner : nullptr;
	}
	return nullptr;
}

float UMHGZMonsterHitzoneComponent::GetQueryBoundingRadius() const
{
	if (QueryShape == EMHGZHitzoneShape::Mesh)
		return MeshQuery ? MeshQuery->Bounds.BoxExtent.Size() : 0.f;
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
	KinsectMarkDebuffs.Reset();
	if (IsValid(BoxQuery)) { BoxQuery->DestroyComponent(); }
	BoxQuery = nullptr;
	if (IsValid(MeshQuery)) { MeshQuery->DestroyComponent(); }
	MeshQuery = nullptr;
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
	if (MeshQuery)
	{
		MeshQuery->SetCollisionProfileName(TEXT("MonsterHitzone"));
		MeshQuery->SetCollisionResponseToChannel(ECC_GameTraceChannel4, ECR_Ignore);
		MeshQuery->SetGenerateOverlapEvents(false);
	}
}
