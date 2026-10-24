// Copyright MHGZ Project. All Rights Reserved.
#include "Res_InsectGlaive.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "ActionSystem/MHGZInsectGlaiveAbility.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "InsectGlaive/Kinsect/InsectGlaiveKinsectData.h"
#include "InsectGlaive/Kinsect/Kinsect.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

bool URes_InsectGlaive::BeginDescendingThrustKinsect(const FWeaponActionToken& Action,
	const FVector& Origin, const FGuid& AttackID, const float MotionValue)
{
	UMHGZWeaponRuntimeHostComponent* Host = Action.RuntimeToken.Host.Get();
	if (bRuntimeShuttingDown || !Host || !Host->IsActionRegistered(Action)
		|| !IsRuntimeRequestCurrent(Action.RuntimeToken) || !KinsectActor || !KinsectData || !CombatConfig
		|| KinsectActor->GetState() != EKinsectState::Attached
		|| KinsectActionAttachmentToken.IsValid() || KinsectPlayerCommandLeaseToken.IsValid()
		|| !HasKinsectCoAttackEntryStamina()
		|| Origin.ContainsNaN() || !AttackID.IsValid() || !FMath::IsFinite(MotionValue) || MotionValue <= 0.f) return false;
	// Admission never rejects the hunter GA. Once detached, normal depletion
	// recalls only the insect; falling below 10% does not revoke this admission.
	const uint64 Serial = AdvanceKinsectCommand(EKinsectCommandSource::DescendingThrust);
	if (!KinsectActor->BeginDescendingThrustWait(Action.RuntimeToken, Serial, Origin))
	{
		AdvanceKinsectCommand(EKinsectCommandSource::None);
		return false;
	}
	FDescendingThrustKinsectSession Session;
	Session.Action = Action; Session.Serial = Serial;
	Session.AttackID = AttackID; Session.MotionValue = MotionValue;
	DescendingThrustKinsect = MoveTemp(Session);
	DescendingSuccessorHandle = Host->OnWeaponActionRegistered.AddUObject(this, &URes_InsectGlaive::HandleDescendingThrustSuccessor);
	DescendingRecoveryHandle = Host->OnWeaponDodgeAcceptWindowOpened.AddUObject(this, &URes_InsectGlaive::HandleDescendingThrustSuccessorRecovery);
	DescendingComboClosedHandle = Host->OnWeaponComboWindowsClosed.AddUObject(this, &URes_InsectGlaive::HandleDescendingThrustComboClosed);
	SetKinsectActiveTag(true);
	return true;
}

bool URes_InsectGlaive::StartDescendingThrustKinsectDive(const FWeaponActionToken& Action,
	const FVector& TargetSnapshot)
{
	if (!DescendingThrustKinsect.IsSet() || DescendingThrustKinsect->Action != Action
		|| DescendingThrustKinsect->bDiving || DescendingThrustKinsect->bArrived
		|| DescendingThrustKinsect->bSourceEnded || !KinsectActor || !KinsectData || !CombatConfig
		|| TargetSnapshot.ContainsNaN()
		|| !IsKinsectCommandCurrent(DescendingThrustKinsect->Serial,
			EKinsectCommandSource::DescendingThrust, 0, Action.RuntimeToken)) return false;
	if (KinsectStamina <= 0.f) { RecallKinsect(EKinsectCommandSource::System); return false; }
	const FVector Target = TargetSnapshot;
	const float Distance = FVector::Dist(KinsectActor->GetActorLocation(), Target);
	if (Distance <= 1.f)
	{
		DescendingThrustKinsect->bArrived = true;
		return true;
	}
	FKinsectFlightRequest Request;
	Request.RuntimeToken = Action.RuntimeToken;
	Request.CommandSource = EKinsectCommandSource::DescendingThrust;
	Request.CommandSerial = DescendingThrustKinsect->Serial;
	Request.ActionActivationSequenceID = Action.ActivationSequenceID;
	Request.FlightInstanceID = FGuid::NewGuid();
	Request.SourceAttackInstanceID = DescendingThrustKinsect->AttackID;
	Request.TrajectoryMode = EKinsectTrajectoryMode::ToPoint;
	Request.TargetPointSnapshot = Target; // world point, not a moving owner target
	Request.FlightSpeed = KinsectData->FlightSpeed;
	Request.MaxDistance = Distance + 2.f;
	Request.ArrivalRadius = 1.f;
	Request.DamageMode = EKinsectDamageMode::Piercing;
	Request.ExtractMode = EKinsectExtractMode::CollectPerValidHit;
	Request.PostFlightPolicy = EKinsectPostFlightPolicy::Hover;
	Request.MotionValue = DescendingThrustKinsect->MotionValue;
	Request.RehitInterval = CombatConfig->AwakenedPierceHitInterval;
	Request.PowderSpawnPolicy = EIGPowderSpawnPolicy::MarkedContact;
	if (!KinsectActor->BeginFlight(Request)) return false;
	DescendingThrustKinsect->bDiving = true;
	DescendingThrustKinsect->FlightID = Request.FlightInstanceID;
	return true;
}

