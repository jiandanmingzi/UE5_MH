// Copyright MHGZ Project. All Rights Reserved.
#include "Res_InsectGlaive.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InsectGlaive/Kinsect/IGCoAttackComponent.h"
#include "InsectGlaive/Kinsect/Kinsect.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

bool URes_InsectGlaive::OpenKinsectCoAttackWindow(const FWeaponActionToken& Action,
	const int32 Segment, const uint64 Occurrence, const FGuid& SourceAttackID,
	const float MotionValue, const int32 MaxHits, const float HitInterval)
{
	UMHGZWeaponRuntimeHostComponent* Host = Action.RuntimeToken.Host.Get();
	if (bRuntimeShuttingDown || !CoAttackComponent || !CoAttackComponent->IsRunning()
		|| !Host || !Host->IsActionRegistered(Action)
		|| !IsKinsectCommandCurrent(CoAttackComponent->GetCommandSerial(),
			EKinsectCommandSource::CoAttack, 0, Action.RuntimeToken)) return false;
	FIGCoAttackWindowSpec Spec;
	Spec.Segment = Segment; Spec.Occurrence = Occurrence; Spec.SourceAttackID = SourceAttackID;
	Spec.MotionValue = MotionValue; Spec.AttackPower = GetModifiedKinsectAttackPower();
	Spec.MaxHits = MaxHits; Spec.HitInterval = HitInterval;
	Spec.OpenTime = GetWorld()->GetTimeSeconds();
	return CoAttackComponent->OpenWindow(Action, Spec);
}

void URes_InsectGlaive::CloseKinsectCoAttackWindow(const FWeaponActionToken& Action, const uint64 Occurrence)
{
	if (CoAttackComponent) CoAttackComponent->CloseWindow(Action, Occurrence);
}

bool URes_InsectGlaive::BeginKinsectCoAttack(const FWeaponActionToken& ActionToken,
	USkeletalMeshComponent* WeaponMesh, const FIGCoAttackSource& Source)
{
	UMHGZWeaponRuntimeHostComponent* Host = ActionToken.RuntimeToken.Host.Get();
	ACharacter* Character = RuntimeContext.Character.Get();
	if (bRuntimeShuttingDown || !ActionToken.IsValid()
		|| !IsRuntimeRequestCurrent(ActionToken.RuntimeToken) || !Host
		|| !Host->IsActionRegistered(ActionToken) || !Character
		|| !Character->GetCharacterMovement()->IsMovingOnGround() || !CombatConfig
		|| !CombatConfig->CoAttackTuning.IsValid() || !Source.bEnabled
		|| Source.LocalOffset.ContainsNaN() || Source.Socket.IsNone()
		|| !IsValid(WeaponMesh) || WeaponMesh->GetOwner() != Character
		|| !WeaponMesh->DoesSocketExist(Source.Socket) || !IsValid(KinsectActor) || !KinsectData
		|| KinsectStamina <= 0.f
		|| KinsectPlayerCommandLeaseToken.IsValid() || KinsectActionAttachmentToken.IsValid()) return false;
	const bool bSameAction = CoAttackComponent && CoAttackComponent->IsRunning()
		&& CoAttackComponent->GetActionToken() == ActionToken;
	const bool bApprovedPending = DescendingThrustKinsect.IsSet()
		&& DescendingThrustKinsect->PendingAction == ActionToken && !DescendingThrustKinsect->bPendingExpired;
	if (!bSameAction && !bApprovedPending && !HasKinsectCoAttackEntryStamina()) return false;
	if (CoAttackComponent && CoAttackComponent->IsRunning())
		return CoAttackComponent->Join(ActionToken, WeaponMesh, Source);
	uint64 HoverSerial = 0;
	if (DescendingThrustKinsect.IsSet())
	{
		if (!PrepareDescendingThrustCoAttackHandoff(ActionToken, WeaponMesh, Source, HoverSerial)) return false;
		if (HoverSerial == 0) return true; // dive still owns movement; no history/windows are created
	}
	else if (KinsectActor->GetState() != EKinsectState::Attached) return false;
	if (!CoAttackComponent)
	{
		CoAttackComponent = NewObject<UIGCoAttackComponent>(Character);
		CoAttackComponent->RegisterComponent();
	}
	const uint64 Serial = AdvanceKinsectCommand(EKinsectCommandSource::CoAttack);
	if (!CoAttackComponent->Start(this, ActionToken, Serial, WeaponMesh, Source,
		CombatConfig->CoAttackTuning, HoverSerial))
	{
		AdvanceKinsectCommand(EKinsectCommandSource::None);
		return false;
	}
	SetKinsectActiveTag(true);
	return true;
}

void URes_InsectGlaive::FinishKinsectCoAttack(const FWeaponActionToken& ActionToken)
{
	if (CoAttackComponent && CoAttackComponent->IsRunning()
		&& CoAttackComponent->GetActionToken() == ActionToken
		&& !CoAttackComponent->Seal(ActionToken))
		EndKinsectCoAttack(ActionToken);
}

void URes_InsectGlaive::FinishKinsectCoAttackPath(const FWeaponActionToken& ActionToken,
	const bool bReturnAfterReplay)
{
	if (CoAttackComponent) CoAttackComponent->FinishAttackPath(ActionToken, bReturnAfterReplay);
}

void URes_InsectGlaive::EndKinsectCoAttack(const FWeaponActionToken& ActionToken)
{
	if (!CoAttackComponent || !CoAttackComponent->IsRunning()
		|| CoAttackComponent->GetActionToken() != ActionToken) return;
	const uint64 Serial = CoAttackComponent->GetCommandSerial();
	const FWeaponRuntimeToken RuntimeToken = ActionToken.RuntimeToken;
	CoAttackComponent->Stop();
	if (IsValid(KinsectActor) && KinsectActor->GetState() == EKinsectState::CoAttack)
	{
		if (IsKinsectCommandCurrent(Serial, EKinsectCommandSource::CoAttack,
			0, RuntimeToken))
		{
			if (!KinsectActor->StartReturn(Serial, EKinsectCommandSource::CoAttack))
			{
				AdvanceKinsectCommand(EKinsectCommandSource::None);
				EarliestAutoMarkUpdateFrame = GFrameCounter + 1;
			}
		}
		else KinsectActor->Interrupt();
	}
}
