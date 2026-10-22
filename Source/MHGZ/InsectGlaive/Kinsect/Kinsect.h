// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/HitResult.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "MHGZWeaponRuntimeTypes.h"
#include "InsectGlaive/IGPowderTypes.h"
#include "Kinsect.generated.h"

class USkeletalMeshComponent;
class UPrimitiveComponent;
class UKinsectCollisionComponent;
class UProjectileMovementComponent;
class UInsectGlaiveKinsectData;
class UMHGZMonsterHitzoneComponent;
class URes_InsectGlaive;
class UAudioComponent;
class USoundBase;

/** 猎虫生命周期状态。 */
UENUM(BlueprintType)
enum class EKinsectState : uint8
{
	Attached  UMETA(DisplayName = "吸附中"),
	Flying    UMETA(DisplayName = "飞行中"),
	Hovering  UMETA(DisplayName = "悬停中"),
	Returning UMETA(DisplayName = "返回中"),
	/** 临时挂在招式专用插槽上；追加在末尾以保留旧状态序列化值。 */
	ActionAttached UMETA(DisplayName = "动作临时附着"),
	ActionReturning UMETA(DisplayName = "动作回槽中"),
	ActionWaiting UMETA(DisplayName = "动作原地待机")
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
	CollectPerValidHit UMETA(DisplayName = "每次有效命中携带不同精华，回手交付")
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

	/** Ordinary aimed send: align an Attached kinsect to the saved ray at the owner's vertical plane. */
	UPROPERTY(BlueprintReadOnly, Category = "Kinsect|Flight")
	bool bProjectAttachedLaunchToAimRay = false;
	UPROPERTY(BlueprintReadOnly, Category = "Kinsect|Flight")
	FVector AimOriginSnapshot = FVector::ZeroVector;

	/** ToPoint 模式下的目标点快照。 */
	UPROPERTY(BlueprintReadOnly, Category = "Kinsect|Flight")
	FVector TargetPointSnapshot = FVector::ZeroVector;

	/** Aimed damaging send must physically touch a hitzone, not stop at a loose arrival radius. */
	UPROPERTY(BlueprintReadOnly, Category = "Kinsect|Flight")
	bool bRequireHitzoneContactForArrival = false;
	/** Frozen blocking-surface normal, used only to keep the center-launch capsule outside the surface. */
	UPROPERTY(BlueprintReadOnly, Category = "Kinsect|Flight")
	FVector AimTargetSurfaceNormal = FVector::ZeroVector;

	/** Optional ToPoint target that follows a component/bone from its local point. */
	UPROPERTY(BlueprintReadOnly, Category = "Kinsect|Flight")
	TWeakObjectPtr<UPrimitiveComponent> TargetComponent;
	UPROPERTY(BlueprintReadOnly, Category = "Kinsect|Flight")
	FName TargetBoneName = NAME_None;
	UPROPERTY(BlueprintReadOnly, Category = "Kinsect|Flight")
	FVector TargetLocalPoint = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Kinsect|Flight")
	bool bFollowTargetComponent = false;
	UPROPERTY(BlueprintReadOnly, Category = "Kinsect|Flight")
	bool bAllowWorldHitAtTarget = false;
	/** C++ action identity for the Resource-owned post-flight transaction. */
	uint32 ActionActivationSequenceID = 0;

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

	/** Per-hit powder policy, evaluated only after the matching damage GE succeeds. */
	UPROPERTY(BlueprintReadOnly, Category = "Kinsect|Flight")
	EIGPowderSpawnPolicy PowderSpawnPolicy = EIGPowderSpawnPolicy::None;

	/** 请求所有者与当前Resource命令代数；自动请求另绑定虫印代数。 */
	UPROPERTY(BlueprintReadOnly, Category = "Kinsect|Flight")
	EKinsectCommandSource CommandSource = EKinsectCommandSource::None;
	uint64 CommandSerial = 0;
	uint64 MarkSerial = 0;

	/** Powder attribution identity; distinct from each successful hit's HitInstanceID. */
	UPROPERTY(BlueprintReadOnly, Category = "Kinsect|Flight")
	FGuid SourceAttackInstanceID;
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
	bool CanBeginFlight(const FKinsectFlightRequest& Request) const;
	bool BeginFlight(const FKinsectFlightRequest& Request);
	static bool ResolveScreenCenterLaunchOrigin(const FVector& CameraOrigin,
		const FVector& CameraDirection, const FVector& OwnerLocation, FVector& OutOrigin);
	/** 由同一动作租约将棍尾临时附着原子转换为飞行，不先回手。 */
	bool BeginFlightFromActionAttachment(const FWeaponActionToken& ActionToken,
		const FKinsectFlightRequest& Request);

