// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZM5KinsectSlashCallbackTestTypes.h"

#include "AbilitySystemComponent.h"
#include "ActionSystem/MHGZKinsectSlashAbility.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

void UMHGZM5KinsectSlashDanceCancellationObserver::HandleDanceStateChanged(
	const int32 Stacks, const EIGDanceSource Source)
{
	// Hitstun may synchronously clear the resource and broadcast again. Only
	// the positive Kinsect Slash commit is the cancellation injection point.
	if (Stacks <= 0 || Source != EIGDanceSource::KinsectSlash)
	{
		return;
	}
	++KinsectSlashBroadcastCount;
	if (KinsectSlashBroadcastCount != 1)
	{
		return;
	}

	StacksAtDanceBroadcast = Stacks;
	bObservedActiveAbility = Ability && Ability->IsActive();
	UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
	FAnimMontageInstance* DanceInstance = AnimInstance && Ability && Ability->DanceMontage
		? AnimInstance->GetActiveInstanceForMontage(Ability->DanceMontage) : nullptr;
	if (DanceInstance)
	{
		DanceMontageInstanceID = DanceInstance->GetInstanceID();
		FWeaponActionToken RegisteredAction;
		bObservedExactRegisteredDanceMontage = Host && Ability
			&& AnimInstance->Montage_GetCurrentSection(Ability->DanceMontage)
				== Ability->DanceSectionName
			&& Host->ResolveMontage(Mesh, DanceMontageInstanceID, RegisteredAction)
			&& RegisteredAction == Ability->GetActionToken();
	}

	if (bCancelOnDanceStateChanged && ASC && Ability)
	{
		bAppliedHitstun = true;
		ASC->SetLooseGameplayTagCount(FGameplayTag::RequestGameplayTag(
			TEXT("Combat.State.Hitstun")), 1);
		bCancelledSynchronously = !Ability->IsActive();
	}
}
