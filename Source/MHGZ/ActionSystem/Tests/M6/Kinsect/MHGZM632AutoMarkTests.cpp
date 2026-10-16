// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "AttributeSystem/MHGZAttributeSet.h"
#include "AttributeSystem/Res_InsectGlaive.h"
#include "CollisionQueryParams.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/BoxComponent.h"
#include "InsectGlaive/Kinsect/KinsectCollisionComponent.h"
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

#endif
