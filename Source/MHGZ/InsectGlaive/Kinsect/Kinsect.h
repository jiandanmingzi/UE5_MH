// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/HitResult.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "MHGZWeaponRuntimeTypes.h"
#include "Kinsect.generated.h"

class USkeletalMeshComponent;
class UKinsectCollisionComponent;
class UProjectileMovementComponent;
class UInsectGlaiveKinsectData;
class UMHGZMonsterHitzoneComponent;
class URes_InsectGlaive;

/** 猎虫生命周期状态。 */
UENUM(BlueprintType)
enum class EKinsectState : uint8
{
	Attached  UMETA(DisplayName = "吸附中"),
	Flying    UMETA(DisplayName = "飞行中"),
	Hovering  UMETA(DisplayName = "悬停中"),
	Returning UMETA(DisplayName = "返回中")
};

/** 猎虫飞行轨迹模式。 */
UENUM(BlueprintType)
enum class EKinsectTrajectoryMode : uint8
{
	AlongDirection UMETA(DisplayName = "沿方向直线飞行"),
	ToPoint        UMETA(DisplayName = "飞向目标点")
};

/** 猎虫伤害模式。 */
UENUM(BlueprintType)
enum class EKinsectDamageMode : uint8
{
	None      UMETA(DisplayName = "无伤害"),
	SingleHit UMETA(DisplayName = "单发——命中一次即停"),
	Piercing  UMETA(DisplayName = "贯穿——按间隔持续伤害")
};

/** 猎虫萃取行为。 */
UENUM(BlueprintType)
enum class EKinsectExtractMode : uint8
{
	None             UMETA(DisplayName = "不萃取"),
	FirstHitOnly     UMETA(DisplayName = "仅首次命中记录"),
	ApplyPerValidHit UMETA(DisplayName = "每次有效命中直接应用")
};

/** 飞行结束后的策略。 */
UENUM(BlueprintType)
enum class EKinsectPostFlightPolicy : uint8
{
	Hover         UMETA(DisplayName = "悬停"),
	ReturnToOwner UMETA(DisplayName = "返回主人")
};

/** 飞行结束原因（观察/调试用）。 */
UENUM(BlueprintType)
enum class EKinsectFlightEndReason : uint8
{
	None,
	MaxDistance,
	Arrival,
	WorldHit,
	HitzoneHit,
	Interrupted
};

/**
 * 一次猎虫飞行的完整请求。
 * 由 bool BeginFlight(const Request&) 完整校验后原子提交；
 * 不允许保留 StartFlight / SetDamageParams 两步 API。
 */
USTRUCT(BlueprintType)
struct FKinsectFlightRequest
{
	GENERATED_BODY()

	/** 发起本次飞行的 Runtime 身份；飞行中每帧核对。 */
	UPROPERTY(BlueprintReadOnly, Category = "Kinsect|Flight")
	FWeaponRuntimeToken RuntimeToken;

	UPROPERTY(BlueprintReadOnly, Category = "Kinsect|Flight")
	EKinsectTrajectoryMode TrajectoryMode = EKinsectTrajectoryMode::AlongDirection;

	/** AlongDirection 模式下的方向快照。 */
	UPROPERTY(BlueprintReadOnly, Category = "Kinsect|Flight")
	FVector DirectionSnapshot = FVector::ForwardVector;

	/** ToPoint 模式下的目标点快照。 */
	UPROPERTY(BlueprintReadOnly, Category = "Kinsect|Flight")
	FVector TargetPointSnapshot = FVector::ZeroVector;

	/** 最大飞行距离（自 FlightStartLocation 起算）。 */
	UPROPERTY(BlueprintReadOnly, Category = "Kinsect|Flight", meta = (ClampMin = "0.0"))
	float MaxDistance = 0.f;

	/** 本次飞行速度（cm/s）。 */
	UPROPERTY(BlueprintReadOnly, Category = "Kinsect|Flight", meta = (ClampMin = "0.0"))
	float FlightSpeed = 0.f;

	/** ToPoint 到达判定半径。 */
	UPROPERTY(BlueprintReadOnly, Category = "Kinsect|Flight", meta = (ClampMin = "0.0"))
	float ArrivalRadius = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Kinsect|Flight")
	EKinsectDamageMode DamageMode = EKinsectDamageMode::None;

	UPROPERTY(BlueprintReadOnly, Category = "Kinsect|Flight")
	EKinsectExtractMode ExtractMode = EKinsectExtractMode::None;

	UPROPERTY(BlueprintReadOnly, Category = "Kinsect|Flight")
	EKinsectPostFlightPolicy PostFlightPolicy = EKinsectPostFlightPolicy::Hover;

	/** 伤害 MotionValue。 */
	UPROPERTY(BlueprintReadOnly, Category = "Kinsect|Flight", meta = (ClampMin = "0.0"))
	float MotionValue = 0.f;

	/** Piercing 模式下每个 Hitzone 组件独立的重复命中间隔（秒）。 */
	UPROPERTY(BlueprintReadOnly, Category = "Kinsect|Flight", meta = (ClampMin = "0.0"))
	float RehitInterval = 0.f;

	/** 本次 Flight 实例 ID；每次实际伤害另生成 HitInstanceID 供 Resolver 去重。 */
	UPROPERTY(BlueprintReadOnly, Category = "Kinsect|Flight")
	FGuid FlightInstanceID;
};

/**
 * AKinsect — 猎虫独立 Actor。
 * Collision（胶囊）为 Root，Mesh 附着其上，ProjectileMovement 驱动 Collision；
 * 命中判定完全由代码 Capsule Sweep 完成，不使用 Overlap / Weapon 通道。
 */
