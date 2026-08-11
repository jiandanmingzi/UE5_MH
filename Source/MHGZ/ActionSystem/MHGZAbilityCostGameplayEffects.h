// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "ScalableFloat.h"
#include "MHGZAbilityCostGameplayEffects.generated.h"

/**
 * Instant 耐力成本 GE。
 * 修饰器：Stamina 属性 Additive，幅度为 SetByCaller（Data.Cost.Stamina）。
 * 调用方（GA / Drain Task）在 Spec 上写入负值幅度。
 */
UCLASS()
class MHGZ_API UMHGZStaminaCostGameplayEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UMHGZStaminaCostGameplayEffect();

	/** SetByCaller 数据标签（Data.Cost.Stamina）。 */
	static FGameplayTag GetStaminaCostSetByCallerTag();
};

/**
 * 冷却 GE：HasDuration，时长由调用方写入 Spec（SetDuration），
 * CooldownTag 由 GA 在 ApplyCooldown 时写入 DynamicGrantedTags。
 */
UCLASS()
class MHGZ_API UMHGZCooldownGameplayEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UMHGZCooldownGameplayEffect();
};
