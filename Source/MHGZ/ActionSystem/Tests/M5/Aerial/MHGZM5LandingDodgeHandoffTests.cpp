// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "MHGZM5LandingDodgeHandoffTestTypes.h"
#include "../../Shared/MHGZM3TestHarness.h"
#include "ActionSystem/MHGZComboCoordinatorAbility.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Engine/SkeletalMesh.h"
#include "AttributeSystem/MHGZAttributeSet.h"

namespace
{
template <typename T>
T* ActiveAbility(UMHGZAbilitySystemComponent& ASC, FGameplayAbilitySpecHandle Handle)
{
	FGameplayAbilitySpec* Spec = ASC.FindAbilitySpecFromHandle(Handle);
	if (!Spec) { return nullptr; }
	for (UGameplayAbility* Instance : Spec->GetAbilityInstances())
	{
		if (T* Ability = Cast<T>(Instance); Ability && Ability->IsActive()) { return Ability; }
	}
	return nullptr;
}

enum class EHandoffCase { Direct, Buffered, ClosedWindow, FailedPlayback, ExternalInterrupt };

bool CheckLandingHandoff(FAutomationTestBase& Test, UClass* SourceClass,
	EHandoffCase Case, EDirectionalInput Direction)
{
	FMHGZM3Harness H;
	if (!Test.TestTrue(TEXT("ground handoff harness builds"),
		H.Setup(false, AMHGZM3TestCharacter::StaticClass())))
	{
		H.Teardown(); return false;
	}
	USkeletalMesh* Body = LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Characters/Demo/Meshes/Body/SKM_Demo_Body.SKM_Demo_Body"));
	USkeletalMeshComponent* Mesh = H.Character->GetMesh();
	Mesh->SetSkeletalMesh(Body);
	Mesh->SetAnimInstanceClass(UAnimInstance::StaticClass());
	H.ASC->RefreshAbilityActorInfo();
	Test.TestTrue(TEXT("fixture retains its exact RuntimeHost"), H.ASC->GetRuntimeHost() == H.Host);
	UAnimInstance* Anim = Mesh->GetAnimInstance();
	UGA_WeaponComboCoordinator* Coordinator = H.ASC->GetActiveComboCoordinator();
	UClass* DodgeClass = LoadClass<UMHGZDodgeAbility>(nullptr,
		TEXT("/Game/Blueprints/Ability/Core/GA_Dodge.GA_Dodge_C"));
	if (!Test.TestNotNull(TEXT("real AnimInstance"), Anim)
		|| !Test.TestNotNull(TEXT("coordinator"), Coordinator)
		|| !Test.TestNotNull(TEXT("resident GA_Dodge class"), DodgeClass))
	{
		H.Teardown(); return false;
	}
	const ERootMotionMode::Type Baseline = ERootMotionMode::RootMotionFromEverything;
	Anim->SetRootMotionMode(Baseline);
	H.Host->SetGrounded(true);
	H.Host->SetSheathed(false);
	H.Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	const FGameplayAbilitySpecHandle SourceHandle = H.GiveAbility(SourceClass);
	const FGameplayAbilitySpecHandle DodgeHandle = H.GiveAbility(
		Case == EHandoffCase::FailedPlayback
			? UMHGZM5FailedLandingDodgeTestAbility::StaticClass() : DodgeClass);
	UMHGZWeaponComboData* Combo = NewObject<UMHGZWeaponComboData>(Coordinator);
	FComboTransition& Transition = Combo->Transitions.AddDefaulted_GetRef();
	Transition.TransitionID = TEXT("M5LandingHandoffSource");
	Transition.SourceState = TEXT("Idle");
	Transition.TargetState = TEXT("M5LandingSource");
	Transition.InputTag = M3::Tag(TEXT("Input.Weapon.Y"));
	Transition.AbilityClass = SourceClass;
	Coordinator->InjectComboData(Combo);
	FWeaponInputSnapshot Input = M3::MakePosedInput(true, false);
	Input.ResolvedInputTag = Transition.InputTag;
	Input.SourceControlTag = Input.ResolvedInputTag;
	Input.SequenceID = 1;
	Input.Phase = EWeaponInputPhase::Started;
	Coordinator->HandleWeaponInput(Input);
	UMHGZAttackAbility* Source = ActiveAbility<UMHGZAttackAbility>(*H.ASC, SourceHandle);
	if (!Test.TestNotNull(TEXT("source remains active in its production Over task"), Source))
	{
		H.Teardown(); return false;
	}
	const FWeaponActionToken OldToken = Source->GetActionToken();
	UAnimMontage* Over = Anim->GetCurrentActiveMontage();
	FAnimMontageInstance* OverInstance = Over ? Anim->GetActiveInstanceForMontage(Over) : nullptr;
	if (!Test.TestNotNull(TEXT("resident Over montage instance"), OverInstance)
		|| !Test.TestTrue(TEXT("source is exact root-motion owner"),
			H.Host->IsMontageRootMotionOwnedBy(OldToken)))
	{
		H.Teardown(); return false;
	}
	const int32 OldMontageID = OverInstance->GetInstanceID();
	const float StaminaBefore = H.ASC->GetNumericAttribute(UMHGZAttributeSet::GetStaminaAttribute());
	Input.ResolvedInputTag = M3::Tag(TEXT("Input.Dodge"));
	Input.SourceControlTag = Input.ResolvedInputTag;
	Input.SequenceID = 2;
	Input.Direction = Direction;
	const FName Window(TEXT("M5LandingExactDodgeAccept"));

	if (Case == EHandoffCase::ExternalInterrupt)
	{
		Test.AddExpectedError(SourceClass->IsChildOf(UMHGZKinsectPiercerAbility::StaticClass())
			? TEXT("The Landing203 montage was interrupted")
			: TEXT("The resident Over landing montage was interrupted"),
			EAutomationExpectedErrorFlags::Contains, 1);
		Anim->Montage_Stop(0.05f, Over);
		Test.TestFalse(TEXT("unprepared interruption still ends source"), Source->IsActive());
		Test.TestEqual(TEXT("unprepared interruption is Interrupted"),
			Source->GetActionEndReason(), EWeaponActionEndReason::Interrupted);
	}
	else if (Case == EHandoffCase::ClosedWindow)
	{
		Test.TestFalse(TEXT("closed exact window rejects direct A"), H.ASC->TryActivateDirectInput(Input));
		Test.TestTrue(TEXT("rejected A does not cancel source"), Source->IsActive());
		Test.TestTrue(TEXT("rejected A leaves Over playing"), Anim->Montage_IsPlaying(Over));
		Test.TestEqual(TEXT("rejected A has no cost"),
			H.ASC->GetNumericAttribute(UMHGZAttributeSet::GetStaminaAttribute()), StaminaBefore);
	}
	else
	{
		if (Case == EHandoffCase::Buffered)
		{
			H.ASC->HandleResolvedInputSnapshot(Input);
			Test.TestTrue(TEXT("early A retains source until exact window"), Source->IsActive());
			Test.TestNull(TEXT("early A does not start Dodge"),
				ActiveAbility<UMHGZDodgeAbility>(*H.ASC, DodgeHandle));
		}
		Test.TestTrue(TEXT("source opens its exact window"), Source->BeginDodgeAcceptWindow(Window));
		if (Case != EHandoffCase::Buffered) { H.ASC->TryActivateDirectInput(Input); }
		if (Case == EHandoffCase::FailedPlayback)
		{
			Test.TestFalse(TEXT("failed real replacement cannot leave Dodge active"),
				ActiveAbility<UMHGZDodgeAbility>(*H.ASC, DodgeHandle) != nullptr);
			Test.TestFalse(TEXT("rollback ends an already interrupted source"), Source->IsActive());
			Test.TestEqual(TEXT("rollback is Interrupted, not a false Superseded"),
				Source->GetActionEndReason(), EWeaponActionEndReason::Interrupted);
		}
		else
		{
			UMHGZDodgeAbility* Dodge = ActiveAbility<UMHGZDodgeAbility>(*H.ASC, DodgeHandle);
			Test.TestNotNull(TEXT("real Dodge survives synchronous old-Montage interruption"), Dodge);
			Test.TestFalse(TEXT("handoff ends the old source"), Source->IsActive());
			Test.TestEqual(TEXT("handoff classifies source as Superseded"),
				Source->GetActionEndReason(), EWeaponActionEndReason::Superseded);
			Test.TestTrue(TEXT("new Dodge montage is playing"),
				Anim->GetCurrentActiveMontage() && Anim->GetCurrentActiveMontage() != Over
					&& Anim->Montage_IsPlaying(Anim->GetCurrentActiveMontage()));
			Test.TestEqual(TEXT("handoff consumes exactly one Dodge cost"),
				H.ASC->GetNumericAttribute(UMHGZAttributeSet::GetStaminaAttribute()), StaminaBefore - 25.0f);
			Test.TestEqual(TEXT("direct handoff clears source FSM after commit"),
				Coordinator->GetCurrentState(), FName(TEXT("Idle")));
			if (Dodge)
			{
				H.ASC->CancelAbilityHandle(DodgeHandle);
				Test.TestFalse(TEXT("cancelling successor releases its registration"), H.Host->HasRegisteredAction());
			}
		}
	}
	if (Case != EHandoffCase::ClosedWindow)
	{
		FWeaponActionToken Resolved;
		Test.TestFalse(TEXT("ended source montage has no stale routing registration"),
			H.Host->ResolveMontage(Mesh, OldMontageID, Resolved));
		Test.TestFalse(TEXT("source root owner is released"), H.Host->IsMontageRootMotionOwnedBy(OldToken));
		Test.TestEqual(TEXT("source scoped root mode is restored"), Anim->RootMotionMode.GetValue(), Baseline);
		Test.TestFalse(TEXT("no stale attack/window/dodge tags"),
			H.ASC->HasMatchingGameplayTag(M3::Tag(TEXT("Combat.State.Attacking")))
			|| H.ASC->HasMatchingGameplayTag(M3::Tag(TEXT("Combat.State.DodgeAcceptOpen")))
			|| H.ASC->HasMatchingGameplayTag(M3::Tag(TEXT("Combat.State.Dodging"))));
	}
	H.Teardown();
	return true;
}

bool CheckCases(FAutomationTestBase& Test, UClass* SourceClass)
{
	for (EDirectionalInput Direction : { EDirectionalInput::Forward, EDirectionalInput::Left,
		EDirectionalInput::Right, EDirectionalInput::Back })
	{
		CheckLandingHandoff(Test, SourceClass, EHandoffCase::Direct, Direction);
	}
	for (EHandoffCase Case : { EHandoffCase::Buffered, EHandoffCase::ClosedWindow,
		EHandoffCase::FailedPlayback, EHandoffCase::ExternalInterrupt })
	{
		CheckLandingHandoff(Test, SourceClass, Case, EDirectionalInput::Forward);
	}
	return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZM5PiercerLandingDodgeHandoff,
	"MHGZ.M5.Aerial.LandingDodgeHandoff.KinsectPiercer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZM5PiercerLandingDodgeHandoff::RunTest(const FString& Parameters)
{
	return CheckCases(*this, UMHGZM5PiercerLandingHandoffTestAbility::StaticClass());
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZM5ThrustLandingDodgeHandoff,
	"MHGZ.M5.Aerial.LandingDodgeHandoff.DescendingThrust",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZM5ThrustLandingDodgeHandoff::RunTest(const FString& Parameters)
{
	return CheckCases(*this, UMHGZM5ThrustLandingHandoffTestAbility::StaticClass());
}

#endif
