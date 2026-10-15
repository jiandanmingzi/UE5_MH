// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "AbilitySystemComponent.h"
#include "ActionSystem/MHGZDamageGameplayEffect.h"
#include "ActionSystem/MHGZGameplayEffectContext.h"
#include "ActionSystem/MHGZAttackAbility.h"
#include "ActionSystem/MHGZM1PlaceholderAbilities.h"
#include "ActionSystem/MHGZPowderVortexAbility.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "AttributeSystem/Res_InsectGlaive.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "InsectGlaive/IGPowderActor.h"
#include "InsectGlaive/Kinsect/Kinsect.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"
#include "../../Shared/MHGZM3TestHarness.h"

namespace
{
struct FVortexTestTarget
{
	AActor* Actor = nullptr;
	UAbilitySystemComponent* ASC = nullptr;
	UMHGZAttributeSet* Attributes = nullptr;
	UMHGZMonsterHitzoneComponent* FirstHitzone = nullptr;
	UMHGZMonsterHitzoneComponent* SecondHitzone = nullptr;
};

UMHGZMonsterHitzoneComponent* AddHitzone(AActor& Actor,
	USceneComponent* Parent, const FVector& Location)
{
	UMHGZMonsterHitzoneComponent* Hitzone =
		NewObject<UMHGZMonsterHitzoneComponent>(&Actor, NAME_None, RF_Transient);
	if (!Hitzone)
	{
		return nullptr;
	}
	Actor.AddInstanceComponent(Hitzone);
	if (Parent)
	{
		Hitzone->SetupAttachment(Parent);
	}
	else
	{
		Actor.SetRootComponent(Hitzone);
	}
	Hitzone->HitzoneTag = M3::Tag(TEXT("Hitzone.Head"));
	Hitzone->DefenseMultiplier = 10.0f;
	Hitzone->SetSphereRadius(20.0f);
	Hitzone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Hitzone->SetCollisionResponseToAllChannels(ECR_Ignore);
	Hitzone->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block);
	Hitzone->RegisterComponent();
	Hitzone->SetWorldLocation(Location);
	return Hitzone;
}

FVortexTestTarget SpawnDamageTarget(UWorld& World, const FVector& Location,
	const bool bAddSecondHitzone = true)
{
	FVortexTestTarget Target;
	Target.Actor = World.SpawnActor<AActor>(AActor::StaticClass(), Location,
		FRotator::ZeroRotator);
	if (!Target.Actor)
	{
		return Target;
	}
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
	Target.Attributes->InitMaxHealth(1000.0f);
	Target.Attributes->InitHealth(1000.0f);
	Target.Attributes->InitDefense(0.0f);
	Target.FirstHitzone = AddHitzone(*Target.Actor, nullptr, Location);
	if (bAddSecondHitzone && Target.FirstHitzone)
	{
		Target.SecondHitzone = AddHitzone(*Target.Actor, Target.FirstHitzone,
			Location + FVector(8.0f, 0.0f, 0.0f));
	}
	return Target;
}

bool ConfigureVortex(FMHGZM3Harness& Harness)
{
	Harness.CombatConfig->PowderActorClass = AIGPowderActor::StaticClass();
	Harness.CombatConfig->PowderLifetime = 30.0f;
	Harness.CombatConfig->PowderDamageContribution = 5.0f;
	Harness.CombatConfig->PowderGatherRadius = 200.0f;
	Harness.CombatConfig->PowderGatherMaxCount = 1; // legacy cap must not limit M6.3.4
	Harness.CombatConfig->PowderVortexDelay = 1.0f;
	Harness.CombatConfig->PowderVortexDamageAt1 = 10.0f;
	Harness.CombatConfig->PowderVortexDamageAt5 = 50.0f;
	Harness.CombatConfig->PowderVortexDamageAt10 = 100.0f;
	Harness.CombatConfig->PowderVortexHitCueTag =
		M3::Tag(TEXT("GameplayCue.Hit.IG.Powder"));
	Harness.CombatConfig->PowderVortexExplosionCueTag =
		M3::Tag(TEXT("GameplayCue.IG.PowderVortex"));
	Harness.CombatConfig->PowderDespawnDistance = 0.0f;
	return Harness.CombatConfig->PowderVortexHitCueTag.IsValid()
		&& Harness.CombatConfig->PowderVortexExplosionCueTag.IsValid();
}

