// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZGameplayAbility.h"
#include "MHGZAbilitySystemComponent.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "AbilitySystemGlobals.h"

UMHGZGameplayAbility::UMHGZGameplayAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

bool UMHGZGameplayAbility::CanActivateAbility(
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

	const UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
	if (!ASC) return false;

	const UMHGZAttributeSet* AttrSet = ASC->GetSet<UMHGZAttributeSet>();
	if (!AttrSet) return true; // 无属性集则跳过消耗检查

	// 检查耐力
	const float RequiredStamina = StaminaCost.GetValueAtLevel(GetAbilityLevel());
	if (RequiredStamina > 0.f && AttrSet->GetStamina() < RequiredStamina)
	{
		return false;
	}

	// 检查武器资源
	if (bRequiresWeaponResource && !CheckWeaponResourceForAbility())
	{
		return false;
	}

	// 检查冷却（通过 GAS 内置 Cooldown Tag）
	if (CooldownTag.IsValid() && ASC->HasMatchingGameplayTag(CooldownTag))
	{
		return false;
	}

	return true;
}

void UMHGZGameplayAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
	if (!ASC) return;

	if (bIsContinuous)
	{
		// 持续型：启动每帧扣耐 Timer
		StartContinuousStaminaDrain();
	}
	else
	{
		// 单次型：一次性扣耐
		DeductStaminaOnce();
	}

	// 启动冷却
	if (CooldownTag.IsValid())
	{
		ASC->AddLooseGameplayTag(CooldownTag);
	}
}

void UMHGZGameplayAbility::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	// 停止持续耗耐
	if (bIsContinuous)
	{
		StopContinuousStaminaDrain();
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UMHGZGameplayAbility::DeductStaminaOnce()
{
	const float Cost = StaminaCost.GetValueAtLevel(GetAbilityLevel());
	if (Cost <= 0.f) return;

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC) return;

	const UMHGZAttributeSet* AttrSet = ASC->GetSet<UMHGZAttributeSet>();
	if (!AttrSet) return;

	// Cost × StaminaDeductionRate
	const float FinalCost = Cost * AttrSet->GetStaminaDeductionRate();

	FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(
		nullptr, 1.0f, ASC->MakeEffectContext());
	if (Spec.IsValid())
	{
		// 直接 ApplyModToAttribute —— 走 ApplyMod 触发 PreAttributeChange Clamp
		ASC->ApplyModToAttribute(UMHGZAttributeSet::GetStaminaAttribute(),
			EGameplayModOp::Additive, -FinalCost);
	}
}

void UMHGZGameplayAbility::StartContinuousStaminaDrain()
{
	// 每 0.1s 扣一次耐（10Hz），避免每帧 Timer 开销过大
	AActor* Owner = GetOwningActorFromActorInfo();
	if (Owner)
	{
		Owner->GetWorldTimerManager().SetTimer(
			ContinuousStaminaTimer,
			this, &UMHGZGameplayAbility::OnContinuousStaminaTick,
			0.1f, true);
	}
}

void UMHGZGameplayAbility::StopContinuousStaminaDrain()
{
	AActor* Owner = GetOwningActorFromActorInfo();
	if (Owner)
	{
		Owner->GetWorldTimerManager().ClearTimer(ContinuousStaminaTimer);
	}
}

void UMHGZGameplayAbility::OnContinuousStaminaTick()
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC) return;

	const UMHGZAttributeSet* AttrSet = ASC->GetSet<UMHGZAttributeSet>();
	if (!AttrSet) return;

	// Rate × ConsumptionRate × Δt（单机，用固定 Δt = 0.1s）
	const float Rate = StaminaCostRate.GetValueAtLevel(GetAbilityLevel());
	const float FinalDrain = Rate * AttrSet->GetStaminaConsumptionRate() * 0.1f;

	const float CurrentStamina = AttrSet->GetStamina();
	if (CurrentStamina <= 0.f)
	{
		// 耐力归零 → 取消此 Ability
		CancelAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, false);
		return;
	}

	ASC->ApplyModToAttribute(UMHGZAttributeSet::GetStaminaAttribute(),
		EGameplayModOp::Additive, -FinalDrain);
}
