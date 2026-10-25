// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActionSystem/MHGZRecordedLocalTrajectory.h"
#include "ActionSystem/MHGZSegmentedDirectionalMovement.h"
#include "Engine/HitResult.h"
#include "GameplayAbilitySpecHandle.h"
#include "GameplayTagContainer.h"
#include "ScalableFloat.h"
#include "MHGZWeaponRuntimeTypes.generated.h"

class ACharacter;
class APlayerController;
class UAbilitySystemComponent;
class UCurveFloat;
class UCurveVector;
class UGameplayAbility;
class UMHGZAbilitySystemComponent;
class UMHGZEquipmentComponent;
class UMHGZWeaponRuntimeHostComponent;
class UMHGZWeaponDefinition;
class UWeaponCombatConfigBase;
class USkeletalMeshComponent;

/** 输入产生时冻结的方向分类。None 表示该转移不要求方向。 */
UENUM(BlueprintType)
enum class EDirectionalInput : uint8
{
	None    UMETA(DisplayName = "无方向要求"),
	Forward UMETA(DisplayName = "前"),
	Back    UMETA(DisplayName = "后"),
	Left    UMETA(DisplayName = "左"),
	Right   UMETA(DisplayName = "右")
};

UENUM(BlueprintType)
enum class EWeaponActionEndReason : uint8
{
	Normal,
	Cancelled,
	Interrupted,
	Superseded,
	Hit,
	Landed,
	Death,
	WeaponChanged,
	RuntimeShutdown
};

UENUM(BlueprintType)
enum class EWeaponRuntimeEndReason : uint8
{
	WeaponChanged,
	AvatarChanged,
	Death,
	EndPlay,
	RuntimeShutdown
};

/** 猎虫当前请求的所有权来源；自动寻印始终低于有效玩家/招式指令。 */
UENUM(BlueprintType)
enum class EKinsectCommandSource : uint8
{
	None UMETA(DisplayName = "None"),
	Player UMETA(DisplayName = "Player Command"),
	Action UMETA(DisplayName = "Action Lease"),
	AutoMark UMETA(DisplayName = "Automatic Mark"),
	System UMETA(DisplayName = "System Return"),
	PowderVortex UMETA(DisplayName = "Powder Vortex"),
	CoAttack UMETA(DisplayName = "Ground Cooperative Attack"),
	DescendingThrust UMETA(DisplayName = "Descending Thrust Kinsect")
};

/**
 * A deliberately small routing vocabulary for the point where a functional
 * Montage hands its no-function tail to Motion Matching.  The enum identifies
 * a candidate *family*, never a concrete animation: E4.2's Chooser selects a
 * Pose Search database from it and Pose Search still makes the final choice.
 */
UENUM(BlueprintType)
enum class EMHGZMotionMatchingHandoffType : uint8
{
	None UMETA(DisplayName = "None"),
	SheatheMoveExit UMETA(DisplayName = "Sheathe Move Exit"),
	DodgeMoveExit UMETA(DisplayName = "Dodge Move Exit"),
	DrawExit UMETA(DisplayName = "Draw Exit"),
	AttackExit UMETA(DisplayName = "Attack Exit")
};

/** Normal release blends the visual correction back to identity; exceptional cleanup snaps it off. */
UENUM(BlueprintType)
enum class EActionVisualPitchReleaseMode : uint8
{
	BlendOut UMETA(DisplayName = "Blend Out"),
	ImmediateReset UMETA(DisplayName = "Immediate Reset")
};

/** 临时 Loose Tag 的所有权来源种类 */
UENUM(BlueprintType)
enum class EWeaponTagOwnerKind : uint8
{
	Input        UMETA(DisplayName = "Input"),
	Transition   UMETA(DisplayName = "Transition"),
	Ability      UMETA(DisplayName = "Ability"),
	NotifyWindow UMETA(DisplayName = "Notify Window"),
	Resource     UMETA(DisplayName = "Resource"),
	Pose         UMETA(DisplayName = "Pose")
};

/** Ability 耐力成本结算策略；PerSecond 由 StaminaDrain 任务按真实经过时间结算 */
UENUM(BlueprintType)
enum class EAbilityStaminaCostPolicy : uint8
{
	None,
	Instant,
	PerSecond
};

/** 瞄准快照上下文；LT+RT 虫印 / LT+B 操虫斩使用 Kinsect，RT+Y+B 觉虫击使用 Action */
UENUM(BlueprintType)
enum class EWeaponAimSnapshotContext : uint8
{
	None,
	Kinsect,
	Action,
	Slinger
};

/** 输入相位；离散武器动作只由 Started/组合解析产生，Completed 只用于释放身份 */
UENUM(BlueprintType)
enum class EWeaponInputPhase : uint8
{
	Started,
	Triggered,
	Completed
};

/** 通用武器位移模式 */
UENUM(BlueprintType)
enum class EWeaponMovementMode : uint8
{
	BoundedDirectional,
	BallisticVault,
	/** A recorded, collision-aware path: MoveToForce supplies planar target while a vector curve supplies the local offset. */
	CurvedVault,
	AdditiveInertia,
	/** A 3D bounded dash whose normalized progress is driven by DistanceCurve. */
	CurvedDirectional,
	/** Owns a stationary aerial lead until the authored movement begins. */
	StationaryHover,
	/** CMC Falling integrates an authored launch velocity and planar acceleration for a bounded interval. */
	AuthoredAerialPhysics,
	/** CMC-owned hold/straight/glide, then Falling in the same physics update. */
	SegmentedDirectional,
	/**
	 * CMC owns the capsule from an authored local CurveVector: every physics
	 * substep applies Curve(t1)-Curve(t0) in the frozen yaw frame. The curve is
	 * the phase's *only* displacement source — no inherited inertia, no Montage
	 * root motion, no stick acceleration, and no second helping of gravity.
	 *
	 * Appended last on purpose: the enum's earlier values are serialized.
	 */
	RecordedLocalTrajectory
};

