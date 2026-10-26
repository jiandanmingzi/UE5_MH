// Copyright MHGZ Project. All Rights Reserved.

// 虫印Debuff、表面表现与自动攻击行为。
#include "Res_InsectGlaive.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameplayEffect.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"
#include "AbilitySystemGlobals.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/EngineTypes.h"
#include "TimerManager.h"
#include "Particles/ParticleSystemComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InsectGlaive/Kinsect/IGMarkProjectile.h"
#include "InsectGlaive/Kinsect/InsectGlaiveKinsectData.h"
#include "InsectGlaive/Kinsect/KinsectCollisionComponent.h"
#include "InsectGlaive/Kinsect/Kinsect.h"
#include "Monster/MHGZMonsterBase.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"
#include "Math/RotationMatrix.h"

namespace
{
const FGameplayTag& MarkActiveTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("WeaponResource.IG.Mark.Active"));
	return Tag;
}
}

bool URes_InsectGlaive::IsAutomaticMarkBehaviorEligible() const
{
	if (bRuntimeShuttingDown || !KinsectActor || !KinsectData || !CombatConfig
		|| KinsectStamina <= 0.0f || !HasValidKinsectMark()
		|| !KinsectAttachComponent.IsValid()
		|| KinsectActionAttachmentToken.IsValid()
		|| KinsectPlayerCommandLeaseToken.IsValid()
		|| (KinsectCommandSource != EKinsectCommandSource::None
			&& KinsectCommandSource != EKinsectCommandSource::AutoMark)
		|| !FMath::IsFinite(CombatConfig->KinsectAutoMarkSearchRadius)
		|| !FMath::IsFinite(CombatConfig->KinsectAutoMarkAttackInterval)
		|| CombatConfig->KinsectAutoMarkSearchRadius <= 0.0f
		|| CombatConfig->KinsectAutoMarkAttackInterval <= 0.0f)
	{
		return false;
	}
	const EKinsectState KinsectState = KinsectActor->GetState();
	const bool bCanStartFromHover = KinsectState == EKinsectState::Hovering;
	const bool bCanContinueAutoFlight = KinsectState == EKinsectState::Flying
		&& KinsectCommandSource == EKinsectCommandSource::AutoMark
		&& KinsectActor->ActiveRequest.CommandSerial == KinsectCommandSerial
		&& KinsectActor->ActiveRequest.MarkSerial == MarkSerial;
	if (!bCanStartFromHover && !bCanContinueAutoFlight)
	{
		return false;
	}
	const UMHGZWeaponRuntimeHostComponent* Host = RuntimeContext.RuntimeToken.Host.Get();
	// Hunter attacks can coexist with a detached Kinsect's automatic command.
	// The leases, attachment and command owner above arbitrate actual Kinsect use.
	return IsRuntimeRequestCurrent(RuntimeContext.RuntimeToken) && Host;
}

void URes_InsectGlaive::CancelAutomaticMarkBehavior(
	const bool bInterruptFlight)
{
	if (KinsectCommandSource != EKinsectCommandSource::AutoMark)
	{
		return;
	}
	const bool bInterruptingAutoFlight = KinsectActor && bInterruptFlight
		&& KinsectActor->GetState() == EKinsectState::Flying
		&& KinsectActor->ActiveRequest.CommandSource == EKinsectCommandSource::AutoMark
		&& KinsectActor->ActiveRequest.CommandSerial == KinsectCommandSerial;
	AdvanceKinsectCommand(EKinsectCommandSource::None);
	if (bInterruptingAutoFlight)
	{
		KinsectActor->Interrupt();
	}
}

void URes_InsectGlaive::RecordAutomaticMarkWorldBlock(
	const FKinsectFlightRequest& Request, const FHitResult& Hit) const
{
	if (Request.CommandSource != EKinsectCommandSource::AutoMark
		|| !IsKinsectCommandCurrent(Request))
	{
		return;
	}
	FWeaponCombatTelemetryEvent Event;
	Event.EventName = TEXT("AutoMarkWorldBlocked");
	Event.MarkID = ActiveMarkID;
	Event.CommandSerial = Request.CommandSerial;
	Event.FlightID = Request.FlightInstanceID;
	Event.TargetActor = Hit.GetActor() ? Hit.GetActor()->GetFName() : NAME_None;
	Event.Hitzone = Hit.GetComponent() ? Hit.GetComponent()->GetFName() : NAME_None;
	Event.ImpactPoint = Hit.ImpactPoint;
	Event.Reason = Hit.bStartPenetrating ? TEXT("StartPenetrating") : TEXT("BlockingSweep");
	RecordCombatTelemetryEvent(MoveTemp(Event));
}

