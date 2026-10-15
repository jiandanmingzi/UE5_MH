// Copyright MHGZ Project. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "ActionSystem/AnimNotifyState_AttackCollision.h"
#include "ActionSystem/MHGZAwakenedKinsectAttackAbility.h"
#include "Animation/AnimMontage.h"
#include "Components/BoxComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "InsectGlaive/Kinsect/KinsectCollisionComponent.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"
#include "../../Shared/MHGZM3TestHarness.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZM6AimedContactNotArrival,
	"MHGZ.M6.KinsectAim.ContactMustPrecedeArrival",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZM6AimedContactNotArrival::RunTest(const FString&)
{
	FMHGZM3Harness H;
	if (!H.Setup()) { return false; }
	const FVector Origin = H.Kinsect->GetActorLocation();
	AActor* Target = H.World->SpawnActor<AActor>();
	UMHGZMonsterHitzoneComponent* Zone = NewObject<UMHGZMonsterHitzoneComponent>(Target);
	Target->SetRootComponent(Zone); Target->AddInstanceComponent(Zone);
	Zone->ExtractColorTag = M3::Tag(TEXT("WeaponResource.IG.Extract.Red"));
	Zone->RegisterComponent(); Zone->SetSphereRadius(20.0f);
	Zone->SetWorldLocation(Origin + FVector(250, 0, 0));
	FKinsectFlightRequest R;
	R.RuntimeToken = H.Host->GetCurrentToken(); R.FlightInstanceID = FGuid::NewGuid();
	R.TrajectoryMode = EKinsectTrajectoryMode::ToPoint;
	R.TargetPointSnapshot = Origin + FVector(230, 0, 0); R.FlightSpeed = 2000;
	R.MaxDistance = 1000; R.ArrivalRadius = 50; R.MotionValue = 20.0f;
	R.ExtractMode = EKinsectExtractMode::FirstHitOnly;
	R.bRequireHitzoneContactForArrival = true;
	TestTrue(TEXT("aimed damaging flight starts"), H.Resource->DeployKinsect(R));
	H.Kinsect->SetActorLocation(Origin + FVector(185, 0, 0)); H.Kinsect->Tick(0.016f);
	TestEqual(TEXT("loose 50cm arrival cannot stop a damaging aimed send"), H.Kinsect->GetState(), EKinsectState::Flying);
	H.Kinsect->SetActorLocation(Origin + FVector(235, 0, 0)); H.Kinsect->Tick(0.016f);
	TestEqual(TEXT("actual contact terminates the single hit"), H.Kinsect->GetState(), EKinsectState::Hovering);
	TestTrue(TEXT("actual contact captures single cargo"), H.Kinsect->HasPendingExtract());
	TestEqual(TEXT("cargo snapshot exposes one color"), H.Resource->GetUIState().KinsectPendingExtractColors.Num(), 1);
	TestFalse(TEXT("capture does not grant hunter lamp before return"), H.Resource->HasExtract(Zone->ExtractColorTag));
	H.Teardown(); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZM6FloorRetarget,
	"MHGZ.M6.KinsectAim.FloorHoverCanRetargetWithoutCapsuleTilt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZM6FloorRetarget::RunTest(const FString&)
{
	FMHGZM3Harness H;
	if (!H.Setup()) { return false; }
	AActor* Floor = H.World->SpawnActor<AActor>();
	UBoxComponent* Box = NewObject<UBoxComponent>(Floor);
	Floor->SetRootComponent(Box); Floor->AddInstanceComponent(Box);
	Box->SetBoxExtent(FVector(1500, 1500, 50));
	Box->SetCollisionObjectType(ECC_WorldStatic); Box->SetCollisionResponseToAllChannels(ECR_Block);
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Box->RegisterComponent();
	Box->SetWorldLocation(FVector(0, 0, -50));
	H.Kinsect->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	H.Kinsect->SetActorLocation(FVector(0, 0, 30.1));
	FKinsectFlightRequest R;
	R.RuntimeToken = H.Host->GetCurrentToken(); R.FlightInstanceID = FGuid::NewGuid();
	R.DirectionSnapshot = FVector(1, 0, 1).GetSafeNormal(); R.FlightSpeed = 2000;
	R.MaxDistance = 1000; R.ArrivalRadius = 10;
	H.Kinsect->Interrupt();
	TestTrue(TEXT("ordinary player retarget from floor hover succeeds"), H.Resource->DeployKinsect(R));
	TestTrue(TEXT("physical capsule stays upright"), H.Kinsect->GetActorUpVector().Equals(FVector::UpVector, 0.001));
	TestFalse(TEXT("projectile movement must not pitch the physical capsule"), H.Kinsect->Movement->bRotationFollowsVelocity);
	const FVector Before = H.Kinsect->GetActorLocation();
	H.Kinsect->Movement->TickComponent(0.016f, LEVELTICK_All, nullptr); H.Kinsect->Tick(0.016f);
	TestTrue(TEXT("new command escapes the floor instead of immediate penetration stop"), H.Kinsect->GetActorLocation().Z > Before.Z + 5);
	H.Teardown(); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZM6AwakenedWindowAudit,
	"MHGZ.M6.AwakenedKinsect.AuthoredAttackWindowAudit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZM6AwakenedWindowAudit::RunTest(const FString&)
{
	UClass* Class = LoadClass<UMHGZAwakenedKinsectAttackAbility>(nullptr,
		TEXT("/Game/Blueprints/Ability/InsectGlaive/GA_IG_JueChongJi.GA_IG_JueChongJi_C"));
	const UMHGZAwakenedKinsectAttackAbility* Ability = Class ? Class->GetDefaultObject<UMHGZAwakenedKinsectAttackAbility>() : nullptr;
	if (!TestNotNull(TEXT("actual persistent awakened ability loads"), Ability)) { return false; }
	const UAnimMontage* Montage = Ability->AttackMontage;
	if (!TestNotNull(TEXT("actual authored Montage exists"), Montage)) { return false; }
	int32 Windows = 0;
	for (const FAnimNotifyEvent& Event : Montage->Notifies)
	{
		if (const UAnimNotifyState_AttackCollision* Window = Cast<UAnimNotifyState_AttackCollision>(Event.NotifyStateClass))
		{
			++Windows;
			AddInfo(FString::Printf(TEXT("Authored AttackCollision index=%d start=%.6f end=%.6f"), Window->ConfigIndex, Event.GetTriggerTime(), Event.GetEndTriggerTime()));
		}
	}
	TestEqual(TEXT("one authored collision window is preserved"), Windows, 1);
	for (const FAttackSegmentConfig& Segment : Ability->AttackSegments)
	{
		for (const FWeaponTraceRegion& Region : Segment.Collision.TraceRegions)
		{
			AddInfo(FString::Printf(TEXT("Trace %s -> %s radius=%.2f spacing=%.2f maxSamples=%d"), *Region.StartSocketName.ToString(), *Region.EndSocketName.ToString(), Region.Radius, Region.MaxSampleSpacing, Region.MaxSampleCount));
		}
	}
	return true;
}
#endif