/** BallisticVault 的弹道参数来源；两者必须且只能选择其一 */
UENUM(BlueprintType)
enum class EBallisticParameterMode : uint8
{
	ApexHeightAndDuration,
	ExplicitLaunchVelocity
};

/** 动作期间的旋转所有权策略；Character locomotion 不得无条件覆盖旋转 */
UENUM(BlueprintType)
enum class EActionRotationPolicy : uint8
{
	Locked,
	FaceDirection,
	SteerWithinCone
};

/** 位移被取消/中断时对 CMC 速度的处理策略 */
UENUM(BlueprintType)
enum class EMovementCancelVelocityPolicy : uint8
{
	PreserveVelocity,
	ZeroVelocity,
	NoChange
};

/** 位移任务的唯一结束原因；每次位移必须恰好产生一个结果 */
UENUM(BlueprintType)
enum class EWeaponMovementEndReason : uint8
{
	Completed,
	HitHitzone,
	BlockingHit,
	Landed,
	Interrupted,
	Cancelled,
	Death,
	WeaponChanged,
	RuntimeShutdown,
	Failed
};

/**
 * 武器运行时身份 Token：Host 弱引用 + 单调递增 Generation。
 * 所有跨异步回调都必须携带并核对完整 Token（Host 与 Generation 同时比较）。
 */
USTRUCT(BlueprintType)
struct FWeaponRuntimeToken
{
	GENERATED_BODY()

	/** 持有武器运行期的宿主，用于验证当前代际与动作身份。 */
	UPROPERTY()
	TWeakObjectPtr<UMHGZWeaponRuntimeHostComponent> Host;

	/** 宿主当前的运行期代际；换武器或重建时旧代际失效。 */
	UPROPERTY()
	uint64 Generation = 0;

	bool IsValid() const
	{
		return Host.IsValid() && Generation != 0;
	}

	bool operator==(const FWeaponRuntimeToken& Other) const
	{
		return Host == Other.Host && Generation == Other.Generation;
	}

	bool operator!=(const FWeaponRuntimeToken& Other) const
	{
		return !(*this == Other);
	}
};

inline uint32 GetTypeHash(const FWeaponRuntimeToken& Token)
{
	return HashCombine(GetTypeHash(Token.Host.Get()), GetTypeHash(Token.Generation));
}

/** 武器运行时上下文：RuntimeHost 初始化/重建时冻结的当前接线快照 */
USTRUCT(BlueprintType)
struct FWeaponRuntimeContext
{
	GENERATED_BODY()

	/** 当前武器运行期的身份令牌；代际变化后旧回调和旧请求失效。 */
	UPROPERTY()
	FWeaponRuntimeToken RuntimeToken;

	/** 当前运行期关联的角色实例。 */
	UPROPERTY()
	TWeakObjectPtr<ACharacter> Character;

	/** 当前运行期关联的玩家控制器。 */
	UPROPERTY()
	TWeakObjectPtr<APlayerController> Controller;

	/** 当前关联角色的能力系统组件，用于读取标签、属性和执行效果。 */
	UPROPERTY()
	TWeakObjectPtr<UMHGZAbilitySystemComponent> ASC;

	/** 提供当前已装备武器及其不可变快照的装备组件。 */
	UPROPERTY()
	TWeakObjectPtr<UMHGZEquipmentComponent> Equipment;

	/** 当前武器定义（物品攻击力/外观/词条）；消费者只读。 */
	UPROPERTY()
	TObjectPtr<UMHGZWeaponDefinition> WeaponDefinition;

	/** 当前武器类型的战斗配置；RuntimeHost 从 RuntimeDefinition 注入，消费者只读。 */
	UPROPERTY()
	TObjectPtr<UWeaponCombatConfigBase> CombatConfig;
};

/** 一次武器动作激活的精确身份；Notify/位移/命中/资源回调用它解析归属 */
USTRUCT(BlueprintType)
struct FWeaponActionToken
{
	GENERATED_BODY()

	/** 当前武器运行期的身份令牌；代际变化后旧回调和旧请求失效。 */
	UPROPERTY()
	FWeaponRuntimeToken RuntimeToken;

	/** 所属能力在ASC中的授予句柄；与激活序号共同确定动作身份。 */
	UPROPERTY()
	FGameplayAbilitySpecHandle AbilityHandle;

	/** 本次能力激活的单调序号，用于区分同一能力的连续执行。 */
	UPROPERTY()
	uint32 ActivationSequenceID = 0;

	/** 此次激活的能力实例引用；不是共享的能力类默认对象。 */
	UPROPERTY()
	TWeakObjectPtr<UGameplayAbility> AbilityInstance;

	bool IsValid() const
	{
		return RuntimeToken.IsValid() && AbilityHandle.IsValid()
			&& ActivationSequenceID != 0 && AbilityInstance.IsValid();
	}

