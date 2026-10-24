// Copyright MHGZ Project. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "MHGZCoAttackTestFixture.h"
#include "ActionSystem/MHGZComboCoordinatorAbility.h"

using MHGZCoAttackTest::FCoAttackFixture;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZCoAttackM3ComboTest,
	"MHGZ.Maintenance.KinsectCoAttack.M3.ActualComboConfirmationOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZCoAttackM3ComboTest::RunTest(const FString&)
{
	FCoAttackFixture F;
	if (!TestTrue(TEXT("fixture"), F.Build())) return false;
	F.WarmUp();
	auto* Coordinator = F.H.ASC->GetActiveComboCoordinator();
	if (!TestNotNull(TEXT("real runtime coordinator"), Coordinator)) return false;
	auto* Combo = NewObject<UMHGZWeaponComboData>(F.H.ASC);
	const FGameplayTag InputTag = M3::Tag(TEXT("Input.Weapon.Y"));
	for (int32 I = 0; I < 2; ++I)
	{
		auto& Edge = Combo->Transitions.AddDefaulted_GetRef();
		Edge.TransitionID = I == 0 ? TEXT("IdleToCo1") : TEXT("Co1ToCo2");
		Edge.SourceState = I == 0 ? TEXT("Idle") : TEXT("Co1");
		Edge.TargetState = I == 0 ? TEXT("Co1") : TEXT("Co2");
		Edge.InputTag = InputTag;
		Edge.AbilityClass = UMHGZCoAttackTestAbility::StaticClass();
		Edge.bRequiresDodgeAcceptWindow = I == 1;
	}
	Coordinator->InjectComboData(Combo);
	auto Send = [&](uint32 Sequence)
	{
		FWeaponInputSnapshot Input = M3::MakePosedInput(true, false);
		Input.ResolvedInputTag = InputTag; Input.SourceControlTag = InputTag;
		Input.Phase = EWeaponInputPhase::Started; Input.SequenceID = Sequence;
		Coordinator->HandleWeaponInput(Input);
		return Coordinator->GetActiveTransition().IsSet()
			? Cast<UMHGZCoAttackTestAbility>(Coordinator->GetActiveTransition()->ActionToken.AbilityInstance.Get()) : nullptr;
	};
	F.At(.3, F.Origin);
	auto* First = Send(1);
	if (!TestNotNull(TEXT("first combo action"), First)) return false;
	First->EnableCollision(0);
	const uint64 Serial = F.Sampler->GetCommandSerial();
	F.At(.35, F.Origin);
	TestTrue(TEXT("second input waits for a real dodge-accept opening"), Send(2) == First);
	bool bOpeningBeforeBufferedSuccessor = false;
	const FWeaponActionToken FirstToken = First->GetActionToken();
	const FDelegateHandle Opening = F.H.Host->OnWeaponDodgeAcceptWindowOpened.AddLambda(
		[&](const FWeaponActionToken& Action)
		{
			bOpeningBeforeBufferedSuccessor = Action == FirstToken
				&& F.Sampler->GetActionToken() == FirstToken && First->IsActive();
		});
	const bool bOpened = First->BeginDodgeAcceptWindow(TEXT("CoAttack.Chain"));
	F.H.Host->OnWeaponDodgeAcceptWindowOpened.Remove(Opening);
	TestTrue(TEXT("source window successfully opens"), bOpened);
	TestTrue(TEXT("opening boundary is delivered before same-frame buffered takeover"), bOpeningBeforeBufferedSuccessor);
	auto* Second = Coordinator->GetActiveTransition().IsSet()
		? Cast<UMHGZCoAttackTestAbility>(Coordinator->GetActiveTransition()->ActionToken.AbilityInstance.Get()) : nullptr;
	if (!TestNotNull(TEXT("confirmed successor"), Second)) return false;
	TestTrue(TEXT("per-execution new source"), First != Second);
	TestEqual(TEXT("real coordinator superseded predecessor"), First->GetActionEndReason(), EWeaponActionEndReason::Superseded);
	TestFalse(TEXT("old GA did not retain registration"), F.H.Host->IsActionRegistered(First->GetActionToken()));
	TestEqual(TEXT("coordinator order preserves command stream"), F.Sampler->GetCommandSerial(), Serial);
	TestTrue(TEXT("new registration already owns sampler"), F.Sampler->GetActionToken() == Second->GetActionToken());
	Second->EnableCollision(0);
	F.At(.4, F.Origin); Second->DisableCollision(0);
	F.At(.51, F.Origin); F.At(.56, F.Origin); F.At(.61, F.Origin);
	TestEqual(TEXT("coordinator transition preserved both receipts"), F.KinsectHits, 2);
	const FWeaponActionToken SecondToken = Second->GetActionToken();
	TestTrue(TEXT("real combo window opens"), Coordinator->OpenComboWindow(SecondToken, TEXT("CoAttack.Exit")));
	Coordinator->CloseComboWindow(SecondToken, TEXT("CoAttack.Exit"));
	TestFalse(TEXT("combo window close returns insect before GA ends"), F.Sampler->IsRunning());
	TestTrue(TEXT("closing the window does not end the attack GA"), Second->IsActive());
	Coordinator->ResetCombo(EWeaponActionEndReason::Cancelled);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZCoAttackM3TailTest,
	"MHGZ.Maintenance.KinsectCoAttack.M3.NormalTailWithoutSourceAbility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZCoAttackM3TailTest::RunTest(const FString&)
{
	FCoAttackFixture F;
	if (!TestTrue(TEXT("fixture"), F.Build())) return false;
	F.WarmUp(); F.At(.3, F.Origin); F.Ability->EnableCollision(0);
	F.At(.35, F.Origin);
	const FWeaponActionToken Source = F.Ability->GetActionToken();
	F.Ability->RequestEndAction(EWeaponActionEndReason::Normal);
	TestFalse(TEXT("GA ended immediately, no held action registration"), F.H.Host->IsActionRegistered(Source));
	TestFalse(TEXT("GA not kept alive for tail"), F.Ability->IsActive());
	TestFalse(TEXT("tail does not retain Attacking tag"),
		F.H.ASC->HasMatchingGameplayTag(M3::Tag(TEXT("Combat.State.Attacking"))));
	TestFalse(TEXT("tail does not retain movement input lock"),
		F.H.ASC->HasMatchingGameplayTag(M3::Tag(TEXT("Combat.State.BlockMovement"))));
	TestTrue(TEXT("resource still owns legal tail"), F.Sampler->IsRunning());
	F.H.ASC->ClearAbility(F.Handle); F.Handle = FGameplayAbilitySpecHandle();
	F.Ability->MarkAsGarbage(); F.Ability = nullptr;
	TestFalse(TEXT("source weak pointer may expire"), Source.AbilityInstance.IsValid());
	TestFalse(TEXT("expired source cannot submit a new receipt"),
		F.H.Resource->OpenKinsectCoAttackWindow(Source, 0, 99, FGuid::NewGuid(), 10, 1, .1f));
	F.H.KinsectData->KinsectAttackPower = 999.f;
	F.At(.49, F.Origin + FVector(1000, 0, 0));
	TestEqual(TEXT("tail obeys original delay"), F.KinsectHits, 0);
	F.At(.51, F.Origin + FVector(1000, 0, 0));
	TestEqual(TEXT("immutable receipt hits after old ability died"), F.KinsectHits, 1);
	TestEqual(TEXT("real damage not only a callback"), F.Target->GetAttributeSet()->GetHealth(), 9990.f);
	if (F.Powers.Num() == 1) TestEqual(TEXT("tail keeps original power snapshot"), F.Powers[0], 100.f);
	TestTrue(TEXT("tail cannot sample an unrelated new pose"), F.H.Kinsect->GetActorLocation().Equals(F.Origin, .02f));
	F.At(.56, F.Origin);
	TestFalse(TEXT("tail retires at sealed end plus delay"), F.Sampler->IsRunning());
	TestEqual(TEXT("retirement uses normal return"), F.H.Kinsect->State, EKinsectState::Returning);
	F.At(.7, F.Origin);
	TestEqual(TEXT("no ghost damage"), F.KinsectHits, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZCoAttackM3ChainTest,
	"MHGZ.Maintenance.KinsectCoAttack.M3.OrderedSuccessorAndSourceIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZCoAttackM3ChainTest::RunTest(const FString&)
{
	FCoAttackFixture F;
	if (!TestTrue(TEXT("fixture"), F.Build())) return false;
	F.WarmUp(); F.At(.3, F.Origin); F.Ability->EnableCollision(0);
	const uint64 Occurrence = F.Ability->GetCollisionWindowOccurrence(0);
	const FWeaponActionToken Old = F.Ability->GetActionToken();
	const uint64 Serial = F.Sampler->GetCommandSerial();
	F.At(.35, F.Origin);
	// Real coordinator order: predecessor is Superseded before successor registers.
	F.Ability->RequestEndAction(EWeaponActionEndReason::Superseded);
	const auto NextHandle = F.H.GiveAbility(UMHGZCoAttackTestAbility::StaticClass());
	auto* Next = F.Activate(NextHandle);
	if (!TestNotNull(TEXT("successful successor"), Next)) return false;
	TestEqual(TEXT("one command stream, no intermediate return"), F.Sampler->GetCommandSerial(), Serial);
	TestEqual(TEXT("insect remains cooperative"), F.H.Kinsect->State, EKinsectState::CoAttack);
	TestTrue(TEXT("current owner changed to exact successor"), F.Sampler->GetActionToken() == Next->GetActionToken());
	Next->EnableCollision(0);
	TestEqual(TEXT("two GAs can both have occurrence one"), Next->GetCollisionWindowOccurrence(0), Occurrence);
	F.H.Resource->EndKinsectCoAttack(Old);
	F.H.Resource->FinishKinsectCoAttack(Old);
	F.H.Resource->CloseKinsectCoAttackWindow(Old, Occurrence);
	TestTrue(TEXT("old End/Finish/Close cannot cancel successor"), F.Sampler->IsRunning());
	F.At(.4, F.Origin); Next->DisableCollision(0);
	F.At(.51, F.Origin); F.At(.56, F.Origin); F.At(.61, F.Origin);
	TestEqual(TEXT("both source receipts retain independent budgets"), F.KinsectHits, 2);
	TestEqual(TEXT("both hits really damage target"), F.Target->GetAttributeSet()->GetHealth(), 9980.f);
	F.At(.65, F.Origin); Next->RequestEndAction(EWeaponActionEndReason::Normal);
	F.At(.8, F.Origin); F.At(.86, F.Origin);
	TestFalse(TEXT("last source tail retires once"), F.Sampler->IsRunning());
	F.H.ASC->ClearAbility(NextHandle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZCoAttackM3PriorityTest,
	"MHGZ.Maintenance.KinsectCoAttack.M3.FailedCommitAndNonCoopTakeover",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZCoAttackM3PriorityTest::RunTest(const FString&)
{
	FCoAttackFixture F;
	if (!TestTrue(TEXT("fixture"), F.Build())) return false;
	F.WarmUp(); F.At(.3, F.Origin); F.Ability->EnableCollision(0);
	const uint64 Serial = F.Sampler->GetCommandSerial();
	const auto Rejected = F.H.GiveAbility(UMHGZCoAttackRejectedTestAbility::StaticClass());
	TestNull(TEXT("failed Commit leaves no active successor"), F.Activate(Rejected));
	TestEqual(TEXT("failed Commit preserves command serial"), F.Sampler->GetCommandSerial(), Serial);
	TestTrue(TEXT("failed Commit preserves registered predecessor"), F.H.Host->IsActionRegistered(F.Ability->GetActionToken()));
	F.At(.35, F.Origin); F.Ability->DisableCollision(0);
	F.At(.51, F.Origin);
	TestEqual(TEXT("original opportunity still hits after failure"), F.KinsectHits, 1);
	F.At(.55, F.Origin); F.Ability->EnableCollision(1);
	const auto Other = F.H.GiveAbility(UMHGZCoAttackNonCoopTestAbility::StaticClass());
	TestNotNull(TEXT("successful non-cooperative action"), F.Activate(Other));
	TestFalse(TEXT("registration revokes stream before new montage"), F.Sampler->IsRunning());
	F.At(.76, F.Origin);
	TestEqual(TEXT("unplayed old window is canceled"), F.KinsectHits, 1);
	F.H.ASC->CancelAbilityHandle(Other); F.H.ASC->ClearAbility(Other); F.H.ASC->ClearAbility(Rejected);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZCoAttackM3CancelTest,
	"MHGZ.Maintenance.KinsectCoAttack.M3.EndReasonsAndTailPreemption",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZCoAttackM3CancelTest::RunTest(const FString&)
{
	for (int32 Mode = 0; Mode < 10; ++Mode)
	{
		FCoAttackFixture F;
		if (!TestTrue(TEXT("fixture"), F.Build())) return false;
		F.WarmUp(); F.At(.3, F.Origin); F.Ability->EnableCollision(0); F.At(.35, F.Origin);
		const FWeaponActionToken Old = F.Ability->GetActionToken();
		const uint64 Serial = F.Sampler->GetCommandSerial();
		if (Mode == 0) F.H.ASC->CancelAbilityHandle(F.Handle); // no RequestEndAction: Normal getter is not sufficient
		else if (Mode == 1) F.Ability->RequestEndAction(EWeaponActionEndReason::Interrupted);
		else
		{
			F.Ability->RequestEndAction(EWeaponActionEndReason::Normal);
			if (Mode == 2) TestTrue(TEXT("manual recall preempts tail"), F.H.Resource->RecallKinsect());
			if (Mode == 3)
			{
				FKinsectFlightRequest R;
				R.RuntimeToken = Old.RuntimeToken; R.FlightInstanceID = FGuid::NewGuid();
				R.FlightSpeed = 2000; R.MaxDistance = 1000; R.ArrivalRadius = 10;
				TestTrue(TEXT("manual flight preempts tail"), F.H.Resource->DeployKinsect(R));
			}
			if (Mode == 4) F.H.Resource->TickComponent(1000.f, LEVELTICK_All, nullptr);
			if (Mode == 5) F.H.Character->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
			if (Mode == 6) F.H.Host->ShutdownRuntime(EWeaponRuntimeEndReason::WeaponChanged);
			if (Mode == 7) F.Weapon->DestroyComponent();
			if (Mode == 8)
			{
				const auto Special = F.H.MakeActionToken(UMHGZSendKinsectAbility::StaticClass());
				F.H.Host->RegisterAction(Special);
				TestTrue(TEXT("special attachment can take ownership"),
					F.H.Resource->BeginKinsectActionAttachment(Special, F.Weapon, TEXT("IG_FrontTip")));
			}
			if (Mode == 9) F.H.ASC->AddLooseGameplayTag(M3::Tag(TEXT("Combat.State.Dead")));
		}
		if (Mode != 6) F.At(.51, F.Origin);
		TestFalse(FString::Printf(TEXT("case %d invalidates stream"), Mode), F.Sampler->IsRunning());
		TestEqual(TEXT("revoked tail cannot hit"), F.KinsectHits, 0);
		if (Mode != 6)
		{
			F.H.Resource->EndKinsectCoAttack(Old);
			TestFalse(TEXT("old pose rejected after takeover"),
				F.H.Kinsect->ApplyCoAttackPose(Serial, F.Origin, FVector::ForwardVector));
			if (Mode == 3) TestEqual(TEXT("old end does not recall manual flight"), F.H.Kinsect->State, EKinsectState::Flying);
			if (Mode == 8) TestEqual(TEXT("old end does not detach special socket"), F.H.Kinsect->State, EKinsectState::ActionAttached);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZCoAttackM3AnchorTest,
	"MHGZ.Maintenance.KinsectCoAttack.M3.IncompatibleAnchorAndReentrantSuccessor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZCoAttackM3AnchorTest::RunTest(const FString&)
{
	for (const bool Reentrant : {false, true})
	{
		FCoAttackFixture F;
		if (!TestTrue(TEXT("fixture"), F.Build())) return false;
		F.WarmUp(); F.At(.3, F.Origin); F.Ability->EnableCollision(0);
		F.At(.35, F.Origin); F.Ability->DisableCollision(0);
		FGameplayAbilitySpecHandle NextHandle;
		UMHGZCoAttackTestAbility* Next = nullptr;
		if (!Reentrant)
		{
			F.Ability->RequestEndAction(EWeaponActionEndReason::Superseded);
			NextHandle = F.H.GiveAbility(UMHGZCoAttackOtherAnchorTestAbility::StaticClass());
			Next = F.Activate(NextHandle);
			TestNotNull(TEXT("action itself remains playable"), Next);
			TestFalse(TEXT("different anchor stops, never creates a teleport chord"), F.Sampler->IsRunning());
		}
		else
		{
			F.Target->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
				[&](UAbilitySystemComponent*, const FGameplayEffectSpec& S, FActiveGameplayEffectHandle)
				{
					const auto* C = FMHGZGameplayEffectContext::ExtractEffectContext(S.GetContext());
					if (!C || C->DamageSourceType != EMHGZDamageSourceType::Kinsect || NextHandle.IsValid()) return;
					F.Ability->RequestEndAction(EWeaponActionEndReason::Superseded);
					NextHandle = F.H.GiveAbility(UMHGZCoAttackTestAbility::StaticClass());
					Next = F.Activate(NextHandle);
					if (Next) Next->EnableCollision(0);
				});
		}
		F.At(.51, F.Origin);
		if (Reentrant)
		{
			if (!TestNotNull(TEXT("successor committed inside damage callback"), Next)) return false;
			TestTrue(TEXT("old replay cannot retire new current source"), F.Sampler->GetActionToken() == Next->GetActionToken());
			F.At(.56, F.Origin); Next->DisableCollision(0);
			F.At(.7, F.Origin); F.At(.72, F.Origin); F.At(.77, F.Origin);
			TestEqual(TEXT("old and new receipt both settle once under reentry"), F.KinsectHits, 2);
		}
		else TestEqual(TEXT("incompatible successor canceled old pending hit"), F.KinsectHits, 0);
		F.H.ASC->CancelAbilityHandle(NextHandle); F.H.ASC->ClearAbility(NextHandle);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZCoAttackM3AutoMarkTest,
	"MHGZ.Maintenance.KinsectCoAttack.M3.AutoMarkCannotStealTail",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZCoAttackM3AutoMarkTest::RunTest(const FString&)
{
	FCoAttackFixture F;
	if (!TestTrue(TEXT("fixture"), F.Build())) return false;
	F.WarmUp(); F.At(.3, F.Origin);
	TestTrue(TEXT("real mark accepted"), F.H.Resource->SetKinsectMark(F.Part,
		F.Part->GetComponentLocation(), nullptr, FVector::ForwardVector));
	F.Ability->EnableCollision(0); F.At(.35, F.Origin);
	const uint64 Serial = F.Sampler->GetCommandSerial();
	const FWeaponRuntimeToken Runtime = F.Ability->GetActionToken().RuntimeToken;
	F.Ability->RequestEndAction(EWeaponActionEndReason::Normal);
	for (const double T : {.4, .45, .51})
	{
		F.At(T, F.Origin); F.H.Resource->TickComponent(.05f, LEVELTICK_All, nullptr);
		TestTrue(TEXT("auto mark cannot acquire source-less legal tail"),
			F.H.Resource->IsKinsectCommandCurrent(Serial, EKinsectCommandSource::CoAttack, 0, Runtime));
		TestEqual(TEXT("tail remains cooperative"), F.H.Kinsect->State, EKinsectState::CoAttack);
	}
	F.At(.56, F.Origin);
	TestFalse(TEXT("sampler eventually retires"), F.Sampler->IsRunning());
	TestEqual(TEXT("normal return wins before re-evaluation"), F.H.Kinsect->State, EKinsectState::Returning);
	return true;
}
#endif
