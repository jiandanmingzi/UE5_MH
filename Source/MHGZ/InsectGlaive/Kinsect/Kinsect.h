// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Kinsect.generated.h"

class USkeletalMeshComponent;
class UKinsectCollisionComponent;
class UProjectileMovementComponent;
class UInsectGlaiveKinsectData;
class UMHGZMonsterHitzoneComponent;
class URes_InsectGlaive;

/**
 * 猎虫状态
 */
UENUM(BlueprintType)
enum class EKinsectState : uint8
{
	Attached  UMETA(DisplayName = "吸附中"),
	Flying    UMETA(DisplayName = "飞行中"),
	Hovering  UMETA(DisplayName = "悬停中"),
	Returning UMETA(DisplayName = "返回中"),
	Recalled  UMETA(DisplayName = "已召回")
};

/**
 * 猎虫伤害模式
 */
UENUM(BlueprintType)
enum class EKinsectDamageMode : uint8
{
	SingleHit  UMETA(DisplayName = "普通单发——命中 1 次即停"),
	Piercing   UMETA(DisplayName = "贯穿——按间隔持续伤害，碰怪不停")
};

/**
 * 猎虫萃取行为
 */
UENUM(BlueprintType)
enum class EKinsectExtractMode : uint8
{
	NoExtract         UMETA(DisplayName = "不萃取"),
	FirstHitOnly      UMETA(DisplayName = "仅首次命中"),
	AlwaysOverwrite   UMETA(DisplayName = "高优先级覆盖")
};

/**
 * AKinsect — 猎虫独立 Actor
 * 骨骼模型 + 碰撞体 + 飞行移动组件
 * 由 URes_InsectGlaive 管理生命周期，不挂载 ASC
 */
UCLASS()
class AKinsect : public AActor
{
	GENERATED_BODY()

public:
	AKinsect();

	virtual void Tick(float DeltaTime) override;

	// ═══════════════════════════════════════════
	// 组件
	// ═══════════════════════════════════════════

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Kinsect|Components")
	TObjectPtr<USkeletalMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Kinsect|Components")
	TObjectPtr<UKinsectCollisionComponent> Collision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Kinsect|Components")
	TObjectPtr<UProjectileMovementComponent> Movement;

	// ═══════════════════════════════════════════
	// 状态
	// ═══════════════════════════════════════════

	UPROPERTY(BlueprintReadOnly, Category = "Kinsect|State")
	EKinsectState State = EKinsectState::Attached;

	UPROPERTY(BlueprintReadOnly, Category = "Kinsect|State")
	TObjectPtr<UInsectGlaiveKinsectData> KinsectData;

	// ═══════════════════════════════════════════
	// 伤害/萃取配置
	// ═══════════════════════════════════════════

	EKinsectDamageMode DamageMode = EKinsectDamageMode::SingleHit;
	EKinsectExtractMode ExtractMode = EKinsectExtractMode::FirstHitOnly;
	float CurrentMotionValue = 1.0f;
	float CurrentDamageInterval = 0.12f;

	// ═══════════════════════════════════════════
	// 飞行控制
	// ═══════════════════════════════════════════

	/** 沿射线方向飞行（臂上放虫） */
	void StartFlightAlongRay(FVector InRayDirection, float MaxDistance);

	/** 直线飞向目标坐标（悬停放虫） */
	void StartFlightToPoint(FVector Destination);

	/** 设置伤害和萃取参数 */
	void SetDamageParams(EKinsectDamageMode InDamageMode, float InMotionValue,
		float InDamageInterval, EKinsectExtractMode InExtractMode = EKinsectExtractMode::FirstHitOnly);

	/** 停止飞行并悬停 */
	void StopAndHover();

	/** 开始返回玩家 */
	void StartReturn();

	/** 强制召回（耐力归零）——不清除 PendingExtractColor */
	void ForceRecall();

	/** 中断当前飞行/悬停/返回 */
	void Interrupt();

	/** 吸附到玩家手臂 */
	void AttachToPlayer(USceneComponent* ArmSocket);

	// ═══════════════════════════════════════════
	// 查询
	// ═══════════════════════════════════════════

	EKinsectState GetState() const { return State; }
	bool HasPendingExtract() const { return PendingExtractColor.IsValid(); }
	FGameplayTag GetPendingExtractColor() const { return PendingExtractColor; }
	float GetFlightSpeed() const;
	float GetHoverDrainRate() const;
	float GetFlightDrainRate() const;
	bool IsDeployed() const { return State == EKinsectState::Flying || State == EKinsectState::Hovering || State == EKinsectState::Returning; }

	// ═══════════════════════════════════════════
	// 引用
	// ═══════════════════════════════════════════

	UPROPERTY()
	TWeakObjectPtr<AActor> OwnerActor;

	UPROPERTY()
	TWeakObjectPtr<URes_InsectGlaive> ResourceComponent;

protected:
	virtual void BeginPlay() override;

	/** Overlap 回调——命中怪物部位 */
	UFUNCTION()
	void OnHitMonsterHitzone(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/** Hit 回调——撞墙 */
	UFUNCTION()
	void OnWorldCollision(UPrimitiveComponent* HitComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	/** 尝试记录萃取颜色 */
	void TryRecordExtract(UMHGZMonsterHitzoneComponent* Hitzone);

	/** Apply 一次伤害 */
	void ApplyDamageOnce(UMHGZMonsterHitzoneComponent* Hitzone, float MotionValue);

	/** 贯穿伤害 Tick */
	void TryApplyKinsectDamage(float DeltaTime);

	/** 获取当前重叠的怪物部位 */
	UMHGZMonsterHitzoneComponent* GetOverlappingHitzone() const;

	/** 是否应该停止飞行 */
	bool ShouldStopFlying() const;

	/** 飞行结束处理 */
	void OnFlightEnded();

private:
	// 飞行参数
	bool bFollowRay = false;
	FVector RayDirection = FVector::ZeroVector;
	FVector FlyDestination = FVector::ZeroVector;
	float MaxFlightRange = 3000.f;

	// 伤害/萃取
	UPROPERTY()
	FGameplayTag PendingExtractColor;

	float TimeSinceLastDamage = 999.f;
	bool bHasDealtDamage = false;

	// 动画
	float FlyPlayRate = 1.5f;
};
