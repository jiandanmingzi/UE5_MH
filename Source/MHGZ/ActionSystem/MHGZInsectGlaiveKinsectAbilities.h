// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/DataValidation.h"
#include "InsectGlaive/IGActionConfigTypes.h"
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

	/** 该动作蒙太奇的播放倍率；1表示按资产原速播放。 */
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

	/** 当前蒙太奇播放任务的运行时引用；随本次能力清理。 */
	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> MontageTask;

	/** 本次实际播放的动作蒙太奇引用，供结束与旧回调校验。 */
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

	/** 该动作蒙太奇的播放倍率；1表示按资产原速播放。 */
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
	/** 当前蒙太奇播放任务的运行时引用；随本次能力清理。 */
	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> MontageTask;

	/** 本次实际播放的动作蒙太奇引用，供结束与旧回调校验。 */
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
	// 专属数值随本GA保存；Combat同名旧字段不再参与运行时。
	/** 拔刀操虫的猎虫动作值；仅影响本次猎虫命中。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "招式参数|操虫", meta = (ClampMin = "0.0"))
	float DrawSendKinsectMotionValue = 20.0f;


#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

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

	/** 原地上半身操虫蒙太奇，通常使用操虫同款常驻资产。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect|Action")
	TObjectPtr<UAnimMontage> ActionMontage;

	/** 该动作蒙太奇的播放倍率；1表示按资产原速播放。 */
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

	/** 当前蒙太奇播放任务的运行时引用；随本次能力清理。 */
	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> MontageTask;

	/** 本次实际播放的动作蒙太奇引用，供结束与旧回调校验。 */
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
