// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/HitResult.h"
#include "GameplayAbilitySpecHandle.h"
#include "GameplayTagContainer.h"
#include "ScalableFloat.h"
#include "MHGZWeaponRuntimeTypes.generated.h"

class ACharacter;
class APlayerController;
class UAbilitySystemComponent;
class UCurveFloat;
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
	AdditiveInertia
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

/** 位移碰撞处理策略 */
UENUM(BlueprintType)
enum class EMovementCollisionPolicy : uint8
{
	StopOnBlockingHit,
	SlideAlongBlockingHits,
	IgnoreBlockingHits
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

	UPROPERTY()
	TWeakObjectPtr<UMHGZWeaponRuntimeHostComponent> Host;

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

	UPROPERTY()
	FWeaponRuntimeToken RuntimeToken;

	UPROPERTY()
	TWeakObjectPtr<ACharacter> Character;

	UPROPERTY()
	TWeakObjectPtr<APlayerController> Controller;

	UPROPERTY()
	TWeakObjectPtr<UMHGZAbilitySystemComponent> ASC;

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

	UPROPERTY()
	FWeaponRuntimeToken RuntimeToken;

	UPROPERTY()
	FGameplayAbilitySpecHandle AbilityHandle;

	UPROPERTY()
	uint32 ActivationSequenceID = 0;

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

	UPROPERTY()
	FWeaponActionToken ActionToken;

	UPROPERTY()
	TWeakObjectPtr<USkeletalMeshComponent> Mesh;

	UPROPERTY()
	int32 MontageInstanceID = INDEX_NONE;
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

	UPROPERTY(BlueprintReadOnly)
	EMHGZMotionMatchingHandoffType Type = EMHGZMotionMatchingHandoffType::None;

	UPROPERTY(BlueprintReadOnly)
	int64 Serial = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 ActivationSequenceID = 0;

	UPROPERTY(BlueprintReadOnly)
	FVector2D RawMoveInput = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	bool bHasRawMoveInputAtHandoff = false;

	UPROPERTY(BlueprintReadOnly)
	bool bHadRawMoveInputInMobilePhase = false;

	UPROPERTY(BlueprintReadOnly)
	bool bReleasedRawMoveInputInMobilePhase = false;

	/** True only for a mobile Exit that observed input and its later release. */
	UPROPERTY(BlueprintReadOnly)
	bool bPendingStopAtHandoff = false;

	/** The configured locomotion lane of the last non-neutral physical input sampled in the mobile phase. */
	UPROPERTY(BlueprintReadOnly)
	float LastActiveRawMoveCruiseSpeedInMobilePhase = 0.0f;

	/** Internal identity used only by RuntimeHost to invalidate stale payloads. */
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

	UPROPERTY()
	FWeaponRuntimeToken RuntimeToken;

	UPROPERTY()
	EWeaponTagOwnerKind Kind = EWeaponTagOwnerKind::Input;

	UPROPERTY()
	FGameplayAbilitySpecHandle AbilityHandle;

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

	UPROPERTY()
	FWeaponRuntimeToken RuntimeToken;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag CostType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FScalableFloat Amount;
};

/** TryReserveCosts 返回的资源预留；在 Commit 失败时 Release，成功时 Consume */
USTRUCT(BlueprintType)
struct FWeaponResourceCostReservation
{
	GENERATED_BODY()

	UPROPERTY()
	FWeaponRuntimeToken RuntimeToken;

	UPROPERTY()
	uint32 ActivationSequenceID = 0;

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

	UPROPERTY(BlueprintReadOnly)
	EWeaponAimSnapshotContext Context = EWeaponAimSnapshotContext::None;

	UPROPERTY(BlueprintReadOnly)
	FVector Origin = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	FVector Direction = FVector::ForwardVector;

	UPROPERTY(BlueprintReadOnly)
	FVector TargetPoint = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	bool bHasHitResult = false;

	UPROPERTY(BlueprintReadOnly)
	FHitResult HitResult;
};

/** 路由器输出的不可变输入快照；GA 激活后不得重新读取摇杆/相机 */
USTRUCT(BlueprintType)
struct FWeaponInputSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FGameplayTag ResolvedInputTag;

	UPROPERTY(BlueprintReadOnly)
	FGameplayTag SourceControlTag;

	UPROPERTY(BlueprintReadOnly)
	FGameplayTagContainer HeldModifierTags;

	/** Grounded/Aerial、Sheathed/Unsheathed、Aim 子集 */
	UPROPERTY(BlueprintReadOnly)
	FGameplayTagContainer ContextTags;

	UPROPERTY(BlueprintReadOnly)
	FVector2D RawMoveInput = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	FVector WorldDirection = FVector::ForwardVector;

	/** 输入解析瞬间的角色 Forward；收刀拔刀等动作不得在 GA 激活后重新采样。 */
	UPROPERTY(BlueprintReadOnly)
	FVector ActorForward = FVector::ForwardVector;

	UPROPERTY(BlueprintReadOnly)
	EDirectionalInput Direction = EDirectionalInput::None;

	UPROPERTY(BlueprintReadOnly)
	FWeaponAimSnapshot Aim;

	UPROPERTY(BlueprintReadOnly)
	EWeaponInputPhase Phase = EWeaponInputPhase::Started;

	UPROPERTY(BlueprintReadOnly)
	double Timestamp = 0.0;

	UPROPERTY()
	uint32 SequenceID = 0;
};

