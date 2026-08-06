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

	// 碰撞初始化在 OnRegister 中完成——半径由蓝图或 DummyConfig 决定。
	SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SetCollisionObjectType(ECC_WorldDynamic);
	SetGenerateOverlapEvents(false);
	SetCollisionResponseToAllChannels(ECR_Ignore);
	SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block);  // Weapon → Block
	SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Ignore); // MonsterAttack → Ignore
	SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);               // Pawn → Block
	SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
}

void UMHGZMonsterHitzoneComponent::EnableMonsterAttackChannel()
{
	SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Block);
}

void UMHGZMonsterHitzoneComponent::DisableMonsterAttackChannel()
{
	SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Ignore);
}

void UMHGZMonsterHitzoneComponent::ForceRestoreAllChannels()
{
	SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block);
	SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Ignore);
	SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
}
