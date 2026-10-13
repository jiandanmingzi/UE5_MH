// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActionSystem/MHGZInsectGlaiveAbility.h"
#include "WeaponRuntime/MHGZWeaponRuntimeTypes.h"
#include "MHGZKinsectSlashAbility.generated.h"

class UAbilityTask_MHGZKinsectSlashProbe;
class UAbilityTask_MHGZPlayMontageAndWait;
class UAbilityTask_MHGZWeaponMovement;
class UAbilitySystemComponent;
class UAnimMontage;
class UCharacterMovementComponent;
class USkeletalMeshComponent;
class UMHGZWeaponRuntimeHostComponent;

UENUM(BlueprintType)
enum class EKinsectSlashAimSource : uint8
{
	None,
	WeaponB,
	KinsectAim
};

UENUM(BlueprintType)
enum class EKinsectSlashMontageEvent : uint8
{
	AimLock,
	DashBegin,
	// Deprecated no-op values retained at their original numeric positions.
	LateHitOpen,
	LateHitClose,
	HoldMissPose,
	DanceBegin
};

UENUM(BlueprintType)
enum class EKinsectSlashPhase : uint8
{
	Inactive,
	Hover,
	Dash,
	MissFall,
	DancePending,
	DanceFromDash,
	DanceFromMiss,
	Ended
};

/** Both 189 hit entries share the captured trajectory; each remains editable. */
USTRUCT(BlueprintType)
struct FKinsectSlashDanceEntryTuning
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement")
	bool bUseCapturedTrajectory = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement",
		meta = (ClampMin = "0.001", ForceUnits = "s"))
	float PrepDuration = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement",
		meta = (ClampMin = "0.001", ForceUnits = "s", EditCondition = "bUseCapturedTrajectory"))
	float FlightDuration = 1.1833333f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement",
		meta = (ClampMin = "0.001", EditCondition = "bUseCapturedTrajectory"))
	float LaunchUpSpeed = 1080.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement",
		meta = (ClampMin = "0.001", EditCondition = "bUseCapturedTrajectory"))
	float LaunchBackwardSpeed = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement",
		meta = (ClampMin = "0.001", EditCondition = "bUseCapturedTrajectory"))
	float GravityMagnitude = 1800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement",
		meta = (ClampMin = "0.001", EditCondition = "bUseCapturedTrajectory"))
	float BackwardAcceleration = 360.0f;

	// Keep the original property names so serialized legacy tunings still load.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Legacy",
		meta = (DeprecatedProperty, DeprecationMessage = "Use the captured 189 trajectory.",
			EditCondition = "!bUseCapturedTrajectory"))
	EBallisticParameterMode ParameterMode = EBallisticParameterMode::ApexHeightAndDuration;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Legacy",
		meta = (ClampMin = "0.0", DeprecatedProperty,
			DeprecationMessage = "Use LaunchUpSpeed and GravityMagnitude.",
			EditCondition = "!bUseCapturedTrajectory"))
	float ApexHeight = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Legacy",
		meta = (ClampMin = "0.0", DeprecatedProperty, DeprecationMessage = "Use FlightDuration.",
			EditCondition = "!bUseCapturedTrajectory"))
	float Duration = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Legacy",
		meta = (ClampMin = "0.0", DeprecatedProperty,
			DeprecationMessage = "Use LaunchBackwardSpeed and BackwardAcceleration.",
			EditCondition = "!bUseCapturedTrajectory"))
	float HorizontalDistance = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Legacy",
		meta = (DeprecatedProperty, DeprecationMessage = "Use the captured 189 trajectory.",
			EditCondition = "!bUseCapturedTrajectory"))
	FVector ExplicitLaunchVelocity = FVector::ZeroVector;

	bool IsValid() const;
	FWeaponMovementRequest MakeRequest(const FWeaponActionToken& ActionToken,
		const FVector& LaunchDirection, UObject* CurveOuter = nullptr) const;
};

/**
 * Production GA for both aerial B and RT+B Insect Glaive Kinsect Slash paths.
 * One committed Action owns 186 -> 187 -> 188 or 189, the real temporarily
 * attached Kinsect, three AttackCollision windows, movement tasks, aerial
 * budget, damage/extract effects, and all notify callbacks.
 */
UCLASS(BlueprintType, Blueprintable)
class MHGZ_API UMHGZKinsectSlashAbility : public UMHGZInsectGlaiveAbility
{
	GENERATED_BODY()

public:
	UMHGZKinsectSlashAbility();

