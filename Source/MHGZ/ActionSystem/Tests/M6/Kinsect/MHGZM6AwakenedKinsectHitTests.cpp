// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "AttributeSystem/MHGZAttributeSet.h"
#include "InsectGlaive/IGPowderActor.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"
#include "Monster/MHGZTrainingDummy.h"
#include "../../Shared/MHGZM3TestHarness.h"

namespace
{
UMHGZMonsterHitzoneComponent* AddHitzone(AActor& Owner,
	const FVector& Location, const FGameplayTag& ExtractColor)
{
	UMHGZMonsterHitzoneComponent* Hitzone =
		NewObject<UMHGZMonsterHitzoneComponent>(&Owner);
	if (!Hitzone)
	{
		return nullptr;
	}
	Owner.AddInstanceComponent(Hitzone);
	if (USceneComponent* Root = Owner.GetRootComponent())
	{
		Hitzone->SetupAttachment(Root);
	}
	else
	{
		Owner.SetRootComponent(Hitzone);
	}
	Hitzone->ExtractColorTag = ExtractColor;
	Hitzone->SetCollisionObjectType(ECC_GameTraceChannel3);
	Hitzone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Hitzone->SetCollisionResponseToAllChannels(ECR_Block);
	Hitzone->RegisterComponent();
	Hitzone->SetSphereRadius(38.0f);
	Hitzone->SetWorldLocation(Location);
	return Hitzone;
}

FKinsectFlightRequest MakeAwakenedRequest(const FMHGZM3Harness& H)
{
	FKinsectFlightRequest Request;
	Request.RuntimeToken = H.Host->GetCurrentToken();
	Request.TrajectoryMode = EKinsectTrajectoryMode::AlongDirection;
	Request.DirectionSnapshot = FVector::ForwardVector;
	Request.MaxDistance = 1000.0f;
	Request.FlightSpeed = 2000.0f;
	Request.ArrivalRadius = 40.0f;
	Request.DamageMode = EKinsectDamageMode::Piercing;
	Request.ExtractMode = EKinsectExtractMode::CollectPerValidHit;
	Request.PostFlightPolicy = EKinsectPostFlightPolicy::ReturnToOwner;
	Request.MotionValue = 30.0f;
	Request.RehitInterval = 0.20f;
	Request.FlightInstanceID = FGuid::NewGuid();
	Request.PowderSpawnPolicy = EIGPowderSpawnPolicy::AwakenedPiercing;
	Request.SourceAttackInstanceID = FGuid::NewGuid();
	return Request;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM6OrdinaryAwakenedKinsectDefersReturnUntilExplicitRecall,
	"MHGZ.M6.AwakenedKinsect.OrdinaryHitWaitsForHunterFlightEndToRecall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM6OrdinaryAwakenedKinsectDefersReturnUntilExplicitRecall::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FMHGZM3Harness H;
	if (!H.Setup())
	{
		AddError(TEXT("M3 runtime harness setup failed"));
		return false;
	}

	const FVector StartLocation = H.Kinsect->GetActorLocation();
	AMHGZTrainingDummy* Target = H.World->SpawnActor<AMHGZTrainingDummy>(
		AMHGZTrainingDummy::StaticClass(),
		StartLocation + FVector(190.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("ordinary hit target spawns"), Target)
		|| !TestNotNull(TEXT("ordinary hit target ASC exists"),
			Target->GetAbilitySystemComponent())
		|| !TestNotNull(TEXT("ordinary hit target attributes exist"),
			Target->GetAttributeSet())
		|| !TestNotNull(TEXT("ordinary hitzone is configured"),
			AddHitzone(*Target, StartLocation + FVector(190.0f, 0.0f, 0.0f),
				M3::Tag(TEXT("WeaponResource.IG.Extract.White")))))
	{
		H.Teardown();
		return false;
	}
	Target->GetAbilitySystemComponent()->InitAbilityActorInfo(Target, Target);
	Target->GetAttributeSet()->InitMaxHealth(1000.0f);
	Target->GetAttributeSet()->InitHealth(1000.0f);

	FKinsectFlightRequest Request = MakeAwakenedRequest(H);
	Request.DamageMode = EKinsectDamageMode::SingleHit;
	Request.ExtractMode = EKinsectExtractMode::FirstHitOnly;
	Request.PostFlightPolicy = EKinsectPostFlightPolicy::Hover;
	Request.PowderSpawnPolicy = EIGPowderSpawnPolicy::None;
	Request.SourceAttackInstanceID.Invalidate();
	TestTrue(TEXT("ordinary single-hit flight deploys"),
		H.Resource->DeployKinsect(Request));
	H.Kinsect->SetActorLocation(StartLocation + FVector(300.0f, 0.0f, 0.0f));
	H.Kinsect->Tick(0.016f);
	TestEqual(TEXT("monster hit stops the insect at the hit point without recalling it"),
		H.Kinsect->GetState(), EKinsectState::Hovering);
	TestTrue(TEXT("the first-hit extract remains pending until return"),
		H.Kinsect->HasPendingExtract());
	TestTrue(TEXT("explicit recall at HunterFlight end begins return"),
		H.Resource->RecallKinsect());
	TestEqual(TEXT("insect returns only after the owning action requests recall"),
		H.Kinsect->GetState(), EKinsectState::Returning);

	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM6AwakenedKinsectRequestRequiresExplicitPowderIdentityAndPiercing,
	"MHGZ.M6.AwakenedKinsect.PowderRequiresExplicitPiercingRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM6AwakenedKinsectRequestRequiresExplicitPowderIdentityAndPiercing::RunTest(
	const FString& Parameters)
{
	FMHGZM3Harness H;
	if (!H.Setup())
	{
		AddError(TEXT("M3 runtime harness setup failed"));
		return false;
	}

	const FVector StartLocation = H.Kinsect->GetActorLocation();
	FKinsectFlightRequest Request = MakeAwakenedRequest(H);
	Request.SourceAttackInstanceID.Invalidate();
	TestFalse(TEXT("powder-enabled request without source attack identity is rejected"),
		H.Kinsect->BeginFlight(Request));
	TestEqual(TEXT("invalid request leaves the kinsect attached"),
		H.Kinsect->GetState(), EKinsectState::Attached);

	Request = MakeAwakenedRequest(H);
	Request.DamageMode = EKinsectDamageMode::SingleHit;
	TestFalse(TEXT("powder cannot be enabled for a single-hit request"),
		H.Kinsect->BeginFlight(Request));
	TestEqual(TEXT("single-hit rejection leaves the kinsect attached"),
		H.Kinsect->GetState(), EKinsectState::Attached);

	Request = MakeAwakenedRequest(H);
	Request.ExtractMode = EKinsectExtractMode::None;
	TestFalse(TEXT("powder-enabled piercing requires per-valid-hit extract policy"),
		H.Kinsect->BeginFlight(Request));
	TestTrue(TEXT("validation failures do not move the kinsect"),
		H.Kinsect->GetActorLocation().Equals(StartLocation));

	Request = MakeAwakenedRequest(H);
	TestTrue(TEXT("complete explicit piercing request is accepted"),
		H.Kinsect->BeginFlight(Request));
	TestTrue(TEXT("accepted request stores the source attack identity"),
		H.Kinsect->ActiveRequest.SourceAttackInstanceID == Request.SourceAttackInstanceID);

	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM6AwakenedKinsectPowderFollowsOnlySuccessfulPiercingDamage,
	"MHGZ.M6.AwakenedKinsect.PowderAndExtractFollowSuccessfulDamageOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM6AwakenedKinsectPowderFollowsOnlySuccessfulPiercingDamage::RunTest(
	const FString& Parameters)
{
	FMHGZM3Harness H;
	if (!H.Setup())
	{
		AddError(TEXT("M3 runtime harness setup failed"));
		return false;
	}

	H.CombatConfig->PowderActorClass = AIGPowderActor::StaticClass();
	H.CombatConfig->PowderLifetime = 30.0f;
	H.CombatConfig->PowderDamageContribution = 2.0f;
	H.CombatConfig->PowderMotionValue = 50.0f;

	const FVector StartLocation = H.Kinsect->GetActorLocation();
	AActor* MissingASCActor = H.World->SpawnActor<AActor>(AActor::StaticClass(),
		StartLocation + FVector(70.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	AMHGZTrainingDummy* DamageTarget = H.World->SpawnActor<AMHGZTrainingDummy>(
		AMHGZTrainingDummy::StaticClass(),
		StartLocation + FVector(190.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("damage-failure target spawns"), MissingASCActor)
		|| !TestNotNull(TEXT("damage-success target spawns"), DamageTarget)
		|| !TestNotNull(TEXT("target ASC exists"), DamageTarget->GetAbilitySystemComponent())
		|| !TestNotNull(TEXT("target attributes exist"), DamageTarget->GetAttributeSet()))
	{
		H.Teardown();
		return false;
	}

	DamageTarget->GetAbilitySystemComponent()->InitAbilityActorInfo(
		DamageTarget, DamageTarget);
	DamageTarget->GetAttributeSet()->InitMaxHealth(1000.0f);
	DamageTarget->GetAttributeSet()->InitHealth(1000.0f);
	const FGameplayTag White = M3::Tag(TEXT("WeaponResource.IG.Extract.White"));
	const FGameplayTag Red = M3::Tag(TEXT("WeaponResource.IG.Extract.Red"));
	TestNotNull(TEXT("first hitzone registers without a target ASC"),
		AddHitzone(*MissingASCActor, StartLocation + FVector(70.0f, 0.0f, 0.0f), Red));
	UMHGZMonsterHitzoneComponent* DamageHitzone = AddHitzone(*DamageTarget,
		StartLocation + FVector(190.0f, 0.0f, 0.0f), White);
	TestNotNull(TEXT("second hitzone registers with a valid damage target"), DamageHitzone);
	if (!DamageHitzone)
	{
		H.Teardown();
		return false;
	}
	FKinsectFlightRequest Request = MakeAwakenedRequest(H);
	Request.MaxDistance = 1000.0f;
	Request.PowderSpawnPolicy = EIGPowderSpawnPolicy::AwakenedPiercing;
	TestTrue(TEXT("powder-enabled flight deploys"), H.Resource->DeployKinsect(Request));
	H.Kinsect->SetActorLocation(StartLocation + FVector(300.0f, 0.0f, 0.0f));
	H.Kinsect->Tick(0.016f);

	TestFalse(TEXT("successful piercing hit does not grant hunter extract before return"), H.Resource->HasExtract(White));
	TestTrue(TEXT("only successful damage collects cargo"), H.Kinsect->GetPendingExtractColors().HasTagExact(White));
	TestFalse(TEXT("failed damage cannot collect the other color"), H.Kinsect->GetPendingExtractColors().HasTagExact(Red));
	const TArray<FIGPowderRuntimeSnapshot> Powders = H.Resource->GetPowderSnapshots();
	TestEqual(TEXT("only the successful piercing hit spawned exactly one powder"),
		Powders.Num(), 1);
	if (!Powders.IsEmpty())
	{
		TestEqual(TEXT("powder keeps the source attack identity"),
			Powders[0].SourceAttackInstanceID, Request.SourceAttackInstanceID);
		TestTrue(TEXT("powder source hit identity is a distinct valid per-hit id"),
			Powders[0].SourceHitInstanceID.IsValid()
				&& Powders[0].SourceHitInstanceID != Request.SourceAttackInstanceID);
		TestTrue(TEXT("powder damage snapshots Kinsect power and raw-percent motion value"),
			FMath::IsNearlyEqual(Powders[0].DamageContribution, 5.0f));
		TestTrue(TEXT("powder stores the spawn-time Kinsect attack power"),
			FMath::IsNearlyEqual(Powders[0].KinsectAttackPowerSnapshot, 10.0f));
		TestTrue(TEXT("powder stores the exact configured raw-percent motion value"),
			FMath::IsNearlyEqual(Powders[0].MotionValueSnapshot, 50.0f));
		TestEqual(TEXT("powder snapshot records the cooldown-bypassing piercing policy"),
			Powders[0].SpawnPolicy, EIGPowderSpawnPolicy::AwakenedPiercing);
	}

	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM6AwakenedKinsectCargoReturn,
	"MHGZ.M6.AwakenedKinsect.PiercingCargoDefersOneTwoThreeColorsUntilReturn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM6AwakenedKinsectCargoReturn::RunTest(const FString& Parameters)
{
	const FGameplayTag Colors[] = {M3::Tag(TEXT("WeaponResource.IG.Extract.Red")),
		M3::Tag(TEXT("WeaponResource.IG.Extract.White")), M3::Tag(TEXT("WeaponResource.IG.Extract.Orange"))};
	for (int32 ColorCount = 1; ColorCount <= 3; ++ColorCount)
	{
		FMHGZM3Harness H;
		if (!TestTrue(TEXT("cargo harness builds"), H.Setup())) { H.Teardown(); return false; }
		const FVector Start = H.Kinsect->GetActorLocation();
		AMHGZTrainingDummy* Target = H.World->SpawnActor<AMHGZTrainingDummy>(
			AMHGZTrainingDummy::StaticClass(), Start + FVector(300, 0, 0), FRotator::ZeroRotator);
		if (!TestNotNull(TEXT("real damage target"), Target)) { H.Teardown(); return false; }
		Target->GetAbilitySystemComponent()->InitAbilityActorInfo(Target, Target);
		Target->GetAttributeSet()->InitMaxHealth(1000.0f);
		Target->GetAttributeSet()->InitHealth(1000.0f);
		for (int32 Index = 0; Index < ColorCount; ++Index)
		{
			if (!TestNotNull(TEXT("distinct colored hitzone"), AddHitzone(*Target,
				Start + FVector(160 * (Index + 1), 0, 0), Colors[Index]))) { H.Teardown(); return false; }
		}
		FKinsectFlightRequest Request = MakeAwakenedRequest(H);
		Request.PowderSpawnPolicy = EIGPowderSpawnPolicy::None;
		Request.RehitInterval = 0.0f; // Real repeated damage is allowed; cargo must still deduplicate colors.
		if (!TestTrue(TEXT("empowered collect flight deploys"), H.Resource->DeployKinsect(Request)))
		{
			H.Teardown(); return false;
		}
		for (int32 Index = 0; Index < ColorCount; ++Index)
		{
			H.Kinsect->SetActorLocation(Start + FVector(160 * (Index + 1) + 45, 0, 0));
			H.Kinsect->Tick(0.016f);
			const FIGResourceUISnapshot State = H.Resource->GetUIState();
			TestEqual(TEXT("live UI sees each collected color"), State.KinsectPendingExtractColors.Num(), Index + 1);
			TestTrue(TEXT("cargo contains the real hitzone color"), State.KinsectPendingExtractColors.HasTagExact(Colors[Index]));
			TestFalse(TEXT("hunter lamps remain ungranted in flight"), State.bRed || State.bWhite || State.bOrange || State.bTripleUp);
			H.Kinsect->Tick(0.016f);
			TestEqual(TEXT("repeated same-color hit adds no cargo slot"), H.Kinsect->GetPendingExtractColors().Num(), Index + 1);
		}
		TestTrue(TEXT("ordinary capture cannot add a second kind to occupied cargo"),
			!H.Kinsect->CapturePendingExtractColor(Colors[ColorCount == 1 ? 1 : 0]));
		H.Kinsect->Interrupt();
		TestEqual(TEXT("interruption preserves all carried colors"), H.Kinsect->GetPendingExtractColors().Num(), ColorCount);
		TestTrue(TEXT("explicit recall starts"), H.Resource->RecallKinsect());
		TestFalse(TEXT("returning still does not grant buffs"), H.Resource->IsTripleUpActive() || H.Resource->HasExtract(Colors[0]));
		H.Kinsect->SetActorLocation(H.Character->GetMesh()->GetComponentLocation() + FVector(10, 0, 0));
		H.Kinsect->Tick(0.016f);
		TestEqual(TEXT("delivery follows real return-to-arm path"), H.Kinsect->GetState(), EKinsectState::Attached);
		TestFalse(TEXT("all cargo atomically cleared on delivery"), H.Kinsect->HasPendingExtract());
		TestTrue(TEXT("UI cargo cleared on delivery"), H.Resource->GetUIState().KinsectPendingExtractColors.IsEmpty());
		if (ColorCount == 3)
		{
			TestTrue(TEXT("three delivered colors form TripleUp only at return"), H.Resource->IsTripleUpActive());
		}
		else
		{
			for (int32 Index = 0; Index < ColorCount; ++Index)
			{
				TestTrue(TEXT("one/two colors both delivered"), H.Resource->HasExtract(Colors[Index]));
			}
			TestFalse(TEXT("incomplete cargo does not form TripleUp"), H.Resource->IsTripleUpActive());
		}
		H.Kinsect->Tick(0.016f);
		TestTrue(TEXT("later ticks have no cargo to redeliver"), H.Resource->GetUIState().KinsectPendingExtractColors.IsEmpty());
		H.Teardown();
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
