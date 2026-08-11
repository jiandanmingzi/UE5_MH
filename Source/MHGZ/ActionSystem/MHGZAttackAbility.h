// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MHGZGameplayAbility.h"
#include "GameplayTagContainer.h"
#include "Misc/DataValidation.h"
#include "MHGZAttackAbility.generated.h"

class USoundBase;
class UCameraShakeBase;
class UAnimMontage;
class UMotionWarpingComponent;
class UAbilityTask_PlayMontageAndWait;
class UMHGZMonsterHitzoneComponent;
class USkeletalMeshComponent;

/**
 * 碰撞形状枚举
 */
UENUM(BlueprintType)
enum class EAttackCollisionShape : uint8
{
	Sphere  UMETA(DisplayName = "球体"),
	Capsule UMETA(DisplayName = "胶囊体"),
	Box     UMETA(DisplayName = "盒子")
};

/** 多段伤害默认只能来自真实接触；离散复击必须显式选择并逐跳重验。 */
UENUM(BlueprintType)
enum class EAttackMultiHitPolicy : uint8
{
	ContactOnly,
	LockedTargetTicks
};

/**
 * 一段武器轨迹区域。多个区域可以在同一碰撞窗口内同时生效，
 * 例如虫棍前端和后端同时横扫。
 */
USTRUCT(BlueprintType)
struct FWeaponTraceRegion
{
	GENERATED_BODY()

	/** 有效攻击区域起点 Socket 或骨骼；留空时退化为 EndSocketName 单点 Sweep。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trace")
	FName StartSocketName;

	/** 有效攻击区域终点 Socket 或骨骼，必填。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trace")
	FName EndSocketName;

	/** 球形 Sweep 半径（厘米）。球体不受武器旋转朝向影响，适合高速长武器。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trace", meta = (ClampMin = "1.0"))
	float Radius = 14.0f;

	/** 棍身相邻采样点的最大间距；运行时还会限制为不超过 2 × Radius。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trace", meta = (ClampMin = "1.0"))
	float MaxSampleSpacing = 20.0f;

	/** 单个区域最多使用的棍身采样点数。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trace", meta = (ClampMin = "1", ClampMax = "32"))
	int32 MaxSampleCount = 16;

	/** 每帧旋转超过该角度时增加时间子步，降低高速旋转沿弧线漏判。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trace", meta = (ClampMin = "1.0", ClampMax = "90.0"))
	float MaxAngularStepDegrees = 15.0f;
};

/**
 * FAttackCollisionConfig — 单段碰撞配置
 */
USTRUCT(BlueprintType)
struct FAttackCollisionConfig
{
	GENERATED_BODY()

	/**
	 * 用于定位武器 SkeletalMeshComponent 的组件 Tag。
	 * 默认 WeaponTrace；找不到时回退到角色主 Mesh，再回退到任意含目标 Socket 的骨骼网格。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName TraceMeshComponentTag = FName(TEXT("WeaponTrace"));

	/**
	 * 本碰撞窗口内同时生效的武器轨迹区域。非空时覆盖下面的旧版单区域字段。
	 * 虫棍通常配置 Front、Rear 或两者同时启用。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trace")
	TArray<FWeaponTraceRegion> TraceRegions;

	/** 新版单区域终点；TraceRegions 为空时使用。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trace")
	FName TraceEndSocketName;

	/** 旧版字段：实际含义是轨迹终点。保留用于读取现有蓝图。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DeprecatedProperty, DeprecationMessage = "Use TraceEndSocketName or TraceRegions"))
	FName AttachSocketName;

	/**
	 * 旧版单区域起点。TraceRegions 为空时仍与 TraceEndSocketName/AttachSocketName 配合使用。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName TraceStartSocketName;

	/** 碰撞形状 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EAttackCollisionShape Shape = EAttackCollisionShape::Sphere;

	/** 形状参数：Sphere→X=Radius；Capsule→X=Radius+Z=HalfHeight；Box→HalfExtent */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector ShapeExtent = FVector(20, 20, 20);

	/** 碰撞通道 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TEnumAsByte<ECollisionChannel> CollisionChannel = ECC_GameTraceChannel1;

	/** 限定碰撞仅检测带此 Tag 的组件 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag HitzoneQueryTag;

	/** 旧版单区域固定采样数。TraceRegions 非空时改为按长度自动计算。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1", ClampMax = "8"))
	int32 TraceSampleCount = 3;

	/** PIE 中绘制本段 Sweep，便于校准 Socket 和半径 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bDrawDebug = false;
};

/**
 * FAttackDamageConfig — 单段伤害配置
 */
USTRUCT(BlueprintType)
struct FAttackDamageConfig
{
	GENERATED_BODY()

