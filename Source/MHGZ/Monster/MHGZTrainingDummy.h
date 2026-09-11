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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MHGZ|Dummy", meta = (ClampMin = "1.0"))
	float DummyMaxHealth = 1000.f;

	UPROPERTY(BlueprintAssignable, Category = "MHGZ|Dummy")
	FOnDummyHealthChanged OnHealthChanged;

	/** Fired for Blueprint-only telegraph/audio extensions when the ring appears/disappears. */
	UPROPERTY(BlueprintAssignable, Category = "MHGZ|Dummy|Fire Ring")
	FOnDummyFireRingStateChanged OnFireRingStateChanged;

	/** The requested 1 m diameter / 2.25 m high white-orange-red cylinder. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MHGZ|Dummy|Visual")
	TObjectPtr<UStaticMeshComponent> WhiteSegmentVisual;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MHGZ|Dummy|Visual")
	TObjectPtr<UStaticMeshComponent> OrangeSegmentVisual;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MHGZ|Dummy|Visual")
	TObjectPtr<UStaticMeshComponent> RedSegmentVisual;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MHGZ|Dummy|Visual")
	TObjectPtr<UPointLightComponent> WhiteExtractLight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MHGZ|Dummy|Visual")
	TObjectPtr<UPointLightComponent> OrangeExtractLight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MHGZ|Dummy|Visual")
	TObjectPtr<UPointLightComponent> RedExtractLight;

	/** Visible only during an active emission; component details remain editable in BP. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MHGZ|Dummy|Fire Ring")
	TObjectPtr<UInstancedStaticMeshComponent> FireRingVisual;

	/** Horizontal ring of fire particles. The effect is activated only while the ring is active. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MHGZ|Dummy|Fire Ring")
	TObjectPtr<UParticleSystemComponent> FireRingFX;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MHGZ|Dummy|Fire Ring")
	TObjectPtr<UPointLightComponent> FireRingLight;

	/** 应用配置 */
	UFUNCTION(BlueprintCallable, Category = "MHGZ|Dummy")
	void ApplyConfig(UMHGZDummyConfig* Config);

	UFUNCTION(BlueprintPure, Category = "MHGZ|Dummy")
	float GetCurrentHealth() const;

	UFUNCTION(BlueprintPure, Category = "MHGZ|Dummy")
	float GetMaxHealth() const;

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
