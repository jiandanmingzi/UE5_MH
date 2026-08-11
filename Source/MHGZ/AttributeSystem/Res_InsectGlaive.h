// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MHGZWeaponResourceComponent.h"
#include "GameplayTagContainer.h"
#include "ActiveGameplayEffectHandle.h"
#include "Res_InsectGlaive.generated.h"

class AKinsect;
class UInsectGlaiveKinsectData;
class UMHGZMonsterHitzoneComponent;
class UGameplayEffect;
class USoundBase;
class UAbilitySystemComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnKinsectStaminaChanged, float, Current, float, Max);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnExtractTimeUpdated, FGameplayTag, ExtractColor, float, Ratio);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnTripleUpChanged);

/**
 * URes_InsectGlaive — 虫棍资源组件
 * 由 Character RuntimeHost 动态创建。管理猎虫生命周期、猎虫耐力、三灯萃取、灯消耗。
 */
UCLASS(ClassGroup = (MHGZ), BlueprintType)
class URes_InsectGlaive : public UMHGZWeaponResourceComponent
{
	GENERATED_BODY()

public:
	URes_InsectGlaive();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	virtual void ShutdownRuntime(EWeaponRuntimeEndReason Reason) override;

	// ═══════════════════════════════════════════
	// 猎虫生命周期
	// ═══════════════════════════════════════════

	/** 装备虫棍时——Spawn 猎虫 */
	void OnWeaponEquipped(UInsectGlaiveKinsectData* Data, USceneComponent* ArmSocket);

	/** 卸下虫棍时——销毁猎虫 */
	void OnWeaponUnequipped();

	/** 送虫——瞄准送虫（沿相机方向） */
	void DeployKinsect();

	/** 送虫——沿指定方向（收刀 RT 直飞） */
	void DeployKinsectAlongDirection(FVector Direction, float Distance);

	/** 召回猎虫 */
	void RecallKinsect();

	/** 猎虫回到玩家回调 */
	void OnKinsectReachedPlayer(FGameplayTag ExtractColor);

	// ═══════════════════════════════════════════
	// 萃取系统
	// ═══════════════════════════════════════════

	/**
	 * 部位→萃取颜色映射（虚函数，支持不同猎虫品种覆写）
	 */
	UFUNCTION(BlueprintCallable, Category = "MHGZ|IG")
	virtual FGameplayTag MapHitzoneToExtract(FGameplayTag HitzoneTag) const;

	/** ★ 静态版本——UMHGZAimComponent 共用同一份映射 */
	UFUNCTION(BlueprintCallable, Category = "MHGZ|IG")
	static FGameplayTag StaticMapHitzoneToExtract(FGameplayTag HitzoneTag);

	/** Apply 萃取（召回时调用） */
	void ApplyExtract(FGameplayTag ExtractColor);

	/** 消耗指定灯 */
	void ConsumeExtract(FGameplayTag ExtractType);

	/** 消耗三灯 */
	void ConsumeTripleUp();

	/** 检查是否持有指定灯 */
	bool HasExtract(FGameplayTag ExtractType) const;

	/** 检查是否处于三灯 */
	bool IsTripleUpActive() const { return bTripleUpActive; }

	// ═══════════════════════════════════════════
	// 猎虫伤害
	// ═══════════════════════════════════════════

	/** Apply 猎虫伤害——通过玩家 ASC 的统一 GE 管道 */
	void ApplyKinsectDamage(UMHGZMonsterHitzoneComponent* Hitzone, AActor* Monster, float MotionValue);

	/** 获取修正后的猎虫攻击力 */
	float GetModifiedKinsectAttackPower() const;

	// ═══════════════════════════════════════════
	// 词条修饰器覆写
	// ═══════════════════════════════════════════

	virtual void ApplyEntryModifier(FGameplayTag AttributeTag, float Value, TEnumAsByte<EGameplayModOp::Type> Op) override;
	virtual void ClearAllEntryModifiers() override;

	// ═══════════════════════════════════════════
	// 音效
	// ═══════════════════════════════════════════

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IG|Audio")
	TObjectPtr<USoundBase> ExtractCollectedSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IG|Audio")
	TObjectPtr<USoundBase> TripleUpActivatedSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IG|Audio")
	TObjectPtr<USoundBase> TripleUpExpiredSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IG|Audio")
	TObjectPtr<USoundBase> KinsectDepletedSound;

	// ═══════════════════════════════════════════
	// Delegate
	// ═══════════════════════════════════════════

	UPROPERTY(BlueprintAssignable, Category = "IG|Delegate")
	FOnKinsectStaminaChanged OnKinsectStaminaChanged;

	UPROPERTY(BlueprintAssignable, Category = "IG|Delegate")
	FOnExtractTimeUpdated OnExtractTimeUpdated;

	UPROPERTY(BlueprintAssignable, Category = "IG|Delegate")
	FOnTripleUpChanged OnTripleUpChanged;

	// ═══════════════════════════════════════════
	// 配置
	// ═══════════════════════════════════════════

	/** 三灯固定时长（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IG|Config")
	float TripleUpDuration = 90.f;

protected:
	/** 检查并激活三灯 */
	void CheckAndActivateTripleUp();

	/** 重新 Apply 剩余时长的单灯 GE（三灯被破后） */
	void ReapplyRemainingExtracts(const TArray<FGameplayTag>& RemainingColors);

private:
	// ── 猎虫 ──
	UPROPERTY()
	TObjectPtr<AKinsect> KinsectActor;

	UPROPERTY()
	TObjectPtr<UInsectGlaiveKinsectData> KinsectData;

	bool bKinsectDeployed = false;

	/** 耐力归零强制召回中——不可被放虫打断 */
	bool bForceRecalling = false;

	// ── 猎虫耐力 ──
	float KinsectStamina = 100.f;
	float MaxKinsectStamina = 100.f;

	// 耐力倍率（词条修饰后）
	float KinsectRegenRateMultiplier = 1.0f;
	float HoverDrainRateMultiplier = 1.0f;
	float FlightDrainRateMultiplier = 1.0f;

	// ── 三灯状态 ──
	bool bTripleUpActive = false;

	UPROPERTY()
	TMap<FGameplayTag, FActiveGameplayEffectHandle> ActiveExtractHandles;

	UPROPERTY()
	FActiveGameplayEffectHandle TripleUpHandle;

	// 单灯基础时长
	static constexpr float WHITE_DURATION = 90.f;
	static constexpr float ORANGE_DURATION = 120.f;
	static constexpr float RED_DURATION = 60.f;
};
