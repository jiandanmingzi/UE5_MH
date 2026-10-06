// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActionSystem/MHGZInsectGlaiveAbility.h"
#include "WeaponRuntime/MHGZWeaponRuntimeTypes.h"
#include "MHGZDescendingThrustAbility.generated.h"

class AActor;
class UAbilityTask_MHGZPlayMontageAndWait;
class UAbilityTask_MHGZWeaponMovement;
class UAnimInstance;
class UAnimMontage;
class UCharacterMovementComponent;
class URes_InsectGlaive;
class USkeletalMeshComponent;

/** The lifecycle phases owned by the Descending Thrust GA. */
UENUM(BlueprintType)
enum class EDescendingThrustPhase : uint8
{
	Inactive,
	AirStart,
	FallingLoop,
	GroundOver,
	Ended
};

/** Authored point events carried by the resident Descending Thrust montages. */
UENUM(BlueprintType)
enum class EDescendingThrustMontageEvent : uint8
{
	EnterFallingLoop,
	Correction3
};

/** Read-only runtime evidence for the existing opt-in telemetry sampler. */
struct FMHGZDescendingThrustTelemetrySnapshot
{
	bool bAbilityActive = false;
	uint32 ActivationSequenceID = 0;
	EDescendingThrustPhase Phase = EDescendingThrustPhase::Inactive;
	FWeaponActionToken ActionToken;
	FGameplayAbilitySpecHandle AbilityConsumer;

	FGameplayTag InputTag;
	FGameplayTag InputSourceControlTag;
	uint32 InputSequenceID = 0;
	float PhysicalPressDurationSeconds = 0.0f;
	bool bInputReleaseFallback = false;
	FGameplayTag PriorAerialSource;
	FGameplayTag PublishedAerialSource;

	FName CurrentMontageSection = NAME_None;
	FString MainMontagePath;
	FString LandingMontagePath;
	int32 MainMontageInstanceID = INDEX_NONE;
	int32 LandingMontageInstanceID = INDEX_NONE;
	float ActionClockSeconds = 0.0f;
	uint64 MovementLease = 0;
	bool bRecordedTrajectoryActive = false;
	bool bActionMovementOwned = false;
	bool bLandingRootMotionOwned = false;
	bool bLandingRootMotionModeCaptured = false;
	int32 SavedAnimRootMotionMode = INDEX_NONE;
	int32 CurrentAnimRootMotionMode = INDEX_NONE;

	uint64 CorrectionFrameNumbers[3] = { 0, 0, 0 };
	bool bCorrectionSnapshotValid[3] = { false, false, false };
	bool bCorrectionApplied[3] = { false, false, false };
	bool bCorrectionConsumed[3] = { false, false, false };
	float CorrectionAngles[3] = { 0.0f, 0.0f, 0.0f };
	FVector2D CorrectionRawMoveInputs[3] = {
		FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector };
	FVector CorrectionTargets[3] = {
		FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector };

	FVector LockedPlanarVelocity = FVector::ZeroVector;
	float FrozenDanceDamageMultiplier = 1.0f;
	FGuid SegmentDamageInstanceIDs[4] = {
		FGuid(), FGuid(), FGuid(), FGuid() };
	int32 SuccessfulTargetCounts[4] = { 0, 0, 0, 0 };
	int32 LastSuccessfulSegmentIndex = INDEX_NONE;
};

/**
 * 急袭突刺（M2）原生 Ability。
 *
 * 空中段只使用 AttackMontage 的 Start/FallingLoop；真实可行走触地后，
 * 仅使用 LandingMontage 的 Over。位移、Montage 实例、Root Motion、方向修正、
 * 命中额度和 ActionToken 均由同一次激活贯穿两段表现。
 */
UCLASS(BlueprintType, Blueprintable)
class MHGZ_API UMHGZDescendingThrustAbility : public UMHGZInsectGlaiveAbility
{
	GENERATED_BODY()

public:
	UMHGZDescendingThrustAbility();

