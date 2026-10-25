// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActionSystem/MHGZIncomingHitResolverComponent.h"
#include "MHGZInsectGlaiveAbility.h"
#include "WeaponRuntime/MHGZWeaponRuntimeTypes.h"
#include "MHGZAdvancingCounterAbility.generated.h"

class UAbilityTask_MHGZWeaponMovement;
class UAbilityTask_PlayMontageAndWait;
class UAnimSequenceBase;

/**
 * 突进回旋斩的反击层。
 *
 * Notify 只负责用精确 Mesh + MontageInstanceID 找到本实例，再把窗口开关交给
 * 此 Ability。真正的伤害消费始终由目标侧 IncomingHitResolver 完成；因此反击
 * 不是把角色改成无敌，也不会吞掉没有 AttackInstanceID 的环境伤害。
 */
UCLASS(BlueprintType, Blueprintable)
class MHGZ_API UMHGZAdvancingCounterAbility : public UMHGZInsectGlaiveAbility
{
	GENERATED_BODY()

public:
	UMHGZAdvancingCounterAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility, bool bWasCancelled) override;

	/** 由精确的 Advancing Counter Notify Begin 调用。 */
	bool BeginAdvancingCounterWindow(FName NotifyEventID, float TotalDuration);

	/** 由精确的 Advancing Counter Notify End 及所有取消路径幂等调用。 */
	void EndAdvancingCounterWindow(FName NotifyEventID);

	/** 供调试/自动化确认本次动作最多只允许一次成功反击。 */
	bool HasCounterSucceeded() const { return bCounterSucceeded; }

	/** Resolver 的高优先级数值；同优先级按注册顺序稳定排序。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Counter")
	int32 CounterInterceptorPriority = 100;

	/**
	 * 舞踏起跳的纯表现序列（in-place，不提供 Root Motion）。实际弹道始终由同一
	 * MovementTask 的 BallisticVault 驱动。
	 *
	 * 这是 **DanceVaultMontage 加载失败时的回退**，不是主路径 —— 见下方
	 * DanceVaultMontage 的说明。（本注释曾写「无需创建永久单段 Montage 资产」，
	 * 那是 UMHGZWuTaMontageSetupCommandlet 引入之前的结论，已过时。）
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Counter|Dance Vault")
	TSoftObjectPtr<UAnimSequenceBase> DanceVaultSequence;

	/** 优先使用的常驻舞踏蒙太奇；设置后以资产片段倍率及混入混出配置为准，不读取下方回退参数。动态回退在UE5.6中无法有效改变源段播放倍率，不能靠该路径精确对齐录制时长。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Counter|Dance Vault")
	TSoftObjectPtr<UAnimMontage> DanceVaultMontage;

	/** 未指定常驻舞踏蒙太奇时，回退播放使用的Slot名。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Counter|Dance Vault")
	FName DanceVaultMontageSlot = FName(TEXT("DefaultSlot"));

	/** 仅为旧动态回退保留的播放倍率参数；UE5.6该构建路径实际忽略倍率，已指定常驻蒙太奇时同样不读取。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Counter|Dance Vault",
		meta = (ClampMin = "0.01"))
	float DanceVaultAnimationPlayRate = 1.0f;

	/** 仅旧舞踏回退路径使用的混入时长，单位为秒；常驻蒙太奇采用资产设置。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Counter|Dance Vault",
		meta = (ClampMin = "0.0"))
	float DanceVaultBlendInTime = 0.0f;

	/** 仅旧舞踏回退路径使用的混出时长，单位为秒；常驻蒙太奇采用资产设置。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Counter|Dance Vault",
		meta = (ClampMin = "0.0"))
	float DanceVaultBlendOutTime = 0.05f;

protected:
	virtual bool ValidateActionDependencies() const override;

	/**
	 * 反击已由 Resolver 消费后调用。M4.7 只建立舞踏入口；M5 在这里的
	 * 派生实现中接入唯一 Movement Token / BallisticVault，不让地面 GA 偷写 CMC。
	 */
	virtual bool AddAdvancingCounterDanceStack();

	/** 表现或后续空中状态可在蓝图覆写；不承担伤害消费或层数结算。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Counter")
	void OnAdvancingCounterSucceeded(const FIncomingHitContext& Context);

private:
	bool StartAdvancingCounterVault();
	bool StartAdvancingCounterVaultVisual();

	UFUNCTION()
	void HandleAdvancingCounterVaultFinished(const FWeaponMovementResult& MovementResult);

	struct FCounterWindowState
	{
		int64 ResolverTokenID = 0;
		FWeaponOwnedTagToken TagToken;
	};

	EIncomingHitInterceptResult HandleIncomingHit(const FWeaponActionToken& ExpectedAction,
		const FIncomingHitContext& Context);
	void CloseAllAdvancingCounterWindows();

	TMap<FName, FCounterWindowState> CounterWindows;
	/** 突进反击成功后执行舞踏位移的当前任务。 */
	UPROPERTY()
	TObjectPtr<UAbilityTask_MHGZWeaponMovement> AdvancingCounterVaultTask;

	/** 只负责姿势；它的完成或中断绝不能抢先结束尚在空中的 Action。 */
	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> AdvancingCounterVaultMontageTask;
	bool bCounterSucceeded = false;
	/** True only when the BallisticVault completed while CMC still owns an airborne body. */
	bool bBeginFreeFallAfterEnd = false;
	/**
	 * True only when the BallisticVault reached the ground under its own arc, so
	 * this Action owns the landing pose even though the Host never started a
	 * free fall.  Mutually exclusive with bBeginFreeFallAfterEnd.
	 */
	bool bPlayLandedPresentation = false;
	bool bIsEndingCounterAbility = false;
};
