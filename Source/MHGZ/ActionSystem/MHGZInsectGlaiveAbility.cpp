// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZInsectGlaiveAbility.h"
#include "AttributeSystem/Res_InsectGlaive.h"
#include "MHGZAbilitySystemComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameplayEffect.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

UMHGZInsectGlaiveAbility::UMHGZInsectGlaiveAbility()
{
}

URes_InsectGlaive* UMHGZInsectGlaiveAbility::GetIGResourceComponent() const
{
	const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC) return nullptr;

	// M2：资源查找统一走 RuntimeHost 的 ResourceProvider，不再扫描 PlayerState 组件。
	const UMHGZAbilitySystemComponent* MHGZASC = Cast<UMHGZAbilitySystemComponent>(ASC);
	if (!MHGZASC) return nullptr;

	const UMHGZWeaponRuntimeHostComponent* Host = MHGZASC->GetRuntimeHost();
	if (!Host) return nullptr;

	return Cast<URes_InsectGlaive>(Host->GetResourceProvider());
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

	return true;
}

void UMHGZInsectGlaiveAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// 父类（UMHGZAttackAbility）：扣耐力、方向修正、播 Montage
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!IsActionActivationCommitted()) return;

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
