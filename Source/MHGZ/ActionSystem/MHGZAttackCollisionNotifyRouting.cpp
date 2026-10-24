// Copyright MHGZ Project. All Rights Reserved.
#include "MHGZAttackAbility.h"
#include "MHGZAnimNotifyActionResolver.h"

namespace
{
const void* NotifyContext(const FAnimNotifyEventReference& Ref)
{
	return Ref.GetContextData<UE::Anim::FAnimNotifyMontageInstanceContext>();
}
}

uint64 UMHGZAttackAbility::GetCollisionWindowOccurrence(const int32 Segment) const
{
	const auto* State = ActiveCollisionWindows.Find(Segment);
	return State ? State->Occurrence : 0;
}

void UMHGZAttackAbility::BeginCollisionFromNotify(const int32 Segment,
	USkeletalMeshComponent* Mesh, const int32 MontageID, const FAnimNotifyEventReference& Ref)
{
	if (!Mesh || MontageID == INDEX_NONE || !NotifyContext(Ref) || !Ref.GetNotify()) return;
	// Bounded even if malformed content repeatedly begins without ever ending.
	if (CollisionNotifyRoutes.Num() >= 256) return;
	FCollisionNotifyRoute Route;
	Route.Segment = Segment;
	Route.Mesh = Mesh;
	Route.MontageID = MontageID;
	Route.EventID = MHGZ::AnimNotify::MakeNotifyEventID(Ref);
	Route.BeginReference = Route.LatestReference = Ref;
	EnableCollisionInternal(Segment, &Route);
}

int32 UMHGZAttackAbility::FindCollisionNotifyRoute(const int32 Segment,
	USkeletalMeshComponent* Mesh, const int32 MontageID,
	const FAnimNotifyEventReference& Ref, const bool bAllowNewTickContext) const
{
	const void* Context = NotifyContext(Ref);
	if (!Context || !Ref.GetNotify()) return INDEX_NONE;
	const FName EventID = MHGZ::AnimNotify::MakeNotifyEventID(Ref);
	int32 Latest = INDEX_NONE;
	for (int32 I = 0; I < CollisionNotifyRoutes.Num(); ++I)
	{
		const auto& Route = CollisionNotifyRoutes[I];
		if (Route.Segment != Segment || Route.Mesh.Get() != Mesh || Route.MontageID != MontageID
			|| Route.EventID != EventID || Route.BeginReference.GetSourceObject() != Ref.GetSourceObject()) continue;
		if (NotifyContext(Route.BeginReference) == Context || NotifyContext(Route.LatestReference) == Context) return I;
		Latest = I;
	}
	// Tick gets a fresh engine context every animation update. End MUST match a
	// retained Begin/latest Tick context; it never falls back to a newer Begin.
	return bAllowNewTickContext ? Latest : INDEX_NONE;
}

void UMHGZAttackAbility::TickCollisionFromNotify(const int32 Segment,
	USkeletalMeshComponent* Mesh, const int32 MontageID,
	const FAnimNotifyEventReference& Ref, const float DeltaSeconds)
{
	const int32 Index = FindCollisionNotifyRoute(Segment, Mesh, MontageID, Ref, true);
	if (Index == INDEX_NONE) return;
	CollisionNotifyRoutes[Index].LatestReference = Ref;
	const uint64 Occurrence = CollisionNotifyRoutes[Index].Occurrence;
	if (GetCollisionWindowOccurrence(Segment) == Occurrence) TickCollision(Segment, DeltaSeconds);
}

void UMHGZAttackAbility::EndCollisionFromNotify(const int32 Segment,
	USkeletalMeshComponent* Mesh, const int32 MontageID, const FAnimNotifyEventReference& Ref)
{
	const int32 Index = FindCollisionNotifyRoute(Segment, Mesh, MontageID, Ref, false);
	if (Index == INDEX_NONE) return;
	const uint64 Occurrence = CollisionNotifyRoutes[Index].Occurrence;
	const FWeaponActionToken Action = GetActionToken();
	if (GetCollisionWindowOccurrence(Segment) == Occurrence)
	{
		TickCollision(Segment, 0.f); // final authored interval, before closure
		if (GetActionToken() == Action && GetCollisionWindowOccurrence(Segment) == Occurrence)
			DisableCollision(Segment);
	}
	if (GetActionToken() == Action)
		CollisionNotifyRoutes.RemoveAll([Occurrence](const auto& R) { return R.Occurrence == Occurrence; });
}
