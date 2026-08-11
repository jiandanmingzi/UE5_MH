// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/HitResult.h"
#include "GameplayEffectTypes.h"
#include "GameplayTagContainer.h"
#include "MHGZHitFeedbackTypes.generated.h"

class AActor;
class UCameraShakeBase;

/**
 * 一次已结算命中产生的唯一反馈结果。
 *
 * 由 UMHGZAttributeSet::PostGameplayEffectExecute 在 IncomingHitSignal
 * 到达时构造一次；UMHGZHitFeedbackRouterComponent 只消费本结果，
 * 不重算伤害、不扫描 DynamicAssetTags。
 */
USTRUCT(BlueprintType)
struct MHGZ_API FMHGZHitFeedbackResult
{
	GENERATED_BODY()

	/** 最终实际扣血（按目标当前 Health Clamp 后，>=0）。 */
	UPROPERTY(BlueprintReadOnly, Category = "MHGZ|Feedback")
	float ActualDamage = 0.f;

	/** 本次命中的硬直值（BaseStagger × StaggerMultiplier × HitzoneStaggerRate，>=0）。 */
	UPROPERTY(BlueprintReadOnly, Category = "MHGZ|Feedback")
	float ActualStagger = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "MHGZ|Feedback")
	bool bCritical = false;

	/** 真实 Sweep HitResult（深拷贝；Router 从这里读 ImpactPoint/ImpactNormal/组件）。 */
	UPROPERTY(BlueprintReadOnly, Category = "MHGZ|Feedback")
	FHitResult Hit;

	/** 物理命中 Cue Tag（如 GameplayCue.Hit.Slash/Blunt/Kinsect）。 */
	UPROPERTY(BlueprintReadOnly, Category = "MHGZ|Feedback")
	FGameplayTag HitCueTag;

	/** 元素附魔命中 Cue Tag（可空）。 */
	UPROPERTY(BlueprintReadOnly, Category = "MHGZ|Feedback")
	FGameplayTag ElementCueTag;

	/** 硬直等级 Tag（Combat.Stagger.Light/Medium/Heavy；与 ActualStagger>0 同时有效才广播事件）。 */
	UPROPERTY(BlueprintReadOnly, Category = "MHGZ|Feedback")
	FGameplayTag HitStaggerTag;

	/** 本次攻击激活的稳定攻击身份。 */
	UPROPERTY(BlueprintReadOnly, Category = "MHGZ|Feedback")
	FGuid AttackInstanceID;

	UPROPERTY(BlueprintReadOnly, Category = "MHGZ|Feedback")
	TWeakObjectPtr<AActor> SourceActor;

	/** 物理表现目标：优先 Target ASC 的 AvatarActor，木桩等无 Avatar 才回退 OwnerActor。 */
	UPROPERTY(BlueprintReadOnly, Category = "MHGZ|Feedback")
	TWeakObjectPtr<AActor> TargetActor;

	/** 卡肉时长（秒）；>0 时 Router 提交给 SourceActor 的 HitStopController。 */
	UPROPERTY(BlueprintReadOnly, Category = "MHGZ|Feedback")
	float HitStopDuration = 0.f;

	/** 镜头震动类（可空）。 */
	UPROPERTY(BlueprintReadOnly, Category = "MHGZ|Feedback")
	TSubclassOf<UCameraShakeBase> CameraShakeClass;

	/** 镜头震动强度倍率（>0 且 CameraShakeClass 有效时才触发）。 */
	UPROPERTY(BlueprintReadOnly, Category = "MHGZ|Feedback")
	float CameraShakeScale = 0.f;

	/** 原始 GE Context；Cue 参数与事件共享同一身份。 */
	UPROPERTY(BlueprintReadOnly, Category = "MHGZ|Feedback")
	FGameplayEffectContextHandle ContextHandle;
};
