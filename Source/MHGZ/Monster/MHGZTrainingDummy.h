// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MHGZMonsterBase.h"
#include "ActionSystem/MHGZIncomingHitResolverComponent.h"
#include "MHGZTrainingDummy.generated.h"

struct FOnAttributeChangeData;
class UMHGZDummyConfig;
class UInstancedStaticMeshComponent;
class UParticleSystemComponent;
class UPointLightComponent;
class UStaticMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDummyHealthChanged,
	float, CurrentHealth, float, MaxHealth);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDummyFireRingStateChanged,
	bool, bActive);

/**
 * AMHGZTrainingDummy — 训练木桩
 * 简化怪物——无 AI；含可重复的木桩火圈测试攻击。
 */
UCLASS()
class AMHGZTrainingDummy : public AMHGZMonsterBase
{
	GENERATED_BODY()

public:
	AMHGZTrainingDummy();

	/** 放到关卡后直接配置；BeginPlay 自动生成 Hitzone。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MHGZ|Dummy")
	TObjectPtr<UMHGZDummyConfig> DummyConfig;

	/** 木桩有限生命模式的默认最大生命；无限生命模式仍保留正常命中反馈。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MHGZ|Dummy", meta = (ClampMin = "1.0"))
	float DummyMaxHealth = 1000.f;

	/** 是否保持训练木桩生命为满值；开启后命中仍结算并产生正常反馈。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MHGZ|Dummy|Health")
	bool bInfiniteHealth = true;

	/** 生命变化事件，向已绑定的界面或表现通知最新生命值。 */
	UPROPERTY(BlueprintAssignable, Category = "MHGZ|Dummy")
	FOnDummyHealthChanged OnHealthChanged;

	/** 火圈出现或消失时广播，供蓝图扩展预警、音效或表现。 */
	UPROPERTY(BlueprintAssignable, Category = "MHGZ|Dummy|Fire Ring")
	FOnDummyFireRingStateChanged OnFireRingStateChanged;

	/** 木桩白色精华区域的可见模型组件；实例可覆盖尺寸或形状。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MHGZ|Dummy|Visual")
	TObjectPtr<UStaticMeshComponent> WhiteSegmentVisual;

	/** 木桩橙色精华区域的可见模型组件。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MHGZ|Dummy|Visual")
	TObjectPtr<UStaticMeshComponent> OrangeSegmentVisual;

	/** 木桩红色精华区域的可见模型组件。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MHGZ|Dummy|Visual")
	TObjectPtr<UStaticMeshComponent> RedSegmentVisual;

	/** 白色精华区域的辅助灯光组件。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MHGZ|Dummy|Visual")
	TObjectPtr<UPointLightComponent> WhiteExtractLight;

	/** 橙色精华区域的辅助灯光组件。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MHGZ|Dummy|Visual")
	TObjectPtr<UPointLightComponent> OrangeExtractLight;

	/** 红色精华区域的辅助灯光组件。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MHGZ|Dummy|Visual")
	TObjectPtr<UPointLightComponent> RedExtractLight;

	/** 仅火圈有效期间显示的引导网格组件，细节可在蓝图配置。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MHGZ|Dummy|Fire Ring")
	TObjectPtr<UInstancedStaticMeshComponent> FireRingVisual;

	/** 仅火圈有效期间激活的水平环状火焰粒子组件。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MHGZ|Dummy|Fire Ring")
	TObjectPtr<UParticleSystemComponent> FireRingFX;

	/** 火圈有效期间使用的辅助灯光组件。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MHGZ|Dummy|Fire Ring")
	TObjectPtr<UPointLightComponent> FireRingLight;

	/** 应用配置 */
	UFUNCTION(BlueprintCallable, Category = "MHGZ|Dummy")
	void ApplyConfig(UMHGZDummyConfig* Config);

	UFUNCTION(BlueprintPure, Category = "MHGZ|Dummy")
	float GetCurrentHealth() const;

	UFUNCTION(BlueprintPure, Category = "MHGZ|Dummy")
	float GetMaxHealth() const;

	UFUNCTION(BlueprintPure, Category = "MHGZ|Dummy|Health")
	bool HasInfiniteHealth() const { return bInfiniteHealth; }

	/** Runtime toggle; does not mutate the shared DummyConfig DataAsset. */
	UFUNCTION(BlueprintCallable, Category = "MHGZ|Dummy|Fire Ring")
	void SetFireRingEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "MHGZ|Dummy|Fire Ring")
	bool IsFireRingEnabled() const { return bFireRingRuntimeEnabled; }

	UFUNCTION(BlueprintPure, Category = "MHGZ|Dummy|Fire Ring")
	bool IsFireRingActive() const { return bFireRingActive; }

	/** Emits the ring immediately when runtime-enabled; useful for Blueprint tests. */
	UFUNCTION(BlueprintCallable, Category = "MHGZ|Dummy|Fire Ring")
	void TriggerFireRingNow();

	/**
	 * Deterministic counter-test attack: builds a fixed real FHitResult against the
	 * target character's capsule and submits it to the target IncomingHitResolver.
	 * Re-submitting the same AttackInstanceID is settled at most once by the resolver.
	 */
	UFUNCTION(BlueprintCallable, Category = "MHGZ|Dummy")
	EIncomingHitSubmitResult SubmitCounterTestAttack(
		ACharacter* TargetCharacter, FGuid AttackInstanceID);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnConstruction(const FTransform& Transform) override;

private:
	const struct FDummyFireRingConfig* GetFireRingConfig() const;
	void RefreshFireRingSchedule();
	void BeginFireRing();
	void EndFireRing();
	void ApplyFireRingHit();
	bool IsCharacterInsideFireRing(const ACharacter& Character) const;
	EIncomingHitSubmitResult SubmitFireRingHit(ACharacter& TargetCharacter, FGuid AttackInstanceID);
	void SetFireRingVisualActive(bool bActive);
	void RebuildFireRingVisuals();
	void HandleHealthChanged(const FOnAttributeChangeData& ChangeData);

	FTimerHandle FireRingEmissionTimer;
	FTimerHandle FireRingEndTimer;
	FTimerHandle FireRingHitTimer;
	bool bFireRingRuntimeEnabled = false;
	bool bFireRingActive = false;
};
