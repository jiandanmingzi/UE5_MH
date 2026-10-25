// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActiveGameplayEffectHandle.h"
#include "GameplayTagContainer.h"
#include "MHGZWeaponResourceComponent.h"
#include "InsectGlaive/IGPowderTypes.h"
#include "InsectGlaive/IGResourceUITypes.h"
#include "InsectGlaive/InsectGlaiveCombatConfig.h"
#include "Res_InsectGlaive.generated.h"

class AIGMarkProjectile;
class AIGPowderActor;
class UAudioComponent;
class AKinsect;
class UAbilitySystemComponent;
class UInsectGlaiveCombatConfig;
class UInsectGlaiveKinsectData;
class UMHGZMonsterHitzoneComponent;
class USkeletalMeshComponent;
class UParticleSystemComponent;
class UIGCoAttackComponent;
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
	/** Ground-only opt-in. Compatible successors join the existing ordered stream. */
	bool BeginKinsectCoAttack(const FWeaponActionToken& ActionToken,
		USkeletalMeshComponent* WeaponMesh, const FIGCoAttackSource& Source);
	void EndKinsectCoAttack(const FWeaponActionToken& ActionToken);
	/** Normal/Superseded end: seal receipts, without retaining the source GA. */
	void FinishKinsectCoAttack(const FWeaponActionToken& ActionToken);
	void FinishKinsectCoAttackPath(const FWeaponActionToken& ActionToken, bool bReturnAfterReplay);
	bool OpenKinsectCoAttackWindow(const FWeaponActionToken& ActionToken,
		int32 Segment, uint64 Occurrence, const FGuid& SourceAttackID,
		float MotionValue, int32 MaxHits, float HitInterval);
	void CloseKinsectCoAttackWindow(const FWeaponActionToken& ActionToken, uint64 Occurrence);
	/** Optional special companion: failures never reject the hunter's ability. */
	bool BeginDescendingThrustKinsect(const FWeaponActionToken& Action, const FVector& Origin,
		const FGuid& AttackID, float MotionValue);
	bool StartDescendingThrustKinsectDive(const FWeaponActionToken& Action, const FVector& TargetSnapshot);
	void FinishDescendingThrustKinsect(const FWeaponActionToken& Action, EWeaponActionEndReason Reason);
	/** Admission only: no extra charge and no threshold-triggered mid-action recall. */
	bool HasKinsectCoAttackEntryStamina() const
	{
		return FMath::IsFinite(MaxKinsectStamina) && MaxKinsectStamina > 0.f
			&& FMath::IsFinite(KinsectStamina) && KinsectStamina >= MaxKinsectStamina * .1f;
	}
	/** 招式临时占用真实猎虫；租约由精确 ActionToken 所有。 */
	bool BeginKinsectActionAttachment(const FWeaponActionToken& ActionToken,
		USkeletalMeshComponent* WeaponMesh, FName SocketName);
	bool BeginKinsectSlashPreparation(const FWeaponActionToken& ActionToken,
		USkeletalMeshComponent* WeaponMesh, FName SocketName, float AvailableReturnTime);
	bool IsKinsectActionControlCurrent(const FWeaponActionToken& ActionToken) const;
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

	/** 当前有效精华决定的耗耐倍率；三灯覆盖红灯，回耐不乘此值。 */
	UFUNCTION(BlueprintPure, Category = "MHGZ|IG|Kinsect")
	float GetKinsectStaminaDrainMultiplier() const;

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
	/** After successful commit; optionally returns the colors actually consumed, not carried cargo. */
	bool ConsumeSingleExtractsForAction(const FWeaponActionToken& Action,
		FGameplayTagContainer* OutConsumedExtracts = nullptr);

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
	/** Explicit window-time power snapshot; legacy ordinary flight keeps its live-power wrapper. */
	bool ApplyKinsectDamageWithSnapshot(const FHitResult& Hit, float MotionValue,
		float AttackPower, const FGuid& HitInstanceID, const FGuid& SourceAttackID);
	float GetModifiedKinsectAttackPower() const;
	/** 真实有效贯通命中可提交保底结算；结算与飞行/召回状态分别持有。 */
	bool ApplyGuaranteedKinsectPiercingContact(const FKinsectFlightRequest& Request, const FHitResult& Hit);

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
	/** Real contact with the exact part carrying this Resource's active mark Debuff. */
	bool IsHitOnKinsectMarkedPart(const FHitResult& Hit) const;
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

	/** 从已授予的对应GA默认配置查询当前舞踏倍率；未授予/未指定/无效时为1。执行中GA自行冻结实例倍率。 */
	UFUNCTION(BlueprintPure, Category = "MHGZ|IG|Dance")
	float GetDanceDamageMultiplier(EIGDanceDamageAction Action = EIGDanceDamageAction::None) const;

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
	/** 显式AP/MV的低层生成入口；正常猎虫命中仍走统一策略/CD入口。 */
	bool SpawnPowderForSuccessfulHit(const FWeaponRuntimeToken& Runtime,
		const FGuid& SourceAttackID, const FGuid& HitID, const FHitResult& Hit,
        float KinsectAttackPower, float MotionValue);
	bool CanGatherPowders(const FVector& Center, float GatherRadius) const;
	/** Submit the Kinsect flight using the activation-time aim snapshot. */
	bool StartPowderVortex(const FWeaponActionToken& Action,
		const FWeaponAimSnapshot& AimSnapshot, const FIGPowderVortexSettings& Settings);
	bool TryReservePowders(const FWeaponActionToken& Action, const FVector& Center,
		FIGPowderReservationToken& OutToken, float GatherRadius, int32 MaxCount);
	bool BeginPowderGathering(const FIGPowderReservationToken& Token);
	bool UpdatePowderGathering(const FIGPowderReservationToken& Token, float Progress);
	bool CommitPowderReservation(const FIGPowderReservationToken& Token,
		FIGPowderExplosionSnapshot& OutExplosion, float ExplosionRadius);
	void ReleasePowderReservation(const FIGPowderReservationToken& Token);
	void ClearOwnedPowders(EWeaponRuntimeEndReason Reason);
	TArray<FIGPowderRuntimeSnapshot> GetPowderSnapshots() const;

	// Demo 禁用词条入口，但保留幂等清理兼容。
	virtual void ApplyEntryModifier(FGameplayTag AttributeTag, float Value,
		TEnumAsByte<EGameplayModOp::Type> Op) override;
	virtual void ClearAllEntryModifiers() override;

	/** 猎虫当前耐力或上限变化时广播。 */
	UPROPERTY(BlueprintAssignable, Category = "IG|Delegate")
	FOnKinsectStaminaChanged OnKinsectStaminaChanged;

	/** 玩家精华剩余时间更新时广播，供界面刷新。 */
	UPROPERTY(BlueprintAssignable, Category = "IG|Delegate")
	FOnExtractTimeUpdated OnExtractTimeUpdated;

	/** 三灯强化状态变化时广播。 */
	UPROPERTY(BlueprintAssignable, Category = "IG|Delegate")
	FOnTripleUpChanged OnTripleUpChanged;

	/** 舞踏层数或来源状态变化时广播。 */
	UPROPERTY(BlueprintAssignable, Category = "IG|Delegate")
	FOnDanceStateChanged OnDanceStateChanged;

	const UInsectGlaiveCombatConfig* GetCombatConfig() const { return CombatConfig; }

