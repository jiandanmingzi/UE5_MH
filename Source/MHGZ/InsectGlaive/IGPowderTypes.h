// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WeaponRuntime/MHGZWeaponRuntimeTypes.h"
#include "IGPowderTypes.generated.h"

class AIGPowderActor;
class USoundBase;

/** 聚爆单招配置：由GA编辑，提交后按值交给Resource，生命周期可长于GA。 */
USTRUCT(BlueprintType)
struct FIGPowderVortexSettings
{
	GENERATED_BODY()

	/** 聚集球半径（cm），不限制可聚集的粉尘数量。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.01", ForceUnits = "cm"))
	float GatherRadius = 3000.f;
	/** 猎虫到达目标点后，从开始吸粉到引爆的游戏秒数。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.01", ForceUnits = "s"))
	float GatherDelay = 1.f;
	/** 消耗1–4团粉尘时，第一段伤害的动作值；最终伤害=本次猎虫攻击力×动作值/100。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float MotionValueAt1 = 10.f;
	/** 消耗5–9团粉尘时第一段动作值；不会与其他数量档叠加。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float MotionValueAt5 = 50.f;
	/** 消耗至少10团粉尘时第一段动作值；零团不造成两段伤害。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float MotionValueAt10 = 100.f;
	/** 两段聚爆共用的伤害球半径（cm），不影响吸粉半径或视觉尺寸。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.01", ForceUnits = "cm"))
	float ExplosionRadius = 300.f;
	/** 各目标受伤时执行的反馈Cue，不负责附加伤害。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FGameplayTag HitCueTag = FGameplayTag::RequestGameplayTag(TEXT("GameplayCue.Hit.IG.Powder"));
	/** 聚爆真正提交时在聚集点执行一次的爆炸表现Cue。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FGameplayTag ExplosionCueTag = FGameplayTag::RequestGameplayTag(TEXT("GameplayCue.IG.PowderVortex"));
	/** 到达后开始播放的空间聚集声层；引爆、取消或卸装立即停止。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<TObjectPtr<USoundBase>> GatherSounds;
	/** 有粉尘时引爆的附加声音；与Combat内共享基础粉尘爆炸声各播一次。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<USoundBase> ExplosionSound;

	bool IsValid() const
	{
		return FMath::IsFinite(GatherRadius) && GatherRadius > 0.f
			&& FMath::IsFinite(GatherDelay) && GatherDelay > 0.f
			&& FMath::IsFinite(ExplosionRadius) && ExplosionRadius > 0.f
			&& FMath::IsFinite(MotionValueAt1) && MotionValueAt1 >= 0.f
			&& FMath::IsFinite(MotionValueAt5) && MotionValueAt5 >= 0.f
			&& FMath::IsFinite(MotionValueAt10) && MotionValueAt10 >= 0.f
			&& HitCueTag.IsValid() && ExplosionCueTag.IsValid();
	}
};

UENUM(BlueprintType)
enum class EIGPowderState : uint8
{
	Available UMETA(DisplayName = "Available"),
	Reserved UMETA(DisplayName = "Reserved"),
	Consumed UMETA(DisplayName = "Consumed")
};

/** A powder's one and only terminal outcome. */
UENUM(BlueprintType)
enum class EIGPowderTerminalReason : uint8
{
	None UMETA(DisplayName = "None"),
	NaturalExplosion UMETA(DisplayName = "Natural Explosion"),
	GatherConsumed UMETA(DisplayName = "Gather Consumed"),
	DistanceDespawn UMETA(DisplayName = "Distance Despawn"),
	RuntimeShutdown UMETA(DisplayName = "Runtime Shutdown")
};

/** Successful real Kinsect damage route that is allowed to create powder. */
UENUM(BlueprintType)
enum class EIGPowderSpawnPolicy : uint8
{
	None UMETA(DisplayName = "None"),
	MarkedContact UMETA(DisplayName = "Marked Contact"),
	/** Legacy serialized policy: keep the original numeric value and strict piercing contract. */
	AwakenedPiercing UMETA(DisplayName = "Awakened Piercing"),
	/** 黄灯觉虫击：单击/贯通均在每次成功伤害后产粉，无需虫印且绕过普通冷却。 */
	AwakenedEachHit UMETA(DisplayName = "觉虫击黄灯逐击产粉"),
	/** 自动寻印攻击：仍须命中有效虫印局部区域，但不检查或占用普通产粉冷却。 */
	AutoMarkContact UMETA(DisplayName = "猎虫自动寻印逐击产粉")
};

/** Exact identity for one fixed set of powders reserved by one action activation. */
USTRUCT()
struct FIGPowderReservationToken
{
	GENERATED_BODY()

	FWeaponRuntimeToken RuntimeToken;

	uint32 ActivationSequenceID = 0;

	uint64 ReservationID = 0;

	/** Resource command generation for a post-Montage vortex transaction. */
	uint64 CommandSerial = 0;