	/** 中断当前飞行/返回并转悬停；不清除 Pending 萃取。 */
	void Interrupt();

	/** 开始返回；幂等（Attached/Returning 时直接返回），保留 Pending。 */
	bool StartReturn(uint64 CommandSerial = 0,
		EKinsectCommandSource Source = EKinsectCommandSource::None);

	/** 强制召回（耐力归零等）；保留 Pending。 */
	void ForceRecall(uint64 CommandSerial = 0,
		EKinsectCommandSource Source = EKinsectCommandSource::System);
	/** 自动寻印飞行中的目标更新，需同时匹配请求代数和虫印代数。 */
	bool UpdateAutomaticMarkTarget(const FVector& TargetPoint,
		uint64 ExpectedCommandSerial, uint64 ExpectedMarkSerial);
	/** 已在虫印前方时登记自动接触请求，不伪造一次零长度飞行。 */
	bool BeginAutomaticMarkContact(const FKinsectFlightRequest& Request);
	/** 自动悬停时仅调整视觉朝向，不旋转靠地胶囊；请求身份必须仍有效。 */
	bool FaceAutomaticMark(uint64 ExpectedCommandSerial, uint64 ExpectedMarkSerial);
	/** 在当前虫印处进行真实 Hitzone 接触Trace并校验表面斑块，不直接应用伤害。 */
	bool SweepAutomaticMarkContact(uint64 ExpectedCommandSerial,
		uint64 ExpectedMarkSerial, const FWeaponRuntimeToken& RuntimeToken,
		FHitResult& OutHit) const;

	/** 附着到指定组件 Socket，保存组件与 Socket 供返回时实时追踪。 */
	void AttachToPlayer(USceneComponent* InAttachComponent, FName InSocketName = NAME_None);

	/** 由资源组件持有的动作租约调用；不修改正常手臂返回目标。 */
	bool BeginActionAttachment(const FWeaponActionToken& ActionToken,
		USkeletalMeshComponent* WeaponMesh, FName SocketName);
	/** 操虫斩起手快照；远处只在返回速度足够时直线回槽，不传送、不进行碰撞/萃取。 */
	bool BeginSlashPreparation(const FWeaponActionToken& ActionToken,
		USkeletalMeshComponent* WeaponMesh, FName SocketName,
		float AvailableReturnTime, bool bHasRequiredStamina);
	/** 前冲边界封口：未回槽的猎虫原地停住，不能迟到补入本次攻击。 */
	void FinalizeSlashPreparation(const FWeaponActionToken& ActionToken);
	bool IsActionControlOwnedBy(const FWeaponActionToken& ActionToken) const;
	bool IsSlashActionControlled() const { return bSlashPreparation && ActionAttachmentToken.IsValid(); }
	bool EndActionAttachmentAndReturn(const FWeaponActionToken& ActionToken,
		const FWeaponRuntimeToken& RuntimeToken, float ArrivalRadius,
		uint64 CommandSerial = 0,
		EKinsectCommandSource Source = EKinsectCommandSource::Action);
	bool IsActionAttachmentOwnedBy(const FWeaponActionToken& ActionToken) const;

	// ── 查询 ──
	EKinsectState GetState() const { return State; }
	/** 猎虫模型本地 +Y 为头部方向，不能使用物理胶囊的 Actor +X。 */
	FVector GetAttackForwardVector() const;
	/** 用 Sweep 接触时的 Location 判断正面，而非本帧末的位置，避免高速穿过后漏判。 */
	bool IsAttackContactInFront(const FHitResult& Hit) const;
	bool HasPendingExtract() const { return !PendingExtractColors.IsEmpty(); }
	/** 兼容普通单精华查询；多色显示必须读取完整容器。 */
	FGameplayTag GetPendingExtractColor() const;
	const FGameplayTagContainer& GetPendingExtractColors() const { return PendingExtractColors; }
	/** 单色兼容入口，仅取出首色；多色返回交付使用 TakePendingExtractColors。 */
	FGameplayTag TakePendingExtractColor();
	/** 原子取出完整携带精华，仅返回到主人时交付。 */
	FGameplayTagContainer TakePendingExtractColors();
	/** 普通/操虫斩只存首色；强化贯通显式允许红白橙去重多色。 */
	bool CapturePendingExtractColor(FGameplayTag ExtractColor, bool bAllowMultipleColors = false);
	float GetFlightSpeed() const;
	float GetReturnSpeed() const;
	float GetHoverDrainRate() const;
	float GetFlightDrainRate() const;
	bool IsDeployed() const
	{
		return State == EKinsectState::Flying
			|| State == EKinsectState::Hovering
			|| State == EKinsectState::Returning
			|| State == EKinsectState::ActionAttached
			|| State == EKinsectState::ActionReturning
			|| State == EKinsectState::ActionWaiting;
	}

