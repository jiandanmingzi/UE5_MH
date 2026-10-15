// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Engine/EngineTypes.h"
#include "Engine/OverlapResult.h"
#include "EngineUtils.h"
#include "AbilitySystemGlobals.h"
#include "InsectGlaive/IGPowderActor.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"
#include "Monster/MHGZTrainingDummy.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "../../Shared/MHGZM3TestHarness.h"

namespace
{
struct FPowderTestTarget
{
	AActor* Actor = nullptr;
	UMHGZMonsterHitzoneComponent* Hitzone = nullptr;
	UAbilitySystemComponent* ASC = nullptr;
	UMHGZAttributeSet* Attributes = nullptr;
};

FPowderTestTarget SpawnPowderTarget(UWorld& World, const FVector& Location,
	const bool bWithASC, const float HitzoneRadius = 12.0f)
{
	FPowderTestTarget Target;
	Target.Actor = World.SpawnActor<AActor>(AActor::StaticClass(), Location,
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
	Target.Actor->AddInstanceComponent(Target.Hitzone);
	Target.Actor->SetRootComponent(Target.Hitzone);
	Target.Hitzone->RegisterComponent();
	Target.Hitzone->SetSphereRadius(HitzoneRadius);
	Target.Hitzone->SetCollisionObjectType(ECC_GameTraceChannel3);
	Target.Hitzone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Target.Hitzone->SetCollisionResponseToAllChannels(ECR_Block);
	Target.Hitzone->SetWorldLocation(Location);

	if (bWithASC)
	{
		Target.ASC = NewObject<UAbilitySystemComponent>(Target.Actor);
		Target.Actor->AddInstanceComponent(Target.ASC);
		Target.ASC->RegisterComponent();
		Target.Attributes = NewObject<UMHGZAttributeSet>(Target.Actor);
		Target.ASC->AddAttributeSetSubobject(Target.Attributes);
		Target.ASC->InitAbilityActorInfo(Target.Actor, Target.Actor);
		Target.Attributes->InitMaxHealth(100.0f);
		Target.Attributes->InitHealth(100.0f);
	}
	return Target;
}

FHitResult MakePowderHit(const FPowderTestTarget& Target,
	const FVector& Point, const FVector& Normal = FVector::UpVector)
{
	FHitResult Hit(Target.Actor, Target.Hitzone, Point, Normal);
	Hit.bBlockingHit = true;
	Hit.Location = Point;
	Hit.ImpactPoint = Point;
	Hit.ImpactNormal = Normal;
	return Hit;
}

FHitResult MakeRawPowderSourceHit(AActor& Actor, const FVector& Point)
{
	FHitResult Hit(&Actor, nullptr, Point, FVector::UpVector);
	Hit.bBlockingHit = true;
	Hit.Location = Point;
	Hit.ImpactPoint = Point;
	Hit.ImpactNormal = FVector::UpVector;
	return Hit;
}

AIGPowderActor* FindPowderByHit(UWorld& World, const FGuid& HitID)
{
	for (TActorIterator<AIGPowderActor> It(&World); It; ++It)
	{
		if (It->GetSourceHitInstanceID() == HitID)
		{
			return *It;
		}
	}
	return nullptr;
}

bool ConfigurePowder(FMHGZM3Harness& Harness, const float Multiplier = 1.0f)
{
	Harness.CombatConfig->PowderActorClass = AIGPowderActor::StaticClass();
	Harness.CombatConfig->PowderLifetime = 180.0f;
	Harness.CombatConfig->PowderDamageContribution = 5.0f;
	Harness.CombatConfig->PowderMotionValue = Multiplier * 100.0f;
	return true;
}

bool ApplyAndNotifyKinsectHit(FMHGZM3Harness& Harness,
	const FHitResult& Hit,
	const FGuid& AttackID, const FGuid& HitID,
	const EIGPowderSpawnPolicy Policy, bool& bOutPowderSpawned)
{
	const bool bDamageApplied = Harness.Resource->ApplyKinsectDamage(Hit, 20.0f, HitID);
	bOutPowderSpawned = bDamageApplied
		&& Harness.Resource->HandleSuccessfulKinsectDamage(
			Harness.Host->GetCurrentToken(), AttackID, HitID, Hit, Policy);
	return bDamageApplied;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM633PowderMarkedContactCooldownAndAwakenedBypass,
	"MHGZ.M6.Powder.MarkedContactCooldownAndAwakenedBypass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM633PowderMarkedContactCooldownAndAwakenedBypass::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FMHGZM3Harness H;
	if (!H.Setup())
	{
		AddError(TEXT("M3 runtime harness setup failed"));
		return false;
	}
	ConfigurePowder(H, 0.5f);

	FPowderTestTarget Target = SpawnPowderTarget(*H.World,
		FVector(500.0f, 0.0f, 100.0f), false);
	if (!TestNotNull(TEXT("marked target and Hitzone exist"), Target.Hitzone))
	{
		H.Teardown();
		return false;
	}
	const FVector MarkPoint = Target.Hitzone->GetComponentLocation();
	const FHitResult MarkHit = MakePowderHit(Target, MarkPoint);
	TestTrue(TEXT("a local mark can be established before the target exposes an ASC"),
		H.Resource->SetKinsectMarkFromMeleeHit(MarkHit));

	bool bSpawned = false;
	const FGuid FailedHitID = FGuid::NewGuid();
	TestFalse(TEXT("damage against a target without ASC fails"),
		ApplyAndNotifyKinsectHit(H, MarkHit, FGuid::NewGuid(),
			FailedHitID, EIGPowderSpawnPolicy::MarkedContact, bSpawned));
	TestFalse(TEXT("failed damage never enters the successful-hit powder handler"), bSpawned);

	Target.ASC = NewObject<UAbilitySystemComponent>(Target.Actor);
	Target.Actor->AddInstanceComponent(Target.ASC);
	Target.ASC->RegisterComponent();
	Target.Attributes = NewObject<UMHGZAttributeSet>(Target.Actor);
	Target.ASC->AddAttributeSetSubobject(Target.Attributes);
	Target.ASC->InitAbilityActorInfo(Target.Actor, Target.Actor);
	Target.Attributes->InitMaxHealth(100.0f);
	Target.Attributes->InitHealth(100.0f);

	const FGuid FirstHitID = FGuid::NewGuid();
	TestTrue(TEXT("first real damage in the marked patch succeeds"),
		ApplyAndNotifyKinsectHit(H, MarkHit, FGuid::NewGuid(),
			FirstHitID, EIGPowderSpawnPolicy::MarkedContact, bSpawned));
	TestTrue(TEXT("first eligible marked hit spawns immediately"), bSpawned);
	TArray<FIGPowderRuntimeSnapshot> Powders = H.Resource->GetPowderSnapshots();
	TestEqual(TEXT("one powder is present after the first marked hit"), Powders.Num(), 1);
	if (!Powders.IsEmpty())
	{
		TestEqual(TEXT("marked contact policy is recorded"), Powders[0].SpawnPolicy,
			EIGPowderSpawnPolicy::MarkedContact);
		TestTrue(TEXT("power and raw-percent motion value are snapshotted at creation"),
			FMath::IsNearlyEqual(Powders[0].KinsectAttackPowerSnapshot, 10.0f)
			&& FMath::IsNearlyEqual(Powders[0].MotionValueSnapshot, 50.0f)
			&& FMath::IsNearlyEqual(Powders[0].DamageContribution, 5.0f));
	}

	const FGuid CooldownHitID = FGuid::NewGuid();
	TestTrue(TEXT("a second actual damage still applies during cooldown"),
		H.Resource->ApplyKinsectDamage(MarkHit, 0.20f, CooldownHitID));
	TestFalse(TEXT("shared marked-contact cooldown blocks immediate second powder"),
		H.Resource->HandleSuccessfulKinsectDamage(H.Host->GetCurrentToken(),
			FGuid::NewGuid(), CooldownHitID, MarkHit,
			EIGPowderSpawnPolicy::MarkedContact));

	const FGuid SameHitID = FGuid::NewGuid();
	TestTrue(TEXT("same-hit duplicate test damage applies once"),
		H.Resource->ApplyKinsectDamage(MarkHit, 0.20f, SameHitID));
	TestTrue(TEXT("piercing exception can spawn during marked cooldown"),
		H.Resource->HandleSuccessfulKinsectDamage(H.Host->GetCurrentToken(),
			FGuid::NewGuid(), SameHitID, MarkHit,
			EIGPowderSpawnPolicy::AwakenedPiercing));
	TestFalse(TEXT("same successful HitID cannot create a second powder under another policy"),
		H.Resource->HandleSuccessfulKinsectDamage(H.Host->GetCurrentToken(),
			FGuid::NewGuid(), SameHitID, MarkHit,
			EIGPowderSpawnPolicy::MarkedContact));
	Powders = H.Resource->GetPowderSnapshots();
	TestEqual(TEXT("one marked and one piercing powder exist"), Powders.Num(), 2);

	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM633PowderCapCountsReserved,
	"MHGZ.M6.Powder.CapCountsReservedPowders",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM633PowderCapCountsReserved::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FMHGZM3Harness H;
	if (!H.Setup(false, AMHGZM3TestCharacter::StaticClass(), true))
	{
		AddError(TEXT("M3 runtime harness setup failed"));
		return false;
	}
	ConfigurePowder(H);
	FPowderTestTarget MarkTarget = SpawnPowderTarget(*H.World,
		FVector(500.0f, 0.0f, 0.0f), true);
	if (!TestNotNull(TEXT("cap/cooldown target has an ASC and Hitzone"), MarkTarget.ASC)
		|| !MarkTarget.Hitzone)
	{
		H.Teardown();
		return false;
	}
	MarkTarget.ASC->InitAbilityActorInfo(MarkTarget.Actor, MarkTarget.Actor);
	MarkTarget.Hitzone->HitzoneTag = FGameplayTag::RequestGameplayTag(TEXT("Hitzone.Torso"));
	MarkTarget.Attributes->InitMaxHealth(100.0f);
	MarkTarget.Attributes->InitHealth(100.0f);
	const FHitResult MarkHit = MakePowderHit(MarkTarget,
		MarkTarget.Hitzone->GetComponentLocation());
	TestTrue(TEXT("cap test establishes a valid mark patch"),
		H.Resource->SetKinsectMarkFromMeleeHit(MarkHit));
	H.CombatConfig->PowderGatherRadius = 1000.0f;
	H.CombatConfig->PowderGatherMaxCount = 1;
	const FGuid AttackID = FGuid::NewGuid();
	FGuid FirstPowderHitID;
	for (int32 Index = 0; Index < 99; ++Index)
	{
		const FVector Point(4.0f + Index * 2.0f, 0.0f, 10.0f);
		AActor* Source = H.World->SpawnActor<AActor>(AActor::StaticClass(),
			Point, FRotator::ZeroRotator);
		const FGuid HitID = FGuid::NewGuid();
		if (Index == 0)
		{
			FirstPowderHitID = HitID;
		}
		TestNotNull(TEXT("low-level powder source actor exists"), Source);
		if (!Source || !H.Resource->SpawnPowderForSuccessfulHit(
			H.Host->GetCurrentToken(), AttackID, HitID,
			MakeRawPowderSourceHit(*Source, Point)))
		{
			AddError(FString::Printf(TEXT("failed to create powder %d/99"), Index + 1));
			H.Teardown();
			return false;
		}
	}

	FWeaponActionToken GatherAction =
		H.MakeActionToken(UMHGZSendKinsectAbility::StaticClass());
	TestTrue(TEXT("gather action registers"), H.Host->RegisterAction(GatherAction));
	FIGPowderReservationToken Reservation;
	TestTrue(TEXT("one powder can be reserved before reaching the cap"),
		H.Resource->TryReservePowders(GatherAction, FVector(0.0f, 0.0f, 12.0f), Reservation));

	const FVector HundredPoint(400.0f, 0.0f, 10.0f);
	AActor* HundredSource = H.World->SpawnActor<AActor>(AActor::StaticClass(),
		HundredPoint, FRotator::ZeroRotator);
	TestTrue(TEXT("the 100th powder is accepted"), HundredSource
		&& H.Resource->SpawnPowderForSuccessfulHit(H.Host->GetCurrentToken(), AttackID,
			FGuid::NewGuid(), MakeRawPowderSourceHit(*HundredSource, HundredPoint)));

	const FVector OverCapPoint(420.0f, 0.0f, 10.0f);
	AActor* OverCapSource = H.World->SpawnActor<AActor>(AActor::StaticClass(),
		OverCapPoint, FRotator::ZeroRotator);
	TestFalse(TEXT("the 101st powder is rejected when one of the 100 is Reserved"),
		OverCapSource && H.Resource->SpawnPowderForSuccessfulHit(
			H.Host->GetCurrentToken(), AttackID, FGuid::NewGuid(),
			MakeRawPowderSourceHit(*OverCapSource, OverCapPoint)));
	const TArray<FIGPowderRuntimeSnapshot> Powders = H.Resource->GetPowderSnapshots();
	TestEqual(TEXT("cap rejection preserves all 100 existing powders"), Powders.Num(), 100);
	int32 ReservedCount = 0;
	for (const FIGPowderRuntimeSnapshot& Snapshot : Powders)
	{
		ReservedCount += Snapshot.State == EIGPowderState::Reserved ? 1 : 0;
	}
	TestEqual(TEXT("test inventory contains one reserved member"), ReservedCount, 1);

	const FGuid RejectedHitID = FGuid::NewGuid();
	TestTrue(TEXT("marked kinsect damage still succeeds while powder inventory is full"),
		H.Resource->ApplyKinsectDamage(MarkHit, 0.20f, RejectedHitID));
	TestFalse(TEXT("cap rejection does not create powder or consume the marked cooldown"),
		H.Resource->HandleSuccessfulKinsectDamage(H.Host->GetCurrentToken(),
			FGuid::NewGuid(), RejectedHitID, MarkHit,
			EIGPowderSpawnPolicy::MarkedContact));

	AIGPowderActor* FreedPowder = FindPowderByHit(*H.World, FirstPowderHitID);
	TestNotNull(TEXT("one available powder can be removed to reopen capacity"), FreedPowder);
	if (FreedPowder)
	{
		H.World->DestroyActor(FreedPowder);
	}
	const FGuid RetriedHitID = FGuid::NewGuid();
	TestTrue(TEXT("a later marked hit applies after capacity reopens"),
		H.Resource->ApplyKinsectDamage(MarkHit, 0.20f, RetriedHitID));
	TestTrue(TEXT("first actual powder after a cap rejection spawns immediately"),
		H.Resource->HandleSuccessfulKinsectDamage(H.Host->GetCurrentToken(),
			FGuid::NewGuid(), RetriedHitID, MarkHit,
			EIGPowderSpawnPolicy::MarkedContact));
	TestEqual(TEXT("successful retry restores inventory to the 100-powder cap"),
		H.Resource->GetPowderSnapshots().Num(), 100);
	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM633PowderNaturalExpiryTest,
	"MHGZ.M6.Powder.NaturalExpiryIsSingleFixedDamagePerActor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM633PowderNaturalExpiryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FMHGZM3Harness H;
	if (!H.Setup(false, AMHGZM3TestCharacter::StaticClass(), true))
	{
		AddError(TEXT("M3 runtime harness setup failed"));
		return false;
	}
	ConfigurePowder(H);

	AActor* Source = H.World->SpawnActor<AActor>(AActor::StaticClass(),
		FVector::ZeroVector, FRotator::ZeroRotator);
	// This scenario deliberately tests a 50cm radius, independent of the 150cm demo default.
	H.CombatConfig->PowderNaturalExplosionRadius = 50.0f;
	const FGuid AttackID = FGuid::NewGuid();
	const FGuid HitID = FGuid::NewGuid();
	TestTrue(TEXT("natural-expiry powder source actor exists"), IsValid(Source));
	if (!Source || !H.Resource->SpawnPowderForSuccessfulHit(
		H.Host->GetCurrentToken(), AttackID, HitID,
		MakeRawPowderSourceHit(*Source, FVector::ZeroVector)))
	{
		AddError(TEXT("failed to create powder for natural-expiry test"));
		H.Teardown();
		return false;
	}
	AIGPowderActor* Powder = FindPowderByHit(*H.World, HitID);
	if (!TestNotNull(TEXT("spawned powder actor resolves by source HitID"), Powder))
	{
		H.Teardown();
		return false;
	}
	Powder->DamageContribution = 7.0f;

	auto SpawnMonsterTarget = [&H](const FVector& Location)
	{
		FPowderTestTarget Result;
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AMHGZTrainingDummy* Dummy = H.World->SpawnActor<AMHGZTrainingDummy>(
			AMHGZTrainingDummy::StaticClass(), Location, FRotator::ZeroRotator,
			SpawnParameters);
		Result.Actor = Dummy;
		if (!Dummy)
		{
			return Result;
		}
		Result.ASC = Dummy->GetAbilitySystemComponent();
		Result.Attributes = Dummy->GetAttributeSet();
		// This test asserts exact finite health deltas, not the demo's infinite-health policy.
		Dummy->bInfiniteHealth = false;
		// The harness world is not advanced through BeginPlay. Production
		// AMHGZMonsterBase initializes this ActorInfo there, so mirror that setup
		// explicitly for the spawned test target before applying a GameplayEffect.
		if (Result.ASC)
		{
			Result.ASC->InitAbilityActorInfo(Dummy, Dummy);
		}
		Result.Attributes->InitMaxHealth(100.0f);
		Result.Attributes->InitHealth(100.0f);
		Result.Hitzone = NewObject<UMHGZMonsterHitzoneComponent>(Dummy,
			NAME_None, RF_Transient);
		Dummy->AddInstanceComponent(Result.Hitzone);
		Result.Hitzone->SetupAttachment(Dummy->GetRootComponent());
		Result.Hitzone->RegisterComponent();
		Result.Hitzone->SetSphereRadius(12.0f);
		Result.Hitzone->SetCollisionObjectType(ECC_GameTraceChannel3);
		Result.Hitzone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Result.Hitzone->SetCollisionResponseToAllChannels(ECR_Block);
		Result.Hitzone->SetWorldLocation(Location);
		return Result;
	};
	FPowderTestTarget Inside = SpawnMonsterTarget(FVector(40.0f, 0.0f, 2.0f));
	FPowderTestTarget Outside = SpawnMonsterTarget(FVector(70.0f, 0.0f, 2.0f));
	if (!TestNotNull(TEXT("inside blast target is configured"), Inside.Attributes)
		|| !TestNotNull(TEXT("outside blast target is configured"), Outside.Attributes))
	{
		H.Teardown();
		return false;
	}
	UMHGZMonsterHitzoneComponent* SecondInsideHitzone =
		NewObject<UMHGZMonsterHitzoneComponent>(Inside.Actor, NAME_None, RF_Transient);
	Inside.Actor->AddInstanceComponent(SecondInsideHitzone);
	SecondInsideHitzone->SetupAttachment(Inside.Actor->GetRootComponent());
	SecondInsideHitzone->RegisterComponent();
	SecondInsideHitzone->SetSphereRadius(12.0f);
	SecondInsideHitzone->SetCollisionObjectType(ECC_GameTraceChannel3);
	SecondInsideHitzone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SecondInsideHitzone->SetCollisionResponseToAllChannels(ECR_Block);
	SecondInsideHitzone->SetWorldLocation(FVector(41.0f, 0.0f, 2.0f));
	Inside.Hitzone->DefenseMultiplier = 10.0f;
	SecondInsideHitzone->DefenseMultiplier = 10.0f;
	H.ASC->SetNumericAttributeBase(UMHGZAttributeSet::GetCriticalRateAttribute(), 100.0f);
	TestTrue(TEXT("control kinsect damage applies to the production training-dummy ASC"),
		H.Resource->ApplyKinsectDamage(MakePowderHit(Inside,
			FVector(40.0f, 0.0f, 2.0f)), 0.20f, FGuid::NewGuid()));
	TestTrue(TEXT("control kinsect damage executed before reset"),
		Inside.Attributes->GetHealth() < 100.0f);
	Inside.Attributes->SetHealth(100.0f);
	TestNotNull(TEXT("natural explosion target ASC resolves through AbilitySystemGlobals"),
		UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Inside.Actor));
	FCollisionObjectQueryParams ObjectQuery;
	ObjectQuery.AddObjectTypesToQuery(ECC_GameTraceChannel3);
	TArray<FOverlapResult> InitialOverlaps;
	const bool bHasInitialOverlap = H.World->OverlapMultiByObjectType(InitialOverlaps,
		FVector(0.0f, 0.0f, 2.0f), FQuat::Identity, ObjectQuery,
		FCollisionShape::MakeSphere(50.0f));
	TestTrue(TEXT("natural explosion collision query discovers the in-range Hitzone"),
		bHasInitialOverlap && InitialOverlaps.ContainsByPredicate(
			[&Inside](const FOverlapResult& Overlap)
			{
				return Overlap.GetComponent() == Inside.Hitzone;
			}));

	const int32 OldRevision = Powder->LifetimeRevision;
	Powder->ExpireWorldTime = H.World->GetTimeSeconds() + 1.0;
	++Powder->LifetimeRevision;
	H.Resource->HandlePowderLifetimeExpired(Powder, OldRevision);
	TestTrue(TEXT("stale lifetime callback cannot consume or explode a refreshed revision"),
		H.Resource->GetPowderSnapshots().Num() == 1
		&& FMath::IsNearlyEqual(Inside.Attributes->GetHealth(), 100.0f));

	Powder->ExpireWorldTime = H.World->GetTimeSeconds();
	H.Resource->HandlePowderLifetimeExpired(Powder, Powder->LifetimeRevision);
	TestTrue(TEXT("one powder deals exactly its fixed 7 damage, ignoring hitzone defense and crit"),
		FMath::IsNearlyEqual(Inside.Attributes->GetHealth(), 93.0f));
	TestTrue(TEXT("each actor is damaged only once despite multiple overlapping hitzones"),
		FMath::IsNearlyEqual(Inside.Attributes->GetHealth(), 93.0f));
	TestTrue(TEXT("actors outside the 50cm radius are not damaged"),
		FMath::IsNearlyEqual(Outside.Attributes->GetHealth(), 100.0f));
	TestTrue(TEXT("terminal powder is unregistered before EndPlay completes"),
		H.Resource->GetPowderSnapshots().IsEmpty());
	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM633PowderDistanceDespawnTest,
	"MHGZ.M6.Powder.DistanceDespawnIsNonDamaging",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM633PowderDistanceDespawnTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FMHGZM3Harness H;
	if (!H.Setup())
	{
		AddError(TEXT("M3 runtime harness setup failed"));
		return false;
	}
	ConfigurePowder(H);
	H.CombatConfig->PowderDespawnDistance = 100.0f;

	FPowderTestTarget Target = SpawnPowderTarget(*H.World,
		FVector(250.0f, 0.0f, 0.0f), true, 12.0f);
	if (!TestNotNull(TEXT("despawn test target has health"), Target.Attributes))
	{
		H.Teardown();
		return false;
	}
	const FGuid HitID = FGuid::NewGuid();
	TestTrue(TEXT("powder spawns at its hit point far from player"),
		H.Resource->SpawnPowderForSuccessfulHit(H.Host->GetCurrentToken(),
			FGuid::NewGuid(), HitID, MakePowderHit(Target,
				Target.Hitzone->GetComponentLocation())));
	TestEqual(TEXT("powder is initially registered"),
		H.Resource->GetPowderSnapshots().Num(), 1);

	H.Resource->TickPowderMaintenance();
	TestTrue(TEXT("distance threshold removes the powder from Runtime inventory"),
		H.Resource->GetPowderSnapshots().IsEmpty());
	TestTrue(TEXT("distance removal does not apply natural explosion damage"),
		FMath::IsNearlyEqual(Target.Attributes->GetHealth(), 100.0f));
	H.Teardown();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
