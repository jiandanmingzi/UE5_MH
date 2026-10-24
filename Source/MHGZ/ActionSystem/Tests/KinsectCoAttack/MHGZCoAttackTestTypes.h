// Copyright MHGZ Project. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "ActionSystem/MHGZInsectGlaiveAbility.h"
#include "MHGZCoAttackTestTypes.generated.h"

/** Transient test GA: production commit/window/GE paths, no Content or animation playback writes. */
UCLASS()
class UMHGZCoAttackTestAbility : public UMHGZInsectGlaiveAbility
{
	GENERATED_BODY()
public:
	UMHGZCoAttackTestAbility();
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	int32 OpenCount = 0;
	int32 CloseCount = 0;
	bool bCloseOnOpen = false;
protected:
	virtual bool ValidateActionDependencies() const override { return true; }
	virtual bool StartAttackMontage(ACharacter&, UAnimMontage*, FName) override { return true; }
	virtual void OnAttackCollisionWindowOpened(int32 Segment, uint64 Occurrence) override;
	virtual void OnAttackCollisionWindowRetired(int32 Segment, uint64 Occurrence) override;
};

UCLASS()
class UMHGZCoAttackNonCoopTestAbility : public UMHGZCoAttackTestAbility
{
	GENERATED_BODY()
public:
	UMHGZCoAttackNonCoopTestAbility() { CoAttack.bEnabled = false; }
};

UCLASS()
class UMHGZCoAttackRejectedTestAbility : public UMHGZCoAttackTestAbility
{
	GENERATED_BODY()
public:
	virtual bool CommitAbility(FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo*,
		FGameplayAbilityActivationInfo, FGameplayTagContainer* = nullptr) override { return false; }
};

UCLASS()
class UMHGZCoAttackOtherAnchorTestAbility : public UMHGZCoAttackTestAbility
{
	GENERATED_BODY()
public:
	UMHGZCoAttackOtherAnchorTestAbility() { CoAttack.LocalOffset = FVector(1000, 0, 0); }
};
