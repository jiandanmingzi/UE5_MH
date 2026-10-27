// Copyright MHGZ Project. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "MHGZCoAttackTestFixture.h"
#include "Components/BoxComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "ActionSystem/AnimNotify_KinsectRecallCommit.h"
#include "Animation/ActiveMontageInstanceScope.h"
#include "StaticMeshCompiler.h"

using MHGZCoAttackTest::FCoAttackFixture;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZCoAttackAuthoredRecallTest,
	"MHGZ.Maintenance.KinsectCoAttack.Recovery.AuthoredRecallUsesExactAction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZCoAttackAuthoredRecallTest::RunTest(const FString&)
{
	FCoAttackFixture F;
	if (!TestTrue(TEXT("fixture"), F.Build())) return false;
	F.WarmUp();
	const FWeaponActionToken Action = F.Ability->GetActionToken();
	USkeletalMeshComponent* Mesh = F.H.Character->GetMesh();
	if (!TestTrue(TEXT("exact source montage registered"), F.H.Host->RegisterMontage(Action, Mesh, 901))) return false;
	UAnimNotify_KinsectRecallCommit* Notify = NewObject<UAnimNotify_KinsectRecallCommit>();
	FAnimNotifyEvent Event;
	Event.Notify = Notify;
	FAnimNotifyEventReference Wrong(&Event, F.Ability->AttackMontage);
	Wrong.AddContextData<UE::Anim::FAnimNotifyMontageInstanceContext>(902);
	Notify->Notify(Mesh, F.Ability->AttackMontage, Wrong);
	TestTrue(TEXT("unregistered instance cannot recall this session"), F.Sampler->IsRunning());
	FAnimNotifyEventReference Exact(&Event, F.Ability->AttackMontage);
	Exact.AddContextData<UE::Anim::FAnimNotifyMontageInstanceContext>(901);
	F.Ability->CoAttack.bEnabled = false;
	Notify->Notify(Mesh, F.Ability->AttackMontage, Exact);
	TestTrue(TEXT("non-cooperative action keeps existing behavior"), F.Sampler->IsRunning());
	F.Ability->CoAttack.bEnabled = true;
	Notify->Notify(Mesh, F.Ability->AttackMontage, Exact);
	TestFalse(TEXT("authored point ends cooperative replay immediately"), F.Sampler->IsRunning());
	TestEqual(TEXT("insect starts its normal live-arm return"), F.H.Kinsect->State, EKinsectState::Returning);
	TestTrue(TEXT("recall does not close the hunter action or combo"), F.Ability->IsActive());
	Notify->Notify(Mesh, F.Ability->AttackMontage, Exact);
	TestEqual(TEXT("duplicate point does not interrupt the return"), F.H.Kinsect->State, EKinsectState::Returning);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZKinsectAnyStateRecallTest,
	"MHGZ.Maintenance.KinsectCoAttack.Recovery.AuthoredAnyStateRecall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZKinsectAnyStateRecallTest::RunTest(const FString&)
{
	for (const EKinsectState State : {EKinsectState::Attached, EKinsectState::Flying,
		EKinsectState::Hovering, EKinsectState::Returning, EKinsectState::ActionAttached,
		EKinsectState::ActionReturning, EKinsectState::ActionWaiting, EKinsectState::CoAttack})
	{
		FCoAttackFixture F;
		if (!TestTrue(TEXT("fixture"), F.Build())) return false;
		const FWeaponActionToken Action = F.Ability->GetActionToken();
		USkeletalMeshComponent* Mesh = F.H.Character->GetMesh();
		if (!TestTrue(TEXT("exact source montage registered"), F.H.Host->RegisterMontage(Action, Mesh, 901))) return false;
		if (State != EKinsectState::CoAttack)
		{
			F.H.Resource->RecallKinsect();
			F.H.Kinsect->SetActorLocation(Mesh->GetComponentLocation());
			F.H.Kinsect->Tick(.016f);
			if (!TestEqual(TEXT("setup returned to arm"), F.H.Kinsect->State, EKinsectState::Attached)) return false;
			if (State == EKinsectState::ActionAttached)
				TestTrue(TEXT("setup action socket"), F.H.Resource->BeginKinsectActionAttachment(Action, F.Weapon, TEXT("IG_FrontTip")));
			else if (State != EKinsectState::Attached)
			{
				FKinsectFlightRequest Request;
				Request.RuntimeToken = Action.RuntimeToken; Request.FlightInstanceID = FGuid::NewGuid();
				Request.FlightSpeed = 1000; Request.MaxDistance = 2000; Request.ArrivalRadius = 10;
				TestTrue(TEXT("setup player flight"), F.H.Resource->DeployKinsect(Request));
				F.H.Kinsect->SetActorLocation(F.Origin + FVector(100, 0, 0));
				if (State == EKinsectState::Hovering) F.H.Kinsect->Interrupt();
				else if (State == EKinsectState::Returning) F.H.Resource->RecallKinsect();
				else if (State == EKinsectState::ActionReturning || State == EKinsectState::ActionWaiting)
					TestTrue(TEXT("setup slash preparation"), F.H.Resource->BeginKinsectSlashPreparation(
						Action, F.Weapon, TEXT("IG_FrontTip"), State == EKinsectState::ActionReturning ? 1.f : 0.f));
			}
		}
		if (!TestEqual(TEXT("actual source state"), F.H.Kinsect->State, State)) return false;
		F.Ability->CoAttack.bEnabled = false; // the authored option must not depend on cooperation
		auto* Notify = NewObject<UAnimNotify_KinsectRecallCommit>();
		Notify->bRecallFromAnyState = true;
		FAnimNotifyEvent Event; Event.Notify = Notify;
		FAnimNotifyEventReference Wrong(&Event, F.Ability->AttackMontage);
		Wrong.AddContextData<UE::Anim::FAnimNotifyMontageInstanceContext>(902);
		Notify->Notify(Mesh, F.Ability->AttackMontage, Wrong);
		TestEqual(TEXT("wrong instance cannot issue a recall"), F.H.Kinsect->State, State);
		FAnimNotifyEventReference Exact(&Event, F.Ability->AttackMontage);
		Exact.AddContextData<UE::Anim::FAnimNotifyMontageInstanceContext>(901);
		Notify->Notify(Mesh, F.Ability->AttackMontage, Exact);
		const EKinsectState Expected = State == EKinsectState::Attached ? State : EKinsectState::Returning;
		TestEqual(FString::Printf(TEXT("state %d responds to authored recall"), int32(State)), F.H.Kinsect->State, Expected);
		TestFalse(TEXT("cooperative sampler retired"), F.Sampler->IsRunning());
		TestTrue(TEXT("hunter action stays active"), F.Ability->IsActive());
		Notify->Notify(Mesh, F.Ability->AttackMontage, Exact);
		TestEqual(TEXT("repeated recall is harmless"), F.H.Kinsect->State, Expected);
	}
	return true;
}

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZCoAttackMarkAdmissionTest,
	"MHGZ.Maintenance.KinsectCoAttack.Admission.MarkRangeAndActivationLatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZCoAttackMarkAdmissionTest::RunTest(const FString&)
{
	for (int32 Case = 0; Case < 5; ++Case)
	{
		FCoAttackFixture F;
		if (!TestTrue(TEXT("fixture with initial mark"), F.Build())) return false;
		F.H.ASC->CancelAbilityHandle(F.Handle);
		F.H.Kinsect->AttachToPlayer(F.H.Character->GetMesh());
		F.H.CombatConfig->CoAttackTuning.MarkSearchRadius = 200;
		FVector Point;
		if (!TestTrue(TEXT("actual surface anchor"), F.H.Resource->GetKinsectMarkWorldLocation(Point))) return false;
		F.H.Character->SetActorLocation(Point + FVector(Case == 2 ? 200 : Case == 3 ? 201 : 199, 0, 0));
		if (Case == 0) F.H.Resource->ClearKinsectMark(EIGMarkClearReason::Replaced);
		if (Case == 4) F.H.World->TimeSeconds = F.H.Resource->GetKinsectMarkExpireTime() + .01;
		const bool Allowed = Case == 1 || Case == 2;
		TestEqual(TEXT("range and validity helper"), F.H.Resource->HasKinsectCoAttackMarkInRange(), Allowed);
		F.Ability = F.Activate(F.Handle);
		if (!TestNotNull(TEXT("mark admission never blocks the hunter action"), F.Ability)) return false;
		TestEqual(FString::Printf(TEXT("case %d only admits a valid in-range mark"), Case), F.Sampler->IsRunning(), Allowed);
		if (!Allowed)
		{
			// 在同一招中补印/靠近不应迟到启动；下一招重新检查。
			F.H.Resource->SetKinsectMark(F.Part, F.Part->GetComponentLocation(), nullptr);
			F.H.Character->SetActorLocation(F.Part->GetComponentLocation());
			F.Ability->EnableCollision(0);
			TestFalse(TEXT("source window cannot retroactively admit the same action"), F.Sampler->IsRunning());
			F.H.ASC->CancelAbilityHandle(F.Handle);
			F.H.Kinsect->AttachToPlayer(F.H.Character->GetMesh());
			F.Ability = F.Activate(F.Handle);
			TestTrue(TEXT("next action rechecks the now-valid mark"), F.Ability && F.Sampler->IsRunning());
		}
		else
		{
			F.H.Resource->ClearKinsectMark(EIGMarkClearReason::Replaced);
			F.Ability->EnableCollision(0);
			TestTrue(TEXT("mark removal does not abort an admitted attack"), F.Sampler->IsRunning());
			const auto Next = F.H.GiveAbility(UMHGZCoAttackTestAbility::StaticClass());
			TestNotNull(TEXT("successor without mark is still a hunter action"), F.Activate(Next));
			TestFalse(TEXT("successor cannot join without its own admission"), F.Sampler->IsRunning());
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZCoAttackCurvedContactTest,
	"MHGZ.Maintenance.KinsectCoAttack.Recovery.FrontVolumeAtCloseSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZCoAttackCurvedContactTest::RunTest(const FString&)
{
	for (int32 Case = 0; Case < 4; ++Case)
	{
		const EMHGZHitzoneShape Shape = Case == 0 ? EMHGZHitzoneShape::Sphere
			: Case == 1 ? EMHGZHitzoneShape::Box : EMHGZHitzoneShape::Mesh;
		FCoAttackFixture F;
		if (!TestTrue(TEXT("fixture"), F.Build())) return false;
		F.Part->SetSphereRadius(400.f);
		if (Shape == EMHGZHitzoneShape::Mesh)
		{
			const TCHAR* Path = Case == 2
				? TEXT("/Game/Monster/TrainingDummy/Meshes/SM_TD_Query_Cylinder.SM_TD_Query_Cylinder")
				: TEXT("/Game/Monster/TrainingDummy/Meshes/SM_TD_Query_Hemisphere_White.SM_TD_Query_Hemisphere_White");
			F.Part->QueryMesh = LoadObject<UStaticMesh>(nullptr, Path);
			if (!TestNotNull(TEXT("actual round dummy query mesh"), F.Part->QueryMesh.Get())) return false;
			// A freshly loaded async mesh has no physics state yet. Complete only
			// this test's mesh before registering, just as it is ready during PIE.
			TArray<UStaticMesh*> Meshes{F.Part->QueryMesh.Get()};
			FStaticMeshCompilingManager::Get().FinishCompilation(Meshes);
			F.Part->QueryMeshScale = Case == 2 ? FVector(400, 400, 1000) : FVector::OneVector;
		}
		F.Part->ConfigureQueryShape(Shape, FVector(400, 400, 1000));
		F.SetTarget(F.Origin + FVector(395, 0, 0));
		if (Shape == EMHGZHitzoneShape::Mesh)
		{
			UPrimitiveComponent* Query = F.Part->GetQueryCollisionComponent();
			if (!TestTrue(TEXT("real curved query physics is ready"), Query->IsPhysicsStateCreated())) return false;
			FCollisionObjectQueryParams Objects;
			Objects.AddObjectTypesToQuery(ECC_GameTraceChannel3);
			TArray<FHitResult> Contacts;
			F.H.World->SweepMultiByObjectType(Contacts, F.Origin, F.Origin + FVector(0, 0, 20),
				FQuat::Identity, Objects, FCollisionShape::MakeSphere(30));
			const FHitResult* Contact = Contacts.FindByPredicate([Query](const FHitResult& H) {
				return H.GetComponent() == Query; });
			if (!TestNotNull(TEXT("legacy sphere really overlaps the curved target"), Contact)) return false;
			TestFalse(TEXT("old normal filter incorrectly rejects this upward front-volume contact"),
				AKinsect::IsContactInFront(*Contact, FVector::UpVector));
		}
		F.WarmUp();
		F.At(.3, F.Origin); F.Ability->EnableCollision(0);
		F.At(.35, F.Origin + FVector(0, 0, 20)); F.Ability->DisableCollision(0);
		F.At(.49, F.Origin + FVector(0, 0, 20));
		TestEqual(TEXT("no contact before the existing 0.2s delay"), F.KinsectHits, 0);
		F.At(.61, F.Origin + FVector(0, 0, 20));
		TestEqual(FString::Printf(TEXT("front volume inside/tangent to case %d still hits"), Case),
			F.KinsectHits, 1);
		F.At(.7, F.Origin + FVector(0, 0, 20));
		TestEqual(TEXT("one hit budget is preserved"), F.KinsectHits, 1);
	}
	// Upward movement must not turn a wholly rear contact into a hit.
	FCoAttackFixture Rear;
	if (!TestTrue(TEXT("rear fixture"), Rear.Build())) return false;
	Rear.WarmUp();
	Rear.SetTarget(Rear.Origin - FVector(0, 0, 20));
	Rear.At(.3, Rear.Origin); Rear.Ability->EnableCollision(0);
	Rear.At(.35, Rear.Origin + FVector(0, 0, 20)); Rear.Ability->DisableCollision(0);
	Rear.At(.61, Rear.Origin + FVector(0, 0, 20));
	TestEqual(TEXT("target wholly behind the attack plane is not damaged"), Rear.KinsectHits, 0);
	return true;
}
#endif
