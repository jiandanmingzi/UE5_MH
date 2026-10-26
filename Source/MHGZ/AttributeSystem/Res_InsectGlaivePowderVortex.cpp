// Copyright MHGZ Project. All Rights Reserved.

// 聚爆指令、聚集表现、事务提交与分段伤害。
#include "Res_InsectGlaive.h"
#include "MHGZGameplayTags.h"
#include "MHGZMath.h"
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
#include "Components/AudioComponent.h"
#include "Engine/OverlapResult.h"
#include "TimerManager.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "InsectGlaive/IGPowderActor.h"
#include "InsectGlaive/Kinsect/Kinsect.h"
#include "InsectGlaive/Kinsect/InsectGlaiveKinsectData.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

using UE::MHGZ::IGResource::PowderWorldLifetimeSeconds;

namespace
{
bool IsDamageablePowderTarget(AActor* Target, UAbilitySystemComponent* TargetASC)
{
	if (!IsValid(Target) || !TargetASC
		|| TargetASC->HasMatchingGameplayTag(MHGZGameplayTags::Dead()))
	{
		return false;
	}
	return !TargetASC->HasAttributeSetForAttribute(
		UMHGZAttributeSet::GetHealthAttribute())
		|| TargetASC->GetNumericAttribute(
			UMHGZAttributeSet::GetHealthAttribute()) > 0.0f;
}
}

bool URes_InsectGlaive::StartPowderVortex(const FWeaponActionToken& Action,
	const FWeaponAimSnapshot& AimSnapshot, const FIGPowderVortexSettings& Settings)
{
	UMHGZWeaponRuntimeHostComponent* Host = RuntimeContext.RuntimeToken.Host.Get();
	if (bRuntimeShuttingDown || ActivePowderVortexCommandSerial != 0
		|| !Action.IsValid() || Action.RuntimeToken != RuntimeContext.RuntimeToken
		|| !IsRuntimeRequestCurrent(Action.RuntimeToken)
		|| !Host || !Host->IsActionRegistered(Action)
		|| !CombatConfig || !KinsectActor || !KinsectData
		|| KinsectActionAttachmentToken.IsValid()
		|| AimSnapshot.Context != EWeaponAimSnapshotContext::Kinsect
		|| !MHGZMath::IsFiniteVector(AimSnapshot.TargetPoint)
		|| !Settings.IsValid()
		|| !FMath::IsFinite(KinsectData->FlightSpeed)
		|| KinsectData->FlightSpeed <= 0.0f
		|| !FMath::IsFinite(KinsectData->MaxFlightRange)
		|| KinsectData->MaxFlightRange <= 0.0f
		|| !FMath::IsFinite(CombatConfig->KinsectArrivalRadius)
		|| CombatConfig->KinsectArrivalRadius <= 0.0f)
	{
		return false;
	}

	FVector TargetPoint = AimSnapshot.TargetPoint;
	const FHitResult* AimHit = AimSnapshot.bHasHitResult
		? &AimSnapshot.HitResult : nullptr;
	if (AimHit && AimHit->bBlockingHit && MHGZMath::IsFiniteVector(AimHit->ImpactPoint))
	{
		TargetPoint = AimHit->ImpactPoint;
	}
	else if (AimSnapshot.bHasBlockingHit && AimSnapshot.BlockingHit.bBlockingHit
		&& MHGZMath::IsFiniteVector(AimSnapshot.BlockingHit.ImpactPoint))
	{
		TargetPoint = AimSnapshot.BlockingHit.ImpactPoint;
	}
	const FVector Start = KinsectActor->GetActorLocation();
	const float TargetDistance = FVector::Distance(Start, TargetPoint);
	if (!MHGZMath::IsFiniteVector(TargetPoint) || TargetDistance <= KINDA_SMALL_NUMBER
		|| TargetDistance > KinsectData->MaxFlightRange + KINDA_SMALL_NUMBER)
	{
		return false;
	}

	FKinsectFlightRequest Request;
	Request.RuntimeToken = Action.RuntimeToken;
	Request.TrajectoryMode = EKinsectTrajectoryMode::ToPoint;
	Request.TargetPointSnapshot = TargetPoint;
	Request.MaxDistance = KinsectData->MaxFlightRange
		+ CombatConfig->KinsectArrivalRadius + 1.0f;
	Request.FlightSpeed = KinsectData->FlightSpeed;
	Request.ArrivalRadius = CombatConfig->KinsectArrivalRadius;
	Request.DamageMode = EKinsectDamageMode::None;
	Request.ExtractMode = EKinsectExtractMode::None;
	Request.PostFlightPolicy = EKinsectPostFlightPolicy::Hover;
	Request.FlightInstanceID = FGuid::NewGuid();
	Request.CommandSource = EKinsectCommandSource::PowderVortex;
	Request.ActionActivationSequenceID = Action.ActivationSequenceID;

	const FHitResult* FollowHit = AimHit;
	if (FollowHit && FollowHit->bBlockingHit)
	{
		if (UPrimitiveComponent* Component = FollowHit->GetComponent())
		{
			Request.TargetComponent = Component;
			if (USkeletalMeshComponent* SkeletalMesh =
				Cast<USkeletalMeshComponent>(Component);
				SkeletalMesh && !FollowHit->BoneName.IsNone())
			{
				Request.TargetBoneName = FollowHit->BoneName;
				Request.TargetLocalPoint = SkeletalMesh->GetSocketTransform(
					Request.TargetBoneName, RTS_World).InverseTransformPosition(TargetPoint);
			}
			else
			{
				Request.TargetLocalPoint = Component->GetComponentTransform()
					.InverseTransformPosition(TargetPoint);
			}
			Request.bFollowTargetComponent = true;
		}
	}
	// Any blocking point captured by the activation-time aim is a legitimate
	// A floor/wall remains an aim point, not a physical Kinsect hit. Arrival is
	// resolved only from the saved/moving point, with no terrain-stop exception.
	Request.bAllowWorldHitAtTarget = false; // legacy reflected field

	const uint64 CommandSerial = AdvanceKinsectCommand(
		EKinsectCommandSource::PowderVortex);
	Request.CommandSerial = CommandSerial;
	if (KinsectActor->GetState() == EKinsectState::Flying
		|| KinsectActor->GetState() == EKinsectState::Returning)
	{
		KinsectActor->Interrupt();
	}
	if (!KinsectActor->BeginFlight(Request))
	{
		if (KinsectCommandSerial == CommandSerial)
		{
			AdvanceKinsectCommand(EKinsectCommandSource::None);
		}
		return false;
	}

	ActivePowderVortexCommandSerial = CommandSerial;
	ActivePowderVortexReservationID = 0;
	ActivePowderVortexSettings = Settings;
	ActivePowderVortexAttackPower = GetModifiedKinsectAttackPower();
	SetKinsectActiveTag(true);
	return true;
}

