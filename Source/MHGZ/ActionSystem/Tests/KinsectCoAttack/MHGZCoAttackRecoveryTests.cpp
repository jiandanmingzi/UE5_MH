// Copyright MHGZ Project. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "MHGZCoAttackTestFixture.h"
#include "Components/BoxComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"

using MHGZCoAttackTest::FCoAttackFixture;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZCoAttackDodgeAcceptPoseTest,
	"MHGZ.Maintenance.KinsectCoAttack.Recovery.DodgeAcceptOpeningFreezesPreviousPose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZCoAttackDodgeAcceptPoseTest::RunTest(const FString&)
{
	FCoAttackFixture F;
	if (!TestTrue(TEXT("fixture"), F.Build())) return false;
	F.WarmUp(); F.At(.3, F.Origin); F.Ability->EnableCollision(0);
	const FWeaponActionToken Token = F.Ability->GetActionToken();
	const uint64 Serial = F.Sampler->GetCommandSerial();
	F.At(.35, F.Origin + FVector(100, 0, 0)); F.Ability->DisableCollision(0);
	F.At(.4, F.Origin + FVector(200, 0, 0));
	FWeaponActionToken Wrong = Token; ++Wrong.ActivationSequenceID;
	F.H.Host->OnWeaponDodgeAcceptWindowOpened.Broadcast(Wrong);
	const FVector LastCompletedPose = F.Origin + FVector(250, 0, 0);
	F.At(.42, LastCompletedPose);
	// Simulate NotifyBegin before this frame's pose completion: the socket is
	// already erratic, but the last completed post-pose sample is still valid.
	F.H.World->TimeSeconds = .43;
	F.SetSource(F.Origin + FVector(3000, 500, -300));
	TestTrue(TEXT("real dodge accept opens even without an active combo transition"),
		F.Ability->BeginDodgeAcceptWindow(TEXT("CoAttack.Freeze")));
	F.At(.5, F.Origin + FVector(-3000, 0, 0));
	F.At(.6, F.Origin + FVector(5000, 0, 0));
	F.At(.65, F.Origin + FVector(7000, 0, 0));
	TestTrue(TEXT("replays through the pre-dodge-accept gap, then holds the previous completed frame"),
		F.H.Kinsect->GetActorLocation().Equals(LastCompletedPose, .02f));
	TestTrue(TEXT("opening freezes movement, not the action or recall"), F.Sampler->IsRunning() && F.Ability->IsActive());
	TestTrue(TEXT("duplicate opening succeeds without restarting the boundary"),
		F.Ability->BeginDodgeAcceptWindow(TEXT("CoAttack.Freeze")));
	TestTrue(TEXT("overlapping opening also cannot restart sampling"),
		F.Ability->BeginDodgeAcceptWindow(TEXT("CoAttack.FreezeOverlap")));
	F.Ability->EndDodgeAcceptWindow(TEXT("CoAttack.Freeze"));
	TestTrue(TEXT("dodge accept closure itself does not recall insect"), F.Sampler->IsRunning());
	F.Ability->EnableCollision(1); // a late window must not unfreeze this source
	F.At(.7, F.Origin + FVector(-7000, 500, 0)); F.Ability->DisableCollision(1);
	F.At(.8, F.Origin + FVector(9000, 0, 0));
	TestTrue(TEXT("duplicate openings and late attacks cannot resume recovery tracking"),
		F.H.Kinsect->GetActorLocation().Equals(LastCompletedPose, .02f));
	F.Ability->RequestEndAction(EWeaponActionEndReason::Superseded);
	const auto NextHandle = F.H.GiveAbility(UMHGZCoAttackTestAbility::StaticClass());
	auto* Next = F.Activate(NextHandle);
	if (!TestNotNull(TEXT("successor after a long recovery hold"), Next)) return false;
	TestEqual(TEXT("hold longer than delay still joins the original command stream"), F.Sampler->GetCommandSerial(), Serial);
	TestTrue(TEXT("successor owns sampler"), F.Sampler->GetActionToken() == Next->GetActionToken());
	F.H.Host->OnWeaponDodgeAcceptWindowOpened.Broadcast(Token); // stale source
	F.H.Host->OnWeaponComboWindowsClosed.Broadcast(Token);
	const FVector NextPose = F.Origin + FVector(400, 0, 0);
	F.At(.85, NextPose); F.At(.95, NextPose); F.At(1.06, NextPose);
	TestTrue(TEXT("successor resets freeze and follows its own path"),
		F.H.Kinsect->GetActorLocation().Equals(NextPose, .02f));
	F.H.Host->OnWeaponComboWindowsClosed.Broadcast(Next->GetActionToken());
	TestFalse(TEXT("actual combo closure retains the original immediate recall"), F.Sampler->IsRunning());
	TestTrue(TEXT("recall never ends the source GA"), Next->IsActive());
	F.H.ASC->ClearAbility(NextHandle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZCoAttackRecoveryPoseTest,
	"MHGZ.Maintenance.KinsectCoAttack.Recovery.LastAttackPoseAndReturnBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZCoAttackRecoveryPoseTest::RunTest(const FString&)
{
	for (const bool HasCombo : {false, true})
	{
		FCoAttackFixture F;
		if (!TestTrue(TEXT("fixture"), F.Build())) return false;
		F.WarmUp(); F.At(.3, F.Origin); F.Ability->EnableCollision(0);
		const FWeaponActionToken Token = F.Ability->GetActionToken();
		const FVector LastAttackPose = F.Origin + FVector(100, 0, 0);
		F.At(.35, LastAttackPose); F.Ability->DisableCollision(0);
		F.H.Resource->FinishKinsectCoAttackPath(Token, !HasCombo);
		F.At(.45, F.Origin + FVector(1000, 500, -300));
		F.At(.51, F.Origin + FVector(-1000, -500, 300));
		F.At(.56, F.Origin + FVector(5000, 0, 0));
		TestTrue(TEXT("never follows recovery's erratic tip"), F.H.Kinsect->GetActorLocation().Equals(LastAttackPose, .02f));
		TestEqual(TEXT("no-combo returns after delayed final sample, combo waits"), F.Sampler->IsRunning(), HasCombo);
		TestTrue(TEXT("source GA is not ended by insect return"), F.Ability->IsActive());
		if (HasCombo)
		{
			F.At(.7, F.Origin + FVector(8000, 0, 0));
			F.H.Host->OnWeaponComboWindowsClosed.Broadcast(Token);
			TestFalse(TEXT("exact combo closure retires now"), F.Sampler->IsRunning());
		}
		TestEqual(TEXT("normal live-arm return"), F.H.Kinsect->State, EKinsectState::Returning);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZCoAttackPowderTest,
	"MHGZ.Maintenance.KinsectCoAttack.Recovery.MarkedDamageAndSharedPowderCooldown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZCoAttackPowderTest::RunTest(const FString&)
{
	FCoAttackFixture F;
	if (!TestTrue(TEXT("fixture"), F.Build())) return false;
	F.WarmUp();
	F.H.CombatConfig->MarkedHitPowderCooldown = .5f;
	TestTrue(TEXT("real mark established"), F.H.Resource->SetKinsectMark(F.Part,
		F.Part->GetComponentLocation(), nullptr, FVector::BackwardVector));
	for (int32 I = 0; I < 3; ++I)
	{
		const double T = .3 + I * .35;
		F.At(T, F.Origin); F.Ability->EnableCollision(0);
		F.At(T + .05, F.Origin); F.Ability->DisableCollision(0);
		F.At(T + .21, F.Origin); F.At(T + .26, F.Origin);
		TestEqual(TEXT("independent damage is always submitted"), F.KinsectHits, I + 1);
		TestEqual(TEXT("first generates, second shared-CD rejected, third generates again"),
			F.H.Resource->GetPowderSnapshots().Num(), I == 2 ? 2 : 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZCoAttackTerrainTest,
	"MHGZ.Maintenance.KinsectCoAttack.Recovery.TerrainPassThroughAndSurfaceArrival",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZCoAttackTerrainTest::RunTest(const FString&)
{
	FCoAttackFixture F;
	if (!TestTrue(TEXT("fixture"), F.Build())) return false;
	F.SetTarget(F.Origin + FVector(5000, 0, 0));
	AActor* Wall = F.H.World->SpawnActor<AActor>();
	auto* Box = NewObject<UBoxComponent>(Wall);
	Wall->SetRootComponent(Box); Box->SetBoxExtent(FVector(50, 200, 200));
	Box->SetCollisionObjectType(ECC_WorldStatic); Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetCollisionResponseToAllChannels(ECR_Block); Box->RegisterComponent();
	Box->SetWorldLocation(F.Origin + FVector(150, 0, 0));
	FKinsectFlightRequest R;
	R.RuntimeToken = F.H.Host->GetCurrentToken(); R.FlightInstanceID = FGuid::NewGuid();
	R.TrajectoryMode = EKinsectTrajectoryMode::ToPoint; R.TargetPointSnapshot = F.Origin + FVector(600, 0, 0);
	R.FlightSpeed = 2000; R.MaxDistance = 2000; R.ArrivalRadius = 10;
	TestTrue(TEXT("manual command from cooperative state"), F.H.Resource->DeployKinsect(R));
	F.H.Kinsect->Movement->TickComponent(.2f, LEVELTICK_All, nullptr);
	F.H.Kinsect->Tick(.2f);
	TestTrue(TEXT("movement crosses the blocking terrain"), F.H.Kinsect->GetActorLocation().X > F.Origin.X + 250);
	TestEqual(TEXT("no terrain world-hit state"), F.H.Kinsect->State, EKinsectState::Flying);
	TestEqual(TEXT("terrain causes no kinsect damage"), F.KinsectHits, 0);
	FHitResult AimHit;
	TestTrue(TEXT("same terrain is still captured by camera visibility trace"),
		F.H.World->LineTraceSingleByChannel(AimHit, F.Origin, R.TargetPointSnapshot, ECC_Visibility));
	TestNull(TEXT("terrain is not a combat reticle target"), UMHGZMonsterHitzoneComponent::Resolve(AimHit.GetComponent()));
	F.H.Kinsect->SetActorLocation(R.TargetPointSnapshot);
	F.H.Kinsect->Tick(.01f);
	TestEqual(TEXT("surface point arrival remains an ordinary hover"), F.H.Kinsect->State, EKinsectState::Hovering);
	R.TargetPointSnapshot = F.Origin + FVector(-300, 0, 0); R.FlightInstanceID = FGuid::NewGuid();
	TestTrue(TEXT("sending again from terrain remains possible"), F.H.Resource->DeployKinsect(R));
	F.H.Kinsect->Movement->TickComponent(.1f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("new command has nonzero movement"), !F.H.Kinsect->Movement->Velocity.IsNearlyZero());
	return true;
}
#endif
