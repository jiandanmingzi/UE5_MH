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

	/** 物理肉质百分数；100为100%吸收，类型表缺项时沿用此值。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MHGZ|Hitzone", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float DefenseMultiplier = 100.0f;

	/** 按斩/打/弹区分的物理肉质百分数：45表示45%；显式0免疫。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MHGZ|Hitzone|Damage", meta = (ClampMin = "0.0", ClampMax = "100.0"))
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
	MHGZ_API float GetQueryBoundingRadius() const;
	float GetClosestPointOnQueryCollision(const FVector& Point, FVector& OutPoint) const;

	/** 强制恢复所有通道到常态 */
	void ForceRestoreAllChannels();

	/** 部位级虫印Debuff；按来源及实例隔离，不给整只怪物施加虫印。 */
	void ApplyKinsectMarkDebuff(UObject* Source, const FGuid& MarkID, double ExpireAt);
	void RemoveKinsectMarkDebuff(const UObject* Source, const FGuid& MarkID);
	bool HasKinsectMarkDebuffFrom(const UObject* Source, const FGuid& MarkID) const;

	/** 该部位是否仍带有任意有效来源的虫印Debuff。 */
	UFUNCTION(BlueprintPure, Category = "MHGZ|Hitzone|Debuff")
	bool HasKinsectMarkDebuff() const;

protected:
	virtual void OnRegister() override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

private:
	struct FKinsectMarkDebuff
	{
		FGuid MarkID;
		double ExpireAt = 0.0;
	};
	TMap<TWeakObjectPtr<const UObject>, FKinsectMarkDebuff> KinsectMarkDebuffs;

	UPROPERTY(Transient)
	TObjectPtr<UMHGZMonsterBoxHitzoneQueryComponent> BoxQuery;
};
