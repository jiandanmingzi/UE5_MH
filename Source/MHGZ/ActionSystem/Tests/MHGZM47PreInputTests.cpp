// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "ActionSystem/MHGZComboCoordinatorAbility.h"
#include "ActionSystem/MHGZM1PlaceholderAbilities.h"
#include "ActionSystem/MHGZWeaponComboData.h"
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

UGA_WeaponComboCoordinator* ConfigureAttackAndFollowUps(FMHGZM3Harness& Harness,
	FGameplayAbilitySpecHandle& OutAttackHandle, float PreInputLifetime = 0.5f)
{
	UGA_WeaponComboCoordinator* Coordinator = Harness.ASC
		? Harness.ASC->GetActiveComboCoordinator()
		: nullptr;
	if (!Coordinator)
	{
		return nullptr;
	}

	OutAttackHandle = Harness.GiveAbility(UMHGZM4TestAttackAbility::StaticClass());
	Harness.GiveAbility(UMHGZM1PlaceholderActionA::StaticClass());
	Harness.GiveAbility(UMHGZM1PlaceholderActionB::StaticClass());
	UMHGZWeaponComboData* ComboData = NewObject<UMHGZWeaponComboData>(Coordinator);
	ComboData->PreInputLifetime = PreInputLifetime;

	FComboTransition& Start = ComboData->Transitions.AddDefaulted_GetRef();
	Start.TransitionID = FName(TEXT("PreInputIdleToAttack"));
	Start.SourceState = FName(TEXT("Idle"));
	Start.TargetState = FName(TEXT("PreInputAttack"));
	Start.InputTag = M3::Tag(TEXT("Input.Weapon.Y"));
	Start.AbilityClass = UMHGZM4TestAttackAbility::StaticClass();

	FComboTransition& FollowB = ComboData->Transitions.AddDefaulted_GetRef();
	FollowB.TransitionID = FName(TEXT("PreInputFollowB"));
	FollowB.SourceState = FName(TEXT("PreInputAttack"));
	FollowB.TargetState = FName(TEXT("PreInputB"));
	FollowB.InputTag = M3::Tag(TEXT("Input.Weapon.B"));
	FollowB.AbilityClass = UMHGZM1PlaceholderActionA::StaticClass();
	FollowB.bRequiresComboWindow = true;

	FComboTransition& FollowY = ComboData->Transitions.AddDefaulted_GetRef();
	FollowY.TransitionID = FName(TEXT("PreInputFollowY"));
	FollowY.SourceState = FName(TEXT("PreInputAttack"));
	FollowY.TargetState = FName(TEXT("PreInputY"));
	FollowY.InputTag = M3::Tag(TEXT("Input.Weapon.Y"));
	FollowY.AbilityClass = UMHGZM1PlaceholderActionB::StaticClass();
	FollowY.bRequiresComboWindow = true;

	Coordinator->InjectComboData(ComboData);
	return Coordinator;
}