	bool operator==(const FWeaponActionToken& Other) const
	{
		return RuntimeToken == Other.RuntimeToken
			&& AbilityHandle == Other.AbilityHandle
			&& ActivationSequenceID == Other.ActivationSequenceID
			&& AbilityInstance == Other.AbilityInstance;
	}

	bool operator!=(const FWeaponActionToken& Other) const
	{
		return !(*this == Other);
	}
};

/** Montage/Notify 注册项：由 (Mesh, MontageInstanceID) 精确解析到 ActionToken */
USTRUCT(BlueprintType)
struct FWeaponMontageRegistration
{
	GENERATED_BODY()

	/** 精确动作身份，关联运行期、能力实例和激活序号，用于隔离迟到回调。 */
	UPROPERTY()
	FWeaponActionToken ActionToken;

	/** 真正播放此蒙太奇的骨骼网格组件，用于区分不同网格上的同名动作。 */
	UPROPERTY()
	TWeakObjectPtr<USkeletalMeshComponent> Mesh;

	/** 本次播放对应的蒙太奇实例编号；重播后旧编号不得接管新动作。 */
	UPROPERTY()
	int32 MontageInstanceID = INDEX_NONE;
};

/** Host-owned, exact-action state consumed read-only by the character AnimInstance. */
USTRUCT(BlueprintType)
struct FWeaponActionVisualPitchState
{
	GENERATED_BODY()

	/** 持有该请求或表现的动作身份；仅所属动作可更新或释放。 */
	UPROPERTY()
	FWeaponActionToken OwnerAction;

	/** 建立视觉俯仰表现的蒙太奇实例编号。 */
	UPROPERTY()
	int32 SourceMontageInstanceID = INDEX_NONE;

	/** 本次视觉俯仰锁定的世界方向，不随后续相机输入重新瞄准。 */
	UPROPERTY()
	FVector LockedDirection = FVector::ForwardVector;

	/** 视觉俯仰的目标权重，取值0到1。 */
	UPROPERTY()
	float TargetWeight = 0.0f;

	/** 视觉俯仰从当前权重切入目标权重的时长，单位为秒。 */
	UPROPERTY()
	float BlendInSeconds = 0.1f;

	/** 释放视觉俯仰时淡回零权重的时长，单位为秒。 */
	UPROPERTY()
	float BlendOutSeconds = 0.1f;

	/** 视觉俯仰状态的修订序号，用于识别新的设置或释放请求。 */
	UPROPERTY()
	uint64 StateRevision = 0;

	/** 当前是否持有有效的动作视觉俯仰状态。 */
	UPROPERTY()
	bool bValid = false;

	/** 是否处于只读的视觉俯仰淡出阶段。 */
	UPROPERTY()
	bool bReleasing = false;
};

/** Sparse action-pitch lifecycle event copied into the active RuntimeTelemetry session. */
struct FWeaponActionVisualPitchTelemetryEvent
{
	uint64 Serial = 0;
	uint64 Frame = 0;
	double WorldTimeSeconds = 0.0;
	FName EventName = NAME_None;
	FWeaponActionToken OwnerAction;
	int32 SourceMontageInstanceID = INDEX_NONE;
	FVector LockedDirection = FVector::ForwardVector;
	float TargetWeight = 0.0f;
	float BlendInSeconds = 0.0f;
	float BlendOutSeconds = 0.0f;
	uint64 StateRevision = 0;
	bool bValid = false;
	bool bReleasing = false;
};

/** Sparse combat lifecycle event forwarded to the active RuntimeTelemetry writer. */
struct FWeaponCombatTelemetryEvent
{
	uint64 Serial = 0;
	uint64 Frame = 0;
	double WorldTimeSeconds = 0.0;
	FName EventName = NAME_None;
	uint64 RuntimeGeneration = 0;
	uint32 ActivationSequenceID = 0;
	FGuid MarkID;
	uint64 CommandSerial = 0;
	FGuid FlightID;
	FGuid SourceAttackID;
	FGuid HitID;
	FGuid PowderID;
	uint64 ReservationID = 0;
	FGuid ExplosionID;
	int32 StageIndex = INDEX_NONE;
	FGuid StageAttackID;
	FName TargetActor = NAME_None;
	FName Hitzone = NAME_None;
	FVector ImpactPoint = FVector::ZeroVector;
	FName SpawnPolicy = NAME_None;
	FName ResourceTag = NAME_None;
	FName Reason = NAME_None;
	double ExpireAt = 0.0;
	int32 LifetimeRevision = 0;
	int32 MemberCount = 0;
	float Damage = 0.0f;
};

/**
 * Runtime-only, generation-scoped payload published by the exact owning
 * Action when an authored MotionMatchingHandoff notify succeeds.  It survives
 * that Action's normal EndAbility cleanup so the AnimBP/Chooser can consume it
 * on the following MM update, but the next unrelated Action registration
 * invalidates it.  It deliberately contains no selected animation.
 */
USTRUCT(BlueprintType)
struct FWeaponMotionMatchingHandoff
{
	GENERATED_BODY()

	/** 本次交接允许搜索的退出候选家族，不直接指定动画序列。 */
	UPROPERTY(BlueprintReadOnly)
	EMHGZMotionMatchingHandoffType Type = EMHGZMotionMatchingHandoffType::None;

	/** 交接发布序号；动画节点按序号仅消费一次。 */
	UPROPERTY(BlueprintReadOnly)
	int64 Serial = 0;