bool URes_InsectGlaive::BeginPowderVortexGathering(
	const FKinsectFlightRequest& Request, const FVector& Center)
{
	if (!GetWorld() || !CombatConfig || !MHGZMath::IsFiniteVector(Center)
		|| Request.CommandSerial != ActivePowderVortexCommandSerial
		|| Request.ActionActivationSequenceID == 0
		|| !IsKinsectCommandCurrent(Request)
		|| ActivePowderVortexReservationID != 0)
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
	Token.RuntimeToken = Request.RuntimeToken;
	Token.ActivationSequenceID = Request.ActionActivationSequenceID;
	Token.ReservationID = ReservationID;
	Token.CommandSerial = Request.CommandSerial;

	const double Now = GetWorld()->GetTimeSeconds();
	const double RadiusSquared = FMath::Square(
		static_cast<double>(ActivePowderVortexSettings.GatherRadius));
	TArray<AIGPowderActor*> Candidates;
	// The attached mark is not a powder member and is never moved by the gather
	// presentation. A mark in the attraction radius is consumed directly when
	// gathering starts below; a separate MarkedContact powder can coexist there.
	for (const TWeakObjectPtr<AIGPowderActor>& WeakPowder : OwnedPowders)
	{
		AIGPowderActor* Powder = WeakPowder.Get();
		if (Powder && Powder->IsAvailableFor(this, Request.RuntimeToken, Now)
			&& FVector::DistSquared(Center, Powder->GetActorLocation()) <= RadiusSquared)
		{
			Candidates.Add(Powder);
		}
	}
	Candidates.Sort([](const AIGPowderActor& A, const AIGPowderActor& B)
	{
		return A.GetSpawnSequenceID() < B.GetSpawnSequenceID();
	});

	FReservedPowderSet Pending;
	Pending.Token = Token;
	Pending.Center = Center;
	Pending.CreatedWorldTime = Now;
	Pending.GatherDeadlineWorldTime = Now + ActivePowderVortexSettings.GatherDelay;
	Pending.bPowderVortexTransaction = true;
	Pending.bGathering = true;
	for (AIGPowderActor* Powder : Candidates)
	{
		if (!IsValid(Powder) || !Powder->TryReserve(Token))
		{
			for (const FReservedPowderMember& Member : Pending.Members)
			{
				if (AIGPowderActor* Reserved = Member.Actor.Get())
				{
					Reserved->ReleaseReservationAtCurrentLocation(Token, Now);
				}
			}
			return false;
		}
		FReservedPowderMember& Member = Pending.Members.AddDefaulted_GetRef();
		Member.Actor = Powder;
		Member.OriginalTransform = Powder->GetActorTransform();
		FWeaponCombatTelemetryEvent ReserveEvent;
		ReserveEvent.EventName = TEXT("PowderReserved");
		ReserveEvent.FlightID = Request.FlightInstanceID;
		ReserveEvent.CommandSerial = Request.CommandSerial;
		ReserveEvent.ActivationSequenceID = Request.ActionActivationSequenceID;
		ReserveEvent.PowderID = Powder->GetPowderID();
		ReserveEvent.ReservationID = ReservationID;
		ReserveEvent.ImpactPoint = Center;
		RecordCombatTelemetryEvent(MoveTemp(ReserveEvent));
		if (!Powder->RefreshLifetime(Token, Now, PowderWorldLifetimeSeconds))
		{
			for (const FReservedPowderMember& ReservedMember : Pending.Members)
			{
				if (AIGPowderActor* Reserved = ReservedMember.Actor.Get())
				{
					Reserved->ReleaseReservationAtCurrentLocation(Token, Now);
				}
			}
			return false;
		}
		FWeaponCombatTelemetryEvent RefreshEvent;
		RefreshEvent.EventName = TEXT("PowderLifetimeRefreshed");
		RefreshEvent.FlightID = Request.FlightInstanceID;
		RefreshEvent.CommandSerial = Request.CommandSerial;
		RefreshEvent.ActivationSequenceID = Request.ActionActivationSequenceID;
		RefreshEvent.PowderID = Powder->GetPowderID();
		RefreshEvent.ReservationID = ReservationID;
		RefreshEvent.ExpireAt = Powder->GetExpireWorldTime();
		RefreshEvent.LifetimeRevision = Powder->GetLifetimeRevision();
		RecordCombatTelemetryEvent(MoveTemp(RefreshEvent));
	}

	const int32 ReservedMemberCount = Pending.Members.Num();
	PowderReservations.Add(ReservationID, MoveTemp(Pending));
	ActivePowderVortexReservationID = ReservationID;
	FWeaponCombatTelemetryEvent ArrivedEvent;
	ArrivedEvent.EventName = TEXT("VortexArrived");
	ArrivedEvent.FlightID = Request.FlightInstanceID;
	ArrivedEvent.CommandSerial = Request.CommandSerial;
	ArrivedEvent.ActivationSequenceID = Request.ActionActivationSequenceID;
	ArrivedEvent.ReservationID = ReservationID;
	ArrivedEvent.ImpactPoint = Center;
	ArrivedEvent.MemberCount = ReservedMemberCount;
	ArrivedEvent.Reason = TEXT("TargetReached");
	RecordCombatTelemetryEvent(MoveTemp(ArrivedEvent));
	if (!UpdatePowderGathering(Token, 0.0f))
	{
		CancelPowderVortexTransaction(Request.CommandSerial, true);
		return false;
	}
	FVector MarkLocation = FVector::ZeroVector;
	FVector MarkNormal = FVector::UpVector;
	if (ActiveMarkID.IsValid()
		&& GetKinsectMarkSurfaceFrame(MarkSerial, MarkLocation, MarkNormal)
		&& FVector::DistSquared(Center, MarkLocation) <= RadiusSquared)
	{
		ClearKinsectMark(EIGMarkClearReason::PowderVortex);
	}
	GetWorld()->GetTimerManager().ClearTimer(PowderVortexCommitTimer);
	FTimerDelegate CommitDelegate = FTimerDelegate::CreateWeakLambda(this,
		[this, ExpectedCommandSerial = Request.CommandSerial]()
		{
			CommitPowderVortexTransaction(ExpectedCommandSerial);
		});
	GetWorld()->GetTimerManager().SetTimer(PowderVortexCommitTimer,
		CommitDelegate, ActivePowderVortexSettings.GatherDelay, false);
	StartPowderVortexGatherSounds(Center);
	return true;
}

