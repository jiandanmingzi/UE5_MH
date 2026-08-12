// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActiveGameplayEffectHandle.h"
#include "GameplayTagContainer.h"
#include "MHGZWeaponResourceComponent.h"
#include "Res_InsectGlaive.generated.h"

class AIGMarkProjectile;
class AKinsect;
class UInsectGlaiveCombatConfig;
class UInsectGlaiveKinsectData;
class UMHGZMonsterHitzoneComponent;
struct FGameplayEffectRemovalInfo;
struct FKinsectFlightRequest;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnKinsectStaminaChanged, float, Current, float, Max);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnExtractTimeUpdated, FGameplayTag, ExtractColor, float, Ratio);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnTripleUpChanged);

UENUM(BlueprintType)
enum class EIGMarkClearReason : uint8
{
	Replaced,
	Expired,
	TargetInvalid,
	WeaponChanged,
	RuntimeShutdown
};

/**
 * 虫棍运行时资源。由 Character 的 RuntimeHost 动态创建；只保存当前 Pawn/武器实例状态，
 * 所有可调规则与资产引用均来自唯一 UInsectGlaiveCombatConfig。
 */
UCLASS(ClassGroup = (MHGZ), BlueprintType)
class MHGZ_API URes_InsectGlaive : public UMHGZWeaponResourceComponent
{
	GENERATED_BODY()

public:
	URes_InsectGlaive();

