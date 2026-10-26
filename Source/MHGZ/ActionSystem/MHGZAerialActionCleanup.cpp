// Copyright MHGZ Project. All Rights Reserved.

#include "ActionSystem/MHGZAerialActionCleanup.h"

#include "AbilitySystemComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

namespace MHGZAerialActionCleanup
{
void UnbindRuntimeInvalidation(UMHGZWeaponRuntimeHostComponent* Host,
	FDelegateHandle& Handle)
{
	if (Host)
	{
		if (Handle.IsValid())
		{
			Host->OnWeaponRuntimeInvalidated.Remove(Handle);
		}
	}
	Handle.Reset();
}

void UnbindStateTagDelegates(UAbilitySystemComponent* ASC,
	TMap<FGameplayTag, FDelegateHandle>& Handles)
{
	if (ASC)
	{
		for (const TPair<FGameplayTag, FDelegateHandle>& Pair : Handles)
		{
			ASC->RegisterGameplayTagEvent(Pair.Key,
				EGameplayTagEventType::NewOrRemoved).Remove(Pair.Value);
		}
	}
	Handles.Reset();
}

void RestoreMovementModeFromFloor(ACharacter& Character,
	UCharacterMovementComponent& Movement)
{
	FFindFloorResult Floor;
	Movement.FindFloor(Character.GetActorLocation(), Floor, false);
	const bool bOnFloor = Floor.IsWalkableFloor()
		&& Floor.FloorDist <= UCharacterMovementComponent::MAX_FLOOR_DIST;
	Movement.SetMovementMode(bOnFloor ? MOVE_Walking : MOVE_Falling);
}
}