	virtual bool CanActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags,
		const FGameplayTagContainer* TargetTags,
		FGameplayTagContainer* OptionalRelevantTags) const override;

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility, bool bWasCancelled) override;

	virtual void HandleLanded(const FHitResult& Hit) override;

	/** Called only by an exact Montage-instance notify resolver. */
	bool HandleMontageEvent(const FWeaponActionToken& ActionToken,
		int32 MontageInstanceID, EKinsectSlashMontageEvent Event);

	/** Exact overload used by UAnimNotify_IG_AerialHandoff for this GA. */
	bool NotifyAerialHandoff(const FWeaponActionToken& ActionToken,
		int32 MontageInstanceID);
	using UMHGZInsectGlaiveAbility::NotifyAerialHandoff;

	UFUNCTION(BlueprintPure, Category = "MHGZ|KinsectSlash")
	EKinsectSlashAimSource GetAimSource() const { return AimSource; }

	UFUNCTION(BlueprintPure, Category = "MHGZ|KinsectSlash")
	EKinsectSlashPhase GetSlashPhase() const { return Phase; }

	UFUNCTION(BlueprintPure, Category = "MHGZ|KinsectSlash")
	FVector GetLockedLaunchDirection() const { return LaunchDirection; }

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect Slash|Montage Sections")
	FName HoverSectionName = TEXT("Hover186");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect Slash|Montage Sections")
	FName DashSectionName = TEXT("Dash187");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect Slash|Montage Sections")
	FName MissFallSectionName = TEXT("Miss188");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect Slash|Montage")
	TObjectPtr<UAnimMontage> DanceMontage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect Slash|Montage Sections")
	FName DanceSectionName = TEXT("HitDance189");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect Slash|Montage",
		meta = (ClampMin = "0.0", ForceUnits = "s"))
	float DanceMontageBlendInTime = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect Slash|Visual Pitch",
		meta = (ClampMin = "0.0", ForceUnits = "s"))
	float VisualPitchBlendInTime = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect Slash|Visual Pitch",
		meta = (ClampMin = "0.0", ForceUnits = "s"))
	float VisualPitchBlendOutTime = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect Slash|Movement")
	FKinsectSlashDanceEntryTuning DanceFromDashTuning;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect Slash|Movement")
	FKinsectSlashDanceEntryTuning DanceFromMissTuning;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect Slash|Movement",
		meta = (ClampMin = "0.001"))
	float MissGravityMagnitude = 3096.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect Slash|Debug",
		meta = (DeprecatedProperty, DeprecationMessage = "Use AttackSegments[].Collision.bDrawDebug."))
	bool bDrawProbeDebug = false;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

protected:
	virtual bool ValidateActionDependencies() const override;
	virtual FGameplayEffectSpecHandle MakeDamageSpec(
		const FHitResult& Hit, int32 SegmentIndex) override;
	virtual bool PrepareAttackMontage() override;
	virtual bool StartAttackMontage(ACharacter& Character, UAnimMontage* Montage,
		FName StartSection) override;
	virtual void ApplyActivationDirectionCorrection() override;
	virtual void HandleSuccessfulAttackDamage(const FHitResult& Hit,
		int32 SegmentIndex) override;
	virtual bool CanApplyCollisionHit(const FHitResult& Hit,
		int32 SegmentIndex) const override;
	virtual bool ShouldConsumeCollisionOnDamageFailure(int32 SegmentIndex) const override;
	virtual void OnAttackCollisionWindowClosed(int32 SegmentIndex) override;
	virtual void OnMontageCompleted() override;

