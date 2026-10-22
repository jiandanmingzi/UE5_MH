// Copyright MHGZ Project. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "HAL/PlatformProcess.h"
#include "UObject/UnrealType.h"
#include "ActionSystem/MHGZM1PlaceholderAbilities.h"
#include "../Shared/MHGZM3TestHarness.h"
#include "../Shared/MHGZM4TestTypes.h"
#include "ActionSystem/MHGZComboCoordinatorAbility.h"

namespace
{
template<typename T> T* Active(FMHGZM3Harness& H, FGameplayAbilitySpecHandle Handle)
{
	const auto* Spec = H.ASC->FindAbilitySpecFromHandle(Handle);
	if (!Spec) { return nullptr; }
	for (UGameplayAbility* Instance : Spec->GetAbilityInstances())
	{
		if (auto* Result = Cast<T>(Instance); Result && Result->IsActive()) { return Result; }
	}
	return nullptr;
}
FWeaponInputSnapshot Command(const TCHAR* Tag, uint32 Sequence, EDirectionalInput Direction = EDirectionalInput::Back)
{
	auto Input = M3::MakePosedInput(true, false);
	Input.ResolvedInputTag = M3::Tag(Tag);
	Input.SourceControlTag = Input.ResolvedInputTag;
	Input.SequenceID = Sequence;
	Input.Phase = EWeaponInputPhase::Started;
	Input.Direction = Direction;
	Input.RawMoveInput = FVector2D(.1f, -.9f);
	Input.WorldDirection = FVector(-1.f, 0.f, 0.f);
	Input.ActorForward = FVector::ForwardVector;
	return Input;
}
UMHGZWeaponComboData* Configure(FMHGZM3Harness& H,
	TSubclassOf<UGameplayAbility> Target = UMHGZM1PlaceholderActionA::StaticClass())
{
	H.Host->SetSheathed(false);
	H.GiveAbility(Target);
	auto* Coordinator = H.ASC->GetActiveComboCoordinator();
	auto* Data = NewObject<UMHGZWeaponComboData>(Coordinator);
	auto& Edge = Data->Transitions.AddDefaulted_GetRef();
	Edge.TransitionID = TEXT("DodgePreInputIdleAttack");
	Edge.SourceState = TEXT("Idle");
	Edge.TargetState = TEXT("DodgePreInputAttack");
	Edge.InputTag = M3::Tag(TEXT("Input.Weapon.Y"));
	Edge.AbilityClass = Target;
	Edge.BlockedTags.AddTag(M3::Tag(TEXT("Combat.State.Dodging")));
	Coordinator->InjectComboData(Data);
	return Data;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZDodgePreInputOrder,
	"MHGZ.M4.Input.PreInput.DodgeReleasePriorityAndFrozenLatestDirection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZDodgePreInputOrder::RunTest(const FString&)
{
	for (auto Direction : {EDirectionalInput::Forward, EDirectionalInput::Left,
		EDirectionalInput::Right, EDirectionalInput::Back})
	{
		FMHGZM3Harness H;
		if (!H.Setup()) { H.Teardown(); return false; }
		Configure(H);
		const auto Handle = H.GiveAbility(UMHGZM4UnrestrictedDodgeAbility::StaticClass());
		TestTrue(TEXT("source Dodge starts"), H.TryActivateWithInput(Handle, Command(TEXT("Input.Dodge"), 10, Direction)));
		auto* Source = Active<UMHGZM4UnrestrictedDodgeAbility>(H, Handle);
		if (!TestNotNull(TEXT("source is live"), Source)) { H.Teardown(); return false; }
		Source->SetLiveMovementInputForTest(true);
		H.ASC->HandleResolvedInputSnapshot(Command(TEXT("Input.Weapon.Y"), 11));
		const auto Latest = Command(TEXT("Input.Dodge"), 12);
		H.ASC->HandleResolvedInputSnapshot(Latest);
		TestTrue(TEXT("Core is not canceled"), Source->IsActive());
		TestFalse(TEXT("weapon does not activate in Core"), H.ASC->GetActiveComboCoordinator()->GetActiveTransition().IsSet());
		Source->EnterDodgeSectionForTest(TEXT("IdleExit"));
		if (Direction == EDirectionalInput::Forward)
		{
			TestTrue(TEXT("forward waits for the selected MoveExit section entry"), Source->IsActive());
			TestFalse(TEXT("redirect alone is not action permission"), Source->IsActionRecoveryOpen());
			Source->EnterDodgeSectionForTest(TEXT("MoveExit"));
		}
		auto* Next = Active<UMHGZM4UnrestrictedDodgeAbility>(H, Handle);
		if (TestNotNull(TEXT("latest Dodge starts before ordinary movement"), Next))
		{
			TestTrue(TEXT("new instance replaces exact source"), Next != Source && !Source->IsActive());
			const auto& Frozen = Next->GetWeaponActivationContext().Input;
			TestEqual(TEXT("latest input retained"), Frozen.SequenceID, 12u);
			TestEqual(TEXT("Back does not get reclassified after rotation"), Frozen.Direction, Latest.Direction);
			TestEqual(TEXT("world direction retained"), Frozen.WorldDirection, Latest.WorldDirection);
			TestEqual(TEXT("physical axes retained"), Frozen.RawMoveInput, Latest.RawMoveInput);
			TestTrue(TEXT("new root-motion owner survives old cleanup"), H.Host->IsMontageRootMotionOwnedBy(Next->GetActionToken()));
			Next->InterruptForTest();
		}
		H.Teardown();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZForwardDodgePreInputBothExits,
	"MHGZ.M4.Input.PreInput.ForwardDodgeBothExitsConsumeWeaponBeforeMovement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZForwardDodgePreInputBothExits::RunTest(const FString&)
{
	for (bool bMoveExit : {false, true})
	{
		FMHGZM3Harness H;
		if (!H.Setup()) { H.Teardown(); return false; }
		Configure(H);
		const auto Handle = H.GiveAbility(UMHGZM4TestDodgeAbility::StaticClass());
		TestTrue(TEXT("forward starts"), H.TryActivateWithInput(Handle,
			Command(TEXT("Input.Dodge"), 100, EDirectionalInput::Forward)));
		auto* Source = Active<UMHGZM4TestDodgeAbility>(H, Handle);
		if (!TestNotNull(TEXT("forward buffer source"), Source)) { H.Teardown(); return false; }
		H.ASC->HandleResolvedInputSnapshot(Command(TEXT("Input.Weapon.Y"), 101));
		TestFalse(TEXT("Core keeps attack buffered"), H.ASC->GetActiveComboCoordinator()->GetActiveTransition().IsSet());
		Source->SetLiveMovementInputForTest(bMoveExit);
		Source->EnterDodgeSectionForTest(TEXT("IdleExit"));
		if (bMoveExit)
		{
			TestTrue(TEXT("pending redirect retains old action"), Source->IsActive());
			TestFalse(TEXT("no consumption before actual MoveExit entry"),
				H.ASC->GetActiveComboCoordinator()->GetActiveTransition().IsSet());
			Source->EnterDodgeSectionForTest(TEXT("MoveExit"));
		}
		TestTrue(TEXT("exit entry consumes buffered weapon input"), H.ASC->GetActiveComboCoordinator()->GetActiveTransition().IsSet());
		TestFalse(TEXT("committed attack supersedes forward exit"), Source->IsActive());
		TestEqual(TEXT("source ends as superseded"), Source->GetActionEndReason(), EWeaponActionEndReason::Superseded);
		TestFalse(TEXT("old montage root ownership released before attack"), H.Host->IsMontageRootMotionOwned());
		H.Teardown();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZPreInputPerAbilityOptOut,
	"MHGZ.M4.Input.PreInput.GAOptOutOnlyBlocksQueuedActivation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZPreInputPerAbilityOptOut::RunTest(const FString&)
{
	const auto* Property = FindFProperty<FBoolProperty>(UMHGZGameplayAbility::StaticClass(), TEXT("bAllowPreInputActivation"));
	TestTrue(TEXT("GA class defaults expose the switch"), Property && Property->HasAnyPropertyFlags(CPF_Edit)
		&& Property->HasAnyPropertyFlags(CPF_DisableEditOnInstance));
	TestFalse(TEXT("AirDodge keeps the opted-out default"), GetDefault<UMHGZAirDodgeAbility>()->bAllowPreInputActivation);
	auto* Target = GetMutableDefault<UMHGZM1PlaceholderActionA>();
	TGuardValue<bool> TargetGuard(Target->bAllowPreInputActivation, true);
	for (bool bDisableAtCapture : {true, false})
	{
		FMHGZM3Harness H;
		if (!H.Setup()) { H.Teardown(); return false; }
		Configure(H);
		const auto Handle = H.GiveAbility(UMHGZM4UnrestrictedDodgeAbility::StaticClass());
		H.TryActivateWithInput(Handle, Command(TEXT("Input.Dodge"), 20));
		auto* Source = Active<UMHGZM4UnrestrictedDodgeAbility>(H, Handle);
		if (!TestNotNull(TEXT("opt-out source"), Source)) { H.Teardown(); return false; }
		Target->bAllowPreInputActivation = !bDisableAtCapture;
		H.ASC->HandleResolvedInputSnapshot(Command(TEXT("Input.Weapon.Y"), 21));
		Target->bAllowPreInputActivation = false;
		Source->EnterDodgeSectionForTest(TEXT("IdleExit"));
		TestFalse(TEXT("disabled at capture or consumption never auto-fires"), H.ASC->GetActiveComboCoordinator()->GetActiveTransition().IsSet());
		TestTrue(TEXT("rejected queue retains old recovery"), Source->IsDirectionalRecoveryOpen());
		H.ASC->HandleResolvedInputSnapshot(Command(TEXT("Input.Weapon.Y"), 22));
		TestTrue(TEXT("the same disabled GA still accepts immediate input"), H.ASC->GetActiveComboCoordinator()->GetActiveTransition().IsSet());
		TestFalse(TEXT("successful immediate action retires source"), Source->IsActive());
		H.Teardown();
	}
	// The direct route checks the same class-default policy, not only Combo edges.
	auto* Direct = GetMutableDefault<UMHGZM4UnrestrictedDodgeAbility>();
	TGuardValue<bool> DirectGuard(Direct->bAllowPreInputActivation, false);
	FMHGZM3Harness H;
	if (!H.Setup()) { H.Teardown(); return false; }
	Configure(H);
	const auto Handle = H.GiveAbility(UMHGZM4UnrestrictedDodgeAbility::StaticClass());
	H.TryActivateWithInput(Handle, Command(TEXT("Input.Dodge"), 23));
	auto* Source = Active<UMHGZM4UnrestrictedDodgeAbility>(H, Handle);
	if (TestNotNull(TEXT("direct policy source"), Source))
	{
		H.ASC->HandleResolvedInputSnapshot(Command(TEXT("Input.Dodge"), 24));
		Source->EnterDodgeSectionForTest(TEXT("IdleExit"));
		TestTrue(TEXT("disabled direct queue leaves source active"), Source->IsActive());
		H.ASC->HandleResolvedInputSnapshot(Command(TEXT("Input.Dodge"), 25));
		const auto* Next = Active<UMHGZM4UnrestrictedDodgeAbility>(H, Handle);
		TestTrue(TEXT("disabled direct GA still starts immediately"), Next && Next != Source && !Source->IsActive());
	}
	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZDodgePreInputInvalidation,
	"MHGZ.M4.Input.PreInput.DodgeExpiryCancelStaleSourceAndCostFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZDodgePreInputInvalidation::RunTest(const FString&)
{
	for (int32 Case = 0; Case < 6; ++Case)
	{
		FMHGZM3Harness H;
		if (!H.Setup()) { H.Teardown(); return false; }
		auto* Data = Configure(H, Case == 3 ? UMHGZM1CommitFailureAbility::StaticClass()
			: UMHGZM1PlaceholderActionA::StaticClass());
		if (Case == 0) { Data->PreInputLifetime = .001f; }
		if (Case == 5) { Data->PreInputLifetime = 0.f; }
		const auto Handle = H.GiveAbility(UMHGZM4UnrestrictedDodgeAbility::StaticClass());
		H.TryActivateWithInput(Handle, Command(TEXT("Input.Dodge"), 30));
		auto* Source = Active<UMHGZM4UnrestrictedDodgeAbility>(H, Handle);
		if (!TestNotNull(TEXT("invalidation source"), Source)) { H.Teardown(); return false; }
		const auto Token = Source->GetActionToken();
		H.ASC->HandleResolvedInputSnapshot(Command(TEXT("Input.Weapon.Y"), 31));
		if (Case == 0) { FPlatformProcess::SleepNoStats(.005f); }
		if (Case == 1 || Case == 4)
		{
			if (Case == 4) { H.ASC->CancelAbilityHandle(Handle); }
			else { Source->InterruptForTest(); }
			H.TryActivateWithInput(Handle, Command(TEXT("Input.Dodge"), 32));
			Source = Active<UMHGZM4UnrestrictedDodgeAbility>(H, Handle);
		}
		if (Case == 2)
		{
			auto Stale = Token;
			++Stale.ActivationSequenceID;
			H.ASC->GetActiveComboCoordinator()->OnDodgePreInputWindowOpened(Stale);
			TestTrue(TEXT("foreign source cannot unlock or consume"), Source->IsActive());
		}
		Source->EnterDodgeSectionForTest(TEXT("IdleExit"));
		const bool bCommitted = H.ASC->GetActiveComboCoordinator()->GetActiveTransition().IsSet();
		TestEqual(TEXT("only exact live valid queue can commit"), bCommitted, Case == 2);
		if (Case != 2)
		{
			TestTrue(TEXT("expired/canceled/failed target does not cancel recovery"), Source->IsDirectionalRecoveryOpen());
			TestTrue(TEXT("failed successor keeps original root owner"), H.Host->IsMontageRootMotionOwnedBy(Source->GetActionToken()));
		}
		H.Teardown();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZDodgePreInputWeaponAndGate,
	"MHGZ.M4.Input.PreInput.DodgeWeaponSuccessAndDirectionGateRecheck",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZDodgePreInputWeaponAndGate::RunTest(const FString&)
{
	for (bool bWeapon : {true, false})
	{
		FMHGZM3Harness H;
		if (!H.Setup()) { H.Teardown(); return false; }
		Configure(H);
		const auto Handle = H.GiveAbility(UMHGZM4TestDodgeAbility::StaticClass());
		H.TryActivateWithInput(Handle, Command(TEXT("Input.Dodge"), 40, EDirectionalInput::Forward));
		auto* Source = Active<UMHGZM4TestDodgeAbility>(H, Handle);
		if (!TestNotNull(TEXT("forward source"), Source)) { H.Teardown(); return false; }
		H.ASC->HandleResolvedInputSnapshot(Command(bWeapon ? TEXT("Input.Weapon.Y") : TEXT("Input.Dodge"), 41));
		Source->FinishNormallyForTest();
		TestEqual(TEXT("weapon commits, Back retains live Attacking requirement"),
			H.ASC->GetActiveComboCoordinator()->GetActiveTransition().IsSet(), bWeapon);
		TestNull(TEXT("closed Back direction never turns into a forward roll"), Active<UMHGZM4TestDodgeAbility>(H, Handle));
		H.Teardown();
	}
	return true;
}
#endif
