// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "ActionSystem/MHGZComboCoordinatorAbility.h"
#include "ActionSystem/MHGZM1PlaceholderAbilities.h"
#include "ActionSystem/MHGZWeaponComboData.h"
#include "Animation/AnimMontage.h"
#include "Components/CapsuleComponent.h"
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
	for (UGameplayAbility* Instance : Spec->GetAbilityInstances())
	{
		if (TAbility* Typed = Cast<TAbility>(Instance))
		{
			return Typed;
		}
	}
	return nullptr;
}

UGA_WeaponComboCoordinator* ConfigureTestAttack(
	FMHGZM3Harness& H, FGameplayAbilitySpecHandle& OutAttackHandle)
{
	UGA_WeaponComboCoordinator* Coordinator = H.ASC
		? H.ASC->GetActiveComboCoordinator()
		: nullptr;
	if (!Coordinator)
	{
		return nullptr;
	}
	OutAttackHandle = H.GiveAbility(UMHGZM4TestAttackAbility::StaticClass());
	UMHGZWeaponComboData* ComboData = NewObject<UMHGZWeaponComboData>(Coordinator);
	FComboTransition& Transition = ComboData->Transitions.AddDefaulted_GetRef();
	Transition.TransitionID = FName(TEXT("M4IdleToAttack"));
	Transition.SourceState = FName(TEXT("Idle"));
	Transition.TargetState = FName(TEXT("M4Attack"));
	Transition.InputTag = M3::Tag(TEXT("Input.Weapon.Y"));
	Transition.AbilityClass = UMHGZM4TestAttackAbility::StaticClass();
	Coordinator->InjectComboData(ComboData);
	return Coordinator;
}

