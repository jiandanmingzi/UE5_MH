// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MHGZGameplayAbility.h"
#include "MHGZDodgeAbility.generated.h"

/**
 * 翻滚方向
 */
UENUM(BlueprintType)
enum class EComboDirection : uint8
{
	None    UMETA(DisplayName = "无方向"),
	Forward UMETA(DisplayName = "前"),
	Back    UMETA(DisplayName = "后"),
	Left    UMETA(DisplayName = "左"),
	Right   UMETA(DisplayName = "右")
};

/**
 * 武器翻滚配置——存于 DT_WeaponDodgeConfig
 */
USTRUCT(BlueprintType)
struct FWeaponDodgeConfig : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag WeaponTypeTag;

	/** 拔刀态各方向翻滚 Montage */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TMap<EComboDirection, TSoftObjectPtr<UAnimMontage>> UnsheathedMontages;
};

/**
 * UMHGZDodgeAbility — 翻滚/闪避 Ability（不进连招表）
 * 通过 TryActivateAbilityByTag(Input.Dodge) 激活
 */
UCLASS(BlueprintType, Blueprintable)
class UMHGZDodgeAbility : public UMHGZGameplayAbility
{
	GENERATED_BODY()

public:
	UMHGZDodgeAbility();

	// ═══════════════════════════════════════════
	// 配置
	// ═══════════════════════════════════════════

	/** 收刀态各方向翻滚 Montage（所有武器共用） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dodge")
	TMap<EComboDirection, TSoftObjectPtr<UAnimMontage>> SheathedDodgeMontages;

	// ═══════════════════════════════════════════
	// 覆写
	// ═══════════════════════════════════════════

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
	/** 根据摇杆方向选择对应 Montage */
	UAnimMontage* SelectDodgeMontage() const;

	/** 从摇杆输入推断翻滚方向 */
	EComboDirection GetDodgeDirection() const;
};