	bool IsValid() const
	{
		return RuntimeToken.IsValid() && ActivationSequenceID != 0 && ReservationID != 0;
	}

	bool operator==(const FIGPowderReservationToken& Other) const
	{
		return RuntimeToken == Other.RuntimeToken
			&& ActivationSequenceID == Other.ActivationSequenceID
			&& ReservationID == Other.ReservationID
			&& CommandSerial == Other.CommandSerial;
	}

	bool operator!=(const FIGPowderReservationToken& Other) const
	{
		return !(*this == Other);
	}
};

/** Read-only per-powder data for later UI, telemetry, and transaction audits. */
USTRUCT(BlueprintType)
struct FIGPowderRuntimeSnapshot
{
	GENERATED_BODY()

	/** 该团粉尘的唯一标识，用于预留、聚集、消耗及去重。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid PowderID;

	/** 产生这次猎虫伤害或粉尘的攻击身份，与每下命中的独立身份分开。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid SourceAttackInstanceID;

	/** 产生该团粉尘的成功命中身份，用于追溯与去重。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid SourceHitInstanceID;

	/** 对象当前的世界空间位置，单位为厘米。 */
	UPROPERTY(BlueprintReadOnly)
	FVector Location = FVector::ZeroVector;

	/** 剩余存在时间，单位为游戏秒。 */
	UPROPERTY(BlueprintReadOnly)
	float RemainingLifetime = 0.0f;

	/** 该团粉尘独立爆炸时的伤害贡献，聚爆第二段按实际消耗粉尘求和。 */
	UPROPERTY(BlueprintReadOnly)
	float DamageContribution = 0.0f;

	/** 生成或提交该次行为时锁定的猎虫攻击力，后续结算沿用此快照。 */
	UPROPERTY(BlueprintReadOnly)
	float KinsectAttackPowerSnapshot = 0.0f;

	/** 该次行为锁定的动作值，伤害公式中按动作值除以100计算。 */
	UPROPERTY(BlueprintReadOnly)
	float MotionValueSnapshot = 0.0f;

	/** 当前对象的运行时状态快照；状态流转由所属系统管理。 */
	UPROPERTY(BlueprintReadOnly)
	EIGPowderState State = EIGPowderState::Available;

	/** 对象进入终止状态的原因，用于区分爆炸、消耗、超距或清理。 */
	UPROPERTY(BlueprintReadOnly)
	EIGPowderTerminalReason TerminalReason = EIGPowderTerminalReason::None;

	/** 粉尘寿命刷新修订号，用于识别聚集刷新后的新截止时间。 */
	UPROPERTY(BlueprintReadOnly)
	int32 LifetimeRevision = 0;

	/** 本次粉尘生成采用的规则，决定是否检查普通产粉冷却。 */
	UPROPERTY(BlueprintReadOnly)
	EIGPowderSpawnPolicy SpawnPolicy = EIGPowderSpawnPolicy::None;
};

/** Immutable result of committing a powder reservation. */
USTRUCT(BlueprintType)
struct FIGPowderExplosionSnapshot
{
	GENERATED_BODY()

	/** 本次粉尘爆炸事务的唯一身份。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid ExplosionID;

	/** 聚爆第一段数量档伤害的独立攻击身份。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid Stage1AttackID;

	/** 聚爆第二段粉尘贡献求和伤害的独立攻击身份。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid Stage2AttackID;

	/** 本次爆炸判定的世界空间中心，单位为厘米。 */
	UPROPERTY(BlueprintReadOnly)
	FVector Center = FVector::ZeroVector;

	/** 本次实际提交并消耗的粉尘数量，不包含无效或被取消的预留。 */
	UPROPERTY(BlueprintReadOnly)
	int32 ConsumedCount = 0;

	/** 本次爆炸快照中两段伤害的总和。 */
	UPROPERTY(BlueprintReadOnly)
	float TotalDamage = 0.0f;

	/** 按实际粉尘数量档及猎虫攻击力计算的聚爆第一段伤害。 */
	UPROPERTY(BlueprintReadOnly)
	float Stage1Damage = 0.0f;

	/** 第一段保留原始攻击力快照与动作值，由统一粉尘公式实际结算。 */
	UPROPERTY(BlueprintReadOnly)
	float Stage1AttackPower = 0.0f;
	/** 聚爆第一段数量档使用的动作值，公式中除以100。 */
	UPROPERTY(BlueprintReadOnly)
	float Stage1MotionValue = 0.0f;

	/** 实际消耗的各团粉尘独立伤害贡献之和。 */
	UPROPERTY(BlueprintReadOnly)
	float Stage2Damage = 0.0f;

	/** 本次爆炸的球形伤害半径，单位为厘米。 */
	UPROPERTY(BlueprintReadOnly)
	float Radius = 0.0f;

	/** 本次命中的基础硬直量，由后续结算按部位与抗性处理。 */
	UPROPERTY(BlueprintReadOnly)
	float BaseStagger = 0.0f;
};