	/** 本次能力激活的单调序号，用于区分同一能力的连续执行。 */
	UPROPERTY(BlueprintReadOnly)
	int32 ActivationSequenceID = 0;

	/** 交接瞬间的物理摇杆轴快照，不读取上次非零输入缓存。 */
	UPROPERTY(BlueprintReadOnly)
	FVector2D RawMoveInput = FVector2D::ZeroVector;

	/** 交接瞬间是否仍有超过中立阈值的物理移动输入。 */
	UPROPERTY(BlueprintReadOnly)
	bool bHasRawMoveInputAtHandoff = false;

	/** 动作可移动尾段内是否曾记录到有效物理移动输入。 */
	UPROPERTY(BlueprintReadOnly)
	bool bHadRawMoveInputInMobilePhase = false;

	/** 动作可移动尾段内是否观察到物理移动输入被收回。 */
	UPROPERTY(BlueprintReadOnly)
	bool bReleasedRawMoveInputInMobilePhase = false;

	/** 仅在可移动退出段观察到有效输入及其后续释放时，携带待停步请求。 */
	UPROPERTY(BlueprintReadOnly)
	bool bPendingStopAtHandoff = false;

	/** 可移动尾段最后一次非中立输入所对应的配置巡航挡位速度，单位为厘米每秒。 */
	UPROPERTY(BlueprintReadOnly)
	float LastActiveRawMoveCruiseSpeedInMobilePhase = 0.0f;

	/** 宿主用于作废陈旧交接载荷的内部动作身份。 */
	UPROPERTY()
	FWeaponActionToken ActionToken;

	bool IsValid() const
	{
		return Type != EMHGZMotionMatchingHandoffType::None && Serial > 0;
	}
};

/** TagLedger 计数账本中的所有权身份；Release 只回退该 OwnerID 增加的计数 */
USTRUCT(BlueprintType)
struct FWeaponTagOwnerID
{
	GENERATED_BODY()

	/** 当前武器运行期的身份令牌；代际变化后旧回调和旧请求失效。 */
	UPROPERTY()
	FWeaponRuntimeToken RuntimeToken;

	/** 该批标签的所有者类别，区分动作、运行期及其他授权来源。 */
	UPROPERTY()
	EWeaponTagOwnerKind Kind = EWeaponTagOwnerKind::Input;

	/** 所属能力在ASC中的授予句柄；与激活序号共同确定动作身份。 */
	UPROPERTY()
	FGameplayAbilitySpecHandle AbilityHandle;

	/** 本次能力激活的单调序号，用于区分同一能力的连续执行。 */
	UPROPERTY()
	uint32 ActivationSequenceID = 0;

	/** TransitionID 或 NotifyEventID */
	UPROPERTY()
	FName LocalID;

	bool operator==(const FWeaponTagOwnerID& Other) const
	{
		return RuntimeToken == Other.RuntimeToken && Kind == Other.Kind
			&& AbilityHandle == Other.AbilityHandle
			&& ActivationSequenceID == Other.ActivationSequenceID
			&& LocalID == Other.LocalID;
	}
};

/** Acquire 返回的唯一计数 Token；重复释放无效果 */
USTRUCT(BlueprintType)
struct FWeaponOwnedTagToken
{
	GENERATED_BODY()

	/** 当前武器运行期的身份令牌；代际变化后旧回调和旧请求失效。 */
	UPROPERTY()
	FWeaponRuntimeToken RuntimeToken;

	/** 该批已持有标签的唯一令牌，释放时只撤销本令牌的贡献。 */
	UPROPERTY()
	uint64 TokenID = 0;

	bool IsValid() const
	{
		return RuntimeToken.IsValid() && TokenID != 0;
	}
};

/**
 * RuntimeHost 持有的 Loose Tag 有所有权计数账本。
 * M0 冻结接口；M1 接入 ASC 后实现 Acquire/Release 的计数与幂等语义。
 */
class MHGZ_API FWeaponRuntimeTagLedger
{
public:
	void Initialize(const FWeaponRuntimeToken& InRuntimeToken, UAbilitySystemComponent* InASC);
	FWeaponOwnedTagToken Acquire(const FWeaponTagOwnerID& OwnerID, const FGameplayTagContainer& Tags);
	bool Release(const FWeaponOwnedTagToken& Token);
	int32 ReleaseOwner(const FWeaponTagOwnerID& OwnerID);
	void ReleaseAll(const FWeaponRuntimeToken& RuntimeToken);
	bool IsActive(const FWeaponOwnedTagToken& Token) const;

private:
	struct FOwnedEntry
	{
		FWeaponTagOwnerID OwnerID;
		FGameplayTagContainer Tags;
	};

	FWeaponRuntimeToken ActiveRuntimeToken;
	TWeakObjectPtr<UAbilitySystemComponent> ASC;
	TMap<uint64, FOwnedEntry> Entries;
	uint64 NextTokenID = 1;
};

/** 武器资源成本规格；通用 GA 只透传 CostType/Amount，不解释含义 */
USTRUCT(BlueprintType)
struct FWeaponResourceCostSpec
{
	GENERATED_BODY()

	/** 要消耗的武器专属资源种类，由资源提供方解释。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag CostType;

	/** 本次需要预留并在提交成功后消耗的资源数量。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FScalableFloat Amount;
};

/** TryReserveCosts 返回的资源预留；在 Commit 失败时 Release，成功时 Consume */
USTRUCT(BlueprintType)
struct FWeaponResourceCostReservation
{
	GENERATED_BODY()

