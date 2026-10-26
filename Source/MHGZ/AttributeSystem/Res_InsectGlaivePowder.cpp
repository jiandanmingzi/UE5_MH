// Copyright MHGZ Project. All Rights Reserved.

// 粉尘生成、寿命、自然爆炸及聚集预约。
#include "Res_InsectGlaive.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameplayEffect.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"
#include "Res_InsectGlaivePowderPrivate.h"
#include "ActionSystem/MHGZDamageMath.h"
#include "AbilitySystemGlobals.h"
#include "ActionSystem/MHGZDamageGameplayEffect.h"
#include "ActionSystem/MHGZGameplayEffectContext.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/OverlapResult.h"
#include "TimerManager.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "InsectGlaive/IGPowderActor.h"
#include "InsectGlaive/Kinsect/InsectGlaiveKinsectData.h"
#include "InsectGlaive/Kinsect/Kinsect.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"
#include "Monster/MHGZMonsterBase.h"
#include "Kismet/GameplayStatics.h"

using UE::MHGZ::IGResource::PowderWorldLifetimeSeconds;

namespace
{
constexpr double PowderMaintenanceIntervalSeconds = 0.25;

FName PowderSpawnPolicyName(const EIGPowderSpawnPolicy Policy)
{
	switch (Policy)
	{
	case EIGPowderSpawnPolicy::MarkedContact: return TEXT("MarkedContact");
	case EIGPowderSpawnPolicy::AwakenedPiercing: return TEXT("AwakenedPiercing");
	case EIGPowderSpawnPolicy::AwakenedEachHit: return TEXT("AwakenedEachHit");
	case EIGPowderSpawnPolicy::AutoMarkContact: return TEXT("AutoMarkContact");
	default: return TEXT("None");
	}
}
}

bool URes_InsectGlaive::SpawnPowderForSuccessfulHit(
	const FWeaponRuntimeToken& Runtime,
	const FGuid& SourceAttackID,
	const FGuid& HitID,
	const FHitResult& Hit, const float KinsectAttackPower, const float MotionValue)
{
	if (!FMath::IsFinite(KinsectAttackPower) || KinsectAttackPower < 0.0f
        || !FMath::IsFinite(MotionValue) || MotionValue < 0.0f) return false;
    return SpawnPowderInternal(Runtime, SourceAttackID, HitID, Hit,
        MHGZDamageMath::PowderDamage(KinsectAttackPower, MotionValue), EIGPowderSpawnPolicy::None,
        KinsectAttackPower, MotionValue);
}

bool URes_InsectGlaive::HandleSuccessfulKinsectDamage(
	const FWeaponRuntimeToken& Runtime, const FGuid& SourceAttackID,
	const FGuid& HitID, const FHitResult& Hit,
	const EIGPowderSpawnPolicy SpawnPolicy)
{
	const auto RecordRejectedSpawn = [this, &SourceAttackID, &HitID, &Hit, SpawnPolicy](
		const FName Reason)
	{
		FWeaponCombatTelemetryEvent Event;
		Event.EventName = TEXT("PowderRejected");
		Event.SourceAttackID = SourceAttackID;
		Event.HitID = HitID;
		Event.TargetActor = IsValid(Hit.GetActor()) ? Hit.GetActor()->GetFName() : NAME_None;
		Event.Hitzone = Hit.GetComponent() ? Hit.GetComponent()->GetFName() : NAME_None;
		Event.ImpactPoint = Hit.ImpactPoint;
		Event.SpawnPolicy = PowderSpawnPolicyName(SpawnPolicy);
		Event.Reason = Reason;
		RecordCombatTelemetryEvent(MoveTemp(Event));
	};
	if (SpawnPolicy == EIGPowderSpawnPolicy::None || bRuntimeShuttingDown
		|| !CombatConfig || !GetWorld() || !IsRuntimeRequestCurrent(Runtime)
		|| Runtime != RuntimeContext.RuntimeToken || !SourceAttackID.IsValid()
		|| !HitID.IsValid() || SpawnedPowderHitIDs.Contains(HitID)
		|| !Hit.bBlockingHit || !IsValid(Hit.GetActor()))
	{
		RecordRejectedSpawn(SpawnedPowderHitIDs.Contains(HitID)
			? FName(TEXT("DuplicateHit")) : FName(TEXT("InvalidRequest")));
		return false;
	}

	const double Now = GetWorld()->GetTimeSeconds();
	if (SpawnPolicy == EIGPowderSpawnPolicy::MarkedContact
		|| SpawnPolicy == EIGPowderSpawnPolicy::AutoMarkContact)
	{
		if (!HasValidKinsectMark())
		{
			RecordRejectedSpawn(TEXT("NoActiveMark"));
			return false;
		}
		if (Now >= ActiveMarkExpireTime)
		{
			RecordRejectedSpawn(TEXT("MarkExpired"));
			return false;
		}
		if (Hit.GetActor() != ActiveMarkTargetActor.Get())
		{
			RecordRejectedSpawn(TEXT("WrongMarkTarget"));
			return false;
		}
		if (!UMHGZMonsterHitzoneComponent::Resolve(Hit.GetComponent()))
		{
			RecordRejectedSpawn(TEXT("NotMonsterHitzone"));
			return false;
		}
		if (!IsHitOnKinsectMarkedPart(Hit))
		{
			RecordRejectedSpawn(TEXT("UnmarkedPart"));
			return false;
		}
		if (SpawnPolicy == EIGPowderSpawnPolicy::MarkedContact
			&& LastMarkedContactPowderWorldTime >= 0.0
			&& Now - LastMarkedContactPowderWorldTime < CombatConfig->MarkedHitPowderCooldown)
		{
			RecordRejectedSpawn(TEXT("SharedCooldown"));
			return false;
		}
	}
	else if (SpawnPolicy != EIGPowderSpawnPolicy::AwakenedPiercing
		&& SpawnPolicy != EIGPowderSpawnPolicy::AwakenedEachHit)
	{
		RecordRejectedSpawn(TEXT("UnsupportedPolicy"));
		return false;
	}

	if (!FMath::IsFinite(CombatConfig->PowderMotionValue)
		|| CombatConfig->PowderMotionValue <= 0.0f)
	{
		RecordRejectedSpawn(TEXT("DamageConfigDisabled"));
		return false;
	}
	const float AttackPowerSnapshot = GetModifiedKinsectAttackPower();
	const float MotionValueSnapshot = CombatConfig->PowderMotionValue;
	const float DamageSnapshot = MHGZDamageMath::PowderDamage(AttackPowerSnapshot, MotionValueSnapshot);
	if (!FMath::IsFinite(AttackPowerSnapshot) || AttackPowerSnapshot <= 0.0f
		|| !FMath::IsFinite(DamageSnapshot) || DamageSnapshot <= 0.0f)
	{
		RecordRejectedSpawn(TEXT("InvalidDamageSnapshot"));
		return false;
	}

	if (!SpawnPowderInternal(Runtime, SourceAttackID, HitID, Hit, DamageSnapshot,
		SpawnPolicy, AttackPowerSnapshot, MotionValueSnapshot))
	{
		RecordRejectedSpawn(TEXT("SpawnFailedOrAtCapacity"));
		return false;
	}
	if (SpawnPolicy == EIGPowderSpawnPolicy::MarkedContact)
	{
		LastMarkedContactPowderWorldTime = Now;
	}
	return true;
}

