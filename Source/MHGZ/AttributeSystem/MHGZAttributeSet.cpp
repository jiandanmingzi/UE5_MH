// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZAttributeSet.h"
#include "GameplayEffectExtension.h"
#include "MHGZCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"

UMHGZAttributeSet::UMHGZAttributeSet()
{
	// 基础值
	InitHealth(100.f);
	InitMaxHealth(100.f);
	InitStamina(100.f);
	InitMaxStamina(100.f);
	InitStaminaRegenRate(1.0f);
	InitStaminaDeductionRate(1.0f);
	InitStaminaConsumptionRate(1.0f);
	InitAttackPower(0.f);
	InitDefense(0.f);
	InitCriticalRate(0.f);
	InitStaggerMultiplier(1.0f);
	InitMoveSpeedMultiplier(1.0f);
}

void UMHGZAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION_NOTIFY(UMHGZAttributeSet, Health, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMHGZAttributeSet, MaxHealth, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMHGZAttributeSet, Stamina, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMHGZAttributeSet, MaxStamina, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMHGZAttributeSet, StaminaRegenRate, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMHGZAttributeSet, StaminaDeductionRate, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMHGZAttributeSet, StaminaConsumptionRate, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMHGZAttributeSet, AttackPower, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMHGZAttributeSet, Defense, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMHGZAttributeSet, CriticalRate, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMHGZAttributeSet, StaggerMultiplier, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMHGZAttributeSet, MoveSpeedMultiplier, COND_None, REPNOTIFY_Always);
}

void UMHGZAttributeSet::ClampAttribute(const FGameplayAttribute& Attribute, float& NewValue) const
{
	if (Attribute == GetHealthAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxHealth());
	}
	else if (Attribute == GetMaxHealthAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 1.f, 200.f);
	}
	else if (Attribute == GetStaminaAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxStamina());
	}
	else if (Attribute == GetMaxStaminaAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 1.f, 200.f);
	}
	else if (Attribute == GetCriticalRateAttribute())
	{
		NewValue = FMath::Clamp(NewValue, -100.f, 100.f);
	}
	else if (Attribute == GetAttackPowerAttribute() || Attribute == GetDefenseAttribute())
	{
		NewValue = FMath::Max(NewValue, 0.f);
	}
	else if (Attribute == GetStaggerMultiplierAttribute())
	{
		NewValue = FMath::Max(NewValue, 0.f);
	}
	else if (Attribute == GetStaminaRegenRateAttribute() ||
		Attribute == GetStaminaDeductionRateAttribute() ||
		Attribute == GetStaminaConsumptionRateAttribute())
	{
		NewValue = FMath::Max(NewValue, 0.f);
	}
	else if (Attribute == GetMoveSpeedMultiplierAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.1f, 3.0f);
	}
}

void UMHGZAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);
	ClampAttribute(Attribute, NewValue);
}

void UMHGZAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);

	// Clamp 最终值
	// 注意：这里遍历所有修改过的属性做 Clamp
	FGameplayEffectContextHandle Context = Data.EffectSpec.GetContext();

	// ── MoveSpeedMultiplier → CMC 同步 ──
	if (Data.EvaluatedData.Attribute == GetMoveSpeedMultiplierAttribute())
	{
		if (AMHGZCharacter* Character = Cast<AMHGZCharacter>(Context.GetInstigator()))
		{
			Character->UpdateMaxWalkSpeed();
		}
	}

	// ── 硬直事件广播 ──
	// ★ I-6 修复：在 PostGameplayEffectExecute 中广播，非 ExecCalc 中
	const FGameplayTagContainer& DynamicTags = Data.EffectSpec.GetDynamicAssetTags();
	if (DynamicTags.HasTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.Event.HitStagger"))))
	{
		FGameplayEventData EventData;
		EventData.Instigator = Data.EffectSpec.GetContext().GetInstigator();
		EventData.Target = GetOwningActor();
		EventData.EventTag = FGameplayTag::RequestGameplayTag(TEXT("Combat.Event.HitStagger"));
		EventData.ContextHandle = Data.EffectSpec.GetContext();

		if (UAbilitySystemComponent* ASC = GetOwningActor()->FindComponentByClass<UAbilitySystemComponent>())
		{
			ASC->HandleGameplayEvent(
				FGameplayTag::RequestGameplayTag(TEXT("Combat.Event.HitStagger")),
				&EventData);
		}
	}
}

// ═══════════════════════════════════════════
// OnRep 回调（当前单机，预留网络接口）
// ═══════════════════════════════════════════
void UMHGZAttributeSet::OnRep_Health(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UMHGZAttributeSet, Health, OldValue); }
void UMHGZAttributeSet::OnRep_MaxHealth(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UMHGZAttributeSet, MaxHealth, OldValue); }
void UMHGZAttributeSet::OnRep_Stamina(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UMHGZAttributeSet, Stamina, OldValue); }
void UMHGZAttributeSet::OnRep_MaxStamina(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UMHGZAttributeSet, MaxStamina, OldValue); }
void UMHGZAttributeSet::OnRep_StaminaRegenRate(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UMHGZAttributeSet, StaminaRegenRate, OldValue); }
void UMHGZAttributeSet::OnRep_StaminaDeductionRate(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UMHGZAttributeSet, StaminaDeductionRate, OldValue); }
void UMHGZAttributeSet::OnRep_StaminaConsumptionRate(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UMHGZAttributeSet, StaminaConsumptionRate, OldValue); }
void UMHGZAttributeSet::OnRep_AttackPower(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UMHGZAttributeSet, AttackPower, OldValue); }
void UMHGZAttributeSet::OnRep_Defense(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UMHGZAttributeSet, Defense, OldValue); }
void UMHGZAttributeSet::OnRep_CriticalRate(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UMHGZAttributeSet, CriticalRate, OldValue); }
void UMHGZAttributeSet::OnRep_StaggerMultiplier(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UMHGZAttributeSet, StaggerMultiplier, OldValue); }
void UMHGZAttributeSet::OnRep_MoveSpeedMultiplier(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UMHGZAttributeSet, MoveSpeedMultiplier, OldValue); }