	// ── 引用（由 URes_InsectGlaive 注入） ──
	UPROPERTY()
	TWeakObjectPtr<AActor> OwnerActor;

	UPROPERTY()
	TWeakObjectPtr<URes_InsectGlaive> ResourceComponent;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 世界碰撞回调——只缓存 PendingWorldHit 并停 Movement，不直接结束飞行。 */
	UFUNCTION()
	void OnWorldCollision(UPrimitiveComponent* HitComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FMHGZM6PowderVortexAimsFromSnapshotAndManualRecallSupersedes;
	friend class FMHGZKinsectAudioLifecycleTests;
#endif

	/** 只读当前 Runtime/状态；不加载音源，不改变飞行、碰撞或指令身份。 */
	USoundBase* ResolveFlightLoopSound() const;
	void RefreshFlightAudio(bool bPlayLaunch = false);

	UPROPERTY(VisibleAnywhere, Category = "Kinsect|Audio")
	TObjectPtr<UAudioComponent> FlightAudio;

	/** 校验请求；纯查询，不修改状态。 */
	bool IsFlightRequestValid(const FKinsectFlightRequest& Request) const;
	bool BeginFlightInternal(const FKinsectFlightRequest& Request,
		const FWeaponActionToken* ActionAttachmentOwner);

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
	bool ArmMovement(const FVector& Direction, float Speed, bool bKeepCollisionOrientation = false);

	/** 在附着原始朝向与飞行朝向修正之间切换猎虫视觉 Mesh。 */
	void SetFlightVisualFacing(bool bInFlight);

	/** 返回 Tick：实时追踪附着 Socket 世界位置；到达后取 Pending → Attach → 回调一次。 */
	void TickReturn();
	void StartReturnInternal();
	void TickSlashPreparation(float DeltaTime);
	void ResetSlashPreparation();

	/** 解析返回目标的世界位置（优先附着 Socket，回退 OwnerActor）。 */
	bool ResolveAttachSocketLocation(FVector& OutLocation) const;
	bool ResolveToPointTarget(const FKinsectFlightRequest& Request,
		FVector& OutLocation) const;
	void UpdateMovingToPointTarget();

	// 飞行
	FVector FlightStartLocation = FVector::ZeroVector;
	FVector LastTickLocation = FVector::ZeroVector;
	FHitResult PendingWorldHit;
	bool bHasDealtDamage = false;
	FWeaponActionToken ActionAttachmentToken;
	TWeakObjectPtr<USkeletalMeshComponent> SlashAttachMesh;
	FName SlashAttachSocket = NAME_None;
	float SlashReturnTimeRemaining = 0.f;
	bool bSlashPreparation = false;
	FWeaponRuntimeToken ReturnRuntimeToken;
	uint64 ReturnCommandSerial = 0;
	uint64 ReturnMarkSerial = 0;
	EKinsectCommandSource ReturnCommandSource = EKinsectCommandSource::None;
	float ReturnArrivalRadius = RETURN_ARRIVAL_DISTANCE;
	FVector LastReturnRelativeLocation = FVector::ZeroVector;
	bool bHasLastReturnRelativeLocation = false;

	// 贯穿：每个 Hitzone 组件上次成功伤害的绝对世界时间。
	TMap<TWeakObjectPtr<UMHGZMonsterHitzoneComponent>, float> HitzoneHitTimers;

	// 萃取（无颜色优先级、无部位名映射）
	UPROPERTY()
	FGameplayTagContainer PendingExtractColors;

	// 返回附着目标
	UPROPERTY()
	TWeakObjectPtr<USceneComponent> AttachComponent;

	FName AttachSocketName = NAME_None;

	// 只有附着时记录的美术原始相对旋转会在回到手臂 Socket 后恢复。
	FRotator AttachedMeshRelativeRotation = FRotator::ZeroRotator;
	FVector AttachedMeshRelativeScale = FVector::OneVector;
	bool bHasAttachedMeshRelativeRotation = false;

	static constexpr float RETURN_ARRIVAL_DISTANCE = 50.f; // 无已提交 Request 时的防御回退
	static constexpr float RETURN_SWEEP_TOLERANCE = 5.f; // 返回路径跨帧可捕获半径，避免高速越过手臂 Socket
};
