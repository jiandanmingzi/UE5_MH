// Copyright MHGZ Project. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "ActionSystem/MHGZDamageGameplayEffect.h"
#include "ActionSystem/Tests/M6/MHGZM6AwakenedKinsectTestTypes.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "Monster/MHGZTrainingDummy.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"
#include "../../Shared/MHGZM3TestHarness.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZM6FourHitSecondDamageMarks,
	"MHGZ.M6.Mark.FourHitOnlySecondSuccessfulDamageEstablishesMark",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM6FourHitSecondDamageMarks::RunTest(const FString& Parameters)
{
	FMHGZM3Harness H;
	if (!TestTrue(TEXT("mark runtime harness builds"), H.Setup())) { H.Teardown(); return false; }
	UClass* TargetClass = LoadClass<AMHGZTrainingDummy>(nullptr,
		TEXT("/Game/Blueprints/Monster/TrainingDummy/BP_TrainingDummy.BP_TrainingDummy_C"));
	AMHGZTrainingDummy* Target = TargetClass
		? H.World->SpawnActor<AMHGZTrainingDummy>(TargetClass, FVector(300, 0, 0), FRotator::ZeroRotator) : nullptr;
	if (!TestNotNull(TEXT("real mark target"), Target)) { H.Teardown(); return false; }
	if (!Target->HasActorBegunPlay()) { Target->DispatchBeginPlay(); }
	Target->GetAbilitySystemComponent()->InitAbilityActorInfo(Target, Target);
	TArray<UMHGZMonsterHitzoneComponent*> Zones;
	Target->GetComponents(Zones);
	if (!TestTrue(TEXT("target has hitzones"), !Zones.IsEmpty())) { H.Teardown(); return false; }
	UMHGZMonsterHitzoneComponent* Zone = Zones[0];
	const FVector Point = Zone->GetComponentLocation() - FVector(75, 0, 0);
	FHitResult Hit(Target, Zone->GetQueryCollisionComponent(), Point, -FVector::ForwardVector);
	Hit.bBlockingHit = true;
	Hit.Location = Hit.ImpactPoint = Point;
	Hit.TraceStart = Point - FVector(200, 0, 0);
	Hit.TraceEnd = Point + FVector(200, 0, 0);
	Hit.Normal = Hit.ImpactNormal = -FVector::ForwardVector;
	const FGameplayAbilitySpecHandle Handle = H.GiveAbility(UMHGZM6FourHitMarkTestAbility::StaticClass());
	FWeaponInputSnapshot Input = M3::MakePosedInput(true, false);
	Input.ResolvedInputTag = M3::Tag(TEXT("Input.Weapon.YB"));
	for (int32 Activation = 0; Activation < 2; ++Activation)
	{
		if (!TestTrue(TEXT("per-execution source activates"), H.TryActivateWithInput(Handle, Input))) { H.Teardown(); return false; }
		UMHGZM6FourHitMarkTestAbility* Source = nullptr;
		for (UGameplayAbility* Instance : H.ASC->FindAbilitySpecFromHandle(Handle)->GetAbilityInstances())
		{
			if (Instance->IsActive()) { Source = Cast<UMHGZM6FourHitMarkTestAbility>(Instance); }
		}
		if (!TestNotNull(TEXT("active source"), Source)) { H.Teardown(); return false; }
		const uint64 Before = H.Resource->GetKinsectMarkSerial();
		TestTrue(TEXT("first damage succeeds"), Source->ApplyDamageForTest(Hit, 0));
		TestEqual(TEXT("first hit does not mark"), H.Resource->GetKinsectMarkSerial(), Before);
		Source->AttackSegments[1].Damage.DamageEffectClass = nullptr;
		TestFalse(TEXT("invalid second damage rejected"), Source->ApplyDamageForTest(Hit, 1));
		TestEqual(TEXT("failed damage does not mark"), H.Resource->GetKinsectMarkSerial(), Before);
		Source->AttackSegments[1].Damage.DamageEffectClass = UMHGZDamageGameplayEffect::StaticClass();
		TestTrue(TEXT("second damage succeeds"), Source->ApplyDamageForTest(Hit, 1));
		TestTrue(TEXT("second successful damage establishes/replaces mark"),
			H.Resource->HasValidKinsectMark() && H.Resource->GetKinsectMarkSerial() > Before);
		TestEqual(TEXT("mark uses actual struck hitzone"), H.Resource->GetKinsectMarkHitzone(), Zone);
		TestEqual(TEXT("four-hit default does not dispatch a kinsect"), H.Kinsect->State, EKinsectState::Attached);
		const uint64 Established = H.Resource->GetKinsectMarkSerial();
		for (const int32 Segment : { 1, 2, 3 })
		{
			TestTrue(TEXT("later/repeated damage succeeds"), Source->ApplyDamageForTest(Hit, Segment));
			TestEqual(TEXT("no repeated or third/fourth-hit mark"), H.Resource->GetKinsectMarkSerial(), Established);
		}
		H.ASC->CancelAbilityHandle(Handle);
		TestTrue(TEXT("action end preserves mark"), H.Resource->HasValidKinsectMark());
	}
	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZMarkSlashDispatchTest,
	"MHGZ.M6.Mark.MarkSlashDispatchUsesRealContactAndBypassesCooldown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZMarkSlashDispatchTest::RunTest(const FString&)
{
	for (const bool HasStamina : {true, false})
	{
		FMHGZM3Harness H;
		if (!TestTrue(TEXT("runtime"), H.Setup(false, AMHGZM3TestCharacter::StaticClass()))) return false;
		H.Character->SetPlayerState(H.PlayerState);
		AMHGZTrainingDummy* Target = H.World->SpawnActor<AMHGZTrainingDummy>();
		Target->bInfiniteHealth = false;
		Target->GetAbilitySystemComponent()->InitAbilityActorInfo(Target, Target);
		Target->GetAttributeSet()->InitMaxHealth(10000); Target->GetAttributeSet()->InitHealth(10000);
		Target->GetAttributeSet()->InitDefense(0);
		auto* Part = NewObject<UMHGZMonsterHitzoneComponent>(Target);
		Part->ExtractColorTag = M3::Tag(TEXT("WeaponResource.IG.Extract.Red"));
		Part->SetSphereRadius(20); Part->RegisterComponent(); Part->SetWorldLocation(FVector(300, 0, 100));
		FHitResult Hit(Target, Part, FVector(280, 0, 100), -FVector::ForwardVector);
		Hit.bBlockingHit = true; Hit.Location = Hit.ImpactPoint;
		const auto Handle = H.GiveAbility(UMHGZM6FourHitMarkTestAbility::StaticClass());
		if (!TestTrue(TEXT("source activation"), H.TryActivateWithInput(Handle, M3::MakePosedInput(true, false))))
		{ H.Teardown(); return false; }
		UMHGZM6FourHitMarkTestAbility* Source = nullptr;
		for (UGameplayAbility* Instance : H.ASC->FindAbilitySpecFromHandle(Handle)->GetAbilityInstances())
			if (Instance->IsActive()) Source = Cast<UMHGZM6FourHitMarkTestAbility>(Instance);
		if (!TestNotNull(TEXT("source"), Source)) { H.Teardown(); return false; }
		Source->MarkSegmentIndex = 0; Source->bSendKinsectOnMarkHit = true;
		Source->AttackSegments[0].Damage.DamageEffectClass = nullptr;
		TestFalse(TEXT("rejected weapon damage"), Source->ApplyDamageForTest(Hit, 0));
		TestFalse(TEXT("no mark from rejected damage"), H.Resource->HasValidKinsectMark());
		TestEqual(TEXT("no dispatch from rejected damage"), H.Kinsect->State, EKinsectState::Attached);
		Source->AttackSegments[0].Damage.DamageEffectClass = UMHGZDamageGameplayEffect::StaticClass();
		if (!HasStamina)
		{
			FKinsectFlightRequest Drain;
			Drain.RuntimeToken = H.Host->GetCurrentToken(); Drain.FlightInstanceID = FGuid::NewGuid();
			Drain.FlightSpeed = 1000; Drain.MaxDistance = 2000; Drain.ArrivalRadius = 10;
			TestTrue(TEXT("setup drain flight"), H.Resource->DeployKinsect(Drain));
			H.Resource->TickComponent(100.f, LEVELTICK_All, nullptr);
			H.Kinsect->SetActorLocation(H.Character->GetMesh()->GetComponentLocation()); H.Kinsect->Tick(.016f);
			TestEqual(TEXT("stamina depleted through normal resource tick"), H.Resource->GetKinsectStamina(), 0.f);
		}
		TestTrue(TEXT("weapon damage commits"), Source->ApplyDamageForTest(Hit, 0));
		TestTrue(TEXT("successful weapon hit still establishes mark"), H.Resource->HasValidKinsectMark());
		TestEqual(TEXT("powder waits for real kinsect contact"), H.Resource->GetPowderSnapshots().Num(), 0);
		if (!HasStamina)
		{
			TestEqual(TEXT("empty stamina does not cancel the mark or force a flight"), H.Kinsect->State, EKinsectState::Attached);
			H.Teardown(); continue;
		}
		TestEqual(TEXT("successful marked hit dispatches"), H.Kinsect->State, EKinsectState::Flying);
		const FKinsectFlightRequest Request = H.Kinsect->ActiveRequest;
		TestTrue(TEXT("target is the struck component"), Request.TargetComponent.Get() == Part && Request.bFollowTargetComponent);
		TestTrue(TEXT("target is the struck local point"), Request.TargetLocalPoint.Equals(FVector(-20, 0, 0), .01));
		TestEqual(TEXT("ordinary command motion value"), Request.MotionValue, H.CombatConfig->SendKinsectMotionValue);
		TestTrue(TEXT("repeated weapon callback succeeds"), Source->ApplyDamageForTest(Hit, 0));
		TestEqual(TEXT("one dispatch per activation"), H.Kinsect->ActiveRequest.FlightInstanceID, Request.FlightInstanceID);
		TestTrue(TEXT("ordinary contact starts its cooldown"), H.Resource->HandleSuccessfulKinsectDamage(
			H.Host->GetCurrentToken(), FGuid::NewGuid(), FGuid::NewGuid(), Hit, EIGPowderSpawnPolicy::MarkedContact));
		Part->AddWorldOffset(FVector(0, 100, 0));
		const FVector Point = Part->GetComponentTransform().TransformPosition(Request.TargetLocalPoint);
		const FVector Direction = (Point - H.Kinsect->GetActorLocation()).GetSafeNormal();
		H.Kinsect->SetActorLocation(Point - Direction * 5.f); H.Kinsect->Tick(.016f);
		TestEqual(TEXT("real single hit completes flight"), H.Kinsect->State, EKinsectState::Hovering);
		TestEqual(TEXT("one guaranteed powder bypasses ordinary cooldown"), H.Resource->GetPowderSnapshots().Num(), 2);
		H.Kinsect->Tick(.016f);
		TestEqual(TEXT("no second guaranteed powder"), H.Resource->GetPowderSnapshots().Num(), 2);
		TestTrue(TEXT("ordinary extract is held by kinsect"), H.Kinsect->HasPendingExtract());
		TestFalse(TEXT("hunter does not receive extract before return"), H.Resource->HasExtract(Part->ExtractColorTag));
		Hit.ImpactPoint = Point;
		TestFalse(TEXT("ordinary contact still respects its cooldown"), H.Resource->HandleSuccessfulKinsectDamage(
			H.Host->GetCurrentToken(), FGuid::NewGuid(), FGuid::NewGuid(), Hit, EIGPowderSpawnPolicy::MarkedContact));
		H.Teardown();
	}
	return true;
}
#endif