	/** 当前武器运行期的身份令牌；代际变化后旧回调和旧请求失效。 */
	UPROPERTY()
	FWeaponRuntimeToken RuntimeToken;

	/** 本次能力激活的单调序号，用于区分同一能力的连续执行。 */
	UPROPERTY()
	uint32 ActivationSequenceID = 0;

	/** 本次资源预留事务的唯一标识，防止重复提交或退回。 */
	UPROPERTY()
	uint64 ReservationID = 0;

	bool IsValid() const
	{
		return RuntimeToken.IsValid() && ReservationID != 0;
	}
};

/** 瞄准快照：Context、Origin、Direction、TargetPoint 与可选真实 HitResult */
USTRUCT(BlueprintType)
struct FWeaponAimSnapshot
{
	GENERATED_BODY()

	/** 取得快照时的瞄准上下文，区分当前瞄准用途。 */
	UPROPERTY(BlueprintReadOnly)
	EWeaponAimSnapshotContext Context = EWeaponAimSnapshotContext::None;

	/** 瞄准射线的世界空间起点，单位为厘米。 */
	UPROPERTY(BlueprintReadOnly)
	FVector Origin = FVector::ZeroVector;

	/** 瞄准射线的归一化世界方向。 */
	UPROPERTY(BlueprintReadOnly)
	FVector Direction = FVector::ForwardVector;

	/** 射线确定的世界目标点，单位为厘米。 */
	UPROPERTY(BlueprintReadOnly)
	FVector TargetPoint = FVector::ZeroVector;

	/** 是否保存了可作为目标的受击部位命中结果。 */
	UPROPERTY(BlueprintReadOnly)
	bool bHasHitResult = false;

	/** 目标受击部位的真实射线命中信息。 */
	UPROPERTY(BlueprintReadOnly)
	FHitResult HitResult;

	/** 是否命中第一个阻挡表面；地形也可阻挡射线，不要求是受击部位。 */
	UPROPERTY(BlueprintReadOnly)
	bool bHasBlockingHit = false;

	/** 瞄准射线第一个阻挡表面的命中信息。 */
	UPROPERTY(BlueprintReadOnly)
	FHitResult BlockingHit;
};

/** A fresh screen-centre sample resolved for a late action-direction lock. */
USTRUCT(BlueprintType)
struct FWeaponAimLockSample
{
	GENERATED_BODY()

	/** 锁定瞄准时使用的相机世界位置。 */
	UPROPERTY(BlueprintReadOnly)
	FVector CameraOrigin = FVector::ZeroVector;

	/** 锁定瞄准时使用的相机归一化世界方向。 */
	UPROPERTY(BlueprintReadOnly)
	FVector CameraDirection = FVector::ForwardVector;

	/** 锁定射线确定的世界目标点。 */
	UPROPERTY(BlueprintReadOnly)
	FVector TargetPoint = FVector::ZeroVector;

	/** 结合实际发射位置得到的归一化发射方向。 */
	UPROPERTY(BlueprintReadOnly)
	FVector LaunchDirection = FVector::ForwardVector;

	/** 射线是否存在首个阻挡命中，包含墙面；不会穿过该阻挡继续瞄准。 */
	UPROPERTY(BlueprintReadOnly)
	bool bHasBlockingHit = false;

	/** 锁定瞄准时的首个阻挡命中结果。 */
	UPROPERTY(BlueprintReadOnly)
	FHitResult BlockingHit;

	/** 没有可用本地玩家视点时，是否回退使用角色正前方。 */
	UPROPERTY(BlueprintReadOnly)
	bool bUsedActorForwardFallback = false;
};

/** 路由器输出的不可变输入快照；GA 激活后不得重新读取摇杆/相机 */
USTRUCT(BlueprintType)
struct FWeaponInputSnapshot
{
	GENERATED_BODY()

	/** 路由解析完成后的逻辑输入标签。 */
	UPROPERTY(BlueprintReadOnly)
	FGameplayTag ResolvedInputTag;

	/** 产生该输入的原始物理控制标签，用于对应按下与释放。 */
	UPROPERTY(BlueprintReadOnly)
	FGameplayTag SourceControlTag;

	/** 取得快照时仍按住的修饰键标签集合。 */
	UPROPERTY(BlueprintReadOnly)
	FGameplayTagContainer HeldModifierTags;

	/** Grounded/Aerial、Sheathed/Unsheathed、Aim 子集 */
	UPROPERTY(BlueprintReadOnly)
	FGameplayTagContainer ContextTags;

	/** 取得快照时的物理移动轴值；保持中立值，不借用历史输入。 */
	UPROPERTY(BlueprintReadOnly)
	FVector2D RawMoveInput = FVector2D::ZeroVector;

	/** 快照当时按相机水平朝向计算的世界方向；轴中立、低于方向阈值或控制器朝向不可用时为零，不使用最后非零方向缓存。 */
	UPROPERTY(BlueprintReadOnly)
	FVector WorldDirection = FVector::ZeroVector;

	/** 输入解析瞬间的角色 Forward；收刀拔刀等动作不得在 GA 激活后重新采样。 */
	UPROPERTY(BlueprintReadOnly)
	FVector ActorForward = FVector::ForwardVector;

	/** 该配置或输入快照所属的离散方向。 */
	UPROPERTY(BlueprintReadOnly)
	EDirectionalInput Direction = EDirectionalInput::None;

