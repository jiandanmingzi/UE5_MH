// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "GameplayEffectTypes.h"
#include "WeaponRuntime/MHGZWeaponRuntimeTypes.h"
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
 * UMHGZWeaponResourceComponent —— 武器资源基类
 * M2 起由 RuntimeHost 创建，挂在 Character/Pawn 上；切换武器时旧状态全部清空。
 * 与 ASC 同属一个运行期，通过只读 RuntimeContext 访问 ASC/Token/WeaponDefinition，
 * 不再假设 Owner 是 PlayerState。
 */
UCLASS(ClassGroup = (MHGZ), BlueprintType, Abstract)
class UMHGZWeaponResourceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMHGZWeaponResourceComponent();

	// ----------------------------------------------------------------------
	// M2 生命周期（由 RuntimeHost 调用；均幂等）
	// ----------------------------------------------------------------------
	/** 注入只读运行上下文（Token/ASC/WeaponDefinition），并清空上一把武器的词条修饰器。 */
	virtual void InitializeRuntime(const FWeaponRuntimeContext& Context);

	/** 换武器/关停时调用：清空全部词条修饰器。 */
	virtual void ShutdownRuntime(EWeaponRuntimeEndReason Reason);

	/** 只读运行上下文访问。 */
	const FWeaponRuntimeContext& GetRuntimeContext() const { return RuntimeContext; }

	// ----------------------------------------------------------------------
	// 资源接口（子类覆写）
	// ----------------------------------------------------------------------
	UFUNCTION(BlueprintCallable, Category = "MHGZ|WeaponResource")
	virtual float GetCurrentValue() const { return 0.f; }

	UFUNCTION(BlueprintCallable, Category = "MHGZ|WeaponResource")
	virtual float GetMaxValue() const { return 1.f; }

	UFUNCTION(BlueprintCallable, Category = "MHGZ|WeaponResource")
	virtual bool Consume(float Amount) { return false; }

	UFUNCTION(BlueprintCallable, Category = "MHGZ|WeaponResource")
	virtual void Restore(float Amount) {}

	// ----------------------------------------------------------------------
	// 武器资源成本预留（M1 基础；子类实现具体资源语义）
	// ----------------------------------------------------------------------
	/** 纯查询：当前能否满足 Specs 的全部预留需求。默认仅接受空 Specs。 */
	virtual bool CanReserveCosts(const TArray<FWeaponResourceCostSpec>& Specs) const;

	/**
	 * 尝试为一次 Action 预留资源成本。
	 * 默认实现无副作用：空 Specs 时返回 true 并产出无效 Reservation，非空时返回 false。
	 */
	virtual bool TryReserveCosts(
		const FWeaponActionToken& ActionToken,
		const TArray<FWeaponResourceCostSpec>& Specs,
		FWeaponResourceCostReservation& OutReservation);

	/** 释放未消耗的预留。默认无操作。 */
	virtual void ReleaseReservation(const FWeaponResourceCostReservation& Reservation);

	/** 结算已预留的成本。默认无操作。 */
	virtual void ConsumeReservedCosts(const FWeaponResourceCostReservation& Reservation);

	UFUNCTION(BlueprintCallable, Category = "MHGZ|WeaponResource")
	float GetNormalizedValue() const
	{
		const float Max = GetMaxValue();
		return Max > 0.f ? GetCurrentValue() / Max : 0.f;
	}

	// ----------------------------------------------------------------------
	// 词条修饰器
	// ----------------------------------------------------------------------
	/**
	 * 接收装备词条修改倍率参数
	 * 按 AttributeTag 前缀路由到内部倍率参数
	 */
	virtual void ApplyEntryModifier(FGameplayTag AttributeTag, float Value, TEnumAsByte<EGameplayModOp::Type> Op);

	/** 全量重建时清空所有修饰器 */
	virtual void ClearAllEntryModifiers();

	/** 求值活跃修饰器——对基础参数应用所有已注册修饰器 */
	virtual float GetModifiedParam(FName ParamName) const;

	// ----------------------------------------------------------------------
	// 工具方法
	// ----------------------------------------------------------------------
	/** 播放资源音效（UI 反馈用 2D 音效） */
	UFUNCTION(BlueprintCallable, Category = "MHGZ|WeaponResource")
	void PlayResourceSound(USoundBase* Sound);

	// ----------------------------------------------------------------------
	// 武器识别
	// ----------------------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MHGZ|WeaponResource")
	FGameplayTag WeaponTypeTag;

protected:
	/** 获取玩家 ASC：优先使用 RuntimeContext，兼容旧 PlayerState 挂载路径。 */
	UAbilitySystemComponent* GetPlayerASC() const;

	/** 由 RuntimeHost 注入的只读运行上下文。 */
	UPROPERTY()
	FWeaponRuntimeContext RuntimeContext;

	// ----------------------------------------------------------------------
	// 活跃修饰器
	// ----------------------------------------------------------------------
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MHGZ|WeaponResource|Modifiers")
	TMap<FGameplayTag, FActiveModifier> ActiveModifiers;

	// ----------------------------------------------------------------------
	// 基础参数（子类覆写用）
	// ----------------------------------------------------------------------
	TMap<FName, float> BaseParams;
};