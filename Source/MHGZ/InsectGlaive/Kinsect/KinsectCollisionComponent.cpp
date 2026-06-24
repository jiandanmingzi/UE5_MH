// Copyright MHGZ Project. All Rights Reserved.

#include "KinsectCollisionComponent.h"

UKinsectCollisionComponent::UKinsectCollisionComponent()
{
	// 小型胶囊体——猎虫尺寸
	InitCapsuleSize(15.f, 30.f);
	SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// 默认全部 Ignore
	SetCollisionResponseToAllChannels(ECR_Ignore);
}

void UKinsectCollisionComponent::EnableKinsectCollision()
{
	SetCollisionEnabled(ECollisionEnabled::QueryOnly);

	// Weapon 通道 → Overlap（对怪物部位产生 Overlap 事件）
	SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Overlap);

	// WorldStatic → Block（撞墙停止）
	SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
}

void UKinsectCollisionComponent::DisableKinsectCollision()
{
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
}
