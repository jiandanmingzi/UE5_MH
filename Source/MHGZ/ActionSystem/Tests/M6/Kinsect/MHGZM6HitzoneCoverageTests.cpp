// Copyright MHGZ Project. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Components/CapsuleComponent.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "Engine/OverlapResult.h"
#include "Monster/MHGZDummyConfig.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"
#include "Monster/MHGZTrainingDummy.h"
#include "../../Shared/MHGZM3TestHarness.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZM6DummySurfaceCoverage,
	"MHGZ.M6.HitzoneCoverage.RealDummySurfaceSeamsAndGrazingSweeps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZM6DummySurfaceCoverage::RunTest(const FString&)
{
	FMHGZM3Harness H;
	if (!H.Setup()) { return false; }
	UClass* DummyClass = LoadClass<AMHGZTrainingDummy>(nullptr,
		TEXT("/Game/Blueprints/Monster/TrainingDummy/BP_TrainingDummy.BP_TrainingDummy_C"));
	AMHGZTrainingDummy* Dummy = DummyClass
		? H.World->SpawnActor<AMHGZTrainingDummy>(DummyClass, FVector(3000, 0, 1000), FRotator::ZeroRotator) : nullptr;
	if (!TestNotNull(TEXT("production training target blueprint loads and spawns"), Dummy))
	{ H.Teardown(); return false; }
	Dummy->SetFireRingEnabled(false);
	if (!Dummy->HasActorBegunPlay()) { Dummy->DispatchBeginPlay(); }
	const FVector Center = Dummy->GetActorLocation();
	const float Radius = Dummy->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const float HalfHeight = Dummy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	TArray<UMHGZMonsterHitzoneComponent*> Zones;
	Dummy->GetComponents(Zones);
	if (!TestEqual(TEXT("real asset still creates exactly three part identities"), Zones.Num(), 3))
	{ H.Teardown(); return false; }
	for (UMHGZMonsterHitzoneComponent* Zone : Zones)
	{
		TestEqual(TEXT("production asset selects box query geometry"), Zone->QueryShape, EMHGZHitzoneShape::Box);
		TestEqual(TEXT("metadata sphere is not a second active collider"), Zone->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
		TestNotNull(TEXT("part has one active query primitive"), Zone->GetQueryCollisionComponent());
		TestTrue(TEXT("identity resolver returns its logical part"),
			UMHGZMonsterHitzoneComponent::Resolve(Zone->GetQueryCollisionComponent()) == Zone);
	}
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_GameTraceChannel3);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(M6DummyCoverage), false);
	int32 SurfaceSamples = 0;
	for (const float Z : { -HalfHeight, -112.5f, -56.25f, -1.0f, 0.0f, 1.0f, 56.25f, 112.5f, HalfHeight })
	{
		for (int32 Angle = 0; Angle < 16; ++Angle)
		{
			const float Radians = Angle * UE_TWO_PI / 16.0f;
			const FVector Point = Center + FVector(FMath::Cos(Radians) * Radius, FMath::Sin(Radians) * Radius, Z);
			TArray<FOverlapResult> Hits;
			H.World->OverlapMultiByObjectType(Hits, Point, FQuat::Identity, Objects, FCollisionShape::MakeSphere(0.1f), Params);
			bool bCovered = false;
			for (const FOverlapResult& Hit : Hits)
			{
				const UMHGZMonsterHitzoneComponent* Zone = UMHGZMonsterHitzoneComponent::Resolve(Hit.GetComponent());
				bCovered |= Zone && Zone->GetOwner() == Dummy;
			}
			TestTrue(FString::Printf(TEXT("visible cylinder surface is covered at z=%.2f angle=%d"), Z, Angle), bCovered);
			++SurfaceSamples;
		}
	}
	for (const float Z : { -112.5f, 0.0f, 112.5f })
	{
		FHitResult Hit;
		H.World->LineTraceSingleByChannel(Hit, Center + FVector(-200, 0, Z), Center + FVector(200, 0, Z), ECC_Visibility, Params);
		const UMHGZMonsterHitzoneComponent* Zone = UMHGZMonsterHitzoneComponent::Resolve(Hit.GetComponent());
		TestNotNull(TEXT("aim trace resolves real box hit to part identity"), Zone);
		if (Zone)
		{
			const TCHAR* Color = Z < 0 ? TEXT("White") : Z > 0 ? TEXT("Red") : TEXT("Orange");
			TestEqual(TEXT("aimed band retains its extract color"), Zone->ExtractColorTag,
				M3::Tag(*FString::Printf(TEXT("WeaponResource.IG.Extract.%s"), Color)));
		}
	}
	for (const float Z : { -112.5f, -56.25f, 0.0f, 56.25f, 112.5f })
	{
		TArray<FHitResult> Hits;
		H.World->SweepMultiByChannel(Hits, Center + FVector(-200, -83, Z),
			Center + FVector(200, -83, Z), FQuat::Identity, ECC_GameTraceChannel1,
			FCollisionShape::MakeSphere(14.0f), Params);
		bool bHitPart = false;
		for (const FHitResult& Hit : Hits)
		{
			const UMHGZMonsterHitzoneComponent* Zone = UMHGZMonsterHitzoneComponent::Resolve(Hit.GetComponent());
			bHitPart |= Zone && Zone->GetOwner() == Dummy;
		}
		TestTrue(TEXT("14cm weapon graze touching visible surface reaches a real part"), bHitPart);
	}
	AddInfo(FString::Printf(TEXT("Real dummy: %d surface samples, 3 color bands, 5 grazing sweeps; physical radius=%.2f halfHeight=%.2f"), SurfaceSamples, Radius, HalfHeight));
	// Repeated ApplyConfig must retire old child colliders, not leave hidden duplicates.
	Dummy->ApplyConfig(Dummy->DummyConfig);
	TArray<UMHGZMonsterBoxHitzoneQueryComponent*> Boxes;
	Dummy->GetComponents(Boxes);
	int32 RegisteredBoxes = 0;
	for (const auto* Box : Boxes) { RegisteredBoxes += Box && Box->IsRegistered() ? 1 : 0; }
	TestEqual(TEXT("reapplying config leaves exactly three registered query boxes"), RegisteredBoxes, 3);
	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZM6HitzoneShapeLifecycle,
	"MHGZ.M6.HitzoneCoverage.ShapeLifecycleHasOneQueryAndPreservesHitResult",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZM6HitzoneShapeLifecycle::RunTest(const FString&)
{
	FMHGZM3Harness H;
	if (!H.Setup()) { return false; }
	AActor* Owner = H.World->SpawnActor<AActor>();
	UMHGZMonsterHitzoneComponent* Zone = NewObject<UMHGZMonsterHitzoneComponent>(Owner);
	Owner->SetRootComponent(Zone); Owner->AddInstanceComponent(Zone);
	Zone->RegisterComponent(); Owner->SetActorLocation(FVector(2000, 0, 1000));
	TestTrue(TEXT("sphere remains its own query primitive"), Zone->GetQueryCollisionComponent() == Zone);
	Zone->ConfigureQueryShape(EMHGZHitzoneShape::Box, FVector(50, 50, 40));
	UPrimitiveComponent* Box = Zone->GetQueryCollisionComponent();
	Zone->ConfigureQueryShape(EMHGZHitzoneShape::Box, FVector(60, 30, 40));
	TestTrue(TEXT("resize updates the existing box instead of creating another collider"), Box == Zone->GetQueryCollisionComponent());
	FHitResult Hit(Owner, Box, Owner->GetActorLocation() + FVector(-60, 0, 0), FVector(-1, 0, 0));
	TestTrue(TEXT("real hit resolves logical metadata without replacing the hit component"),
		UMHGZMonsterHitzoneComponent::Resolve(Hit.GetComponent()) == Zone && Hit.GetComponent() == Box);
	FVector Closest;
	TestTrue(TEXT("closest-point query uses the active box, not its disabled metadata sphere"),
		FMath::IsNearlyEqual(Zone->GetClosestPointOnQueryCollision(Owner->GetActorLocation() + FVector(100, 0, 0), Closest), 40.0f, 0.5f));
	Zone->SetWorldScale3D(FVector(2, 1, 1));
	Owner->SetActorRotation(FRotator(0, 90, 0));
	TestTrue(TEXT("query follows nonuniform scale and rotation without shrinking its coverage"),
		FMath::IsNearlyEqual(Zone->GetClosestPointOnQueryCollision(Owner->GetActorLocation() + FVector(100, 0, 0), Closest), 70.0f, 0.5f));
	TestTrue(TEXT("shape-independent probe bound encloses the scaled box"),
		FMath::IsNearlyEqual(Zone->GetQueryBoundingRadius(), 130.0f, 0.5f));
	Zone->ForceRestoreAllChannels();
	TestEqual(TEXT("profile restore does not reactivate the old sphere"), Zone->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	TestEqual(TEXT("restored box is query-only"), Box->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);
	Zone->ConfigureQueryShape(EMHGZHitzoneShape::Sphere, FVector(30));
	TestTrue(TEXT("switch back retires box and restores the single sphere"),
		Zone->GetQueryCollisionComponent() == Zone && !Box->IsRegistered());
	H.Teardown(); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZM6BoxHitzoneKinsectDamage,
	"MHGZ.M6.HitzoneCoverage.RealBoxContactSettlesDamageAndCapturesExtract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZM6BoxHitzoneKinsectDamage::RunTest(const FString&)
{
	FMHGZM3Harness H;
	if (!H.Setup(false, AMHGZM3TestCharacter::StaticClass(), true)) { return false; }
	const FVector Origin = H.Kinsect->GetActorLocation();
	UClass* DummyClass = LoadClass<AMHGZTrainingDummy>(nullptr,
		TEXT("/Game/Blueprints/Monster/TrainingDummy/BP_TrainingDummy.BP_TrainingDummy_C"));
	AMHGZTrainingDummy* Dummy = DummyClass ? H.World->SpawnActor<AMHGZTrainingDummy>(DummyClass,
		Origin + FVector(250, 0, -112.5f), FRotator::ZeroRotator) : nullptr;
	if (!TestNotNull(TEXT("production dummy is available for actual kinsect contact"), Dummy))
	{ H.Teardown(); return false; }
	if (!Dummy->HasActorBegunPlay()) { Dummy->DispatchBeginPlay(); }
	Dummy->SetFireRingEnabled(false);
	Dummy->bInfiniteHealth = false; // Explicit finite-health test fixture, not a production change.
	Dummy->GetAbilitySystemComponent()->InitAbilityActorInfo(Dummy, Dummy);
	TestTrue(TEXT("target ASC has a live avatar for actual damage execution"),
		Dummy->GetAbilitySystemComponent()->GetAvatarActor() == Dummy);
	Dummy->GetAttributeSet()->InitMaxHealth(1000.0f);
	Dummy->GetAttributeSet()->InitHealth(1000.0f);
	FKinsectFlightRequest Request;
	Request.RuntimeToken = H.Host->GetCurrentToken();
	Request.FlightInstanceID = FGuid::NewGuid();
	Request.TrajectoryMode = EKinsectTrajectoryMode::AlongDirection;
	Request.DirectionSnapshot = FVector::ForwardVector;
	Request.MaxDistance = 1000.0f; Request.FlightSpeed = 2000.0f;
	Request.ArrivalRadius = 40.0f;
	Request.MotionValue = 0.20f;
	Request.DamageMode = EKinsectDamageMode::SingleHit;
	Request.ExtractMode = EKinsectExtractMode::FirstHitOnly;
	Request.PostFlightPolicy = EKinsectPostFlightPolicy::Hover;
	if (!TestTrue(TEXT("real resource deploys the single-hit kinsect"), H.Resource->DeployKinsect(Request)))
	{ H.Teardown(); return false; }
	H.Kinsect->SetActorLocation(Origin + FVector(300, 0, 0));
	H.Kinsect->Tick(1.0f / 60.0f);
	AddInfo(FString::Printf(TEXT("Box contact: health=%.2f attackPower=%.2f infinite=%d"),
		Dummy->GetAttributeSet()->GetHealth(), H.Resource->GetModifiedKinsectAttackPower(), Dummy->HasInfiniteHealth()));
	TestEqual(TEXT("box contact ends ordinary flight in hover"), H.Kinsect->GetState(), EKinsectState::Hovering);
	TestTrue(TEXT("real box HitResult resolves flesh data and submits damage"), Dummy->GetAttributeSet()->GetHealth() < 1000.0f);
	TestEqual(TEXT("box part provides red cargo to the existing resource/UI snapshot"),
		H.Resource->GetUIState().KinsectPendingExtractColor, M3::Tag(TEXT("WeaponResource.IG.Extract.Red")));
	H.Teardown(); return true;
}
#endif
