// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "GameplayEffectTypes.h"
#include "MHGZWeaponResourceComponent.generated.h"

class UAbilitySystemComponent;
class USoundBase;

/**
 * 武器资源修饰器
 */
USTRUCT(BlueprintType)
struct FActiveModifier
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FGameplayTag AttributeTag;

	UPROPERTY(BlueprintReadOnly)
	float Value = 1.0f;

	UPROPERTY(BlueprintReadOnly)
	TEnumAsByte<EGameplayModOp::Type> Op = EGameplayModOp::Multiplicitive;
};

/**
 * UMHGZWeaponResourceComponent — 武器资源基类
 * 挂载到 PlayerState。动态创建/销毁，切换武器时旧状态全部清空。
 * 与 ASC 同宿主，零跨 Actor 引用。
 */
UCLASS(ClassGroup = (MHGZ), BlueprintType, Abstract)
class UMHGZWeaponResourceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMHGZWeaponResourceComponent();

	// ── 资源接口（子类覆写） ──
	UFUNCTION(BlueprintCallable, Category = "MHGZ|WeaponResource")
	virtual float GetCurrentValue() const { return 0.f; }

	UFUNCTION(BlueprintCallable, Category = "MHGZ|WeaponResource")
	virtual float GetMaxValue() const { return 1.f; }

	UFUNCTION(BlueprintCallable, Category = "MHGZ|WeaponResource")
	virtual bool Consume(float Amount) { return false; }

	UFUNCTION(BlueprintCallable, Category = "MHGZ|WeaponResource")
	virtual void Restore(float Amount) {}

	UFUNCTION(BlueprintCallable, Category = "MHGZ|WeaponResource")
	float GetNormalizedValue() const
	{
		const float Max = GetMaxValue();
		return Max > 0.f ? GetCurrentValue() / Max : 0.f;
	}

	// ── 词条修饰器 ──
	/**
	 * 接收装备词条修改倍率参数
	 * 按 AttributeTag 前缀路由到内部倍率参数
	 */
	virtual void ApplyEntryModifier(FGameplayTag AttributeTag, float Value, TEnumAsByte<EGameplayModOp::Type> Op);

	/** 全量重建时清空所有修饰器 */
	virtual void ClearAllEntryModifiers();

	/** 求值活跃修饰器——对基础参数应用所有已注册修饰器 */
	virtual float GetModifiedParam(FName ParamName) const;

	// ── 工具方法 ──
	/** 播放资源音效（UI 反馈用 2D 音效） */
	UFUNCTION(BlueprintCallable, Category = "MHGZ|WeaponResource")
	void PlayResourceSound(USoundBase* Sound);

	// ── 武器识别 ──
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MHGZ|WeaponResource")
	FGameplayTag WeaponTypeTag;

protected:
	/** 获取玩家 ASC */
	UAbilitySystemComponent* GetPlayerASC() const;

	// ── 活跃修饰器 ──
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MHGZ|WeaponResource|Modifiers")
	TMap<FGameplayTag, FActiveModifier> ActiveModifiers;

	// ── 基础参数（子类覆写用） ──
	TMap<FName, float> BaseParams;
};