bool URes_InsectGlaive::SpawnPowderInternal(
	const FWeaponRuntimeToken& Runtime, const FGuid& SourceAttackID,
	const FGuid& HitID, const FHitResult& Hit, const float DamageContribution,
	const EIGPowderSpawnPolicy SpawnPolicy,
	const float KinsectAttackPowerSnapshot,
	const float MotionValueSnapshot)
{
	UWorld* World = GetWorld();
	if (bRuntimeShuttingDown || !CombatConfig || !World
		|| !IsRuntimeRequestCurrent(Runtime) || Runtime != RuntimeContext.RuntimeToken
		|| !SourceAttackID.IsValid() || !HitID.IsValid()
		|| SpawnedPowderHitIDs.Contains(HitID)
		|| !Hit.bBlockingHit || !IsValid(Hit.GetActor())
		|| !FMath::IsFinite(Hit.ImpactPoint.X) || !FMath::IsFinite(Hit.ImpactPoint.Y)
		|| !FMath::IsFinite(Hit.ImpactPoint.Z)
		|| !FMath::IsFinite(DamageContribution) || DamageContribution < 0.0f
		|| !FMath::IsFinite(KinsectAttackPowerSnapshot)
		|| KinsectAttackPowerSnapshot < 0.0f
		|| !FMath::IsFinite(MotionValueSnapshot)
		|| MotionValueSnapshot < 0.0f)
	{
		return false;
	}
	int32 LivePowderCount = 0;
	const double Now = World->GetTimeSeconds();
	const TArray<TWeakObjectPtr<AIGPowderActor>> ExistingPowders = OwnedPowders;
	for (const TWeakObjectPtr<AIGPowderActor>& WeakPowder : ExistingPowders)
	{
		if (AIGPowderActor* Powder = WeakPowder.Get();
			Powder && Powder->GetExpireWorldTime() <= Now)
		{
			HandlePowderLifetimeExpired(Powder, Powder->GetLifetimeRevision());
		}
	}
	for (const TWeakObjectPtr<AIGPowderActor>& WeakPowder : OwnedPowders)
	{
		const AIGPowderActor* Powder = WeakPowder.Get();
		if (Powder && Powder->GetRuntimeToken() == Runtime
			&& Powder->GetPowderState() != EIGPowderState::Consumed
			&& Powder->GetExpireWorldTime() > Now)
		{
			++LivePowderCount;
		}
	}
	if (LivePowderCount >= CombatConfig->PowderMaxCount)
	{
		FWeaponCombatTelemetryEvent Event;
		Event.EventName = TEXT("PowderRejected");
		Event.SourceAttackID = SourceAttackID;
		Event.HitID = HitID;
		Event.TargetActor = IsValid(Hit.GetActor()) ? Hit.GetActor()->GetFName() : NAME_None;
		Event.Hitzone = Hit.GetComponent() ? Hit.GetComponent()->GetFName() : NAME_None;
		Event.ImpactPoint = Hit.ImpactPoint;
		Event.SpawnPolicy = PowderSpawnPolicyName(SpawnPolicy);
		Event.Reason = FName(*FString::Printf(TEXT("RuntimeCap%d"), CombatConfig->PowderMaxCount));
		Event.MemberCount = LivePowderCount;
		RecordCombatTelemetryEvent(MoveTemp(Event));
		UE_LOG(LogTemp, Warning,
			TEXT("[IG Powder] CapRejected: Runtime owns %d live powders (configured cap %d)."),
			LivePowderCount, CombatConfig->PowderMaxCount);
		return false;
	}

	FVector VisualSurfacePoint = Hit.ImpactPoint;
	FVector VisualSurfaceNormal = Hit.ImpactNormal.GetSafeNormal();
	const UMHGZMonsterHitzoneComponent* HitHitzone =
		UMHGZMonsterHitzoneComponent::Resolve(Hit.GetComponent());
	if (const AMHGZMonsterBase* Monster = Cast<AMHGZMonsterBase>(Hit.GetActor());
		Monster && HitHitzone)
	{
		FVector ResolvedPoint = VisualSurfacePoint;
		FVector ResolvedNormal = VisualSurfaceNormal;
		if (Monster->ResolveHitEffectVisualSurface(HitHitzone,
			Hit.ImpactPoint, Hit.ImpactNormal,
			Hit.TraceStart - Hit.ImpactPoint, ResolvedPoint, ResolvedNormal)
			&& !ResolvedPoint.ContainsNaN() && !ResolvedNormal.IsNearlyZero())
		{
			VisualSurfacePoint = ResolvedPoint;
			VisualSurfaceNormal = ResolvedNormal.GetSafeNormal();
		}
	}
	if (VisualSurfaceNormal.IsNearlyZero() && HitHitzone)
	{
		VisualSurfaceNormal = (Hit.ImpactPoint - HitHitzone->GetComponentLocation()).GetSafeNormal();
	}
	if (VisualSurfaceNormal.IsNearlyZero())
	{
		VisualSurfaceNormal = FVector::UpVector;
	}
	// The rendered powder cloud is about 15 cm across. Put its center one radius
	// plus a small margin outside the resolved visible surface so it cannot spawn
	// inside large proxy hitzones or the monster's opaque collision mesh.
	constexpr float PowderVisualSurfaceClearanceCm = 9.0f;
	const FVector PresentationLocation = VisualSurfacePoint
		+ VisualSurfaceNormal * PowderVisualSurfaceClearanceCm;
	const FTransform SpawnTransform(FRotator::ZeroRotator, PresentationLocation);
	const double SpawnTime = Now;
	uint64 SpawnID = NextPowderSpawnSequenceID++;
	if (SpawnID == 0)
	{
		SpawnID = NextPowderSpawnSequenceID++;
	}
	if (SpawnID == 0)
	{
		return false;
	}

	TSubclassOf<AIGPowderActor> ActorClass = CombatConfig->PowderActorClass;
	if (!ActorClass)
	{
		ActorClass = AIGPowderActor::StaticClass();
	}
	const float Lifetime = FMath::IsFinite(CombatConfig->PowderLifetime)
		&& CombatConfig->PowderLifetime > 0.0f
		? CombatConfig->PowderLifetime : PowderWorldLifetimeSeconds;
	AIGPowderActor* Powder = World->SpawnActorDeferred<AIGPowderActor>(
		ActorClass, SpawnTransform, GetOwner(),
		Cast<APawn>(GetOwner()), ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Powder)
	{
		UE_LOG(LogTemp, Error, TEXT("[IG Powder] SpawnActorDeferred failed for %s."),
			*GetNameSafe(*ActorClass));
		return false;
	}

	const bool bInitialized = Powder->InitializePowder(this,
		RuntimeContext.Controller.Get(), Runtime, FGuid::NewGuid(),
		SourceAttackID, HitID, Hit, SpawnPolicy, SpawnTime, Lifetime,
		KinsectAttackPowerSnapshot, MotionValueSnapshot,
		DamageContribution, SpawnID);
	if (!bInitialized)
	{
		Powder->Destroy();
		return false;
	}
	Powder->FinishSpawning(SpawnTransform);
	if (!IsValid(Powder) || Powder->GetOwnerResource() != this
		|| Powder->GetRuntimeToken() != Runtime)
	{
		if (IsValid(Powder))
		{
			Powder->Destroy();
		}
		return false;
	}

	OwnedPowders.AddUnique(Powder);
	SpawnedPowderHitIDs.Add(HitID);
	FWeaponCombatTelemetryEvent Event;
	Event.EventName = TEXT("PowderSpawn");
	Event.SourceAttackID = SourceAttackID;
	Event.HitID = HitID;
	Event.PowderID = Powder->GetPowderID();
	Event.TargetActor = IsValid(Hit.GetActor()) ? Hit.GetActor()->GetFName() : NAME_None;
	Event.Hitzone = Hit.GetComponent() ? Hit.GetComponent()->GetFName() : NAME_None;
	Event.ImpactPoint = Hit.ImpactPoint;
	Event.SpawnPolicy = PowderSpawnPolicyName(SpawnPolicy);
	Event.ExpireAt = Powder->GetExpireWorldTime();
	Event.LifetimeRevision = Powder->GetLifetimeRevision();
	Event.Damage = DamageContribution;
	RecordCombatTelemetryEvent(MoveTemp(Event));
	return true;
}