bool URes_InsectGlaive::ResolveAutomaticMarkApproachPoint(FVector& OutPoint) const
{
	FVector MarkLocation;
	FVector MarkNormal;
	if (!KinsectActor || !GetWorld()
		|| !GetKinsectMarkSurfaceFrame(MarkSerial, MarkLocation, MarkNormal))
	{
		return false;
	}
	// A sphere enclosing the rotating flight capsule keeps the approach reachable
	// even for a mark near a floor or on a downward-facing surface.
	const float Clearance = KinsectActor->Collision
		? KinsectActor->Collision->GetScaledCapsuleHalfHeight() + 2.0f : 2.0f;
	OutPoint = MarkLocation + MarkNormal * Clearance;
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(KinsectMarkApproach), false);
	Params.AddIgnoredActor(KinsectActor);
	Params.AddIgnoredActor(GetOwner());
	Params.AddIgnoredActor(ActiveMarkTargetActor.Get());
	FHitResult FloorHit;
	if (GetWorld()->SweepSingleByObjectType(FloorHit,
		OutPoint + FVector::UpVector * (Clearance * 4.0f), OutPoint,
		FQuat::Identity, Objects, FCollisionShape::MakeSphere(Clearance), Params))
	{
		if (FloorHit.bStartPenetrating)
		{
			return false;
		}
		OutPoint = FloorHit.Location + FloorHit.ImpactNormal * 2.0f;
	}
	return !OutPoint.ContainsNaN();
}

bool URes_InsectGlaive::StartAutomaticMarkSeek()
{
	if (!IsAutomaticMarkBehaviorEligible() || !KinsectActor || !GetWorld()
		|| KinsectActor->GetState() != EKinsectState::Hovering)
	{
		return false;
	}

	FVector MarkLocation;
	FVector MarkNormal;
	if (!GetKinsectMarkSurfaceFrame(MarkSerial, MarkLocation, MarkNormal)
		|| FVector::Dist(KinsectActor->GetActorLocation(), MarkLocation)
			> CombatConfig->KinsectAutoMarkSearchRadius)
	{
		return false;
	}

	FVector ApproachPoint;
	if (!ResolveAutomaticMarkApproachPoint(ApproachPoint))
	{
		return false;
	}
	const float ApproachOffset = FVector::Distance(MarkLocation, ApproachPoint);
	const double Now = GetWorld()->GetTimeSeconds();
	FKinsectFlightRequest Request;
	Request.RuntimeToken = RuntimeContext.RuntimeToken;
	Request.TrajectoryMode = EKinsectTrajectoryMode::ToPoint;
	Request.TargetPointSnapshot = ApproachPoint;
	Request.MaxDistance = FMath::Max(KinsectData->MaxFlightRange,
		CombatConfig->KinsectAutoMarkSearchRadius + ApproachOffset);
	Request.FlightSpeed = KinsectData->FlightSpeed;
	Request.ArrivalRadius = 1.0f;
	Request.DamageMode = EKinsectDamageMode::None;
	Request.ExtractMode = EKinsectExtractMode::None;
	Request.PostFlightPolicy = EKinsectPostFlightPolicy::Hover;
	Request.MotionValue = 0.0f;
	Request.RehitInterval = 0.0f;
	Request.FlightInstanceID = FGuid::NewGuid();
	Request.CommandSource = EKinsectCommandSource::AutoMark;
	Request.CommandSerial = GetNextKinsectCommandSerial();
	Request.MarkSerial = MarkSerial;
	if (FVector::DistSquared(KinsectActor->GetActorLocation(), ApproachPoint) <= 1.0f)
	{
		AdvanceKinsectCommand(EKinsectCommandSource::AutoMark);
		Request.CommandSerial = KinsectCommandSerial;
		if (!KinsectActor->BeginAutomaticMarkContact(Request))
		{
			AdvanceKinsectCommand(EKinsectCommandSource::None);
			return false;
		}
		AutoMarkCommandSerial = KinsectCommandSerial;
		AutoMarkCommandMarkSerial = MarkSerial;
		bAutoMarkApproachComplete = true;
		NextAutoMarkActionWorldTime = Now
			+ CombatConfig->KinsectAutoMarkAttackInterval;
		return true;
	}
	if (!KinsectActor->CanBeginFlight(Request)
		|| !KinsectActor->BeginFlight(Request))
	{
		return false;
	}
	AdvanceKinsectCommand(EKinsectCommandSource::AutoMark);
	AutoMarkCommandSerial = KinsectCommandSerial;
	AutoMarkCommandMarkSerial = MarkSerial;
	bAutoMarkApproachComplete = false;
	NextAutoMarkActionWorldTime = 0.0;
	return true;
}