void URes_InsectGlaive::ResetDescendingThrustKinsect()
{
	if (DescendingThrustKinsect.IsSet())
		if (auto* Host = DescendingThrustKinsect->Action.RuntimeToken.Host.Get())
		{
			Host->OnWeaponActionRegistered.Remove(DescendingSuccessorHandle);
			Host->OnWeaponDodgeAcceptWindowOpened.Remove(DescendingRecoveryHandle);
			Host->OnWeaponComboWindowsClosed.Remove(DescendingComboClosedHandle);
		}
	DescendingSuccessorHandle.Reset(); DescendingRecoveryHandle.Reset(); DescendingComboClosedHandle.Reset();
	DescendingThrustKinsect.Reset();
}

void URes_InsectGlaive::FinishDescendingThrustKinsect(const FWeaponActionToken& Action, const EWeaponActionEndReason Reason)
{
	if (!DescendingThrustKinsect.IsSet() || DescendingThrustKinsect->Action != Action) return;
	if (Reason != EWeaponActionEndReason::Normal && Reason != EWeaponActionEndReason::Superseded)
	{
		RecallKinsect(EKinsectCommandSource::System);
		return;
	}
	DescendingThrustKinsect->bSourceEnded = true;
	DescendingThrustKinsect->bSourceSuperseded = Reason == EWeaponActionEndReason::Superseded;
	DescendingThrustKinsect->SourceEndFrame = GFrameCounter;
	// If superseded before the last ground window ever runs, never invent a
	// retroactive attack. The already detached insect simply remains on standby.
	if (!DescendingThrustKinsect->bDiving) DescendingThrustKinsect->bArrived = true;
}

bool URes_InsectGlaive::HandleDescendingThrustKinsectFlightEnd(const FKinsectFlightRequest& Request,
	const EKinsectFlightEndReason Reason)
{
	if (Request.CommandSource != EKinsectCommandSource::DescendingThrust) return false;
	if (DescendingThrustKinsect.IsSet() && DescendingThrustKinsect->Serial == Request.CommandSerial
		&& DescendingThrustKinsect->FlightID == Request.FlightInstanceID)
	{
		DescendingThrustKinsect->bArrived = Reason == EKinsectFlightEndReason::Arrival;
		if (!DescendingThrustKinsect->bArrived) RecallKinsect(EKinsectCommandSource::System);
	}
	return true;
}

bool URes_InsectGlaive::PrepareDescendingThrustCoAttackHandoff(const FWeaponActionToken& Action,
	USkeletalMeshComponent* Mesh, const FIGCoAttackSource& Source, uint64& OutHoverSerial)
{
	OutHoverSerial = 0;
	if (!DescendingThrustKinsect.IsSet() || Action == DescendingThrustKinsect->Action
		|| Action.RuntimeToken != DescendingThrustKinsect->Action.RuntimeToken
		|| !IsKinsectCommandCurrent(DescendingThrustKinsect->Serial,
			EKinsectCommandSource::DescendingThrust, 0, Action.RuntimeToken)) return false;
	if (Action != DescendingThrustKinsect->PendingAction)
	{
		const bool bApprovedChain = DescendingThrustKinsect->PendingAction.IsValid()
			&& !DescendingThrustKinsect->bPendingExpired;
		const bool bImmediateSuccessor = DescendingThrustKinsect->bSourceSuperseded
			&& !DescendingThrustKinsect->PendingAction.IsValid()
			&& DescendingThrustKinsect->SourceEndFrame == GFrameCounter;
		if ((DescendingThrustKinsect->bSourceComboClosed || DescendingThrustKinsect->bSourceEnded)
			&& !bApprovedChain && !bImmediateSuccessor) return false;
		DescendingThrustKinsect->bPendingExpired = false;
		DescendingThrustKinsect->PendingAction = Action;
	}
	DescendingThrustKinsect->PendingMesh = Mesh;
	DescendingThrustKinsect->PendingSource = Source;
	if (DescendingThrustKinsect->bPendingExpired) return false;
	if (!DescendingThrustKinsect->bArrived) return true; // accepted, not yet sampling
	if (KinsectActor->GetState() != EKinsectState::Hovering) return false;
	OutHoverSerial = DescendingThrustKinsect->Serial;
	return true;
}