	/** 取得快照时一并锁定的瞄准信息。 */
	UPROPERTY(BlueprintReadOnly)
	FWeaponAimSnapshot Aim;

	/** 该输入处于开始、持续或完成等哪个阶段。 */
	UPROPERTY(BlueprintReadOnly)
	EWeaponInputPhase Phase = EWeaponInputPhase::Started;

	/** 输入快照的路由器世界时间戳，单位为秒。 */
	UPROPERTY(BlueprintReadOnly)
	double Timestamp = 0.0;

	/** 原始控制键本次物理按下的起始世界时间，单位为秒。 */
	UPROPERTY(BlueprintReadOnly)
	double PhysicalPressStartedAtSeconds = 0.0;

	/** 实际按下到释放的持续时长，单位为秒；仅在完成或释放回退快照有效。 */
	UPROPERTY(BlueprintReadOnly)
	float PhysicalPressDurationSeconds = 0.0f;

	/** 快照是否包含有效的物理按键持续时长。 */
	UPROPERTY(BlueprintReadOnly)
	bool bHasPhysicalPressDuration = false;

	/** 逻辑输入快照的唯一序号，用于去重及匹配消费。 */
	UPROPERTY()
	uint32 SequenceID = 0;
};

/** Coordinator 冻结并随本次激活传递的不可变上下文。 */
USTRUCT(BlueprintType)
struct FWeaponAbilityActivationContext
{
	GENERATED_BODY()

	/** 当前武器运行期的身份令牌；代际变化后旧回调和旧请求失效。 */
	UPROPERTY(BlueprintReadOnly)
	FWeaponRuntimeToken RuntimeToken;

	/** 本次能力激活的单调序号，用于区分同一能力的连续执行。 */
	UPROPERTY()
	uint32 ActivationSequenceID = 0;

	/** 连招边的稳定标识，用于匹配转移记录和诊断来源。 */
	UPROPERTY(BlueprintReadOnly)
	FName TransitionID;

	/** 这次连招转移出发时的源状态。 */
	UPROPERTY(BlueprintReadOnly)
	FName SourceState;

	/** 这次连招转移完成后进入的目标状态。 */
	UPROPERTY(BlueprintReadOnly)
	FName TargetState;

	/** 该连招边覆盖蒙太奇混入时长的值，单位为秒；负数沿用目标资产默认值。 */
	UPROPERTY(BlueprintReadOnly)
	float MontageBlendInTime = -1.0f;

	/** 该连招边覆盖起手水平修正上限的值，单位为度；负数沿用目标GA默认值。 */
	UPROPERTY(BlueprintReadOnly)
	float MaxCorrectionAngle = -1.0f;

	/** 驱动这次能力激活的不可变输入快照。 */
	UPROPERTY(BlueprintReadOnly)
	FWeaponInputSnapshot Input;
};

USTRUCT(BlueprintType)
struct FActiveComboTransition
{
	GENERATED_BODY()

	/** 连招边的稳定标识，用于匹配转移记录和诊断来源。 */
	UPROPERTY(BlueprintReadOnly)
	FName TransitionID;

	/** 精确动作身份，关联运行期、能力实例和激活序号，用于隔离迟到回调。 */
	UPROPERTY(BlueprintReadOnly)
	FWeaponActionToken ActionToken;

	/** 这次连招转移出发时的源状态。 */
	UPROPERTY(BlueprintReadOnly)
	FName SourceState;

	/** 这次连招转移完成后进入的目标状态。 */
	UPROPERTY(BlueprintReadOnly)
	FName TargetState;

	/** 当前已确认转移持有的标签，退出时只释放这些标签的所有权。 */
	UPROPERTY(BlueprintReadOnly)
	FGameplayTagContainer OwnedTags;

	/** 当前转移是否已收到首次成功命中，用于一次性命中授权。 */
	UPROPERTY(BlueprintReadOnly)
	bool bFirstHitReceived = false;
};

/** TryActivate 与 GA Commit/Confirm 之间的短生命周期事务。 */
USTRUCT()
struct FPendingComboTransition
{
	GENERATED_BODY()

	/** 连招边的稳定标识，用于匹配转移记录和诊断来源。 */
	UPROPERTY()
	FName TransitionID;

	/** 所属能力在ASC中的授予句柄；与激活序号共同确定动作身份。 */
	UPROPERTY()
	FGameplayAbilitySpecHandle AbilityHandle;

	/** 本次能力激活的单调序号，用于区分同一能力的连续执行。 */
	UPROPERTY()
	uint32 ActivationSequenceID = 0;

	/** 等待激活确认的连招上下文，提交前不替代当前转移。 */
	UPROPERTY()
	FWeaponAbilityActivationContext ActivationContext;

	/** 连招状态机中被本次转移接替的上一动作身份。 */
	UPROPERTY()
	FWeaponActionToken PreviousActionToken;

	/** 真正通过空中交接放行本次转移的动作身份；可来自未记录在当前连招边中的空回，与状态机前驱身份分开。 */
	UPROPERTY()
	FWeaponActionToken AerialHandoffSourceActionToken;
};

/** 通用武器位移请求（运行时结构，不是 DataAsset；标量调优在 CombatConfig 验证） */
USTRUCT(BlueprintType)
struct FWeaponMovementRequest
{
	GENERATED_BODY()

	/** 持有该请求或表现的动作身份；仅所属动作可更新或释放。 */
	UPROPERTY(BlueprintReadOnly)
	FWeaponActionToken OwnerAction;

