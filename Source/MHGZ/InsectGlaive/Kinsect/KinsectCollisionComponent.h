// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/CapsuleComponent.h"
#include "KinsectCollisionComponent.generated.h"

/**
 * UKinsectCollisionComponent — 猎虫专用碰撞胶囊（AKinsect 的 Root）。
 * 使用 "Kinsect" preset：QueryOnly / WorldStatic=Block / Weapon=Ignore / Hitzone=Ignore；
 * 不生成 Overlap 事件，命中判定由 AKinsect 的代码 Capsule Sweep 完成。
 */
UCLASS(ClassGroup = (MHGZ), BlueprintType, meta = (BlueprintSpawnableComponent))
class UKinsectCollisionComponent : public UCapsuleComponent
{
	GENERATED_BODY()

public:
	UKinsectCollisionComponent();

	/** 启用碰撞：仅 QueryOnly，重新应用 Kinsect preset。 */
	void EnableKinsectCollision();

	/** 关闭碰撞：NoCollision。 */
	void DisableKinsectCollision();

protected:
	virtual void OnRegister() override;
};