void URes_InsectGlaive::RemovePowderFromReservations(AIGPowderActor* Powder)
{
	if (!Powder)
	{
		return;
	}
	for (TPair<uint64, FReservedPowderSet>& Pair : PowderReservations)
	{
		Pair.Value.Members.RemoveAll([Powder](const FReservedPowderMember& Member)
		{
			return !Member.Actor.IsValid() || Member.Actor.Get() == Powder;
		});
	}
}

void URes_InsectGlaive::HandlePowderLifetimeExpired(
	AIGPowderActor* Powder, const int32 ExpectedLifetimeRevision)
{
	UWorld* World = GetWorld();
	if (!Powder || bRuntimeShuttingDown || !World
		|| Powder->GetOwnerResource() != this
		|| Powder->GetRuntimeToken() != RuntimeContext.RuntimeToken
		|| !IsRuntimeRequestCurrent(Powder->GetRuntimeToken())
		|| Powder->GetLifetimeRevision() != ExpectedLifetimeRevision
		|| Powder->GetPowderState() == EIGPowderState::Consumed)
	{
		return;
	}
	if (World->GetTimeSeconds() + UE_SMALL_NUMBER < Powder->GetExpireWorldTime())
	{
		Powder->ScheduleLifetimeTimer();
		return;
	}

	// Claim the terminal outcome and unregister before damage/callbacks. Destroy/EndPlay
	// can therefore never race a second expiry, distance removal, or gather commit.
	const FGuid ExplosionID = FGuid::NewGuid();
	OwnedPowders.RemoveAll([Powder](const TWeakObjectPtr<AIGPowderActor>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == Powder;
	});
	RemovePowderFromReservations(Powder);
	Powder->MarkTerminal(EIGPowderTerminalReason::NaturalExplosion);
	FWeaponCombatTelemetryEvent Event;
	Event.EventName = TEXT("PowderNaturalDetonate");
	Event.ExplosionID = ExplosionID;
	Event.PowderID = Powder->GetPowderID();
	Event.ImpactPoint = Powder->GetActorLocation();
	Event.Damage = Powder->GetDamageContribution();
	Event.ExpireAt = Powder->GetExpireWorldTime();
	Event.LifetimeRevision = Powder->GetLifetimeRevision();
	RecordCombatTelemetryEvent(MoveTemp(Event));
	ApplyNaturalPowderExplosion(Powder, ExplosionID);
	Powder->Destroy();
}

