// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActiveGameplayEffectHandle.h"
#include "GameplayTagContainer.h"
#include "MHGZWeaponResourceComponent.h"
#include "InsectGlaive/IGPowderTypes.h"
#include "InsectGlaive/IGResourceUITypes.h"
#include "Res_InsectGlaive.generated.h"

class AIGMarkProjectile;
class AIGPowderActor;
class AKinsect;
class UAbilitySystemComponent;
class UInsectGlaiveCombatConfig;
class UInsectGlaiveKinsectData;
class UMHGZMonsterHitzoneComponent;
class USkeletalMeshComponent;
class UParticleSystemComponent;
struct FGameplayEffectRemovalInfo;
struct FOnAttributeChangeData;
struct FWeaponAimSnapshot;
struct FKinsectFlightRequest;
enum class EKinsectFlightEndReason : uint8;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnKinsectStaminaChanged, float, Current, float, Max);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnExtractTimeUpdated, FGameplayTag, ExtractColor, float, Ratio);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnTripleUpChanged);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDanceStateChanged, int32, Stacks,
	EIGDanceSource, Source);

DECLARE_MULTICAST_DELEGATE_OneParam(FOnIGResourceUIChanged,
	const FIGResourceUISnapshot&);

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
	TargetDead,
	PowderVortex,
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
	/** Send/Recall在动作播放期持有的玩家命令租约；仅在动作已通过预检后获取。 */
	bool BeginKinsectPlayerCommandLease(const FWeaponActionToken& ActionToken);
	void EndKinsectPlayerCommandLease(const FWeaponActionToken& ActionToken);
	/** 将当前动作持有的武器 Socket 附着原子转换为飞行。 */
	bool DeployKinsectFromActionAttachment(const FWeaponActionToken& ActionToken,
		const FKinsectFlightRequest& Request);
	bool RecallKinsect(EKinsectCommandSource Source = EKinsectCommandSource::Player);
	/** 招式临时占用真实猎虫；租约由精确 ActionToken 所有。 */
	bool BeginKinsectActionAttachment(const FWeaponActionToken& ActionToken,
		USkeletalMeshComponent* WeaponMesh, FName SocketName);
	bool IsKinsectActionAttachmentCurrent(const FWeaponActionToken& ActionToken) const;
	bool EndKinsectActionAttachmentAndReturn(const FWeaponActionToken& ActionToken,
		EKinsectCommandSource Source = EKinsectCommandSource::Action);
	void OnKinsectReachedPlayer(const FGameplayTagContainer& ExtractColors, uint64 CommandSerial = 0,
		EKinsectCommandSource Source = EKinsectCommandSource::None);
	void OnKinsectFlightEnded(const FKinsectFlightRequest& Request,
		EKinsectFlightEndReason Reason);
	bool IsKinsectCommandCurrent(const FKinsectFlightRequest& Request) const;
	bool IsKinsectCommandCurrent(uint64 CommandSerial,
		EKinsectCommandSource Source, uint64 ExpectedMarkSerial,
		const FWeaponRuntimeToken& RuntimeToken) const;
	bool IsRuntimeRequestCurrent(const FWeaponRuntimeToken& Token) const;

	UFUNCTION(BlueprintPure, Category = "MHGZ|IG")
	AKinsect* GetKinsectActor() const { return KinsectActor; }

	UFUNCTION(BlueprintPure, Category = "MHGZ|IG")
	float GetKinsectStamina() const { return KinsectStamina; }

	UFUNCTION(BlueprintPure, Category = "MHGZ|IG")
	float GetMaxKinsectStamina() const { return MaxKinsectStamina; }

	FIGResourceUISnapshot GetUIState() const;
	FOnIGResourceUIChanged& OnIGResourceUIChanged() { return IGResourceUIChanged; }

	const UInsectGlaiveKinsectData* GetKinsectData() const { return KinsectData; }

	// ── 萃取与三灯 ─────────────────────────────────────────────
	UFUNCTION(BlueprintCallable, Category = "MHGZ|IG")
	bool ApplyExtract(FGameplayTag ExtractColor);
	/** Explicit resource grant/telemetry helper; kinsect flight hits must collect cargo instead. */
	bool ApplyExtractFromKinsectHit(FGameplayTag ExtractColor,
		const FHitResult& Hit, const FGuid& SourceAttackID, const FGuid& HitID,
		uint64 CommandSerial = 0);
	/** Successful empowered piercing hit collects cargo, never grants hunter buffs here. */
	bool CaptureExtractFromKinsectHit(const FKinsectFlightRequest& Request,
		const FHitResult& Hit, const FGuid& HitID);

	UFUNCTION(BlueprintCallable, Category = "MHGZ|IG")
	bool ApplyExtractFromHitzone(const UMHGZMonsterHitzoneComponent* Hitzone);
	/** Action-attached hit uses the same single cargo and return delivery as ordinary send. */
	bool CaptureActionKinsectExtract(const FWeaponActionToken& Action,
		const UMHGZMonsterHitzoneComponent* Hitzone);
	/** Called only after successful action commit; does not touch carried cargo. */
	bool ConsumeSingleExtractsForAction(const FWeaponActionToken& Action);

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
		const FGuid& HitInstanceID, const FGuid& SourceAttackID = FGuid());
	float GetModifiedKinsectAttackPower() const;

	// ── 唯一虫印 ───────────────────────────────────────────────
	bool LaunchKinsectMark(const FWeaponAimSnapshot& AimSnapshot);
	/**
	 * 用一次已经成功提交伤害的近战 Hitzone 命中建立/替换虫印。
	 * 近战虫印不生成投射物 Actor；Resource 仍是两种来源唯一的生命周期所有者。
	 */
	bool SetKinsectMarkFromMeleeHit(const FHitResult& Hit);
	bool SetKinsectMark(UMHGZMonsterHitzoneComponent* Hitzone,
		const FVector& ImpactPoint, AIGMarkProjectile* Projectile,
		const FVector& PreferredOutwardDirection = FVector::ZeroVector);
	void ClearKinsectMark(EIGMarkClearReason Reason);
	bool HasValidKinsectMark() const;
	bool GetKinsectMarkWorldLocation(FVector& OutLocation) const;
	bool GetKinsectMarkSurfaceFrame(uint64 ExpectedMarkSerial,
		FVector& OutLocation, FVector& OutNormal) const;
	UMHGZMonsterHitzoneComponent* GetKinsectMarkHitzone() const
	{
		return HasValidKinsectMark() ? ActiveMarkHitzone.Get() : nullptr;
	}
	void RecordAutomaticMarkWorldBlock(const FKinsectFlightRequest& Request,
		const FHitResult& Hit) const;
	/** True only for a real Hit on the marked Hitzone inside its fixed-world 20cm patch. */
	bool IsHitWithinKinsectMarkPatch(const FHitResult& Hit) const;
	uint64 GetKinsectMarkSerial() const { return MarkSerial; }
	FName GetKinsectMarkBoneName() const { return ActiveMarkBoneName; }
	double GetKinsectMarkExpireTime() const { return ActiveMarkExpireTime; }

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
	virtual bool ConsumeReservedCosts(const FWeaponResourceCostReservation& Reservation) override;

	// ── 通用粉尘生命周期与原子事务 ─────────────────────────────
	/**
	 * Must be called only after the matching Kinsect damage GE succeeded.
	 * This is the sole gameplay entry for the M6.3.3 powder policy/cooldown rules.
	 */
	bool HandleSuccessfulKinsectDamage(const FWeaponRuntimeToken& Runtime,
		const FGuid& SourceAttackID, const FGuid& HitID, const FHitResult& Hit,
		EIGPowderSpawnPolicy SpawnPolicy = EIGPowderSpawnPolicy::MarkedContact);
	/** Legacy low-level spawn seam retained for existing transaction tests/callers. */
	bool SpawnPowderForSuccessfulHit(const FWeaponRuntimeToken& Runtime,
		const FGuid& SourceAttackID, const FGuid& HitID, const FHitResult& Hit);
	bool CanGatherPowders(const FVector& Center) const;
	/** Submit the Kinsect flight using the activation-time aim snapshot. */
	bool StartPowderVortex(const FWeaponActionToken& Action,
		const FWeaponAimSnapshot& AimSnapshot);
	bool TryReservePowders(const FWeaponActionToken& Action, const FVector& Center,
		FIGPowderReservationToken& OutToken);
	bool BeginPowderGathering(const FIGPowderReservationToken& Token);
	bool UpdatePowderGathering(const FIGPowderReservationToken& Token, float Progress);
	bool CommitPowderReservation(const FIGPowderReservationToken& Token,
		FIGPowderExplosionSnapshot& OutExplosion);
	void ReleasePowderReservation(const FIGPowderReservationToken& Token);
	void ClearOwnedPowders(EWeaponRuntimeEndReason Reason);
	TArray<FIGPowderRuntimeSnapshot> GetPowderSnapshots() const;

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
	friend class AIGPowderActor;