void URes_InsectGlaive::StartPowderVortexGatherSounds(const FVector& Center)
{
	StopPowderVortexGatherSounds();
	if (!CombatConfig || !GetWorld() || !MHGZMath::IsFiniteVector(Center))
	{
		return;
	}
	for (USoundBase* Sound : ActivePowderVortexSettings.GatherSounds)
	{
		if (Sound)
		{
			if (UAudioComponent* Audio = UGameplayStatics::SpawnSoundAtLocation(this, Sound, Center))
			{
				PowderVortexGatherAudio.Add(Audio);
			}
		}
	}
}

void URes_InsectGlaive::StopPowderVortexGatherSounds()
{
	for (UAudioComponent* Audio : PowderVortexGatherAudio)
	{
		if (IsValid(Audio))
		{
			Audio->Stop();
		}
	}
	PowderVortexGatherAudio.Reset();
}

void URes_InsectGlaive::TickPowderVortexPresentation()
{
	if (ActivePowderVortexReservationID == 0 || !GetWorld())
	{
		return;
	}
	FReservedPowderSet* Reservation = PowderReservations.Find(
		ActivePowderVortexReservationID);
	if (!Reservation || !Reservation->bPowderVortexTransaction
		|| Reservation->Token.CommandSerial != ActivePowderVortexCommandSerial
		|| !IsKinsectCommandCurrent(Reservation->Token.CommandSerial,
			EKinsectCommandSource::PowderVortex, 0, Reservation->Token.RuntimeToken))
	{
		CancelPowderVortexTransaction(ActivePowderVortexCommandSerial, true);
		return;
	}
	const double Duration = FMath::Max(
		static_cast<double>(KINDA_SMALL_NUMBER),
		Reservation->GatherDeadlineWorldTime - Reservation->CreatedWorldTime);
	const float Progress = static_cast<float>(FMath::Clamp(
		(GetWorld()->GetTimeSeconds() - Reservation->CreatedWorldTime) / Duration,
		0.0, 1.0));
	if (!UpdatePowderGathering(Reservation->Token, Progress))
	{
		CancelPowderVortexTransaction(ActivePowderVortexCommandSerial, true);
	}
}

