// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "AttributeSystem/Res_InsectGlaive.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Monster/MHGZTrainingDummy.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"
#include "../../Shared/MHGZM3TestHarness.h"

namespace
{
struct FMHGZM6MarkTarget
{
	AActor* Actor = nullptr;
	UMHGZMonsterHitzoneComponent* Hitzone = nullptr;
	UAbilitySystemComponent* ASC = nullptr;
	UMHGZAttributeSet* Attributes = nullptr;
};

FMHGZM6MarkTarget SpawnMarkTarget(UWorld& World, const FVector& Location,
	const bool bWithHealth)
{
	FMHGZM6MarkTarget Target;
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
	Target.Hitzone->BoneName = TEXT("mark_surface");
	Target.Actor->AddInstanceComponent(Target.Hitzone);
	Target.Actor->SetRootComponent(Target.Hitzone);
	Target.Hitzone->RegisterComponent();
	Target.Hitzone->SetWorldLocation(Location);

	if (bWithHealth)
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

FHitResult MakeMarkHit(const FMHGZM6MarkTarget& Target,
	const FVector& ImpactPoint, const FVector& ImpactNormal = FVector::UpVector)
{
	FHitResult Hit(Target.Actor, Target.Hitzone, ImpactPoint, ImpactNormal);
	Hit.bBlockingHit = true;
	Hit.Location = ImpactPoint;
	Hit.ImpactPoint = ImpactPoint;
	Hit.ImpactNormal = ImpactNormal;
	return Hit;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM6MonsterBaseMarkProjectsFromProxyToVisibleMesh,
	"MHGZ.M6.Mark.MonsterBaseProjectsProxyPointToVisibleMesh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM6MonsterBaseMarkProjectsFromProxyToVisibleMesh::RunTest(
	const FString& Parameters)
{
	FMHGZM3Harness Harness;
	if (!Harness.Setup())
	{
		AddError(TEXT("M3 runtime harness setup failed"));
		return false;
	}

	AMHGZTrainingDummy* Dummy = Harness.World->SpawnActor<AMHGZTrainingDummy>(
		AMHGZTrainingDummy::StaticClass(), FVector(300.0f, 0.0f, 0.0f),
		FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("training dummy exists"), Dummy))
	{
		Harness.Teardown();
		return false;
	}
	// Surface-channel initialization belongs to production BeginPlay, not construction.
	if (!Dummy->HasActorBegunPlay()) { Dummy->DispatchBeginPlay(); }
	if (!TestNotNull(TEXT("dummy exposes a visible mesh for the base-class query"),
		Dummy->WhiteSegmentVisual.Get()))
	{
		Harness.Teardown();
		return false;
	}

	UMHGZMonsterHitzoneComponent* Hitzone = NewObject<UMHGZMonsterHitzoneComponent>(
		Dummy, TEXT("TestMarkHitzone"), RF_Transient);
	if (!TestNotNull(TEXT("proxy hitzone exists"), Hitzone))
	{
		Harness.Teardown();
		return false;
	}
	Hitzone->SetupAttachment(Dummy->GetCapsuleComponent());
	Dummy->AddInstanceComponent(Hitzone);
	Hitzone->RegisterComponent();
	Hitzone->SetRelativeLocation(FVector(0.0f, 0.0f, 112.5f));
	Hitzone->SetCollisionResponseToChannel(ECC_GameTraceChannel4, ECR_Ignore);

	TestEqual(TEXT("generic base makes visible meshes queryable on the mark-only channel"),
		Dummy->WhiteSegmentVisual->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);
	TestEqual(TEXT("visible mesh overlaps the dedicated non-blocking surface trace"),
		Dummy->WhiteSegmentVisual->GetCollisionResponseToChannel(ECC_GameTraceChannel4),
		ECR_Overlap);

	const FVector CollisionPoint = Dummy->GetCapsuleComponent()->GetComponentLocation()
		+ FVector(56.25f, 0.0f, 112.5f);
	FVector VisualPoint = FVector::ZeroVector;
	FVector VisualNormal = FVector::ZeroVector;
	TestTrue(TEXT("proxy collision resolves through the generic base implementation"),
		Dummy->ResolveHitEffectVisualSurface(Hitzone, CollisionPoint,
			FVector::ForwardVector, VisualPoint, VisualNormal));
	// Compare against the actual faceted imported cylinder, not an ideal analytic circle.
	FHitResult ControlSurface;
	const FCollisionQueryParams SurfaceParams(SCENE_QUERY_STAT(M6MarkSurfaceControl), true);
	TestTrue(TEXT("independent primitive query reaches the actual red visual mesh"),
		Dummy->RedSegmentVisual->LineTraceComponent(ControlSurface,
			CollisionPoint + FVector(500, 0, 0), CollisionPoint - FVector(500, 0, 0), SurfaceParams));
	TestTrue(TEXT("surface point matches the actual visible mesh rather than its internal proxy"),
		VisualPoint.Equals(ControlSurface.ImpactPoint, 0.1f));
	TestTrue(TEXT("visual normal comes from the actual faceted surface"),
		VisualNormal.Equals(ControlSurface.ImpactNormal, 0.01f));

	Harness.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM6MarkUsesWorldSpacePatchAndFollowsHitzone,
	"MHGZ.M6.Mark.WorldSpacePatchFollowsTargetWithoutScaleInflation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM6MarkUsesWorldSpacePatchAndFollowsHitzone::RunTest(
	const FString& Parameters)
{
	FMHGZM3Harness Harness;
	if (!Harness.Setup())
	{
		AddError(TEXT("M3 runtime harness setup failed"));
		return false;
	}

	TestEqual(TEXT("new combat config uses the required 90-second mark default"),
		Harness.CombatConfig->KinsectMarkDuration, 90.0f);
	TestEqual(TEXT("mark patch half extent matches the 100cm visual footprint"),
		Harness.CombatConfig->KinsectMarkPatchHalfExtent, 50.0f);
	FMHGZM6MarkTarget Target = SpawnMarkTarget(*Harness.World,
		FVector(300.0f, 0.0f, 100.0f), false);
	if (!TestNotNull(TEXT("target actor exists"), Target.Actor)
		|| !TestNotNull(TEXT("target Hitzone exists"), Target.Hitzone))
	{
		Harness.Teardown();
		return false;
	}

	// Enlarging the dummy/hitzone must not enlarge the gameplay footprint.
	Target.Hitzone->SetWorldScale3D(FVector(4.0f));
	const FVector MarkPoint = Target.Hitzone->GetComponentLocation()
		+ FVector(0.0f, 0.0f, 30.0f);
	TestTrue(TEXT("a successful melee hit establishes a mark"),
		Harness.Resource->SetKinsectMarkFromMeleeHit(
			MakeMarkHit(Target, MarkPoint)));
	TestEqual(TEXT("hitzone bone identity is retained"),
		Harness.Resource->GetKinsectMarkBoneName(), FName(TEXT("mark_surface")));
	TestTrue(TEXT("mark expiry is 90 world seconds after creation"),
		FMath::IsNearlyEqual(static_cast<float>(Harness.Resource->GetKinsectMarkExpireTime()
			- Harness.World->GetTimeSeconds()), 90.0f, 0.01f));

	TestTrue(TEXT("a hit inside the 100cm square patch matches"),
		Harness.Resource->IsHitWithinKinsectMarkPatch(MakeMarkHit(
			Target, MarkPoint + FVector(49.0f, -49.0f, 0.0f))));
	TestFalse(TEXT("a hit beyond the fixed 50cm half-width does not match"),
		Harness.Resource->IsHitWithinKinsectMarkPatch(MakeMarkHit(
			Target, MarkPoint + FVector(50.1f, 0.0f, 0.0f))));
	TestFalse(TEXT("a hit beyond the surface-normal tolerance does not match"),
		Harness.Resource->IsHitWithinKinsectMarkPatch(MakeMarkHit(
			Target, MarkPoint + FVector(0.0f, 0.0f, 5.1f))));
	FMHGZM6MarkTarget SameActorDifferentComponent = Target;
	SameActorDifferentComponent.Hitzone =
		NewObject<UMHGZMonsterHitzoneComponent>(Target.Actor, NAME_None, RF_Transient);
	Target.Actor->AddInstanceComponent(SameActorDifferentComponent.Hitzone);
	SameActorDifferentComponent.Hitzone->RegisterComponent();
	SameActorDifferentComponent.Hitzone->SetWorldLocation(
		Target.Hitzone->GetComponentLocation());
	TestTrue(TEXT("a different Hitzone on the same actor matches by surface location"),
		Harness.Resource->IsHitWithinKinsectMarkPatch(MakeMarkHit(
			SameActorDifferentComponent, MarkPoint)));

	FVector WorldMark = FVector::ZeroVector;
	TestTrue(TEXT("mark world point is available before target movement"),
		Harness.Resource->GetKinsectMarkWorldLocation(WorldMark));
	Target.Actor->AddActorWorldOffset(FVector(75.0f, -20.0f, 12.0f));
	FVector MovedMark = FVector::ZeroVector;
	TestTrue(TEXT("mark follows its target component"),
		Harness.Resource->GetKinsectMarkWorldLocation(MovedMark));
	TestTrue(TEXT("mark follows the target by the exact world translation"),
		MovedMark.Equals(WorldMark + FVector(75.0f, -20.0f, 12.0f), 0.01f));

	Harness.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM6MarkClearsOnDeathAndReplacement,
	"MHGZ.M6.Mark.ClearsOnTargetDeathAndReplacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM6MarkClearsOnDeathAndReplacement::RunTest(const FString& Parameters)
{
	FMHGZM3Harness Harness;
	if (!Harness.Setup())
	{
		AddError(TEXT("M3 runtime harness setup failed"));
		return false;
	}
	FMHGZM6MarkTarget First = SpawnMarkTarget(*Harness.World,
		FVector(300.0f, 0.0f, 100.0f), true);
	FMHGZM6MarkTarget Second = SpawnMarkTarget(*Harness.World,
		FVector(500.0f, 0.0f, 100.0f), true);
	if (!TestNotNull(TEXT("first target Hitzone exists"), First.Hitzone)
		|| !TestNotNull(TEXT("second target Hitzone exists"), Second.Hitzone)
		|| !TestNotNull(TEXT("first target ASC exists"), First.ASC)
		|| !TestNotNull(TEXT("second target ASC exists"), Second.ASC))
	{
		Harness.Teardown();
		return false;
	}

	const FVector FirstPoint = First.Hitzone->GetComponentLocation()
		+ FVector(0.0f, 0.0f, 30.0f);
	const FVector SecondPoint = Second.Hitzone->GetComponentLocation()
		+ FVector(0.0f, 0.0f, 30.0f);
	TestTrue(TEXT("first living target accepts a mark"),
		Harness.Resource->SetKinsectMarkFromMeleeHit(MakeMarkHit(First, FirstPoint)));
	TestTrue(TEXT("a later valid mark replaces the first"),
		Harness.Resource->SetKinsectMarkFromMeleeHit(MakeMarkHit(Second, SecondPoint)));

	First.ASC->SetNumericAttributeBase(UMHGZAttributeSet::GetHealthAttribute(), 0.0f);
	TestTrue(TEXT("death of the replaced target cannot clear the current mark"),
		Harness.Resource->HasValidKinsectMark());
	Second.ASC->SetNumericAttributeBase(UMHGZAttributeSet::GetHealthAttribute(), 0.0f);
	TestFalse(TEXT("current target death clears its mark immediately"),
		Harness.Resource->HasValidKinsectMark());

	Harness.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM6MarkExpiresAndDeadTargetCannotReplace,
	"MHGZ.M6.Mark.ExpiresAndDeadTargetCannotReplace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM6MarkExpiresAndDeadTargetCannotReplace::RunTest(
	const FString& Parameters)
{
	FMHGZM3Harness Harness;
	if (!Harness.Setup())
	{
		AddError(TEXT("M3 runtime harness setup failed"));
		return false;
	}
	FMHGZM6MarkTarget Living = SpawnMarkTarget(*Harness.World,
		FVector(300.0f, 0.0f, 100.0f), false);
	FMHGZM6MarkTarget Dead = SpawnMarkTarget(*Harness.World,
		FVector(500.0f, 0.0f, 100.0f), true);
	if (!TestNotNull(TEXT("living target Hitzone exists"), Living.Hitzone)
		|| !TestNotNull(TEXT("dead target Hitzone exists"), Dead.Hitzone)
		|| !TestNotNull(TEXT("dead target attributes exist"), Dead.Attributes))
	{
		Harness.Teardown();
		return false;
	}

	const FVector LivingPoint = Living.Hitzone->GetComponentLocation()
		+ FVector(0.0f, 0.0f, 30.0f);
	TestTrue(TEXT("living mark is established"),
		Harness.Resource->SetKinsectMarkFromMeleeHit(MakeMarkHit(Living, LivingPoint)));
	const uint64 PreviousMarkSerial = Harness.Resource->GetKinsectMarkSerial();

	Dead.Attributes->SetHealth(0.0f);
	const FVector DeadPoint = Dead.Hitzone->GetComponentLocation()
		+ FVector(0.0f, 0.0f, 30.0f);
	TestFalse(TEXT("already-dead target cannot create a new mark"),
		Harness.Resource->SetKinsectMarkFromMeleeHit(MakeMarkHit(Dead, DeadPoint)));
	TestEqual(TEXT("rejected dead-target hit preserves the old mark serial"),
		Harness.Resource->GetKinsectMarkSerial(), PreviousMarkSerial);
	TestTrue(TEXT("rejected dead-target hit preserves the old mark"),
		Harness.Resource->HasValidKinsectMark());

	Harness.CombatConfig->KinsectMarkDuration = 0.05f;
	TestTrue(TEXT("short test mark replaces normally"),
		Harness.Resource->SetKinsectMarkFromMeleeHit(MakeMarkHit(Living, LivingPoint)));
	const uint64 ShortMarkSerial = Harness.Resource->GetKinsectMarkSerial();
	TestTrue(TEXT("expiry time uses the configured world-time duration"),
		FMath::IsNearlyEqual(static_cast<float>(Harness.Resource->GetKinsectMarkExpireTime()
			- Harness.World->GetTimeSeconds()), 0.05f, 0.01f));
	Harness.Resource->ExpireKinsectMark(ShortMarkSerial - 1);
	TestTrue(TEXT("stale expiry callback cannot clear a replacement mark"),
		Harness.Resource->HasValidKinsectMark());
	Harness.Resource->ExpireKinsectMark(ShortMarkSerial);
	TestFalse(TEXT("world-time expiry clears the mark"),
		Harness.Resource->HasValidKinsectMark());

	Harness.Teardown();
	return true;
}

#endif
