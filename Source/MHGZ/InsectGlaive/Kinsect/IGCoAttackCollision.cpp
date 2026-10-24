// Copyright MHGZ Project. All Rights Reserved.
#include "IGCoAttackComponent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "AttributeSystem/Res_InsectGlaive.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kinsect.h"
#include "KinsectCollisionComponent.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"
#include "DrawDebugHelpers.h"

bool UIGCoAttackComponent::IsSessionCurrent(const uint64 Serial) const
{
	const URes_InsectGlaive* RC = Resource.Get();
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const AKinsect* Insect = RC ? RC->GetKinsectActor() : nullptr;
	const UAbilitySystemComponent* OwnerASC = Character
		? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Character) : nullptr;
	if (OwnerASC && (OwnerASC->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Dead")))
		|| (OwnerASC->HasAttributeSetForAttribute(UMHGZAttributeSet::GetHealthAttribute())
			&& OwnerASC->GetNumericAttribute(UMHGZAttributeSet::GetHealthAttribute()) <= 0.f))) return false;
	return Serial != 0 && CommandSerial == Serial && RC && IsValid(Insect)
		&& Insect->State == EKinsectState::CoAttack && Character
		&& Weapon.IsValid() && Weapon->IsRegistered() && CharacterMesh.IsValid()
		&& CharacterMesh->IsRegistered() && Weapon->DoesSocketExist(Source.Socket)
		&& Character->GetCharacterMovement()->IsMovingOnGround()
		&& RC->IsKinsectCommandCurrent(Serial, EKinsectCommandSource::CoAttack, 0, ActionToken.RuntimeToken)
		&& ActionToken.RuntimeToken.Host.IsValid()
		&& (SourceEndTime.IsSet() || ActionToken.RuntimeToken.Host->IsActionRegistered(ActionToken));
}

bool UIGCoAttackComponent::OpenWindow(const FWeaponActionToken& Action, const FIGCoAttackWindowSpec& Spec)
{
	if (Action != ActionToken || SourceEndTime.IsSet() || !IsSessionCurrent(CommandSerial) || Spec.Occurrence == 0
		|| Spec.Segment < 0 || !Spec.SourceAttackID.IsValid() || Spec.MaxHits < 1
		|| !FMath::IsFinite(Spec.MotionValue) || Spec.MotionValue <= 0.f
		|| !FMath::IsFinite(Spec.AttackPower) || Spec.AttackPower < 0.f
		|| !FMath::IsFinite(Spec.HitInterval) || Spec.HitInterval < 0.f
		|| !FMath::IsFinite(Spec.OpenTime) || Windows.Num() >= 64
		|| Windows.ContainsByPredicate([&](const FWindow& W) {
			return W.SourceAction == Action && W.Spec.Occurrence == Spec.Occurrence; })) return false;
	FWindow& Window = Windows.AddDefaulted_GetRef();
	// A later occurrence may resume an attack-end fallback, but cannot undo this
	// action's first DodgeAccept boundary. A confirmed successor resets it in Join.
	if (!bRecoveryPathFrozen)
	{
		AttackPathEndTime.Reset(); FrozenAttackPose.Reset(); bReturnAfterAttackReplay = false;
	}
	Window.ReceiptID = ++NextReceiptID;
	Window.SourceAction = Action;
	Window.Spec = Spec;
	return true;
}

void UIGCoAttackComponent::CloseWindow(const FWeaponActionToken& Action, const uint64 Occurrence)
{
	if (Action != ActionToken || SourceEndTime.IsSet() || !IsRunning()) return;
	if (FWindow* W = Windows.FindByPredicate([&](const FWindow& Item) {
		return Item.SourceAction == Action && Item.Spec.Occurrence == Occurrence; }))
		if (!W->CloseTime.IsSet()) W->CloseTime = FMath::Max(W->Spec.OpenTime, double(GetWorld()->GetTimeSeconds()));
}

