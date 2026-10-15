// Copyright MHGZ Project. All Rights Reserved.

#include "InsectGlaive/IGPowderActor.h"

#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "AttributeSystem/Res_InsectGlaive.h"

AIGPowderActor::AIGPowderActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetActorEnableCollision(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	SceneRoot->SetMobility(EComponentMobility::Movable);
}

bool AIGPowderActor::InitializePowder(
	URes_InsectGlaive* InOwnerResource,
	APlayerController* InOwningPlayer,
	const FWeaponRuntimeToken& InRuntimeToken,
	const FGuid& InPowderID,
	const FGuid& InSourceAttackInstanceID,
	const FGuid& InSourceHitInstanceID,
	const FHitResult& InSourceHit,
	const EIGPowderSpawnPolicy InSpawnPolicy,
	const double InSpawnWorldTime,
	const float InLifetime,
	const float InKinsectAttackPowerSnapshot,
	const float InMotionValueSnapshot,
	const float InDamageContribution,
	const uint64 InSpawnSequenceID)
{
	if (bInitialized || !InOwnerResource || !InRuntimeToken.IsValid()
		|| !InPowderID.IsValid() || !InSourceAttackInstanceID.IsValid()
		|| !InSourceHitInstanceID.IsValid() || !InSourceHit.bBlockingHit
		|| !FMath::IsFinite(InSpawnWorldTime) || !FMath::IsFinite(InLifetime)
		|| InLifetime <= 0.0f || !FMath::IsFinite(InKinsectAttackPowerSnapshot)
		|| InKinsectAttackPowerSnapshot < 0.0f
		|| !FMath::IsFinite(InMotionValueSnapshot)
		|| InMotionValueSnapshot < 0.0f
		|| !FMath::IsFinite(InDamageContribution)
		|| InDamageContribution < 0.0f || InSpawnSequenceID == 0)
	{
		return false;
	}

	OwnerResource = InOwnerResource;
	OwningPlayer = InOwningPlayer;
	RuntimeToken = InRuntimeToken;
	PowderID = InPowderID;
	SourceAttackInstanceID = InSourceAttackInstanceID;
	SourceHitInstanceID = InSourceHitInstanceID;
	SourceHit = InSourceHit;
	SpawnPolicy = InSpawnPolicy;
	SpawnWorldTime = InSpawnWorldTime;
	Lifetime = InLifetime;
	ExpireWorldTime = InSpawnWorldTime + InLifetime;
	KinsectAttackPowerSnapshot = InKinsectAttackPowerSnapshot;
	MotionValueSnapshot = InMotionValueSnapshot;
	DamageContribution = InDamageContribution;
	SpawnSequenceID = InSpawnSequenceID;
	State = EIGPowderState::Available;
	TerminalReason = EIGPowderTerminalReason::None;
	LifetimeRevision = 1;
	bInitialized = true;
	SetActorEnableCollision(false);
	return true;
}

void AIGPowderActor::BeginPlay()
{
	Super::BeginPlay();
	if (!bInitialized || !GetWorld())
	{
		Destroy();
		return;
	}

	ScheduleLifetimeTimer();
}

void AIGPowderActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(LifetimeTimer);
	}
	if (URes_InsectGlaive* Resource = OwnerResource.Get())
	{
		Resource->HandlePowderActorEndPlay(this);
	}
	OwnerResource.Reset();
	Super::EndPlay(EndPlayReason);
}

bool AIGPowderActor::IsLifetimeValidAt(const double WorldTime) const
{
	return bInitialized && FMath::IsFinite(WorldTime)
		&& WorldTime < ExpireWorldTime
		&& !IsActorBeingDestroyed();
}

bool AIGPowderActor::IsAvailableFor(const URes_InsectGlaive* Resource,
	const FWeaponRuntimeToken& InRuntimeToken, const double WorldTime) const
{
	return IsValid(Resource) && OwnerResource.Get() == Resource
		&& RuntimeToken == InRuntimeToken && State == EIGPowderState::Available
		&& IsLifetimeValidAt(WorldTime);
}

bool AIGPowderActor::TryReserve(const FIGPowderReservationToken& Token)
{
	if (!Token.IsValid() || State != EIGPowderState::Available
		|| Token.RuntimeToken != RuntimeToken || IsActorBeingDestroyed())
	{
		return false;
	}
	State = EIGPowderState::Reserved;
	ReservationToken = Token;
	return true;
}

bool AIGPowderActor::IsReservedBy(const FIGPowderReservationToken& Token) const
{
	return State == EIGPowderState::Reserved && ReservationToken == Token;
}

bool AIGPowderActor::CanCommitReservation(const FIGPowderReservationToken& Token,
	const double WorldTime) const
{
	return IsReservedBy(Token) && RuntimeToken == Token.RuntimeToken
		&& IsLifetimeValidAt(WorldTime);
}