bool URes_InsectGlaive::PerformAutomaticMarkContactAttack()
{
	if (!IsAutomaticMarkBehaviorEligible() || !KinsectActor
		|| KinsectActor->GetState() != EKinsectState::Hovering
		|| KinsectCommandSerial != AutoMarkCommandSerial
		|| MarkSerial != AutoMarkCommandMarkSerial)
	{
		return false;
	}

	FHitResult Hit;
	if (!KinsectActor->SweepAutomaticMarkContact(KinsectCommandSerial,
		MarkSerial, RuntimeContext.RuntimeToken, Hit))
	{
		FWeaponCombatTelemetryEvent Event;
		Event.EventName = TEXT("AutoMarkContactMiss");
		Event.MarkID = ActiveMarkID;
		Event.CommandSerial = KinsectCommandSerial;
		Event.TargetActor = ActiveMarkTargetActor->GetFName();
		Event.Hitzone = ActiveMarkHitzone->GetFName();
		Event.ImpactPoint = KinsectActor->GetActorLocation();
		Event.Reason = TEXT("NoMarkedPartContact");
		RecordCombatTelemetryEvent(MoveTemp(Event));
		return false;
	}
	const FGuid HitInstanceID = FGuid::NewGuid();
	const FGuid AttackInstanceID = FGuid::NewGuid();
	const FKinsectFlightRequest ContactRequest = KinsectActor->ActiveRequest;
	const TWeakObjectPtr<AKinsect> ContactKinsect = KinsectActor;
	const TWeakObjectPtr<UMHGZMonsterHitzoneComponent> ContactPart =
		UMHGZMonsterHitzoneComponent::Resolve(Hit.GetComponent());
	const auto IsContactCurrent = [&]()
	{
		return ContactKinsect.IsValid() && KinsectActor == ContactKinsect.Get()
			&& KinsectActor->GetState() == EKinsectState::Hovering
			&& KinsectActor->ActiveRequest.FlightInstanceID == ContactRequest.FlightInstanceID
			&& IsKinsectCommandCurrent(ContactRequest);
	};
	const bool bDamageApplied = ApplyKinsectDamage(Hit,
		CombatConfig->SendKinsectMotionValue, HitInstanceID, AttackInstanceID);
	if (bDamageApplied && IsContactCurrent())
	{
		// Seeking stays non-extracting. Only this successful real contact may
		// collect, using the ordinary single-color cargo and return-delivery path.
		if (ContactPart.IsValid()
			&& KinsectActor->CapturePendingExtractColor(ContactPart->ExtractColorTag))
		{
			FWeaponCombatTelemetryEvent Event;
			Event.EventName = TEXT("KinsectCargoCaptured");
			Event.ResourceTag = ContactPart->ExtractColorTag.GetTagName();
			Event.SourceAttackID = AttackInstanceID;
			Event.HitID = HitInstanceID;
			Event.CommandSerial = ContactRequest.CommandSerial;
			Event.MarkID = ActiveMarkID;
			Event.TargetActor = Hit.GetActor()->GetFName();
			Event.Hitzone = ContactPart->GetFName();
			Event.ImpactPoint = Hit.ImpactPoint;
			Event.Reason = TEXT("AutoMarkPendingReturn");
			RecordCombatTelemetryEvent(MoveTemp(Event));
			BroadcastResourceUIState(true);
		}
		// Damage/UI callbacks may issue a newer player command.
		if (!IsContactCurrent()) return bDamageApplied;
		HandleSuccessfulKinsectDamage(RuntimeContext.RuntimeToken,
			AttackInstanceID, HitInstanceID, Hit,
			EIGPowderSpawnPolicy::AutoMarkContact);
	}
	return bDamageApplied;
}

void URes_InsectGlaive::UpdateAutomaticMarkBehavior()
{
	if (!KinsectActor || bRuntimeShuttingDown
		|| GFrameCounter < EarliestAutoMarkUpdateFrame)
	{
		return;
	}

	if (KinsectCommandSource == EKinsectCommandSource::AutoMark
		&& (!IsAutomaticMarkBehaviorEligible()
			|| MarkSerial != AutoMarkCommandMarkSerial
			|| KinsectCommandSerial != AutoMarkCommandSerial))
	{
		CancelAutomaticMarkBehavior(true);
		return;
	}
	if (!IsAutomaticMarkBehaviorEligible())
	{
		const bool bDeferredByPriority = HasValidKinsectMark()
			&& KinsectActor->GetState() != EKinsectState::Attached
			&& (KinsectPlayerCommandLeaseToken.IsValid()
				|| KinsectActionAttachmentToken.IsValid()
				|| (KinsectCommandSource != EKinsectCommandSource::None
					&& KinsectCommandSource != EKinsectCommandSource::AutoMark));
		if (bDeferredByPriority && !bAutoMarkDeferredByPriority)
		{
			FWeaponCombatTelemetryEvent Event;
			Event.EventName = TEXT("AutoDeferred");
			Event.MarkID = ActiveMarkID;
			Event.CommandSerial = KinsectCommandSerial;
			Event.Reason = TEXT("HigherPriorityPlayerOrActionCommand");
			RecordCombatTelemetryEvent(MoveTemp(Event));
		}
		bAutoMarkDeferredByPriority = bDeferredByPriority;
		return;
	}
	bAutoMarkDeferredByPriority = false;

	const EKinsectState State = KinsectActor->GetState();
	if (State == EKinsectState::Attached || State == EKinsectState::Returning
		|| State == EKinsectState::ActionAttached)
	{
		CancelAutomaticMarkBehavior(false);
		return;
	}
	FVector MarkLocation;
	FVector MarkNormal;
	if (!GetKinsectMarkSurfaceFrame(MarkSerial, MarkLocation, MarkNormal))
	{
		CancelAutomaticMarkBehavior(true);
		return;
	}
	if (FVector::Dist(KinsectActor->GetActorLocation(), MarkLocation)
		> CombatConfig->KinsectAutoMarkSearchRadius)
	{
		CancelAutomaticMarkBehavior(true);
		return;
	}

	FVector ApproachPoint;
	if (!ResolveAutomaticMarkApproachPoint(ApproachPoint))
	{
		CancelAutomaticMarkBehavior(true);
		return;
	}
	if (State == EKinsectState::Flying)
	{
		KinsectActor->UpdateAutomaticMarkTarget(ApproachPoint,
			AutoMarkCommandSerial, AutoMarkCommandMarkSerial);
		return;
	}
	if (State != EKinsectState::Hovering)
	{
		return;
	}

	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (KinsectCommandSource != EKinsectCommandSource::AutoMark)
	{
		StartAutomaticMarkSeek();
		return;
	}
	if (!bAutoMarkApproachComplete)
	{
		if (Now >= NextAutoMarkActionWorldTime)
		{
			StartAutomaticMarkSeek();
		}
		return;
	}

	if (FVector::DistSquared(KinsectActor->GetActorLocation(), ApproachPoint) > 25.0f)
	{
		bAutoMarkApproachComplete = false;
		NextAutoMarkActionWorldTime = Now;
		StartAutomaticMarkSeek();
		return;
	}
	// Keep facing the local mark between attacks as its component/bone moves.
	// Only the visual mesh rotates: the physical capsule must stay floor-safe.
	if (!KinsectActor->FaceAutomaticMark(AutoMarkCommandSerial, AutoMarkCommandMarkSerial))
	{
		return;
	}
	if (Now >= NextAutoMarkActionWorldTime)
	{
		const uint64 ContactSerial = AutoMarkCommandSerial;
		const uint64 ContactMarkSerial = AutoMarkCommandMarkSerial;
		PerformAutomaticMarkContactAttack();
		if (KinsectCommandSerial == ContactSerial && MarkSerial == ContactMarkSerial
			&& !bRuntimeShuttingDown && CombatConfig)
		{
			NextAutoMarkActionWorldTime = Now + CombatConfig->KinsectAutoMarkAttackInterval;
		}
	}
}