bool UIGCoAttackComponent::ReplayWindows(const double From, const double To, const uint64 Serial)
{
	if (To < From || !IsSessionCurrent(Serial)) return false;
	TArray<uint64> IDs;
	for (const FWindow& W : Windows) IDs.Add(W.ReceiptID);
	int32 Queries = 0;
	for (const uint64 ID : IDs)
	{
		FWindow* Window = Windows.FindByPredicate([ID](const FWindow& W) { return W.ReceiptID == ID; });
		if (!Window || Window->bSkipped || Window->Spec.OpenTime > To) continue;
		if (!Window->bStarted)
		{
			Window->bStarted = true;
			// Arrival after opening skips the WHOLE opportunity, never a late partial hit.
			Window->bSkipped = !TrackingSince.IsSet() || TrackingSince.GetValue() > Window->Spec.OpenTime + 1.e-6;
			if (Window->bSkipped) continue;
		}
		const double Start = FMath::Max(From, Window->Spec.OpenTime);
		const double End = Window->CloseTime.IsSet() ? FMath::Min(To, Window->CloseTime.GetValue()) : To;
		if (End < Start) continue;
		TArray<FIGCoAttackHistory::FSample> Path;
		if (!History.GetPath(Start, End, Path)) return false;
		FVector Forward = ReplayForward;
		// Initial zero-distance contact at the exact open boundary is part of the window.
		if (!Window->bQueriedOpen && Start == Window->Spec.OpenTime)
		{
			Window->bQueriedOpen = true;
			if (Path.Num() > 1) Forward = (Path[1].Pose.GetLocation() - Path[0].Pose.GetLocation()).GetSafeNormal(SMALL_NUMBER, Forward);
			if (++Queries > Tuning.MaxSweepSegments || !SweepWindow(ID, Path[0].Pose.GetLocation(),
				Path[0].Pose.GetLocation(), Forward, Start + Tuning.TrajectoryDelaySeconds,
				Start + Tuning.TrajectoryDelaySeconds, Serial)) return false;
		}
		for (int32 I = 1; I < Path.Num(); ++I)
		{
			const FVector A = Path[I - 1].Pose.GetLocation(), B = Path[I].Pose.GetLocation();
			Forward = (B - A).GetSafeNormal(SMALL_NUMBER, Forward);
			if (++Queries > Tuning.MaxSweepSegments
				|| !SweepWindow(ID, A, B, Forward, Path[I - 1].Time + Tuning.TrajectoryDelaySeconds,
					Path[I].Time + Tuning.TrajectoryDelaySeconds, Serial)) return false;
		}
		Window = Windows.FindByPredicate([ID](const FWindow& W) { return W.ReceiptID == ID; });
		if (Window && Window->CloseTime.IsSet() && End == Window->CloseTime.GetValue())
		{
			// Also observe a stationary/overlapping closing pose at its actual timestamp.
			// The per-target timestamp guard prevents counting a shared endpoint twice.
			if (++Queries > Tuning.MaxSweepSegments || !SweepWindow(ID, Path.Last().Pose.GetLocation(),
				Path.Last().Pose.GetLocation(), Forward, End + Tuning.TrajectoryDelaySeconds,
				End + Tuning.TrajectoryDelaySeconds, Serial)) return false;
		}
	}
	// Prune only AFTER the closing endpoint was queried. No references survive a GE callback.
	Windows.RemoveAll([To](const FWindow& W) { return W.CloseTime.IsSet() && W.CloseTime.GetValue() <= To; });
	TArray<FIGCoAttackHistory::FSample> WholePath;
	if (History.GetPath(From, To, WholePath))
		for (int32 I = 1; I < WholePath.Num(); ++I)
			ReplayForward = (WholePath[I].Pose.GetLocation() - WholePath[I - 1].Pose.GetLocation()).GetSafeNormal(SMALL_NUMBER, ReplayForward);
	return IsSessionCurrent(Serial);
}

