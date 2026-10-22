// Copyright MHGZ Project. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "../Shared/MHGZM3TestHarness.h"
#include "../Shared/MHGZM4TestTypes.h"
#include "ActionSystem/MHGZM1PlaceholderAbilities.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Engine/SkeletalMesh.h"

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
FWeaponInputSnapshot ForwardInput(bool bSheathed)
{
	auto Input = M3::MakePosedInput(true, bSheathed);
	Input.ResolvedInputTag = M3::Tag(TEXT("Input.Dodge"));
	Input.SourceControlTag = Input.ResolvedInputTag;
	Input.Direction = EDirectionalInput::Forward;
	return Input;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZForwardDodgeRecoveryBothExits,
	"MHGZ.Maintenance.DodgeRecovery.ForwardBothExitsCommitBeforeYieldWithoutMovementTakeover",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZForwardDodgeRecoveryBothExits::RunTest(const FString&)
{
	for (bool bSheathed : {false, true})
	for (bool bMoveExit : {false, true})
	{
		FMHGZM3Harness H;
		if (!H.Setup()) { H.Teardown(); return false; }
		H.Host->SetSheathed(bSheathed);
		const auto DodgeHandle = H.GiveAbility(UMHGZM4TestDodgeAbility::StaticClass());
		const auto FailHandle = H.GiveAbility(UMHGZM1CommitFailureAbility::StaticClass());
		const auto ActionHandle = H.GiveAbility(UMHGZM1PlaceholderActionA::StaticClass());
		const auto Input = ForwardInput(bSheathed);
		TestTrue(TEXT("forward starts"), H.TryActivateWithInput(DodgeHandle, Input));
		auto* Dodge = Active<UMHGZM4TestDodgeAbility>(H, DodgeHandle);
		if (!TestNotNull(TEXT("forward source"), Dodge)) { H.Teardown(); return false; }
		const auto Token = Dodge->GetActionToken();
		TestFalse(TEXT("Core has no action permission"), Dodge->IsActionRecoveryOpen());
		TestFalse(TEXT("Core rejects a valid attack action"), H.TryActivateWithInput(ActionHandle, Input));
		Dodge->SetLiveMovementInputForTest(bMoveExit);
		Dodge->EnterDodgeSectionForTest(TEXT("IdleExit"));
		if (bMoveExit)
		{
			TestFalse(TEXT("redirect is not actual section entry"), Dodge->IsActionRecoveryOpen());
			Dodge->EnterDodgeSectionForTest(TEXT("MoveExit"));
		}
		TestTrue(TEXT("both exits open action permission"), Dodge->IsActionRecoveryOpen());
		TestFalse(TEXT("forward never opens directional movement permission"), Dodge->IsDirectionalRecoveryOpen());
		TestTrue(TEXT("finds forward for action handoff"), UMHGZDodgeAbility::FindOpenActionRecovery(H.ASC) == Dodge);
		TestNull(TEXT("no directional MM start for forward"), UMHGZDodgeAbility::FindOpenDirectionalRecovery(H.ASC));
		TestTrue(TEXT("without a successor original exit keeps playing"), Dodge->IsActive());
		TestTrue(TEXT("original exit retains root motion"), H.Host->IsMontageRootMotionOwnedBy(Token));
		TestFalse(TEXT("exit action lock released"), H.ASC->HasMatchingGameplayTag(M3::Tag(TEXT("Combat.State.Dodging"))));
		TestEqual(TEXT("only original MoveExit releases movement lock"),
			H.ASC->HasMatchingGameplayTag(M3::Tag(TEXT("Combat.State.BlockMovement"))), !bMoveExit);
		Dodge->SetLiveMovementInputForTest(true);
		TestFalse(TEXT("held or late movement never truncates forward recovery"), Dodge->TryCompleteDirectionalRecoveryForMovement());
		H.TryActivateWithInput(FailHandle, Input);
		TestTrue(TEXT("failed Commit preserves source recovery"), Dodge->IsActionRecoveryOpen());
		TestTrue(TEXT("failed Commit preserves source root owner"), H.Host->IsMontageRootMotionOwnedBy(Token));
		TestTrue(TEXT("committed successor starts"), H.TryActivateWithInput(ActionHandle, Input));
		auto* Action = Active<UMHGZM1PlaceholderActionA>(H, ActionHandle);
		if (TestNotNull(TEXT("successor active"), Action))
		{
			TestFalse(TEXT("old forward action retired"), Dodge->IsActive());
			TestEqual(TEXT("retired only as superseded"), Dodge->GetActionEndReason(), EWeaponActionEndReason::Superseded);
			TestTrue(TEXT("successor uses configured recovery blend"),
				FMath::IsNearlyEqual(Action->GetWeaponActivationContext().MontageBlendInTime, Dodge->DirectionalIdleExitBlendTime));
			TestFalse(TEXT("old root-motion owner cleared"), H.Host->IsMontageRootMotionOwned());
			TestTrue(TEXT("new action acquires root-motion ownership"), H.Host->AcquireMontageRootMotion(Action->GetActionToken()));
			Dodge->CompleteDirectionalRecoveryForAction();
			TestTrue(TEXT("stale callback cannot clear successor owner"), H.Host->IsMontageRootMotionOwnedBy(Action->GetActionToken()));
		}
		H.Teardown();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZForwardDodgeResidentRecoveryFade,
	"MHGZ.Maintenance.DodgeRecovery.ForwardResidentBothExitsFadeDisablesOutgoingRoot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZForwardDodgeResidentRecoveryFade::RunTest(const FString&)
{
	for (bool bSheathed : {false, true})
	for (bool bMoveExit : {false, true})
	{
		FMHGZM3Harness H;
		if (!H.Setup(false, AMHGZM3TestCharacter::StaticClass())) { H.Teardown(); return false; }
		auto* Mesh = H.Character->GetMesh();
		Mesh->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr,
			TEXT("/Game/Characters/Demo/Meshes/Body/SKM_Demo_Body.SKM_Demo_Body")));
		Mesh->SetAnimInstanceClass(UAnimInstance::StaticClass());
		H.ASC->RefreshAbilityActorInfo();
		H.Host->SetSheathed(bSheathed);
		UClass* Class = LoadClass<UMHGZDodgeAbility>(nullptr,
			TEXT("/Game/Blueprints/Ability/Core/GA_Dodge.GA_Dodge_C"));
		if (!TestNotNull(TEXT("resident GA_Dodge"), Class)) { H.Teardown(); return false; }
		const auto DodgeHandle = H.GiveAbility(Class);
		const auto ActionHandle = H.GiveAbility(UMHGZM1PlaceholderActionA::StaticClass());
		const auto Input = ForwardInput(bSheathed);
		TestTrue(TEXT("resident forward montage starts"), H.TryActivateWithInput(DodgeHandle, Input));
		auto* Dodge = Active<UMHGZDodgeAbility>(H, DodgeHandle);
		auto* Anim = Mesh->GetAnimInstance();
		auto* Montage = Anim ? Anim->GetCurrentActiveMontage() : nullptr;
		auto* Playing = Anim && Montage ? Anim->GetActiveInstanceForMontage(Montage) : nullptr;
		if (TestNotNull(TEXT("live production forward"), Dodge) && TestNotNull(TEXT("live montage instance"), Playing))
		{
			TestTrue(TEXT("source authored phase acquires owner"), Dodge->BeginActionRootMotionPhase(Dodge->GetActionToken(), true, false));
			const FName Exit = bMoveExit ? TEXT("MoveExit") : TEXT("IdleExit");
			Anim->Montage_JumpToSection(Exit, Montage);
			Playing->OnMontageSectionChanged.ExecuteIfBound(Montage, Exit, false);
			TestTrue(TEXT("real callback opens action recovery"), Dodge->IsActionRecoveryOpen());
			TestFalse(TEXT("no attack retains normal root extraction"), Playing->IsRootMotionDisabled());
			TestTrue(TEXT("successor commits through production base"), H.TryActivateWithInput(ActionHandle, Input));
			TestFalse(TEXT("source ended"), Dodge->IsActive());
			TestTrue(TEXT("fading source cannot move capsule"), Playing->IsRootMotionDisabled());
			TestTrue(TEXT("task teardown does not shorten explicit fade to zero"),
				FMath::IsNearlyEqual(Playing->GetBlendTime(), FMath::Max(0.f, Dodge->DirectionalIdleExitBlendTime)));
			TestFalse(TEXT("source root-motion phase released"), H.Host->IsMontageRootMotionOwned());
			FWeaponMotionMatchingHandoff Handoff;
			TestFalse(TEXT("attack handoff does not publish locomotion handoff"), H.Host->GetPendingMotionMatchingHandoff(Handoff));
		}
		H.Teardown();
	}
	return true;
}
#endif