	/** True only when the sampled raw stick magnitude is strictly above 0.50. */
	static bool IsDirectionalBoostInputAboveThreshold(
		const FVector2D& RawMoveInput);

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

	UFUNCTION(BlueprintPure, Category = "MHGZ|DescendingThrust")
	EDescendingThrustPhase GetDescendingThrustPhase() const { return Phase; }

	UFUNCTION(BlueprintPure, Category = "MHGZ|DescendingThrust")
	FVector GetLockedPlanarVelocity() const { return LockedPlanarVelocity; }

	/** Exact resolver target for UAnimNotify_DescendingThrustEvent. */
	void HandleMontageEvent(const FWeaponActionToken& ActionToken,
		int32 MontageInstanceID, EDescendingThrustMontageEvent Event);

	/** Read-only snapshot; this does not publish AnimBP CSV rows. */
	void GetRuntimeTelemetrySnapshot(
		FMHGZDescendingThrustTelemetrySnapshot& OutSnapshot) const;

protected:
	virtual bool ValidateActionDependencies() const override;

	virtual FGameplayEffectSpecHandle MakeDamageSpec(
		const FHitResult& Hit, int32 SegmentIndex) override;

	virtual bool PrepareAttackMontage() override;
	virtual bool StartAttackMontage(ACharacter& Character,
		UAnimMontage* Montage, FName StartSection) override;
	virtual void ApplyActivationDirectionCorrection() override;
	virtual void HandleSuccessfulAttackDamage(const FHitResult& Hit,
		int32 SegmentIndex) override;
	virtual bool CanApplyCollisionHit(const FHitResult& Hit,
		int32 SegmentIndex) const override;
	virtual void OnMontageCompleted() override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(
		FDataValidationContext& Context) const override;
#endif

private:
	// Configuration and input gates.
	bool HasRequiredSections(const UAnimMontage* Montage) const;
	bool HasRequiredLandingMontage() const;
	bool HasRequiredMontageEvents() const;
	bool HasUsableTrajectoryProfile() const;
	const URes_InsectGlaive* GetUsableResource() const;
	bool IsReleaseFallbackInput(const FWeaponInputSnapshot& Input) const;
	bool IsLiveAerialEntry() const;

	// Entry snapshot, locking and the three one-shot correction ordinals.
	bool CaptureFirstSnapshotAndLockVelocity();
	bool ConsumeCorrectionFromPreviousFrame(int32 CorrectionOrdinal,
		float MaxCorrectionAngleDegrees, float CorrectionDuration,
		bool& bOutSnapshotValid, bool& bOutApplied,
		bool bApplyDirectionCorrectionImmediately = true);
	void ApplyFirstCorrectionAfterMovementStarts();
	bool ReadPreviousFrameSnapshot(FActionMoveInputFrameSnapshot& OutSnapshot) const;
	bool IsUsableMoveSnapshot(const FActionMoveInputFrameSnapshot& Snapshot) const;

	// Movement and landing transaction.
	FWeaponMovementRequest MakeMovementRequest() const;
	bool StartRecordedTrajectoryMovement();
	bool BeginLandingSwitch(const FHitResult& Hit);
	bool AcquireLandingRootMotion();
	void ReleaseLandingRootMotion();
	bool PlayLandingMontage();
	void StartWatchdog();
	void ClearWatchdog();

	// Damage identity and lifecycle.
	void GenerateSegmentDamageInstanceIDs();
	void RecordDamagedTarget(int32 SegmentIndex, const AActor* Target);
	bool HasDamagedTarget(int32 SegmentIndex, const AActor* Target) const;
	void CompleteDescendingThrust();
	void CancelDescendingThrust(EWeaponActionEndReason EndReason,
		const TCHAR* Diagnostic);
	bool IsCurrentAction(const FWeaponActionToken& ActionToken) const;

	void BindLifecycleGuards();
	void UnbindLifecycleGuards();
	bool IsLiveContinuationStateValid() const;

