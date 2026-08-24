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
		if (TAbility* Instance = Cast<TAbility>(Ability); Instance && Instance->IsActive())
		{
			return Instance;
		}
	}
	return nullptr;
}

FWeaponInputSnapshot MakeKinsectAimInput()
{
	FWeaponInputSnapshot Input = M3::MakePosedInput(/*bGrounded=*/true,
		/*bSheathed=*/false);
	Input.Aim.Context = EWeaponAimSnapshotContext::Kinsect;
	Input.Aim.Direction = FVector(1.f, 0.f, 0.f);
	return Input;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM4KinsectMontageCommitBoundaries,
	"MHGZ.M4.Kinsect.MontageCommitBoundaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM4KinsectMontageCommitBoundaries::RunTest(const FString& Parameters)
{
	FMHGZM3Harness H;
	if (!H.Setup())
	{
		AddError(TEXT("M4 kinsect montage harness setup failed"));
		H.Teardown();
		return false;
	}

	TestTrue(TEXT("harness enters unsheathed pose"), H.Host->SetSheathed(false));
	const FGameplayAbilitySpecHandle SendHandle =
		H.GiveAbility(UMHGZM4TestSendKinsectAbility::StaticClass());
	const FGameplayAbilitySpecHandle RecallHandle =
		H.GiveAbility(UMHGZM4TestRecallKinsectAbility::StaticClass());
	TestTrue(TEXT("test send granted"), SendHandle.IsValid());
	TestTrue(TEXT("test recall granted"), RecallHandle.IsValid());

	const FGameplayTag Attacking = M3::Tag(TEXT("Combat.State.Attacking"));
	const FGameplayTag BlockMovement = M3::Tag(TEXT("Combat.State.BlockMovement"));
	const FWeaponInputSnapshot Input = MakeKinsectAimInput();

	// A normal montage completion without its precise Commit notify is a cancel.
	TestTrue(TEXT("send starts before its commit"),
		H.TryActivateWithInput(SendHandle, Input));
	UMHGZM4TestSendKinsectAbility* Send =
		GetActiveInstance<UMHGZM4TestSendKinsectAbility>(*H.ASC, SendHandle);
	TestNotNull(TEXT("active pre-commit send exists"), Send);
	TestEqual(TEXT("send keeps kinsect attached before commit"),
		H.Kinsect->GetState(), EKinsectState::Attached);
	TestEqual(TEXT("send acquires no attacking tag"), H.ASC->GetTagCount(Attacking), 0);
	TestEqual(TEXT("send acquires no movement lock"), H.ASC->GetTagCount(BlockMovement), 0);
	TestFalse(TEXT("send never owns montage root motion"),
		H.Host->IsMontageRootMotionOwned());
	if (Send)
	{
		FWeaponActionToken WrongAction = Send->GetActionToken();
		++WrongAction.ActivationSequenceID;
		TestFalse(TEXT("stale send notify cannot commit"),
			Send->CommitSendKinsect(WrongAction));
		Send->FinishNormallyForTest();
	}
	TestEqual(TEXT("missing SendCommit preserves attached state"),
		H.Kinsect->GetState(), EKinsectState::Attached);

	// PendingRequest is created at Confirm and not rebuilt from mutable aim/data at Commit.
	TestTrue(TEXT("send starts for frozen request test"),
		H.TryActivateWithInput(SendHandle, Input));
	Send = GetActiveInstance<UMHGZM4TestSendKinsectAbility>(*H.ASC, SendHandle);
	TestNotNull(TEXT("active frozen-request send exists"), Send);
	const float FrozenRange = H.KinsectData->MaxFlightRange;
	H.KinsectData->MaxFlightRange = FrozenRange * 0.5f;
	if (Send)
	{
		TestTrue(TEXT("exact SendCommit deploys once"), Send->CommitForTest());
		TestTrue(TEXT("repeated exact SendCommit is idempotent"), Send->CommitForTest());
		Send->InterruptForTest();
	}
	TestEqual(TEXT("post-commit interruption keeps flight"),
		H.Kinsect->GetState(), EKinsectState::Flying);
	TestEqual(TEXT("SendCommit uses the confirm-time request"),
		H.Kinsect->ActiveRequest.MaxDistance, FrozenRange);

	// Recall has the same notify boundary: neither missing nor stale commits start return.
	TestTrue(TEXT("recall starts before its commit"),
		H.TryActivateWithInput(RecallHandle, M3::MakePosedInput(true, false)));
	UMHGZM4TestRecallKinsectAbility* Recall =
		GetActiveInstance<UMHGZM4TestRecallKinsectAbility>(*H.ASC, RecallHandle);
	TestNotNull(TEXT("active pre-commit recall exists"), Recall);
	TestEqual(TEXT("recall keeps kinsect flying before commit"),
		H.Kinsect->GetState(), EKinsectState::Flying);
	if (Recall)
	{
		FWeaponActionToken WrongAction = Recall->GetActionToken();
		++WrongAction.ActivationSequenceID;
		TestFalse(TEXT("stale recall notify cannot commit"),
			Recall->CommitRecallKinsect(WrongAction));
		Recall->FinishNormallyForTest();
	}
	TestEqual(TEXT("missing RecallCommit preserves flight"),
		H.Kinsect->GetState(), EKinsectState::Flying);

	TestTrue(TEXT("recall starts for exact commit"),
		H.TryActivateWithInput(RecallHandle, M3::MakePosedInput(true, false)));
	Recall = GetActiveInstance<UMHGZM4TestRecallKinsectAbility>(*H.ASC, RecallHandle);
	TestNotNull(TEXT("active exact-commit recall exists"), Recall);
	if (Recall)
	{
		TestTrue(TEXT("exact RecallCommit starts return"), Recall->CommitForTest());
		TestTrue(TEXT("repeated exact RecallCommit is idempotent"), Recall->CommitForTest());
		Recall->InterruptForTest();
	}
	TestEqual(TEXT("post-commit interruption keeps return"),
		H.Kinsect->GetState(), EKinsectState::Returning);
	TestEqual(TEXT("send/recall leave no attacking tag"), H.ASC->GetTagCount(Attacking), 0);
	TestEqual(TEXT("send/recall leave no movement lock"), H.ASC->GetTagCount(BlockMovement), 0);
	TestFalse(TEXT("send/recall leave no montage root-motion owner"),
		H.Host->IsMontageRootMotionOwned());

	H.Teardown();
	return true;
}

#endif