void URes_InsectGlaive::TickPowderMaintenance()
{
	if (bRuntimeShuttingDown || !GetWorld())
	{
		return;
	}
	TickPowderVortexPresentation();
	if (OwnedPowders.IsEmpty())
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now < NextPowderMaintenanceWorldTime)
	{
		return;
	}
	NextPowderMaintenanceWorldTime = Now + PowderMaintenanceIntervalSeconds;

	const TArray<TWeakObjectPtr<AIGPowderActor>> PowderSnapshot = OwnedPowders;
	AActor* PlayerActor = RuntimeContext.Character.Get();
	const float DespawnDistance = CombatConfig
		? CombatConfig->PowderDespawnDistance : 0.0f;
	const bool bDistanceRemovalEnabled = FMath::IsFinite(DespawnDistance)
		&& DespawnDistance > 0.0f && IsValid(PlayerActor);
	const double DespawnDistanceSquared = bDistanceRemovalEnabled
		? FMath::Square(static_cast<double>(DespawnDistance)) : 0.0;

	for (const TWeakObjectPtr<AIGPowderActor>& WeakPowder : PowderSnapshot)
	{
		AIGPowderActor* Powder = WeakPowder.Get();
		if (!Powder)
		{
			OwnedPowders.Remove(WeakPowder);
			continue;
		}
		if (Powder->GetRuntimeToken() != RuntimeContext.RuntimeToken
			|| Powder->GetOwnerResource() != this)
		{
			RemovePowderFromReservations(Powder);
			OwnedPowders.Remove(Powder);
			Powder->MarkTerminal(EIGPowderTerminalReason::RuntimeShutdown);
			Powder->Destroy();
			continue;
		}
		if (Powder->GetExpireWorldTime() <= Now)
		{
			HandlePowderLifetimeExpired(Powder, Powder->GetLifetimeRevision());
			continue;
		}
		if (bDistanceRemovalEnabled
			&& FVector::DistSquared(PlayerActor->GetActorLocation(),
				Powder->GetActorLocation()) > DespawnDistanceSquared)
		{
			FWeaponCombatTelemetryEvent Event;
			Event.EventName = TEXT("PowderDistanceCulled");
			Event.PowderID = Powder->GetPowderID();
			Event.ImpactPoint = Powder->GetActorLocation();
			Event.ExpireAt = Powder->GetExpireWorldTime();
			Event.LifetimeRevision = Powder->GetLifetimeRevision();
			Event.Reason = TEXT("BeyondDespawnDistance");
			RecordCombatTelemetryEvent(MoveTemp(Event));
			RemovePowderFromReservations(Powder);
			OwnedPowders.Remove(Powder);
			Powder->MarkTerminal(EIGPowderTerminalReason::DistanceDespawn);
			Powder->Destroy();
		}
	}
}