private:
	struct FGuaranteedKinsectHitReceipt
	{
		FGuid ID, FlightID, SourceAttackID;
		FWeaponRuntimeToken Runtime;
		TWeakObjectPtr<AActor> Target;
		FHitResult LastHit;
		FVector LocalImpactPoint = FVector::ZeroVector, LocalLocation = FVector::ZeroVector;
		FVector LocalNormal = FVector::UpVector;
		TMap<TWeakObjectPtr<UMHGZMonsterHitzoneComponent>, double> HitTimes;
		float MotionValue = 0.f, AttackPower = 0.f, Interval = 0.f;
		int32 Minimum = 0, Successful = 0;
		double NextDue = 0.0;
		EIGPowderSpawnPolicy PowderPolicy = EIGPowderSpawnPolicy::None;
		bool bApplying = false;
	};
	TArray<FGuaranteedKinsectHitReceipt> GuaranteedKinsectHitReceipts;
	bool SubmitGuaranteedKinsectHit(FGuid ReceiptID, const FHitResult& Hit, double Now, bool bScheduled);
	void TickGuaranteedKinsectHits();
	void RetireGuaranteedKinsectHit(const FGuid& ReceiptID);

	friend class AIGPowderActor;
#if WITH_DEV_AUTOMATION_TESTS
	friend class FMHGZCoAttackRuntimeTest;
	friend class FMHGZKinsectDeliveryAudioTests;
	friend class FMHGZPowderAudioLifecycleTests;
	friend class FMHGZKinsectSlashPreparationTest;
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
	void StartPowderVortexGatherSounds(const FVector& Center);
	void StopPowderVortexGatherSounds();
	bool BeginPowderVortexGathering(const FKinsectFlightRequest& Request,
		const FVector& Center);
	void CommitPowderVortexTransaction(uint64 ExpectedCommandSerial);
	void CancelPowderVortexTransaction(uint64 ExpectedCommandSerial = 0,
		bool bReleaseCommand = true, FName CancellationReason = NAME_None);
	void ApplyPowderVortexExplosion(const FIGPowderExplosionSnapshot& Explosion,
		const FIGPowderVortexSettings& Settings);
	void ApplyPowderVortexDamageStage(const FIGPowderExplosionSnapshot& Explosion,
		const FGuid& StageAttackID, float Damage, FGameplayTag HitCueTag);
	void RemovePowderFromReservations(AIGPowderActor* Powder);
	bool SpawnPowderInternal(const FWeaponRuntimeToken& Runtime,
		const FGuid& SourceAttackID, const FGuid& HitID, const FHitResult& Hit,
		float DamageContribution,
		EIGPowderSpawnPolicy SpawnPolicy = EIGPowderSpawnPolicy::None,
		float KinsectAttackPowerSnapshot = 0.0f,
		float MotionValueSnapshot = 0.0f);
	void ApplyNaturalPowderExplosion(AIGPowderActor* Powder,
		const FGuid& ExplosionID);
	bool IsLeafExtractTag(const FGameplayTag& ExtractColor) const;
	void PlayExtractCollectedSound(uint64 CommandSerial);
	uint64 LastExtractSoundCommandSerial = 0;
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
	void ResetDescendingThrustKinsect();
	void TickDescendingThrustKinsect();
	bool HandleDescendingThrustKinsectFlightEnd(const FKinsectFlightRequest& Request,
		EKinsectFlightEndReason Reason);
	void HandleDescendingThrustSuccessor(const FWeaponActionToken& Action);
	void HandleDescendingThrustSuccessorRecovery(const FWeaponActionToken& Action);
	void HandleDescendingThrustComboClosed(const FWeaponActionToken& Action);
	bool PrepareDescendingThrustCoAttackHandoff(const FWeaponActionToken& Action,
		USkeletalMeshComponent* Mesh, const FIGCoAttackSource& Source, uint64& OutHoverSerial);
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

	/** 当前武器共用战斗配置；招式独占参数仍由对应GA管理。 */
	UPROPERTY()
	TObjectPtr<UInsectGlaiveCombatConfig> CombatConfig;

	/** 当前武器运行期拥有的猎虫实例。 */
	UPROPERTY()
	TObjectPtr<AKinsect> KinsectActor;
	/** 负责记录武器轨迹并执行猎虫延迟协同的组件。 */
	UPROPERTY(Transient)
	TObjectPtr<UIGCoAttackComponent> CoAttackComponent;
	struct FDescendingThrustKinsectSession
	{
		FWeaponActionToken Action;
		uint64 Serial = 0;
		FGuid AttackID, FlightID;
		float MotionValue = 10.f;
		bool bDiving = false, bArrived = false, bSourceEnded = false;
		bool bSourceComboClosed = false, bSourceSuperseded = false;
		uint64 SourceEndFrame = 0;
		bool bPendingExpired = false;
		FWeaponActionToken PendingAction;
		TWeakObjectPtr<USkeletalMeshComponent> PendingMesh;
		FIGCoAttackSource PendingSource;
	};
	TOptional<FDescendingThrustKinsectSession> DescendingThrustKinsect;
	FDelegateHandle DescendingSuccessorHandle, DescendingRecoveryHandle, DescendingComboClosedHandle;
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
	friend class FMHGZDescendingThrustCoAttackTestAccess;
