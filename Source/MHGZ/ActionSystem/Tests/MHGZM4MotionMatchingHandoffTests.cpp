// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "MHGZM3TestHarness.h"
#include "MHGZM4TestTypes.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

namespace
{
UMHGZM4TestAttackAbility* GetActiveAttack(UMHGZAbilitySystemComponent& ASC,
	const FGameplayAbilitySpecHandle& Handle)
{
	const FGameplayAbilitySpec* Spec = ASC.FindAbilitySpecFromHandle(Handle);
	if (!Spec)
	{
		return nullptr;
	}
	for (UGameplayAbility* Instance : Spec->GetAbilityInstances())
	{
		if (UMHGZM4TestAttackAbility* Attack = Cast<UMHGZM4TestAttackAbility>(Instance))
		{
			return Attack;
		}
	}
	return nullptr;
}

bool ActivateTestAttack(FMHGZM3Harness& Harness,
	const FGameplayAbilitySpecHandle& Handle, FAutomationTestBase& Test,
	UMHGZM4TestAttackAbility*& OutAttack)
{
	FWeaponInputSnapshot Input = M3::MakePosedInput(true, true);
	Input.ResolvedInputTag = M3::Tag(TEXT("Input.Weapon.Y"));
	Input.SourceControlTag = Input.ResolvedInputTag;
	Input.Phase = EWeaponInputPhase::Started;
	if (!Test.TestTrue(TEXT("test attack activates"), Harness.TryActivateWithInput(Handle, Input)))
	{
		return false;
	}
	OutAttack = GetActiveAttack(*Harness.ASC, Handle);
	Test.TestNotNull(TEXT("test attack instance exists"), OutAttack);
	return OutAttack != nullptr;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM4RootMotionPhaseIsExact,
	"MHGZ.M4.4.Handoff.RootMotionPhaseIsExact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM4RootMotionPhaseIsExact::RunTest(const FString& Parameters)
{
	FMHGZM3Harness H;
	if (!H.Setup())
	{
		AddError(TEXT("M4.4 root-motion phase harness setup failed"));
		H.Teardown();
		return false;
	}

	const FGameplayAbilitySpecHandle Handle =
		H.GiveAbility(UMHGZM4TestAttackAbility::StaticClass());
	UMHGZM4TestAttackAbility* Attack = nullptr;
	if (!ActivateTestAttack(H, Handle, *this, Attack))
	{
		H.Teardown();
		return false;
	}

	const FWeaponActionToken Token = Attack->GetActionToken();
	FWeaponActionToken WrongToken = Token;
	++WrongToken.ActivationSequenceID;
	TestTrue(TEXT("exact action starts a root-motion phase"),
		Attack->BeginActionRootMotionPhase(Token, true, false));
	TestTrue(TEXT("phase acquires the exact root-motion owner"),
		H.Host->IsMontageRootMotionOwnedBy(Token));
	TestFalse(TEXT("wrong token cannot end the phase"),
		Attack->EndActionRootMotionPhase(WrongToken, true, false));
	TestTrue(TEXT("wrong Notify end cannot release the real owner"),
		H.Host->IsMontageRootMotionOwnedBy(Token));
	TestFalse(TEXT("handoff cannot run while a root-motion phase is active"),
		Attack->HandleMotionMatchingHandoff(Token,
			EMHGZMotionMatchingHandoffType::AttackExit));
	TestTrue(TEXT("failed active-phase handoff keeps root-motion ownership"),
		H.Host->IsMontageRootMotionOwnedBy(Token));
	TestTrue(TEXT("exact action ends its root-motion phase"),
		Attack->EndActionRootMotionPhase(Token, true, false));
	TestTrue(TEXT("phase end keeps ownership until a safe handoff validates"),
		H.Host->IsMontageRootMotionOwnedBy(Token));
	TestTrue(TEXT("safe handoff atomically releases the exact owner"),
		Attack->HandleMotionMatchingHandoff(Token,
			EMHGZMotionMatchingHandoffType::AttackExit));
	TestFalse(TEXT("successful handoff releases root-motion ownership"),
		H.Host->IsMontageRootMotionOwned());
	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM4HandoffPublishesAndConsumesExactPayload,
	"MHGZ.M4.4.Handoff.PublishesAndConsumesExactPayload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM4HandoffPublishesAndConsumesExactPayload::RunTest(const FString& Parameters)
{
	FMHGZM3Harness H;
	if (!H.Setup())
	{
		AddError(TEXT("M4.4 handoff payload harness setup failed"));
		H.Teardown();
		return false;
	}

	const FGameplayAbilitySpecHandle Handle =
		H.GiveAbility(UMHGZM4TestAttackAbility::StaticClass());
	UMHGZM4TestAttackAbility* Attack = nullptr;
	if (!ActivateTestAttack(H, Handle, *this, Attack))
	{
		H.Teardown();
		return false;
	}

	const FWeaponActionToken Token = Attack->GetActionToken();
	// The test Ability's raw-input seam mirrors the production physical-stick
	// read while retaining the existing lightweight ACharacter harness.
	Attack->SetRawMoveInputForTest(FVector2D(0.0f, 1.0f));
	TestTrue(TEXT("mobile phase starts and observes raw input"),
		Attack->BeginActionRootMotionPhase(Token, true, true));
	Attack->SetRawMoveInputForTest(FVector2D::ZeroVector);
	TestTrue(TEXT("mobile phase observes the later raw release"),
		Attack->ObserveActionRootMotionPhase(Token));
	TestTrue(TEXT("mobile phase ends before handoff"),
		Attack->EndActionRootMotionPhase(Token, true, true));
	TestTrue(TEXT("safe-frame handoff succeeds after the root-motion phase closes"),
		Attack->HandleMotionMatchingHandoff(Token,
			EMHGZMotionMatchingHandoffType::AttackExit));
	TestFalse(TEXT("successful handoff releases the source root-motion owner"),
		H.Host->IsMontageRootMotionOwned());

	FWeaponMotionMatchingHandoff Handoff;
	TestTrue(TEXT("Host exposes the published handoff"),
		H.Host->GetPendingMotionMatchingHandoff(Handoff));
	TestEqual(TEXT("handoff keeps its configured family"), Handoff.Type,
		EMHGZMotionMatchingHandoffType::AttackExit);
	TestEqual(TEXT("handoff keeps the exact activation sequence"),
		Handoff.ActivationSequenceID, static_cast<int32>(Token.ActivationSequenceID));
	TestTrue(TEXT("mobile input is recorded independently of BlockMovement"),
		Handoff.bHadRawMoveInputInMobilePhase);
	TestTrue(TEXT("mobile release arms exactly one pending Stop"),
		Handoff.bPendingStopAtHandoff);
	TestTrue(TEXT("handoff normal completion ends the source GA"),
		GetActiveAttack(*H.ASC, Handle) == nullptr);
	TestFalse(TEXT("stale serial cannot consume a newer handoff"),
		H.Host->ClearPendingMotionMatchingHandoff(Handoff.Serial + 1));
	TestTrue(TEXT("exact serial consumes the handoff"),
		H.Host->ClearPendingMotionMatchingHandoff(Handoff.Serial));
	TestFalse(TEXT("consumed payload is no longer exposed"),
		H.Host->GetPendingMotionMatchingHandoff(Handoff));

	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM4HandoffRejectsUnsafeOrUndeclaredRoutes,
	"MHGZ.M4.4.Handoff.RejectsUnsafeOrUndeclaredRoutes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM4HandoffRejectsUnsafeOrUndeclaredRoutes::RunTest(const FString& Parameters)
{
	FMHGZM3Harness H;
	if (!H.Setup())
	{
		AddError(TEXT("M4.4 handoff rejection harness setup failed"));
		H.Teardown();
		return false;
	}

	const FGameplayAbilitySpecHandle Handle =
		H.GiveAbility(UMHGZM4TestAttackAbility::StaticClass());
	UMHGZM4TestAttackAbility* Attack = nullptr;
	if (!ActivateTestAttack(H, Handle, *this, Attack))
	{
		H.Teardown();
		return false;
	}

	const FWeaponActionToken Token = Attack->GetActionToken();
	TestTrue(TEXT("phase starts before route validation"),
		Attack->BeginActionRootMotionPhase(Token, false, false));
	TestTrue(TEXT("phase ends without creating an owner for in-place content"),
		Attack->EndActionRootMotionPhase(Token, false, false));
	TestFalse(TEXT("undeclared route leaves the source action alive"),
		Attack->HandleMotionMatchingHandoff(Token,
			EMHGZMotionMatchingHandoffType::DodgeMoveExit));
	TestNotNull(TEXT("rejected route never ends the action"),
		GetActiveAttack(*H.ASC, Handle));
	FWeaponMotionMatchingHandoff Handoff;
	TestFalse(TEXT("rejected route publishes no payload"),
		H.Host->GetPendingMotionMatchingHandoff(Handoff));

	Attack->RequestEndAction(EWeaponActionEndReason::Normal);
	H.Teardown();
	return true;
}

#endif
