// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MHGZInsectGlaiveAbility.h"
#include "MHGZDrawAttackAbility.generated.h"

/**
 * Shared native base for all insect-glaive draw actions. The authoritative
 * Sheathed -> Unsheathed transition is deferred to AnimNotify_DrawCommit;
 * activation, montage start, or interruption before that notify never changes
 * the weapon pose.
 */
UCLASS(BlueprintType, Blueprintable, Abstract)
class MHGZ_API UMHGZDrawAttackAbility : public UMHGZInsectGlaiveAbility
{
	GENERATED_BODY()

public:
	UMHGZDrawAttackAbility();

	/** Exact Montage-instance callback used by AnimNotify_DrawCommit. */
	bool CommitDraw(const FWeaponActionToken& ActionToken);

	virtual bool CanActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags,
		const FGameplayTagContainer* TargetTags,
		FGameplayTagContainer* OptionalRelevantTags) const override;

	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

protected:
	virtual bool ValidateActionDependencies() const override;
	virtual bool PrepareAttackMontage() override;

	/** Derived draw actions may perform their post-pose commit work exactly once. */
	virtual void OnDrawCommitted() {}

	bool AcquireDrawMontageRootMotion();
	void ReleaseDrawMontageRootMotion();

private:
	bool bDrawCommitted = false;
	bool bOwnsMontageRootMotion = false;
};