void URes_InsectGlaive::CommitPowderVortexTransaction(
	const uint64 ExpectedCommandSerial)
{
	if (bRuntimeShuttingDown || !GetWorld() || !CombatConfig
		|| ExpectedCommandSerial == 0
		|| ExpectedCommandSerial != ActivePowderVortexCommandSerial
		|| !IsKinsectCommandCurrent(ExpectedCommandSerial,
			EKinsectCommandSource::PowderVortex, 0, RuntimeContext.RuntimeToken))
	{
		CancelPowderVortexTransaction(ExpectedCommandSerial, true);
		return;
	}
	FReservedPowderSet* Found = PowderReservations.Find(
		ActivePowderVortexReservationID);
	if (!Found || !Found->bPowderVortexTransaction
		|| Found->Token.CommandSerial != ExpectedCommandSerial)
	{
		CancelPowderVortexTransaction(ExpectedCommandSerial, true);
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now + UE_SMALL_NUMBER < Found->GatherDeadlineWorldTime)
	{
		const float Remaining = static_cast<float>(
			Found->GatherDeadlineWorldTime - Now);
		FTimerDelegate CommitDelegate = FTimerDelegate::CreateWeakLambda(this,
			[this, ExpectedCommandSerial]()
			{
				CommitPowderVortexTransaction(ExpectedCommandSerial);
			});
		GetWorld()->GetTimerManager().SetTimer(PowderVortexCommitTimer,
			CommitDelegate, FMath::Max(Remaining, KINDA_SMALL_NUMBER), false);
		return;
	}
	GetWorld()->GetTimerManager().ClearTimer(PowderVortexCommitTimer);

	const FReservedPowderSet Reservation = *Found;
	StopPowderVortexGatherSounds();
	TArray<AIGPowderActor*> LiveMembers;
	double Stage2Damage = 0.0;
	for (const FReservedPowderMember& Member : Reservation.Members)
	{
		AIGPowderActor* Powder = Member.Actor.Get();
		if (Powder && Powder->CanCommitReservation(Reservation.Token, Now))
		{
			LiveMembers.Add(Powder);
			// Sum the damage of individually exploding powders, never previously
			// rounded float ratios. Every member keeps its own generation-time AP/MV.
			Stage2Damage += MHGZDamageMath::PowderDamage(
				Powder->GetKinsectAttackPowerSnapshot(), Powder->GetMotionValueSnapshot());
		}
		else if (Powder)
		{
			Powder->ReleaseReservationAtCurrentLocation(Reservation.Token, Now);
		}
	}

	PowderReservations.Remove(Reservation.Token.ReservationID);
	ActivePowderVortexReservationID = 0;
	FIGPowderExplosionSnapshot Explosion;
	Explosion.Center = Reservation.Center;
	Explosion.ConsumedCount = LiveMembers.Num();
	Explosion.Radius = ActivePowderVortexSettings.ExplosionRadius;
	Explosion.BaseStagger = 0.0f;
	if (!LiveMembers.IsEmpty())
	{
		const int32 Count = LiveMembers.Num();
		Explosion.Stage1AttackPower = ActivePowderVortexAttackPower;
		Explosion.Stage1MotionValue = Count < 5 ? ActivePowderVortexSettings.MotionValueAt1
			: (Count < 10 ? ActivePowderVortexSettings.MotionValueAt5 : ActivePowderVortexSettings.MotionValueAt10);
		Explosion.Stage1Damage = MHGZDamageMath::PowderDamage(Explosion.Stage1AttackPower, Explosion.Stage1MotionValue);
		Explosion.Stage2Damage = static_cast<float>(Stage2Damage);
		Explosion.Stage1AttackID = FGuid::NewGuid();
		Explosion.Stage2AttackID = FGuid::NewGuid();
		Explosion.ExplosionID = Explosion.Stage1AttackID;
		Explosion.TotalDamage = Explosion.Stage1Damage + Explosion.Stage2Damage;
	}

	for (AIGPowderActor* Powder : LiveMembers)
	{
		Powder->MarkConsumed(Reservation.Token);
		FWeaponCombatTelemetryEvent ConsumeEvent;
		ConsumeEvent.EventName = TEXT("PowderConsumed");
		ConsumeEvent.CommandSerial = ExpectedCommandSerial;
		ConsumeEvent.ActivationSequenceID = Reservation.Token.ActivationSequenceID;
		ConsumeEvent.PowderID = Powder->GetPowderID();
		ConsumeEvent.ReservationID = Reservation.Token.ReservationID;
		ConsumeEvent.ExplosionID = Explosion.ExplosionID;
		ConsumeEvent.StageAttackID = Explosion.Stage2AttackID;
		ConsumeEvent.ImpactPoint = Explosion.Center;
		ConsumeEvent.MemberCount = LiveMembers.Num();
		ConsumeEvent.Damage = Powder->GetDamageContribution();
		RecordCombatTelemetryEvent(MoveTemp(ConsumeEvent));
		Powder->Destroy();
	}
	FWeaponCombatTelemetryEvent CommitEvent;
	CommitEvent.EventName = TEXT("VortexCommitted");
	CommitEvent.CommandSerial = ExpectedCommandSerial;
	CommitEvent.ActivationSequenceID = Reservation.Token.ActivationSequenceID;
	CommitEvent.ReservationID = Reservation.Token.ReservationID;
	CommitEvent.ExplosionID = Explosion.ExplosionID;
	CommitEvent.StageAttackID = Explosion.Stage1AttackID;
	CommitEvent.ImpactPoint = Explosion.Center;
	CommitEvent.MemberCount = Explosion.ConsumedCount;
	CommitEvent.Damage = Explosion.TotalDamage;
	RecordCombatTelemetryEvent(MoveTemp(CommitEvent));
	const FIGPowderVortexSettings ExplosionSettings = ActivePowderVortexSettings;
	ApplyPowderVortexExplosion(Explosion, ExplosionSettings);

	if (ActivePowderVortexCommandSerial == ExpectedCommandSerial)
	{
		ActivePowderVortexCommandSerial = 0;
		ActivePowderVortexSettings = FIGPowderVortexSettings();
		ActivePowderVortexAttackPower = 0.f;
	}
	if (KinsectCommandSerial == ExpectedCommandSerial
		&& KinsectCommandSource == EKinsectCommandSource::PowderVortex)
	{
		AdvanceKinsectCommand(EKinsectCommandSource::None);
		EarliestAutoMarkUpdateFrame = GFrameCounter + 1;
	}
}

