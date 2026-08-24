// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InsectGlaive/Kinsect/Kinsect.h"
#include "MHGZGameplayAbility.h"
#include "MHGZInsectGlaiveKinsectAbilities.generated.h"

class ACharacter;
class UAbilityTask_PlayMontageAndWait;
class UAnimMontage;
class URes_InsectGlaive;

/** 持刀地面 LT+Y：臂上沿瞄准方向送虫；已部署时飞向冻结目标点。 */
UCLASS(BlueprintType, Blueprintable)
class MHGZ_API UMHGZSendKinsectAbility : public UMHGZGameplayAbility
{
	GENERATED_BODY()

public:
	UMHGZSendKinsectAbility();

	/** Exact Montage-instance callback used by AnimNotify_KinsectSendCommit. */
	bool CommitSendKinsect(const FWeaponActionToken& ActionToken);

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

	/** 无伤害、in-place 的上半身动作 Montage；由最终 GA 数据型子类填写。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect|Action")
	TObjectPtr<UAnimMontage> ActionMontage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect|Action",
		meta = (ClampMin = "0.01"))
	float ActionMontagePlayRate = 1.0f;

protected:
	virtual bool ValidateActionDependencies() const override;
	virtual bool ValidateActionMontageDependencies() const;
	virtual bool StartActionMontage(ACharacter& Character, UAnimMontage* Montage);
	bool BuildRequest(FKinsectFlightRequest& OutRequest) const;
	URes_InsectGlaive* GetIGResource() const;

	UFUNCTION()
	void OnActionMontageCompleted();

	UFUNCTION()
	void OnActionMontageInterrupted();

private:
	/** 保存 Confirm 后的完整请求；Commit 时绝不重读 Aim/相机/摇杆。 */
	FKinsectFlightRequest PendingRequest;

	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> MontageTask;

	UPROPERTY()
	TObjectPtr<UAnimMontage> ActiveActionMontage;

	bool bCommandCommitted = false;
};

/** 持刀地面 LT+B：主动召回并在到达时原子交付 Pending 精华。 */
UCLASS(BlueprintType, Blueprintable)
class MHGZ_API UMHGZRecallKinsectAbility : public UMHGZGameplayAbility
{
	GENERATED_BODY()

public:
	UMHGZRecallKinsectAbility();

	/** Exact Montage-instance callback used by AnimNotify_KinsectRecallCommit. */
	bool CommitRecallKinsect(const FWeaponActionToken& ActionToken);

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

	/** 无伤害、in-place 的上半身动作 Montage；由最终 GA 数据型子类填写。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect|Action")
	TObjectPtr<UAnimMontage> ActionMontage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect|Action",
		meta = (ClampMin = "0.01"))
	float ActionMontagePlayRate = 1.0f;

protected:
	virtual bool ValidateActionDependencies() const override;
	virtual bool ValidateActionMontageDependencies() const;
	virtual bool StartActionMontage(ACharacter& Character, UAnimMontage* Montage);
	URes_InsectGlaive* GetIGResource() const;

	UFUNCTION()
	void OnActionMontageCompleted();

	UFUNCTION()
	void OnActionMontageInterrupted();

private:
	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> MontageTask;

	UPROPERTY()
	TObjectPtr<UAnimMontage> ActiveActionMontage;

	bool bCommandCommitted = false;
};

/**
 * 收刀地面 RT：播放无 Root Motion 的上半身拔刀放虫 Montage。
 *
 * This intentionally does not inherit the attack/draw-attack chain: its
 * montage must leave locomotion live, so it never owns Attacking,
 * BlockMovement or MontageRootMotion. DrawCommit equips the weapon; the
 * later KinsectSendCommit launches the frozen request.
 */
UCLASS(BlueprintType, Blueprintable)
class MHGZ_API UMHGZDrawAndSendKinsectAbility : public UMHGZGameplayAbility
{
	GENERATED_BODY()

public:
	UMHGZDrawAndSendKinsectAbility();

	/** Exact Montage-instance callback used by AnimNotify_DrawCommit. */
	bool CommitDraw(const FWeaponActionToken& ActionToken);

	/** Exact Montage-instance callback used by AnimNotify_KinsectSendCommit. */
	bool CommitSendKinsect(const FWeaponActionToken& ActionToken);

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
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

	/** In-place UpperBody_IGAction montage, normally AM_IG_SendKinsect. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect|Action")
	TObjectPtr<UAnimMontage> ActionMontage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect|Action",
		meta = (ClampMin = "0.01"))
	float ActionMontagePlayRate = 1.0f;

protected:
	virtual bool ValidateActionDependencies() const override;
	virtual bool ValidateActionMontageDependencies() const;
	virtual bool StartActionMontage(ACharacter& Character, UAnimMontage* Montage);
	bool BuildRequest(FKinsectFlightRequest& OutRequest) const;
	URes_InsectGlaive* GetIGResource() const;

	UFUNCTION()
	void OnActionMontageCompleted();

	UFUNCTION()
	void OnActionMontageInterrupted();

private:
	FKinsectFlightRequest PendingRequest;

	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> MontageTask;

	UPROPERTY()
	TObjectPtr<UAnimMontage> ActiveActionMontage;

	bool bDrawCommitted = false;
	bool bCommandCommitted = false;
};

/** 持刀地面 LT+RT：发射虫印弹；Resource 维护唯一虫印所有权。 */
UCLASS(BlueprintType, Blueprintable)
class MHGZ_API UMHGZMarkKinsectTargetAbility : public UMHGZGameplayAbility
{
	GENERATED_BODY()

public:
	UMHGZMarkKinsectTargetAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

protected:
	virtual bool ValidateActionDependencies() const override;
	URes_InsectGlaive* GetIGResource() const;
};
