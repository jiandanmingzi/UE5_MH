// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZInsectGlaiveAbility.h"
#include "AttributeSystem/Res_InsectGlaive.h"
#include "MHGZAbilitySystemComponent.h"
#include "MHGZPlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "GameplayEffect.h"

UMHGZInsectGlaiveAbility::UMHGZInsectGlaiveAbility()
{
}

URes_InsectGlaive* UMHGZInsectGlaiveAbility::GetIGResourceComponent() const
{
	const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC) return nullptr;

	const AActor* Owner = ASC->GetOwnerActor();
	if (const AMHGZPlayerState* PS = Cast<AMHGZPlayerState>(Owner))
	{
		TArray<UActorComponent*> Components;
		PS->GetComponents(Components);
		for (UActorComponent* Comp : Components)
		{
			if (URes_InsectGlaive* IGComp = Cast<URes_InsectGlaive>(Comp))
			{
				return IGComp;
			}
		}
	}

	return nullptr;
}

bool UMHGZInsectGlaiveAbility::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	// 子类可覆写 CheckWeaponResourceForAbility 做额外检查
	return CheckWeaponResourceForAbility();
}

void UMHGZInsectGlaiveAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// 父类（UMHGZAttackAbility）：扣耐力、方向修正、播 Montage
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	// 三灯攻击音效——每个攻击 GA 激活时播放（无论是否命中）
	if (TripleUpSwingSound)
	{
		const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
		if (ASC && ASC->HasMatchingGameplayTag(
			FGameplayTag::RequestGameplayTag(TEXT("Combat.Branch.TripleUp"))))
		{
			UGameplayStatics::PlaySound2D(this, TripleUpSwingSound);
		}
	}
}

FGameplayEffectSpecHandle UMHGZInsectGlaiveAbility::MakeDamageSpec(
	AActor* Target, FName HitzoneBoneName, int32 SegmentIndex)
{
	// 父类构造基础 Spec
	FGameplayEffectSpecHandle Spec = Super::MakeDamageSpec(Target, HitzoneBoneName, SegmentIndex);

	// 三灯时注入额外 GameplayCue Tag
	const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (ASC && ASC->HasMatchingGameplayTag(
		FGameplayTag::RequestGameplayTag(TEXT("Combat.Branch.TripleUp"))))
	{
		if (Spec.IsValid())
		{
			Spec.Data->AddDynamicAssetTag(
				FGameplayTag::RequestGameplayTag(TEXT("GameplayCue.IG.TripleUpActivated")));
		}
	}

	return Spec;
}

bool UMHGZInsectGlaiveAbility::CheckExtractRequirement(FGameplayTag ExtractColor) const
{
	if (URes_InsectGlaive* RC = GetIGResourceComponent())
	{
		return RC->HasExtract(ExtractColor);
	}
	return false;
}

bool UMHGZInsectGlaiveAbility::ConsumeExtractAndApplyBurst(
	FGameplayTag ExtractType, TSubclassOf<UGameplayEffect> BurstGE)
{
	URes_InsectGlaive* RC = GetIGResourceComponent();
	if (!RC || !RC->HasExtract(ExtractType)) return false;

	// 消耗灯
	RC->ConsumeExtract(ExtractType);

	// Apply 爆发 Buff
	if (BurstGE)
	{
		UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
		if (ASC)
		{
			ASC->ApplyGameplayEffectToSelf(
				BurstGE->GetDefaultObject<UGameplayEffect>(),
				1.0f, ASC->MakeEffectContext());
		}
	}
	return true;
}