bool URes_InsectGlaive::LaunchKinsectMark(const FWeaponAimSnapshot& AimSnapshot)
{
	if (bRuntimeShuttingDown || !CombatConfig || !GetWorld()
		|| AimSnapshot.Context != EWeaponAimSnapshotContext::Kinsect
		|| AimSnapshot.Direction.ContainsNaN()
		|| AimSnapshot.Direction.GetSafeNormal().IsNearlyZero())
	{
		return false;
	}

	TSubclassOf<AIGMarkProjectile> ProjectileClass = CombatConfig->KinsectMarkProjectileClass;
	if (!ProjectileClass)
	{
		ProjectileClass = AIGMarkProjectile::StaticClass();
	}

	FVector LaunchOrigin = AimSnapshot.Origin;
	FVector LaunchDirection = AimSnapshot.Direction.GetSafeNormal();
	if (!CombatConfig->KinsectMarkLaunchSocket.IsNone())
	{
		ACharacter* Character = RuntimeContext.Character.Get();
		TArray<USkeletalMeshComponent*> Meshes;
		if (Character)
		{
			Character->GetComponents<USkeletalMeshComponent>(Meshes);
		}
		USkeletalMeshComponent* LaunchMesh = nullptr;
		for (USkeletalMeshComponent* Mesh : Meshes)
		{
			if (Mesh && Mesh->ComponentHasTag(TEXT("WeaponTrace"))
				&& Mesh->DoesSocketExist(CombatConfig->KinsectMarkLaunchSocket))
			{
				LaunchMesh = Mesh;
				break;
			}
		}
		if (!LaunchMesh)
		{
			return false;
		}
		LaunchOrigin = LaunchMesh->GetSocketLocation(CombatConfig->KinsectMarkLaunchSocket);
		LaunchDirection = (AimSnapshot.TargetPoint - LaunchOrigin).GetSafeNormal();
		if (LaunchDirection.IsNearlyZero())
		{
			return false;
		}
	}
	FWeaponAimSnapshot LaunchSnapshot = AimSnapshot;
	LaunchSnapshot.Origin = LaunchOrigin;
	LaunchSnapshot.Direction = LaunchDirection;
	AIGMarkProjectile* Projectile = GetWorld()->SpawnActorDeferred<AIGMarkProjectile>(
		ProjectileClass,
		FTransform(LaunchDirection.Rotation(), LaunchOrigin),
		GetOwner(), Cast<APawn>(GetOwner()), ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Projectile)
	{
		return false;
	}
	Projectile->Initialize(this, RuntimeContext.RuntimeToken, LaunchSnapshot,
		CombatConfig->KinsectMarkProjectileSpeed,
		CombatConfig->KinsectMarkProjectileRadius,
		CombatConfig->KinsectMarkMaxDistance,
		CombatConfig->KinsectMarkProjectileLifetime);
	Projectile->FinishSpawning(FTransform(LaunchDirection.Rotation(), LaunchOrigin));
	return true;
}

bool URes_InsectGlaive::SetKinsectMarkFromMeleeHit(const FHitResult& Hit)
{
	UMHGZMonsterHitzoneComponent* Hitzone =
		UMHGZMonsterHitzoneComponent::Resolve(Hit.GetComponent());
	return SetKinsectMarkInternal(Hitzone, Hit.ImpactPoint,
		Hit.ImpactNormal, Hit.TraceStart - Hit.ImpactPoint, nullptr);
}

bool URes_InsectGlaive::SetKinsectMark(UMHGZMonsterHitzoneComponent* Hitzone,
	const FVector& ImpactPoint, AIGMarkProjectile* Projectile,
	const FVector& PreferredOutwardDirection)
{
	return SetKinsectMarkInternal(Hitzone, ImpactPoint,
		FVector::ZeroVector, PreferredOutwardDirection, Projectile);
}