void URes_InsectGlaive::CancelPowderVortexTransaction(
	const uint64 ExpectedCommandSerial, const bool bReleaseCommand,
	const FName CancellationReason)
{
	if (ExpectedCommandSerial != 0 && ActivePowderVortexCommandSerial != 0
		&& ExpectedCommandSerial != ActivePowderVortexCommandSerial)
	{
		return;
	}
	const uint64 CancelledSerial = ActivePowderVortexCommandSerial;
	StopPowderVortexGatherSounds();
	const uint64 CancelledReservationID = ActivePowderVortexReservationID;
	if (CancelledSerial != 0 || CancelledReservationID != 0)
	{
		FWeaponCombatTelemetryEvent Event;
		Event.EventName = TEXT("VortexCancelled");
		Event.CommandSerial = CancelledSerial;
		Event.ReservationID = CancelledReservationID;
		Event.Reason = !CancellationReason.IsNone() ? CancellationReason
			: (bReleaseCommand ? FName(TEXT("InputOrCommandSuperseded"))
				: FName(TEXT("RuntimeTransition")));
		if (const FReservedPowderSet* Reservation = PowderReservations.Find(
			CancelledReservationID))
		{
			Event.ActivationSequenceID = Reservation->Token.ActivationSequenceID;
			Event.MemberCount = Reservation->Members.Num();
			Event.ImpactPoint = Reservation->Center;
		}
		RecordCombatTelemetryEvent(MoveTemp(Event));
	}
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(PowderVortexCommitTimer);
	}
	if (ActivePowderVortexReservationID != 0)
	{
		if (FReservedPowderSet* Found = PowderReservations.Find(
			ActivePowderVortexReservationID))
		{
			const FReservedPowderSet Reservation = *Found;
			PowderReservations.Remove(Reservation.Token.ReservationID);
			const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
			for (const FReservedPowderMember& Member : Reservation.Members)
			{
				if (AIGPowderActor* Powder = Member.Actor.Get())
				{
					FWeaponCombatTelemetryEvent ReleaseEvent;
					ReleaseEvent.EventName = TEXT("PowderReleased");
					ReleaseEvent.CommandSerial = Reservation.Token.CommandSerial;
					ReleaseEvent.ActivationSequenceID = Reservation.Token.ActivationSequenceID;
					ReleaseEvent.PowderID = Powder->GetPowderID();
					ReleaseEvent.ReservationID = Reservation.Token.ReservationID;
					ReleaseEvent.LifetimeRevision = Powder->GetLifetimeRevision();
					RecordCombatTelemetryEvent(MoveTemp(ReleaseEvent));
					Powder->ReleaseReservationAtCurrentLocation(Reservation.Token, Now);
				}
			}
		}
	}
	ActivePowderVortexReservationID = 0;
	ActivePowderVortexCommandSerial = 0;
	ActivePowderVortexSettings = FIGPowderVortexSettings();
	ActivePowderVortexAttackPower = 0.f;
	if (bReleaseCommand && CancelledSerial != 0
		&& KinsectCommandSerial == CancelledSerial
		&& KinsectCommandSource == EKinsectCommandSource::PowderVortex)
	{
		AdvanceKinsectCommand(EKinsectCommandSource::None);
		EarliestAutoMarkUpdateFrame = GFrameCounter + 1;
	}
}