/** Coordinator 冻结并随本次激活传递的不可变上下文。 */
USTRUCT(BlueprintType)
struct FWeaponAbilityActivationContext
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FWeaponRuntimeToken RuntimeToken;

	UPROPERTY()
	uint32 ActivationSequenceID = 0;

	UPROPERTY(BlueprintReadOnly)
	FName TransitionID;

	UPROPERTY(BlueprintReadOnly)
	FName SourceState;

	UPROPERTY(BlueprintReadOnly)
	FName TargetState;

	UPROPERTY(BlueprintReadOnly)
	FWeaponInputSnapshot Input;
};

USTRUCT(BlueprintType)
struct FActiveComboTransition
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FName TransitionID;

	UPROPERTY(BlueprintReadOnly)
	FWeaponActionToken ActionToken;

	UPROPERTY(BlueprintReadOnly)
	FName SourceState;

	UPROPERTY(BlueprintReadOnly)
	FName TargetState;

	UPROPERTY(BlueprintReadOnly)
	FGameplayTagContainer OwnedTags;

	UPROPERTY(BlueprintReadOnly)
	bool bFirstHitReceived = false;
};

/** TryActivate 与 GA Commit/Confirm 之间的短生命周期事务。 */
USTRUCT()
struct FPendingComboTransition
{
	GENERATED_BODY()

	UPROPERTY()
	FName TransitionID;

	UPROPERTY()
	FGameplayAbilitySpecHandle AbilityHandle;

	UPROPERTY()
	uint32 ActivationSequenceID = 0;

	UPROPERTY()
	FWeaponAbilityActivationContext ActivationContext;

	UPROPERTY()
	FWeaponActionToken PreviousActionToken;
};

/** 通用武器位移请求（运行时结构，不是 DataAsset；标量调优在 CombatConfig 验证） */
USTRUCT(BlueprintType)
struct FWeaponMovementRequest
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FWeaponActionToken OwnerAction;

	UPROPERTY(BlueprintReadOnly)
	EWeaponMovementMode Mode = EWeaponMovementMode::BoundedDirectional;

	UPROPERTY(BlueprintReadOnly)
	FVector DirectionSnapshot = FVector::ForwardVector;

	UPROPERTY(BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float MaxDistance = 0.f;

	UPROPERTY(BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float Duration = 0.f;

	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<UCurveFloat> DistanceCurve;

	UPROPERTY(BlueprintReadOnly)
	FVector InheritedVelocity = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float InheritedVelocityRatio = 0.f;

	UPROPERTY(BlueprintReadOnly)
	EBallisticParameterMode BallisticMode = EBallisticParameterMode::ApexHeightAndDuration;

	UPROPERTY(BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float ApexHeight = 0.f;

	UPROPERTY(BlueprintReadOnly)
	FVector LaunchVelocity = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AirControlScale = 0.f;

	UPROPERTY(BlueprintReadOnly)
	EActionRotationPolicy RotationPolicy = EActionRotationPolicy::Locked;

	UPROPERTY(BlueprintReadOnly, meta = (ClampMin = "0.0", ClampMax = "360.0"))
	float MaxTurnRateDegrees = 0.f;

	UPROPERTY(BlueprintReadOnly, meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float SteeringConeHalfAngle = 0.f;

	/** 由 RuntimeHost 按 ActionToken 生成唯一名称；结束/Superseded/取消时必须移除 */
	UPROPERTY(BlueprintReadOnly)
	FName WarpTargetName;

	UPROPERTY(BlueprintReadOnly)
	EMovementCollisionPolicy CollisionPolicy = EMovementCollisionPolicy::StopOnBlockingHit;

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
};

/** 位移任务的统一结果；FinalVelocity 明确交还 CMC */
USTRUCT(BlueprintType)
struct FWeaponMovementResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	EWeaponMovementEndReason EndReason = EWeaponMovementEndReason::Completed;

	UPROPERTY(BlueprintReadOnly)
	float TravelledDistance = 0.f;

	UPROPERTY(BlueprintReadOnly)
	FVector FinalVelocity = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	FHitResult BlockingHit;
};
