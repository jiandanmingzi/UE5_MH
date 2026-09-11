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
class UParticleSystemComponent;
struct FGameplayEffectRemovalInfo;
struct FKinsectFlightRequest;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnKinsectStaminaChanged, float, Current, float, Max);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnExtractTimeUpdated, FGameplayTag, ExtractColor, float, Ratio);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnTripleUpChanged);

/** 唯一允许建立舞踏层数的来源；不能用 CantAttack/CantDodge 推导。 */
UENUM(BlueprintType)
enum class EIGDanceSource : uint8
{
	None,
	KinsectSlash,
	AdvancingCounter
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDanceStateChanged, int32, Stacks,
	EIGDanceSource, Source);

/** 舞踏层数的明确清空原因；动作层不得直接写计数。 */
UENUM(BlueprintType)
enum class EIGDanceClearReason : uint8
{
	Landed,
	Hit,
	Sheathed,
	Unequipped,
	DescendingThrust,
	DivingWyvern,
	RuntimeShutdown
};

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
	/**
	 * 用一次已经成功提交伤害的近战 Hitzone 命中建立/替换虫印。
	 * 近战虫印不生成投射物 Actor；Resource 仍是两种来源唯一的生命周期所有者。
	 */
	bool SetKinsectMarkFromMeleeHit(const FHitResult& Hit);
	bool SetKinsectMark(UMHGZMonsterHitzoneComponent* Hitzone,
		const FVector& ImpactPoint, AIGMarkProjectile* Projectile);
	void ClearKinsectMark(EIGMarkClearReason Reason);
	bool HasValidKinsectMark() const;
	bool GetKinsectMarkWorldLocation(FVector& OutLocation) const;

	// ── 舞踏 ───────────────────────────────────────────────────
	/**
	 * 以唯一允许的来源增加一层舞踏。达到 MaxDanceStacks 后保持上限，
	 * 仍更新来源以便后续空中动作按来源分流。
	 */
	UFUNCTION(BlueprintCallable, Category = "MHGZ|IG|Dance")
	bool AddDanceStack(EIGDanceSource Source);

	/** 由落地、受击、收刀、卸装和指定空中动作走统一入口清空。 */
	UFUNCTION(BlueprintCallable, Category = "MHGZ|IG|Dance")
	void ClearDanceStacks(EIGDanceClearReason Reason);

	UFUNCTION(BlueprintPure, Category = "MHGZ|IG|Dance")
	int32 GetDanceStacks() const { return DanceStacks; }

	UFUNCTION(BlueprintPure, Category = "MHGZ|IG|Dance")
	EIGDanceSource GetDanceSource() const { return DanceSource; }

	/** 当前层数对应的配置倍率；无效/未配置时安全回退 1.0。 */
	UFUNCTION(BlueprintPure, Category = "MHGZ|IG|Dance")
	float GetDanceDamageMultiplier() const;

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

	UPROPERTY(BlueprintAssignable, Category = "IG|Delegate")
	FOnDanceStateChanged OnDanceStateChanged;

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
	bool SetKinsectMarkInternal(UMHGZMonsterHitzoneComponent* Hitzone,
		const FVector& ImpactPoint, AIGMarkProjectile* Projectile);
	/** 表现只由 Resource 的成功虫印生命周期驱动，不由武器 Sweep 预触发。 */
	void SpawnKinsectMarkEffect();
	void ClearKinsectMarkEffect();

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
	int32 DanceStacks = 0;
	EIGDanceSource DanceSource = EIGDanceSource::None;

	UPROPERTY()
	TWeakObjectPtr<UMHGZMonsterHitzoneComponent> ActiveMarkHitzone;

	UPROPERTY()
	TWeakObjectPtr<AIGMarkProjectile> ActiveMarkProjectile;

	UPROPERTY(Transient)
	TObjectPtr<UParticleSystemComponent> ActiveMarkEffect;

	FVector ActiveMarkLocalPoint = FVector::ZeroVector;
	FTimerHandle MarkExpiryTimer;
	uint64 MarkSerial = 0;
	/** 仅远程虫印持有投射物；近战虫印不能因没有 Projectile 被误清理。 */
	bool bActiveMarkUsesProjectile = false;

	FWeaponOwnedTagToken KinsectActiveTagToken;
	FWeaponOwnedTagToken MarkActiveTagToken;

	uint64 NextReservationID = 1;
	TMap<uint64, FActiveGameplayEffectHandle> TripleReservations;
};