	void HandleRuntimeInvalidated(const FWeaponRuntimeToken& InvalidatedToken);
	void HandleDisqualifyingStateTagChanged(FGameplayTag Tag, int32 NewCount);
	void HandleWatchdogExpired();
	void ResetActivationState();

	UFUNCTION()
	void HandleMainMontageCompleted();

	UFUNCTION()
	void HandleMainMontageInterrupted();

	UFUNCTION()
	void HandleLandingMontageCompleted();

	UFUNCTION()
	void HandleLandingMontageInterrupted();

	UFUNCTION()
	void HandleMovementFinished(const FWeaponMovementResult& Result);

	UPROPERTY(Transient)
	TObjectPtr<UAbilityTask_MHGZPlayMontageAndWait> MainMontageTask;

	UPROPERTY(Transient)
	TObjectPtr<UAbilityTask_MHGZPlayMontageAndWait> LandingMontageTask;

	UPROPERTY(Transient)
	TObjectPtr<UAbilityTask_MHGZWeaponMovement> MovementTask;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveMainMontage;

	/** Air montage must contain Start -> FallingLoop and FallingLoop -> itself. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "DescendingThrust|Montage Sections",
		meta = (AllowPrivateAccess = "true"))
	FName StartSectionName = TEXT("Start");

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "DescendingThrust|Montage Sections",
		meta = (AllowPrivateAccess = "true"))
	FName FallingLoopSectionName = TEXT("FallingLoop");

	/** Resident ground presentation; runtime montage construction is forbidden. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "DescendingThrust|Montage",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAnimMontage> LandingMontage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "DescendingThrust|Montage Sections",
		meta = (AllowPrivateAccess = "true"))
	FName LandingSectionName = TEXT("Over");

	FWeaponActionToken DescendingActionToken;
	FWeaponActionToken MovementActionToken;
	FWeaponInputSnapshot ActivationInputSnapshot;
	FGameplayTag PriorAerialSource;

	EDescendingThrustPhase Phase = EDescendingThrustPhase::Inactive;
	FName CurrentMontageSection = NAME_None;
	FString MainMontagePath;
	FString LandingMontagePath;
	int32 MainMontageInstanceID = INDEX_NONE;
	int32 LandingMontageInstanceID = INDEX_NONE;

	FVector LockedPlanarVelocity = FVector::ZeroVector;
	float FrozenDanceDamageMultiplier = 1.0f;
	FGuid SegmentDamageInstanceIDs[4] = {
		FGuid(), FGuid(), FGuid(), FGuid() };
	TArray<TSet<TWeakObjectPtr<const AActor>>> DamagedTargetsPerSegment;
	int32 LastSuccessfulSegmentIndex = INDEX_NONE;

	uint64 CorrectionFrameNumbers[3] = { 0, 0, 0 };
	bool bCorrectionSnapshotValid[3] = { false, false, false };
	bool bCorrectionApplied[3] = { false, false, false };
	bool bCorrectionConsumed[3] = { false, false, false };
	float CorrectionAngles[3] = { 0.0f, 0.0f, 0.0f };
	FVector2D CorrectionRawMoveInputs[3] = {
		FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector };
	FVector CorrectionTargets[3] = {
		FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector };

	FTimerHandle WatchdogTimer;
	FDelegateHandle RuntimeInvalidationDelegateHandle;
	TMap<FGameplayTag, FDelegateHandle> DisqualifyingStateTagDelegateHandles;

	bool bFallingLoopEntered = false;
	bool bLandingLatched = false;
	bool bSwitchingToLanding = false;
	bool bLandingMontageActive = false;
	bool bLandingMontageFinished = false;
	bool bEndingDescendingThrust = false;

	TWeakObjectPtr<UAnimInstance> LandingRootMotionAnimInstance;
	uint8 SavedLandingRootMotionMode = 0;
	bool bLandingRootMotionModeCaptured = false;
};
