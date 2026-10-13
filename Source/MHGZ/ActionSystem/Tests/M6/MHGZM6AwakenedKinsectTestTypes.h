// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActionSystem/AbilityTask_MHGZWeaponMovement.h"
#include "ActionSystem/MHGZAwakenedKinsectAttackAbility.h"
#include "ActionSystem/MHGZInsectGlaiveGroundAbilities.h"
#include "MHGZCharacter.h"
#include "MHGZM6AwakenedKinsectTestTypes.generated.h"

/** Concrete production-character shell for M6 CMC/Host lifecycle tests. */
UCLASS()
class AMHGZM6AwakenedKinsectTestCharacter : public AMHGZCharacter
{
	GENERATED_BODY()
};

/** Test-only seam: exercise the production M6 activation transaction without editor assets. */
UCLASS()
class UMHGZM6AwakenedKinsectAbilityTestDouble
	: public UMHGZAwakenedKinsectAttackAbility
{
	GENERATED_BODY()

public:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;
	bool ConfigureAttackMontageForTest(UAnimMontage* Montage)
	{
		AttackMontage = Montage;
		return AttackMontage != nullptr;
	}
	bool StartProductionAttackMontageForTest(ACharacter& Character,
		UAnimMontage* Montage, FName StartSection)
	{
		return UMHGZAwakenedKinsectAttackAbility::StartAttackMontage(
			Character, Montage, StartSection);
	}
	UAbilityTask_MHGZWeaponMovement* GetHunterFlightMovementTaskForTest() const
	{
		return UMHGZAwakenedKinsectAttackAbility::GetHunterFlightMovementTaskForTest();
	}
	UAbilityTask_MHGZActionDirectionCorrection* GetFacingCorrectionTaskForTest() const
	{
		return GetActiveDirectionCorrectionTaskForTest();
	}
	void ApplyActivationDirectionCorrectionForTest()
	{
		UMHGZAwakenedKinsectAttackAbility::ApplyActivationDirectionCorrection();
	}
	bool PrepareKinsectAttachmentForTest()
	{
		return UMHGZAwakenedKinsectAttackAbility::PrepareAttackMontage();
	}
	bool ApplyDamageForTest(const FHitResult& Hit, int32 SegmentIndex)
	{
		return ApplyDamage(Hit, SegmentIndex);
	}

protected:
	virtual bool ValidateActionDependencies() const override { return true; }
	virtual bool StartAttackMontage(ACharacter& Character, UAnimMontage* Montage,
		FName StartSection) override { return true; }
};

/** Only the animation dependency is replaced; real GE and mark callbacks are exercised. */
UCLASS()
class UMHGZM6FourHitMarkTestAbility : public UMHGZMarkSlashAbility
{
	GENERATED_BODY()
public:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;
	bool ApplyDamageForTest(const FHitResult& Hit, int32 SegmentIndex)
	{
		return ApplyDamage(Hit, SegmentIndex);
	}
protected:
	virtual bool ValidateActionDependencies() const override { return true; }
};
