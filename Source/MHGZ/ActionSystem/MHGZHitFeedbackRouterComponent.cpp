// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZHitFeedbackRouterComponent.h"

#include "AttributeSystem/MHGZAttributeSet.h"
#include "MHGZHitStopControllerComponent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

UMHGZHitFeedbackRouterComponent::UMHGZHitFeedbackRouterComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

UAbilitySystemComponent* UMHGZHitFeedbackRouterComponent::GetTargetASC() const
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}

	if (IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Owner))
	{
		return ASI->GetAbilitySystemComponent();
	}
	return Owner->FindComponentByClass<UAbilitySystemComponent>();
}

APlayerController* UMHGZHitFeedbackRouterComponent::FindSourcePlayerController(
	AActor* SourceActor) const
{
	if (!SourceActor)
	{
		return nullptr;
	}
	if (APlayerController* PC = Cast<APlayerController>(SourceActor))
	{
		return PC;
	}
	if (APawn* Pawn = Cast<APawn>(SourceActor))
	{
		return Cast<APlayerController>(Pawn->GetController());
	}
	return Cast<APlayerController>(SourceActor->GetInstigatorController());
}

void UMHGZHitFeedbackRouterComponent::ExecuteCueOnTarget(
	UAbilitySystemComponent* TargetASC, const FGameplayTag& CueTag,
	const FGameplayCueParameters& Params)
{
	if (!TargetASC || !CueTag.IsValid())
	{
		return;
	}
	TargetASC->ExecuteGameplayCue(CueTag, Params);
	++ExecutedCueCount;
	LastExecutedCueTag = CueTag;
}

void UMHGZHitFeedbackRouterComponent::RouteHitFeedback(
	const FMHGZHitFeedbackResult& Result)
{
	UAbilitySystemComponent* TargetASC = GetTargetASC();
	if (!TargetASC)
	{
		return;
	}

	// 所有 Cue 参数携带真实命中位置/法线与原 GE Context；不重算伤害。
	FGameplayCueParameters Params;
	Params.Location = Result.Hit.ImpactPoint;
	Params.Normal = Result.Hit.ImpactNormal;
	Params.Instigator = Result.SourceActor;
	Params.EffectCauser = Result.SourceActor;
	Params.TargetAttachComponent = Result.Hit.GetComponent();
	Params.EffectContext = Result.ContextHandle;

	// 1. 物理命中 Cue
	if (Result.HitCueTag.IsValid())
	{
		ExecuteCueOnTarget(TargetASC, Result.HitCueTag, Params);
	}

	// 2. 元素附魔 Cue（可空）
	if (Result.ElementCueTag.IsValid())
	{
		ExecuteCueOnTarget(TargetASC, Result.ElementCueTag, Params);
	}

	// 3. 暴击 Cue（仅暴击且 Tag 有效）
	if (Result.bCritical)
	{
		static const FGameplayTag CritTag =
			FGameplayTag::RequestGameplayTag(TEXT("GameplayCue.Hit.Crit"));
		if (CritTag.IsValid())
		{
			ExecuteCueOnTarget(TargetASC, CritTag, Params);
		}
	}

	// 4. 伤害数字：目标没有 Health 属性时不显示伪造数字，保留无伤害命中反馈。
	if (TargetASC->HasAttributeSetForAttribute(UMHGZAttributeSet::GetHealthAttribute()))
	{
		static const FGameplayTag DamageNumberTag =
			FGameplayTag::RequestGameplayTag(TEXT("GameplayCue.Hit.DamageNumber"));
		if (DamageNumberTag.IsValid())
		{
			FGameplayCueParameters NumberParams = Params;
			NumberParams.RawMagnitude = Result.ActualDamage;
			ExecuteCueOnTarget(TargetASC, DamageNumberTag, NumberParams);
			LastDamageNumberMagnitude = Result.ActualDamage;
		}
	}

	// 5. 卡肉：提交给 SourceActor 上的 HitStopController（Token 合并由控制器负责）。
	if (Result.HitStopDuration > 0.f)
	{
		if (AActor* Source = Result.SourceActor.Get())
		{
			if (UMHGZHitStopControllerComponent* HitStop =
				Source->FindComponentByClass<UMHGZHitStopControllerComponent>())
			{
				HitStop->RequestHitStop(Result.HitStopDuration);
			}
		}
	}

	// 6. 镜头反馈：从源 Controller 触发。
	if (Result.CameraShakeClass && Result.CameraShakeScale > 0.f)
	{
		if (APlayerController* PC = FindSourcePlayerController(Result.SourceActor.Get()))
		{
			PC->ClientStartCameraShake(Result.CameraShakeClass, Result.CameraShakeScale);
		}
	}
}