void SendWeaponInput(UGA_WeaponComboCoordinator& Coordinator, const TCHAR* InputTag,
	uint32 SequenceID)
{
	FWeaponInputSnapshot Input = M3::MakePosedInput(true, true);
	Input.ResolvedInputTag = M3::Tag(InputTag);
	Input.SourceControlTag = Input.ResolvedInputTag;
	Input.SequenceID = SequenceID;
	Input.Phase = EWeaponInputPhase::Started;
	Coordinator.HandleWeaponInput(Input);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM47PreInputComboLastInputWins,
	"MHGZ.M4.Input.PreInput.ComboWindowConsumesLatestInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM47PreInputComboLastInputWins::RunTest(const FString& Parameters)
{
	FMHGZM3Harness Harness;
	if (!Harness.Setup())
	{
		AddError(TEXT("preinput combo harness setup failed"));
		Harness.Teardown();
		return false;
	}

	FGameplayAbilitySpecHandle AttackHandle;
	UGA_WeaponComboCoordinator* Coordinator = ConfigureAttackAndFollowUps(Harness,
		AttackHandle);
	if (!Coordinator)
	{
		AddError(TEXT("active coordinator missing"));
		Harness.Teardown();
		return false;
	}

	SendWeaponInput(*Coordinator, TEXT("Input.Weapon.Y"), 1);
	UMHGZM4TestAttackAbility* Attack = GetActiveInstance<UMHGZM4TestAttackAbility>(
		*Harness.ASC, AttackHandle);
	TestNotNull(TEXT("source attack is active"), Attack);
	SendWeaponInput(*Coordinator, TEXT("Input.Weapon.B"), 2);
	SendWeaponInput(*Coordinator, TEXT("Input.Weapon.Y"), 3);
	TestEqual(TEXT("inputs before the ComboWindow do not activate immediately"),
		Coordinator->GetCurrentState(), FName(TEXT("PreInputAttack")));

	if (Attack)
	{
		TestTrue(TEXT("opening ComboWindow consumes the single latest input"),
			Coordinator->OpenComboWindow(Attack->GetActionToken(), FName(TEXT("PreInputCombo"))));
	}
	TestEqual(TEXT("latest Y overwrites earlier B"), Coordinator->GetCurrentState(),
		FName(TEXT("PreInputY")));

	Coordinator->ResetCombo(EWeaponActionEndReason::Normal);
	Harness.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM47PreInputDisabledAndDodgeAccept,
	"MHGZ.M4.Input.PreInput.DisabledAndDodgeAcceptConsume",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM47PreInputDisabledAndDodgeAccept::RunTest(const FString& Parameters)
{
	{
		FMHGZM3Harness Harness;
		if (!Harness.Setup())
		{
			AddError(TEXT("preinput disabled harness setup failed"));
			Harness.Teardown();
			return false;
		}

		FGameplayAbilitySpecHandle AttackHandle;
		UGA_WeaponComboCoordinator* Coordinator = ConfigureAttackAndFollowUps(Harness,
			AttackHandle, 0.0f);
		if (!Coordinator)
		{
			AddError(TEXT("active coordinator missing for disabled case"));
			Harness.Teardown();
			return false;
		}
		SendWeaponInput(*Coordinator, TEXT("Input.Weapon.Y"), 1);
		UMHGZM4TestAttackAbility* Attack = GetActiveInstance<UMHGZM4TestAttackAbility>(
			*Harness.ASC, AttackHandle);
		SendWeaponInput(*Coordinator, TEXT("Input.Weapon.B"), 2);
		if (Attack)
		{
			Coordinator->OpenComboWindow(Attack->GetActionToken(),
				FName(TEXT("DisabledPreInputCombo")));
		}
		TestEqual(TEXT("zero lifetime disables the preinput buffer"),
			Coordinator->GetCurrentState(), FName(TEXT("PreInputAttack")));
		Coordinator->ResetCombo(EWeaponActionEndReason::Normal);
		Harness.Teardown();
	}

	FMHGZM3Harness Harness;
	if (!Harness.Setup())
	{
		AddError(TEXT("preinput dodge harness setup failed"));
		Harness.Teardown();
		return false;
	}

	FGameplayAbilitySpecHandle AttackHandle;
	UGA_WeaponComboCoordinator* Coordinator = ConfigureAttackAndFollowUps(Harness,
		AttackHandle);
	if (!Coordinator)
	{
		AddError(TEXT("active coordinator missing for dodge case"));
		Harness.Teardown();
		return false;
	}
	const FGameplayAbilitySpecHandle DodgeHandle =
		Harness.GiveAbility(UMHGZM4TestDodgeAbility::StaticClass());
	SendWeaponInput(*Coordinator, TEXT("Input.Weapon.Y"), 1);
	UMHGZM4TestAttackAbility* Attack = GetActiveInstance<UMHGZM4TestAttackAbility>(
		*Harness.ASC, AttackHandle);
	TestNotNull(TEXT("source attack for Dodge preinput is active"), Attack);

	FWeaponInputSnapshot DodgeInput = M3::MakePosedInput(true, true);
	DodgeInput.ResolvedInputTag = M3::Tag(TEXT("Input.Dodge"));
	DodgeInput.SourceControlTag = DodgeInput.ResolvedInputTag;
	DodgeInput.SequenceID = 2;
	DodgeInput.Phase = EWeaponInputPhase::Started;
	Harness.ASC->HandleResolvedInputSnapshot(DodgeInput);
	TestNull(TEXT("Dodge stays pending while its exact gate is closed"),
		GetActiveInstance<UMHGZM4TestDodgeAbility>(*Harness.ASC, DodgeHandle));

	if (Attack)
	{
		TestTrue(TEXT("DodgeAccept opening consumes the direct Dodge preinput"),
			Attack->BeginDodgeAcceptWindow(FName(TEXT("PreInputDodgeAccept"))));
	}
	UMHGZM4TestDodgeAbility* Dodge = GetActiveInstance<UMHGZM4TestDodgeAbility>(
		*Harness.ASC, DodgeHandle);
	TestNotNull(TEXT("buffered Dodge activates through the normal direct route"), Dodge);
	if (Dodge)
	{
		Dodge->FinishNormallyForTest();
	}

	Harness.Teardown();
	return true;
}

#endif