void URes_InsectGlaive::ApplyNaturalPowderExplosion(
	AIGPowderActor* Powder, const FGuid& ExplosionID)
{
	UWorld* World = GetWorld();
	UAbilitySystemComponent* SourceASC = GetPlayerASC();
	if (!Powder || !World || !SourceASC || !CombatConfig || !ExplosionID.IsValid()
		|| !FMath::IsFinite(CombatConfig->PowderNaturalExplosionRadius)
		|| CombatConfig->PowderNaturalExplosionRadius <= 0.0f)
	{
		return;
	}

	struct FPowderTargetHit
	{
		AActor* Actor = nullptr;
		UMHGZMonsterHitzoneComponent* Hitzone = nullptr;
		FHitResult Hit;
		double DistanceSquared = TNumericLimits<double>::Max();
	};

	TArray<FOverlapResult> Overlaps;
	FCollisionObjectQueryParams ObjectQuery;
	ObjectQuery.AddObjectTypesToQuery(ECC_GameTraceChannel3);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(IGPowderNaturalExplosion), false,
		GetOwner());
	const FVector Center = Powder->GetActorLocation();
	// Presentation belongs to the committed explosion, including an empty blast.
	// Never attach it to Powder: that actor is destroyed immediately afterwards.
	if (CombatConfig->PowderExplosionSound)
	{
		UGameplayStatics::SpawnSoundAtLocation(this,
			CombatConfig->PowderExplosionSound, Center);
	}
	const bool bFoundOverlap = World->OverlapMultiByObjectType(Overlaps, Center,
		FQuat::Identity, ObjectQuery,
		FCollisionShape::MakeSphere(CombatConfig->PowderNaturalExplosionRadius), QueryParams);
	if (!bFoundOverlap)
	{
		return;
	}

	TMap<AActor*, FPowderTargetHit> BestHitByActor;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		UMHGZMonsterHitzoneComponent* Hitzone =
			UMHGZMonsterHitzoneComponent::Resolve(Overlap.GetComponent());
		AActor* TargetActor = Hitzone ? Hitzone->GetOwner() : nullptr;
		if (!IsValid(Hitzone) || !IsValid(TargetActor) || TargetActor == GetOwner()
			|| !UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(TargetActor))
		{
			continue;
		}

		FVector ClosestPoint = Hitzone->GetComponentLocation();
		const float ClosestDistance = Hitzone->GetClosestPointOnQueryCollision(Center,
			ClosestPoint);
		if (ClosestDistance < 0.0f || ClosestPoint.ContainsNaN())
		{
			ClosestPoint = Hitzone->GetComponentLocation();
		}
		const double DistanceSquared = FVector::DistSquared(Center, ClosestPoint);
		FPowderTargetHit* Existing = BestHitByActor.Find(TargetActor);
		if (Existing && (Existing->DistanceSquared < DistanceSquared
			|| (FMath::IsNearlyEqual(Existing->DistanceSquared, DistanceSquared)
				&& Existing->Hitzone->GetName().Compare(Hitzone->GetName(),
					ESearchCase::CaseSensitive) <= 0)))
		{
			continue;
		}

		FPowderTargetHit Candidate;
		Candidate.Actor = TargetActor;
		Candidate.Hitzone = Hitzone;
		Candidate.DistanceSquared = DistanceSquared;
		Candidate.Hit = FHitResult(TargetActor, Hitzone->GetQueryCollisionComponent(), ClosestPoint,
			(Center - ClosestPoint).GetSafeNormal());
		Candidate.Hit.bBlockingHit = true;
		Candidate.Hit.TraceStart = Center;
		Candidate.Hit.TraceEnd = ClosestPoint;
		Candidate.Hit.Location = ClosestPoint;
		Candidate.Hit.ImpactPoint = ClosestPoint;
		Candidate.Hit.Normal = (Center - ClosestPoint).GetSafeNormal();
		Candidate.Hit.ImpactNormal = Candidate.Hit.Normal;
		BestHitByActor.Add(TargetActor, MoveTemp(Candidate));
	}

	TArray<FPowderTargetHit> Targets;
	BestHitByActor.GenerateValueArray(Targets);
	Targets.Sort([](const FPowderTargetHit& A, const FPowderTargetHit& B)
	{
		return GetNameSafe(A.Actor) < GetNameSafe(B.Actor);
	});

	static const FGameplayTag AttackPowerTag = FGameplayTag::RequestGameplayTag(
		TEXT("Damage.AttackPower"));
	static const FGameplayTag MotionValueTag = FGameplayTag::RequestGameplayTag(
		TEXT("Damage.MotionValue"));
	static const FGameplayTag BaseStaggerTag = FGameplayTag::RequestGameplayTag(
		TEXT("Damage.BaseStagger"));
	static const FGameplayTag CritOverrideTag = FGameplayTag::RequestGameplayTag(
		TEXT("Damage.CritOverride"));
	static const FGameplayTag DanceMultiplierTag = FGameplayTag::RequestGameplayTag(
		TEXT("Damage.DanceMultiplier"));
	const float AttackPowerSnapshot = Powder->GetKinsectAttackPowerSnapshot();
	const float MotionValueSnapshot = Powder->GetMotionValueSnapshot();
	for (const FPowderTargetHit& Target : Targets)
	{
		UAbilitySystemComponent* TargetASC =
			UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target.Actor);
		if (!TargetASC)
		{
			continue;
		}

		FGameplayEffectContextHandle ContextHandle = SourceASC->MakeEffectContext();
		FMHGZGameplayEffectContext* Context =
			FMHGZGameplayEffectContext::ExtractEffectContext(ContextHandle);
		if (!Context)
		{
			continue;
		}
		Context->AddHitResult(Target.Hit, true);
		Context->AttackInstanceID = ExplosionID;
		Context->DamageSourceType = EMHGZDamageSourceType::Powder;
		Context->bUseHitzoneDefense = false;
		Context->HitStopDuration = 0.0f;
		Context->HitzoneTag = Target.Hitzone->HitzoneTag;
		if (AActor* Source = RuntimeContext.Character.Get())
		{
			Context->AddInstigator(Source, Source);
		}

		FGameplayEffectSpecHandle Spec = SourceASC->MakeOutgoingSpec(
			UMHGZDamageGameplayEffect::StaticClass(), 1.0f, ContextHandle);
		if (!Spec.IsValid())
		{
			continue;
		}
		Spec.Data->SetSetByCallerMagnitude(AttackPowerTag, AttackPowerSnapshot);
		Spec.Data->SetSetByCallerMagnitude(MotionValueTag, MotionValueSnapshot);
		Spec.Data->SetSetByCallerMagnitude(BaseStaggerTag, 0.0f);
		Spec.Data->SetSetByCallerMagnitude(CritOverrideTag, 0.0f);
		Spec.Data->SetSetByCallerMagnitude(DanceMultiplierTag, 1.0f);
	SourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data, TargetASC);
	}
}