bool URes_InsectGlaive::SetKinsectMarkInternal(
	UMHGZMonsterHitzoneComponent* Hitzone, const FVector& ImpactPoint,
	const FVector& ImpactNormal, const FVector& PreferredOutwardDirection,
	AIGMarkProjectile* Projectile)
{
	if (bRuntimeShuttingDown || !CombatConfig || !GetWorld() || !Hitzone
		|| Hitzone->GetWorld() != GetWorld()
		|| !IsValid(Hitzone->GetOwner())
		|| !FMath::IsFinite(ImpactPoint.X) || !FMath::IsFinite(ImpactPoint.Y)
		|| !FMath::IsFinite(ImpactPoint.Z)
		|| !FMath::IsFinite(CombatConfig->KinsectMarkDuration)
		|| CombatConfig->KinsectMarkDuration <= 0.0f
		|| (Projectile && Projectile->GetOwner() != GetOwner()))
	{
		return false;
	}

	AActor* TargetActor = Hitzone->GetOwner();
	UAbilitySystemComponent* TargetASC =
		UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(TargetActor);
	const UMHGZAttributeSet* TargetAttributes = TargetASC
		? TargetASC->GetSet<UMHGZAttributeSet>() : nullptr;
	if (TargetAttributes && TargetAttributes->GetHealth() <= 0.0f)
	{
		return false;
	}

	const FQuat HitzoneRotation = Hitzone->GetComponentQuat();
	FVector SurfaceNormal = ImpactNormal.GetSafeNormal();
	if (SurfaceNormal.IsNearlyZero())
	{
		SurfaceNormal = (ImpactPoint - Hitzone->GetComponentLocation()).GetSafeNormal();
	}
	if (SurfaceNormal.IsNearlyZero())
	{
		SurfaceNormal = HitzoneRotation.GetUpVector();
	}
	FVector VisualSurfacePoint = ImpactPoint;
	FVector VisualSurfaceNormal = SurfaceNormal;
	if (const AMHGZMonsterBase* Monster = Cast<AMHGZMonsterBase>(TargetActor))
	{
		if (!Monster->ResolveHitEffectVisualSurface(
			Hitzone, ImpactPoint, SurfaceNormal, PreferredOutwardDirection,
			VisualSurfacePoint, VisualSurfaceNormal))
		{
			VisualSurfacePoint = ImpactPoint;
			VisualSurfaceNormal = SurfaceNormal;
		}
	}
	VisualSurfaceNormal = VisualSurfaceNormal.GetSafeNormal();
	if (VisualSurfacePoint.ContainsNaN() || VisualSurfaceNormal.IsNearlyZero())
	{
		VisualSurfacePoint = ImpactPoint;
		VisualSurfaceNormal = SurfaceNormal;
	}
	if (VisualSurfaceNormal.IsNearlyZero())
	{
		return false;
	}
	FVector TangentX = FVector::VectorPlaneProject(
		HitzoneRotation.GetForwardVector(), VisualSurfaceNormal).GetSafeNormal();
	if (TangentX.IsNearlyZero())
	{
		TangentX = FVector::VectorPlaneProject(
			HitzoneRotation.GetRightVector(), VisualSurfaceNormal).GetSafeNormal();
	}
	if (TangentX.IsNearlyZero())
	{
		return false;
	}
	const FVector TangentY = FVector::CrossProduct(
		VisualSurfaceNormal, TangentX).GetSafeNormal();
	if (TangentY.IsNearlyZero())
	{
		return false;
	}
	const double CreatedAt = GetWorld()->GetTimeSeconds();
	const double ExpireAt = CreatedAt + CombatConfig->KinsectMarkDuration;
	if (!FMath::IsFinite(ExpireAt))
	{
		return false;
	}

	ClearKinsectMark(EIGMarkClearReason::Replaced);
	ActiveMarkHitzone = Hitzone;
	ActiveMarkProjectile = Projectile;
	ActiveMarkTargetActor = TargetActor;
	ActiveMarkTargetASC = TargetAttributes ? TargetASC : nullptr;
	ActiveMarkBoneName = Hitzone->BoneName;
	bActiveMarkUsesProjectile = Projectile != nullptr;
	const FTransform HitzoneTransform = Hitzone->GetComponentTransform();
	// The surface point is only the visual/automatic-seek anchor; the Debuff
	// applies to the whole hitzone, independent of effect size or surface location.
	ActiveMarkLocalPoint = HitzoneTransform.InverseTransformPosition(VisualSurfacePoint);
	ActiveMarkLocalNormal = HitzoneRotation.UnrotateVector(
		VisualSurfaceNormal).GetSafeNormal();
	ActiveMarkLocalTangentX = HitzoneRotation.UnrotateVector(TangentX).GetSafeNormal();
	ActiveMarkLocalTangentY = HitzoneRotation.UnrotateVector(TangentY).GetSafeNormal();
	ActiveMarkVisualLocalPoint = ActiveMarkLocalPoint;
	ActiveMarkVisualLocalNormal = ActiveMarkLocalNormal;
	ActiveMarkCreatedAt = CreatedAt;
	ActiveMarkExpireTime = ExpireAt;
	ActiveMarkID = FGuid::NewGuid();
	Hitzone->ApplyKinsectMarkDebuff(this, ActiveMarkID, ExpireAt);
	if (TargetAttributes && TargetASC)
	{
		MarkTargetHealthChangedDelegateHandle =
			TargetASC->GetGameplayAttributeValueChangeDelegate(
				UMHGZAttributeSet::GetHealthAttribute()).AddUObject(this,
					&URes_InsectGlaive::HandleMarkTargetHealthChanged);
	}
	SetMarkActiveTag(true);

	const uint64 ThisMarkSerial = ++MarkSerial;
	GetWorld()->GetTimerManager().SetTimer(MarkExpiryTimer,
		FTimerDelegate::CreateWeakLambda(this, [this, ThisMarkSerial]()
		{
			ExpireKinsectMark(ThisMarkSerial);
		}), CombatConfig->KinsectMarkDuration, false);
	FWeaponCombatTelemetryEvent MarkEvent;
	MarkEvent.EventName = TEXT("MarkCreated");
	MarkEvent.MarkID = ActiveMarkID;
	MarkEvent.TargetActor = TargetActor->GetFName();
	MarkEvent.Hitzone = Hitzone->GetFName();
	MarkEvent.ImpactPoint = ImpactPoint;
	MarkEvent.ExpireAt = ExpireAt;
	RecordCombatTelemetryEvent(MoveTemp(MarkEvent));
	SpawnKinsectMarkEffect();
	return true;
}