void URes_InsectGlaive::HandleDescendingThrustSuccessor(const FWeaponActionToken& Action)
{
	if (!DescendingThrustKinsect.IsSet() || Action == DescendingThrustKinsect->Action
		|| Action.RuntimeToken != DescendingThrustKinsect->Action.RuntimeToken) return;
	if (auto* IG = Cast<UMHGZInsectGlaiveAbility>(Action.AbilityInstance.Get()))
		if (IG->CoAttack.bEnabled)
		{
			// A ground attack without admission must not abort the pending dive.
			// It simply gets no coattack; expiry will release the old session.
			IG->TryStartKinsectCoAttack();
			return;
		}
	// Successful non-cooperative action: retain the established command priority.
	if (DescendingThrustKinsect.IsSet()) RecallKinsect(EKinsectCommandSource::System);
}

void URes_InsectGlaive::HandleDescendingThrustSuccessorRecovery(const FWeaponActionToken& Action)
{
	if (DescendingThrustKinsect.IsSet() && Action == DescendingThrustKinsect->PendingAction)
		DescendingThrustKinsect->bPendingExpired = true;
}

void URes_InsectGlaive::HandleDescendingThrustComboClosed(const FWeaponActionToken& Action)
{
	if (DescendingThrustKinsect.IsSet() && Action == DescendingThrustKinsect->Action)
		DescendingThrustKinsect->bSourceComboClosed = true;
	// Finish the dive, but close admission for unrelated later attacks.
	HandleDescendingThrustSuccessorRecovery(Action);
}

void URes_InsectGlaive::TickDescendingThrustKinsect()
{
	if (!DescendingThrustKinsect.IsSet()) return;
	const FWeaponRuntimeToken Runtime = DescendingThrustKinsect->Action.RuntimeToken;
	if (!KinsectActor || !IsKinsectCommandCurrent(DescendingThrustKinsect->Serial,
		EKinsectCommandSource::DescendingThrust, 0, Runtime)) { ResetDescendingThrustKinsect(); return; }
	const auto* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(RuntimeContext.Character.Get());
	if (ASC && (ASC->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Dead")))
		|| (ASC->HasAttributeSetForAttribute(UMHGZAttributeSet::GetHealthAttribute())
			&& ASC->GetNumericAttribute(UMHGZAttributeSet::GetHealthAttribute()) <= 0.f)))
	{ RecallKinsect(EKinsectCommandSource::System); return; }
	if (!DescendingThrustKinsect->bSourceEnded && !Runtime.Host->IsActionRegistered(DescendingThrustKinsect->Action))
	{ RecallKinsect(EKinsectCommandSource::System); return; }
	const FWeaponActionToken Pending = DescendingThrustKinsect->PendingAction;
	if (Pending.IsValid() && !Runtime.Host->IsActionRegistered(Pending))
	{
		DescendingThrustKinsect->bPendingExpired = true;
	}
	if (!DescendingThrustKinsect->bArrived) return;
	if (!Pending.IsValid() || DescendingThrustKinsect->bPendingExpired)
	{
		if (DescendingThrustKinsect->bSourceComboClosed || DescendingThrustKinsect->bSourceEnded)
		{
			// Actor already hovers. Release the special command/registration lease,
			// preserving position/cargo and allowing ordinary hover/auto-mark behavior.
			AdvanceKinsectCommand(EKinsectCommandSource::None);
			EarliestAutoMarkUpdateFrame = GFrameCounter + 1;
		}
		return;
	}
	const auto Mesh = DescendingThrustKinsect->PendingMesh;
	const FIGCoAttackSource Source = DescendingThrustKinsect->PendingSource;
	BeginKinsectCoAttack(Pending, Mesh.Get(), Source);
}