bool UIGCoAttackComponent::SweepWindow(const uint64 Occurrence, const FVector& From,
	const FVector& To, const FVector& Forward, const double BeginTime, const double EndTime, const uint64 Serial)
{
	if (!IsSessionCurrent(Serial)) return false;
	URes_InsectGlaive* RC = Resource.Get();
	AKinsect* Insect = RC->GetKinsectActor();
	// Sphere encloses the existing capsule and current visible mesh, never smaller than visuals.
	const float VisibleRadius = Insect->Mesh && Insect->Mesh->GetSkeletalMeshAsset()
		? float(Insect->Mesh->Bounds.SphereRadius
			+ FVector::Distance(Insect->Mesh->Bounds.Origin, Insect->GetActorLocation())) : 0.f;
	const float Radius = FMath::Max3(Tuning.SweepRadius,
		Insect->Collision ? Insect->Collision->GetScaledCapsuleHalfHeight() : 0.f,
		VisibleRadius);
	if (!FMath::IsFinite(Radius) || Radius <= 0.f) return false;
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_GameTraceChannel3);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(KinsectCoAttack), false, GetOwner());
	Params.AddIgnoredActor(Insect);
	TArray<FHitResult> Hits;
	GetWorld()->SweepMultiByObjectType(Hits, From, To, FQuat::Identity, Objects,
		FCollisionShape::MakeSphere(Radius), Params);
	Hits.Sort([](const FHitResult& A, const FHitResult& B) { return A.Time < B.Time; });
	TSet<TWeakObjectPtr<AActor>> Seen;
	for (const FHitResult& Hit : Hits)
	{
		AActor* Target = Hit.GetActor();
		if (!IsValid(Target) || Seen.Contains(Target) || !UMHGZMonsterHitzoneComponent::Resolve(Hit.GetComponent())
			|| !AKinsect::IsContactInFront(Hit, Forward)) continue;
		UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target);
		if (!ASC || ASC->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Dead")))
			|| (ASC->HasAttributeSetForAttribute(UMHGZAttributeSet::GetHealthAttribute())
				&& ASC->GetNumericAttribute(UMHGZAttributeSet::GetHealthAttribute()) <= 0.f)) continue;
		Seen.Add(Target); // several hitzones on one target are one contact opportunity
		FWindow* W = Windows.FindByPredicate([Occurrence](const FWindow& Item) { return Item.ReceiptID == Occurrence; });
		if (!W) return false;
		FTargetBudget& Budget = W->Targets.FindOrAdd(Target);
		const double ContactTime = From.Equals(To, .001f)
			? EndTime : FMath::Lerp(BeginTime, EndTime, double(Hit.Time));
		if (Budget.Hits >= W->Spec.MaxHits || ContactTime <= Budget.LastHit + 1.e-6
			|| ContactTime - Budget.LastHit + 1.e-6 < W->Spec.HitInterval) continue;
		const FIGCoAttackWindowSpec Snapshot = W->Spec;
		const FTargetBudget Before = Budget;
		++Budget.Hits; Budget.LastHit = ContactTime; // reserve before re-entrant GE callbacks
		const FGuid HitID = FGuid::NewGuid();
		const bool bApplied = RC->ApplyKinsectDamageWithSnapshot(Hit, Snapshot.MotionValue,
			Snapshot.AttackPower, HitID, Snapshot.SourceAttackID);
		if (!IsSessionCurrent(Serial)) return false;
		W = Windows.FindByPredicate([Occurrence](const FWindow& Item) { return Item.ReceiptID == Occurrence; });
		if (!W) return false;
		if (!bApplied) W->Targets.FindOrAdd(Target) = Before;
		// No extract/weapon self effect/mark/dance; ordinary marked-contact policy only.
		else RC->HandleSuccessfulKinsectDamage(ActionToken.RuntimeToken, Snapshot.SourceAttackID,
			HitID, Hit, EIGPowderSpawnPolicy::MarkedContact);
		if (!IsSessionCurrent(Serial)) return false;
	}
	if (Tuning.bDrawDebug)
	{
		DrawDebugLine(GetWorld(), From, To, FColor::Red);
		if (const auto* W = Windows.FindByPredicate([Occurrence](const FWindow& Item) { return Item.ReceiptID == Occurrence; }))
		{
			int32 SubmittedHits = 0;
			for (const auto& Target : W->Targets) SubmittedHits += Target.Value.Hits;
			DrawDebugString(GetWorld(), To, FString::Printf(TEXT("CoAttack #%llu Seg=%d Hits=%d"),
				Occurrence, W->Spec.Segment, SubmittedHits), nullptr, FColor::Red, 0.f);
		}
	}
	return true;
}