void URes_InsectGlaive::ExpireKinsectMark(const uint64 ExpectedMarkSerial)
{
	if (ExpectedMarkSerial == MarkSerial)
	{
		ClearKinsectMark(EIGMarkClearReason::Expired);
	}
}

void URes_InsectGlaive::ClearKinsectMark(EIGMarkClearReason Reason)
{
	if (UMHGZMonsterHitzoneComponent* Part = ActiveMarkHitzone.Get())
	{
		Part->RemoveKinsectMarkDebuff(this, ActiveMarkID);
	}
	if (ActiveMarkID.IsValid())
	{
		FWeaponCombatTelemetryEvent MarkEvent;
		MarkEvent.EventName = (Reason == EIGMarkClearReason::TargetInvalid
			|| Reason == EIGMarkClearReason::TargetDead)
			? FName(TEXT("MarkTargetDeadOrInvalid")) : FName(TEXT("MarkCleared"));
		MarkEvent.MarkID = ActiveMarkID;
		MarkEvent.TargetActor = ActiveMarkTargetActor.IsValid()
			? ActiveMarkTargetActor->GetFName() : NAME_None;
		MarkEvent.Hitzone = ActiveMarkBoneName;
		MarkEvent.ImpactPoint = ActiveMarkHitzone.IsValid()
			? ActiveMarkHitzone->GetComponentTransform().TransformPosition(ActiveMarkLocalPoint)
			: FVector::ZeroVector;
		MarkEvent.ExpireAt = ActiveMarkExpireTime;
		switch (Reason)
		{
		case EIGMarkClearReason::Replaced: MarkEvent.Reason = TEXT("Replaced"); break;
		case EIGMarkClearReason::Expired: MarkEvent.Reason = TEXT("Expired"); break;
		case EIGMarkClearReason::TargetInvalid: MarkEvent.Reason = TEXT("TargetInvalid"); break;
		case EIGMarkClearReason::TargetDead: MarkEvent.Reason = TEXT("TargetDead"); break;
		case EIGMarkClearReason::PowderVortex: MarkEvent.Reason = TEXT("PowderVortex"); break;
		case EIGMarkClearReason::WeaponChanged: MarkEvent.Reason = TEXT("WeaponChanged"); break;
		case EIGMarkClearReason::RuntimeShutdown: MarkEvent.Reason = TEXT("RuntimeShutdown"); break;
		default: MarkEvent.Reason = TEXT("Unknown"); break;
		}
		RecordCombatTelemetryEvent(MoveTemp(MarkEvent));
	}
	++MarkSerial;
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(MarkExpiryTimer);
	}
	UnbindMarkTargetHealthDelegate();
	if (AIGMarkProjectile* Projectile = ActiveMarkProjectile.Get())
	{
		Projectile->Destroy();
	}
	ClearKinsectMarkEffect();
	ActiveMarkProjectile.Reset();
	ActiveMarkHitzone.Reset();
	ActiveMarkTargetActor.Reset();
	ActiveMarkTargetASC.Reset();
	ActiveMarkBoneName = NAME_None;
	ActiveMarkID.Invalidate();
	ActiveMarkLocalPoint = FVector::ZeroVector;
	ActiveMarkLocalNormal = FVector::UpVector;
	ActiveMarkLocalTangentX = FVector::ForwardVector;
	ActiveMarkLocalTangentY = FVector::RightVector;
	ActiveMarkVisualLocalPoint = FVector::ZeroVector;
	ActiveMarkVisualLocalNormal = FVector::UpVector;
	ActiveMarkCreatedAt = 0.0;
	ActiveMarkExpireTime = 0.0;
	bActiveMarkUsesProjectile = false;
	SetMarkActiveTag(false);
}

void URes_InsectGlaive::HandleMarkTargetHealthChanged(
	const FOnAttributeChangeData& ChangeData)
{
	if (ChangeData.NewValue <= 0.0f && ActiveMarkTargetASC.IsValid()
		&& ActiveMarkHitzone.IsValid())
	{
		ClearKinsectMark(EIGMarkClearReason::TargetDead);
	}
}

void URes_InsectGlaive::UnbindMarkTargetHealthDelegate()
{
	if (UAbilitySystemComponent* TargetASC = ActiveMarkTargetASC.Get();
		TargetASC && MarkTargetHealthChangedDelegateHandle.IsValid())
	{
		TargetASC->GetGameplayAttributeValueChangeDelegate(
			UMHGZAttributeSet::GetHealthAttribute()).Remove(
				MarkTargetHealthChangedDelegateHandle);
	}
	MarkTargetHealthChangedDelegateHandle.Reset();
}

