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
 * - Weapon: Block（始终，供玩家攻击检测）
 * - MonsterAttack: Ignore（常态——攻击窗口内由 AnimNotifyState 切换为 Block）
 * - Pawn: Block（始终，物理阻挡）
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

	// ═══════════════════════════════════════════
	// 通道切换
	// ═══════════════════════════════════════════

	/** MonsterAttack 通道设为 Block（攻击窗口内调用） */
	void EnableMonsterAttackChannel();

	/** MonsterAttack 通道设为 Ignore（收招调用） */
	void DisableMonsterAttackChannel();

	/** 强制恢复所有通道到常态 */
	void ForceRestoreAllChannels();

protected:
	virtual void OnRegister() override;
};
