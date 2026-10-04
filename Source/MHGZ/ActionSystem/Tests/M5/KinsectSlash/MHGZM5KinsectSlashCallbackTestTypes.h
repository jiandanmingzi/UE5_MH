// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AttributeSystem/Res_InsectGlaive.h"
#include "MHGZM5KinsectSlashCallbackTestTypes.generated.h"

class UAbilitySystemComponent;
class UMHGZKinsectSlashAbility;
class UMHGZWeaponRuntimeHostComponent;
class USkeletalMeshComponent;

/** Observes the real resource broadcast and optionally cancels its owning GA inline. */
UCLASS()
class UMHGZM5KinsectSlashDanceCancellationObserver : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<UAbilitySystemComponent> ASC;

	UPROPERTY()
	TObjectPtr<UMHGZKinsectSlashAbility> Ability;

	UPROPERTY()
	TObjectPtr<UMHGZWeaponRuntimeHostComponent> Host;

	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> Mesh;

	bool bCancelOnDanceStateChanged = true;
	int32 KinsectSlashBroadcastCount = 0;
	int32 StacksAtDanceBroadcast = 0;
	int32 DanceMontageInstanceID = INDEX_NONE;
	bool bObservedActiveAbility = false;
	bool bObservedExactRegisteredDanceMontage = false;
	bool bAppliedHitstun = false;
	bool bCancelledSynchronously = false;

	UFUNCTION()
	void HandleDanceStateChanged(int32 Stacks, EIGDanceSource Source);
};