bool URes_InsectGlaive::CanGatherPowders(const FVector& Center, const float GatherRadius) const
{
	if (bRuntimeShuttingDown || !CombatConfig || !GetWorld()
		|| !RuntimeContext.RuntimeToken.IsValid()
		|| !IsRuntimeRequestCurrent(RuntimeContext.RuntimeToken)
		|| !FMath::IsFinite(Center.X) || !FMath::IsFinite(Center.Y)
		|| !FMath::IsFinite(Center.Z)
		|| !FMath::IsFinite(GatherRadius)
		|| GatherRadius <= 0.0f)
	{
		return false;
	}

	const double Now = GetWorld()->GetTimeSeconds();
	const double RadiusSquared = FMath::Square(
		static_cast<double>(GatherRadius));
	int32 AvailableCount = 0;
	for (const TWeakObjectPtr<AIGPowderActor>& WeakPowder : OwnedPowders)
	{
		const AIGPowderActor* Powder = WeakPowder.Get();
		if (Powder && Powder->IsAvailableFor(this, RuntimeContext.RuntimeToken, Now)
			&& FVector::DistSquared(Center, Powder->GetActorLocation()) <= RadiusSquared
			&& ++AvailableCount > 0)
		{
			return true;
		}
	}
	return AvailableCount > 0;
}

bool URes_InsectGlaive::TryReservePowders(const FWeaponActionToken& Action,
	const FVector& Center, FIGPowderReservationToken& OutToken, const float GatherRadius, const int32 MaxCount)
{
	OutToken = FIGPowderReservationToken();
	UMHGZWeaponRuntimeHostComponent* Host = RuntimeContext.RuntimeToken.Host.Get();
	if (!Action.IsValid() || !IsRuntimeRequestCurrent(Action.RuntimeToken)
		|| Action.RuntimeToken != RuntimeContext.RuntimeToken || !GetWorld()
		|| !Host || !Host->IsActionRegistered(Action)
		|| !CombatConfig || !FMath::IsFinite(Center.X)
		|| !FMath::IsFinite(Center.Y) || !FMath::IsFinite(Center.Z)
		|| MaxCount <= 0 || !CanGatherPowders(Center, GatherRadius))
	{
		return false;
	}
	for (const TPair<uint64, FReservedPowderSet>& Pair : PowderReservations)
	{
		if (Pair.Value.Token.RuntimeToken == Action.RuntimeToken
			&& Pair.Value.Token.ActivationSequenceID == Action.ActivationSequenceID)
		{
			return false; // repeated GatherBegin cannot acquire a second set
		}
	}

	struct FCandidate
	{
		AIGPowderActor* Actor = nullptr;
		double DistanceSquared = 0.0;
	};
	const double Now = GetWorld()->GetTimeSeconds();
	const double RadiusSquared = FMath::Square(
		static_cast<double>(GatherRadius));
	TArray<FCandidate> Candidates;
	for (const TWeakObjectPtr<AIGPowderActor>& WeakPowder : OwnedPowders)
	{
		AIGPowderActor* Powder = WeakPowder.Get();
		if (!Powder || !Powder->IsAvailableFor(this, Action.RuntimeToken, Now))
		{
			continue;
		}
		const double DistanceSquared = FVector::DistSquared(Center, Powder->GetActorLocation());
		if (DistanceSquared <= RadiusSquared)
		{
			Candidates.Add({ Powder, DistanceSquared });
		}
	}
	Candidates.Sort([](const FCandidate& A, const FCandidate& B)
	{
		if (A.DistanceSquared != B.DistanceSquared)
		{
			return A.DistanceSquared < B.DistanceSquared;
		}
		return A.Actor->GetSpawnSequenceID() < B.Actor->GetSpawnSequenceID();
	});
	Candidates.SetNum(FMath::Min(Candidates.Num(), MaxCount));
	if (Candidates.IsEmpty())
	{
		return false;
	}

	uint64 ReservationID = NextPowderReservationID++;
	if (ReservationID == 0)
	{
		ReservationID = NextPowderReservationID++;
	}
	if (ReservationID == 0)
	{
		return false;
	}
	FIGPowderReservationToken Token;
	Token.RuntimeToken = Action.RuntimeToken;
	Token.ActivationSequenceID = Action.ActivationSequenceID;
	Token.ReservationID = ReservationID;

	FReservedPowderSet Pending;
	Pending.Token = Token;
	Pending.Center = Center;
	Pending.CreatedWorldTime = Now;
	for (const FCandidate& Candidate : Candidates)
	{
		AIGPowderActor* Powder = Candidate.Actor;
		if (!IsValid(Powder) || !Powder->TryReserve(Token))
		{
			for (const FReservedPowderMember& Member : Pending.Members)
			{
				if (AIGPowderActor* Reserved = Member.Actor.Get())
				{
					Reserved->ReleaseReservation(Token, Member.OriginalTransform, Now);
				}
			}
			return false;
		}
		FReservedPowderMember& Member = Pending.Members.AddDefaulted_GetRef();
		Member.Actor = Powder;
		Member.OriginalTransform = Powder->GetActorTransform();
	}

	PowderReservations.Add(Token.ReservationID, MoveTemp(Pending));
	OutToken = Token;
	return true;
}