void URes_InsectGlaive::ApplyPowderVortexExplosion(
	const FIGPowderExplosionSnapshot& Explosion, const FIGPowderVortexSettings& Settings)
{
	if (!MHGZMath::IsFiniteVector(Explosion.Center))
	{
		return;
	}
	UAbilitySystemComponent* SourceASC = GetPlayerASC();
	AActor* SourceActor = GetOwner();
	if (!SourceASC || !SourceActor || !CombatConfig)
	{
		return;
	}
	FGameplayCueParameters CueParameters;
	CueParameters.Location = Explosion.Center;
	CueParameters.RawMagnitude = Explosion.TotalDamage;
	CueParameters.Instigator = SourceActor;
	CueParameters.EffectCauser = SourceActor;
	CueParameters.EffectContext = SourceASC->MakeEffectContext();
	SourceASC->ExecuteGameplayCue(Settings.ExplosionCueTag,
		CueParameters);
	if (Explosion.ConsumedCount <= 0)
	{
		return;
	}
	// A single spatial burst per transaction, not one per powder, target or stage.
	for (USoundBase* Sound : { CombatConfig->PowderExplosionSound.Get(),
		Settings.ExplosionSound.Get() })
	{
		if (Sound)
		{
			UGameplayStatics::SpawnSoundAtLocation(this, Sound, Explosion.Center);
		}
	}
	ApplyPowderVortexDamageStage(Explosion, Explosion.Stage1AttackID,
		Explosion.Stage1Damage, Settings.HitCueTag);
	ApplyPowderVortexDamageStage(Explosion, Explosion.Stage2AttackID,
		Explosion.Stage2Damage, Settings.HitCueTag);
}

