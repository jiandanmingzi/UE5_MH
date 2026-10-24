// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "AttributeSystem/MHGZAttributeSet.h"
#include "AttributeSystem/Res_InsectGlaive.h"
#include "CollisionQueryParams.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/BoxComponent.h"
#include "InsectGlaive/Kinsect/KinsectCollisionComponent.h"
#include "InsectGlaive/IGPowderActor.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"
#include "../../Shared/MHGZM3TestHarness.h"

namespace
{
struct FMHGZM632Harness : FMHGZM3Harness
{
	bool Setup()
	{
		return FMHGZM3Harness::Setup(false, AMHGZM3TestCharacter::StaticClass(), true);
	}
};

struct FScopedM632FrameCounter
{
	uint64 InitialValue = GFrameCounter;
	~FScopedM632FrameCounter() { GFrameCounter = InitialValue; }
};

struct FMHGZM632MarkTarget
{
	AActor* Actor = nullptr;
	UMHGZMonsterHitzoneComponent* Hitzone = nullptr;
	UAbilitySystemComponent* ASC = nullptr;
	UMHGZAttributeSet* Attributes = nullptr;
};

FMHGZM632MarkTarget SpawnM632MarkTarget(UWorld& World,
	const FVector& Center, const bool bWithHealth)
{
	FMHGZM632MarkTarget Target;
	Target.Actor = World.SpawnActor<AActor>(AActor::StaticClass(), Center,
		FRotator::ZeroRotator);
	if (!Target.Actor)
	{
		return Target;
	}
	Target.Hitzone = NewObject<UMHGZMonsterHitzoneComponent>(Target.Actor,
		NAME_None, RF_Transient);
	if (!Target.Hitzone)
	{
		return Target;
	}
	Target.Hitzone->Radius = 30.0f;
	Target.Hitzone->BoneName = TEXT("m632_mark_surface");
	Target.Actor->AddInstanceComponent(Target.Hitzone);
	Target.Actor->SetRootComponent(Target.Hitzone);
	Target.Hitzone->RegisterComponent();
	Target.Hitzone->InitSphereRadius(30.0f);
	Target.Hitzone->SetWorldLocation(Center);

	if (bWithHealth)
	{
		Target.ASC = NewObject<UAbilitySystemComponent>(Target.Actor);
		Target.Attributes = NewObject<UMHGZAttributeSet>(Target.Actor);
		if (!Target.ASC || !Target.Attributes)
		{
			return Target;
		}
		Target.Actor->AddInstanceComponent(Target.ASC);
		Target.ASC->RegisterComponent();
		Target.ASC->AddAttributeSetSubobject(Target.Attributes);
		Target.ASC->InitAbilityActorInfo(Target.Actor, Target.Actor);
		Target.Attributes->InitMaxHealth(100.0f);
		Target.Attributes->InitHealth(100.0f);
	}
	return Target;
}

FHitResult MakeM632MarkHit(const FMHGZM632MarkTarget& Target,
	const FVector& ImpactPoint)
{
	FHitResult Hit(Target.Actor, Target.Hitzone, ImpactPoint, FVector::UpVector);
	Hit.bBlockingHit = true;
	Hit.Location = ImpactPoint;
	Hit.ImpactPoint = ImpactPoint;
	Hit.ImpactNormal = FVector::UpVector;
	return Hit;
}

FKinsectFlightRequest MakeM632PlayerHoverRequest(const FMHGZM3Harness& Harness)
{
	FKinsectFlightRequest Request;
	Request.RuntimeToken = Harness.Host->GetCurrentToken();
	Request.TrajectoryMode = EKinsectTrajectoryMode::AlongDirection;
	Request.DirectionSnapshot = FVector::ForwardVector;
	Request.MaxDistance = 1000.0f;
	Request.FlightSpeed = Harness.KinsectData->FlightSpeed;
	Request.ArrivalRadius = 50.0f;
	Request.DamageMode = EKinsectDamageMode::None;
	Request.ExtractMode = EKinsectExtractMode::None;
	Request.PostFlightPolicy = EKinsectPostFlightPolicy::Hover;
	Request.FlightInstanceID = FGuid::NewGuid();
	return Request;
}

bool TickM632World(FMHGZM3Harness& Harness, const int32 Frames,
	const float DeltaSeconds)
{
	for (int32 Frame = 0; Frame < Frames; ++Frame)
	{
		// Manually ticked test worlds do not advance the engine frame counter.
		++GFrameCounter;
		Harness.World->Tick(ELevelTick::LEVELTICK_All, DeltaSeconds);
	}
	return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM632AutoMarkOnlyRunsDetachedAndUsesRealHit,
	"MHGZ.M6.Kinsect.AutoMarkDetachedSeekAndRealHit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM632AutoMarkOnlyRunsDetachedAndUsesRealHit::RunTest(
	const FString& Parameters)
{
	FScopedM632FrameCounter FrameCounterGuard;
	FMHGZM632Harness Harness;
	if (!Harness.Setup())
	{
		AddError(TEXT("M3 runtime harness setup failed"));
		return false;
	}

	TestEqual(TEXT("auto mark search radius defaults to half the arena side"),
		Harness.CombatConfig->KinsectAutoMarkSearchRadius, 3000.0f);
	TestEqual(TEXT("auto mark attack interval defaults to two seconds"),
		Harness.CombatConfig->KinsectAutoMarkAttackInterval, 2.0f);
	Harness.CombatConfig->KinsectAutoMarkSearchRadius = 1200.0f;
	Harness.CombatConfig->KinsectAutoMarkAttackInterval = 0.15f;
	Harness.CombatConfig->PowderActorClass = AIGPowderActor::StaticClass();
	Harness.CombatConfig->PowderMotionValue = 50.0f;
	Harness.CombatConfig->MarkedHitPowderCooldown = 10.0f;
	FMHGZM632MarkTarget Target = SpawnM632MarkTarget(*Harness.World,
		FVector(300.0f, 0.0f, 0.0f), true);
	if (!TestNotNull(TEXT("marked target exists"), Target.Hitzone)
		|| !TestNotNull(TEXT("marked target has health"), Target.Attributes))
	{
		Harness.Teardown();
		return false;
	}

	const FVector MarkPoint = Target.Hitzone->GetComponentLocation()
		+ FVector::UpVector * 30.0f;
	TestTrue(TEXT("valid local mark is established"),
		Harness.Resource->SetKinsectMarkFromMeleeHit(
			MakeM632MarkHit(Target, MarkPoint)));
	Harness.Resource->TickComponent(1.0f / 60.0f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("an attached Kinsect never deploys itself for a mark"),
		Harness.Kinsect->GetState(), EKinsectState::Attached);

	TestTrue(TEXT("a player request can deploy the Kinsect"),
		Harness.Resource->DeployKinsect(MakeM632PlayerHoverRequest(Harness)));
	Harness.Kinsect->Interrupt();
	TestEqual(TEXT("player request leaves the Kinsect detached and hovering"),
		Harness.Kinsect->GetState(), EKinsectState::Hovering);
	TickM632World(Harness, 1, 1.0f / 60.0f);
	TestEqual(TEXT("an eligible detached Kinsect starts seeking the mark"),
		Harness.Kinsect->ActiveRequest.CommandSource,
		EKinsectCommandSource::AutoMark);
	Target.Actor->AddActorWorldOffset(FVector(0.0f, 160.0f, 0.0f));
	TickM632World(Harness, 1, 1.0f / 60.0f);
	FVector MovedMarkLocation = FVector::ZeroVector;
	TestTrue(TEXT("moving target retains its local mark"),
		Harness.Resource->GetKinsectMarkWorldLocation(MovedMarkLocation));
	const FVector ExpectedApproachPoint = MovedMarkLocation
		+ FVector::UpVector
			* (Harness.Kinsect->Collision->GetUnscaledCapsuleHalfHeight() + 2.0f);
	TestTrue(TEXT("in-flight automatic request follows the moved mark"),
		Harness.Kinsect->ActiveRequest.TargetPointSnapshot.Equals(
			ExpectedApproachPoint, 1.0f));
	const float HealthBefore = Target.Attributes->GetHealth();
	for (int32 Frame = 0; Frame < 180
		&& Target.Attributes->GetHealth() >= HealthBefore; ++Frame)
	{
		TickM632World(Harness, 1, 1.0f / 60.0f);
	}
	TestTrue(TEXT("automatic seek reaches the marked Hitzone and applies real Kinsect damage"),
		Target.Attributes->GetHealth() < HealthBefore);
	const auto FirstPowders = Harness.Resource->GetPowderSnapshots();
	TestEqual(TEXT("real automatic contact creates one powder"), FirstPowders.Num(), 1);
	if (!FirstPowders.IsEmpty())
	{
		TestEqual(TEXT("production auto contact uses its independent policy"),
			FirstPowders[0].SpawnPolicy, EIGPowderSpawnPolicy::AutoMarkContact);
	}
	TickM632World(Harness, 12, 1.0f / 60.0f);
	TestTrue(TEXT("another automatic attack creates powder well inside the ordinary ten-second cooldown"),
		Harness.Resource->GetPowderSnapshots().Num() >= 2);
	TestTrue(TEXT("target ASC owns the exact health set observed by the test"),
		Target.ASC->GetSet<UMHGZAttributeSet>() == Target.Attributes);
	TestEqual(TEXT("automatic attack remains a detached hovering behavior"),
		Harness.Kinsect->GetState(), EKinsectState::Hovering);
	TestEqual(TEXT("the committed automatic request carries its source identity"),
		Harness.Kinsect->ActiveRequest.CommandSource,
		EKinsectCommandSource::AutoMark);
	TestTrue(TEXT("the active automatic request is current for this Runtime and mark"),
		Harness.Resource->IsKinsectCommandCurrent(Harness.Kinsect->ActiveRequest));
	FVector MarkNormal;
	TestTrue(TEXT("marked surface frame remains valid"),
		Harness.Resource->GetKinsectMarkSurfaceFrame(
			Harness.Kinsect->ActiveRequest.MarkSerial, MovedMarkLocation, MarkNormal));
	const FVector ToMark = (MovedMarkLocation - Harness.Kinsect->GetActorLocation()).GetSafeNormal();
	TestTrue(TEXT("arrival faces the actual mark instead of the last travel heading"),
		FVector::DotProduct(Harness.Kinsect->GetAttackForwardVector(), ToMark) > 0.999);
	FHitResult Contact;
	const FKinsectFlightRequest AutoRequest = Harness.Kinsect->ActiveRequest;
	Harness.Kinsect->Mesh->AddWorldRotation(FRotator(0, 0, 180));
	TestFalse(TEXT("the mark proxy cannot damage through the Kinsect's back"),
		Harness.Kinsect->SweepAutomaticMarkContact(AutoRequest.CommandSerial,
			AutoRequest.MarkSerial, AutoRequest.RuntimeToken, Contact));
	TestFalse(TEXT("stale mark cannot change facing"),
		Harness.Kinsect->FaceAutomaticMark(AutoRequest.CommandSerial, AutoRequest.MarkSerial + 1));
	TickM632World(Harness, 1, 1.0f / 60.0f);
	TestTrue(TEXT("hover refresh restores facing before the next attack interval"),
		FVector::DotProduct(Harness.Kinsect->GetAttackForwardVector(), ToMark) > 0.999);
	TestTrue(TEXT("front nearby marked surface still produces a real contact"),
		Harness.Kinsect->SweepAutomaticMarkContact(AutoRequest.CommandSerial,
			AutoRequest.MarkSerial, AutoRequest.RuntimeToken, Contact));
	Harness.Kinsect->SetActorLocation(Harness.Kinsect->GetActorLocation() - ToMark * 200);
	TestFalse(TEXT("mark-centered proxy ray cannot damage from afar"),
		Harness.Kinsect->SweepAutomaticMarkContact(AutoRequest.CommandSerial,
			AutoRequest.MarkSerial, AutoRequest.RuntimeToken, Contact));

	Harness.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZKinsectLowMarkFrontContact,
	"MHGZ.Maintenance.Kinsect.LowMarkFloorClearanceAndImmediateFacing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZKinsectLowMarkFrontContact::RunTest(const FString&)
{
	FScopedM632FrameCounter FrameCounterGuard;
	FMHGZM632Harness H;
	if (!H.Setup()) { return false; }
	H.CombatConfig->KinsectAutoMarkAttackInterval = 0.1f;
	AActor* Floor = H.World->SpawnActor<AActor>();
	UBoxComponent* Box = NewObject<UBoxComponent>(Floor);
	Floor->SetRootComponent(Box); Floor->AddInstanceComponent(Box);
	Box->SetBoxExtent(FVector(1500, 1500, 50));
	Box->SetCollisionObjectType(ECC_WorldStatic);
	Box->SetCollisionResponseToAllChannels(ECR_Block);
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Box->RegisterComponent();
	Box->SetWorldLocation(FVector(0, 0, -50));
	const FMHGZM632MarkTarget Target = SpawnM632MarkTarget(*H.World, FVector(300, 0, 0), true);
	if (!TestNotNull(TEXT("low marked target exists"), Target.Attributes))
	{ H.Teardown(); return false; }
	const FVector MarkPoint(270, 0, 0);
	FHitResult MarkHit = MakeM632MarkHit(Target, MarkPoint);
	MarkHit.ImpactNormal = -FVector::ForwardVector;
	TestTrue(TEXT("low side mark is established"), H.Resource->SetKinsectMarkFromMeleeHit(MarkHit));
	TestTrue(TEXT("player deploys before automation"), H.Resource->DeployKinsect(MakeM632PlayerHoverRequest(H)));
	H.Kinsect->Interrupt();
	// Same floor-safe approach produced by ResolveAutomaticMarkApproachPoint:
	// 32cm surface-normal standoff, raised to 34cm by the ground sweep.
	H.Kinsect->SetActorLocation(FVector(238, 0, 34));
	TickM632World(H, 1, 1.0f / 60.0f);
	TestEqual(TEXT("already at the approach uses contact, not a new flight"),
		H.Kinsect->GetState(), EKinsectState::Hovering);
	TestEqual(TEXT("contact is owned by the current auto-mark request"),
		H.Kinsect->ActiveRequest.CommandSource, EKinsectCommandSource::AutoMark);
	TestTrue(TEXT("immediate contact faces the low mark in 3D"),
		FVector::DotProduct(H.Kinsect->GetAttackForwardVector(),
			(MarkPoint - H.Kinsect->GetActorLocation()).GetSafeNormal()) > 0.999);
	const float Health = Target.Attributes->GetHealth();
	TickM632World(H, 20, 1.0f / 60.0f);
	TestTrue(TEXT("floor clearance does not disable a valid front contact"),
		Target.Attributes->GetHealth() < Health);
	TestTrue(TEXT("facing downward never tilts the physical capsule into the ground"),
		H.Kinsect->GetActorUpVector().Equals(FVector::UpVector, 0.001));
	H.Teardown(); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM632PlayerLeasePreemptsAutoAndRejectsStaleMark,
	"MHGZ.M6.Kinsect.PlayerLeasePreemptsAutoAndRejectsStaleMark",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM632PlayerLeasePreemptsAutoAndRejectsStaleMark::RunTest(
	const FString& Parameters)
{
	FScopedM632FrameCounter FrameCounterGuard;
	FMHGZM632Harness Harness;
	if (!Harness.Setup())
	{
		AddError(TEXT("M3 runtime harness setup failed"));
		return false;
	}

	Harness.CombatConfig->KinsectAutoMarkSearchRadius = 1500.0f;
	Harness.CombatConfig->KinsectAutoMarkAttackInterval = 0.2f;
	FMHGZM632MarkTarget First = SpawnM632MarkTarget(*Harness.World,
		FVector(700.0f, 0.0f, 0.0f), false);
	FMHGZM632MarkTarget Replacement = SpawnM632MarkTarget(*Harness.World,
		FVector(900.0f, 0.0f, 0.0f), false);
	if (!TestNotNull(TEXT("first marked Hitzone exists"), First.Hitzone)
		|| !TestNotNull(TEXT("replacement Hitzone exists"), Replacement.Hitzone))
	{
		Harness.Teardown();
		return false;
	}
	auto SetMark = [&Harness](const FMHGZM632MarkTarget& Target)
	{
		const FVector Point = Target.Hitzone->GetComponentLocation()
			+ FVector::UpVector * 30.0f;
		return Harness.Resource->SetKinsectMarkFromMeleeHit(
			MakeM632MarkHit(Target, Point));
	};
	TestTrue(TEXT("initial mark is established"), SetMark(First));
	TestTrue(TEXT("player deployment succeeds"),
		Harness.Resource->DeployKinsect(MakeM632PlayerHoverRequest(Harness)));
	Harness.Kinsect->Interrupt();
	// Advance the held global frame once so the post-player handoff gate expires.
	TickM632World(Harness, 1, 1.0f / 60.0f);
	TestEqual(TEXT("valid mark starts an automatic approach from Hovering"),
		Harness.Kinsect->GetState(), EKinsectState::Flying);
	const FKinsectFlightRequest OldAutoRequest = Harness.Kinsect->ActiveRequest;
	TestEqual(TEXT("the approach is owned by AutoMark"), OldAutoRequest.CommandSource,
		EKinsectCommandSource::AutoMark);
	TestTrue(TEXT("automatic approach identity is initially current"),
		Harness.Resource->IsKinsectCommandCurrent(OldAutoRequest));
	FWeaponRuntimeToken StaleRuntimeToken = OldAutoRequest.RuntimeToken;
	++StaleRuntimeToken.Generation;
	TestFalse(TEXT("an old RuntimeToken cannot authorize an automatic callback"),
		Harness.Resource->IsKinsectCommandCurrent(OldAutoRequest.CommandSerial,
			EKinsectCommandSource::AutoMark, OldAutoRequest.MarkSerial,
			StaleRuntimeToken));

	FWeaponActionToken UnregisteredAction = Harness.MakeActionToken(
		UMHGZSendKinsectAbility::StaticClass());
	TestFalse(TEXT("an unregistered/failed player action cannot preempt automation"),
		Harness.Resource->BeginKinsectPlayerCommandLease(UnregisteredAction));
	TestEqual(TEXT("failed preflight leaves the automatic flight running"),
		Harness.Kinsect->GetState(), EKinsectState::Flying);
	TestTrue(TEXT("failed preflight keeps the current command serial"),
		Harness.Resource->IsKinsectCommandCurrent(OldAutoRequest));

	FWeaponActionToken PlayerAction = Harness.MakeActionToken(
		UMHGZSendKinsectAbility::StaticClass());
	TestTrue(TEXT("the valid player action registers"),
		Harness.Host->RegisterAction(PlayerAction));
	TestTrue(TEXT("a committed player Montage lease acquires priority"),
		Harness.Resource->BeginKinsectPlayerCommandLease(PlayerAction));
	TestEqual(TEXT("player lease interrupts an automatic approach into Hovering"),
		Harness.Kinsect->GetState(), EKinsectState::Hovering);
	TestFalse(TEXT("the superseded automatic request is now stale"),
		Harness.Resource->IsKinsectCommandCurrent(OldAutoRequest));

	TestTrue(TEXT("new successful mark replaces the old mark identity"),
		SetMark(Replacement));
	TestFalse(TEXT("old mark serial cannot authorize an automatic callback"),
		Harness.Resource->IsKinsectCommandCurrent(OldAutoRequest.CommandSerial,
			EKinsectCommandSource::AutoMark, OldAutoRequest.MarkSerial,
			OldAutoRequest.RuntimeToken));
	Harness.Resource->EndKinsectPlayerCommandLease(PlayerAction);
	TestEqual(TEXT("ending the player lease does not synchronously restart automation"),
		Harness.Kinsect->GetState(), EKinsectState::Hovering);

	Harness.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM632ReturnedKinsectDoesNotAutoRedeploy,
	"MHGZ.M6.Kinsect.ReturnedKinsectDoesNotAutoRedeploy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM632ReturnedKinsectDoesNotAutoRedeploy::RunTest(
	const FString& Parameters)
{
	FScopedM632FrameCounter FrameCounterGuard;
	FMHGZM632Harness Harness;
	if (!Harness.Setup())
	{
		AddError(TEXT("M3 runtime harness setup failed"));
		return false;
	}
	Harness.CombatConfig->KinsectAutoMarkSearchRadius = 1000.0f;
	Harness.CombatConfig->KinsectAutoMarkAttackInterval = 0.1f;
	FMHGZM632MarkTarget Target = SpawnM632MarkTarget(*Harness.World,
		FVector(300.0f, 0.0f, 0.0f), false);
	if (!TestNotNull(TEXT("marked Hitzone exists"), Target.Hitzone))
	{
		Harness.Teardown();
		return false;
	}
	const FVector MarkPoint = Target.Hitzone->GetComponentLocation()
		+ FVector::UpVector * 30.0f;
	TestTrue(TEXT("valid mark exists while the Kinsect is attached"),
		Harness.Resource->SetKinsectMarkFromMeleeHit(
			MakeM632MarkHit(Target, MarkPoint)));

	TestTrue(TEXT("player can send the Kinsect before recall"),
		Harness.Resource->DeployKinsect(MakeM632PlayerHoverRequest(Harness)));
	Harness.Kinsect->Interrupt();
	TestTrue(TEXT("recall returns the hovering Kinsect"),
		Harness.Resource->RecallKinsect());
	TickM632World(Harness, 3, 1.0f / 60.0f);
	TestEqual(TEXT("returned Kinsect remains attached despite an active mark"),
		Harness.Kinsect->GetState(), EKinsectState::Attached);
	TickM632World(Harness, 10, 1.0f / 60.0f);
	TestEqual(TEXT("subsequent ticks never auto-deploy an attached Kinsect"),
		Harness.Kinsect->GetState(), EKinsectState::Attached);

	Harness.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZAutoMarkDuringHunterAttack,
	"MHGZ.Maintenance.Kinsect.AutoMarkDuringHunterAttack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZAutoMarkDuringHunterAttack::RunTest(const FString&)
{
	FScopedM632FrameCounter FrameCounterGuard;
	UClass* AttackClass = LoadClass<UGameplayAbility>(nullptr,
		TEXT("/Game/Blueprints/Ability/InsectGlaive/GA_IG_TuCi.GA_IG_TuCi_C"));
	if (!TestNotNull(TEXT("resident ordinary ground attack class exists"), AttackClass)) return false;
	// Both starting a seek while attacking and attacking during an existing seek.
	for (const bool bSeekAlreadyRunning : {false, true})
	{
		FMHGZM632Harness H;
		if (!H.Setup()) { H.Teardown(); return false; }
		H.CombatConfig->KinsectAutoMarkSearchRadius = 1500.f;
		H.CombatConfig->KinsectAutoMarkAttackInterval = .1f;
		H.KinsectData->FlightSpeed = 600.f;
		auto Target = SpawnM632MarkTarget(*H.World, FVector(700, 0, 0), true);
		if (!TestNotNull(TEXT("real marked damage target exists"), Target.Attributes))
		{
			H.Teardown(); return false;
		}
		Target.Attributes->InitMaxHealth(1000.f);
		Target.Attributes->InitHealth(1000.f);
		Target.Hitzone->ExtractColorTag = M3::Tag(TEXT("WeaponResource.IG.Extract.Red"));
		TestTrue(TEXT("mark is established"), H.Resource->SetKinsectMarkFromMeleeHit(
			MakeM632MarkHit(Target, Target.Hitzone->GetComponentLocation() + FVector(0, 0, 30))));
		TestTrue(TEXT("player deploys the Kinsect"), H.Resource->DeployKinsect(MakeM632PlayerHoverRequest(H)));
		H.Kinsect->Interrupt();
		FKinsectFlightRequest PreviousAuto;
		if (bSeekAlreadyRunning)
		{
			TickM632World(H, 1, 1.f / 60.f);
			if (!TestEqual(TEXT("automatic seek starts before the hunter attack"), H.Kinsect->GetState(), EKinsectState::Flying))
			{
				H.Teardown(); return false;
			}
			PreviousAuto = H.Kinsect->ActiveRequest;
		}
		const auto Attack = H.MakeActionToken(AttackClass);
		TestTrue(TEXT("ordinary attack registers with the host"), H.Host->RegisterAction(Attack));
		const auto Attacking = M3::Tag(TEXT("Combat.State.Attacking"));
		H.ASC->AddLooseGameplayTag(Attacking);
		TickM632World(H, 1, 1.f / 60.f);
		if (!TestEqual(TEXT("hunter attack does not prevent or interrupt seeking"), H.Kinsect->GetState(), EKinsectState::Flying))
		{
			H.Teardown(); return false;
		}
		TestEqual(TEXT("automatic command keeps ownership"), H.Kinsect->ActiveRequest.CommandSource, EKinsectCommandSource::AutoMark);
		if (bSeekAlreadyRunning)
		{
			TestTrue(TEXT("starting an attack preserves the existing automatic flight identity"),
				H.Resource->IsKinsectCommandCurrent(PreviousAuto));
			TestEqual(TEXT("starting an attack does not restart automatic flight"),
				H.Kinsect->ActiveRequest.FlightInstanceID, PreviousAuto.FlightInstanceID);
		}
		for (int32 Frame = 0; Frame < 180 && Target.Attributes->GetHealth() >= 1000.f; ++Frame)
			TickM632World(H, 1, 1.f / 60.f);
		TestTrue(TEXT("automatic contact deals real damage during the hunter attack"), Target.Attributes->GetHealth() < 1000.f);
		TestTrue(TEXT("automatic contact collects cargo during the hunter attack"), H.Kinsect->HasPendingExtract());
		const float FirstHealth = Target.Attributes->GetHealth();
		TickM632World(H, 12, 1.f / 60.f);
		TestTrue(TEXT("automatic attacks continue at the configured interval"), Target.Attributes->GetHealth() < FirstHealth);
		TestTrue(TEXT("the hunter attack remains registered throughout"), H.Host->IsActionRegistered(Attack));
		TestTrue(TEXT("the hunter remains in Attacking state"), H.ASC->HasMatchingGameplayTag(Attacking));

		TestTrue(TEXT("an actual player Kinsect lease still preempts automation"), H.Resource->BeginKinsectPlayerCommandLease(Attack));
		const float LeasedHealth = Target.Attributes->GetHealth();
		TickM632World(H, 12, 1.f / 60.f);
		TestEqual(TEXT("no automatic damage while a player lease owns the Kinsect"), Target.Attributes->GetHealth(), LeasedHealth);
		H.Resource->EndKinsectPlayerCommandLease(Attack);
		for (int32 Frame = 0; Frame < 180 && Target.Attributes->GetHealth() >= LeasedHealth; ++Frame)
			TickM632World(H, 1, 1.f / 60.f);
		TestTrue(TEXT("automation resumes when the lease ends despite the ongoing hunter attack"), Target.Attributes->GetHealth() < LeasedHealth);
		TestTrue(TEXT("resuming automation does not end the hunter action"), H.Host->IsActionRegistered(Attack));
		TestTrue(TEXT("manual recall still takes priority while the hunter attacks"), H.Resource->RecallKinsect());
		for (int32 Frame = 0; Frame < 180 && H.Kinsect->GetState() != EKinsectState::Attached; ++Frame)
			TickM632World(H, 1, 1.f / 60.f);
		TestEqual(TEXT("manual recall returns the Kinsect to the arm"), H.Kinsect->GetState(), EKinsectState::Attached);
		TickM632World(H, 12, 1.f / 60.f);
		TestEqual(TEXT("the mark cannot automatically redeploy an attached Kinsect"), H.Kinsect->GetState(), EKinsectState::Attached);
		H.ASC->RemoveLooseGameplayTag(Attacking);
		H.Teardown();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZAutoMarkExtractCargo,
	"MHGZ.Maintenance.Kinsect.AutoMarkExtractCargoAndReturn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZAutoMarkExtractCargo::RunTest(const FString&)
{
	FScopedM632FrameCounter FrameCounterGuard;
	const auto Red = M3::Tag(TEXT("WeaponResource.IG.Extract.Red"));
	const auto White = M3::Tag(TEXT("WeaponResource.IG.Extract.White"));
	// Empty cargo, existing ordinary cargo, rejected damage, GE callback recall.
	for (int32 Mode = 0; Mode < 4; ++Mode)
	{
		FMHGZM632Harness H;
		if (!H.Setup()) { H.Teardown(); return false; }
		H.CombatConfig->KinsectAutoMarkSearchRadius = 1200.f;
		H.CombatConfig->KinsectAutoMarkAttackInterval = .1f;
		auto Target = SpawnM632MarkTarget(*H.World, FVector(300, 0, 0), true);
		if (!TestNotNull(TEXT("real damage target"), Target.ASC)) { H.Teardown(); return false; }
		Target.Hitzone->ExtractColorTag = Red;
		const auto MarkHit = MakeM632MarkHit(Target, Target.Hitzone->GetComponentLocation() + FVector(0, 0, 30));
		TestTrue(TEXT("mark establishes"), H.Resource->SetKinsectMarkFromMeleeHit(MarkHit));
		if (Mode == 1) TestTrue(TEXT("ordinary cargo already contains white"), H.Kinsect->CapturePendingExtractColor(White));
		int32 DamageAttempts = 0;
		if (Mode == 2)
		{
			FGameplayEffectApplicationQuery Reject;
			Reject.BindLambda([&](const FActiveGameplayEffectsContainer&, const FGameplayEffectSpec&)
			{ ++DamageAttempts; return false; });
			Target.ASC->GameplayEffectApplicationQueries.Add(Reject);
		}
		if (Mode == 3)
		{
			Target.ASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
				[&](UAbilitySystemComponent*, const FGameplayEffectSpec&, FActiveGameplayEffectHandle)
				{ ++DamageAttempts; H.Resource->RecallKinsect(); });
		}
		TestTrue(TEXT("player deploys"), H.Resource->DeployKinsect(MakeM632PlayerHoverRequest(H)));
		H.Kinsect->Interrupt();
		for (int32 Frame = 0; Frame < 120 && Target.Attributes->GetHealth() >= 100.f && DamageAttempts == 0; ++Frame)
		{
			TickM632World(H, 1, 1.f / 60.f);
		}
		if (Mode >= 2)
		{
			TestTrue(TEXT("test reaches a real contact damage attempt"), DamageAttempts > 0);
			TestFalse(TEXT("rejected or superseded contact cannot collect cargo"), H.Kinsect->HasPendingExtract());
			TestFalse(TEXT("rejected or superseded contact cannot grant a hunter extract"), H.Resource->HasExtract(Red));
			if (Mode == 2) TestEqual(TEXT("GE rejection preserves target health"), Target.Attributes->GetHealth(), 100.f);
		}
		else
		{
			TestTrue(TEXT("automatic attack actually deals damage"), Target.Attributes->GetHealth() < 100.f);
			const auto Expected = Mode == 1 ? White : Red;
			TestEqual(TEXT("normal single-color cargo is collected/preserved"), H.Kinsect->GetPendingExtractColor(), Expected);
			TestEqual(TEXT("UI shows the carried color before returning"), H.Resource->GetUIState().KinsectPendingExtractColor, Expected);
			TestFalse(TEXT("automatic hit does not grant the hunter red directly"), H.Resource->HasExtract(Red));
			TickM632World(H, 12, 1.f / 60.f);
			TestEqual(TEXT("repeated attacks neither duplicate nor replace ordinary cargo"), H.Kinsect->GetPendingExtractColors().Num(), 1);
			TestTrue(TEXT("manual recall starts"), H.Resource->RecallKinsect());
			for (int32 Frame = 0; Frame < 120 && H.Kinsect->GetState() != EKinsectState::Attached; ++Frame)
			{
				TickM632World(H, 1, 1.f / 60.f);
			}
			TestEqual(TEXT("returns to the arm"), H.Kinsect->GetState(), EKinsectState::Attached);
			TestTrue(TEXT("return delivers the original carried extract"), H.Resource->HasExtract(Expected));
			TestFalse(TEXT("return atomically clears carried extracts"), H.Kinsect->HasPendingExtract());
			TestFalse(TEXT("UI cargo clears after delivery"), H.Resource->GetUIState().KinsectPendingExtractColor.IsValid());
		}
		H.Teardown();
	}
	return true;
}

#endif
