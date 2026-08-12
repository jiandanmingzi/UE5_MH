// Copyright MHGZ Project. All Rights Reserved.

#include "KinsectCollisionComponent.h"

UKinsectCollisionComponent::UKinsectCollisionComponent()
{
	// 小型胶囊体——猎虫尺寸；碰撞配置在 OnRegister 中完成（避免 CDO 构造期配置碰撞 API）
	InitCapsuleSize(15.f, 30.f);
}

void UKinsectCollisionComponent::OnRegister()
{
	Super::OnRegister();

	// Kinsect preset：QueryOnly / WorldStatic=Block / Weapon=Ignore / Hitzone=Ignore
	SetCollisionProfileName(TEXT("Kinsect"));
	SetGenerateOverlapEvents(false);
	// 默认不参与任何碰撞，仅在飞行中由 EnableKinsectCollision 打开
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void UKinsectCollisionComponent::EnableKinsectCollision()
{
	// 只 QueryOnly + 配置 preset；绝不使用 Weapon Overlap
	SetCollisionProfileName(TEXT("Kinsect"));
	SetGenerateOverlapEvents(false);
	SetCollisionEnabled(ECollisionEnabled::QueryOnly);
}

void UKinsectCollisionComponent::DisableKinsectCollision()
{
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
}
