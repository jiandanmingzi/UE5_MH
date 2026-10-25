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

	/** 包含最终原地与移动收刀段的常驻蒙太奇。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sheathe")
	TObjectPtr<UAnimMontage> SheatheMontage;

	/** 原地收刀的入口Section名。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sheathe")
	FName IdleSectionName = FName(TEXT("Idle"));

	/** 移动收刀的入口Section名。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sheathe")
	FName WalkSectionName = FName(TEXT("Walk"));

	/** 移动收刀是否通过根运动通知窗口取得所有权；仅在Walk段已放置并核对该窗口时开启，Idle仍按原整段所有权处理。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sheathe|Motion Matching",
		meta = (DisplayName = "Walk Uses Action Root Motion Phase"))
	bool bWalkUsesActionRootMotionPhase = false;

	/** Uses only the frozen input snapshot; live movement is never resampled. */
	FName SelectSectionName(const FWeaponInputSnapshot& Input) const;

	/** Exact Montage-instance callback used by AnimNotify_SheatheCommit. */
	bool CommitSheathe(const FWeaponActionToken& ActionToken);

	/** Shares the existing recall transaction without activating a second GA. */
	bool CommitKinsectRecall(const FWeaponActionToken& ActionToken);

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
	virtual bool IsMotionMatchingHandoffCommitComplete() const override
	{
		return bSheatheCommitted;
	}

	/** Test seam for the asynchronous montage boundary; production owns the task. */
	virtual bool StartSheatheMontage(ACharacter& Character, UAnimMontage* Montage,
		FName StartSection);

	UFUNCTION()
	void OnMontageCompleted();

	UFUNCTION()
	void OnMontageInterrupted();

private:
	/** 当前蒙太奇播放任务的运行时引用；随本次能力清理。 */
	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> MontageTask;

	/** 本次实际播放的收刀蒙太奇引用。 */
	UPROPERTY()
	TObjectPtr<UAnimMontage> ActiveSheatheMontage;

	FName ActiveSectionName = NAME_None;
	FWeaponOwnedTagToken IdleBlockMovementToken;
	FWeaponOwnedTagToken SheathingToken;
	bool bUsingWalkSection = false;
	bool bSheatheCommitted = false;
	bool bKinsectRecallCommitted = false;
	bool bOwnsMontageRootMotion = false;
	bool bEndingSheathe = false;

	void ReleaseMontageRootMotionOwnership();
};
