// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InsectGlaive/IGPowderTypes.h"
#include "IGPowderActor.generated.h"

class APlayerController;
class URes_InsectGlaive;
class USceneComponent;

/**
 * Non-colliding, runtime-owned generic powder. The Resource component owns all
 * authoritative lifetime and reservation decisions; this actor only stores its
 * immutable origin data and presents Available/Reserved/Consumed state.
 */
UCLASS(BlueprintType)
class MHGZ_API AIGPowderActor : public AActor
{
	GENERATED_BODY()

public:
	AIGPowderActor();

	bool InitializePowder(
		URes_InsectGlaive* InOwnerResource,
		APlayerController* InOwningPlayer,
		const FWeaponRuntimeToken& InRuntimeToken,
		const FGuid& InPowderID,
		const FGuid& InSourceAttackInstanceID,
		const FGuid& InSourceHitInstanceID,
		const FHitResult& InSourceHit,
		EIGPowderSpawnPolicy InSpawnPolicy,
		double InSpawnWorldTime,
		float InLifetime,
		float InKinsectAttackPowerSnapshot,
		float InMotionValueSnapshot,
		float InDamageContribution,
		uint64 InSpawnSequenceID);

	bool IsAvailableFor(const URes_InsectGlaive* Resource,
		const FWeaponRuntimeToken& RuntimeToken, double WorldTime) const;
	bool TryReserve(const FIGPowderReservationToken& Token);
	bool IsReservedBy(const FIGPowderReservationToken& Token) const;
	bool CanCommitReservation(const FIGPowderReservationToken& Token,
		double WorldTime) const;
	void MarkConsumed(const FIGPowderReservationToken& Token,
		EIGPowderTerminalReason Reason = EIGPowderTerminalReason::GatherConsumed);
	bool ReleaseReservation(const FIGPowderReservationToken& Token,
		const FTransform& RestoreTransform, double WorldTime);
	/** Release without teleporting; used when a user command cancels visible gathering. */
	bool ReleaseReservationAtCurrentLocation(const FIGPowderReservationToken& Token,
		double WorldTime);
	bool SetGatherPresentationLocation(const FIGPowderReservationToken& Token,
		const FVector& Location);
	bool RefreshLifetime(const FIGPowderReservationToken& Token,
		double WorldTime, float LifetimeSeconds);

	FIGPowderRuntimeSnapshot MakeSnapshot(double WorldTime) const;
	double GetSpawnWorldTime() const { return SpawnWorldTime; }
	double GetExpireWorldTime() const { return ExpireWorldTime; }
	float GetLifetime() const { return Lifetime; }
	int32 GetLifetimeRevision() const { return LifetimeRevision; }
	float GetKinsectAttackPowerSnapshot() const { return KinsectAttackPowerSnapshot; }
	float GetMotionValueSnapshot() const { return MotionValueSnapshot; }
	uint64 GetSpawnSequenceID() const { return SpawnSequenceID; }
	const FWeaponRuntimeToken& GetRuntimeToken() const { return RuntimeToken; }
	const FGuid& GetPowderID() const { return PowderID; }
	const FGuid& GetSourceHitInstanceID() const { return SourceHitInstanceID; }
	float GetDamageContribution() const { return DamageContribution; }
	EIGPowderState GetPowderState() const { return State; }
	EIGPowderTerminalReason GetTerminalReason() const { return TerminalReason; }
	EIGPowderSpawnPolicy GetSpawnPolicy() const { return SpawnPolicy; }
	URes_InsectGlaive* GetOwnerResource() const { return OwnerResource.Get(); }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	friend class URes_InsectGlaive;
#if WITH_DEV_AUTOMATION_TESTS
	friend class FMHGZM633PowderNaturalExpiryTest;
#endif

	bool IsLifetimeValidAt(double WorldTime) const;
	void ScheduleLifetimeTimer();
	void HandleLifetimeExpired(int32 ExpectedRevision);
	void MarkTerminal(EIGPowderTerminalReason Reason);

	/** 粉尘对象的场景根组件，承载位置与表现挂载。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Powder", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SceneRoot;

	/** 拥有并管理该团粉尘生命周期的虫棍资源组件。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<URes_InsectGlaive> OwnerResource;

	/** 生成该团粉尘的玩家，用于来源归属与超距清理。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerController> OwningPlayer;

	/** 当前武器运行期的身份令牌；代际变化后旧回调和旧请求失效。 */
	UPROPERTY(Transient)
	FWeaponRuntimeToken RuntimeToken;

	/** 该团粉尘的唯一标识，用于预留、聚集、消耗及去重。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Powder", meta = (AllowPrivateAccess = "true"))
	FGuid PowderID;

	/** 产生这次猎虫伤害或粉尘的攻击身份，与每下命中的独立身份分开。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Powder", meta = (AllowPrivateAccess = "true"))
	FGuid SourceAttackInstanceID;

	/** 产生该团粉尘的成功命中身份，用于追溯与去重。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Powder", meta = (AllowPrivateAccess = "true"))
	FGuid SourceHitInstanceID;

	/** 生成该团粉尘的真实命中信息快照。 */
	UPROPERTY(Transient)
	FHitResult SourceHit;

	/** 该团粉尘生成时的世界时间，单位为秒。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Powder", meta = (AllowPrivateAccess = "true"))
	double SpawnWorldTime = 0.0;

	/** 该团粉尘计划到期的世界时间，单位为秒；聚集可刷新。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Powder", meta = (AllowPrivateAccess = "true"))
	double ExpireWorldTime = 0.0;

	/** 对象的完整存在时长，单位为游戏秒。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Powder", meta = (AllowPrivateAccess = "true"))
	float Lifetime = 0.0f;

	/** 生成或提交该次行为时锁定的猎虫攻击力，后续结算沿用此快照。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Powder", meta = (AllowPrivateAccess = "true"))
	float KinsectAttackPowerSnapshot = 0.0f;

	/** 该次行为锁定的动作值，伤害公式中按动作值除以100计算。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Powder", meta = (AllowPrivateAccess = "true"))
	float MotionValueSnapshot = 0.0f;

	/** 该团粉尘独立爆炸时的伤害贡献，聚爆第二段按实际消耗粉尘求和。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Powder", meta = (AllowPrivateAccess = "true"))
	float DamageContribution = 0.0f;

	/** 当前对象的运行时状态快照；状态流转由所属系统管理。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Powder", meta = (AllowPrivateAccess = "true"))
	EIGPowderState State = EIGPowderState::Available;

	/** 对象进入终止状态的原因，用于区分爆炸、消耗、超距或清理。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Powder", meta = (AllowPrivateAccess = "true"))
	EIGPowderTerminalReason TerminalReason = EIGPowderTerminalReason::None;

	/** 本次粉尘生成采用的规则，决定是否检查普通产粉冷却。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Powder", meta = (AllowPrivateAccess = "true"))
	EIGPowderSpawnPolicy SpawnPolicy = EIGPowderSpawnPolicy::None;

	FIGPowderReservationToken ReservationToken;
	uint64 SpawnSequenceID = 0;
	int32 LifetimeRevision = 0;
	FTimerHandle LifetimeTimer;
	bool bInitialized = false;
};