	/** 伤害 GE 蓝图 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSubclassOf<class UGameplayEffect> DamageEffectClass;

	/** ★ 动作值（倍率） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FScalableFloat MotionValue = -1.0f;

	/** ★ 基础破坏值 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FScalableFloat BaseStaggerValue = 0.f;

	/** 击退方向（相对攻击者朝向） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float KnockbackAngle = 0.f;

	/** 击退力度 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FScalableFloat KnockbackForce = 0.f;

	/** 硬直等级 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag HitStaggerTag;

	/** SetByCaller 伤害值 Tag */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag DamageSetByCallerTag;

	/** 是否按命中部位防御力修正伤害 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bUseHitzoneDefense = true;

	/** 招式内空挥截断 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bRequiresHitToContinue = false;

	/** 命中时对自身施加的 GE */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSubclassOf<UGameplayEffect> OnHitSelfEffect;

	/** 物理命中 GameplayCue Tag */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag HitCueTag;

	/** 元素附魔命中 GC 标签 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag ElementalCueTag;

	/** 震屏类 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSubclassOf<UCameraShakeBase> CameraShakeClass;

	/** 震屏强度倍率 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float CameraShakeScale = 0.f;

	/** 卡肉基础时长（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FScalableFloat HitStopBase = 0.f;

	/** 招式挥刀风声 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<USoundBase> SwingSound;
};

/**
 * FAttackSegmentConfig — 单段攻击配置（碰撞 + 伤害 + 多跳）
 */
USTRUCT(BlueprintType)
struct FAttackSegmentConfig
{
	GENERATED_BODY()

	/** 碰撞参数 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FAttackCollisionConfig Collision;

	/** 伤害参数 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FAttackDamageConfig Damage;

	/** ★ 单次碰撞产生的伤害跳数 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 MultiHitCount = 1;

	/** 默认只结算本次真实 Sweep 接触；LockedTargetTicks 必须逐跳重验距离。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EAttackMultiHitPolicy MultiHitPolicy = EAttackMultiHitPolicy::ContactOnly;

	/** 多次伤害之间的间隔（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float MultiHitInterval = 0.1f;

	/** LockedTargetTicks 每跳允许的最大目标距离。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1.0", EditCondition = "MultiHitPolicy == EAttackMultiHitPolicy::LockedTargetTicks"))
	float LockedTargetMaxDistance = 300.0f;

	/** ★ 本段 MotionWarping 允许的最大旋转修正角度 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float MaxWarpAngle = 30.0f;
};

/**
 * UMHGZAttackAbility — 攻击 Ability 中间层
 * 统一封装所有攻击类 Ability 的碰撞检测、命中过滤、伤害 GE 构造与 Apply
 */
UCLASS(BlueprintType, Blueprintable, Abstract)
class UMHGZAttackAbility : public UMHGZGameplayAbility
{
	GENERATED_BODY()

public:
	UMHGZAttackAbility();

	// ═══════════════════════════════════════════
	// 配置
	// ═══════════════════════════════════════════

	/** ★ 多段攻击配置 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack")
	TArray<FAttackSegmentConfig> AttackSegments;

	/** 攻击 Montage */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack")
	TObjectPtr<UAnimMontage> AttackMontage;

	// ═══════════════════════════════════════════
	// 覆写
	// ═══════════════════════════════════════════

	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

	// ═══════════════════════════════════════════
	// 碰撞窗口控制（由 AnimNotifyState 调用）
	// ═══════════════════════════════════════════

	/** 开启碰撞检测 */
	UFUNCTION(BlueprintCallable, Category = "MHGZ|Attack")
	void EnableCollision(int32 SegmentIndex = 0);

	/** 关闭指定段；INDEX_NONE 表示关闭本 Ability 的全部碰撞窗口。 */
	UFUNCTION(BlueprintCallable, Category = "MHGZ|Attack")
	void DisableCollision(int32 SegmentIndex = -1);

	/** 碰撞窗口内由 AnimNotifyState::NotifyTick 每帧调用 */
	void TickCollision(int32 SegmentIndex, float DeltaSeconds);

	// ═══════════════════════════════════════════
	// 伤害构造与 Apply
	// ═══════════════════════════════════════════