	virtual void InitializeRuntime(const FWeaponRuntimeContext& Context) override;
	virtual void ShutdownRuntime(EWeaponRuntimeEndReason Reason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	// ── 猎虫生命周期与飞行请求 ─────────────────────────────────
	bool OnWeaponEquipped(UInsectGlaiveKinsectData* Data, USceneComponent* AttachComponent,
		FName AttachSocket);
	void OnWeaponUnequipped();
	bool CanDeployKinsect(const FKinsectFlightRequest& Request) const;
	bool DeployKinsect(const FKinsectFlightRequest& Request);
	bool RecallKinsect();
	void OnKinsectReachedPlayer(FGameplayTag ExtractColor);
	bool IsRuntimeRequestCurrent(const FWeaponRuntimeToken& Token) const;

	UFUNCTION(BlueprintPure, Category = "MHGZ|IG")
	AKinsect* GetKinsectActor() const { return KinsectActor; }

	UFUNCTION(BlueprintPure, Category = "MHGZ|IG")
	float GetKinsectStamina() const { return KinsectStamina; }

	UFUNCTION(BlueprintPure, Category = "MHGZ|IG")
	float GetMaxKinsectStamina() const { return MaxKinsectStamina; }

	const UInsectGlaiveKinsectData* GetKinsectData() const { return KinsectData; }

	// ── 萃取与三灯 ─────────────────────────────────────────────
	UFUNCTION(BlueprintCallable, Category = "MHGZ|IG")
	bool ApplyExtract(FGameplayTag ExtractColor);

	UFUNCTION(BlueprintCallable, Category = "MHGZ|IG")
	bool ApplyExtractFromHitzone(const UMHGZMonsterHitzoneComponent* Hitzone);

	UFUNCTION(BlueprintPure, Category = "MHGZ|IG")
	bool HasExtract(FGameplayTag ExtractType) const;

	UFUNCTION(BlueprintPure, Category = "MHGZ|IG")
	bool IsTripleUpActive() const;

	UFUNCTION(BlueprintCallable, Category = "MHGZ|IG")
	bool TryConsumeTripleUpAtomic();

	/** 兼容旧调用名；语义为原子清空完整三灯，不恢复单灯。 */
	void ConsumeTripleUp() { TryConsumeTripleUpAtomic(); }

	/** 单灯消费仅供未来动作；三灯状态下拒绝部分消费。 */
	bool ConsumeExtract(FGameplayTag ExtractType);

	// ── 猎虫伤害 ───────────────────────────────────────────────
	bool ApplyKinsectDamage(const FHitResult& Hit, float MotionValue,
		const FGuid& HitInstanceID);
	float GetModifiedKinsectAttackPower() const;

	// ── 唯一虫印 ───────────────────────────────────────────────
	bool LaunchKinsectMark(const FWeaponAimSnapshot& AimSnapshot);
	bool SetKinsectMark(UMHGZMonsterHitzoneComponent* Hitzone,
		const FVector& ImpactPoint, AIGMarkProjectile* Projectile);
	void ClearKinsectMark(EIGMarkClearReason Reason);
	bool HasValidKinsectMark() const;
	bool GetKinsectMarkWorldLocation(FVector& OutLocation) const;

	// ── 武器资源成本 Reservation ───────────────────────────────
	virtual bool CanReserveCosts(const TArray<FWeaponResourceCostSpec>& Specs) const override;
	virtual bool TryReserveCosts(const FWeaponActionToken& ActionToken,
		const TArray<FWeaponResourceCostSpec>& Specs,
		FWeaponResourceCostReservation& OutReservation) override;
	virtual void ReleaseReservation(const FWeaponResourceCostReservation& Reservation) override;
	virtual void ConsumeReservedCosts(const FWeaponResourceCostReservation& Reservation) override;

	// Demo 禁用词条入口，但保留幂等清理兼容。
	virtual void ApplyEntryModifier(FGameplayTag AttributeTag, float Value,
		TEnumAsByte<EGameplayModOp::Type> Op) override;
	virtual void ClearAllEntryModifiers() override;

	UPROPERTY(BlueprintAssignable, Category = "IG|Delegate")
	FOnKinsectStaminaChanged OnKinsectStaminaChanged;

	UPROPERTY(BlueprintAssignable, Category = "IG|Delegate")
	FOnExtractTimeUpdated OnExtractTimeUpdated;

	UPROPERTY(BlueprintAssignable, Category = "IG|Delegate")
	FOnTripleUpChanged OnTripleUpChanged;

	const UInsectGlaiveCombatConfig* GetCombatConfig() const { return CombatConfig; }

private:
	bool IsLeafExtractTag(const FGameplayTag& ExtractColor) const;
	bool IsHandleActive(const FActiveGameplayEffectHandle& Handle) const;
	bool ApplySingleExtractEffect(FGameplayTag ExtractColor);
	void CheckAndActivateTripleUp();
	void HandleSingleExtractRemoved(const FGameplayEffectRemovalInfo& RemovalInfo,
		FGameplayTag Color, FActiveGameplayEffectHandle ExpectedHandle);
	void HandleTripleUpRemoved(const FGameplayEffectRemovalInfo& RemovalInfo,
		FActiveGameplayEffectHandle ExpectedHandle);
	void BroadcastExtractState();
	void SetKinsectActiveTag(bool bActive);
	void SetMarkActiveTag(bool bActive);
	void ClearAllResourceGameplayEffects();
	bool AreTripleCostSpecs(const TArray<FWeaponResourceCostSpec>& Specs) const;

	UPROPERTY()
	TObjectPtr<UInsectGlaiveCombatConfig> CombatConfig;

	UPROPERTY()
	TObjectPtr<AKinsect> KinsectActor;

	UPROPERTY()
	TObjectPtr<UInsectGlaiveKinsectData> KinsectData;

	UPROPERTY()
	TWeakObjectPtr<USceneComponent> KinsectAttachComponent;

	FName KinsectAttachSocket = NAME_None;
	float KinsectStamina = 0.f;
	float MaxKinsectStamina = 0.f;
	bool bDepletionEdgeTriggered = false;

	UPROPERTY()
	TMap<FGameplayTag, FActiveGameplayEffectHandle> ActiveExtractHandles;

	UPROPERTY()
	FActiveGameplayEffectHandle TripleUpHandle;

	bool bExtractTransitionGuard = false;
	bool bRuntimeShuttingDown = false;

	UPROPERTY()
	TWeakObjectPtr<UMHGZMonsterHitzoneComponent> ActiveMarkHitzone;

	UPROPERTY()
	TWeakObjectPtr<AIGMarkProjectile> ActiveMarkProjectile;

	FVector ActiveMarkLocalPoint = FVector::ZeroVector;
	FTimerHandle MarkExpiryTimer;
	uint64 MarkSerial = 0;

	FWeaponOwnedTagToken KinsectActiveTagToken;
	FWeaponOwnedTagToken MarkActiveTagToken;

	uint64 NextReservationID = 1;
	TMap<uint64, FActiveGameplayEffectHandle> TripleReservations;
};
