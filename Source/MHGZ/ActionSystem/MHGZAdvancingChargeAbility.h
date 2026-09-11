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
 * 后才提前进入 Core。蓄力时长属于 Montage 的 Charge Section，而非运行时循环，
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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Charge")
	FName ChargeEntrySection = FName(TEXT("Entry"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Charge")
	FName ChargeSection = FName(TEXT("Charge"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Charge")
	FName AttackSection = FName(TEXT("Core"));

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
	bool bChargeRouteConfigured = false;
	bool bReleaseStateCheckScheduled = false;
};
