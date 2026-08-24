// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MHGZGameplayAbility.h"
#include "MHGZSheatheAbility.generated.h"

class ACharacter;
class UAbilityTask_PlayMontageAndWait;
class UAnimMontage;

/**
 * Snapshot-driven general sheathe action. It is granted as a core ability and
 * never enters a weapon combo table. Idle/Walk is frozen at activation; the
 * authoritative pose changes only at the exact SheatheCommit montage notify.
 */
UCLASS(BlueprintType, Blueprintable)
class MHGZ_API UMHGZSheatheAbility : public UMHGZGameplayAbility
{
	GENERATED_BODY()

public:
	UMHGZSheatheAbility();

	/** Montage containing the final Idle and Walk sheathe sections. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sheathe")
	TObjectPtr<UAnimMontage> SheatheMontage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sheathe")
	FName IdleSectionName = FName(TEXT("Idle"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sheathe")
	FName WalkSectionName = FName(TEXT("Walk"));

	/** Uses only the frozen input snapshot; live movement is never resampled. */
	FName SelectSectionName(const FWeaponInputSnapshot& Input) const;

	/** Exact Montage-instance callback used by AnimNotify_SheatheCommit. */
	bool CommitSheathe(const FWeaponActionToken& ActionToken);

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
	virtual bool ValidateSheatheMontageDependencies() const;

	/** Test seam for the asynchronous montage boundary; production owns the task. */
	virtual bool StartSheatheMontage(ACharacter& Character, UAnimMontage* Montage,
		FName StartSection);

	UFUNCTION()
	void OnMontageCompleted();

	UFUNCTION()
	void OnMontageInterrupted();

private:
	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> MontageTask;

	UPROPERTY()
	TObjectPtr<UAnimMontage> ActiveSheatheMontage;

	FName ActiveSectionName = NAME_None;
	FWeaponOwnedTagToken IdleBlockMovementToken;
	FWeaponOwnedTagToken SheathingToken;
	bool bUsingWalkSection = false;
	bool bSheatheCommitted = false;
	bool bOwnsMontageRootMotion = false;
	bool bEndingSheathe = false;

	void ReleaseMontageRootMotionOwnership();
};
