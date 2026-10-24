// Copyright MHGZ Project. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "MHGZCoAttackTestTypes.h"
#include "ActionSystem/Tests/Shared/MHGZM3TestHarness.h"
#include "ActionSystem/MHGZGameplayEffectContext.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "InsectGlaive/Kinsect/IGCoAttackComponent.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"
#include "Monster/MHGZTrainingDummy.h"
#include "Animation/ActiveMontageInstanceScope.h"
#include "MHGZCoAttackTestFixture.h"

using MHGZCoAttackTest::FCoAttackFixture;

namespace
{
FAnimNotifyEventReference Ref(const FAnimNotifyEvent& Event, const UObject* Source, const int32 MontageID)
{
	FAnimNotifyEventReference R(&Event, Source);
	R.AddContextData<UE::Anim::FAnimNotifyMontageInstanceContext>(MontageID);
	return R;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZCoAttackM2IdentityTest,
	"MHGZ.Maintenance.KinsectCoAttack.M2.OccurrenceIdentityAndReentry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZCoAttackM2IdentityTest::RunTest(const FString&)
{
	FCoAttackFixture F;
	if (!TestTrue(TEXT("actual committed IG fixture"), F.Build())) return false;
	F.Ability->CoAttack.bEnabled = false; // this test isolates generic weapon routing
	FAnimNotifyEvent E;
	E.NotifyName = TEXT("AttackCollision"); E.TrackIndex = 1;
	const auto A = Ref(E, F.Ability->AttackMontage, 101);
	const auto B = Ref(E, F.Ability->AttackMontage, 101); // same asset/GUID, new occurrence context
	F.Ability->BeginCollisionFromNotify(0, F.H.Character->GetMesh(), 101, A);
	const uint64 First = F.Ability->GetCollisionWindowOccurrence(0);
	F.Ability->BeginCollisionFromNotify(0, F.H.Character->GetMesh(), 101, B);
	const uint64 Second = F.Ability->GetCollisionWindowOccurrence(0);
	TestTrue(TEXT("repeated same index receives a fresh occurrence"), First != 0 && Second > First);
	F.Ability->EndCollisionFromNotify(0, F.H.Character->GetMesh(), 101, A);
	TestEqual(TEXT("old End cannot close a newer occurrence"), F.Ability->GetCollisionWindowOccurrence(0), Second);
	F.Ability->EndCollisionFromNotify(0, F.H.Character->GetMesh(), 102, B);
	TestEqual(TEXT("wrong montage cannot close occurrence"), F.Ability->GetCollisionWindowOccurrence(0), Second);
	const auto FreshTick = Ref(E, F.Ability->AttackMontage, 101);
	F.Ability->TickCollisionFromNotify(0, F.H.Character->GetMesh(), 101, FreshTick, .016f);
	F.Ability->EndCollisionFromNotify(0, F.H.Character->GetMesh(), 101, FreshTick);
	TestEqual(TEXT("End matches the engine latest Tick reference"), F.Ability->GetCollisionWindowOccurrence(0), uint64(0));
	TestEqual(TEXT("replacement and exact closure each retire once"), F.Ability->CloseCount, 2);
	F.Ability->bCloseOnOpen = true;
	F.Ability->EnableCollision(0);
	TestEqual(TEXT("reentrant open hook closure prevents first sweep"), F.Ability->GetCollisionWindowOccurrence(0), uint64(0));
	TestEqual(TEXT("one hook per successful open"), F.Ability->OpenCount, 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZCoAttackM2IndependentTest,
	"MHGZ.Maintenance.KinsectCoAttack.M2.IndependentContactAndSnapshots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZCoAttackM2IndependentTest::RunTest(const FString&)
{
	for (const bool WeaponOnly : {true, false})
	{
		FCoAttackFixture F;
		if (!TestTrue(TEXT("fixture"), F.Build())) return false;
		F.WarmUp(); F.At(.3, F.Origin);
		F.SetTarget(F.Origin + FVector(WeaponOnly ? 5 : 200, 0, 0));
		F.Ability->EnableCollision(0);
		TestEqual(TEXT("source zero sweep is independent"), F.WeaponHits, WeaponOnly ? 1 : 0);
		if (WeaponOnly) TestTrue(TEXT("weapon-only contact actually settles health damage"), F.Target->GetAttributeSet()->GetHealth() < 10000.f);
		F.At(.35, F.Origin); F.Ability->DisableCollision(0);
		F.SetTarget(F.Origin + FVector(WeaponOnly ? 200 : 20, 0, 0));
		F.H.KinsectData->KinsectAttackPower = 999.f;
		F.Ability->CoAttack.SegmentMotionValues[0] = 99.f;
		F.Ability->AttackSegments[0].MultiHitCount = 20;
		F.Part->DefenseMultiplier = 50.f;
		F.At(.49, F.Origin);
		TestEqual(TEXT("no early hit before a+delay"), F.KinsectHits, 0);
		const float Health = F.Target->GetAttributeSet()->GetHealth();
		F.At(.51, F.Origin); F.At(.56, F.Origin); F.At(.65, F.Origin);
		TestEqual(TEXT("weapon hit / insect miss and weapon miss / insect hit both work"), F.KinsectHits, WeaponOnly ? 0 : 1);
		if (!WeaponOnly && TestTrue(TEXT("recorded one immutable damage snapshot"), F.Powers.Num() == 1))
		{
			TestEqual(TEXT("power frozen on source open"), F.Powers[0], 100.f);
			TestEqual(TEXT("motion value frozen on source open"), F.Motions[0], 10.f);
			TestEqual(TEXT("current hitzone 50 percent, not opening-time 100 percent"), Health - F.Target->GetAttributeSet()->GetHealth(), 5.f);
			TestFalse(TEXT("no extract side effect"), F.H.Kinsect->HasPendingExtract());
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZCoAttackM2CountsTest,
	"MHGZ.Maintenance.KinsectCoAttack.M2.TripleAndRepeatedWindowBudgets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZCoAttackM2CountsTest::RunTest(const FString&)
{
	for (const bool Repeated : {false, true})
	{
		FCoAttackFixture F;
		if (!TestTrue(TEXT("fixture"), F.Build())) return false;
		F.WarmUp();
		F.Ability->AttackSegments[0].MultiHitCount = Repeated ? 2 : 1;
		F.Ability->AttackSegments[0].MultiHitInterval = .05f;
		for (int32 I = 0; I < 3; ++I)
		{
			const double Time = .3 + I * .1;
			F.At(Time, F.Origin); F.Ability->EnableCollision(Repeated ? 0 : I);
			F.At(Time + .06, F.Origin); F.Ability->DisableCollision(Repeated ? 0 : I);
		}
		F.At(.61, F.Origin); F.At(.66, F.Origin); F.At(.71, F.Origin); F.At(.76, F.Origin);
		TestEqual(TEXT("source opportunities not AttackSegments.Num or weapon hits"), F.KinsectHits, Repeated ? 6 : 3);
		TSet<FGuid> Unique;
		for (const FGuid& ID : F.HitIDs) Unique.Add(ID);
		TestEqual(TEXT("resolver hit IDs unique for every submitted insect hit"), Unique.Num(), F.KinsectHits);
		TestEqual(TEXT("weapon did not need to hit"), F.WeaponHits, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZCoAttackM2PathTest,
	"MHGZ.Maintenance.KinsectCoAttack.M2.LongFramePolylineAndWindowEdges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZCoAttackM2PathTest::RunTest(const FString&)
{
	FCoAttackFixture F;
	if (!TestTrue(TEXT("fixture"), F.Build())) return false;
	F.WarmUp();
	const FVector A = F.Origin, B = A + FVector(0, 200, 0), C = A + FVector(200, 200, 0);
	F.SetTarget(B + FVector(0, 20, 0));
	F.At(.3, A); F.Ability->EnableCollision(0);
	F.At(.35, B);
	F.At(.4, C); F.Ability->DisableCollision(0);
	F.At(.49, C);
	TestEqual(TEXT("delay before source window"), F.KinsectHits, 0);
	F.At(.61, C); // consumes the corner; endpoint chord A->C would miss this target
	TestEqual(TEXT("all historical polyline knots consumed, including final boundary"), F.KinsectHits, 1);
	F.At(.7, C);
	TestEqual(TEXT("closed receipt cannot leak another hit"), F.KinsectHits, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZCoAttackM2IsolationTest,
	"MHGZ.Maintenance.KinsectCoAttack.M2.EntrySkipUnsupportedAndCancel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZCoAttackM2IsolationTest::RunTest(const FString&)
{
	for (int32 Mode = 0; Mode < 4; ++Mode)
	{
		FCoAttackFixture F;
		if (!TestTrue(TEXT("fixture"), F.Build(Mode == 0 ? 1.f : 2500.f, Mode != 0))) return false;
		F.WarmUp(); F.At(.3, F.Origin);
		if (Mode == 1) F.Ability->AttackSegments[0].MultiHitPolicy = EAttackMultiHitPolicy::LockedTargetTicks;
		if (Mode == 2) F.Ability->CoAttack.SegmentMotionValues.Reset();
		F.Ability->EnableCollision(0); F.At(.35, F.Origin); F.Ability->DisableCollision(0);
		if (Mode == 3) TestTrue(TEXT("recall preempts queued damage"), F.H.Resource->RecallKinsect());
		F.At(.51, F.Origin); F.At(.56, F.Origin);
		if (Mode == 0) { F.H.KinsectData->ReturnSpeed = 100000.f; F.At(.65, F.Origin); }
		TestEqual(TEXT("late entry, unsupported policy, missing mapping or recall never invent a hit"), F.KinsectHits, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZCoAttackM2ReentryTest,
	"MHGZ.Maintenance.KinsectCoAttack.M2.GERejectionAndReentrantTakeover",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZCoAttackM2ReentryTest::RunTest(const FString&)
{
	for (const bool Reentrant : {false, true})
	{
		FCoAttackFixture F;
		if (!TestTrue(TEXT("fixture"), F.Build())) return false;
		F.WarmUp(); F.At(.3, F.Origin); F.Ability->EnableCollision(0);
		F.At(.4, F.Origin); F.Ability->DisableCollision(0);
		bool Allow = false, TookOver = false;
		if (!Reentrant)
		{
			FGameplayEffectApplicationQuery Query;
			Query.BindLambda([&Allow](const FActiveGameplayEffectsContainer&, const FGameplayEffectSpec&) { return Allow; });
			F.Target->GetAbilitySystemComponent()->GameplayEffectApplicationQueries.Add(Query);
		}
		else F.Target->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
			[&](UAbilitySystemComponent*, const FGameplayEffectSpec& S, FActiveGameplayEffectHandle)
			{
				const auto* C = FMHGZGameplayEffectContext::ExtractEffectContext(S.GetContext());
				if (!C || C->DamageSourceType != EMHGZDamageSourceType::Kinsect) return;
				FKinsectFlightRequest R;
				R.RuntimeToken = F.H.Host->GetCurrentToken(); R.FlightInstanceID = FGuid::NewGuid();
				R.FlightSpeed = 2000; R.MaxDistance = 1000; R.ArrivalRadius = 10;
				TookOver = F.H.Resource->DeployKinsect(R);
			});
		F.At(.51, F.Origin);
		if (!Reentrant)
		{
			TestEqual(TEXT("rejected GE does not settle damage"), F.KinsectHits, 0);
			Allow = true;
			F.At(.56, F.Origin); F.At(.61, F.Origin);
			TestEqual(TEXT("failed GE did not consume the one-hit budget"), F.KinsectHits, 1);
			F.Target->GetAbilitySystemComponent()->GameplayEffectApplicationQueries.Reset();
		}
		else
		{
			TestTrue(TEXT("GE callback successfully submits a higher-priority command"), TookOver);
			TestFalse(TEXT("old sampler retired on takeover"), F.Sampler->IsRunning());
			TestEqual(TEXT("old replay cleanup does not cancel the new flight"), F.H.Kinsect->State, EKinsectState::Flying);
			F.H.Resource->EndKinsectCoAttack(F.Ability->GetActionToken());
			F.At(.61, F.Origin);
			TestEqual(TEXT("old End cannot recall the new command"), F.H.Kinsect->State, EKinsectState::Flying);
			TestEqual(TEXT("cleared queue has no ghost hits"), F.KinsectHits, 1);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZCoAttackM2TargetsTest,
	"MHGZ.Maintenance.KinsectCoAttack.M2.MultipleTargetsAndOverlappingParts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZCoAttackM2TargetsTest::RunTest(const FString&)
{
	FCoAttackFixture F;
	if (!TestTrue(TEXT("fixture"), F.Build())) return false;
	UMHGZMonsterHitzoneComponent* Overlap = NewObject<UMHGZMonsterHitzoneComponent>(F.Target);
	Overlap->HitzoneTag = M3::Tag(TEXT("Hitzone.Head")); Overlap->SetSphereRadius(8);
	Overlap->RegisterComponent(); Overlap->SetWorldLocation(F.Origin + FVector(21, 0, 0));
	AMHGZTrainingDummy* Other = F.H.World->SpawnActor<AMHGZTrainingDummy>();
	if (!TestNotNull(TEXT("independent second target"), Other)) return false;
	Other->bInfiniteHealth = false;
	Other->GetAbilitySystemComponent()->InitAbilityActorInfo(Other, Other);
	Other->GetAttributeSet()->InitMaxHealth(1000); Other->GetAttributeSet()->InitHealth(1000);
	UMHGZMonsterHitzoneComponent* Part = NewObject<UMHGZMonsterHitzoneComponent>(Other);
	Part->HitzoneTag = M3::Tag(TEXT("Hitzone.Head")); Part->SetSphereRadius(8);
	Part->RegisterComponent(); Part->SetWorldLocation(F.Origin + FVector(20, 10, 0));
	F.WarmUp(); F.At(.3, F.Origin); F.Ability->EnableCollision(0);
	F.At(.35, F.Origin); F.Ability->DisableCollision(0);
	F.At(.51, F.Origin); F.At(.56, F.Origin);
	TestEqual(TEXT("multiple overlapping hitzones do not multiply one target's hits"), F.KinsectHits, 1);
	TestEqual(TEXT("second target has its own independent budget"), Other->GetAttributeSet()->GetHealth(), 990.f);
	USkeletalMesh* KinsectMesh = LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Weapons/InsectGlaive/Meshes/Kinsect/SKM_IG_Kinsect.SKM_IG_Kinsect"));
	if (!TestNotNull(TEXT("read-only production insect mesh for coverage"), KinsectMesh)) return false;
	F.H.Kinsect->Mesh->SetSkeletalMesh(KinsectMesh);
	F.H.Kinsect->Mesh->UpdateBounds();
	const float Offset = float(F.H.Kinsect->Mesh->Bounds.SphereRadius) * 4.f + 100.f;
	F.H.Kinsect->Mesh->SetRelativeLocation(FVector(Offset, 0, 0));
	Overlap->SetWorldLocation(F.Origin + FVector(5000, 0, 0));
	F.SetTarget(F.Origin + FVector(Offset, 0, 0));
	F.At(.7, F.Origin); F.Ability->EnableCollision(0);
	F.At(.75, F.Origin); F.Ability->DisableCollision(0);
	F.At(.91, F.Origin); F.At(.96, F.Origin);
	TestEqual(TEXT("query covers a mesh offset from actor root, not radius alone"), F.KinsectHits, 2);
	return true;
}
#endif
