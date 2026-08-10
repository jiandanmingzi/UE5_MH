// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZMonsterHitzoneComponent.h"

UMHGZMonsterHitzoneComponent::UMHGZMonsterHitzoneComponent()
{
	// 构造器中不做碰撞设置——BlueprintSpawnableComponent 的 CDO 构造期间
	// SetCollisionEnabled 等 API 会触发 NewObject 空名崩溃
}

void UMHGZMonsterHitzoneComponent::OnRegister()
{
	Super::OnRegister();

	// MonsterHitzone 预设使用独立 Hitzone Object Channel；实体阻挡由 Body 承担。
	SetCollisionProfileName(TEXT("MonsterHitzone"));
	SetGenerateOverlapEvents(false);
}

void UMHGZMonsterHitzoneComponent::ForceRestoreAllChannels()
{
	SetCollisionProfileName(TEXT("MonsterHitzone"));
	SetGenerateOverlapEvents(false);
}
