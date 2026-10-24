#include "ActionSystem/Tests/Shared/MHGZIGActionFixtures.h"
// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "ActionSystem/MHGZKinsectSlashAbility.h"
#include "InsectGlaive/InsectGlaiveCombatConfig.h"
#include "MHGZCharacter.h"
#include "MHGZM5KinsectSlashDamageTestTypes.generated.h"

class AActor;
class UAbilitySystemComponent;
class UMHGZAbilitySystemComponent;
class UMHGZAttributeSet;
class UMHGZEquipmentComponent;
class UMHGZMonsterHitzoneComponent;
class AMHGZPlayerState;
class UMHGZWeaponRuntimeHostComponent;
class URes_InsectGlaive;
class UWeaponRuntimeDefinition;

/** Concrete spawnable pawn for the transient damage and resource harness. */
UCLASS()
class AMHGZM5KinsectSlashDamageTestCharacter : public AMHGZCharacter
{
	GENERATED_BODY()
};

/** Transient red extract GE with the production extract tag shape. */
UCLASS()
class UMHGZM5KinsectSlashDamageRedExtractGE : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UMHGZM5KinsectSlashDamageRedExtractGE();
};

/** Minimal opt-in fall profile required by the production aerial GA activation path. */
UCLASS()
class UMHGZM5KinsectSlashDamageTestCombatConfig : public UInsectGlaiveCombatConfig
{
	GENERATED_BODY()

public:
	virtual bool ResolveAerialFallingPhysics(bool bEnhancedVariant,
		float& OutGravityScale, float& OutBrakingDecelerationFalling) const override;
};

/**
 * Uses the production Kinsect Slash state and damage code while replacing only
 * montage playback and asset-only activation checks unavailable to a transient pawn.
 */
UCLASS()
class UMHGZM5KinsectSlashDamageTestAbility : public UMHGZKinsectSlashAbility
{
	GENERATED_BODY()

public:
	UMHGZM5KinsectSlashDamageTestAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;
	void ConfigureForTest();
	void ResetTestConfiguration();

protected:
	virtual bool ValidateActionDependencies() const override;
	virtual bool PrepareAttackMontage() override;
	virtual bool StartAttackMontage(ACharacter& Character, UAnimMontage* Montage,
		FName StartSection) override;
};

/** Transient world containing an equipped IG resource, the real player ASC, and a damage target. */
struct FMHGZM5KinsectSlashDamageTestHarness
{
	UWorld* World = nullptr;
	AMHGZM5KinsectSlashDamageTestCharacter* Character = nullptr;
	AMHGZPlayerState* PlayerState = nullptr;
	UMHGZAbilitySystemComponent* ASC = nullptr;
	UMHGZWeaponRuntimeHostComponent* Host = nullptr;
	URes_InsectGlaive* Resource = nullptr;
	UMHGZM5KinsectSlashDamageTestCombatConfig* CombatConfig = nullptr;
	IGTest::FScopedActionFixtures ActionFixtures;
	UMHGZEquipmentComponent* Equipment = nullptr;
	UMHGZAttributeSet* SourceAttributes = nullptr;
	AActor* TargetActor = nullptr;
	UAbilitySystemComponent* TargetASC = nullptr;
	UMHGZAttributeSet* TargetAttributes = nullptr;
	UMHGZMonsterHitzoneComponent* Hitzone = nullptr;
	FGameplayAbilitySpecHandle AbilityHandle;
	UMHGZM5KinsectSlashDamageTestAbility* Ability = nullptr;

	bool Build();
	bool ActivateAbility(FGameplayTag ResolvedInputTag = FGameplayTag());
	void Teardown();
};