bool SpawnPowder(FMHGZM3Harness& Harness, const FVector& Location)
{
	FHitResult SourceHit(Harness.Character, nullptr, Location, FVector::UpVector);
	SourceHit.bBlockingHit = true;
	SourceHit.Location = Location;
	SourceHit.ImpactPoint = Location;
	SourceHit.ImpactNormal = FVector::UpVector;
	return Harness.Resource->SpawnPowderForSuccessfulHit(
		Harness.Host->GetCurrentToken(), FGuid::NewGuid(), FGuid::NewGuid(), SourceHit);
}

FKinsectFlightRequest MakeVortexRequest(FMHGZM3Harness& Harness,
	const FVector& Center)
{
	FKinsectFlightRequest Request;
	Request.RuntimeToken = Harness.Host->GetCurrentToken();
	Request.ActionActivationSequenceID = Harness.Host->AllocateActivationSequenceID();
	Request.CommandSource = EKinsectCommandSource::PowderVortex;
	Request.TargetPointSnapshot = Center;
	return Request;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM6PowderVortexMovementPolicyTest,
	"MHGZ.M6.PowderVortex.AllowsLocomotionDuringAction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM6PowderVortexMovementPolicyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const UMHGZAttackAbility* StandardAttack =
		UMHGZAttackAbility::StaticClass()->GetDefaultObject<UMHGZAttackAbility>();
	const UMHGZPowderVortexAbility* PowderVortex =
		UMHGZPowderVortexAbility::StaticClass()->GetDefaultObject<UMHGZPowderVortexAbility>();
	TestNotNull(TEXT("standard attack CDO exists"), StandardAttack);
	TestNotNull(TEXT("Powder Vortex CDO exists"), PowderVortex);
	if (StandardAttack && PowderVortex)
	{
		TestTrue(TEXT("ordinary attacks retain their movement lock"),
			StandardAttack->bBlockCharacterMovementDuringAction);
		TestFalse(TEXT("Powder Vortex remains mobile like Kinsect commands"),
			PowderVortex->bBlockCharacterMovementDuringAction);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM6PowderVortexTransactionTests,
	"MHGZ.M6.PowderVortex.TiersFreezeAllAndCommitTwoIndependentStages",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM6PowderVortexTransactionTests::RunTest(const FString& Parameters)
{
	(void)Parameters;
	struct FTierCase
	{
		int32 Count;
		float ExpectedDirectDamage;
	};
	const FTierCase Cases[] = {
		{ 1, 10.0f }, { 4, 10.0f }, { 5, 50.0f },
		{ 9, 50.0f }, { 10, 100.0f }
	};

	for (const FTierCase& Case : Cases)
	{
		FMHGZM3Harness Harness;
		if (!Harness.Setup() || !ConfigureVortex(Harness))
		{
			AddError(TEXT("M3 runtime or Powder Vortex config setup failed"));
			Harness.Teardown();
			return false;
		}

		const FVector Center(700.0f, 0.0f, 100.0f);
		FVortexTestTarget Target = SpawnDamageTarget(*Harness.World, Center);
		FVortexTestTarget OutsideTarget = SpawnDamageTarget(*Harness.World,
			Center + FVector(350.0f, 0.0f, 0.0f), false);
		if (!TestNotNull(TEXT("in-radius target has a registered Hitzone"),
			Target.FirstHitzone)
			|| !TestNotNull(TEXT("in-radius target has an ASC"), Target.ASC)
			|| !TestNotNull(TEXT("out-of-radius target has a Hitzone"),
				OutsideTarget.FirstHitzone))
		{
			Harness.Teardown();
			return false;
		}

		for (int32 Index = 0; Index < Case.Count; ++Index)
		{
			TestTrue(TEXT("each initial in-radius powder is registered"),
				SpawnPowder(Harness, Center + FVector(0.0f, 10.0f * Index, 0.0f)));
		}
		TestTrue(TEXT("an out-of-radius powder is registered before arrival"),
			SpawnPowder(Harness, Center + FVector(300.0f, 0.0f, 0.0f)));

		FKinsectFlightRequest Request = MakeVortexRequest(Harness, Center);
		Request.CommandSerial = Harness.Resource->AdvanceKinsectCommand(
			EKinsectCommandSource::PowderVortex);
		Harness.Resource->ActivePowderVortexCommandSerial = Request.CommandSerial;
		TestTrue(TEXT("arrival freezes the complete available set, ignoring legacy MaxCount"),
			Harness.Resource->BeginPowderVortexGathering(Request, Center));

		URes_InsectGlaive::FReservedPowderSet* Reservation =
			Harness.Resource->PowderReservations.Find(
				Harness.Resource->ActivePowderVortexReservationID);
		if (!TestNotNull(TEXT("arrival creates a Resource-owned reservation"), Reservation))
		{
			Harness.Teardown();
			return false;
		}
		TestEqual(TEXT("all powders available at the arrival instant are frozen"),
			Reservation->Members.Num(), Case.Count);
		TestTrue(TEXT("the delay begins at arrival and is exactly one world second"),
			FMath::IsNearlyEqual(static_cast<float>(Reservation->GatherDeadlineWorldTime
				- Reservation->CreatedWorldTime), 1.0f, 0.001f));
		for (const FIGPowderRuntimeSnapshot& Snapshot : Harness.Resource->GetPowderSnapshots())
		{
			if (Snapshot.State == EIGPowderState::Reserved)
			{
				TestTrue(TEXT("arrival refreshes each reserved powder to 180 seconds"),
					Snapshot.RemainingLifetime > 179.0f);
			}
		}

		// Spawn after the arrival snapshot: this powder must remain outside this transaction.
		TestTrue(TEXT("a late powder may spawn without joining the frozen set"),
			SpawnPowder(Harness, Center + FVector(50.0f, 0.0f, 0.0f)));

		int32 AppliedGECount = 0;
		TArray<FGuid> StageIDs;
		TArray<float> AppliedAttackPower;
		const FDelegateHandle DamageDelegate =
			Target.ASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
				[&AppliedGECount, &StageIDs, &AppliedAttackPower](UAbilitySystemComponent*,
					const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
				{
					++AppliedGECount;
					const FMHGZGameplayEffectContext* Context =
						FMHGZGameplayEffectContext::ExtractEffectContext(
							Spec.GetEffectContext());
					StageIDs.Add(Context ? Context->AttackInstanceID : FGuid());
					AppliedAttackPower.Add(Spec.GetSetByCallerMagnitude(
						M3::Tag(TEXT("Damage.AttackPower")), true, -1.0f));
				});

		// A premature/stale callback is a no-op; the arrival-relative deadline remains owned.
		Harness.Resource->CommitPowderVortexTransaction(Request.CommandSerial);
		TestEqual(TEXT("commit cannot run before the one-second arrival deadline"),
			Harness.Resource->GetPowderSnapshots().Num(), Case.Count + 2);
		TestTrue(TEXT("early callback applies no damage"),
			FMath::IsNearlyEqual(Target.Attributes->GetHealth(), 1000.0f));

		Reservation = Harness.Resource->PowderReservations.Find(
			Harness.Resource->ActivePowderVortexReservationID);
		if (Reservation)
		{
			Reservation->GatherDeadlineWorldTime = Harness.World->GetTimeSeconds();
		}
		Harness.Resource->CommitPowderVortexTransaction(Request.CommandSerial);
		Target.ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(DamageDelegate);

		const float ExpectedContributionDamage = 5.0f * Case.Count;
		const float ExpectedHealth = 1000.0f - Case.ExpectedDirectDamage
			- ExpectedContributionDamage;
		TestEqual(TEXT("both stage damages are applied to the one in-radius Actor"),
			Target.Attributes->GetHealth(), ExpectedHealth);
		TestTrue(TEXT("the 300cm sphere excludes a target outside its boundary"),
			FMath::IsNearlyEqual(OutsideTarget.Attributes->GetHealth(), 1000.0f));
		TestEqual(TEXT("each independent stage applies at most one GE per Actor"),
			AppliedGECount, 2);
		TestEqual(TEXT("two damage stages carry two AttackIDs"), StageIDs.Num(), 2);
		if (StageIDs.Num() == 2)
		{
			TestTrue(TEXT("stage AttackIDs are distinct"),
				StageIDs[0].IsValid() && StageIDs[1].IsValid()
				&& StageIDs[0] != StageIDs[1]);
		}
		TestEqual(TEXT("stage one uses the count-tier direct damage"),
			AppliedAttackPower.Num(), 2);
		if (AppliedAttackPower.Num() == 2)
		{
			TestTrue(TEXT("stage one damage matches its 1/5/10 tier"),
				FMath::IsNearlyEqual(AppliedAttackPower[0], Case.ExpectedDirectDamage));
			TestTrue(TEXT("stage two damage sums each powder's immutable Di"),
				FMath::IsNearlyEqual(AppliedAttackPower[1], ExpectedContributionDamage));
		}
		const TArray<FIGPowderRuntimeSnapshot> Remaining =
			Harness.Resource->GetPowderSnapshots();
		TestEqual(TEXT("only the pre-arrival outside powder and late powder remain"),
			Remaining.Num(), 2);
		for (const FIGPowderRuntimeSnapshot& Snapshot : Remaining)
		{
			TestEqual(TEXT("non-reserved powders remain available"),
				Snapshot.State, EIGPowderState::Available);
		}
		Harness.Teardown();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM6PowderVortexConsumesMarkAtGatherStart,
	"MHGZ.M6.PowderVortex.ConsumesMarkAtGatherStart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM6PowderVortexConsumesMarkAtGatherStart::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FMHGZM3Harness Harness;
	if (!Harness.Setup() || !ConfigureVortex(Harness))
	{
		AddError(TEXT("M3 runtime or Powder Vortex config setup failed"));
		Harness.Teardown();
		return false;
	}

	const FVector MarkPoint(500.0f, 0.0f, 100.0f);
	FVortexTestTarget Target = SpawnDamageTarget(*Harness.World, MarkPoint, false);
	if (!TestNotNull(TEXT("target Hitzone exists"), Target.FirstHitzone))
	{
		Harness.Teardown();
		return false;
	}

	FHitResult MarkHit(Target.Actor, Target.FirstHitzone, MarkPoint, FVector::UpVector);
	MarkHit.bBlockingHit = true;
	MarkHit.Location = MarkPoint;
	MarkHit.ImpactPoint = MarkPoint;
	MarkHit.ImpactNormal = FVector::UpVector;
	TestTrue(TEXT("establish a persistent gameplay mark"),
		Harness.Resource->SetKinsectMarkFromMeleeHit(MarkHit));
	FVector MarkLocationBefore = FVector::ZeroVector;
	TestTrue(TEXT("active mark has a world location"),
		Harness.Resource->GetKinsectMarkWorldLocation(MarkLocationBefore));

	// A real powder may coexist with the mark, but remains its own movable actor.
	TestTrue(TEXT("spawn a distinct powder at the marked impact"),
		Harness.Resource->SpawnPowderForSuccessfulHit(
			Harness.Host->GetCurrentToken(), FGuid::NewGuid(), FGuid::NewGuid(), MarkHit));
	TestEqual(TEXT("one powder is registered beside the mark"),
		Harness.Resource->GetPowderSnapshots().Num(), 1);

	FKinsectFlightRequest Request = MakeVortexRequest(Harness, MarkLocationBefore);
	Request.CommandSerial = Harness.Resource->AdvanceKinsectCommand(
		EKinsectCommandSource::PowderVortex);
	Harness.Resource->ActivePowderVortexCommandSerial = Request.CommandSerial;
	TestTrue(TEXT("vortex arrival starts the powder transaction"),
		Harness.Resource->BeginPowderVortexGathering(Request, MarkLocationBefore));
	const uint64 ReservationID = Harness.Resource->ActivePowderVortexReservationID;
	URes_InsectGlaive::FReservedPowderSet* Reservation =
		Harness.Resource->PowderReservations.Find(ReservationID);
	if (!TestNotNull(TEXT("powder transaction is registered"), Reservation))
	{
		Harness.Teardown();
		return false;
	}
	TestEqual(TEXT("only the distinct powder actor enters the transaction"),
		Reservation->Members.Num(), 1);
	TestTrue(TEXT("gather animation changes only the powder presentation"),
		Harness.Resource->UpdatePowderGathering(Reservation->Token, 0.75f));
	TestFalse(TEXT("in-range mark is consumed immediately when gathering starts"),
		Harness.Resource->HasValidKinsectMark());

	Harness.Resource->AdvanceKinsectCommand(EKinsectCommandSource::Player);
	TestFalse(TEXT("cancelling after arrival does not restore the consumed mark"),
		Harness.Resource->HasValidKinsectMark());
	const TArray<FIGPowderRuntimeSnapshot> ReleasedPowders =
		Harness.Resource->GetPowderSnapshots();
	TestEqual(TEXT("cancellation releases the separate powder actor"),
		ReleasedPowders.Num(), 1);
	if (!ReleasedPowders.IsEmpty())
	{
		TestEqual(TEXT("released powder returns to Available"),
			ReleasedPowders[0].State, EIGPowderState::Available);
	}

	// A second mark outside the configured attraction radius is not consumed.
	const FVector OutsideMarkPoint = MarkLocationBefore + FVector(250.0f, 0.0f, 0.0f);
	FVortexTestTarget OutsideTarget = SpawnDamageTarget(
		*Harness.World, OutsideMarkPoint, false);
	if (!TestNotNull(TEXT("outside mark target Hitzone exists"), OutsideTarget.FirstHitzone))
	{
		Harness.Teardown();
		return false;
	}
	FHitResult OutsideMarkHit(OutsideTarget.Actor, OutsideTarget.FirstHitzone,
		OutsideMarkPoint, FVector::UpVector);
	OutsideMarkHit.bBlockingHit = true;
	OutsideMarkHit.Location = OutsideMarkPoint;
	OutsideMarkHit.ImpactPoint = OutsideMarkPoint;
	OutsideMarkHit.ImpactNormal = FVector::UpVector;
	TestTrue(TEXT("establish an out-of-range mark"),
		Harness.Resource->SetKinsectMarkFromMeleeHit(OutsideMarkHit));
	FKinsectFlightRequest OutsideRequest = MakeVortexRequest(Harness, MarkLocationBefore);
	OutsideRequest.CommandSerial = Harness.Resource->AdvanceKinsectCommand(
		EKinsectCommandSource::PowderVortex);
	Harness.Resource->ActivePowderVortexCommandSerial = OutsideRequest.CommandSerial;
	TestTrue(TEXT("vortex can begin a second arrival transaction"),
		Harness.Resource->BeginPowderVortexGathering(
			OutsideRequest, MarkLocationBefore));
	TestTrue(TEXT("mark outside attraction radius remains attached and valid"),
		Harness.Resource->HasValidKinsectMark());
	FVector OutsideMarkLocationAfter = FVector::ZeroVector;
	TestTrue(TEXT("outside mark position is not pulled toward the vortex"),
		Harness.Resource->GetKinsectMarkWorldLocation(OutsideMarkLocationAfter)
		&& OutsideMarkLocationAfter.Equals(OutsideMarkPoint, 0.01f));

	Harness.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM6PowderVortexCancellationPreservesCurrentLocation,
	"MHGZ.M6.PowderVortex.CommandCancelsAndPreservesCurrentLocation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM6PowderVortexCancellationPreservesCurrentLocation::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FMHGZM3Harness Harness;
	if (!Harness.Setup() || !ConfigureVortex(Harness))
	{
		AddError(TEXT("M3 runtime or Powder Vortex config setup failed"));
		Harness.Teardown();
		return false;
	}
	const FVector Center(500.0f, 0.0f, 100.0f);
	TestTrue(TEXT("powder spawns before vortex arrival"),
		SpawnPowder(Harness, Center + FVector(100.0f, 0.0f, 0.0f)));

	FKinsectFlightRequest Request = MakeVortexRequest(Harness, Center);
	Request.CommandSerial = Harness.Resource->AdvanceKinsectCommand(
		EKinsectCommandSource::PowderVortex);
	Harness.Resource->ActivePowderVortexCommandSerial = Request.CommandSerial;
	TestTrue(TEXT("arrival starts the cancellable one-second transaction"),
		Harness.Resource->BeginPowderVortexGathering(Request, Center));
	const uint64 ReservationID = Harness.Resource->ActivePowderVortexReservationID;
	URes_InsectGlaive::FReservedPowderSet* Reservation =
		Harness.Resource->PowderReservations.Find(ReservationID);
	if (!TestNotNull(TEXT("transaction reservation exists"), Reservation))
	{
		Harness.Teardown();
		return false;
	}
	const FIGPowderReservationToken Token = Reservation->Token;
	TestTrue(TEXT("presentation progresses before cancellation"),
		Harness.Resource->UpdatePowderGathering(Token, 0.5f));
	const TArray<FIGPowderRuntimeSnapshot> BeforeCancel =
		Harness.Resource->GetPowderSnapshots();
	const FVector CurrentVisibleLocation = BeforeCancel.IsEmpty()
		? FVector::ZeroVector : BeforeCancel[0].Location;

	Harness.Resource->AdvanceKinsectCommand(EKinsectCommandSource::Player);
	const TArray<FIGPowderRuntimeSnapshot> AfterCancel =
		Harness.Resource->GetPowderSnapshots();
	TestEqual(TEXT("manual command immediately releases the reserved powder"),
		AfterCancel.Num(), 1);
	if (!AfterCancel.IsEmpty())
	{
		TestEqual(TEXT("cancelled powder returns to Available"),
			AfterCancel[0].State, EIGPowderState::Available);
		TestTrue(TEXT("cancellation preserves the current presentation location"),
			AfterCancel[0].Location.Equals(CurrentVisibleLocation, 0.01f));
		TestTrue(TEXT("arrival refresh remains; cancellation does not undo its lifetime"),
			AfterCancel[0].RemainingLifetime > 179.0f);
	}
	TestEqual(TEXT("cancelled reservation is removed"),
		Harness.Resource->PowderReservations.Num(), 0);
	TestEqual(TEXT("cancelled vortex command generation is cleared"),
		Harness.Resource->ActivePowderVortexCommandSerial, uint64(0));
	Harness.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM6PowderVortexZeroPowderDoesNotDamage,
	"MHGZ.M6.PowderVortex.ZeroPowdersProduceNoDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM6PowderVortexZeroPowderDoesNotDamage::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FMHGZM3Harness Harness;
	if (!Harness.Setup() || !ConfigureVortex(Harness))
	{
		AddError(TEXT("M3 runtime or Powder Vortex config setup failed"));
		Harness.Teardown();
		return false;
	}
	const FVector Center(500.0f, 0.0f, 100.0f);
	FVortexTestTarget Target = SpawnDamageTarget(*Harness.World, Center);
	if (!TestNotNull(TEXT("zero-powder target has health"), Target.Attributes))
	{
		Harness.Teardown();
		return false;
	}
	int32 AppliedGECount = 0;
	const FDelegateHandle DamageDelegate =
		Target.ASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
			[&AppliedGECount](UAbilitySystemComponent*, const FGameplayEffectSpec&,
				FActiveGameplayEffectHandle)
			{
				++AppliedGECount;
			});

	FKinsectFlightRequest Request = MakeVortexRequest(Harness, Center);
	Request.CommandSerial = Harness.Resource->AdvanceKinsectCommand(
		EKinsectCommandSource::PowderVortex);
	Harness.Resource->ActivePowderVortexCommandSerial = Request.CommandSerial;
	TestTrue(TEXT("arrival is valid even when no powder is available"),
		Harness.Resource->BeginPowderVortexGathering(Request, Center));
	URes_InsectGlaive::FReservedPowderSet* Reservation =
		Harness.Resource->PowderReservations.Find(
			Harness.Resource->ActivePowderVortexReservationID);
	TestNotNull(TEXT("empty reservation still owns the one-second delay"), Reservation);
	if (Reservation)
	{
		Reservation->GatherDeadlineWorldTime = Harness.World->GetTimeSeconds();
	}
	Harness.Resource->CommitPowderVortexTransaction(Request.CommandSerial);
	Target.ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(DamageDelegate);

	TestEqual(TEXT("zero members produce zero damage GE stages"), AppliedGECount, 0);
	TestTrue(TEXT("zero members leave target health unchanged"),
		FMath::IsNearlyEqual(Target.Attributes->GetHealth(), 1000.0f));
	TestTrue(TEXT("zero-member commit leaves no powder"),
		Harness.Resource->GetPowderSnapshots().IsEmpty());
	Harness.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM6PowderVortexAimsFromSnapshotAndManualRecallSupersedes,
	"MHGZ.M6.PowderVortex.AimSnapshotFlightAndManualRecall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM6PowderVortexAimsFromSnapshotAndManualRecallSupersedes::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FMHGZM3Harness Harness;
	if (!Harness.Setup() || !ConfigureVortex(Harness))
	{
		AddError(TEXT("M3 runtime or Powder Vortex config setup failed"));
		Harness.Teardown();
		return false;
	}

	const FWeaponActionToken Action = Harness.MakeActionToken(
		UMHGZM1PlaceholderActionA::StaticClass());
	if (!TestTrue(TEXT("test ActionToken registers with the RuntimeHost"),
		Harness.Host->RegisterAction(Action)))
	{
		Harness.Teardown();
		return false;
	}
	const FVector InitialPoint = Harness.Kinsect->GetActorLocation()
		+ FVector(500.0f, 0.0f, 0.0f);
	AActor* AimActor = Harness.World->SpawnActor<AActor>(AActor::StaticClass(),
		InitialPoint, FRotator::ZeroRotator);
	UMHGZMonsterHitzoneComponent* AimHitzone = AimActor
		? AddHitzone(*AimActor, nullptr, InitialPoint) : nullptr;
	if (!TestNotNull(TEXT("aim hit component exists"), AimHitzone))
	{
		Harness.Teardown();
		return false;
	}
	FHitResult AimHit(AimActor, AimHitzone, InitialPoint, FVector::BackwardVector);
	AimHit.bBlockingHit = true;
	AimHit.Location = InitialPoint;
	AimHit.ImpactPoint = InitialPoint;
	AimHit.ImpactNormal = FVector::BackwardVector;
	FWeaponAimSnapshot Aim;
	Aim.Context = EWeaponAimSnapshotContext::Kinsect;
	Aim.TargetPoint = InitialPoint;
	Aim.bHasBlockingHit = true;
	Aim.BlockingHit = AimHit;
	Aim.bHasHitResult = true;
	Aim.HitResult = AimHit;

	TestTrue(TEXT("Resource starts ToPoint flight from the input-time hit snapshot"),
		Harness.Resource->StartPowderVortex(Action, Aim));
	TestEqual(TEXT("Kinsect uses the PowderVortex command source"),
		Harness.Kinsect->ActiveRequest.CommandSource,
		EKinsectCommandSource::PowderVortex);
	TestTrue(TEXT("monster target is followed by its component-local impact point"),
		Harness.Kinsect->ActiveRequest.bFollowTargetComponent
		&& Harness.Kinsect->ActiveRequest.TargetComponent.Get() == AimHitzone);
	TestTrue(TEXT("blocking component aim allows near-target surface collision to count as arrival"),
		Harness.Kinsect->ActiveRequest.bAllowWorldHitAtTarget);
	TestEqual(TEXT("flight starts from the snapshot, not a later camera read"),
		Harness.Kinsect->ActiveRequest.TargetPointSnapshot, InitialPoint);

	const FVector TargetMotion(0.0f, 80.0f, 0.0f);
	AimActor->AddActorWorldOffset(TargetMotion);
	Harness.Kinsect->Tick(1.0f / 60.0f);
	TestTrue(TEXT("component-follow target updates from its saved local point"),
		Harness.Kinsect->ActiveRequest.TargetPointSnapshot.Equals(
			InitialPoint + TargetMotion, 0.01f));
	const FVector EarlySurfaceContact = InitialPoint + TargetMotion
		- FVector(250.0f, 0.0f, 0.0f);
	FHitResult SurfaceHit(AimActor, AimHitzone, EarlySurfaceContact,
		FVector::BackwardVector);
	SurfaceHit.bBlockingHit = true;
	SurfaceHit.Location = EarlySurfaceContact;
	SurfaceHit.ImpactPoint = EarlySurfaceContact;
	SurfaceHit.ImpactNormal = FVector::BackwardVector;
	Harness.Kinsect->OnWorldCollision(nullptr, AimActor,
		AimHitzone, FVector::ZeroVector, SurfaceHit);
	Harness.Kinsect->Tick(1.0f / 60.0f);
	TestEqual(TEXT("contact with the captured aim component completes flight even before the center reaches the aim point"),
		Harness.Kinsect->GetState(), EKinsectState::Hovering);
	TestTrue(TEXT("surface arrival starts the Powder Vortex gather transaction"),
		Harness.Resource->ActivePowderVortexReservationID != 0);

	TestTrue(TEXT("manual recall succeeds and supersedes the vortex transaction"),
		Harness.Resource->RecallKinsect(EKinsectCommandSource::Player));
	TestEqual(TEXT("manual recall owns the Kinsect after cancellation"),
		Harness.Kinsect->GetState(), EKinsectState::Returning);
	TestEqual(TEXT("the superseded vortex command serial is invalidated"),
		Harness.Resource->ActivePowderVortexCommandSerial, uint64(0));

	const FWeaponActionToken SecondAction = Harness.MakeActionToken(
		UMHGZM1PlaceholderActionA::StaticClass());
	if (!TestTrue(TEXT("second test ActionToken registers with the RuntimeHost"),
		Harness.Host->RegisterAction(SecondAction)))
	{
		Harness.Teardown();
		return false;
	}
	const FVector SecondTargetPoint = Harness.Kinsect->GetActorLocation()
		+ FVector(500.0f, 0.0f, 0.0f);
	AActor* SecondTargetActor = Harness.World->SpawnActor<AActor>(
		AActor::StaticClass(), SecondTargetPoint, FRotator::ZeroRotator);
	UMHGZMonsterHitzoneComponent* SecondTargetComponent = SecondTargetActor
		? AddHitzone(*SecondTargetActor, nullptr, SecondTargetPoint) : nullptr;
	if (!TestNotNull(TEXT("second aim component exists"), SecondTargetComponent))
	{
		Harness.Teardown();
		return false;
	}
	FHitResult SecondAimHit(SecondTargetActor, SecondTargetComponent,
		SecondTargetPoint, FVector::BackwardVector);
	SecondAimHit.bBlockingHit = true;
	SecondAimHit.Location = SecondTargetPoint;
	SecondAimHit.ImpactPoint = SecondTargetPoint;
	FWeaponAimSnapshot SecondAim;
	SecondAim.Context = EWeaponAimSnapshotContext::Kinsect;
	SecondAim.TargetPoint = SecondTargetPoint;
	SecondAim.bHasBlockingHit = true;
	SecondAim.BlockingHit = SecondAimHit;
	SecondAim.bHasHitResult = true;
	SecondAim.HitResult = SecondAimHit;
	TestTrue(TEXT("second Powder Vortex flight starts"),
		Harness.Resource->StartPowderVortex(SecondAction, SecondAim));
	AActor* OccluderActor = Harness.World->SpawnActor<AActor>(
		AActor::StaticClass(), SecondTargetPoint - FVector(250.0f, 0.0f, 0.0f),
		FRotator::ZeroRotator);
	UMHGZMonsterHitzoneComponent* OccluderComponent = OccluderActor
		? AddHitzone(*OccluderActor, nullptr,
			SecondTargetPoint - FVector(250.0f, 0.0f, 0.0f)) : nullptr;
	if (!TestNotNull(TEXT("unrelated occluder component exists"), OccluderComponent))
	{
		Harness.Teardown();
		return false;
	}
	const FVector OccluderPoint = SecondTargetPoint - FVector(250.0f, 0.0f, 0.0f);
	FHitResult OccluderHit(OccluderActor, OccluderComponent, OccluderPoint,
		FVector::BackwardVector);
	OccluderHit.bBlockingHit = true;
	OccluderHit.Location = OccluderPoint;
	OccluderHit.ImpactPoint = OccluderPoint;
	Harness.Kinsect->OnWorldCollision(nullptr, OccluderActor,
		OccluderComponent, FVector::ZeroVector, OccluderHit);
	Harness.Kinsect->Tick(1.0f / 60.0f);
	TestEqual(TEXT("an unrelated blocker still terminates the aimed flight"),
		Harness.Resource->ActivePowderVortexCommandSerial, uint64(0));
	TestEqual(TEXT("unrelated blockers do not start gathering"),
		Harness.Resource->ActivePowderVortexReservationID, uint64(0));
	Harness.Teardown();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
