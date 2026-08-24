// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "ActionSystem/MHGZComboCoordinatorAbility.h"
#include "ActionSystem/MHGZM1PlaceholderAbilities.h"
#include "ActionSystem/MHGZSheatheAbility.h"
#include "ActionSystem/MHGZWeaponComboData.h"
#include "AbilitySystemComponent.h"
#include "MHGZM3TestHarness.h"
#include "MHGZM4TestTypes.h"

namespace
{
UMHGZM4TestSheatheAbility* GetActiveSheatheInstance(
	UMHGZAbilitySystemComponent& ASC, const FGameplayAbilitySpecHandle& Handle)
{
	FGameplayAbilitySpec* Spec = ASC.FindAbilitySpecFromHandle(Handle);
	if (!Spec)
	{
		return nullptr;
	}
	for (UGameplayAbility* Instance : Spec->GetAbilityInstances())
	{
		if (UMHGZM4TestSheatheAbility* Sheathe =
			Cast<UMHGZM4TestSheatheAbility>(Instance))
		{
			return Sheathe;
		}
	}
	return nullptr;
}

UMHGZGameplayAbility* GetActiveActionInstance(
	UMHGZAbilitySystemComponent& ASC, const FGameplayAbilitySpecHandle& Handle)
{
	FGameplayAbilitySpec* Spec = ASC.FindAbilitySpecFromHandle(Handle);
	if (!Spec)
	{
		return nullptr;
	}
	for (UGameplayAbility* Instance : Spec->GetAbilityInstances())
	{
		if (UMHGZGameplayAbility* Action = Cast<UMHGZGameplayAbility>(Instance))
		{
			return Action;
		}
	}
	return nullptr;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM4SheatheDefaultsAndSnapshotSection,
	"MHGZ.M4.Sheathe.DefaultsAndSnapshotSection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM4SheatheDefaultsAndSnapshotSection::RunTest(const FString& Parameters)
{
	const UMHGZSheatheAbility* Ability = GetDefault<UMHGZSheatheAbility>();
	TestEqual(TEXT("sheathe uses the generic input tag"), Ability->InputTag,
		M3::Tag(TEXT("Input.Sheathe")));
	TestEqual(TEXT("sheathe has no stamina cost"), Ability->StaminaCostPolicy,
		EAbilityStaminaCostPolicy::None);
	TestEqual(TEXT("idle section default"), Ability->IdleSectionName,
		FName(TEXT("Idle")));
	TestEqual(TEXT("walk section default"), Ability->WalkSectionName,
		FName(TEXT("Walk")));

	FWeaponInputSnapshot IdleInput = M3::MakePosedInput(true, false);
	IdleInput.Direction = EDirectionalInput::None;
	TestEqual(TEXT("neutral frozen input selects idle section"),
		Ability->SelectSectionName(IdleInput), FName(TEXT("Idle")));

	FWeaponInputSnapshot WalkInput = IdleInput;
	WalkInput.Direction = EDirectionalInput::Forward;
	TestEqual(TEXT("directional frozen input selects walk section"),
		Ability->SelectSectionName(WalkInput), FName(TEXT("Walk")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM4SheatheCommitOwnsPoseChange,
	"MHGZ.M4.Sheathe.CommitOwnsPoseChange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM4SheatheCommitOwnsPoseChange::RunTest(const FString& Parameters)
{
	FMHGZM3Harness H;
	if (!H.Setup())
	{
		AddError(TEXT("M4 sheathe harness setup failed"));
		H.Teardown();
		return false;
	}

	TestTrue(TEXT("test enters unsheathed pose"), H.Host->SetSheathed(false));
	const FGameplayAbilitySpecHandle Handle =
		H.GiveAbility(UMHGZM4TestSheatheAbility::StaticClass());
	FWeaponInputSnapshot Input = M3::MakePosedInput(true, false);
	Input.ResolvedInputTag = M3::Tag(TEXT("Input.Sheathe"));
	Input.Direction = EDirectionalInput::None;
	const FGameplayTag BlockMovement = M3::Tag(TEXT("Combat.State.BlockMovement"));
	const FGameplayTag Sheathing = M3::Tag(TEXT("Combat.State.Sheathing"));

	// Missing commit is an asset error: completion must preserve Unsheathed.
	TestTrue(TEXT("sheathe activation starts"), H.TryActivateWithInput(Handle, Input));
	TestFalse(TEXT("pose stays unsheathed while montage is active"),
		H.Host->IsSheathed());
	TestEqual(TEXT("idle sheathe owns one movement block"),
		H.ASC->GetTagCount(BlockMovement), 1);
	TestEqual(TEXT("active sheathe owns one action input lock"),
		H.ASC->GetTagCount(Sheathing), 1);
	TestTrue(TEXT("active sheathe owns montage root motion"),
		H.Host->IsMontageRootMotionOwned());

	UMHGZM4TestSheatheAbility* Active = GetActiveSheatheInstance(*H.ASC, Handle);
	TestNotNull(TEXT("active sheathe instance exists"), Active);
	if (Active)
	{
		Active->FinishNormallyForTest();
	}
	TestFalse(TEXT("completion without commit preserves unsheathed pose"),
		H.Host->IsSheathed());
	TestNull(TEXT("completion without commit ends the ability instance"),
		GetActiveSheatheInstance(*H.ASC, Handle));
	TestEqual(TEXT("completion releases idle movement block"),
		H.ASC->GetTagCount(BlockMovement), 0);
	TestEqual(TEXT("completion releases sheathe input lock"),
		H.ASC->GetTagCount(Sheathing), 0);
	TestFalse(TEXT("completion releases montage root motion"),
		H.Host->IsMontageRootMotionOwned());

	// Exact commit owns the pose change and is idempotent.
	TestTrue(TEXT("committing sheathe activation starts"),
		H.TryActivateWithInput(Handle, Input));
	Active = GetActiveSheatheInstance(*H.ASC, Handle);
	TestNotNull(TEXT("committing sheathe instance exists"), Active);
	if (Active)
	{
		FWeaponActionToken WrongAction = Active->GetActionToken();
		++WrongAction.ActivationSequenceID;
		TestFalse(TEXT("wrong action token cannot commit sheathe"),
			Active->CommitSheathe(WrongAction));
		TestTrue(TEXT("exact action token commits sheathe"), Active->CommitForTest());
		TestTrue(TEXT("repeated exact commit is idempotent"), Active->CommitForTest());
	}
	TestTrue(TEXT("commit applies sheathed pose before montage completion"),
		H.Host->IsSheathed());
	TestEqual(TEXT("commit does not release sheathe input lock"),
		H.ASC->GetTagCount(Sheathing), 1);
	TestTrue(TEXT("montage keeps root ownership after commit"),
		H.Host->IsMontageRootMotionOwned());
	if (Active)
	{
		Active->FinishNormallyForTest();
	}
	TestNull(TEXT("committed completion ends the ability instance"),
		GetActiveSheatheInstance(*H.ASC, Handle));
	TestFalse(TEXT("committed completion releases montage root motion"),
		H.Host->IsMontageRootMotionOwned());
	TestEqual(TEXT("committed completion releases sheathe input lock"),
		H.ASC->GetTagCount(Sheathing), 0);
	TestFalse(TEXT("already sheathed pose rejects another activation"),
		H.TryActivateWithInput(Handle, Input));

	// Interruption before commit preserves Unsheathed and cleans both owners.
	TestTrue(TEXT("test re-enters unsheathed pose"), H.Host->SetSheathed(false));
	TestTrue(TEXT("pre-commit interruption activation starts"),
		H.TryActivateWithInput(Handle, Input));
	Active = GetActiveSheatheInstance(*H.ASC, Handle);
	TestNotNull(TEXT("pre-commit interruption instance exists"), Active);
	if (Active)
	{
		Active->InterruptForTest();
	}
	TestFalse(TEXT("pre-commit interruption preserves unsheathed pose"),
		H.Host->IsSheathed());
	TestEqual(TEXT("pre-commit interruption releases movement block"),
		H.ASC->GetTagCount(BlockMovement), 0);
	TestEqual(TEXT("pre-commit interruption releases sheathe input lock"),
		H.ASC->GetTagCount(Sheathing), 0);
	TestFalse(TEXT("pre-commit interruption releases root ownership"),
		H.Host->IsMontageRootMotionOwned());

	// Walk selection never blocks steering; post-commit interruption keeps Sheathed.
	FWeaponInputSnapshot WalkInput = Input;
	WalkInput.Direction = EDirectionalInput::Forward;
	TestTrue(TEXT("walk sheathe activation starts"),
		H.TryActivateWithInput(Handle, WalkInput));
	Active = GetActiveSheatheInstance(*H.ASC, Handle);
	TestNotNull(TEXT("walk sheathe instance exists"), Active);
	TestEqual(TEXT("walk sheathe does not acquire BlockMovement"),
		H.ASC->GetTagCount(BlockMovement), 0);
	TestTrue(TEXT("walk sheathe still owns montage root motion"),
		H.Host->IsMontageRootMotionOwned());
	TestEqual(TEXT("walk sheathe owns one action input lock"),
		H.ASC->GetTagCount(Sheathing), 1);
	if (Active)
	{
		FGameplayTagContainer TestTags;
		TestTags.AddTag(BlockMovement);
		FWeaponOwnedTagToken EarlyReleaseToken = Active->AcquireActionTags(
			TestTags, FName(TEXT("M4EarlyReleaseA")));
		FWeaponOwnedTagToken RemainingToken = Active->AcquireActionTags(
			TestTags, FName(TEXT("M4EarlyReleaseB")));
		TestTrue(TEXT("two independently owned test tags are valid"),
			EarlyReleaseToken.IsValid() && RemainingToken.IsValid());
		TestEqual(TEXT("two independently owned tags contribute two counts"),
			H.ASC->GetTagCount(BlockMovement), 2);
		TestTrue(TEXT("one exact action tag token releases early"),
			Active->ReleaseActionTag(EarlyReleaseToken));
		TestFalse(TEXT("released action tag token is cleared"),
			EarlyReleaseToken.IsValid());
		TestEqual(TEXT("early release leaves the other owner intact"),
			H.ASC->GetTagCount(BlockMovement), 1);
		TestFalse(TEXT("releasing the cleared token is idempotent"),
			Active->ReleaseActionTag(EarlyReleaseToken));
		TestTrue(TEXT("walk sheathe commit succeeds"), Active->CommitForTest());
		Active->InterruptForTest();
	}
	TestTrue(TEXT("post-commit interruption preserves sheathed pose"),
		H.Host->IsSheathed());
	TestEqual(TEXT("ability end releases the remaining action tag token"),
		H.ASC->GetTagCount(BlockMovement), 0);
	TestEqual(TEXT("post-commit interruption releases sheathe input lock"),
		H.ASC->GetTagCount(Sheathing), 0);
	TestFalse(TEXT("post-commit interruption releases root ownership"),
		H.Host->IsMontageRootMotionOwned());

	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM4SheatheMissingDependenciesPreservePose,
	"MHGZ.M4.Sheathe.MissingDependenciesPreservePose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM4SheatheMissingDependenciesPreservePose::RunTest(const FString& Parameters)
{
	FMHGZM3Harness H;
	if (!H.Setup())
	{
		AddError(TEXT("M4 sheathe harness setup failed"));
		H.Teardown();
		return false;
	}

	H.Host->SetSheathed(false);
	const FGameplayAbilitySpecHandle Handle =
		H.GiveAbility(UMHGZSheatheAbility::StaticClass());
	FWeaponInputSnapshot Input = M3::MakePosedInput(true, false);
	Input.ResolvedInputTag = M3::Tag(TEXT("Input.Sheathe"));
	H.TryActivateWithInput(Handle, Input);

	TestFalse(TEXT("missing montage and anim instance preserve unsheathed pose"),
		H.Host->IsSheathed());
	const FGameplayAbilitySpec* Spec = H.ASC->FindAbilitySpecFromHandle(Handle);
	TestTrue(TEXT("failed dependency check leaves no active ability instance"),
		Spec && Spec->GetAbilityInstances().IsEmpty());

	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM4SheatheBlockedByActionStates,
	"MHGZ.M4.Sheathe.BlockedByActionStates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM4SheatheBlockedByActionStates::RunTest(const FString& Parameters)
{
	FMHGZM3Harness H;
	if (!H.Setup())
	{
		AddError(TEXT("M4 sheathe harness setup failed"));
		H.Teardown();
		return false;
	}

	H.Host->SetSheathed(false);
	const FGameplayAbilitySpecHandle Handle =
		H.GiveAbility(UMHGZM4TestSheatheAbility::StaticClass());
	FWeaponInputSnapshot Input = M3::MakePosedInput(true, false);
	Input.ResolvedInputTag = M3::Tag(TEXT("Input.Sheathe"));

	for (const TCHAR* BlockedTagName : {
		TEXT("Combat.State.Attacking"),
		TEXT("Combat.State.Hitstun"),
		TEXT("Combat.State.Knockdown"),
		TEXT("Combat.State.Dead"),
		TEXT("Combat.State.Sheathing"),
		TEXT("Combat.State.Dodging") })
	{
		const FGameplayTag BlockedTag = M3::Tag(BlockedTagName);
		H.ASC->AddLooseGameplayTag(BlockedTag);
		TestFalse(FString::Printf(TEXT("%s blocks sheathe"), BlockedTagName),
			H.TryActivateWithInput(Handle, Input));
		TestFalse(TEXT("blocked activation does not change pose"),
			H.Host->IsSheathed());
		H.ASC->RemoveLooseGameplayTag(BlockedTag);
	}

	TestTrue(TEXT("test enters live aerial state"), H.Host->SetGrounded(false));
	TestFalse(TEXT("live aerial state rejects a stale grounded sheathe snapshot"),
		H.TryActivateWithInput(Handle, Input));
	TestTrue(TEXT("test returns to live grounded state"), H.Host->SetGrounded(true));

	FWeaponInputSnapshot AerialInput = M3::MakePosedInput(false, false);
	AerialInput.ResolvedInputTag = M3::Tag(TEXT("Input.Sheathe"));
	H.TryActivateWithInput(Handle, AerialInput);
	TestNull(TEXT("aerial snapshot is rejected before a sheathe instance remains active"),
		GetActiveSheatheInstance(*H.ASC, Handle));
	TestEqual(TEXT("aerial snapshot acquires no sheathe action lock"),
		H.ASC->GetTagCount(M3::Tag(TEXT("Combat.State.Sheathing"))), 0);

	FWeaponInputSnapshot SheathedInput = M3::MakePosedInput(true, true);
	SheathedInput.ResolvedInputTag = M3::Tag(TEXT("Input.Sheathe"));
	H.TryActivateWithInput(Handle, SheathedInput);
	TestNull(TEXT("sheathed snapshot is rejected before a sheathe instance remains active"),
		GetActiveSheatheInstance(*H.ASC, Handle));
	TestEqual(TEXT("sheathed snapshot acquires no sheathe action lock"),
		H.ASC->GetTagCount(M3::Tag(TEXT("Combat.State.Sheathing"))), 0);

	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM4PlayerActionLocksGateDirectAndComboInput,
	"MHGZ.M4.ActionLocks.GateDirectAndComboInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM4PlayerActionLocksGateDirectAndComboInput::RunTest(
	const FString& Parameters)
{
	FMHGZM3Harness H;
	if (!H.Setup())
	{
		AddError(TEXT("M4 action-lock harness setup failed"));
		H.Teardown();
		return false;
	}

	const FGameplayAbilitySpecHandle CoordinatorHandle =
		H.GiveAbility(UGA_WeaponComboCoordinator::StaticClass());
	const FGameplayAbilitySpecHandle ActionHandle =
		H.GiveAbility(UMHGZM1PlaceholderActionA::StaticClass());
	TestTrue(TEXT("persistent coordinator activates"),
		H.ASC->TryActivateAbility(CoordinatorHandle));
	UGA_WeaponComboCoordinator* Coordinator = H.ASC->GetActiveComboCoordinator();
	TestNotNull(TEXT("active coordinator exists"), Coordinator);
	if (!Coordinator)
	{
		H.Teardown();
		return false;
	}

	const FGameplayTag YTag = M3::Tag(TEXT("Input.Weapon.Y"));
	UMHGZWeaponComboData* ComboData = NewObject<UMHGZWeaponComboData>(Coordinator);
	FComboTransition& Transition = ComboData->Transitions.AddDefaulted_GetRef();
	Transition.TransitionID = FName(TEXT("IdleToAction"));
	Transition.SourceState = FName(TEXT("Idle"));
	Transition.InputTag = YTag;
	Transition.AbilityClass = UMHGZM1PlaceholderActionA::StaticClass();
	Transition.TargetState = FName(TEXT("Action"));
	Coordinator->InjectComboData(ComboData);

	uint32 SequenceID = 1;
	auto SendWeaponInput = [&Coordinator, &SequenceID, &YTag]()
	{
		FWeaponInputSnapshot Snapshot = M3::MakePosedInput(true, false);
		Snapshot.ResolvedInputTag = YTag;
		Snapshot.SourceControlTag = YTag;
		Snapshot.SequenceID = SequenceID++;
		Snapshot.Phase = EWeaponInputPhase::Started;
		Coordinator->HandleWeaponInput(Snapshot);
	};

	for (const TCHAR* LockTagName : {
		TEXT("Combat.State.Sheathing"),
		TEXT("Combat.State.Dodging") })
	{
		const FGameplayTag LockTag = M3::Tag(LockTagName);
		H.ASC->AddLooseGameplayTag(LockTag);

		FWeaponInputSnapshot DirectInput = M3::MakePosedInput(true, false);
		TestFalse(FString::Printf(TEXT("%s blocks direct player action activation"),
			LockTagName), H.TryActivateWithInput(ActionHandle, DirectInput));
		FWeaponAbilityActivationContext DiscardedContext;
		H.ASC->ConsumePendingActivationContext(ActionHandle, DiscardedContext);

		SendWeaponInput();
		TestEqual(FString::Printf(TEXT("%s keeps combo state idle"), LockTagName),
			Coordinator->GetCurrentState(), FName(TEXT("Idle")));
		TestFalse(FString::Printf(TEXT("%s creates no active transition"), LockTagName),
			Coordinator->GetActiveTransition().IsSet());

		H.ASC->RemoveLooseGameplayTag(LockTag);
		SendWeaponInput();
		TestEqual(FString::Printf(TEXT("input works after %s is released"), LockTagName),
			Coordinator->GetCurrentState(), FName(TEXT("Action")));
		UMHGZGameplayAbility* Active = GetActiveActionInstance(*H.ASC, ActionHandle);
		TestNotNull(TEXT("unlocked transition has an action instance"), Active);
		if (Active)
		{
			Active->RequestEndAction(EWeaponActionEndReason::Normal);
		}
		TestEqual(TEXT("ending unlocked action returns combo to idle"),
			Coordinator->GetCurrentState(), FName(TEXT("Idle")));
	}

	H.Teardown();
	return true;
}

#endif