UCLASS()
class AKinsect : public AActor
{
	GENERATED_BODY()

public:
	AKinsect();

	virtual void Tick(float DeltaTime) override;

	// ── 组件 ──
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Kinsect|Components")
	TObjectPtr<USkeletalMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Kinsect|Components")
	TObjectPtr<UKinsectCollisionComponent> Collision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Kinsect|Components")
	TObjectPtr<UProjectileMovementComponent> Movement;

	// ── 状态 ──
	UPROPERTY(BlueprintReadOnly, Category = "Kinsect|State")
	EKinsectState State = EKinsectState::Attached;

	UPROPERTY(BlueprintReadOnly, Category = "Kinsect|State")
	TObjectPtr<UInsectGlaiveKinsectData> KinsectData;

	/** 最近一次已提交的飞行请求（Attached/Hovering 时保留快照供查询）。 */
	UPROPERTY(BlueprintReadOnly, Category = "Kinsect|State")
	FKinsectFlightRequest ActiveRequest;

	// ── 飞行 API ──
	/** 完整校验后原子提交一次飞行；校验失败不改变任何状态。 */
	bool BeginFlight(const FKinsectFlightRequest& Request);

	/** 中断当前飞行/返回并转悬停；不清除 Pending 萃取。 */
	void Interrupt();

	/** 开始返回；幂等（Attached/Returning 时直接返回），保留 Pending。 */
	void StartReturn();

	/** 强制召回（耐力归零等）；保留 Pending。 */
	void ForceRecall() { StartReturn(); }

	/** 附着到指定组件 Socket，保存组件与 Socket 供返回时实时追踪。 */
	void AttachToPlayer(USceneComponent* InAttachComponent, FName InSocketName = NAME_None);

	// ── 查询 ──
	EKinsectState GetState() const { return State; }
	bool HasPendingExtract() const { return PendingExtractColor.IsValid(); }
	FGameplayTag GetPendingExtractColor() const { return PendingExtractColor; }
	/** 取出并清除 Pending 萃取；仅应由单个调用方调用一次。 */
	FGameplayTag TakePendingExtractColor();
	float GetFlightSpeed() const;
	float GetHoverDrainRate() const;
	float GetFlightDrainRate() const;
	bool IsDeployed() const
	{
		return State == EKinsectState::Flying
			|| State == EKinsectState::Hovering
			|| State == EKinsectState::Returning;
	}

	// ── 引用（由 URes_InsectGlaive 注入） ──
	UPROPERTY()
	TWeakObjectPtr<AActor> OwnerActor;

	UPROPERTY()
	TWeakObjectPtr<URes_InsectGlaive> ResourceComponent;

protected:
	virtual void BeginPlay() override;

	/** 世界碰撞回调——只缓存 PendingWorldHit 并停 Movement，不直接结束飞行。 */
	UFUNCTION()
	void OnWorldCollision(UPrimitiveComponent* HitComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

private:
	/** 校验请求；纯查询，不修改状态。 */
	bool IsFlightRequestValid(const FKinsectFlightRequest& Request) const;

	/** Tick 内 Hitzone 扫描（Previous→Current，按 Hit.Time 排序）。 */
	void SweepHitzones(float DeltaTime);

	/** 处理单个 Hitzone 命中（SingleHit / Piercing / 萃取）。 */
	void HandleHitzoneHit(const FHitResult& Hit, float DeltaTime);

	/** 处理缓存的墙撞结果并结束飞行。 */
	void HandleWorldHit();

	/** 距离 / ToPoint 到达判定。 */
	void CheckFlightProgress();

	/** 按 PostFlightPolicy 结束飞行（Hover 或 StartReturn）。 */
	void EndFlight(EKinsectFlightEndReason Reason);

	/** 悬停：停 Movement。 */
	void StopAndHover();

	/**
	 * 重新武装 ProjectileMovement 并赋予当前方向的速度。
	 * 非弹跳 Projectile 撞墙后会 StopSimulating，届时 UpdatedComponent 可能被清空；
	 * 后续送虫或召回必须显式绑定回 Collision，不能只调用 Activate。
	 */
	bool ArmMovement(const FVector& Direction, float Speed);

	/** 在附着原始朝向与飞行朝向修正之间切换猎虫视觉 Mesh。 */
	void SetFlightVisualFacing(bool bInFlight);

	/** 返回 Tick：实时追踪附着 Socket 世界位置；到达后取 Pending → Attach → 回调一次。 */
	void TickReturn();

	/** 解析返回目标的世界位置（优先附着 Socket，回退 OwnerActor）。 */
	bool ResolveAttachSocketLocation(FVector& OutLocation) const;

	// 飞行
	FVector FlightStartLocation = FVector::ZeroVector;
	FVector LastTickLocation = FVector::ZeroVector;
	FHitResult PendingWorldHit;
	bool bHasDealtDamage = false;

	// 贯穿：每个 Hitzone 组件上次成功伤害的绝对世界时间。
	TMap<TWeakObjectPtr<UMHGZMonsterHitzoneComponent>, float> HitzoneHitTimers;

	// 萃取（无颜色优先级、无部位名映射）
	UPROPERTY()
	FGameplayTag PendingExtractColor;

	// 返回附着目标
	UPROPERTY()
	TWeakObjectPtr<USceneComponent> AttachComponent;

	FName AttachSocketName = NAME_None;

	// 只有附着时记录的美术原始相对旋转会在回到手臂 Socket 后恢复。
	FRotator AttachedMeshRelativeRotation = FRotator::ZeroRotator;
	bool bHasAttachedMeshRelativeRotation = false;

	static constexpr float RETURN_ARRIVAL_DISTANCE = 50.f; // 无已提交 Request 时的防御回退
};