void URes_InsectGlaive::ApplyPowderVortexDamageStage(
	const FIGPowderExplosionSnapshot& Explosion, const FGuid& StageAttackID,
	const float Damage, const FGameplayTag HitCueTag)
{
	if (!StageAttackID.IsValid() || !FMath::IsFinite(Damage) || Damage <= 0.0f
		|| !GetWorld() || !CombatConfig)
	{
		return;
	}
	UAbilitySystemComponent* SourceASC = GetPlayerASC();
	AActor* SourceActor = GetOwner();
	if (!SourceASC || !SourceActor)
	{
		return;
	}
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(IGPowderVortexStage), false,
		SourceActor);
	QueryParams.AddIgnoredActor(SourceActor);
	TArray<FOverlapResult> Overlaps;
	GetWorld()->OverlapMultiByChannel(Overlaps, Explosion.Center,
		FQuat::Identity, ECC_GameTraceChannel1,
		FCollisionShape::MakeSphere(Explosion.Radius), QueryParams);

	TMap<TWeakObjectPtr<AActor>, FHitResult> NearestHitByActor;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		UMHGZMonsterHitzoneComponent* Hitzone =
			UMHGZMonsterHitzoneComponent::Resolve(Overlap.GetComponent());
		AActor* Target = Hitzone ? Hitzone->GetOwner() : nullptr;
		if (!IsValid(Target) || !IsValid(Hitzone) || !Hitzone->IsRegistered()
			|| Target == SourceActor)
		{
			continue;
		}

		FVector ClosestPoint = Hitzone->GetComponentLocation();
		const float ClosestDistance = Hitzone->GetClosestPointOnQueryCollision(
			Explosion.Center, ClosestPoint);
		if (ClosestDistance < 0.0f || !MHGZMath::IsFiniteVector(ClosestPoint))
		{
			ClosestPoint = Hitzone->GetComponentLocation();
		}
		const double DistanceSquared = FVector::DistSquared(
			Explosion.Center, ClosestPoint);
		const TWeakObjectPtr<AActor> Key(Target);
		FHitResult* Existing = NearestHitByActor.Find(Key);
		if (!Existing || DistanceSquared < FVector::DistSquared(
			Explosion.Center, Existing->ImpactPoint))
		{
			FHitResult Hit(Target, Hitzone->GetQueryCollisionComponent(), ClosestPoint,
				(Explosion.Center - ClosestPoint).GetSafeNormal());
			Hit.bBlockingHit = true;
			Hit.TraceStart = Explosion.Center;
			Hit.TraceEnd = ClosestPoint;
			Hit.Location = ClosestPoint;
			Hit.ImpactPoint = ClosestPoint;
			Hit.ImpactNormal = Hit.Normal;
			NearestHitByActor.Add(Key, MoveTemp(Hit));
		}
	}

	TArray<FHitResult> UniqueHits;
	NearestHitByActor.GenerateValueArray(UniqueHits);
	for (const FHitResult& Hit : UniqueHits)
	{
		AActor* Target = Hit.GetActor();
		UAbilitySystemComponent* TargetASC = Target
			? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target) : nullptr;
		UMHGZMonsterHitzoneComponent* Hitzone =
			UMHGZMonsterHitzoneComponent::Resolve(Hit.GetComponent());
		if (!Hitzone || !IsDamageablePowderTarget(Target, TargetASC))
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
		// The ASC is owned by PlayerState, while the Character is the physical causer.
		// Keep those identities separate so GAS can resolve the source ASC from context.
		Context->AddInstigator(SourceASC->GetOwnerActor(), SourceActor);
		Context->AddHitResult(Hit, true);
		Context->AttackInstanceID = StageAttackID;
		Context->SourceActionTag = FGameplayTag::RequestGameplayTag(
			TEXT("Input.Weapon.LTYB"));
		Context->HitzoneTag = Hitzone->HitzoneTag;
		Context->DamageSourceType = EMHGZDamageSourceType::Powder;
		Context->bUseHitzoneDefense = false;
		Context->HitCueTag = HitCueTag;

		FGameplayEffectSpecHandle Spec = SourceASC->MakeOutgoingSpec(
			UMHGZDamageGameplayEffect::StaticClass(), 1.0f, ContextHandle);
		if (!Spec.IsValid())
		{
			continue;
		}
		Spec.Data->SetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag(
			TEXT("Damage.AttackPower")), StageAttackID == Explosion.Stage1AttackID
				? Explosion.Stage1AttackPower : Damage);
		// Stage 1 retains its actual GA motion value and AP snapshot. Stage 2
		// adapts the sum of individually rounded powder damage with MV=100.
		Spec.Data->SetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag(
			TEXT("Damage.MotionValue")), StageAttackID == Explosion.Stage1AttackID
				? Explosion.Stage1MotionValue : 100.0f);
		Spec.Data->SetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag(
			TEXT("Damage.BaseStagger")), 0.0f);
		Spec.Data->SetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag(
			TEXT("Damage.CritOverride")), 0.0f);
		Spec.Data->SetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag(
			TEXT("Damage.DanceMultiplier")), 1.0f);
		if (Hitzone->HitzoneTag.IsValid())
		{
			Spec.Data->AddDynamicAssetTag(Hitzone->HitzoneTag);
		}
		Spec.Data->AddDynamicAssetTag(FGameplayTag::RequestGameplayTag(
			TEXT("GameplayCue.Hit.DamageNumber")));
		const FActiveGameplayEffectHandle AppliedHandle =
			SourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data, TargetASC);
		if (AppliedHandle.IsValid())
		{
			FWeaponCombatTelemetryEvent Event;
			Event.EventName = TEXT("StageDamageSubmitted");
			Event.ExplosionID = Explosion.ExplosionID;
			Event.StageIndex = StageAttackID == Explosion.Stage1AttackID ? 1 : 2;
			Event.StageAttackID = StageAttackID;
			Event.TargetActor = Target->GetFName();
			Event.Hitzone = Hitzone->GetFName();
			Event.ImpactPoint = Hit.ImpactPoint;
			Event.MemberCount = Explosion.ConsumedCount;
			Event.Damage = Damage;
			RecordCombatTelemetryEvent(MoveTemp(Event));
		}
	}
}