private:
	#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
	friend class FMHGZM5KinsectSlashMontageCompletionRaceTest;
	friend class FMHGZM5KinsectSlashDamageResourceTest;
	friend class FMHGZM5KinsectSlashProbeSweepTest;
	friend class FMHGZM5KinsectSlashLateHitOpenBoundaryTest;
	friend class FMHGZM5KinsectSlashLifecycleCancellationTest;
	friend class FMHGZM5KinsectSlashDancePreparationGuardTest;
	friend class FMHGZM5KinsectSlashReentrantDanceCommitTest;
	#endif

	bool IsLiveAerialSlashStateValid(const UAbilitySystemComponent* ASC,
		const UMHGZWeaponRuntimeHostComponent* Host) const;
	bool ShouldDeferMontageCompletionForPendingDance() const;
	bool IsCurrentAction(const FWeaponActionToken& ActionToken) const;
	bool IsCurrentMontage(const FWeaponActionToken& ActionToken,
		int32 MontageInstanceID) const;
	static bool IsLateHitOpenNotifyPhaseAllowed(EKinsectSlashPhase CurrentPhase,
		FName CurrentSection, FName MissSection);
	void BindLifecycleGuards();
	void UnbindLifecycleGuards();
	void HandleRuntimeInvalidated(const FWeaponRuntimeToken& InvalidatedToken);
	void HandleDisqualifyingStateTagChanged(FGameplayTag Tag, int32 NewCount);
	bool IsLiveContinuationStateValid() const;
	bool HasRequiredSections(const UAnimMontage* Montage) const;
	bool HasRequiredDanceMontage() const;
	USkeletalMeshComponent* FindKinsectSlashWeaponMesh(FName RequiredSocket) const;
	bool IsKinsectActionAttachmentCurrent() const;
	void ReleaseKinsectActionAttachment();
	bool LockAimAndDirection();
	void ReleaseActionVisualPitch(EActionVisualPitchReleaseMode ReleaseMode);
	bool StartDashMovement();
	bool StartHoverMovement();
	bool StartDancePreparationMovement();
	bool StartDanceMovement(bool bFromMiss);
	void HandleQueuedDanceTransition(FWeaponActionToken ActionToken,
		int32 MontageInstanceID);
	void HandleDancePreparationFinished(FWeaponActionToken ActionToken,
		int32 MontageInstanceID);
	void ClearDanceTimers();
	void HandleMontageCompleted();
	bool EnterMissFallPhase();
	void OnSlashMontageSectionChanged(UAnimMontage* Montage,
		FName SectionName, bool bLooped);
	void FinalizeHostLanding(bool bPlayLandingPresentation);
	void CompleteSlashAction();
	void CancelSlashAction(EWeaponActionEndReason EndReason,
		const TCHAR* Diagnostic);
	void ResetActivationState();
	FVector ResolveLaunchYawDirection() const;
	FVector ResolveDashTerminalVelocity() const;
	FWeaponMovementRequest MakeDashMovementRequest() const;

	UFUNCTION()
	void HandleSlashMontageCompleted();

	UFUNCTION()
	void HandleSlashMontageInterrupted();

	UFUNCTION()
	void HandleDanceMontageCompleted();

	UFUNCTION()
	void HandleDanceMontageInterrupted();

	UFUNCTION()
	void HandleMovementFinished(const FWeaponMovementResult& Result);

	UFUNCTION()
	void HandleProbeHitzoneContact(FWeaponActionToken ActionToken, FHitResult Hit);

	FTimerHandle QueuedDanceTransitionTimer;
	FTimerHandle DancePreparationTimer;
	FDelegateHandle RuntimeInvalidationDelegateHandle;
	TMap<FGameplayTag, FDelegateHandle> DisqualifyingStateTagDelegateHandles;

	UPROPERTY()
	TObjectPtr<UAbilityTask_MHGZPlayMontageAndWait> SlashMontageTask;

	UPROPERTY()
	TObjectPtr<UAbilityTask_MHGZPlayMontageAndWait> DanceMontageTask;

	UPROPERTY()
	TObjectPtr<UAbilityTask_MHGZWeaponMovement> MovementTask;

	UPROPERTY()
	TObjectPtr<UAbilityTask_MHGZWeaponMovement> HoverMovementTask;

	UPROPERTY()
	TObjectPtr<UAbilityTask_MHGZKinsectSlashProbe> ProbeTask;

	UPROPERTY()
	TObjectPtr<UAnimMontage> ActiveSlashMontage;

	FWeaponActionToken SlashActionToken;
	FWeaponActionToken MontageActionToken;
	FWeaponActionToken MovementActionToken;
	int32 SlashMontageInstanceID = INDEX_NONE;
	int32 VisualPitchSourceMontageInstanceID = INDEX_NONE;

	EKinsectSlashAimSource AimSource = EKinsectSlashAimSource::None;
	EKinsectSlashPhase Phase = EKinsectSlashPhase::Inactive;
	FVector LaunchDirection = FVector::ForwardVector;
	FVector DashTerminalVelocity = FVector::ZeroVector;
	FHitResult SuccessfulHit;
	FGuid KinsectSlashPowderHitInstanceID;
	FName SuccessfulHitSourceSection = NAME_None;
	float SuccessfulHitSourcePosition = 0.0f;
	int32 SuccessfulHitSourceMontageInstanceID = INDEX_NONE;
	bool bKinsectActionAttached = false;
	bool bAimLocked = false;
	bool bDashMovementFinished = false;
	bool bDanceMovementFinished = false;
	bool bMontageFinished = false;
	bool bPendingDanceTransition = false;
	bool bDamageResolveInProgress = false;
	bool bCurrentDamageApplied = false;
	bool bSuccessfulDamage = false;
	bool bExtractApplied = false;
	bool bDanceEntryCommitted = false;
	bool bDanceMontageActive = false;
	bool bMissPoseHeld = false;
	bool bEndingSlash = false;
	bool bEndingDashForDance = false;
	bool bMovementTaskIsDance = false;
	bool bLanded = false;
	bool PendingDanceFromMiss = false;
};
