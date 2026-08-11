// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MHGZGameplayAbility.h"
#include "MHGZDodgeAbility.generated.h"

class UAbilityTask_PlayMontageAndWait;
class UAnimMontage;
class UCapsuleComponent;

/** Snapshot-driven general dodge. It does not enter the weapon combo table. */
UCLASS(BlueprintType, Blueprintable)
class UMHGZDodgeAbility : public UMHGZGameplayAbility
{
	GENERATED_BODY()

public:
	UMHGZDodgeAbility();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dodge|Sheathed")
	TMap<EDirectionalInput, TSoftObjectPtr<UAnimMontage>> SheathedDodgeMontages;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dodge|Unsheathed")
	TMap<EDirectionalInput, TSoftObjectPtr<UAnimMontage>> UnsheathedDodgeMontages;

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

	bool BeginDodgeWindow(FName NotifyEventID);
	void EndDodgeWindow(FName NotifyEventID);

protected:
	virtual bool ValidateActionDependencies() const override;

private:
	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> MontageTask;

	UPROPERTY()
	TObjectPtr<UAnimMontage> ActiveDodgeMontage;

	TMap<FName, FWeaponOwnedTagToken> DodgeWindowTokens;
	TMap<TEnumAsByte<ECollisionChannel>, ECollisionResponse> CachedCollisionResponses;
	TWeakObjectPtr<UCapsuleComponent> DodgeCapsule;
	bool bEndingDodge = false;

	UAnimMontage* SelectDodgeMontage() const;
	void CloseAllDodgeWindows();
	void RestoreDodgeCollisionResponses();

	UFUNCTION()
	void OnMontageCompleted();

	UFUNCTION()
	void OnMontageInterrupted();
};