#if WITH_DEV_AUTOMATION_TESTS
	friend class FMHGZM6MarkExpiresAndDeadTargetCannotReplace;
	friend class FMHGZM633PowderNaturalExpiryTest;
	friend class FMHGZM633PowderDistanceDespawnTest;
	friend class FMHGZM6PowderVortexTransactionTests;
friend class FMHGZM6PowderVortexConsumesMarkAtGatherStart;
	friend class FMHGZM6PowderVortexCancellationPreservesCurrentLocation;
	friend class FMHGZM6PowderVortexZeroPowderDoesNotDamage;
	friend class FMHGZM6PowderVortexAimsFromSnapshotAndManualRecallSupersedes;
#endif

	struct FReservedPowderMember
	{
		TWeakObjectPtr<AIGPowderActor> Actor;
		FTransform OriginalTransform = FTransform::Identity;
	};

	struct FReservedPowderSet
	{
		FIGPowderReservationToken Token;
		FVector Center = FVector::ZeroVector;
		double CreatedWorldTime = 0.0;
		double GatherDeadlineWorldTime = 0.0;
		bool bPowderVortexTransaction = false;
		bool bGathering = false;
		TArray<FReservedPowderMember> Members;
	};

	void HandlePowderActorEndPlay(AIGPowderActor* Powder);
	void HandlePowderLifetimeExpired(AIGPowderActor* Powder,
		int32 ExpectedLifetimeRevision);
	void TickPowderMaintenance();
	void TickPowderVortexPresentation();
	bool BeginPowderVortexGathering(const FKinsectFlightRequest& Request,
		const FVector& Center);
	void CommitPowderVortexTransaction(uint64 ExpectedCommandSerial);
	void CancelPowderVortexTransaction(uint64 ExpectedCommandSerial = 0,
		bool bReleaseCommand = true, FName CancellationReason = NAME_None);
	void ApplyPowderVortexExplosion(const FIGPowderExplosionSnapshot& Explosion);
	void ApplyPowderVortexDamageStage(const FIGPowderExplosionSnapshot& Explosion,
		const FGuid& StageAttackID, float Damage);
	void RemovePowderFromReservations(AIGPowderActor* Powder);
	bool SpawnPowderInternal(const FWeaponRuntimeToken& Runtime,
		const FGuid& SourceAttackID, const FGuid& HitID, const FHitResult& Hit,
		float DamageContribution,
		EIGPowderSpawnPolicy SpawnPolicy = EIGPowderSpawnPolicy::None,
		float KinsectAttackPowerSnapshot = 0.0f,
		float DamageMultiplierSnapshot = 0.0f);
	void ApplyNaturalPowderExplosion(AIGPowderActor* Powder,
		const FGuid& ExplosionID);
	bool IsLeafExtractTag(const FGameplayTag& ExtractColor) const;
	bool ApplyExtractInternal(FGameplayTag ExtractColor, const FHitResult* SourceHit,
		const FGuid& SourceAttackID, const FGuid& HitID, uint64 CommandSerial);
	bool IsHandleActive(const FActiveGameplayEffectHandle& Handle) const;
	bool ApplySingleExtractEffect(FGameplayTag ExtractColor);
	void CheckAndActivateTripleUp();
	void HandleSingleExtractRemoved(const FGameplayEffectRemovalInfo& RemovalInfo,
		FGameplayTag Color, FActiveGameplayEffectHandle ExpectedHandle);
	void HandleTripleUpRemoved(const FGameplayEffectRemovalInfo& RemovalInfo,
		FActiveGameplayEffectHandle ExpectedHandle);
	void ExecuteResourceFeedbackCue(const FGameplayTag& CueTag,
		const FGameplayTag& SourceTag) const;
	void BroadcastExtractState();
	void BroadcastResourceUIState(bool bForce = false);
	void SetKinsectActiveTag(bool bActive);
	void SetMarkActiveTag(bool bActive);
	void ClearAllResourceGameplayEffects();
	bool AreTripleCostSpecs(const TArray<FWeaponResourceCostSpec>& Specs) const;
	/**
	 * 把 `DanceSource` 同步成 Resource 所有的 GameplayTag 镜像。
	 *
	 * 真值永远是 `DanceSource` 字段，标签只是给下游（Combo RequiredTags、GA 闸门）读的
	 * 对外广播。数值写入与标签同步都必须在 `OnDanceStateChanged` 广播之前完成，
	 * 且清空时**不看**数值是否已经为零 —— 早退会留下没人认领的镜像。
	 */
	void RefreshDanceStateTags();
	bool SetKinsectMarkInternal(UMHGZMonsterHitzoneComponent* Hitzone,
		const FVector& ImpactPoint, const FVector& ImpactNormal,
		const FVector& PreferredOutwardDirection,
		AIGMarkProjectile* Projectile);
	void HandleMarkTargetHealthChanged(const FOnAttributeChangeData& ChangeData);
	void UnbindMarkTargetHealthDelegate();
	void ExpireKinsectMark(uint64 ExpectedMarkSerial);
	uint64 GetNextKinsectCommandSerial() const;
	uint64 AdvanceKinsectCommand(EKinsectCommandSource Source);
	void UpdateAutomaticMarkBehavior();
	void CancelAutomaticMarkBehavior(bool bInterruptFlight);
	bool IsAutomaticMarkBehaviorEligible() const;
	bool StartAutomaticMarkSeek();
	bool ResolveAutomaticMarkApproachPoint(FVector& OutPoint) const;
	bool PerformAutomaticMarkContactAttack();
	/** 表现只由 Resource 的成功虫印生命周期驱动，不由武器 Sweep 预触发。 */
	void SpawnKinsectMarkEffect();
	void ClearKinsectMarkEffect();
	void RecordCombatTelemetryEvent(FWeaponCombatTelemetryEvent Event) const;
	void RecordTripleCostTelemetryEvent(uint32 ActivationSequenceID,
		uint64 ReservationID, FName EventName, FName Reason) const;

	UPROPERTY()
	TObjectPtr<UInsectGlaiveCombatConfig> CombatConfig;

	UPROPERTY()
	TObjectPtr<AKinsect> KinsectActor;

	UPROPERTY()
	TObjectPtr<UInsectGlaiveKinsectData> KinsectData;

	UPROPERTY()
	TWeakObjectPtr<USceneComponent> KinsectAttachComponent;

	FName KinsectAttachSocket = NAME_None;
	FWeaponActionToken KinsectActionAttachmentToken;
	FWeaponActionToken KinsectPlayerCommandLeaseToken;
	EKinsectCommandSource KinsectCommandSource = EKinsectCommandSource::None;
	uint64 KinsectCommandSerial = 0;
	uint64 AutoMarkCommandSerial = 0;
	uint64 AutoMarkCommandMarkSerial = 0;
	bool bAutoMarkApproachComplete = false;
	bool bAutoMarkDeferredByPriority = false;
	double NextAutoMarkActionWorldTime = 0.0;
	uint64 EarliestAutoMarkUpdateFrame = 0;
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
	FOnIGResourceUIChanged IGResourceUIChanged;
	FIGResourceUISnapshot LastPublishedUISnapshot;
	bool bHasPublishedUISnapshot = false;

	UPROPERTY()
	TWeakObjectPtr<UMHGZMonsterHitzoneComponent> ActiveMarkHitzone;

	UPROPERTY()
	TWeakObjectPtr<AIGMarkProjectile> ActiveMarkProjectile;

	TWeakObjectPtr<AActor> ActiveMarkTargetActor;
	TWeakObjectPtr<UAbilitySystemComponent> ActiveMarkTargetASC;
	FDelegateHandle MarkTargetHealthChangedDelegateHandle;
	FName ActiveMarkBoneName = NAME_None;
	FGuid ActiveMarkID;

	UPROPERTY(Transient)
	TObjectPtr<UParticleSystemComponent> ActiveMarkEffect;

	FVector ActiveMarkLocalPoint = FVector::ZeroVector;
	FVector ActiveMarkLocalNormal = FVector::UpVector;
	FVector ActiveMarkLocalTangentX = FVector::ForwardVector;
	FVector ActiveMarkLocalTangentY = FVector::RightVector;
	FVector ActiveMarkVisualLocalPoint = FVector::ZeroVector;
	FVector ActiveMarkVisualLocalNormal = FVector::UpVector;
	double ActiveMarkCreatedAt = 0.0;
	double ActiveMarkExpireTime = 0.0;
	FTimerHandle MarkExpiryTimer;
	uint64 MarkSerial = 0;
	/** 仅远程虫印持有投射物；近战虫印不能因没有 Projectile 被误清理。 */
	bool bActiveMarkUsesProjectile = false;

	FWeaponOwnedTagToken KinsectActiveTagToken;
	FWeaponOwnedTagToken MarkActiveTagToken;
	/** 来源镜像的所有权 Token；`ActiveDanceSourceTag` 记它当前镜像的是哪一条标签。 */
	FWeaponOwnedTagToken DanceSourceTagToken;
	FGameplayTag ActiveDanceSourceTag;

	uint64 NextReservationID = 1;
	struct FTripleCostReservationEntry
	{
		FWeaponResourceCostReservation Reservation;
		FActiveGameplayEffectHandle TripleHandle;
	};
	TMap<uint64, FTripleCostReservationEntry> TripleReservations;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AIGPowderActor>> OwnedPowders;

	TMap<uint64, FReservedPowderSet> PowderReservations;
	TSet<FGuid> SpawnedPowderHitIDs;
	uint64 NextPowderSpawnSequenceID = 1;
	uint64 NextPowderReservationID = 1;
	uint64 ActivePowderVortexCommandSerial = 0;
	uint64 ActivePowderVortexReservationID = 0;
	FTimerHandle PowderVortexCommitTimer;
	double LastMarkedContactPowderWorldTime = -1.0;
	double NextPowderMaintenanceWorldTime = 0.0;
};
