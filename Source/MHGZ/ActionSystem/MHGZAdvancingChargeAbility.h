// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MHGZAdvancingCounterAbility.h"
#include "MHGZAdvancingChargeAbility.generated.h"

/**
 * 可配置的“入口 → 按住蓄力 → 出招”层。
 *
 * Entry 始终完整播放；若所有 ChargeReleaseControls 仍按住，则按 Montage 的
 * Entry → Charge 路由进入一次性拉伸后的蓄力段。任意时刻只有在这些控制都松开
 * 后才提前进入 Core；入口内松开可先经过显式配置的正常速度续段，避免漏掉源帧。
 * 蓄力时长属于 Montage 的 Charge Section，而非运行时循环，
 * 因此可复用于不同动作的入口、按住键与蓄力表现。
 */
UCLASS(BlueprintType, Blueprintable)
class MHGZ_API UMHGZAdvancingChargeAbility : public UMHGZAdvancingCounterAbility
{
	GENERATED_BODY()

public:
	UMHGZAdvancingChargeAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility, bool bWasCancelled) override;

	virtual void HandleInputReleased(const FWeaponInputSnapshot& Snapshot) override;

	/** 由 Charge Section 起点的精确 AnimNotify 调用。 */
	void BeginAdvancingCharge();

	/** 进入保持阶段前使用的起手Section名。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Charge")
	FName ChargeEntrySection = FName(TEXT("Entry"));

	/** 按键仍保持时循环播放的Section名。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Charge")
	FName ChargeSection = FName(TEXT("Charge"));

	/** 保持结束后进入攻击的Section名。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Charge")
	FName AttackSection = FName(TEXT("Core"));

	/** 可选：某入口在未蓄力时连续播完的正常速度段；未配置仍直接进入AttackSection。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Charge")
	TMap<FName, FName> UnchargedExitSectionByEntry;

	/** Read-only route resolution; does not jump, play, or create a Montage. */
	FName ResolveUnchargedExitSection(FName EntrySection) const;

	/** 必须全部松开才提前结束 Charge；通常为组合动作的各物理键。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Charge")
	TArray<FGameplayTag> ChargeReleaseControls;

protected:
	virtual bool StartAttackMontage(ACharacter& Character, UAnimMontage* Montage,
		FName StartSection) override;

private:
	bool ConfigureChargeRoute();
	bool AreAllChargeReleaseControlsReleased() const;
	UAnimInstance* GetAttackAnimInstance() const;
	void ScheduleChargeReleaseStateCheck();
	void ResolveChargeReleaseStateCheck();
	void ProceedToAttackIfAllReleased();

	bool bChargeStarted = false;
	FName ActiveChargeEntrySection;
	bool bChargeRouteConfigured = false;
	bool bReleaseStateCheckScheduled = false;
};
