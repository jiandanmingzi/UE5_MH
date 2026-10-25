// Copyright MHGZ Project. All Rights Reserved.
#include "Res_InsectGlaive.h"
#include "MHGZAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "InsectGlaive/Kinsect/Kinsect.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"

namespace
{
bool AliveForKinsectSettlement(const UAbilitySystemComponent* ASC)
{
	if (!ASC || ASC->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Dead")))) return false;
	const UMHGZAttributeSet* Attributes = ASC->GetSet<UMHGZAttributeSet>();
	return !Attributes || Attributes->GetHealth() > 0.f;
}
}

void URes_InsectGlaive::RetireGuaranteedKinsectHit(const FGuid& ReceiptID)
{
	GuaranteedKinsectHitReceipts.RemoveAll([&](const auto& R) { return R.ID == ReceiptID; });
}

bool URes_InsectGlaive::ApplyGuaranteedKinsectPiercingContact(
	const FKinsectFlightRequest& Request, const FHitResult& Hit)
{
	UMHGZMonsterHitzoneComponent* Part = UMHGZMonsterHitzoneComponent::Resolve(Hit.GetComponent());
	if (bRuntimeShuttingDown || !GetWorld() || !Part || !IsValid(Hit.GetActor())
		|| !KinsectActor || KinsectActor->GetState() != EKinsectState::Flying
		|| KinsectActor->ActiveRequest.FlightInstanceID != Request.FlightInstanceID
		|| KinsectActor->ActiveRequest.SourceAttackInstanceID != Request.SourceAttackInstanceID
		|| KinsectActor->ActiveRequest.MinimumPiercingHitsPerTarget != Request.MinimumPiercingHitsPerTarget
		|| !IsKinsectCommandCurrent(Request) || Request.MinimumPiercingHitsPerTarget <= 0
		|| Request.DamageMode != EKinsectDamageMode::Piercing || Request.RehitInterval <= 0.f
		|| !Request.SourceAttackInstanceID.IsValid()) return false;

	auto* Receipt = GuaranteedKinsectHitReceipts.FindByPredicate([&](const auto& R)
	{
		return R.Runtime == Request.RuntimeToken && R.SourceAttackID == Request.SourceAttackInstanceID
			&& R.Target.Get() == Hit.GetActor();
	});
	const double Now = GetWorld()->GetTimeSeconds();
	if (!Receipt)
	{
		Receipt = &GuaranteedKinsectHitReceipts.AddDefaulted_GetRef();
		Receipt->ID = FGuid::NewGuid();
		Receipt->FlightID = Request.FlightInstanceID;
		Receipt->SourceAttackID = Request.SourceAttackInstanceID;
		Receipt->Runtime = Request.RuntimeToken;
		Receipt->Target = Hit.GetActor();
		Receipt->MotionValue = Request.MotionValue;
		Receipt->AttackPower = GetModifiedKinsectAttackPower();
		Receipt->Interval = Request.RehitInterval;
		Receipt->Minimum = Request.MinimumPiercingHitsPerTarget;
		Receipt->PowderPolicy = Request.PowderSpawnPolicy;
	}
	if (Receipt->bApplying) return false;
	if (const double* Previous = Receipt->HitTimes.Find(Part);
		Previous && Now - *Previous + UE_SMALL_NUMBER < Receipt->Interval) return false;
	return SubmitGuaranteedKinsectHit(Receipt->ID, Hit, Now, false);
}

bool URes_InsectGlaive::SubmitGuaranteedKinsectHit(
	const FGuid ReceiptID, const FHitResult& Hit, const double Now, const bool bScheduled)
{
	auto* Receipt = GuaranteedKinsectHitReceipts.FindByPredicate([&](const auto& R) { return R.ID == ReceiptID; });
	UMHGZMonsterHitzoneComponent* Part = UMHGZMonsterHitzoneComponent::Resolve(Hit.GetComponent());
	if (!Receipt || Receipt->bApplying || bRuntimeShuttingDown || !Part
		|| !IsRuntimeRequestCurrent(Receipt->Runtime) || !IsValid(Receipt->Target.Get())
		|| Hit.GetActor() != Receipt->Target.Get() || !AliveForKinsectSettlement(GetPlayerASC())
		|| !AliveForKinsectSettlement(UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Hit.GetActor())))
	{
		RetireGuaranteedKinsectHit(ReceiptID);
		return false;
	}
	// External GE/cargo/powder callbacks can replace the Runtime or append receipts.
	// Reacquire by immutable receipt ID after every external call.
	const FGuaranteedKinsectHitReceipt Snapshot = *Receipt;
	Receipt->bApplying = true;
	const FGuid HitID = FGuid::NewGuid();
	const bool bApplied = ApplyKinsectDamageWithSnapshot(Hit, Snapshot.MotionValue,
		Snapshot.AttackPower, HitID, Snapshot.SourceAttackID);
	Receipt = GuaranteedKinsectHitReceipts.FindByPredicate([&](const auto& R) { return R.ID == ReceiptID; });
	if (!Receipt) return bApplied;
	if (!bApplied || bRuntimeShuttingDown || !IsRuntimeRequestCurrent(Snapshot.Runtime))
	{
		RetireGuaranteedKinsectHit(ReceiptID);
		return false;
	}
	Part = UMHGZMonsterHitzoneComponent::Resolve(Hit.GetComponent());
	if (!IsValid(Hit.GetComponent()) || !Part)
	{
		RetireGuaranteedKinsectHit(ReceiptID);
		return true; // final successful damage may have destroyed the target part
	}
	++Receipt->Successful;
	Receipt->bApplying = false;
	Receipt->HitTimes.Add(Part, Now);
	Receipt->LastHit = Hit;
	const FTransform Transform = Hit.GetComponent()->GetComponentTransform();
	Receipt->LocalImpactPoint = Transform.InverseTransformPosition(Hit.ImpactPoint);
	Receipt->LocalLocation = Transform.InverseTransformPosition(Hit.Location);
	Receipt->LocalNormal = Transform.InverseTransformVectorNoScale(Hit.ImpactNormal);
	Receipt->NextDue = bScheduled ? Snapshot.NextDue + Snapshot.Interval : Now + Snapshot.Interval;
	// Physical contact collects new part colors normally. Scheduled repeats use
	// an already-collected part, so they never refill cargo after return delivery.
	if (KinsectActor && KinsectActor->GetState() == EKinsectState::Flying
		&& KinsectActor->ActiveRequest.FlightInstanceID == Snapshot.FlightID)
	{
		const FKinsectFlightRequest Flight = KinsectActor->ActiveRequest;
		CaptureExtractFromKinsectHit(Flight, Hit, HitID);
	}
	if (!bRuntimeShuttingDown && IsRuntimeRequestCurrent(Snapshot.Runtime))
		HandleSuccessfulKinsectDamage(Snapshot.Runtime, Snapshot.SourceAttackID, HitID, Hit, Snapshot.PowderPolicy);
	return true;
}

