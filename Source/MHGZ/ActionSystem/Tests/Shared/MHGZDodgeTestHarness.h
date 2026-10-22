// Copyright MHGZ Project. All Rights Reserved.
#pragma once
#include "MHGZM3TestHarness.h"
#include "MHGZM4TestTypes.h"
#include "ActionSystem/MHGZComboCoordinatorAbility.h"

namespace M4Dodge
{
/** Start a real coordinator-owned Attacking action, not a fabricated aggregate tag. */
inline UMHGZM4TestAttackAbility* StartCancelableAttack(FMHGZM3Harness& H, bool bOpenWindow = true)
{
	auto* Coordinator = H.ASC->GetActiveComboCoordinator();
	if (!Coordinator) { return nullptr; }
	auto* Spec = H.ASC->FindAbilitySpecFromClass(UMHGZM4TestAttackAbility::StaticClass());
	const auto Handle = Spec ? Spec->Handle : H.GiveAbility(UMHGZM4TestAttackAbility::StaticClass());
	auto* Combo = NewObject<UMHGZWeaponComboData>(Coordinator);
	auto& Edge = Combo->Transitions.AddDefaulted_GetRef();
	Edge.TransitionID = TEXT("DodgeTestIdleToAttack");
	Edge.SourceState = TEXT("Idle");
	Edge.TargetState = TEXT("DodgeTestAttack");
	Edge.InputTag = M3::Tag(TEXT("Input.Weapon.Y"));
	Edge.AbilityClass = UMHGZM4TestAttackAbility::StaticClass();
	Coordinator->InjectComboData(Combo);
	auto Input = M3::MakePosedInput(true, H.Host->IsSheathed());
	Input.ResolvedInputTag = Edge.InputTag;
	Input.SourceControlTag = Edge.InputTag;
	Input.Phase = EWeaponInputPhase::Started;
	Input.SequenceID = H.Host->AllocateActivationSequenceID();
	Coordinator->HandleWeaponInput(Input);
	Spec = H.ASC->FindAbilitySpecFromHandle(Handle);
	if (!Spec) { return nullptr; }
	for (UGameplayAbility* Instance : Spec->GetAbilityInstances())
	{
		auto* Attack = Cast<UMHGZM4TestAttackAbility>(Instance);
		if (Attack && Attack->IsActive())
		{
			return !bOpenWindow || Attack->BeginDodgeAcceptWindow(TEXT("DirectionalDodgeTestAccept")) ? Attack : nullptr;
		}
	}
	return nullptr;
}
}
