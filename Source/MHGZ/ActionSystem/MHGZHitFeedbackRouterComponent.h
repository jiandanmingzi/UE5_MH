// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "MHGZHitFeedbackTypes.h"
#include "MHGZHitFeedbackRouterComponent.generated.h"

class APlayerController;
class UAbilitySystemComponent;
struct FGameplayCueParameters;

/**
 * 目标侧命中反馈路由器。
 *
 * 挂在受击物理目标上（Character 或木桩/怪物）；只消费 AttributeSet 构造的
 * FMHGZHitFeedbackResult，显式 Execute 物理/元素/暴击/伤害数字 Cue，
 * 并把卡肉请求提交给 SourceActor 的 HitStopController，从源 Controller 触发镜头反馈。
 * 不重算伤害、不扫描 DynamicAssetTags。
 */
UCLASS(ClassGroup = (MHGZ), BlueprintType, meta = (BlueprintSpawnableComponent))
class UMHGZHitFeedbackRouterComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMHGZHitFeedbackRouterComponent();

	/** 结算层唯一入口：按冻结顺序执行 Cue、提交卡肉与镜头反馈。 */
	UFUNCTION(BlueprintCallable, Category = "MHGZ|Feedback")
	void RouteHitFeedback(const FMHGZHitFeedbackResult& Result);

	// ── 最小 C++ 测试查询面 ──
	UFUNCTION(BlueprintPure, Category = "MHGZ|Feedback|Test")
	int32 GetExecutedCueCount() const { return ExecutedCueCount; }

	UFUNCTION(BlueprintPure, Category = "MHGZ|Feedback|Test")
	FGameplayTag GetLastExecutedCueTag() const { return LastExecutedCueTag; }

	UFUNCTION(BlueprintPure, Category = "MHGZ|Feedback|Test")
	float GetLastDamageNumberMagnitude() const { return LastDamageNumberMagnitude; }

private:
	UAbilitySystemComponent* GetTargetASC() const;

	APlayerController* FindSourcePlayerController(AActor* SourceActor) const;

	void ExecuteCueOnTarget(UAbilitySystemComponent* TargetASC,
		const FGameplayTag& CueTag, const FGameplayCueParameters& Params);

	int32 ExecutedCueCount = 0;
	FGameplayTag LastExecutedCueTag;
	float LastDamageNumberMagnitude = 0.f;
};