	/** 本次位移采用的运动模式，决定实际读取哪些轨迹参数。 */
	UPROPERTY(BlueprintReadOnly)
	EWeaponMovementMode Mode = EWeaponMovementMode::BoundedDirectional;

	/** 本次位移锁定的世界方向，不在运动中读取过期输入快照。 */
	UPROPERTY(BlueprintReadOnly)
	FVector DirectionSnapshot = FVector::ForwardVector;

	/** 当前运动模式使用的最大移动距离，单位为厘米。 */
	UPROPERTY(BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float MaxDistance = 0.f;

	/** 本段动作拥有位移的时长，单位为游戏秒。 */
	UPROPERTY(BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float Duration = 0.f;

	/** 仅分段定向运动读取的轨迹配置；世界重力与发射俯仰独立。 */
	UPROPERTY(BlueprintReadOnly)
	FSegmentedDirectionalMovementProfile SegmentedProfile;

	/** 仅录制局部轨迹模式读取。局部X沿锁定水平前向，Y沿世界上向叉乘前向，Z沿世界上向。 */
	UPROPERTY(BlueprintReadOnly)
	FRecordedLocalTrajectoryProfile RecordedTrajectory;

	/** 对录制轨迹逐轴缩放；单位缩放保持原轨迹。急袭突刺用X轴缩放锁定水平速度，而源曲线X轴仍表示时间积分。 */
	UPROPERTY(BlueprintReadOnly)
	FVector RecordedTrajectoryScale = FVector::OneVector;

	/** 录制局部轨迹模式遇到地面时采用的处理策略。 */
	UPROPERTY(BlueprintReadOnly)
	ERecordedTrajectoryGroundPolicy TrajectoryGroundPolicy
		= ERecordedTrajectoryGroundPolicy::ProtectedFlight;

	/** 录制轨迹超出有效时间范围后采用的尾段策略。 */
	UPROPERTY(BlueprintReadOnly)
	ERecordedTrajectoryTailPolicy TrajectoryTailPolicy
		= ERecordedTrajectoryTailPolicy::HoldLastKey;

	/** 曲线定向模式使用的0到1归一化前进进度；其他模式不读取。 */
	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<UCurveFloat> DistanceCurve;

	/** 曲线撑杆跳的归一化路径偏移：X沿锁定前向、Y横向、Z竖直；首尾偏移必须为零，端点由最大距离确定。 */
	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<UCurveVector> PathOffsetCurve;

	/** 曲线撑杆跳的终点相对高度，单位为厘米；零表示保持原等高端点。 */
	UPROPERTY(BlueprintReadOnly)
	float CurvedVaultEndHeight = 0.0f;

	/** 撑杆跳精确交接点的解析切线速度，单位为厘米每秒；局部X前、Y右、Z上，已除以本段时长。任务按路径水平朝向旋转到世界空间，不从CMC末帧残余速度回读。 */
	UPROPERTY(BlueprintReadOnly)
	FVector CurvedVaultHandoffVelocityLocal = FVector::ZeroVector;

	/** 进入本段时需要继承的世界空间速度，单位为厘米每秒。 */
	UPROPERTY(BlueprintReadOnly)
	FVector InheritedVelocity = FVector::ZeroVector;

	/** 进入本段时继承已有速度的比例。 */
	UPROPERTY(BlueprintReadOnly, meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float InheritedVelocityRatio = 0.f;

	/** 抛体参数采用的解释模式，决定按高度、时长或显式速度求解。 */
	UPROPERTY(BlueprintReadOnly)
	EBallisticParameterMode BallisticMode = EBallisticParameterMode::ApexHeightAndDuration;

	/** 抛体模式的目标最高点相对高度，单位为厘米。 */
	UPROPERTY(BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float ApexHeight = 0.f;

	/** 显式发射世界速度，单位为厘米每秒。 */
	UPROPERTY(BlueprintReadOnly)
	FVector LaunchVelocity = FVector::ZeroVector;

	/** 仅自定义空中物理模式读取的恒定水平加速度，单位为厘米每平方秒。 */
	UPROPERTY(BlueprintReadOnly)
	FVector PlanarAcceleration = FVector::ZeroVector;

	/** 已安装到CMC/宿主的重力正幅值，单位为厘米每平方秒；仅自定义空中物理模式读取。 */
	UPROPERTY(BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float GravityMagnitude = 0.0f;

	/** 该段空中运动允许的输入操控比例。 */
	UPROPERTY(BlueprintReadOnly, meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AirControlScale = 0.f;

	/** 本段运动的朝向控制策略。 */
	UPROPERTY(BlueprintReadOnly)
	EActionRotationPolicy RotationPolicy = EActionRotationPolicy::Locked;

	/** 允许的最大转向角速度，单位为度每秒。 */
	UPROPERTY(BlueprintReadOnly, meta = (ClampMin = "0.0", ClampMax = "360.0"))
	float MaxTurnRateDegrees = 0.f;

	/** 允许偏离锁定方向的转向锥半角，单位为度。 */
	UPROPERTY(BlueprintReadOnly, meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float SteeringConeHalfAngle = 0.f;

	/** 由 RuntimeHost 按 ActionToken 生成唯一名称；结束/Superseded/取消时必须移除 */
	UPROPERTY(BlueprintReadOnly)
	FName WarpTargetName;

	/** 运动被取消时如何处理剩余速度的策略。 */
	UPROPERTY(BlueprintReadOnly)
	EMovementCancelVelocityPolicy CancelVelocityPolicy = EMovementCancelVelocityPolicy::PreserveVelocity;

	/** BallisticVault 必须且只能启用所选参数组。 */
	bool HasValidBallisticParameters() const
	{
		if (Mode != EWeaponMovementMode::BallisticVault)
		{
			return true;
		}

		const bool bHasApexParameters = FMath::IsFinite(ApexHeight) && FMath::IsFinite(Duration)
			&& ApexHeight > 0.f && Duration > 0.f;
		const bool bHasExplicitVelocity = !LaunchVelocity.ContainsNaN() && !LaunchVelocity.IsNearlyZero();
		if (bHasApexParameters == bHasExplicitVelocity)
		{
			return false;
		}

		return BallisticMode == EBallisticParameterMode::ApexHeightAndDuration
			? bHasApexParameters
			: bHasExplicitVelocity;
	}

	bool HasValidCurvedVaultParameters() const
	{
		if (Mode != EWeaponMovementMode::CurvedVault)
		{
			return true;
		}

		// A missing analytic tangent is not a degraded configuration: it is
		// exactly the state that handed the capsule a collapsed velocity at the
		// free-fall boundary.  Refuse the move instead of playing it wrong.
		return PathOffsetCurve != nullptr
			&& FMath::IsFinite(Duration) && Duration > 0.f
			&& FMath::IsFinite(MaxDistance) && MaxDistance > 0.f
			&& FMath::IsFinite(CurvedVaultEndHeight)
			&& !CurvedVaultHandoffVelocityLocal.ContainsNaN()
			&& !CurvedVaultHandoffVelocityLocal.IsNearlyZero();
	}

	bool HasValidSegmentedDirectionalParameters() const
	{
		return Mode != EWeaponMovementMode::SegmentedDirectional
			|| (SegmentedProfile.IsValid() && !DirectionSnapshot.ContainsNaN()
				&& !DirectionSnapshot.IsNearlyZero());
	}

	bool HasValidRecordedTrajectoryParameters() const
	{
		if (Mode != EWeaponMovementMode::RecordedLocalTrajectory)
		{
			return true;
		}

		// 方向是局部基的一半，缺了它整条曲线就没有落点；尾段策略在 HoldLastKey 下
		// 可以没有速度模型（200/203 本就录到了全程），所以不在这里要求它非零。
		return RecordedTrajectory.IsValid()
			&& FMath::IsFinite(RecordedTrajectoryScale.X)
			&& FMath::IsFinite(RecordedTrajectoryScale.Y)
			&& FMath::IsFinite(RecordedTrajectoryScale.Z)
			&& RecordedTrajectoryScale.X >= 0.0
			&& RecordedTrajectoryScale.Y >= 0.0
			&& RecordedTrajectoryScale.Z >= 0.0
			&& !DirectionSnapshot.ContainsNaN()
			&& !DirectionSnapshot.IsNearlyZero();
	}

	FVector GetRecordedTrajectoryOffset(float Seconds) const
	{
		return RecordedTrajectory.GetOffset(Seconds, TrajectoryTailPolicy)
			* RecordedTrajectoryScale;
	}

	FVector GetRecordedTrajectoryVelocity(float Seconds) const
	{
		return RecordedTrajectory.GetVelocity(Seconds, TrajectoryTailPolicy)
			* RecordedTrajectoryScale;
	}

	bool HasValidAuthoredAerialPhysicsParameters() const
	{
		if (Mode != EWeaponMovementMode::AuthoredAerialPhysics)
		{
			return true;
		}

		// Authored aerial movement may start level or downward as well as upward;
		// CMC gravity determines the subsequent arc. Reject only a zero launch vector.
		return FMath::IsFinite(Duration) && Duration > 0.f
			&& FMath::IsFinite(LaunchVelocity.X) && FMath::IsFinite(LaunchVelocity.Y)
			&& FMath::IsFinite(LaunchVelocity.Z) && !LaunchVelocity.IsNearlyZero()
			&& FMath::IsFinite(GravityMagnitude) && GravityMagnitude > 0.f
			&& FMath::IsFinite(PlanarAcceleration.X)
			&& FMath::IsFinite(PlanarAcceleration.Y)
			&& FMath::IsFinite(PlanarAcceleration.Z)
			&& FMath::IsNearlyZero(PlanarAcceleration.Z);
	}
};

/** 位移任务的统一结果；FinalVelocity 明确交还 CMC */
USTRUCT(BlueprintType)
struct FWeaponMovementResult
{
	GENERATED_BODY()

	/** 位移任务结束的原因，区分完成、取消、阻挡及其他终止。 */
	UPROPERTY(BlueprintReadOnly)
	EWeaponMovementEndReason EndReason = EWeaponMovementEndReason::Completed;

	/** 该段已发生的水平移动距离，单位为厘米。 */
	UPROPERTY(BlueprintReadOnly)
	float TravelledDistance = 0.f;

	/** 该段完整三维移动距离，单位为厘米；与只统计水平距离的字段分开。 */
	UPROPERTY(BlueprintReadOnly)
	float TravelledDistance3D = 0.f;

	/** 位移任务结束时用于交接的世界速度，单位为厘米每秒。 */
	UPROPERTY(BlueprintReadOnly)
	FVector FinalVelocity = FVector::ZeroVector;

	/** 运动结束时关联的真实阻挡命中信息。 */
	UPROPERTY(BlueprintReadOnly)
	FHitResult BlockingHit;
};