void URes_InsectGlaive::SpawnKinsectMarkEffect()
{
	ClearKinsectMarkEffect();
	UMHGZMonsterHitzoneComponent* Hitzone = ActiveMarkHitzone.Get();
	UParticleSystem* Template = CombatConfig
		? CombatConfig->KinsectMarkEstablishedEffect : nullptr;
	auto RecordEffectEvent = [this, Hitzone](const FName EventName, const FName Reason)
	{
		FWeaponCombatTelemetryEvent Event;
		Event.EventName = EventName;
		Event.MarkID = ActiveMarkID;
		Event.TargetActor = ActiveMarkTargetActor.IsValid()
			? ActiveMarkTargetActor->GetFName() : NAME_None;
		Event.Hitzone = Hitzone ? Hitzone->GetFName() : ActiveMarkBoneName;
		Event.ImpactPoint = Hitzone && IsValid(Hitzone->GetOwner())
			? Hitzone->GetComponentTransform().TransformPosition(ActiveMarkVisualLocalPoint)
			: FVector::ZeroVector;
		Event.ExpireAt = ActiveMarkExpireTime;
		Event.Reason = Reason;
		RecordCombatTelemetryEvent(MoveTemp(Event));
	};
	if (!Hitzone || !Template || !IsValid(Hitzone->GetOwner()))
	{
		RecordEffectEvent(FName(TEXT("MarkEffectSpawnFailed")),
			!Hitzone ? FName(TEXT("MissingHitzone"))
			: !Template ? FName(TEXT("MissingTemplate")) : FName(TEXT("InvalidTarget")));
		return;
	}

	UParticleSystemComponent* Effect = NewObject<UParticleSystemComponent>(
		Hitzone->GetOwner(), TEXT("IG_MarkYellowPowder"));
	if (!Effect)
	{
		RecordEffectEvent(FName(TEXT("MarkEffectSpawnFailed")),
			FName(TEXT("ComponentCreationFailed")));
		return;
	}
	Effect->bAutoDestroy = false;
	// Do not let RegisterComponent auto-activate the burst at the component's
	// default transform. The effect is attached and positioned below first.
	Effect->bAutoActivate = false;
	Effect->SetTemplate(Template);
	if (!Effect->AttachToComponent(Hitzone, FAttachmentTransformRules::KeepWorldTransform))
	{
		Effect->DestroyComponent();
		RecordEffectEvent(FName(TEXT("MarkEffectSpawnFailed")),
			FName(TEXT("AttachFailed")));
		return;
	}
	Effect->RegisterComponent();
	if (!Effect->IsRegistered())
	{
		Effect->DestroyComponent();
		RecordEffectEvent(FName(TEXT("MarkEffectSpawnFailed")),
			FName(TEXT("RegisterFailed")));
		return;
	}
	// The mark remains attached to the moving/scaled hitzone, but its visible patch
	// is authored in world centimeters and must not inherit a monster or dummy scale.
	Effect->SetAbsolute(false, false, true);
	Effect->SetWorldScale3D(FVector::OneVector);
	const FQuat HitzoneRotation = Hitzone->GetComponentQuat();
	const FVector WorldNormal = HitzoneRotation.RotateVector(
		ActiveMarkVisualLocalNormal).GetSafeNormal();
	const FVector WorldTangentX = HitzoneRotation.RotateVector(
		ActiveMarkLocalTangentX).GetSafeNormal();
	if (!WorldNormal.IsNearlyZero() && !WorldTangentX.IsNearlyZero())
	{
		Effect->SetWorldRotation(FRotationMatrix::MakeFromXZ(
			WorldTangentX, WorldNormal).ToQuat());
	}
	Effect->SetWorldLocation(Hitzone->GetComponentTransform()
		.TransformPosition(ActiveMarkVisualLocalPoint) + WorldNormal * 0.5f);
	// Hitzones are query-only collision components and some target setups hide
	// their helper components. Explicitly make the child particle component
	// renderable instead of inheriting an accidental hidden/visibility state.
	Effect->SetHiddenInGame(false, true);
	Effect->SetVisibility(true, true);
	Effect->ActivateSystem(true);
	ActiveMarkEffect = Effect;
	RecordEffectEvent(FName(TEXT("MarkEffectSpawned")), FName(TEXT("Activated")));

	// "Activated" alone cannot distinguish a visible emitter from an empty or
	// culled Cascade system. Sample after the particle component has had time to
	// tick; SetTimerForNextTick can still run before its first particle update.
	const FGuid SpawnedMarkID = ActiveMarkID;
	TWeakObjectPtr<UParticleSystemComponent> WeakEffect = Effect;
	if (UWorld* World = GetWorld())
	{
		FTimerHandle DiagnosticTimer;
		World->GetTimerManager().SetTimer(DiagnosticTimer,
			FTimerDelegate::CreateWeakLambda(this, [this, WeakEffect, SpawnedMarkID]()
			{
				UParticleSystemComponent* CurrentEffect = WeakEffect.Get();
				if (!CurrentEffect || ActiveMarkID != SpawnedMarkID
					|| ActiveMarkEffect != CurrentEffect)
				{
					return;
				}

				FWeaponCombatTelemetryEvent StateEvent;
				StateEvent.EventName = FName(TEXT("MarkEffectRuntimeState"));
				StateEvent.MarkID = SpawnedMarkID;
				StateEvent.TargetActor = ActiveMarkTargetActor.IsValid()
					? ActiveMarkTargetActor->GetFName() : NAME_None;
				StateEvent.Hitzone = ActiveMarkHitzone.IsValid()
					? ActiveMarkHitzone->GetFName() : ActiveMarkBoneName;
				StateEvent.ImpactPoint = CurrentEffect->GetComponentLocation();
				StateEvent.Reason = FName(*FString::Printf(
					TEXT("particles=%d;active=%d;visible=%d;registered=%d;template=%s"),
					CurrentEffect->GetNumActiveParticles(),
					CurrentEffect->IsActive() ? 1 : 0,
					CurrentEffect->IsVisible() ? 1 : 0,
					CurrentEffect->IsRegistered() ? 1 : 0,
					*GetPathNameSafe(CurrentEffect->Template)));
				RecordCombatTelemetryEvent(MoveTemp(StateEvent));
			}), 0.1f, false);
	}
}