	/**
	 * 构造伤害 GE Spec
	 * 子类（如 UMHGZInsectGlaiveAbility）可覆写以注入额外 GC Tag
	 */
	virtual FGameplayEffectSpecHandle MakeDamageSpec(
		AActor* Target,
		FName HitzoneBoneName,
		int32 SegmentIndex);

	/** Apply 伤害到目标 */
	void ApplyDamage(AActor* Target, FName HitzoneBoneName, int32 SegmentIndex);

	// ═══════════════════════════════════════════
	// 命中/派生判断
	// ═══════════════════════════════════════════

	/** 当前碰撞窗口命中后，是否继续下一段 */
	UFUNCTION(BlueprintNativeEvent, Category = "MHGZ|Attack")
	bool ShouldContinueAfterHit() const;
	virtual bool ShouldContinueAfterHit_Implementation() const;

protected:
	virtual bool ValidateActionDependencies() const override;

	/** 执行方向修正（MotionWarping） */
	void ApplyDirectionCorrection();

	/** 获取 MotionWarpingComponent */
	UMotionWarpingComponent* GetMotionWarpingComponent() const;

	/** 通知协调器首次命中 */
	void NotifyCoordinatorFirstHit();

	// ═══════════════════════════════════════════
	// 运行时状态
	// ═══════════════════════════════════════════

	/** 最近开始/结束判定的段索引，仅供兼容 ShouldContinueAfterHit。 */
	int32 CurrentSegmentIndex = 0;

	/** 本次 GA 激活后是否已有命中 */
	bool bHasHitThisActivation = false;

	/** 是否有活跃的 RootMotion Task */
	bool bHasActiveRootMotionTask = false;

	/** 当前播放的攻击 Montage */
	UPROPERTY()
	TObjectPtr<UAnimMontage> ActiveAttackMontage;

private:
	struct FTraceRegionRuntimeState
	{
		FName StartSocketName;
		FName EndSocketName;
		float Radius = 14.0f;
		float MaxSampleSpacing = 20.0f;
		float MaxAngularStepDegrees = 15.0f;
		int32 MaxSampleCount = 16;
		int32 FixedSampleCount = 0;
		EAttackCollisionShape LegacyShape = EAttackCollisionShape::Sphere;
		FVector LegacyShapeExtent = FVector(14.0f);
		bool bUseLegacyShape = false;
		FVector PreviousStart = FVector::ZeroVector;
		FVector PreviousEnd = FVector::ZeroVector;
	};

	struct FCollisionWindowRuntimeState
	{
		TArray<FTraceRegionRuntimeState> Regions;
		TMap<TWeakObjectPtr<AActor>, FName> HitTargets;
		FTimerHandle MultiHitTimer;
		int32 MultiHitCurrentCount = 0;
	};

	/** GAS Montage 任务——统一处理完成、取消和被其他 Montage 打断 */
	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> MontageTask;

	/** 每个 ConfigIndex 独立保存轨迹、命中和多跳状态，允许窗口重叠。 */
	TMap<int32, FCollisionWindowRuntimeState> ActiveCollisionWindows;

	/** 执行指定段的一次 Socket Sweep 检测。 */
	void PerformSweepCheck(int32 SegmentIndex);

	/** 处理本帧为某个怪物选出的最早 Sweep 命中。 */
	void ProcessSweepHit(const FHitResult& Hit, int32 SegmentIndex);

	/** 首次命中后启动本段独立的多跳 Timer。 */
	void StartMultiHitTimerIfNeeded(int32 SegmentIndex);

	/** 指定段的一次多跳伤害。 */
	void OnMultiHitTick(int32 SegmentIndex);

	/** 查找怪物 HitzoneComponent */
	UMHGZMonsterHitzoneComponent* FindHitzoneComponent(AActor* Target, FName BoneName) const;

	/** 根据组件 Tag 和 Socket 配置找到实际参与武器轨迹检测的骨骼网格。 */
	USkeletalMeshComponent* FindTraceMeshComponent(const FAttackCollisionConfig& Collision) const;

	/** 检查 Mesh 是否包含当前配置要求的所有轨迹 Socket/骨骼。 */
	bool HasRequiredTraceSockets(const USkeletalMeshComponent* Mesh,
		const FAttackCollisionConfig& Collision) const;

	UFUNCTION()
	void OnMontageCompleted();

	UFUNCTION()
	void OnMontageInterrupted();

	bool bIsEndingAbility = false;
};