#endif

	/** 当前猎虫的品种数据，提供速度、耐力、攻击力及表现资源。 */
	UPROPERTY()
	TObjectPtr<UInsectGlaiveKinsectData> KinsectData;

	/** 猎虫当前附着使用的目标组件引用。 */
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

	/** 玩家当前各单色精华对应的有效效果句柄。 */
	UPROPERTY()
	TMap<FGameplayTag, FActiveGameplayEffectHandle> ActiveExtractHandles;

	/** 当前三灯替换强化效果的句柄。 */
	UPROPERTY()
	FActiveGameplayEffectHandle TripleUpHandle;

	bool bExtractTransitionGuard = false;
	bool bRuntimeShuttingDown = false;
	int32 DanceStacks = 0;
	EIGDanceSource DanceSource = EIGDanceSource::None;
	FOnIGResourceUIChanged IGResourceUIChanged;
	FIGResourceUISnapshot LastPublishedUISnapshot;
	bool bHasPublishedUISnapshot = false;

	/** 当前唯一虫印实际附着并施加Debuff的受击部位。 */
	UPROPERTY()
	TWeakObjectPtr<UMHGZMonsterHitzoneComponent> ActiveMarkHitzone;

	/** 当前仍由资源组件拥有的虫印弹实例。 */
	UPROPERTY()
	TWeakObjectPtr<AIGMarkProjectile> ActiveMarkProjectile;

	TWeakObjectPtr<AActor> ActiveMarkTargetActor;
	TWeakObjectPtr<UAbilitySystemComponent> ActiveMarkTargetASC;
	FDelegateHandle MarkTargetHealthChangedDelegateHandle;
	FName ActiveMarkBoneName = NAME_None;
	FGuid ActiveMarkID;

	/** 当前虫印粒子表现组件引用。 */
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

	/** 本运行期仍拥有的粉尘实例集合，负责预留、消耗和清理。 */
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AIGPowderActor>> OwnedPowders;

	TMap<uint64, FReservedPowderSet> PowderReservations;
	TSet<FGuid> SpawnedPowderHitIDs;
	uint64 NextPowderSpawnSequenceID = 1;
	uint64 NextPowderReservationID = 1;
	uint64 ActivePowderVortexCommandSerial = 0;
	uint64 ActivePowderVortexReservationID = 0;
	/** 按次冻结的GA配置；UPROPERTY保证GA结束后声音等引用仍有效。 */
	UPROPERTY(Transient)
	FIGPowderVortexSettings ActivePowderVortexSettings;
	float ActivePowderVortexAttackPower = 0.f;
	FTimerHandle PowderVortexCommitTimer;
	/** 聚爆聚集阶段持有的声音组件，取消或结束时停止。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAudioComponent>> PowderVortexGatherAudio;
	double LastMarkedContactPowderWorldTime = -1.0;
	double NextPowderMaintenanceWorldTime = 0.0;
};
