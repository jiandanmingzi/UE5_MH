// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "MHGZCharacter.h"
#include "MHGZPlayerController.h"
#include "MHGZM3TestTypes.generated.h"

/** Router context-tag tests need a concrete spawnable MHGZ character. */
UCLASS()
class AMHGZM3TestCharacter : public AMHGZCharacter
{
	GENERATED_BODY()
};

UCLASS()
class AMHGZM3TestPlayerController : public AMHGZPlayerController
{
	GENERATED_BODY()
};

/**
 * M3 自动化测试专用原生萃取/三灯 GE。
 *
 * 生产萃取 GE 由 E5 蓝图资产落地（DA_WeaponRuntime_IG → UInsectGlaiveCombatConfig）；
 * M3 测试不依赖任何资产，由 transient CombatConfig 注入以下原生类。
 * 数值形态（持续时间/倍率）由 Resource 通过 Spec.SetDuration / SetByCaller 注入，
 * 与生产路径完全一致；每个 GE 以 DynamicGrantedTags 承载对应的
 * WeaponResource.IG.Extract.* / WeaponResource.IG.TripleUp 身份标签，
 * 让测试可以直接观察 ASC 上的"灯"。
 */
UCLASS()
class UMHGZM3WhiteExtractGE : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UMHGZM3WhiteExtractGE();
};

UCLASS()
class UMHGZM3OrangeExtractGE : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UMHGZM3OrangeExtractGE();
};

UCLASS()
class UMHGZM3RedExtractGE : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UMHGZM3RedExtractGE();
};

UCLASS()
class UMHGZM3TripleUpGE : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UMHGZM3TripleUpGE();
};

/** A duration GE whose impossible target-tag requirement makes Apply fail. */
UCLASS()
class UMHGZM3BlockedTripleUpGE : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UMHGZM3BlockedTripleUpGE();
};