void AIGPowderActor::MarkConsumed(const FIGPowderReservationToken& Token,
	const EIGPowderTerminalReason Reason)
{
	if (IsReservedBy(Token) && Reason != EIGPowderTerminalReason::None)
	{
		MarkTerminal(Reason);
	}
}

bool AIGPowderActor::RefreshLifetime(const FIGPowderReservationToken& Token,
	const double WorldTime, const float LifetimeSeconds)
{
	if (!IsReservedBy(Token) || !FMath::IsFinite(WorldTime)
		|| !FMath::IsFinite(LifetimeSeconds) || LifetimeSeconds <= 0.0f
		|| IsActorBeingDestroyed())
	{
		return false;
	}
	Lifetime = LifetimeSeconds;
	ExpireWorldTime = WorldTime + LifetimeSeconds;
	++LifetimeRevision;
	if (LifetimeRevision <= 0)
	{
		LifetimeRevision = 1;
	}
	ScheduleLifetimeTimer();
	return true;
}

bool AIGPowderActor::ReleaseReservation(const FIGPowderReservationToken& Token,
	const FTransform& RestoreTransform, const double WorldTime)
{
	if (!IsReservedBy(Token) || !IsLifetimeValidAt(WorldTime))
	{
		return false;
	}
	SetActorTransform(RestoreTransform, false, nullptr, ETeleportType::TeleportPhysics);
	State = EIGPowderState::Available;
	ReservationToken = FIGPowderReservationToken();
	return true;
}

bool AIGPowderActor::ReleaseReservationAtCurrentLocation(
	const FIGPowderReservationToken& Token, const double WorldTime)
{
	if (!IsReservedBy(Token) || !IsLifetimeValidAt(WorldTime))
	{
		return false;
	}
	State = EIGPowderState::Available;
	ReservationToken = FIGPowderReservationToken();
	return true;
}

bool AIGPowderActor::SetGatherPresentationLocation(
	const FIGPowderReservationToken& Token, const FVector& Location)
{
	if (!IsReservedBy(Token) || Location.ContainsNaN() || IsActorBeingDestroyed())
	{
		return false;
	}
	return SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
}

FIGPowderRuntimeSnapshot AIGPowderActor::MakeSnapshot(const double WorldTime) const
{
	FIGPowderRuntimeSnapshot Snapshot;
	Snapshot.PowderID = PowderID;
	Snapshot.SourceAttackInstanceID = SourceAttackInstanceID;
	Snapshot.SourceHitInstanceID = SourceHitInstanceID;
	Snapshot.Location = GetActorLocation();
	Snapshot.RemainingLifetime = static_cast<float>(FMath::Max(
		0.0, ExpireWorldTime - WorldTime));
	Snapshot.DamageContribution = DamageContribution;
	Snapshot.KinsectAttackPowerSnapshot = KinsectAttackPowerSnapshot;
	Snapshot.MotionValueSnapshot = MotionValueSnapshot;
	Snapshot.State = State;
	Snapshot.TerminalReason = TerminalReason;
	Snapshot.LifetimeRevision = LifetimeRevision;
	Snapshot.SpawnPolicy = SpawnPolicy;
	return Snapshot;
}

void AIGPowderActor::ScheduleLifetimeTimer()
{
	UWorld* World = GetWorld();
	if (!World || IsActorBeingDestroyed() || State == EIGPowderState::Consumed)
	{
		return;
	}

	World->GetTimerManager().ClearTimer(LifetimeTimer);
	const double Remaining = ExpireWorldTime - World->GetTimeSeconds();
	const int32 ExpectedRevision = LifetimeRevision;
	FTimerDelegate ExpiryDelegate = FTimerDelegate::CreateWeakLambda(this,
		[this, ExpectedRevision]()
		{
			HandleLifetimeExpired(ExpectedRevision);
		});
	World->GetTimerManager().SetTimer(LifetimeTimer, ExpiryDelegate,
		static_cast<float>(FMath::Max(0.0, Remaining)), false);
}

void AIGPowderActor::HandleLifetimeExpired(const int32 ExpectedRevision)
{
	if (IsActorBeingDestroyed() || State == EIGPowderState::Consumed
		|| ExpectedRevision != LifetimeRevision)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		Destroy();
		return;
	}
	if (World->GetTimeSeconds() + UE_SMALL_NUMBER < ExpireWorldTime)
	{
		ScheduleLifetimeTimer();
		return;
	}

	if (URes_InsectGlaive* Resource = OwnerResource.Get())
	{
		Resource->HandlePowderLifetimeExpired(this, ExpectedRevision);
	}
	else
	{
		Destroy();
	}
}

void AIGPowderActor::MarkTerminal(const EIGPowderTerminalReason Reason)
{
	if (State == EIGPowderState::Consumed || Reason == EIGPowderTerminalReason::None)
	{
		return;
	}
	State = EIGPowderState::Consumed;
	TerminalReason = Reason;
	ReservationToken = FIGPowderReservationToken();
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(LifetimeTimer);
	}
}
