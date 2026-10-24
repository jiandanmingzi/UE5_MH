// Copyright MHGZ Project. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "ActionSystem/Tests/Shared/MHGZM3TestHarness.h"
#include "ActionSystem/MHGZInsectGlaiveAbility.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Engine/SkeletalMesh.h"
#include "InsectGlaive/Kinsect/IGCoAttackComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZCoAttackHistoryTest,
	"MHGZ.Maintenance.KinsectCoAttack.M1.TimestampInterpolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZCoAttackHistoryTest::RunTest(const FString&)
{
	FIGCoAttackTuning Tuning;
	for (const double Step : {1.0 / 30.0, 1.0 / 60.0, 1.0 / 120.0, .0037})
	{
		FIGCoAttackHistory History;
		for (int32 I = 0; I < int32(2.0 / Step); ++I)
		{
			const double Now = I * Step;
			const FTransform Pose(FQuat(FVector::UpVector, Now * .2), FVector(Now * 100, 17, 33));
			if (!TestTrue(TEXT("sample accepted"), History.Add(Now, Pose, Tuning))) return false;
			FTransform Out;
			const double ReadTime = Now - double(Tuning.TrajectoryDelaySeconds);
			if (ReadTime >= 0)
			{
				if (!TestTrue(TEXT("bracketed history evaluates"), History.Evaluate(ReadTime, Out))) return false;
				TestTrue(TEXT("0.2 seconds, not sample count"), FMath::IsNearlyEqual(Out.GetLocation().X, ReadTime * 100, .001));
				TestTrue(TEXT("interpolated world rotation"), Out.GetRotation().Equals(FQuat(FVector::UpVector, ReadTime * .2), .001));
			}
			else TestFalse(TEXT("no fabricated negative-time pose"), History.Evaluate(ReadTime, Out));
		}
		TestTrue(TEXT("bounded history"), History.Num() <= Tuning.MaxSamples);
	}
	// Simulate global slow motion by advancing game timestamps at 0.2 real rate.
	FIGCoAttackHistory Slow;
	for (int32 I = 0; I <= 120; ++I)
	{
		const double GameTime = I / 60.0 * .2;
		TestTrue(TEXT("slow motion sample"), Slow.Add(GameTime,
			FTransform(FVector(GameTime * 200, 0, 0)), Tuning));
	}
	FTransform Out;
	TestTrue(TEXT("slow motion lookup"), Slow.Evaluate(.2, Out));
	TestTrue(TEXT("slow motion uses game time"), FMath::IsNearlyEqual(Out.GetLocation().X, 40.0, .001));
	const int32 Count = Slow.Num();
	TestTrue(TEXT("paused duplicate accepted without replacing history"), Slow.Add(.4, FTransform(FVector(999, 0, 0)), Tuning));
	TestEqual(TEXT("pause adds no samples"), Slow.Num(), Count);
	TestTrue(TEXT("paused pose remains recorded pose"), Slow.Evaluate(.4, Out) && Out.GetLocation().Equals(FVector(80, 0, 0)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZCoAttackHistoryBoundsTest,
	"MHGZ.Maintenance.KinsectCoAttack.M1.HistoryRejectsDiscontinuity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZCoAttackHistoryBoundsTest::RunTest(const FString&)
{
	FIGCoAttackHistory H;
	FIGCoAttackTuning T;
	TestTrue(TEXT("initial pose"), H.Add(2.0, FTransform::Identity, T));
	TestFalse(TEXT("time rollback rejected"), H.Add(1.0, FTransform::Identity, T));
	TestFalse(TEXT("unrecoverable gap rejected"), H.Add(2.3, FTransform::Identity, T));
	FTransform Out;
	TestFalse(TEXT("future not extrapolated"), H.Evaluate(2.1, Out));
	TestFalse(TEXT("past not extrapolated"), H.Evaluate(1.9, Out));
	T.MaxSamples = 8;
	H.Reset();
	for (int32 I = 0; I < 8; ++I) TestTrue(TEXT("capacity sample"), H.Add(I * .001, FTransform::Identity, T));
	TestFalse(TEXT("overflow retires instead of evicting needed delay"), H.Add(.008, FTransform::Identity, T));
	T.TrajectoryDelaySeconds = 0;
	TestFalse(TEXT("bad tuning rejected"), H.Add(.008, FTransform::Identity, T));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZCoAttackRuntimeTest,
	"MHGZ.Maintenance.KinsectCoAttack.M1.RuntimeGroundStaminaAndOwnership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZCoAttackRuntimeTest::RunTest(const FString&)
{
	USkeletalMesh* Glaive = LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Weapons/InsectGlaive/Meshes/Glaive/SKM_IG_Glaive.SKM_IG_Glaive"));
	if (!TestNotNull(TEXT("resident socket source, read only"), Glaive)) return false;
	TestFalse(TEXT("all old GA defaults remain opt-out"), GetDefault<UMHGZInsectGlaiveAbility>()->CoAttack.bEnabled);
	enum ECase { Replay, StaminaEmpty, ManualSend, ManualRecall, LeftGround, StaleEnd, Shutdown, ExpiredSource, InvalidSocket, Disabled, AerialStart, DeployedStart };
	for (int32 Case = Replay; Case <= DeployedStart; ++Case)
	{
		FMHGZM3Harness H;
		if (!TestTrue(TEXT("equipped real resource"), H.Setup())) { H.Teardown(); return false; }
		const FWeaponActionToken Token = H.MakeActionToken(UMHGZSendKinsectAbility::StaticClass());
		TestTrue(TEXT("register precise source action"), H.Host->RegisterAction(Token));
		USkeletalMeshComponent* Weapon = NewObject<USkeletalMeshComponent>(H.Character);
		Weapon->SetSkeletalMesh(Glaive);
		Weapon->RegisterComponent();
		FIGCoAttackSource Source;
		Source.bEnabled = Case != Disabled;
		if (Case == Replay) Source.LocalOffset = FVector(7, 11, 3);
		if (Case == InvalidSocket) Source.Socket = TEXT("MissingCoAttackSocket");
		if (Case == AerialStart) H.Character->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
		if (Case == DeployedStart)
		{
			H.Kinsect->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
			H.Kinsect->State = EKinsectState::Hovering;
		}
		const bool bStart = H.Resource->BeginKinsectCoAttack(Token, Weapon, Source);
		if (Case >= InvalidSocket)
		{
			TestFalse(TEXT("invalid/disabled/aerial/deployed start rejected"), bStart);
			TestFalse(TEXT("failed start creates no active sampler"), H.Resource->CoAttackComponent && H.Resource->CoAttackComponent->IsRunning());
			H.Teardown(); continue;
		}
		if (!TestTrue(TEXT("attached ground start accepted"), bStart)) { H.Teardown(); return false; }
		UIGCoAttackComponent* C = H.Resource->CoAttackComponent;
		const uint64 Serial = C->GetCommandSerial();
		TestEqual(TEXT("cooperative state appended, not action attachment"), H.Kinsect->State, EKinsectState::CoAttack);
		TestFalse(TEXT("projectile is not a second movement writer"), H.Kinsect->Movement->IsActive());
		TestEqual(TEXT("M1 cannot deal damage"), H.Kinsect->ActiveRequest.DamageMode, EKinsectDamageMode::None);
		TestEqual(TEXT("M1 cannot extract"), H.Kinsect->ActiveRequest.ExtractMode, EKinsectExtractMode::None);
		TestEqual(TEXT("post pose tick group"), C->PrimaryComponentTick.TickGroup, TG_PostUpdateWork);
		const float Before = H.Resource->GetKinsectStamina();
		H.Resource->TickComponent(.25f, LEVELTICK_All, nullptr);
		TestTrue(TEXT("flight drain, no attached regeneration"), FMath::IsNearlyEqual(H.Resource->GetKinsectStamina(), Before - H.KinsectData->FlightDrainRate * .25f));
		if (Case == Replay)
		{
			// Move the real socket near the arm; source transform changes, not a mocked sampler.
			Weapon->AddWorldOffset(H.Kinsect->GetActorLocation() + FVector(20, 0, 0)
				- Weapon->GetSocketTransform(Source.Socket).TransformPosition(Source.LocalOffset));
			const FVector Base = Weapon->GetComponentLocation();
			const FVector Start = H.Kinsect->GetActorLocation();
			const FVector SocketBase = Weapon->GetSocketTransform(Source.Socket).TransformPosition(Source.LocalOffset);
			const double StartTime = H.World->GetTimeSeconds();
			for (int32 I = 1; I <= 40; ++I)
			{
				// Controlled game clock; do not tick the engine's XR subsystem on a
				// transient world with no registered desktop WorldContext.
				H.World->TimeSeconds += .025;
				const double Time = H.World->GetTimeSeconds() - StartTime;
				Weapon->SetWorldLocation(Base + FVector(Time * 100, 0, 0));
				C->TickComponent(.025f, LEVELTICK_All, nullptr);
				if (I == 1) TestTrue(TEXT("entry bounded by original return speed"), FVector::Dist(Start, H.Kinsect->GetActorLocation()) <= H.Kinsect->GetReturnSpeed() * .025f + .01f);
			}
			const double ReplayTime = H.World->GetTimeSeconds() - StartTime - H.CombatConfig->CoAttackTuning.TrajectoryDelaySeconds;
			TestTrue(TEXT("entry completed"), C->IsTracking());
			TestTrue(TEXT("actor follows actual historical socket world pose"), H.Kinsect->GetActorLocation().Equals(SocketBase + FVector(ReplayTime * 100, 0, 0), .02f));
			TestTrue(TEXT("mesh local Y follows tangent"), H.Kinsect->GetAttackForwardVector().Dot(FVector::ForwardVector) > .99f);
			H.Resource->EndKinsectCoAttack(Token);
			TestFalse(TEXT("normal action end stops sampler in M1"), C->IsRunning());
			TestEqual(TEXT("normal action end uses old return"), H.Kinsect->State, EKinsectState::Returning);
		}
		else if (Case == StaminaEmpty)
		{
			H.Resource->KinsectStamina = .01f;
			H.Resource->TickComponent(.25f, LEVELTICK_All, nullptr);
			TestEqual(TEXT("depletion clamped at zero"), H.Resource->GetKinsectStamina(), 0.f);
			TestFalse(TEXT("depletion invalidates sampler"), C->IsRunning());
			TestEqual(TEXT("depletion recalls"), H.Kinsect->State, EKinsectState::Returning);
			TestFalse(TEXT("old pose rejected after system takeover"), H.Kinsect->ApplyCoAttackPose(Serial, FVector(999, 0, 0), FVector::ForwardVector));
		}
		else if (Case == ManualSend)
		{
			FKinsectFlightRequest R;
			R.RuntimeToken = Token.RuntimeToken;
			R.FlightSpeed = 2000; R.MaxDistance = 1000;
			R.FlightInstanceID = FGuid::NewGuid();
			TestFalse(TEXT("invalid command fails without stealing ownership"), H.Resource->DeployKinsect(R));
			TestTrue(TEXT("failed command preserves old sampler"), C->IsRunning());
			R.ArrivalRadius = 10; // required by the existing atomic flight request contract
			TestTrue(TEXT("manual command can take over"), H.Resource->DeployKinsect(R));
			TestFalse(TEXT("manual command stops old sampler"), C->IsRunning());
			TestEqual(TEXT("old cleanup did not interrupt new flight"), H.Kinsect->State, EKinsectState::Flying);
			H.Resource->EndKinsectCoAttack(Token);
			TestEqual(TEXT("old GA end cannot recall new manual flight"), H.Kinsect->State, EKinsectState::Flying);
		}
		else if (Case == ManualRecall)
		{
			TestTrue(TEXT("manual recall works"), H.Resource->RecallKinsect());
			TestFalse(TEXT("manual recall stops sampler"), C->IsRunning());
			TestEqual(TEXT("manual recall returning"), H.Kinsect->State, EKinsectState::Returning);
		}
		else if (Case == LeftGround)
		{
			H.Character->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
			C->TickComponent(.01f, LEVELTICK_All, nullptr);
			TestFalse(TEXT("leaving ground retires sampler"), C->IsRunning());
			TestEqual(TEXT("leaving ground returns insect"), H.Kinsect->State, EKinsectState::Returning);
		}
		else if (Case == StaleEnd)
		{
			FWeaponActionToken Old = Token;
			++Old.ActivationSequenceID;
			H.Resource->EndKinsectCoAttack(Old);
			TestTrue(TEXT("foreign GA end cannot stop current session"), C->IsRunning());
		}
		else if (Case == Shutdown)
		{
			H.Host->ShutdownRuntime(EWeaponRuntimeEndReason::RuntimeShutdown);
			TestFalse(TEXT("shutdown invalidates sampler"), C->IsRunning());
			TestNull(TEXT("shutdown releases owned component"), H.Resource->CoAttackComponent.Get());
		}
		else if (Case == ExpiredSource)
		{
			Token.AbilityInstance->MarkAsGarbage();
			TestTrue(TEXT("expired source still counts as a session needing cleanup"), C->IsRunning());
			C->TickComponent(.01f, LEVELTICK_All, nullptr);
			TestFalse(TEXT("expired source retires sampler"), C->IsRunning());
			TestEqual(TEXT("expired source returns rather than orphaning insect"), H.Kinsect->State, EKinsectState::Returning);
		}
		H.Teardown();
	}
	return true;
}
#endif
