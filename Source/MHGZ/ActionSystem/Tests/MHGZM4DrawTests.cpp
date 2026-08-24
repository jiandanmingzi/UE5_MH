// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "MHGZM3TestHarness.h"
#include "MHGZM4TestTypes.h"

namespace
{
template <typename TAbility>
TAbility* GetActiveInstance(UMHGZAbilitySystemComponent& ASC,
	const FGameplayAbilitySpecHandle& Handle)
{
	FGameplayAbilitySpec* Spec = ASC.FindAbilitySpecFromHandle(Handle);
	if (!Spec)
	{
		return nullptr;
	}
	for (UGameplayAbility* Ability : Spec->GetAbilityInstances())
	{
		if (TAbility* Instance = Cast<TAbility>(Ability))
		{
			return Instance;
		}
	}
	return nullptr;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM4DrawCommitOwnsPoseChange,
	"MHGZ.M4.Draw.CommitOwnsPoseChange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM4DrawCommitOwnsPoseChange::RunTest(const FString& Parameters)
{
	FMHGZM3Harness H;
	if (!H.Setup())
	{
		AddError(TEXT("M4 draw harness setup failed"));
		H.Teardown();
		return false;
	}

	const UMHGZDrawAttackAbility* DrawDefaults = GetDefault<UMHGZDrawAttackAbility>();
	TestNotNull(TEXT("production draw defaults exist"), DrawDefaults);
	if (DrawDefaults)
	{
		TestEqual(TEXT("draw defaults to the single Y input"), DrawDefaults->InputTag,
			M3::Tag(TEXT("Input.Weapon.Y")));
		TestEqual(TEXT("draw has no inherent stamina cost"), DrawDefaults->StaminaCostPolicy,
			EAbilityStaminaCostPolicy::None);
	}

	const FGameplayAbilitySpecHandle Handle =
		H.GiveAbility(UMHGZM4TestDrawAbility::StaticClass());
	FWeaponInputSnapshot Input = M3::MakePosedInput(true, true);
	Input.ResolvedInputTag = M3::Tag(TEXT("Input.Weapon.Y"));
	const FGameplayTag Sheathed = M3::Tag(TEXT("Combat.State.Sheathed"));
	const FGameplayTag Unsheathed = M3::Tag(TEXT("Combat.State.Unsheathed"));

	TestTrue(TEXT("sheathed grounded draw activates"),
		H.TryActivateWithInput(Handle, Input));
	UMHGZM4TestDrawAbility* Draw =
		GetActiveInstance<UMHGZM4TestDrawAbility>(*H.ASC, Handle);
	TestNotNull(TEXT("active draw instance exists"), Draw);
	TestTrue(TEXT("activation alone preserves sheathed pose"), H.Host->IsSheathed());
	TestTrue(TEXT("active draw owns montage root motion"), H.Host->IsMontageRootMotionOwned());
	TestEqual(TEXT("activation keeps the sheathed ledger tag"), H.ASC->GetTagCount(Sheathed), 1);
	TestEqual(TEXT("activation has no unsheathed ledger tag"), H.ASC->GetTagCount(Unsheathed), 0);
	if (Draw)
	{
		FWeaponActionToken WrongAction = Draw->GetActionToken();
		++WrongAction.ActivationSequenceID;
		TestFalse(TEXT("wrong action token cannot commit draw"),
			Draw->CommitDraw(WrongAction));
		TestTrue(TEXT("exact action token commits draw"), Draw->CommitForTest());
		TestTrue(TEXT("repeated exact draw commit is idempotent"), Draw->CommitForTest());
	}
	TestFalse(TEXT("DrawCommit changes to unsheathed"), H.Host->IsSheathed());
	TestEqual(TEXT("DrawCommit removes sheathed tag"), H.ASC->GetTagCount(Sheathed), 0);
	TestEqual(TEXT("DrawCommit acquires unsheathed tag"), H.ASC->GetTagCount(Unsheathed), 1);
	if (Draw)
	{
		Draw->InterruptForTest();
	}
	TestFalse(TEXT("post-commit interruption preserves unsheathed pose"),
		H.Host->IsSheathed());
	TestFalse(TEXT("draw end releases montage root motion"), H.Host->IsMontageRootMotionOwned());

	TestTrue(TEXT("test returns to sheathed pose"), H.Host->SetSheathed(true));
	TestTrue(TEXT("pre-commit interruption activation starts"),
		H.TryActivateWithInput(Handle, Input));
	Draw = GetActiveInstance<UMHGZM4TestDrawAbility>(*H.ASC, Handle);
	TestNotNull(TEXT("pre-commit draw instance exists"), Draw);
	if (Draw)
	{
		Draw->InterruptForTest();
	}
	TestTrue(TEXT("pre-commit interruption preserves sheathed pose"), H.Host->IsSheathed());

	TestTrue(TEXT("test enters unsheathed pose"), H.Host->SetSheathed(false));
	TestFalse(TEXT("unsheathed draw activation is rejected"),
		H.TryActivateWithInput(Handle, Input));
	TestTrue(TEXT("test returns to sheathed pose again"), H.Host->SetSheathed(true));
	TestTrue(TEXT("test enters aerial pose"), H.Host->SetGrounded(false));
	TestFalse(TEXT("aerial draw activation is rejected"),
		H.TryActivateWithInput(Handle, Input));
	TestTrue(TEXT("test returns to grounded pose"), H.Host->SetGrounded(true));

	H.Teardown();
	return true;
}

#endif
