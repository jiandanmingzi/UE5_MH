// Copyright MHGZ Project. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "ActionSystem/MHGZAwakenedKinsectAttackAbility.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "UI/MHGZInsectGlaiveResourceWidget.h"
#include "../../Shared/MHGZM3TestHarness.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZM6ScreenCenterLaunch,
	"MHGZ.M6.KinsectAim.ScreenCenterLaunchAndOutsideRetarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZM6ScreenCenterLaunch::RunTest(const FString&)
{
	FMHGZM3Harness H;
	if (!H.Setup()) { AddError(TEXT("Runtime fixture unavailable")); return false; }
	const FVector Owner = H.Character->GetActorLocation();
	for (const FRotator View : {FRotator(0,0,0), FRotator(35,45,0), FRotator(-30,120,0)})
	{
		const FVector D = View.Vector(), Normal = FVector(D.X,D.Y,0).GetSafeNormal();
		const FVector Camera = Owner - Normal * 600 + FVector::CrossProduct(FVector::UpVector, Normal) * 70
			+ FVector::UpVector * 220;
		FVector Origin;
		TestTrue(TEXT("Valid ray/vertical plane intersection"),
			AKinsect::ResolveScreenCenterLaunchOrigin(Camera, D, Owner, Origin));
		TestTrue(TEXT("Start lies on center camera ray"), FVector::CrossProduct(Origin-Camera,D).Size() < 0.001);
		TestTrue(TEXT("Start remains in owner vertical plane"), FMath::Abs(FVector::DotProduct(Origin-Owner,Normal)) < 0.001);
	}
	FKinsectFlightRequest R;
	R.RuntimeToken=H.Host->GetCurrentToken(); R.FlightInstanceID=FGuid::NewGuid();
	R.TrajectoryMode=EKinsectTrajectoryMode::ToPoint; R.MaxDistance=3000; R.FlightSpeed=2000;
	R.ArrivalRadius=10; R.MotionValue=0.2f; R.PostFlightPolicy=EKinsectPostFlightPolicy::Hover;
	R.bProjectAttachedLaunchToAimRay=true; R.AimOriginSnapshot=Owner+FVector(-600,70,200);
	R.DirectionSnapshot=FVector::ForwardVector; R.TargetPointSnapshot=R.AimOriginSnapshot+R.DirectionSnapshot*2000;
	const FVector Before=H.Kinsect->GetActorLocation();
	FKinsectFlightRequest Invalid=R; Invalid.DirectionSnapshot=FVector::ZeroVector;
	TestFalse(TEXT("Invalid projection rejected atomically"), H.Kinsect->BeginFlight(Invalid));
	TestTrue(TEXT("Rejected request does not teleport"), H.Kinsect->GetActorLocation().Equals(Before));
	TestTrue(TEXT("Attached aimed request starts"), H.Kinsect->BeginFlight(R));
	TestTrue(TEXT("Attached origin aligned to ray at commit"), H.Kinsect->GetActorLocation().Equals(Owner+FVector(0,70,200),0.001));
	TestTrue(TEXT("Velocity reaches saved reticle target, not a parallel offset"),
		H.Kinsect->Movement->Velocity.GetSafeNormal().Equals((R.TargetPointSnapshot-H.Kinsect->GetActorLocation()).GetSafeNormal(),0.001));
	H.Kinsect->Interrupt();
	const FVector Outside=H.Kinsect->GetActorLocation(); R.TargetPointSnapshot+=FVector(0,500,0); R.FlightInstanceID=FGuid::NewGuid();
	TestTrue(TEXT("Outside re-aim starts"),H.Kinsect->BeginFlight(R));
	TestTrue(TEXT("Already deployed kinsect never teleports back to center"),H.Kinsect->GetActorLocation().Equals(Outside));
	TestTrue(TEXT("Outside re-aim converges to new target"), H.Kinsect->Movement->Velocity.GetSafeNormal().Equals((R.TargetPointSnapshot-Outside).GetSafeNormal(),0.001));
	H.Kinsect->AttachToPlayer(H.Character->GetMesh(), NAME_None);
	R.FlightInstanceID = FGuid::NewGuid();
	R.TargetPointSnapshot = R.AimOriginSnapshot + R.DirectionSnapshot * 250;
	TestTrue(TEXT("Close floor/wall target still allows an aimed send"), H.Kinsect->BeginFlight(R));
	const FVector CloseOrigin = H.Kinsect->GetActorLocation();
	TestTrue(TEXT("Close target clamps origin before the blocker, not behind it"),
		FVector::DotProduct(R.TargetPointSnapshot - CloseOrigin, R.DirectionSnapshot) > R.ArrivalRadius);
	TestTrue(TEXT("Close target fallback remains on screen-center ray"),
		FVector::CrossProduct(CloseOrigin - R.AimOriginSnapshot, R.DirectionSnapshot).IsNearlyZero(0.001));
	TestTrue(TEXT("Close target fallback never reverses flight"),
		H.Kinsect->Movement->Velocity.GetSafeNormal().Equals(R.DirectionSnapshot, 0.001));
	H.Teardown(); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZM6AwakenedParallax,
	"MHGZ.M6.KinsectAim.AwakenedTargetParallaxAndCaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZM6AwakenedParallax::RunTest(const FString&)
{
	const FVector Target(1000,70,200), Hunter(0,0,90), Tail(-40,-60,160);
	FVector HunterDir, KinsectDir;
	TestTrue(TEXT("Hunter direction resolved against target"), UMHGZAwakenedKinsectAttackAbility::ResolveLockedTargetDirection(
		FVector::ForwardVector,Target,Hunter,FVector::ForwardVector,90,60,HunterDir));
	TestTrue(TEXT("Tail direction resolved independently"), UMHGZAwakenedKinsectAttackAbility::ResolveLockedTargetDirection(
		FVector::ForwardVector,Target,Tail,FVector::ForwardVector,90,60,KinsectDir));
	TestTrue(TEXT("Hunter ray converges"), HunterDir.Equals((Target-Hunter).GetSafeNormal(),0.001));
	TestTrue(TEXT("Kinsect ray converges"), KinsectDir.Equals((Target-Tail).GetSafeNormal(),0.001));
	TestFalse(TEXT("Different launch origins must not reuse a parallel direction"), HunterDir.Equals(KinsectDir,0.001));
	FVector Limited;
	TestTrue(TEXT("Behind-target direction keeps authored cap"), UMHGZAwakenedKinsectAttackAbility::ResolveLockedTargetDirection(
		FVector::ForwardVector,FVector(-1000,1000,10000),FVector::ZeroVector,FVector::ForwardVector,90,60,Limited));
	TestTrue(TEXT("Yaw limited to 90 degrees"),FMath::IsNearlyEqual(Limited.Rotation().Yaw,90.f,0.01f));
	TestTrue(TEXT("Pitch limited to 60 degrees"),FMath::IsNearlyEqual(Limited.Rotation().Pitch,60.f,0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZM6CargoDisplay,
	"MHGZ.M6.UI.TripleSnapshotAndOneTwoThreeCargoDisplay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZM6CargoDisplay::RunTest(const FString&)
{
	FMHGZM3Harness H;
	if (!H.Setup()) { AddError(TEXT("Runtime fixture unavailable")); return false; }
	for (const TCHAR* Name : {TEXT("WeaponResource.IG.Extract.Red"),TEXT("WeaponResource.IG.Extract.White"),TEXT("WeaponResource.IG.Extract.Orange")})
	{ TestTrue(TEXT("Extract granted"),H.Resource->ApplyExtract(M3::Tag(Name))); }
	const FIGResourceUISnapshot Triple=H.Resource->GetUIState();
	TestTrue(TEXT("Triple effect active"),Triple.bTripleUp);
	TestFalse(TEXT("Single effects correctly replaced, not fabricated for UI"),Triple.bRed||Triple.bWhite||Triple.bOrange);
	TestEqual(TEXT("Hunter triple must not fabricate kinsect cargo"),UMHGZInsectGlaiveResourceWidget::GetDisplayedCargoColors(Triple).Num(),0);
	FIGResourceUISnapshot S;
	S.KinsectPendingExtractColor=M3::Tag(TEXT("WeaponResource.IG.Extract.Red"));
	TestEqual(TEXT("Legacy single cargo fills full oval"),UMHGZInsectGlaiveResourceWidget::GetDisplayedCargoColors(S).Num(),1);
	S.KinsectPendingExtractColors.AddTag(S.KinsectPendingExtractColor);
	S.KinsectPendingExtractColors.AddTag(M3::Tag(TEXT("WeaponResource.IG.Extract.White")));
	TestEqual(TEXT("Two-color display snapshot yields two sectors"),UMHGZInsectGlaiveResourceWidget::GetDisplayedCargoColors(S).Num(),2);
	S.KinsectPendingExtractColors.AddTag(M3::Tag(TEXT("WeaponResource.IG.Extract.Orange")));
	TestEqual(TEXT("Three-color display snapshot yields three sectors"),UMHGZInsectGlaiveResourceWidget::GetDisplayedCargoColors(S).Num(),3);
	TestFalse(TEXT("Display samples do not change actual single-cargo gameplay"),H.Kinsect->HasPendingExtract());
	H.Teardown(); return true;
}
#endif
