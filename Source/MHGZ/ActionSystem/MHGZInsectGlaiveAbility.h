// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MHGZAttackAbility.h"
#include "MHGZInsectGlaiveAbility.generated.h"

class URes_InsectGlaive;
class USoundBase;

/**
 * UMHGZInsectGlaiveAbility — 虫棍 GA 基类
 * 继承 UMHGZAttackAbility，增加萃取检查、三灯音效注入、消耗灯
 */
UCLASS(BlueprintType, Blueprintable, Abstract)
class UMHGZInsectGlaiveAbility : public UMHGZAttackAbility
{
	GENERATED_BODY()

public:
	UMHGZInsectGlaiveAbility();

	// ── 覆写 ──
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

	virtual FGameplayEffectSpecHandle MakeDamageSpec(
		AActor* Target, FName HitzoneBoneName, int32 SegmentIndex) override;

	// ── 配置 ──

	/** 三灯攻击音效——每个攻击 GA 激活时播放 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability|Audio")
	TObjectPtr<USoundBase> TripleUpSwingSound;

	/** 检查是否有指定灯 */
	UFUNCTION(BlueprintCallable, Category = "MHGZ|IG")
	bool CheckExtractRequirement(FGameplayTag ExtractColor) const;

	/** 消耗灯并 Apply 爆发 Buff */
	UFUNCTION(BlueprintCallable, Category = "MHGZ|IG")
	bool ConsumeExtractAndApplyBurst(FGameplayTag ExtractType, TSubclassOf<UGameplayEffect> BurstGE);

protected:
	/** 获取虫棍资源组件 */
	URes_InsectGlaive* GetIGResourceComponent() const;
};
