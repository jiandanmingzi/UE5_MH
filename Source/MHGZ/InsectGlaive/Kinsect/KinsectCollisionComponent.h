// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/CapsuleComponent.h"
#include "KinsectCollisionComponent.generated.h"

/**
 * UKinsectCollisionComponent — 猎虫专用碰撞组件
 * 胶囊体：飞行时 Weapon 通道 = Overlap（萃取判定），WordStatic = Block（撞墙停止）
 */
UCLASS(ClassGroup = (MHGZ), BlueprintType, meta = (BlueprintSpawnableComponent))
class UKinsectCollisionComponent : public UCapsuleComponent
{
	GENERATED_BODY()

public:
	UKinsectCollisionComponent();

	/** 启用碰撞（飞行/悬停中） */
	void EnableKinsectCollision();

	/** 关闭碰撞（停手臂时） */
	void DisableKinsectCollision();
};
