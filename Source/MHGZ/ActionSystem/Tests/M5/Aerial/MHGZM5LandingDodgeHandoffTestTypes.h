// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActionSystem/MHGZKinsectPiercerAbility.h"
#include "ActionSystem/MHGZDescendingThrustAbility.h"
#include "ActionSystem/MHGZDodgeAbility.h"
#include "MHGZM5LandingDodgeHandoffTestTypes.generated.h"

/** Ground-only fixtures: skip aerial entry, retain production Over task/owner/end callbacks. */
UCLASS()
class UMHGZM5PiercerLandingHandoffTestAbility : public UMHGZKinsectPiercerAbility
{
	GENERATED_BODY()
public:
	UMHGZM5PiercerLandingHandoffTestAbility();
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
		const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const override;
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;
protected:
	virtual bool ValidateActionDependencies() const override { return true; }
};

UCLASS()
class UMHGZM5ThrustLandingHandoffTestAbility : public UMHGZDescendingThrustAbility
{
	GENERATED_BODY()
public:
	UMHGZM5ThrustLandingHandoffTestAbility();
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
		const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const override;
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;
protected:
	virtual bool ValidateActionDependencies() const override { return true; }
};

/** Real Dodge playback followed by an injected registration-stage failure. */
UCLASS()
class UMHGZM5FailedLandingDodgeTestAbility : public UMHGZDodgeAbility
{
	GENERATED_BODY()
public:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;
protected:
	virtual bool StartDodgeMontage(ACharacter& Character, UAnimMontage* Montage,
		FName StartSection) override;
};
