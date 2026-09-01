// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MHGZGameplayAbility.h"
#include "MHGZDodgeAbility.generated.h"

class UAbilityTask_PlayMontageAndWait;
class UAnimMontage;
class UCapsuleComponent;
class ACharacter;

UENUM(BlueprintType)
enum class EMHGZDodgeMovementPhase : uint8
{
	LockedRootMotion,
	SteeringRootMotion,
	MotionMatching
};

/** Snapshot-driven general dodge. It does not enter the weapon combo table. */
UCLASS(BlueprintType, Blueprintable)
class MHGZ_API UMHGZDodgeAbility : public UMHGZGameplayAbility
{
	GENERATED_BODY()

public:
	UMHGZDodgeAbility();

	/** Final forward-roll montage for the sheathed pose. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dodge|Sheathed")
	TSoftObjectPtr<UAnimMontage> SheathedDodgeMontage;

	/** Final forward-roll montage for the unsheathed pose. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dodge|Unsheathed")
	TSoftObjectPtr<UAnimMontage> UnsheathedDodgeMontage;

	/** Final left-roll montage. Only valid from the unsheathed pose. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dodge|Unsheathed")
	TSoftObjectPtr<UAnimMontage> UnsheathedLeftDodgeMontage;

	/** Final right-roll montage. Only valid from the unsheathed pose. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dodge|Unsheathed")
	TSoftObjectPtr<UAnimMontage> UnsheathedRightDodgeMontage;

	/** Final back-roll montage. Only valid from the unsheathed pose. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dodge|Unsheathed")
	TSoftObjectPtr<UAnimMontage> UnsheathedBackDodgeMontage;

	/** Legacy migration source. Runtime only reads Forward/None for forward rolls. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dodge|Legacy",
		meta = (DeprecatedProperty, DeprecationMessage = "Use SheathedDodgeMontage"))
	TMap<EDirectionalInput, TSoftObjectPtr<UAnimMontage>> SheathedDodgeMontages;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dodge|Legacy",
		meta = (DeprecatedProperty, DeprecationMessage = "Use UnsheathedDodgeMontage"))
	TMap<EDirectionalInput, TSoftObjectPtr<UAnimMontage>> UnsheathedDodgeMontages;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dodge|Sections")
	FName DodgeCoreSectionName = FName(TEXT("DodgeCore"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dodge|Sections")
	FName IdleExitSectionName = FName(TEXT("IdleExit"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dodge|Sections")
	FName MoveExitSectionName = FName(TEXT("MoveExit"));

	/**
	 * E4.2 migration switch. When enabled, Forward/None rolls (the only rolls
	 * allowed to enter MoveExit) acquire Montage root-motion ownership solely
	 * through AnimNotifyState_ActionRootMotionPhase. Directional unsheathed
	 * rolls retain the legacy whole-action owner.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dodge|Motion Matching",
		meta = (DisplayName = "Forward Dodge Uses Action Root Motion Phase"))
	bool bForwardDodgeUsesActionRootMotionPhase = false;

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

	/**
	 * Legacy exact-token helpers retained for tests and compatibility. Runtime
	 * Dodge exit selection uses this Ability's active-Montage SectionChanged
	 * callback instead of an AnimNotify registry lookup.
	 */
	bool DecideDodgeExit(const FWeaponActionToken& ActionToken);
	bool EnterMoveExit(const FWeaponActionToken& ActionToken);

	EMHGZDodgeMovementPhase GetMovementPhase() const { return MovementPhase; }
	FName GetChosenExitSection() const { return ChosenExitSection; }
	bool DoesActiveDodgeAllowMoveExit() const { return bActiveDodgeAllowsMoveExit; }

protected:
	struct FDodgeSelection
	{
		UAnimMontage* Montage = nullptr;
		bool bAllowMoveExit = false;
	};

	virtual bool ValidateActionDependencies() const override;
	virtual bool ValidateDodgeMontageDependencies() const;
	virtual bool StartDodgeMontage(ACharacter& Character, UAnimMontage* Montage,
		FName StartSection);
	/**
	 * Arms the deterministic DodgeCore -> IdleExit fallback after playback begins.
	 * This Ability's per-Montage SectionChanged callback redirects a forward roll
	 * with live input to MoveExit; directional rolls deliberately use the fallback.
	 */
	virtual bool ConfigureDodgeCoreFallbackExit();
	virtual bool HasLiveMovementInput() const;
	virtual bool JumpToDodgeSection(FName SectionName);
	virtual FDodgeSelection SelectDodgeSelection() const;
	static bool IsDodgeDirectionAllowedForPose(bool bSheathed,
		EDirectionalInput Direction);
	void OnDodgeMontageSectionChanged(UAnimMontage* Montage, FName SectionName,
		bool bLooped);
	void HandleDodgeSectionEntered(FName SectionName);

	UFUNCTION()
	void OnMontageCompleted();

	UFUNCTION()
	void OnMontageBlendOut();

	UFUNCTION()
	void OnMontageInterrupted();

	/** Normal-blend fallback for assets/tasks that do not subsequently emit OnCompleted. */
	void OnMontageEndFailsafeExpired();

private:
	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> MontageTask;

	UPROPERTY()
	TObjectPtr<UAnimMontage> ActiveDodgeMontage;

	TMap<FName, FWeaponOwnedTagToken> DodgeWindowTokens;
	TMap<TEnumAsByte<ECollisionChannel>, ECollisionResponse> CachedCollisionResponses;
	TWeakObjectPtr<UCapsuleComponent> DodgeCapsule;
	FWeaponOwnedTagToken DodgingToken;
	FWeaponOwnedTagToken BlockMovementToken;
	EMHGZDodgeMovementPhase MovementPhase = EMHGZDodgeMovementPhase::LockedRootMotion;
	FName ChosenExitSection = NAME_None;
	bool bActiveDodgeAllowsMoveExit = false;
	bool bOwnsMontageRootMotion = false;
	bool bPreparedAttackSupersede = false;
	bool bEndingDodge = false;
	FTimerHandle MontageEndFailsafeTimer;

	bool IsAttacking() const;
	void ArmMontageEndFailsafe();
	void ClearMontageEndFailsafe();
	void ReleaseMontageRootMotionOwnership();
	void CancelPreparedAttackSupersede();
	void CloseAllDodgeWindows();
	void RestoreDodgeCollisionResponses();
};
