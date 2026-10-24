// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActionSystem/MHGZInsectGlaiveAbility.h"
#include "InsectGlaive/IGPowderTypes.h"
#include "MHGZPowderVortexAbility.generated.h"

/** Grounded LTYB action; Resource owns the aimed Kinsect flight and post-Montage vortex transaction. */
UCLASS(BlueprintType, Blueprintable)
class MHGZ_API UMHGZPowderVortexAbility : public UMHGZInsectGlaiveAbility
{
	GENERATED_BODY()

public:
	UMHGZPowderVortexAbility();

	/** 聚爆专属数值与表现；激活时冻结，提交后的事务不再读取Combat旧字段。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Powder Vortex", meta = (ShowOnlyInnerProperties))
	FIGPowderVortexSettings PowderVortex;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility, bool bWasCancelled) override;

	/** The sole Montage point event submits the activation-time aim snapshot to Resource. */
	virtual bool HandlePowderGatherBegin(
		const FWeaponActionToken& ActionToken) override;

protected:
	virtual bool ValidateActionDependencies() const override;
	virtual bool PrepareAttackMontage() override;

	/** Shared read-only gate for the resource, Kinsect, config and input-time aim contract. */
	bool ValidatePowderVortexRuntimeDependencies() const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

private:
	UPROPERTY(Transient)
	FIGPowderVortexSettings CommittedPowderVortex;
	bool bGatherNotifyHandled = false;
	bool bKinsectCommandSubmitted = false;
};
