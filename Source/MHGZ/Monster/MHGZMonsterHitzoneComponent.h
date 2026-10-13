// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SphereComponent.h"
#include "Components/BoxComponent.h"
#include "GameplayTagContainer.h"
#include "ActionSystem/MHGZDamageTypes.h"
#include "MHGZMonsterHitzoneComponent.generated.h"

UENUM(BlueprintType)
enum class EMHGZHitzoneShape : uint8
{
	Sphere,
	Box
};

class UMHGZMonsterHitzoneComponent;

/** The box is only query geometry; part identity stays on its owning hitzone. */
UCLASS(Transient)
class UMHGZMonsterBoxHitzoneQueryComponent : public UBoxComponent
{
	GENERATED_BODY()
public:
	TWeakObjectPtr<UMHGZMonsterHitzoneComponent> HitzoneOwner;
};

/**
 * UMHGZMonsterHitzoneComponent — 怪物部位碰撞组件
 * 挂在怪物骨骼上，动画驱动跟随
 *
 * 通道配置（常态）：
 * - ObjectType: Hitzone；只承担部位查询，不承担实体阻挡
 * - Weapon / Visibility: Block
 * - MonsterAttack / Pawn / WorldStatic: Ignore
 */
UCLASS(ClassGroup = (MHGZ), BlueprintType, meta = (BlueprintSpawnableComponent))
class UMHGZMonsterHitzoneComponent : public USphereComponent
{
	GENERATED_BODY()

public:
	UMHGZMonsterHitzoneComponent();

	// ═══════════════════════════════════════════
	// 配置
	// ═══════════════════════════════════════════

	/** 骨骼名（挂载点） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MHGZ|Hitzone")
	FName BoneName;

	/** 部位 Tag（Hitzone.Head / Hitzone.Torso / Hitzone.Leg 等） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MHGZ|Hitzone")
	FGameplayTag HitzoneTag;

	/** Extract color provided by this part (leaf Red/White/Orange). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MHGZ|Hitzone")
	FGameplayTag ExtractColorTag;

	/** 肉质——伤害倍率（典型值 0.2~1.5） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MHGZ|Hitzone")
	float DefenseMultiplier = 1.0f;

	/** 按斩/打/弹区分的归一化肉质，0.45=原作肉质45；未配置的类型沿用 DefenseMultiplier。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MHGZ|Hitzone|Damage")
	TMap<EMHGZPhysicalDamageType, float> PhysicalHitzoneMultipliers;

	/** 五种属性的归一化肉质，0.20=肉质20；缺项为0，不从物理肉质推导属性弱点。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MHGZ|Hitzone|Damage")
	TMap<EMHGZElementType, float> ElementHitzoneMultipliers;

	/** 硬直肉质——硬直倍率（典型值 0.2~1.0） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MHGZ|Hitzone")
	float StaggerRate = 1.0f;

	/** Sphere radius in cm; runtime generation uses this, not the old HalfExtent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MHGZ|Hitzone", meta = (ClampMin = "0.0"))
	float Radius = 30.f;

	/** Only one query primitive is active: the sphere or its box child, never both. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MHGZ|Hitzone")
	EMHGZHitzoneShape QueryShape = EMHGZHitzoneShape::Sphere;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MHGZ|Hitzone", meta = (ClampMin = "1.0"))
	FVector BoxHalfExtent = FVector(30.0f);

	void ConfigureQueryShape(EMHGZHitzoneShape Shape, const FVector& HalfExtent);
	UPrimitiveComponent* GetQueryCollisionComponent() const;
	static UMHGZMonsterHitzoneComponent* Resolve(UPrimitiveComponent* HitComponent);
	float GetQueryBoundingRadius() const;
	float GetClosestPointOnQueryCollision(const FVector& Point, FVector& OutPoint) const;

	/** 强制恢复所有通道到常态 */
	void ForceRestoreAllChannels();

protected:
	virtual void OnRegister() override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UMHGZMonsterBoxHitzoneQueryComponent> BoxQuery;
};