bool URes_InsectGlaive::BeginPowderGathering(
	const FIGPowderReservationToken& Token)
{
	UMHGZWeaponRuntimeHostComponent* Host = RuntimeContext.RuntimeToken.Host.Get();
	if (!Token.IsValid() || bRuntimeShuttingDown || !Host
		|| Token.RuntimeToken != RuntimeContext.RuntimeToken
		|| !IsRuntimeRequestCurrent(Token.RuntimeToken)
		|| !Host->IsActionActivationRegistered(Token.RuntimeToken,
			Token.ActivationSequenceID))
	{
		return false;
	}

	FReservedPowderSet* Reservation = PowderReservations.Find(Token.ReservationID);
	if (!Reservation || Reservation->Token != Token || Reservation->bGathering)
	{
		return false;
	}

	Reservation->bGathering = true;
	return true;
}

bool URes_InsectGlaive::UpdatePowderGathering(
	const FIGPowderReservationToken& Token, const float Progress)
{
	if (!Token.IsValid() || bRuntimeShuttingDown || !FMath::IsFinite(Progress)
		|| Token.RuntimeToken != RuntimeContext.RuntimeToken
		|| !IsRuntimeRequestCurrent(Token.RuntimeToken))
	{
		return false;
	}

	FReservedPowderSet* Reservation = PowderReservations.Find(Token.ReservationID);
	if (!Reservation || Reservation->Token != Token || !Reservation->bGathering)
	{
		return false;
	}

	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const float Alpha = FMath::Clamp(Progress, 0.0f, 1.0f);
	for (const FReservedPowderMember& Member : Reservation->Members)
	{
		AIGPowderActor* Powder = Member.Actor.Get();
		if (!Powder)
		{
			continue;
		}
		if (!Powder->CanCommitReservation(Token, Now))
		{
			// An expired or otherwise invalid member must not remain reserved if it
			// survives until its timer callback is processed.
			Powder->ReleaseReservationAtCurrentLocation(Token, Now);
			continue;
		}

		const FVector GatherLocation = FMath::Lerp(
			Member.OriginalTransform.GetLocation(), Reservation->Center, Alpha);
		Powder->SetGatherPresentationLocation(Token, GatherLocation);
	}
	return true;
}

