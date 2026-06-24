// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "MHGZAttributeSet.generated.h"

// ── 属性访问宏 ──
#define ATTRIBUTE_ACCESSORS(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

/**
 * UMHGZAttributeSet — 角色属性集
 * 挂载到 PlayerState（ASC 子对象）
 */
UCLASS()
class UMHGZAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UMHGZAttributeSet();

	// ── 属性复制 ──
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ── Clamp & 事件 ──
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;

	// ═══════════════════════════════════════════
	// 生命值
	// ═══════════════════════════════════════════
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Health", ReplicatedUsing = OnRep_Health)
	FGameplayAttributeData Health;
	ATTRIBUTE_ACCESSORS(UMHGZAttributeSet, Health)

	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Health", ReplicatedUsing = OnRep_MaxHealth)
	FGameplayAttributeData MaxHealth;
	ATTRIBUTE_ACCESSORS(UMHGZAttributeSet, MaxHealth)

	// ═══════════════════════════════════════════
	// 耐力值
	// ═══════════════════════════════════════════
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Stamina", ReplicatedUsing = OnRep_Stamina)
	FGameplayAttributeData Stamina;
	ATTRIBUTE_ACCESSORS(UMHGZAttributeSet, Stamina)

	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Stamina", ReplicatedUsing = OnRep_MaxStamina)
	FGameplayAttributeData MaxStamina;
	ATTRIBUTE_ACCESSORS(UMHGZAttributeSet, MaxStamina)

	// 耐力恢复倍率
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Stamina", ReplicatedUsing = OnRep_StaminaRegenRate)
	FGameplayAttributeData StaminaRegenRate;
	ATTRIBUTE_ACCESSORS(UMHGZAttributeSet, StaminaRegenRate)

	// 单次动作扣耐倍率（闪避/攻击）
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Stamina", ReplicatedUsing = OnRep_StaminaDeductionRate)
	FGameplayAttributeData StaminaDeductionRate;
	ATTRIBUTE_ACCESSORS(UMHGZAttributeSet, StaminaDeductionRate)

	// 持续耗耐倍率（奔跑/蓄力）
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Stamina", ReplicatedUsing = OnRep_StaminaConsumptionRate)
	FGameplayAttributeData StaminaConsumptionRate;
	ATTRIBUTE_ACCESSORS(UMHGZAttributeSet, StaminaConsumptionRate)

	// ═══════════════════════════════════════════
	// 战斗属性
	// ═══════════════════════════════════════════
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Combat", ReplicatedUsing = OnRep_AttackPower)
	FGameplayAttributeData AttackPower;
	ATTRIBUTE_ACCESSORS(UMHGZAttributeSet, AttackPower)

	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Combat", ReplicatedUsing = OnRep_Defense)
	FGameplayAttributeData Defense;
	ATTRIBUTE_ACCESSORS(UMHGZAttributeSet, Defense)

	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Combat", ReplicatedUsing = OnRep_CriticalRate)
	FGameplayAttributeData CriticalRate;
	ATTRIBUTE_ACCESSORS(UMHGZAttributeSet, CriticalRate)

	/** 破坏值倍率——攻击方属性 */
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Combat", ReplicatedUsing = OnRep_StaggerMultiplier)
	FGameplayAttributeData StaggerMultiplier;
	ATTRIBUTE_ACCESSORS(UMHGZAttributeSet, StaggerMultiplier)

	// ═══════════════════════════════════════════
	// 移速
	// ═══════════════════════════════════════════
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Movement", ReplicatedUsing = OnRep_MoveSpeedMultiplier)
	FGameplayAttributeData MoveSpeedMultiplier;
	ATTRIBUTE_ACCESSORS(UMHGZAttributeSet, MoveSpeedMultiplier)

	// ═══════════════════════════════════════════
	// 网络同步回调
	// ═══════════════════════════════════════════
	UFUNCTION()
	virtual void OnRep_Health(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	virtual void OnRep_MaxHealth(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	virtual void OnRep_Stamina(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	virtual void OnRep_MaxStamina(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	virtual void OnRep_StaminaRegenRate(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	virtual void OnRep_StaminaDeductionRate(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	virtual void OnRep_StaminaConsumptionRate(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	virtual void OnRep_AttackPower(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	virtual void OnRep_Defense(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	virtual void OnRep_CriticalRate(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	virtual void OnRep_StaggerMultiplier(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	virtual void OnRep_MoveSpeedMultiplier(const FGameplayAttributeData& OldValue);

private:
	/** Clamp 属性值到合法范围 */
	void ClampAttribute(const FGameplayAttribute& Attribute, float& NewValue) const;
};
