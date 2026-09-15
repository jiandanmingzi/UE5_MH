// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MHGZInsectGlaiveAbility.h"
#include "MHGZBackVaultAbility.generated.h"

class UAbilityTask_MHGZPlayMontageAndWait;
class UAbilityTask_WaitDelay;
class UAbilityTask_MHGZWeaponMovement;
class UAnimMontage;
class UAnimSequenceBase;
class UCurveVector;

/** One point from the recorded local back-vault trajectory. */
USTRUCT(BlueprintType)
struct FBackVaultTrajectoryKey
{
	GENERATED_BODY()

	/** Normalized movement time, in the inclusive [0, 1] range. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Back Vault")
	float Time = 0.0f;

	/**
	 * X = travel progress, Y = lateral offset divided by total travel distance,
	 * Z = height divided by measured apex height.  The full recording ends at
	 * ground level; the ability samples only its action-owned prefix, then CMC
	 * continues from that non-zero-height handoff.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Back Vault")
	FVector NormalizedPosition = FVector::ZeroVector;
};

/**
 * RT+A 后撑杆跳。
 *
 * 这是一个完整的地面动作取消：Combo 边只会在来源攻击的精确
 * DodgeAcceptWindow 内命中；Ability 根据白灯选择姿势序列和实录轨迹。动画
 * 的 Jump 段保留导入的 Montage Root Motion；到 Jump_Over_Back 的精确段边界才
 * 由 CurvedVault RootMotionSource 接管。随后保留末端速度并交由 CMC 自由下落，
 * 避免有根运动的 Jump_Back 与无可靠根运动的 Jump_Over_Back 产生位移所有权断层。
 */
UCLASS(BlueprintType, Blueprintable)
class MHGZ_API UMHGZBackVaultAbility : public UMHGZInsectGlaiveAbility
{
	GENERATED_BODY()

public:
	UMHGZBackVaultAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility, bool bWasCancelled) override;

	/** The normal (no-white-extract) visual chain used by the fallback montage builder. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Back Vault|Animation")
	TSoftObjectPtr<UAnimSequenceBase> BackJumpSequence;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Back Vault|Animation")
	TSoftObjectPtr<UAnimSequenceBase> BackJumpOverSequence;

	/** White-extract visual chain; its air phase is longer and higher. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Back Vault|Animation")
	TSoftObjectPtr<UAnimSequenceBase> WhiteBackJumpSequence;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Back Vault|Animation")
	TSoftObjectPtr<UAnimSequenceBase> WhiteBackJumpOverSequence;

	/** Authored White Extract counterpart of AttackMontage. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Back Vault|Animation")
	TObjectPtr<UAnimMontage> WhiteAttackMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Back Vault|Animation")
	FName BackVaultMontageSlot = FName(TEXT("DefaultSlot"));

	/** Natural effective durations of the ordinary Jmp and Jump_Over source sequences. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Back Vault|Animation", meta = (ForceUnits = "s"))
	float BackJumpDuration = 0.650f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Back Vault|Animation", meta = (ForceUnits = "s"))
	float BackJumpOverDuration = 1.092f;

	/** Natural effective durations of the White Extract Jmp and Jump_Over source sequences. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Back Vault|Animation", meta = (ForceUnits = "s"))
	float WhiteBackJumpDuration = 0.650f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Back Vault|Animation", meta = (ForceUnits = "s"))
	float WhiteBackJumpOverDuration = 1.467f;

	/** Directly sampled from the no-white MHR capture; not an assumed parabola. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Back Vault|Recorded Path")
	TArray<FBackVaultTrajectoryKey> BackVaultTrajectory;

	/** Directly sampled from the white-extract MHR capture. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Back Vault|Recorded Path")
	TArray<FBackVaultTrajectoryKey> WhiteBackVaultTrajectory;

protected:
	virtual bool ValidateActionDependencies() const override;
	virtual bool PrepareAttackMontage() override;
	virtual bool StartAttackMontage(ACharacter& Character, UAnimMontage* Montage,
		FName StartSection) override;

private:
	bool BuildBackVaultMontage();
	/**
	 * Makes the first (Montage-root-motion-owned) Jump segment airborne before
	 * its first root-motion delta is extracted.  The matching restore path is
	 * deliberately owned by this GA rather than by the later CurvedVault task:
	 * the ability can be interrupted before that task exists.
	 */
	bool BeginBackVaultInitialFlight(ACharacter& Character);
	/** Restore the pre-vault locomotion mode after an interrupted flight phase. */
	void RestoreBackVaultInitialFlight(ACharacter& Character);
	/** Schedules the exact Jump -> JumpOver ownership boundary. */
	bool ScheduleJumpOverMovementHandoff();
	/** Runs at the JumpOver section boundary and gives its physical path to CurvedVault. */
	bool StartBackVaultMovement();
	UCurveVector* BuildTrajectoryCurve(const TArray<FBackVaultTrajectoryKey>& Keys,
		float StartProgress,
		float FreeFallHandoffProgress, float TotalDistance, float ApexHeight,
		float& OutHandoffDistance) const;
	void TryFinishBackVault();

	UFUNCTION()
	void HandleBackVaultVisualCompleted();

	UFUNCTION()
	void HandleBackVaultVisualInterrupted();

	UFUNCTION()
	void HandleJumpOverMovementHandoff();

	UFUNCTION()
	void HandleBackVaultMovementFinished(const FWeaponMovementResult& MovementResult);

	UPROPERTY()
	TObjectPtr<UAbilityTask_MHGZPlayMontageAndWait> BackVaultVisualTask;

	/** Keeps the section-boundary delay inside the ability lifecycle. */
	UPROPERTY()
	TObjectPtr<UAbilityTask_WaitDelay> JumpOverHandoffTask;

	UPROPERTY()
	TObjectPtr<UAbilityTask_MHGZWeaponMovement> BackVaultMovementTask;

	/** Keeps the runtime curve alive for the exact lifetime of the movement task. */
	UPROPERTY(Transient)
	TObjectPtr<UCurveVector> ActiveTrajectoryCurve;

	bool bUseWhiteBackVault = false;
	/** Balanced explicitly after Jump has handed movement ownership to CurvedVault. */
	bool bBackVaultVisualRootMotionDisabled = false;
	bool bVisualFinished = false;
	bool bMovementFinished = false;
	/** Set only after an unobstructed action phase has released movement while still airborne. */
	bool bBeginFreeFallAfterEnd = false;
	/** True from the first Jump frame until CurvedVault hands the action to Falling, or cleanup restores it. */
	bool bBackVaultInitialFlightOwned = false;
	/** Movement state to restore if the GA ends before its intentional Falling hand-off. */
	uint8 PreBackVaultMovementMode = 0;
	uint8 PreBackVaultCustomMovementMode = 0;
	bool bIsEndingBackVault = false;
};
