// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SphereComponent.h"
#include "GameplayTagContainer.h"
#include "MHGZMonsterHitzoneComponent.generated.h"

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

	/** 肉质——伤害倍率（典型值 0.2~1.5） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MHGZ|Hitzone")
	float DefenseMultiplier = 1.0f;

	/** 硬直肉质——硬直倍率（典型值 0.2~1.0） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MHGZ|Hitzone")
	float StaggerRate = 1.0f;

	/** 强制恢复所有通道到常态 */
	void ForceRestoreAllChannels();

protected:
	virtual void OnRegister() override;
};
