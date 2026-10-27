// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MHGZInsectGlaiveAbility.h"
#include "MHGZInsectGlaiveGroundAbilities.generated.h"

/**
 * 带一个可配置虫印段的地面近战攻击。
 *
 * 单 RT 虫印斩使用第 0 段；四连印斩使用第 1 段。只有指定段的 Damage GE
 * 成功提交至有效 Hitzone 后，才建立/替换唯一虫印。
 */
UCLASS(BlueprintType, Blueprintable)
class MHGZ_API UMHGZMarkSlashAbility : public UMHGZInsectGlaiveAbility
{
	GENERATED_BODY()

public:
	UMHGZMarkSlashAbility();

	/** 能够施加虫印的攻击伤害段索引，从零开始。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mark", meta = (ClampMin = "0"))
	int32 MarkSegmentIndex = 0;

	/** 指定虫印段成功命中后，自动命令猎虫单次攻击该部位并萃取；猎虫实际命中产一份粉尘，不检查或占用普通产粉冷却。仅虫印斩启用。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mark", meta = (DisplayName = "虫印命中后放虫攻击"))
	bool bSendKinsectOnMarkHit = false;

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

protected:
	virtual bool ValidateActionDependencies() const override;
	virtual void HandleSuccessfulAttackDamage(const FHitResult& Hit,
		int32 SegmentIndex) override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

private:
	bool bEstablishedMarkThisActivation = false;
};
