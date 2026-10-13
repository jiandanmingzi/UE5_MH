// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "AbilitySystemComponent.h"
#include "EngineUtils.h"

#include "AttributeSystem/MHGZAttributeSet.h"
#include "AttributeSystem/Res_InsectGlaive.h"
#include "InsectGlaive/IGPowderActor.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"
#include "../../Shared/MHGZM3TestHarness.h"

namespace
{
FHitResult MakePowderSourceHit(AActor& Target, const FVector& Point)
{
	FHitResult Hit(&Target, nullptr, Point, FVector::UpVector);
	Hit.bBlockingHit = true;
	Hit.Location = Point;
	Hit.ImpactPoint = Point;
	Hit.ImpactNormal = FVector::UpVector;
	return Hit;
}

struct FTelemetryDamageTarget
{
	AActor* Actor = nullptr;
	UMHGZMonsterHitzoneComponent* Hitzone = nullptr;
};

FTelemetryDamageTarget SpawnTelemetryDamageTarget(UWorld& World, const FVector& Location)
{
	FTelemetryDamageTarget Target;
	Target.Actor = World.SpawnActor<AActor>(AActor::StaticClass(), Location,
		FRotator::ZeroRotator);
	if (!Target.Actor)
	{
		return Target;
	}

	Target.Hitzone = NewObject<UMHGZMonsterHitzoneComponent>(Target.Actor,
		NAME_None, RF_Transient);
	Target.Actor->AddInstanceComponent(Target.Hitzone);
	Target.Actor->SetRootComponent(Target.Hitzone);
	Target.Hitzone->RegisterComponent();
	Target.Hitzone->SetSphereRadius(16.0f);
	Target.Hitzone->SetCollisionObjectType(ECC_GameTraceChannel3);
	Target.Hitzone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Target.Hitzone->SetCollisionResponseToAllChannels(ECR_Block);
	Target.Hitzone->SetWorldLocation(Location);
	Target.Hitzone->ExtractColorTag = M3::Tag(TEXT("WeaponResource.IG.Extract.White"));

	UAbilitySystemComponent* TargetASC = NewObject<UAbilitySystemComponent>(Target.Actor);
	Target.Actor->AddInstanceComponent(TargetASC);
	TargetASC->RegisterComponent();
	UMHGZAttributeSet* Attributes = NewObject<UMHGZAttributeSet>(Target.Actor);
	TargetASC->AddAttributeSetSubobject(Attributes);
	TargetASC->InitAbilityActorInfo(Target.Actor, Target.Actor);
	Attributes->InitMaxHealth(100.0f);
	Attributes->InitHealth(100.0f);
	return Target;
}

bool AddTestTriple(URes_InsectGlaive& Resource)
{
	return Resource.ApplyExtract(M3::Tag(TEXT("WeaponResource.IG.Extract.White")))
		&& Resource.ApplyExtract(M3::Tag(TEXT("WeaponResource.IG.Extract.Orange")))
		&& Resource.ApplyExtract(M3::Tag(TEXT("WeaponResource.IG.Extract.Red")))
		&& Resource.IsTripleUpActive();
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM6TripleCostReservationIsExclusiveAndReportsFailure,
	"MHGZ.M6.Resources.TripleCostReservationIsExclusiveAndReportsFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM6TripleCostReservationIsExclusiveAndReportsFailure::RunTest(
	const FString& Parameters)
{
	FMHGZM3Harness H;
	if (!H.Setup())
	{
		AddError(TEXT("M3 runtime harness setup failed"));
		return false;
	}

	URes_InsectGlaive* Resource = H.Resource;
	TestTrue(TEXT("triple extract is established"), AddTestTriple(*Resource));
	FWeaponResourceCostSpec Spec;
	Spec.CostType = M3::Tag(TEXT("Cost.IG.TripleUp"));
	Spec.Amount.Value = 1.0f;
	const TArray<FWeaponResourceCostSpec> Costs = { Spec };
	FWeaponActionToken Action = H.MakeActionToken(UMHGZSendKinsectAbility::StaticClass());
	TestTrue(TEXT("action token is valid"), Action.IsValid());

	FWeaponResourceCostReservation Reservation;
	TestTrue(TEXT("first action reserves the active triple"),
		Resource->TryReserveCosts(Action, Costs, Reservation));
	TestFalse(TEXT("a reserved triple is no longer available"),
		Resource->CanReserveCosts(Costs));
	FWeaponResourceCostReservation CompetingReservation;
	FWeaponActionToken CompetingAction = H.MakeActionToken(UMHGZSendKinsectAbility::StaticClass());
	TestFalse(TEXT("second action cannot reserve the same triple handle"),
		Resource->TryReserveCosts(CompetingAction, Costs, CompetingReservation));

	FWeaponResourceCostReservation WrongIdentity = Reservation;
	++WrongIdentity.ActivationSequenceID;
	TestFalse(TEXT("mismatched activation identity cannot consume"),
		Resource->ConsumeReservedCosts(WrongIdentity));
	Resource->ReleaseReservation(WrongIdentity);
	TestTrue(TEXT("mismatched release leaves the true reservation intact"),
		Resource->ConsumeReservedCosts(Reservation));
	TestFalse(TEXT("same reservation cannot be consumed twice"),
		Resource->ConsumeReservedCosts(Reservation));
	TestFalse(TEXT("successful consume removes triple"), Resource->IsTripleUpActive());

	TestTrue(TEXT("triple can be collected again"), AddTestTriple(*Resource));
	FWeaponResourceCostReservation ExpiringReservation;
	TestTrue(TEXT("triple can be reserved before it expires"),
		Resource->TryReserveCosts(Action, Costs, ExpiringReservation));
	TestTrue(TEXT("simulate external expiry/removal"), Resource->TryConsumeTripleUpAtomic());
	TestFalse(TEXT("stale reserved handle reports settlement failure"),
		Resource->ConsumeReservedCosts(ExpiringReservation));
	TestFalse(TEXT("empty reservation without a current runtime identity is rejected"),
		H.Host->ConsumeReservedCosts(FWeaponResourceCostReservation()));
	FWeaponResourceCostReservation EmptyReservation;
	EmptyReservation.RuntimeToken = H.Host->GetCurrentToken();
	EmptyReservation.ActivationSequenceID = H.Host->AllocateActivationSequenceID();
	TestTrue(TEXT("valid empty-cost reservation succeeds without a resource provider transaction"),
		H.Host->ConsumeReservedCosts(EmptyReservation));

	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM6CombatTelemetryQueueIsOptInAndOrdered,
	"MHGZ.M6.Telemetry.CombatEventQueueIsOptInAndOrdered",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM6CombatTelemetryQueueIsOptInAndOrdered::RunTest(
	const FString& Parameters)
{
	FMHGZM3Harness H;
	if (!H.Setup())
	{
		AddError(TEXT("M3 runtime harness setup failed"));
		return false;
	}

	IConsoleVariable* CaptureVariable = IConsoleManager::Get().FindConsoleVariable(
		TEXT("mhgz.Telemetry.Enable"));
	if (!CaptureVariable)
	{
		AddError(TEXT("mhgz.Telemetry.Enable console variable was not registered"));
		H.Teardown();
		return false;
	}

	const int32 PreviousCaptureValue = CaptureVariable->GetInt();
	uint64 LastSerial = H.Host->GetLatestCombatTelemetrySerial();
	CaptureVariable->Set(0, ECVF_SetByCode);

	FWeaponCombatTelemetryEvent DisabledEvent;
	DisabledEvent.EventName = TEXT("PowderSpawn");
	DisabledEvent.Reason = TEXT("OptInGateCheck");
	H.Host->RecordCombatTelemetryEvent(DisabledEvent);
	TestEqual(TEXT("disabled telemetry does not append a combat event"),
		H.Host->GetLatestCombatTelemetrySerial(), LastSerial);

	CaptureVariable->Set(1, ECVF_SetByCode);
	FWeaponCombatTelemetryEvent FirstEvent;
	FirstEvent.EventName = TEXT("PowderSpawn");
	FirstEvent.Reason = TEXT("MarkedContact");
	FirstEvent.MemberCount = 1;
	H.Host->RecordCombatTelemetryEvent(FirstEvent);

	FWeaponCombatTelemetryEvent SecondEvent;
	SecondEvent.EventName = TEXT("VortexArrived");
	SecondEvent.Reason = TEXT("TargetReached");
	SecondEvent.StageIndex = 1;
	H.Host->RecordCombatTelemetryEvent(SecondEvent);

	TArray<FWeaponCombatTelemetryEvent> Events;
	H.Host->GetCombatTelemetryEventsSince(LastSerial, Events);
	TestEqual(TEXT("both enabled events are returned"), Events.Num(), 2);
	if (Events.Num() == 2)
	{
		TestTrue(TEXT("event serials are strictly increasing"),
			Events[0].Serial < Events[1].Serial);
		TestEqual(TEXT("first event keeps its event name"),
			Events[0].EventName, FName(TEXT("PowderSpawn")));
		TestEqual(TEXT("first event keeps its reason"),
			Events[0].Reason, FName(TEXT("MarkedContact")));
		TestEqual(TEXT("first event keeps its payload"), Events[0].MemberCount, 1);
		TestEqual(TEXT("second event keeps its stage"), Events[1].StageIndex, 1);
		TestEqual(TEXT("runtime generation is stamped by the host"),
			Events[0].RuntimeGeneration, H.Host->GetCurrentToken().Generation);
	}
	TestEqual(TEXT("event cursor advances through returned events"),
		LastSerial, H.Host->GetLatestCombatTelemetrySerial());

	H.Host->GetCombatTelemetryEventsSince(LastSerial, Events);
	TestTrue(TEXT("events already returned are not delivered twice"), Events.IsEmpty());

	TestTrue(TEXT("triple extract is established for transaction telemetry"),
		AddTestTriple(*H.Resource));
	const FGuid SourceAttackID = FGuid::NewGuid();
	const FGuid ExtractHitID = FGuid::NewGuid();
	const FHitResult ExtractHit = MakePowderSourceHit(*H.Character,
		H.Character->GetActorLocation());
	TestTrue(TEXT("an extract attempt during TripleUp is recorded as absorbed"),
		H.Resource->ApplyExtractFromKinsectHit(
			M3::Tag(TEXT("WeaponResource.IG.Extract.White")), ExtractHit,
			SourceAttackID, ExtractHitID, 77));

	FWeaponResourceCostSpec TripleCost;
	TripleCost.CostType = M3::Tag(TEXT("Cost.IG.TripleUp"));
	TripleCost.Amount.Value = 1.0f;
	const TArray<FWeaponResourceCostSpec> TripleCosts = { TripleCost };
	FWeaponActionToken TripleAction = H.MakeActionToken(UMHGZSendKinsectAbility::StaticClass());
	FWeaponResourceCostReservation TripleReservation;
	TestTrue(TEXT("triple cost reservation succeeds"),
		H.Resource->TryReserveCosts(TripleAction, TripleCosts, TripleReservation));
	TestTrue(TEXT("triple cost consumption succeeds"),
		H.Resource->ConsumeReservedCosts(TripleReservation));

	FTelemetryDamageTarget DamageTarget = SpawnTelemetryDamageTarget(
		*H.World, FVector(120.0f, 0.0f, 50.0f));
	if (DamageTarget.Actor && DamageTarget.Hitzone)
	{
		FHitResult DamageHit(DamageTarget.Actor, DamageTarget.Hitzone,
			DamageTarget.Hitzone->GetComponentLocation(), FVector::UpVector);
		DamageHit.bBlockingHit = true;
		DamageHit.Location = DamageHit.ImpactPoint = DamageTarget.Hitzone->GetComponentLocation();
		const FGuid DamageHitID = FGuid::NewGuid();
		TestTrue(TEXT("valid kinsect damage applies and is recorded"),
			H.Resource->ApplyKinsectDamage(DamageHit, 0.2f, DamageHitID, SourceAttackID));
		TestTrue(TEXT("valid post-TripleUp extract is recorded with its hit identity"),
			H.Resource->ApplyExtractFromKinsectHit(
				DamageTarget.Hitzone->ExtractColorTag, DamageHit,
				SourceAttackID, DamageHitID, 78));
	}
	else
	{
		AddError(TEXT("failed to spawn telemetry damage target"));
	}

	H.Host->GetCombatTelemetryEventsSince(LastSerial, Events);
	const auto HasEvent = [&Events](const FName Name)
	{
		return Events.ContainsByPredicate([Name](const FWeaponCombatTelemetryEvent& Event)
		{
			return Event.EventName == Name;
		});
	};
	TestTrue(TEXT("TripleUp consumption has a dedicated event"),
		HasEvent(TEXT("TripleCostConsumed")));
	TestTrue(TEXT("successful kinsect damage has a dedicated event"),
		HasEvent(TEXT("KinsectDamageSubmitted")));
	TestTrue(TEXT("extract attempts have a dedicated event"), HasEvent(TEXT("ExtractAttempt")));
	TestTrue(TEXT("extract telemetry includes the consumed extract color"),
		Events.ContainsByPredicate([](const FWeaponCombatTelemetryEvent& Event)
		{
			return Event.EventName == TEXT("ExtractAttempt")
				&& Event.ResourceTag == FName(TEXT("WeaponResource.IG.Extract.White"))
				&& Event.Reason == FName(TEXT("IgnoredDuringTripleUp"));
		}));

	CaptureVariable->Set(PreviousCaptureValue, ECVF_SetByCode);
	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM6PowderOwnershipReservationAndCommit,
	"MHGZ.M6.Resources.PowderOwnershipReservationAndCommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM6EmptyPowderGatherPreflightIsReadOnly,
	"MHGZ.M6.Resources.EmptyPowderGatherIsReadOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM6EmptyPowderGatherPreflightIsReadOnly::RunTest(
	const FString& Parameters)
{
	FMHGZM3Harness H;
	if (!H.Setup())
	{
		AddError(TEXT("M3 runtime harness setup failed"));
		return false;
	}

	H.CombatConfig->PowderGatherRadius = 100.0f;
	H.CombatConfig->PowderGatherMaxCount = 5;
	const FVector Center = H.Kinsect->GetActorLocation();
	TestTrue(TEXT("the resource begins with no registered powder"),
		H.Resource->GetPowderSnapshots().IsEmpty());
	TestFalse(TEXT("empty powder inventory fails the read-only gather preflight"),
		H.Resource->CanGatherPowders(Center));

	FWeaponActionToken Action = H.MakeActionToken(UMHGZSendKinsectAbility::StaticClass());
	TestTrue(TEXT("the test action is registered before attempting reservation"),
		H.Host->RegisterAction(Action));
	FIGPowderReservationToken Reservation;
	TestFalse(TEXT("empty inventory cannot create a powder reservation"),
		H.Resource->TryReservePowders(Action, Center, Reservation));
	TestFalse(TEXT("failed reservation leaves the output token invalid"),
		Reservation.IsValid());
	TestTrue(TEXT("failed preflight leaves powder inventory unchanged"),
		H.Resource->GetPowderSnapshots().IsEmpty());

	H.Teardown();
	return true;
}

bool FMHGZM6PowderOwnershipReservationAndCommit::RunTest(const FString& Parameters)
{
	FMHGZM3Harness H;
	if (!H.Setup())
	{
		AddError(TEXT("M3 runtime harness setup failed"));
		return false;
	}

	H.CombatConfig->PowderActorClass = AIGPowderActor::StaticClass();
	H.CombatConfig->PowderLifetime = 30.0f;
	H.CombatConfig->PowderDamageContribution = 5.0f;
	H.CombatConfig->PowderVortexBaseDamage = 10.0f;
	H.CombatConfig->PowderVortexExplosionRadius = 240.0f;
	H.CombatConfig->PowderVortexBaseStagger = 2.0f;
	H.CombatConfig->PowderGatherRadius = 100.0f;
	H.CombatConfig->PowderGatherMaxCount = 2;

	const FGuid AttackID = FGuid::NewGuid();
	TArray<FGuid> HitIDs;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		AActor* Target = H.World->SpawnActor<AActor>(AActor::StaticClass(),
			FVector(20.0f * (Index + 1), 0.0f, 0.0f), FRotator::ZeroRotator);
		if (!Target)
		{
			AddError(TEXT("failed to spawn test hit target"));
			H.Teardown();
			return false;
		}
		HitIDs.Add(FGuid::NewGuid());
		const FHitResult Hit = MakePowderSourceHit(*Target,
			FVector(20.0f * (Index + 1), 0.0f, 0.0f));
		TestTrue(FString::Printf(TEXT("successful hit %d spawns one powder"), Index),
			H.Resource->SpawnPowderForSuccessfulHit(
				H.Host->GetCurrentToken(), AttackID, HitIDs.Last(), Hit));
	}
	const FHitResult DuplicateHit = MakePowderSourceHit(
		*H.Character, FVector(20.0f, 0.0f, 0.0f));
	TestFalse(TEXT("same successful HitID cannot spawn a second powder"),
		H.Resource->SpawnPowderForSuccessfulHit(
			H.Host->GetCurrentToken(), AttackID, HitIDs[0], DuplicateHit));
	TestTrue(TEXT("three owned powders can be gathered"),
		H.Resource->CanGatherPowders(FVector::ZeroVector));

	FWeaponActionToken ActionA = H.MakeActionToken(UMHGZSendKinsectAbility::StaticClass());
	FIGPowderReservationToken ReservationA;
	TestFalse(TEXT("unregistered action cannot reserve owned powders"),
		H.Resource->TryReservePowders(ActionA, FVector::ZeroVector, ReservationA));
	TestTrue(TEXT("first action registers with the active runtime"),
		H.Host->RegisterAction(ActionA));
	TestTrue(TEXT("first gather transaction reserves its nearest fixed set"),
		H.Resource->TryReservePowders(ActionA, FVector::ZeroVector, ReservationA));
	TestTrue(TEXT("reservation token includes runtime/action identity"), ReservationA.IsValid());
	const TArray<FIGPowderRuntimeSnapshot> AfterFirstReserve = H.Resource->GetPowderSnapshots();
	int32 ReservedCount = 0;
	for (const FIGPowderRuntimeSnapshot& Snapshot : AfterFirstReserve)
	{
		ReservedCount += Snapshot.State == EIGPowderState::Reserved ? 1 : 0;
	}
	TestEqual(TEXT("exactly the configured maximum is reserved"), ReservedCount, 2);

	FWeaponActionToken ActionB = H.MakeActionToken(UMHGZSendKinsectAbility::StaticClass());
	FIGPowderReservationToken ReservationB;
	TestTrue(TEXT("second action registers with the active runtime"),
		H.Host->RegisterAction(ActionB));
	TestTrue(TEXT("competing gather reserves only the remaining unreserved powder"),
		H.Resource->TryReservePowders(ActionB, FVector::ZeroVector, ReservationB));
	TestTrue(TEXT("first reservation enters Gathering only once"),
		H.Resource->BeginPowderGathering(ReservationA));
	TestFalse(TEXT("duplicate GatherBegin is rejected"),
		H.Resource->BeginPowderGathering(ReservationA));
	TestTrue(TEXT("gather presentation advances to the frozen center"),
		H.Resource->UpdatePowderGathering(ReservationA, 1.0f));
	TestTrue(TEXT("second reservation enters Gathering"),
		H.Resource->BeginPowderGathering(ReservationB));
	FIGPowderExplosionSnapshot ExplosionA;
	TestTrue(TEXT("first transaction commits"),
		H.Resource->CommitPowderReservation(ReservationA, ExplosionA));
	TestEqual(TEXT("first explosion consumes the fixed two-member set"),
		ExplosionA.ConsumedCount, 2);
	TestEqual(TEXT("contributions were snapshotted at creation"),
		ExplosionA.TotalDamage, 20.0f);
	TestEqual(TEXT("radius is frozen in the result"),
		ExplosionA.Radius, 240.0f);
	TestFalse(TEXT("committed reservation cannot explode twice"),
		H.Resource->CommitPowderReservation(ReservationA, ExplosionA));

	FIGPowderExplosionSnapshot ExplosionB;
	TestTrue(TEXT("second transaction commits independently"),
		H.Resource->CommitPowderReservation(ReservationB, ExplosionB));
	TestEqual(TEXT("second transaction consumes only remaining member"),
		ExplosionB.ConsumedCount, 1);
	TestEqual(TEXT("single remaining powder contributes once"),
		ExplosionB.TotalDamage, 15.0f);
	TestTrue(TEXT("all consumed powder actors leave the resource registry"),
		H.Resource->GetPowderSnapshots().IsEmpty());

	// A fixed set never grows during gather; expired members are not resurrected,
	// while Commit consumes the surviving members at their actual count.
	H.CombatConfig->PowderLifetime = 30.0f;
	AActor* FirstTarget = H.World->SpawnActor<AActor>(AActor::StaticClass(),
		FVector(50.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	AActor* SecondTarget = H.World->SpawnActor<AActor>(AActor::StaticClass(),
		FVector(60.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	AActor* FinalTarget = H.World->SpawnActor<AActor>(AActor::StaticClass(),
		FVector(70.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!FirstTarget || !SecondTarget || !FinalTarget)
	{
		AddError(TEXT("failed to spawn fixed-set powder test targets"));
		H.Teardown();
		return false;
	}
	const FGuid FirstHitID = FGuid::NewGuid();
	const FGuid SecondHitID = FGuid::NewGuid();
	const FGuid FinalHitID = FGuid::NewGuid();
	const FHitResult FirstHit = MakePowderSourceHit(*FirstTarget, FVector(50.0f, 0.0f, 0.0f));
	const FHitResult SecondHit = MakePowderSourceHit(*SecondTarget, FVector(60.0f, 0.0f, 0.0f));
	const FHitResult FinalHit = MakePowderSourceHit(*FinalTarget, FVector(70.0f, 0.0f, 0.0f));
	TestTrue(TEXT("first fixed-set powder spawns"),
		H.Resource->SpawnPowderForSuccessfulHit(
			H.Host->GetCurrentToken(), AttackID, FirstHitID, FirstHit));
	TestTrue(TEXT("second fixed-set powder spawns"),
		H.Resource->SpawnPowderForSuccessfulHit(
			H.Host->GetCurrentToken(), AttackID, SecondHitID, SecondHit));
	TestTrue(TEXT("third powder spawns outside the chosen fixed set"),
		H.Resource->SpawnPowderForSuccessfulHit(
			H.Host->GetCurrentToken(), AttackID, FinalHitID, FinalHit));
	TWeakObjectPtr<AIGPowderActor> FinalPowder;
	for (TActorIterator<AIGPowderActor> It(H.World); It; ++It)
	{
		if ((*It)->GetSourceHitInstanceID() == FinalHitID)
		{
			FinalPowder = *It;
			break;
		}
	}
	TestTrue(TEXT("final powder actor was found"), FinalPowder.IsValid());
	FWeaponActionToken ActionC = H.MakeActionToken(UMHGZSendKinsectAbility::StaticClass());
	TestTrue(TEXT("powder transaction action registers"), H.Host->RegisterAction(ActionC));
	FIGPowderReservationToken ReservationC;
	TestTrue(TEXT("action reserves the nearest fixed two-powder set"),
		H.Resource->TryReservePowders(ActionC, FVector(50.0f, 0.0f, 0.0f), ReservationC));
	TestTrue(TEXT("fixed-set transaction enters Gathering"),
		H.Resource->BeginPowderGathering(ReservationC));
	TWeakObjectPtr<AIGPowderActor> ExpiredMember;
	for (TActorIterator<AIGPowderActor> It(H.World); It; ++It)
	{
		if (*It != FinalPowder.Get() && (*It)->IsReservedBy(ReservationC))
		{
			ExpiredMember = *It;
			break;
		}
	}
	TestTrue(TEXT("second fixed-set member was reserved"), ExpiredMember.IsValid());
	if (ExpiredMember.IsValid())
	{
		ExpiredMember->Destroy();
	}
	FIGPowderExplosionSnapshot PartialExplosion;
	TestTrue(TEXT("commit succeeds for surviving members of the fixed set"),
		H.Resource->CommitPowderReservation(ReservationC, PartialExplosion));
	TestEqual(TEXT("commit reports the actual surviving powder count"),
		PartialExplosion.ConsumedCount, 1);
	TestEqual(TEXT("expired reserved powder is excluded from damage"),
		PartialExplosion.TotalDamage, 15.0f);
	if (FinalPowder.IsValid())
	{
		TestEqual(TEXT("unselected powder remains Available"),
			FinalPowder->GetPowderState(), EIGPowderState::Available);
		TestTrue(TEXT("same live action can reserve the unselected powder"),
			H.Resource->TryReservePowders(ActionC, FVector(50.0f, 0.0f, 0.0f), ReservationC));
		TestTrue(TEXT("re-reserved powder enters Gathering"),
			H.Resource->BeginPowderGathering(ReservationC));
		const FTransform OriginalTransform = FinalPowder->GetActorTransform();
		TestTrue(TEXT("reserved powder can move for gather presentation"),
			FinalPowder->SetGatherPresentationLocation(
				ReservationC, OriginalTransform.GetLocation() + FVector(0.0f, 0.0f, 100.0f)));
		TestTrue(TEXT("gather presentation moved the actor"),
			!FinalPowder->GetActorLocation().Equals(OriginalTransform.GetLocation()));
		H.Resource->ReleasePowderReservation(ReservationC);
		TestEqual(TEXT("cancel returns the powder to Available"),
			FinalPowder->GetPowderState(), EIGPowderState::Available);
		TestTrue(TEXT("cancel restores the powder's original world transform"),
			FinalPowder->GetActorTransform().Equals(OriginalTransform));
	}
	FWeaponActionToken ActionD = H.MakeActionToken(UMHGZSendKinsectAbility::StaticClass());
	TestTrue(TEXT("stale-action test registers"), H.Host->RegisterAction(ActionD));
	FIGPowderReservationToken ReservationD;
	TestTrue(TEXT("stale-action test reserves powder"),
		H.Resource->TryReservePowders(ActionD, FVector(50.0f, 0.0f, 0.0f), ReservationD));
	TestTrue(TEXT("ending the action unregisters its identity"), H.Host->UnregisterAction(ActionD));
	FIGPowderExplosionSnapshot StaleExplosion;
	TestFalse(TEXT("inactive action cannot commit its powder reservation"),
		H.Resource->CommitPowderReservation(ReservationD, StaleExplosion));
	H.Resource->ReleasePowderReservation(ReservationD);
	H.Teardown();
	TestFalse(TEXT("runtime shutdown destroys its owned powder"), FinalPowder.IsValid());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