void URes_InsectGlaive::ClearKinsectMarkEffect()
{
	if (ActiveMarkEffect)
	{
		ActiveMarkEffect->DestroyComponent();
		ActiveMarkEffect = nullptr;
	}
}

bool URes_InsectGlaive::HasValidKinsectMark() const
{
	if (!ActiveMarkHitzone.IsValid()
		|| !ActiveMarkHitzone->HasKinsectMarkDebuffFrom(this, ActiveMarkID)
		|| !IsValid(ActiveMarkHitzone->GetOwner())
		|| !ActiveMarkTargetActor.IsValid()
		|| ActiveMarkTargetActor.Get() != ActiveMarkHitzone->GetOwner()
		|| (bActiveMarkUsesProjectile && !ActiveMarkProjectile.IsValid()))
	{
		return false;
	}
	const UAbilitySystemComponent* TargetASC = ActiveMarkTargetASC.Get();
	const UMHGZAttributeSet* TargetAttributes = TargetASC
		? TargetASC->GetSet<UMHGZAttributeSet>() : nullptr;
	return !TargetAttributes || TargetAttributes->GetHealth() > 0.0f;
}

bool URes_InsectGlaive::GetKinsectMarkWorldLocation(FVector& OutLocation) const
{
	if (!HasValidKinsectMark())
	{
		return false;
	}
	OutLocation = ActiveMarkHitzone->GetComponentTransform()
		.TransformPosition(ActiveMarkLocalPoint);
	return true;
}

bool URes_InsectGlaive::GetKinsectMarkSurfaceFrame(
	const uint64 ExpectedMarkSerial, FVector& OutLocation, FVector& OutNormal) const
{
	if (ExpectedMarkSerial == 0 || ExpectedMarkSerial != MarkSerial
		|| !HasValidKinsectMark() || !GetWorld()
		|| GetWorld()->GetTimeSeconds() >= ActiveMarkExpireTime
		|| !ActiveMarkHitzone.IsValid())
	{
		return false;
	}

	const FTransform HitzoneTransform = ActiveMarkHitzone->GetComponentTransform();
	OutLocation = HitzoneTransform.TransformPosition(ActiveMarkLocalPoint);
	OutNormal = ActiveMarkHitzone->GetComponentQuat()
		.RotateVector(ActiveMarkLocalNormal).GetSafeNormal();
	return !OutLocation.ContainsNaN() && !OutNormal.ContainsNaN()
		&& !OutNormal.IsNearlyZero();
}

bool URes_InsectGlaive::IsHitOnKinsectMarkedPart(const FHitResult& Hit) const
{
	const UMHGZMonsterHitzoneComponent* HitHitzone =
		UMHGZMonsterHitzoneComponent::Resolve(Hit.GetComponent());
	if (!HasValidKinsectMark() || !CombatConfig || !GetWorld()
		|| GetWorld()->GetTimeSeconds() >= ActiveMarkExpireTime
		|| !Hit.bBlockingHit
		|| Hit.GetActor() != ActiveMarkTargetActor.Get()
		|| HitHitzone != ActiveMarkHitzone.Get()
		|| !FMath::IsFinite(Hit.ImpactPoint.X)
		|| !FMath::IsFinite(Hit.ImpactPoint.Y)
		|| !FMath::IsFinite(Hit.ImpactPoint.Z))
	{
		return false;
	}

	return HitHitzone->HasKinsectMarkDebuffFrom(this, ActiveMarkID);
}

void URes_InsectGlaive::SetMarkActiveTag(bool bActive)
{
	UMHGZWeaponRuntimeHostComponent* Host = RuntimeContext.RuntimeToken.Host.Get();
	if (!Host)
	{
		return;
	}
	if (bActive && !MarkActiveTagToken.IsValid())
	{
		FGameplayTagContainer Tags;
		Tags.AddTag(MarkActiveTag());
		MarkActiveTagToken = Host->AcquireTags(EWeaponTagOwnerKind::Resource,
			FGameplayAbilitySpecHandle(), 0, TEXT("IG.Mark.Active"), Tags);
	}
	else if (!bActive && MarkActiveTagToken.IsValid())
	{
		Host->ReleaseTags(MarkActiveTagToken);
		MarkActiveTagToken = FWeaponOwnedTagToken();
	}
}
