// Copyright MHGZ Project. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "ActionSystem/MHGZM1PlaceholderAbilities.h"
#include "../Shared/MHGZM3TestHarness.h"
#include "../Shared/MHGZM4TestTypes.h"
#include "../Shared/MHGZDodgeTestHarness.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Engine/SkeletalMesh.h"

namespace
{
template<typename T> T* Instance(UMHGZAbilitySystemComponent* ASC, FGameplayAbilitySpecHandle Handle)
{
	FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(Handle);
	if (!Spec) { return nullptr; }
	for (UGameplayAbility* Ability : Spec->GetAbilityInstances())
	{
		if (T* Typed = Cast<T>(Ability); Typed && Typed->IsActive()) { return Typed; }
	}
	return nullptr;
}
FWeaponInputSnapshot Input(EDirectionalInput Direction)
{
	auto Result = M3::MakePosedInput(true, false);
	Result.ResolvedInputTag = M3::Tag(TEXT("Input.Dodge"));
	Result.SourceControlTag = Result.ResolvedInputTag;
	Result.Direction = Direction;
	return Result;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZDirectionalDodgeRecoveryPermissions,
	"MHGZ.Maintenance.DodgeRecovery.DirectionalBoundaryAndLateMovement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZDirectionalDodgeRecoveryPermissions::RunTest(const FString&)
{
	for (EDirectionalInput Direction : {EDirectionalInput::Left, EDirectionalInput::Right, EDirectionalInput::Back})
	{
		FMHGZM3Harness H;
		if (!H.Setup()) { H.Teardown(); return false; }
		H.Host->SetSheathed(false);
		auto Handle = H.GiveAbility(UMHGZM4TestDodgeAbility::StaticClass());
		TestNotNull(TEXT("source attack owns an open cancel window"), M4Dodge::StartCancelableAttack(H));
		TestTrue(TEXT("directional dodge starts"), H.TryActivateWithInput(Handle, Input(Direction)));
		auto* Dodge = Instance<UMHGZM4TestDodgeAbility>(H.ASC, Handle);
		if (TestNotNull(TEXT("dodge instance"), Dodge))
		{
			const FWeaponActionToken Token = Dodge->GetActionToken();
			TestFalse(TEXT("Core cannot yield"), Dodge->IsDirectionalRecoveryOpen());
			TestTrue(TEXT("Core keeps input lock"), H.ASC->HasMatchingGameplayTag(M3::Tag(TEXT("Combat.State.Dodging"))));
			Dodge->EnterDodgeSectionForTest(TEXT("IdleExit"));
			TestTrue(TEXT("exact IdleExit boundary opens recovery"), Dodge->IsDirectionalRecoveryOpen());
			TestFalse(TEXT("attacks become available"), H.ASC->HasMatchingGameplayTag(M3::Tag(TEXT("Combat.State.Dodging"))));
			TestTrue(TEXT("neutral input retains original exit playback"), Dodge->IsActive());
			TestTrue(TEXT("neutral input retains root ownership"), H.Host->IsMontageRootMotionOwnedBy(Token));
			TestTrue(TEXT("neutral input retains movement lock"), H.ASC->HasMatchingGameplayTag(M3::Tag(TEXT("Combat.State.BlockMovement"))));
			TestFalse(TEXT("neutral input does not cancel"), Dodge->TryCompleteDirectionalRecoveryForMovement());
			TestTrue(TEXT("finds exact live recovery"), UMHGZDodgeAbility::FindOpenDirectionalRecovery(H.ASC) == Dodge);
			Dodge->SetLiveMovementInputForTest(true);
			TestTrue(TEXT("movement pressed later can take over"), Dodge->TryCompleteDirectionalRecoveryForMovement());
			TestFalse(TEXT("recovery ends"), Dodge->IsActive());
			TestFalse(TEXT("root ownership released"), H.Host->IsMontageRootMotionOwned());
			TestFalse(TEXT("movement lock released"), H.ASC->HasMatchingGameplayTag(M3::Tag(TEXT("Combat.State.BlockMovement"))));
			TestFalse(TEXT("stale completion is inert"), Dodge->TryCompleteDirectionalRecoveryForMovement());
		}
		H.Teardown();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZDirectionalDodgeRecoveryCommittedSuccessor,
	"MHGZ.Maintenance.DodgeRecovery.RejectedActionPreservesExitCommittedActionYields",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZDirectionalDodgeRecoveryCommittedSuccessor::RunTest(const FString&)
{
	FMHGZM3Harness H;
	if (!H.Setup()) { H.Teardown(); return false; }
	H.Host->SetSheathed(false);
	auto DodgeHandle = H.GiveAbility(UMHGZM4TestDodgeAbility::StaticClass());
	auto FailingHandle = H.GiveAbility(UMHGZM1CommitFailureAbility::StaticClass());
	auto SuccessHandle = H.GiveAbility(UMHGZM1PlaceholderActionA::StaticClass());
	TestNotNull(TEXT("source attack owns an open cancel window"), M4Dodge::StartCancelableAttack(H));
	H.TryActivateWithInput(DodgeHandle, Input(EDirectionalInput::Back));
	auto* Dodge = Instance<UMHGZM4TestDodgeAbility>(H.ASC, DodgeHandle);
	if (TestNotNull(TEXT("dodge exists"), Dodge))
	{
		Dodge->EnterDodgeSectionForTest(TEXT("IdleExit"));
		H.TryActivateWithInput(FailingHandle, Input(EDirectionalInput::None));
		TestTrue(TEXT("rejected action leaves old recovery active"), Dodge->IsDirectionalRecoveryOpen());
		TestTrue(TEXT("rejected action keeps its root owner"), H.Host->IsMontageRootMotionOwnedBy(Dodge->GetActionToken()));
		TestTrue(TEXT("valid successor activates"), H.TryActivateWithInput(SuccessHandle, Input(EDirectionalInput::None)));
		auto* Action = Instance<UMHGZM1PlaceholderActionA>(H.ASC, SuccessHandle);
		if (TestNotNull(TEXT("successor is live"), Action))
		{
			TestTrue(TEXT("successor committed"), Action->IsActionActivationCommitted());
			TestTrue(TEXT("blend override is per activation 0.1"), FMath::IsNearlyEqual(Action->GetWeaponActivationContext().MontageBlendInTime, .1f));
			TestEqual(TEXT("old source retired as superseded"), Dodge->GetActionEndReason(), EWeaponActionEndReason::Superseded);
			TestFalse(TEXT("old recovery no longer active"), Dodge->IsActive());
			TestFalse(TEXT("old root owner retired before successor movement"), H.Host->IsMontageRootMotionOwned());
			TestTrue(TEXT("successor can acquire root ownership"), H.Host->AcquireMontageRootMotion(Action->GetActionToken()));
			Dodge->CompleteDirectionalRecoveryForAction();
			TestTrue(TEXT("stale source cannot clear new owner"), H.Host->IsMontageRootMotionOwnedBy(Action->GetActionToken()));
		}
	}
	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZDirectionalDodgeRecoveryForwardIsolation,
	"MHGZ.Maintenance.DodgeRecovery.ForwardUnchangedAndHeldMovementAtBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZDirectionalDodgeRecoveryForwardIsolation::RunTest(const FString&)
{
	FMHGZM3Harness H;
	if (!H.Setup()) { H.Teardown(); return false; }
	H.Host->SetSheathed(false);
	auto Handle = H.GiveAbility(UMHGZM4TestDodgeAbility::StaticClass());
	H.TryActivateWithInput(Handle, Input(EDirectionalInput::Forward));
	auto* Dodge = Instance<UMHGZM4TestDodgeAbility>(H.ASC, Handle);
	if (TestNotNull(TEXT("forward exists"), Dodge))
	{
		Dodge->EnterDodgeSectionForTest(TEXT("IdleExit"));
		TestFalse(TEXT("forward never opens directional movement recovery"), Dodge->IsDirectionalRecoveryOpen());
		TestTrue(TEXT("forward IdleExit opens action recovery"), Dodge->IsActionRecoveryOpen());
		TestFalse(TEXT("forward exit permits a successor"), H.ASC->HasMatchingGameplayTag(M3::Tag(TEXT("Combat.State.Dodging"))));
		TestNull(TEXT("forward not found by movement helper"), UMHGZDodgeAbility::FindOpenDirectionalRecovery(H.ASC));
		TestTrue(TEXT("forward found by action helper"), UMHGZDodgeAbility::FindOpenActionRecovery(H.ASC) == Dodge);
		Dodge->FinishNormallyForTest();
	}
	TestNotNull(TEXT("source attack owns an open cancel window"), M4Dodge::StartCancelableAttack(H));
	H.TryActivateWithInput(Handle, Input(EDirectionalInput::Left));
	Dodge = Instance<UMHGZM4TestDodgeAbility>(H.ASC, Handle);
	if (TestNotNull(TEXT("left exists"), Dodge))
	{
		Dodge->SetLiveMovementInputForTest(true);
		Dodge->EnterDodgeSectionForTest(TEXT("IdleExit"));
		TestFalse(TEXT("held stick exits at the boundary, no extra wait"), Dodge->IsActive());
		TestFalse(TEXT("held-stick exit clears movement lock"), H.ASC->HasMatchingGameplayTag(M3::Tag(TEXT("Combat.State.BlockMovement"))));
	}
	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZDirectionalDodgeRecoveryResidentMontage,
	"MHGZ.Maintenance.DodgeRecovery.ResidentMontageFadeDisablesOutgoingRoot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZDirectionalDodgeRecoveryResidentMontage::RunTest(const FString&)
{
	for (EDirectionalInput Direction : {EDirectionalInput::Left, EDirectionalInput::Right, EDirectionalInput::Back})
	{
		FMHGZM3Harness H;
		if (!H.Setup(false, AMHGZM3TestCharacter::StaticClass())) { H.Teardown(); return false; }
		auto* Mesh = H.Character->GetMesh();
		Mesh->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr,
			TEXT("/Game/Characters/Demo/Meshes/Body/SKM_Demo_Body.SKM_Demo_Body")));
		Mesh->SetAnimInstanceClass(UAnimInstance::StaticClass());
		H.ASC->RefreshAbilityActorInfo();
		H.Host->SetSheathed(false);
		UClass* DodgeClass = LoadClass<UMHGZDodgeAbility>(nullptr,
			TEXT("/Game/Blueprints/Ability/Core/GA_Dodge.GA_Dodge_C"));
		if (!TestNotNull(TEXT("resident GA_Dodge class"), DodgeClass)) { H.Teardown(); return false; }
		auto Handle = H.GiveAbility(DodgeClass);
		TestNotNull(TEXT("source attack owns an open cancel window"), M4Dodge::StartCancelableAttack(H));
		TestTrue(TEXT("resident directional montage starts"), H.TryActivateWithInput(Handle, Input(Direction)));
		auto* Dodge = Instance<UMHGZDodgeAbility>(H.ASC, Handle);
		auto* Anim = Mesh->GetAnimInstance();
		UAnimMontage* Montage = Anim ? Anim->GetCurrentActiveMontage() : nullptr;
		auto* Playing = Anim && Montage ? Anim->GetActiveInstanceForMontage(Montage) : nullptr;
		if (TestNotNull(TEXT("live production dodge"), Dodge) && TestNotNull(TEXT("real montage instance"), Playing))
		{
			TestTrue(TEXT("authored phase acquires exact owner"),
				Dodge->BeginActionRootMotionPhase(Dodge->GetActionToken(), true, false));
			Anim->Montage_JumpToSection(TEXT("IdleExit"), Montage);
			// Exercise the actual bound SectionChanged callback without ticking an entire world.
			Playing->OnMontageSectionChanged.ExecuteIfBound(Montage, TEXT("IdleExit"), false);
			TestTrue(TEXT("real section boundary opens recovery"), Dodge->IsDirectionalRecoveryOpen());
			TestFalse(TEXT("neutral exit still extracts root motion"), Playing->IsRootMotionDisabled());
			TestTrue(TEXT("neutral exit still owns movement"), H.Host->IsMontageRootMotionOwned());
			Dodge->CompleteDirectionalRecoveryForAction();
			TestFalse(TEXT("outgoing action ends"), Dodge->IsActive());
			TestTrue(TEXT("fading outgoing pose cannot move capsule"), Playing->IsRootMotionDisabled());
			const float ConfiguredFade = FMath::Max(0.f, Dodge->DirectionalIdleExitBlendTime);
			const float AssetFade = Montage->BlendOut.GetBlendTime();
			AddInfo(FString::Printf(TEXT("Directional recovery %s: GA=%.6f, Montage=%.6f, actual=%.6f"),
				*Montage->GetName(), ConfiguredFade, AssetFade, Playing->GetBlendTime()));
			// The normal GAS montage-task teardown may shorten an existing fade to
			// the authored BlendOut. Respect current user tuning, not a frozen 0.1.
			TestTrue(TEXT("real fade honors current GA/Montage configuration"),
				FMath::IsNearlyEqual(Playing->GetBlendTime(), FMath::Min(ConfiguredFade, AssetFade)));
			TestFalse(TEXT("phase owner released"), H.Host->IsMontageRootMotionOwned());
		}
		H.Teardown();
	}
	return true;
}
#endif