bool URes_InsectGlaive::CommitPowderReservation(
	const FIGPowderReservationToken& Token,
	FIGPowderExplosionSnapshot& OutExplosion, const float ExplosionRadius)
{
	OutExplosion = FIGPowderExplosionSnapshot();
	UMHGZWeaponRuntimeHostComponent* Host = RuntimeContext.RuntimeToken.Host.Get();
	if (!Token.IsValid() || !IsRuntimeRequestCurrent(Token.RuntimeToken)
		|| Token.RuntimeToken != RuntimeContext.RuntimeToken || !GetWorld()
		|| !Host || !Host->IsActionActivationRegistered(
			Token.RuntimeToken, Token.ActivationSequenceID)
		|| !CombatConfig)
	{
		return false;
	}
	FReservedPowderSet* Found = PowderReservations.Find(Token.ReservationID);
	if (!Found || Found->Token != Token || !Found->bGathering)
	{
		return false;
	}
	if (!FMath::IsFinite(ExplosionRadius) || ExplosionRadius <= 0.0f)
    { ReleasePowderReservation(Token); return false; }

	// Copy and remove the transaction before changing actors; EndPlay callbacks may
	// update the registry synchronously when the consumed actors are destroyed.
	const FReservedPowderSet Reservation = *Found;
	PowderReservations.Remove(Token.ReservationID);
	const double Now = GetWorld()->GetTimeSeconds();
	TArray<AIGPowderActor*> LiveMembers;
	double TotalDamage = 0.0;
	for (const FReservedPowderMember& Member : Reservation.Members)
	{
		AIGPowderActor* Powder = Member.Actor.Get();
		if (Powder && Powder->CanCommitReservation(Token, Now))
		{
			LiveMembers.Add(Powder);
			TotalDamage += Powder->GetDamageContribution();
		}
		else if (Powder)
		{
			Powder->ReleaseReservation(Token, Member.OriginalTransform, Now);
		}
	}
	if (LiveMembers.IsEmpty() || !FMath::IsFinite(TotalDamage) || TotalDamage > static_cast<double>(MAX_flt))
	{
		// If all reserved members expired/died, fail and release any surviving stale
		// members; a partial live set is valid and is committed below at actual count.
		for (const FReservedPowderMember& Member : Reservation.Members)
		{
			if (AIGPowderActor* Powder = Member.Actor.Get())
			{
				Powder->ReleaseReservation(Token, Member.OriginalTransform, Now);
			}
		}
		return false;
	}

	for (AIGPowderActor* Powder : LiveMembers)
	{
		Powder->MarkConsumed(Token);
	}
	FIGPowderExplosionSnapshot Snapshot;
	Snapshot.ExplosionID = FGuid::NewGuid();
	Snapshot.Center = Reservation.Center;
	Snapshot.ConsumedCount = LiveMembers.Num();
	Snapshot.TotalDamage = TotalDamage;
	Snapshot.Radius = ExplosionRadius;
	Snapshot.BaseStagger = 0.0f;
	OutExplosion = Snapshot;

	for (AIGPowderActor* Powder : LiveMembers)
	{
		Powder->Destroy();
	}
	return true;
}

void URes_InsectGlaive::ReleasePowderReservation(
	const FIGPowderReservationToken& Token)
{
	if (!Token.IsValid())
	{
		return;
	}
	FReservedPowderSet* Found = PowderReservations.Find(Token.ReservationID);
	if (!Found || Found->Token != Token)
	{
		return;
	}
	const FReservedPowderSet Reservation = *Found;
	PowderReservations.Remove(Token.ReservationID);
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	for (const FReservedPowderMember& Member : Reservation.Members)
	{
		if (AIGPowderActor* Powder = Member.Actor.Get())
		{
			Powder->ReleaseReservation(Token, Member.OriginalTransform, Now);
		}
	}
}

void URes_InsectGlaive::ClearOwnedPowders(const EWeaponRuntimeEndReason Reason)
{
	ActivePowderVortexSettings = FIGPowderVortexSettings();
	ActivePowderVortexAttackPower = 0.f;
	StopPowderVortexGatherSounds();
	const int32 Count = OwnedPowders.Num();
	const TArray<TWeakObjectPtr<AIGPowderActor>> Powders = OwnedPowders;
	OwnedPowders.Reset();
	PowderReservations.Reset();
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(PowderVortexCommitTimer);
	}
	ActivePowderVortexCommandSerial = 0;
	ActivePowderVortexReservationID = 0;
	SpawnedPowderHitIDs.Reset();
	NextPowderSpawnSequenceID = 1;
	NextPowderReservationID = 1;
	LastMarkedContactPowderWorldTime = -1.0;
	NextPowderMaintenanceWorldTime = 0.0;
	for (const TWeakObjectPtr<AIGPowderActor>& WeakPowder : Powders)
	{
		if (AIGPowderActor* Powder = WeakPowder.Get())
		{
			Powder->MarkTerminal(EIGPowderTerminalReason::RuntimeShutdown);
			Powder->Destroy();
		}
	}
	if (Count > 0)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[IG Powder] Cleared %d powder actors for runtime end reason %d."),
			Count, static_cast<int32>(Reason));
	}
}

TArray<FIGPowderRuntimeSnapshot> URes_InsectGlaive::GetPowderSnapshots() const
{
	TArray<FIGPowderRuntimeSnapshot> Snapshots;
	if (!GetWorld())
	{
		return Snapshots;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	for (const TWeakObjectPtr<AIGPowderActor>& WeakPowder : OwnedPowders)
	{
		const AIGPowderActor* Powder = WeakPowder.Get();
		if (Powder && Powder->GetRuntimeToken() == RuntimeContext.RuntimeToken
			&& Powder->GetOwnerResource() == this
			&& Powder->GetPowderState() != EIGPowderState::Consumed)
		{
			FIGPowderRuntimeSnapshot Snapshot = Powder->MakeSnapshot(Now);
			if (Snapshot.RemainingLifetime > 0.0f)
			{
				Snapshots.Add(MoveTemp(Snapshot));
			}
		}
	}
	return Snapshots;
}

void URes_InsectGlaive::HandlePowderActorEndPlay(AIGPowderActor* Powder)
{
	if (!Powder)
	{
		return;
	}
	OwnedPowders.RemoveAll([Powder](const TWeakObjectPtr<AIGPowderActor>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == Powder;
	});
	RemovePowderFromReservations(Powder);
}