void URes_InsectGlaive::TickGuaranteedKinsectHits()
{
	if (GuaranteedKinsectHitReceipts.IsEmpty()) return;
	if (bRuntimeShuttingDown || !GetWorld() || !AliveForKinsectSettlement(GetPlayerASC()))
	{
		GuaranteedKinsectHitReceipts.Reset();
		return;
	}
	TArray<FGuid> IDs;
	for (const auto& R : GuaranteedKinsectHitReceipts) IDs.Add(R.ID);
	const double Now = GetWorld()->GetTimeSeconds();
	for (const FGuid& ID : IDs)
	{
		for (int32 Budget = 0; Budget < 64; ++Budget)
		{
			const auto* Receipt = GuaranteedKinsectHitReceipts.FindByPredicate([&](const auto& R) { return R.ID == ID; });
			if (!Receipt || Receipt->bApplying) break;
			if (!IsRuntimeRequestCurrent(Receipt->Runtime) || !IsValid(Receipt->Target.Get())
				|| !IsValid(Receipt->LastHit.GetComponent())
				|| !AliveForKinsectSettlement(UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Receipt->Target.Get())))
			{
				RetireGuaranteedKinsectHit(ID); break;
			}
			if (Receipt->Successful >= Receipt->Minimum)
			{
				// Retain completed counters until this flight can no longer hit, so a
				// seventh contact cannot establish another six-hit guarantee.
				if (!KinsectActor || KinsectActor->GetState() != EKinsectState::Flying
					|| KinsectActor->ActiveRequest.FlightInstanceID != Receipt->FlightID
					|| !IsKinsectCommandCurrent(KinsectActor->ActiveRequest)) RetireGuaranteedKinsectHit(ID);
				break;
			}
			if (Now + UE_SMALL_NUMBER < Receipt->NextDue) break;
			FHitResult Hit = Receipt->LastHit;
			const FTransform Transform = Hit.GetComponent()->GetComponentTransform();
			Hit.ImpactPoint = Transform.TransformPosition(Receipt->LocalImpactPoint);
			Hit.Location = Transform.TransformPosition(Receipt->LocalLocation);
			Hit.ImpactNormal = Transform.TransformVectorNoScale(Receipt->LocalNormal).GetSafeNormal();
			Hit.Normal = Hit.ImpactNormal;
			if (!SubmitGuaranteedKinsectHit(ID, Hit, Now, true)) break;
		}
	}
}