void StartTestAttack(UGA_WeaponComboCoordinator& Coordinator, uint32 SequenceID)
{
	FWeaponInputSnapshot Input = M3::MakePosedInput(true, true);
	Input.ResolvedInputTag = M3::Tag(TEXT("Input.Weapon.Y"));
	Input.SourceControlTag = Input.ResolvedInputTag;
	Input.SequenceID = SequenceID;
	Input.Phase = EWeaponInputPhase::Started;
	Coordinator.HandleWeaponInput(Input);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM4DodgeMovementPhases,
	"MHGZ.M4.Dodge.LockedSteeringAndMotionMatchingPhases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM4DodgeMovementPhases::RunTest(const FString& Parameters)
{
	FMHGZM3Harness H;
	if (!H.Setup())
	{
		AddError(TEXT("M4 dodge harness setup failed"));
		H.Teardown();
		return false;
	}

	const FGameplayAbilitySpecHandle DodgeHandle =
		H.GiveAbility(UMHGZM4TestDodgeAbility::StaticClass());
	const FGameplayAbilitySpecHandle ActionHandle =
		H.GiveAbility(UMHGZM1PlaceholderActionA::StaticClass());
	FWeaponInputSnapshot Input = M3::MakePosedInput(true, true);
	Input.ResolvedInputTag = M3::Tag(TEXT("Input.Dodge"));
	Input.Direction = EDirectionalInput::Forward;
	const FGameplayTag Dodging = M3::Tag(TEXT("Combat.State.Dodging"));
	const FGameplayTag BlockMovement = M3::Tag(TEXT("Combat.State.BlockMovement"));
	const FGameplayTag Invincible = M3::Tag(TEXT("Combat.State.Invincible"));

	TestTrue(TEXT("idle dodge activates"), H.TryActivateWithInput(DodgeHandle, Input));
	UMHGZM4TestDodgeAbility* Dodge = GetActiveInstance<UMHGZM4TestDodgeAbility>(
		*H.ASC, DodgeHandle);
	TestNotNull(TEXT("dodge instance exists"), Dodge);
	TestEqual(TEXT("dodge owns the action lock for its whole action"),
		H.ASC->GetTagCount(Dodging), 1);
	TestEqual(TEXT("DodgeCore owns one movement lock"),
		H.ASC->GetTagCount(BlockMovement), 1);
	TestEqual(TEXT("dodge configures its Core-to-Idle fallback once"),
		Dodge ? Dodge->GetFallbackExitConfigurationCountForTest() : 0, 1);
	TestTrue(TEXT("DodgeCore owns montage root motion"),
		H.Host->IsMontageRootMotionOwned());
	TestFalse(TEXT("active Dodging blocks an ordinary action"),
		H.TryActivateWithInput(ActionHandle, Input));
	TestFalse(TEXT("active Dodging blocks a second dodge"),
		H.TryActivateWithInput(DodgeHandle, Input));

	if (Dodge)
	{
		FWeaponActionToken WrongToken = Dodge->GetActionToken();
		++WrongToken.ActivationSequenceID;
		TestFalse(TEXT("wrong token cannot choose a dodge exit"),
			Dodge->DecideDodgeExit(WrongToken));
		Dodge->EnterDodgeSectionForTest(FName(TEXT("IdleExit")));
		TestEqual(TEXT("no live input commits IdleExit at the Core boundary"),
			Dodge->GetChosenExitSection(), FName(TEXT("IdleExit")));
		TestFalse(TEXT("IdleExit never opens steering"),
			Dodge->EnterMoveExit(Dodge->GetActionToken()));
		Dodge->FinishNormallyForTest();
	}
	TestEqual(TEXT("normal idle exit releases Dodging"),
		H.ASC->GetTagCount(Dodging), 0);
	TestEqual(TEXT("normal idle exit releases movement lock"),
		H.ASC->GetTagCount(BlockMovement), 0);
	TestFalse(TEXT("normal idle exit releases root ownership"),
		H.Host->IsMontageRootMotionOwned());

	TestTrue(TEXT("second dodge activates"),
		H.TryActivateWithInput(DodgeHandle, Input));
	Dodge = GetActiveInstance<UMHGZM4TestDodgeAbility>(*H.ASC, DodgeHandle);
	TestNotNull(TEXT("second dodge instance exists"), Dodge);
	UCapsuleComponent* Capsule = H.Character->GetCapsuleComponent();
	const ECollisionResponse WeaponBefore =
		Capsule->GetCollisionResponseToChannel(ECC_GameTraceChannel1);
	const ECollisionResponse MonsterAttackBefore =
		Capsule->GetCollisionResponseToChannel(ECC_GameTraceChannel2);
	if (Dodge)
	{
		Dodge->SetLiveMovementInputForTest(true);
		Dodge->EnterDodgeSectionForTest(FName(TEXT("IdleExit")));
		TestEqual(TEXT("live input at the Core boundary selects MoveExit"),
			Dodge->GetLastJumpedSectionForTest(), FName(TEXT("MoveExit")));
		Dodge->EnterDodgeSectionForTest(FName(TEXT("MoveExit")));
		TestEqual(TEXT("MoveExit entry releases only the movement lock"),
			H.ASC->GetTagCount(BlockMovement), 0);
		TestEqual(TEXT("steering phase has no movement lock"),
			H.ASC->GetTagCount(BlockMovement), 0);
		TestEqual(TEXT("steering phase retains Dodging"),
			H.ASC->GetTagCount(Dodging), 1);
		TestTrue(TEXT("steering phase retains montage root motion"),
			H.Host->IsMontageRootMotionOwned());
		TestEqual(TEXT("phase is SteeringRootMotion"), Dodge->GetMovementPhase(),
			EMHGZDodgeMovementPhase::SteeringRootMotion);

		TestTrue(TEXT("exact dodge window opens"),
			Dodge->BeginDodgeWindow(FName(TEXT("M4DodgeWindow"))));
		TestEqual(TEXT("dodge window owns Invincible"),
			H.ASC->GetTagCount(Invincible), 1);
		TestEqual(TEXT("dodge window ignores Weapon"),
			Capsule->GetCollisionResponseToChannel(ECC_GameTraceChannel1), ECR_Ignore);
		TestEqual(TEXT("dodge window ignores MonsterAttack"),
			Capsule->GetCollisionResponseToChannel(ECC_GameTraceChannel2), ECR_Ignore);

		Dodge->BlendOutForTest();
		TestEqual(TEXT("blend out enters MotionMatching"), Dodge->GetMovementPhase(),
			EMHGZDodgeMovementPhase::MotionMatching);
		TestFalse(TEXT("blend out releases only montage root motion"),
			H.Host->IsMontageRootMotionOwned());
		TestEqual(TEXT("blend out still owns Dodging"),
			H.ASC->GetTagCount(Dodging), 1);
		Dodge->ExpireMontageEndFailsafeForTest();
	}
	TestEqual(TEXT("normal-blend failsafe releases Invincible"),
		H.ASC->GetTagCount(Invincible), 0);
	TestEqual(TEXT("normal-blend failsafe releases Dodging"),
		H.ASC->GetTagCount(Dodging), 0);
	TestEqual(TEXT("normal-blend failsafe restores Weapon response"),
		Capsule->GetCollisionResponseToChannel(ECC_GameTraceChannel1), WeaponBefore);
	TestEqual(TEXT("normal-blend failsafe restores MonsterAttack response"),
		Capsule->GetCollisionResponseToChannel(ECC_GameTraceChannel2), MonsterAttackBefore);

	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM4DodgeExactAttackHandoff,
	"MHGZ.M4.Dodge.ExactAttackWindowAndSafeHandoff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM4DodgeExactAttackHandoff::RunTest(const FString& Parameters)
{
	FMHGZM3Harness H;
	if (!H.Setup())
	{
		AddError(TEXT("M4 dodge handoff harness setup failed"));
		H.Teardown();
		return false;
	}

	FGameplayAbilitySpecHandle AttackHandle;
	UGA_WeaponComboCoordinator* Coordinator = ConfigureTestAttack(H, AttackHandle);
	TestNotNull(TEXT("active coordinator exists"), Coordinator);
	if (!Coordinator)
	{
		H.Teardown();
		return false;
	}
	const FGameplayAbilitySpecHandle DodgeHandle =
		H.GiveAbility(UMHGZM4TestDodgeAbility::StaticClass());
	FWeaponInputSnapshot DodgeInput = M3::MakePosedInput(true, true);
	DodgeInput.ResolvedInputTag = M3::Tag(TEXT("Input.Dodge"));

	StartTestAttack(*Coordinator, 1);
	UMHGZM4TestAttackAbility* Attack = GetActiveInstance<UMHGZM4TestAttackAbility>(
		*H.ASC, AttackHandle);
	TestNotNull(TEXT("test attack is active"), Attack);
	TestFalse(TEXT("attack outside its exact window rejects dodge"),
		H.TryActivateWithInput(DodgeHandle, DodgeInput));

	const FGameplayTag DodgeAcceptOpen =
		M3::Tag(TEXT("Combat.State.DodgeAcceptOpen"));
	H.ASC->AddLooseGameplayTag(DodgeAcceptOpen);
	TestFalse(TEXT("unowned global DodgeAcceptOpen cannot authorize handoff"),
		H.TryActivateWithInput(DodgeHandle, DodgeInput));
	H.ASC->RemoveLooseGameplayTag(DodgeAcceptOpen);

	if (Attack)
	{
		TestTrue(TEXT("exact attack window opens"),
			Attack->BeginDodgeAcceptWindow(FName(TEXT("M4Accept"))));
	}
	TestEqual(TEXT("exact window contributes one tag count"),
		H.ASC->GetTagCount(DodgeAcceptOpen), 1);
	TestTrue(TEXT("windowed dodge activates"),
		H.TryActivateWithInput(DodgeHandle, DodgeInput));
	TestEqual(TEXT("old attack ends as Superseded"),
		Attack ? Attack->GetActionEndReason() : EWeaponActionEndReason::Normal,
		EWeaponActionEndReason::Superseded);
	TestNull(TEXT("old attack instance ended"),
		GetActiveInstance<UMHGZM4TestAttackAbility>(*H.ASC, AttackHandle));
	TestEqual(TEXT("old attack end closes its exact window"),
		H.ASC->GetTagCount(DodgeAcceptOpen), 0);
	TestEqual(TEXT("coordinator returns to Idle after direct dodge handoff"),
		Coordinator->GetCurrentState(), FName(TEXT("Idle")));

	UMHGZM4TestDodgeAbility* Dodge = GetActiveInstance<UMHGZM4TestDodgeAbility>(
		*H.ASC, DodgeHandle);
	TestNotNull(TEXT("new dodge remains active"), Dodge);
	if (Dodge)
	{
		Dodge->FinishNormallyForTest();
	}

	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM4DodgeFailurePreservesAttack,
	"MHGZ.M4.Dodge.FailedReplacementPreservesAttack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM4DodgeFailurePreservesAttack::RunTest(const FString& Parameters)
{
	FMHGZM3Harness H;
	if (!H.Setup())
	{
		AddError(TEXT("M4 dodge failure harness setup failed"));
		H.Teardown();
		return false;
	}

	FGameplayAbilitySpecHandle AttackHandle;
	UGA_WeaponComboCoordinator* Coordinator = ConfigureTestAttack(H, AttackHandle);
	if (!Coordinator)
	{
		AddError(TEXT("active coordinator missing"));
		H.Teardown();
		return false;
	}
	const FGameplayAbilitySpecHandle FailingDodgeHandle =
		H.GiveAbility(UMHGZM4FailingDodgeAbility::StaticClass());
	StartTestAttack(*Coordinator, 1);
	UMHGZM4TestAttackAbility* Attack = GetActiveInstance<UMHGZM4TestAttackAbility>(
		*H.ASC, AttackHandle);
	TestNotNull(TEXT("test attack is active"), Attack);
	if (Attack)
	{
		TestTrue(TEXT("attack accept window opens"),
			Attack->BeginDodgeAcceptWindow(FName(TEXT("M4FailureAccept"))));
	}

	FWeaponInputSnapshot DodgeInput = M3::MakePosedInput(true, true);
	DodgeInput.ResolvedInputTag = M3::Tag(TEXT("Input.Dodge"));
	H.TryActivateWithInput(FailingDodgeHandle, DodgeInput);
	TestNotNull(TEXT("failed dodge leaves old attack active"),
		GetActiveInstance<UMHGZM4TestAttackAbility>(*H.ASC, AttackHandle));
	TestEqual(TEXT("failed dodge does not assign Superseded to old attack"),
		Attack ? Attack->GetActionEndReason() : EWeaponActionEndReason::Cancelled,
		EWeaponActionEndReason::Normal);
	TestTrue(TEXT("failed dodge preserves coordinator active transition"),
		Coordinator->GetActiveTransition().IsSet());
	TestEqual(TEXT("failed dodge leaves no Dodging lock"),
		H.ASC->GetTagCount(M3::Tag(TEXT("Combat.State.Dodging"))), 0);
	TestFalse(TEXT("failed dodge leaves no root-motion owner"),
		H.Host->IsMontageRootMotionOwned());

	if (Attack)
	{
		Attack->RequestEndAction(EWeaponActionEndReason::Normal);
	}
	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM4DodgeDirectionalSelection,
	"MHGZ.M4.Dodge.DirectionalSelectionAndExitPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM4DodgeDirectionalSelection::RunTest(const FString& Parameters)
{
	FMHGZM3Harness H;
	if (!H.Setup())
	{
		AddError(TEXT("M4 directional dodge harness setup failed"));
		H.Teardown();
		return false;
	}

	const UMHGZDodgeAbility* DodgeDefaults = GetDefault<UMHGZDodgeAbility>();
	TestNotNull(TEXT("production dodge defaults exist"), DodgeDefaults);
	if (DodgeDefaults)
	{
		TestEqual(TEXT("all dodge variants share the instant cost policy"),
			DodgeDefaults->StaminaCostPolicy, EAbilityStaminaCostPolicy::Instant);
		TestEqual(TEXT("all dodge variants share the same stamina cost"),
			DodgeDefaults->StaminaCost.GetValueAtLevel(1.f), 25.f);
		TestFalse(TEXT("Forward rolls keep legacy root owner until E4.2 places phase notifies"),
			DodgeDefaults->bForwardDodgeUsesActionRootMotionPhase);
	}
	const FGameplayAbilitySpecHandle DodgeHandle =
		H.GiveAbility(UMHGZM4TestDodgeAbility::StaticClass());
	const FGameplayTag BlockMovement = M3::Tag(TEXT("Combat.State.BlockMovement"));
	FWeaponInputSnapshot ForwardInput = M3::MakePosedInput(true, true);
	ForwardInput.ResolvedInputTag = M3::Tag(TEXT("Input.Dodge"));
	ForwardInput.Direction = EDirectionalInput::Forward;
	ForwardInput.SequenceID = 1;

	TestTrue(TEXT("sheathed forward dodge activates"),
		H.TryActivateWithInput(DodgeHandle, ForwardInput));
	UMHGZM4TestDodgeAbility* Dodge = GetActiveInstance<UMHGZM4TestDodgeAbility>(
		*H.ASC, DodgeHandle);
	TestNotNull(TEXT("forward dodge instance exists"), Dodge);
	if (Dodge)
	{
		TestTrue(TEXT("sheathed Forward is allowed"),
			Dodge->IsDirectionAllowedForTest(true, EDirectionalInput::Forward));
		TestTrue(TEXT("sheathed None is allowed"),
			Dodge->IsDirectionAllowedForTest(true, EDirectionalInput::None));
		TestFalse(TEXT("sheathed Left is rejected"),
			Dodge->IsDirectionAllowedForTest(true, EDirectionalInput::Left));
		TestFalse(TEXT("sheathed Right is rejected"),
			Dodge->IsDirectionAllowedForTest(true, EDirectionalInput::Right));
		TestFalse(TEXT("sheathed Back is rejected"),
			Dodge->IsDirectionAllowedForTest(true, EDirectionalInput::Back));
		TestTrue(TEXT("forward dodge permits MoveExit"),
			Dodge->DoesActiveDodgeAllowMoveExit());
		Dodge->FinishNormallyForTest();
	}

	TestTrue(TEXT("test enters unsheathed pose"), H.Host->SetSheathed(false));
	uint32 SequenceID = 2;
	auto VerifyUnsheathedDirectionalDodge = [this, &H, DodgeHandle, &BlockMovement,
		&SequenceID](EDirectionalInput Direction, const FName MontageName,
		const FString& Label)
	{
		FWeaponInputSnapshot Input = M3::MakePosedInput(true, false);
		Input.ResolvedInputTag = M3::Tag(TEXT("Input.Dodge"));
		Input.Direction = Direction;
		Input.SequenceID = SequenceID++;
		TestTrue(FString::Printf(TEXT("unsheathed %s dodge activates"), *Label),
			H.TryActivateWithInput(DodgeHandle, Input));

		UMHGZM4TestDodgeAbility* Active = GetActiveInstance<UMHGZM4TestDodgeAbility>(
			*H.ASC, DodgeHandle);
		TestNotNull(FString::Printf(TEXT("unsheathed %s dodge instance exists"), *Label),
			Active);
		if (!Active)
		{
			return;
		}

		UAnimMontage* DirectionalMontage = NewObject<UAnimMontage>(
			GetTransientPackage(), MontageName);
		switch (Direction)
		{
		case EDirectionalInput::Left:
			Active->UnsheathedLeftDodgeMontage = DirectionalMontage;
			break;
		case EDirectionalInput::Right:
			Active->UnsheathedRightDodgeMontage = DirectionalMontage;
			break;
		case EDirectionalInput::Back:
			Active->UnsheathedBackDodgeMontage = DirectionalMontage;
			break;
		default:
			AddError(TEXT("directional dodge test received a non-directional input"));
			break;
		}

		TestEqual(FString::Printf(TEXT("unsheathed %s resolves its direct montage field"),
			*Label), Active->ResolveDodgeMontageForTest(), DirectionalMontage);
		TestFalse(FString::Printf(TEXT("unsheathed %s never permits MoveExit"), *Label),
			Active->DoesActiveDodgeAllowMoveExit());
		Active->SetLiveMovementInputForTest(true);
		TestTrue(FString::Printf(TEXT("unsheathed %s decides an exit"), *Label),
			Active->DecideDodgeExit(Active->GetActionToken()));
		TestEqual(FString::Printf(TEXT("unsheathed %s forces IdleExit"), *Label),
			Active->GetLastJumpedSectionForTest(), FName(TEXT("IdleExit")));
		TestFalse(FString::Printf(TEXT("unsheathed %s cannot enter MoveExit"), *Label),
			Active->EnterMoveExit(Active->GetActionToken()));
		TestEqual(FString::Printf(TEXT("unsheathed %s retains movement lock"), *Label),
			H.ASC->GetTagCount(BlockMovement), 1);
		Active->FinishNormallyForTest();
	};

	VerifyUnsheathedDirectionalDodge(EDirectionalInput::Left,
		FName(TEXT("M4LeftDodge")), TEXT("Left"));
	VerifyUnsheathedDirectionalDodge(EDirectionalInput::Right,
		FName(TEXT("M4RightDodge")), TEXT("Right"));
	VerifyUnsheathedDirectionalDodge(EDirectionalInput::Back,
		FName(TEXT("M4BackDodge")), TEXT("Back"));

	H.Teardown();
	return true;
}

#endif
