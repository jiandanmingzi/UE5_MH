// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InsectGlaive/IGPowderTypes.h"
#include "InsectGlaive/Kinsect/Kinsect.h"
#include "WeaponRuntime/MHGZWeaponRuntimeTypes.h"
#include "IGResourceUITypes.generated.h"

/** 唯一允许建立舞踏层数的来源；不能用 CantAttack/CantDodge 推导。 */
UENUM(BlueprintType)
enum class EIGDanceSource : uint8
{
	None,
	KinsectSlash,
	AdvancingCounter,
	AwakenedKinsect
};

/** 只读显示快照；它不是战斗状态的第二份所有者。 */
USTRUCT(BlueprintType)
struct FIGResourceUISnapshot
{
	GENERATED_BODY()

	/** 当前武器运行期的身份令牌；代际变化后旧回调和旧请求失效。 */
	UPROPERTY(BlueprintReadOnly)
	FWeaponRuntimeToken RuntimeToken;

	/** 猎虫当前耐力快照。 */
	UPROPERTY(BlueprintReadOnly)
	float KinsectStamina = 0.0f;

	/** 猎虫耐力上限快照。 */
	UPROPERTY(BlueprintReadOnly)
	float MaxKinsectStamina = 0.0f;

	/** 猎虫当前处于附着、飞行、悬停或返回等状态的界面快照。 */
	UPROPERTY(BlueprintReadOnly)
	EKinsectState KinsectState = EKinsectState::Attached;

	/** 猎虫已采集但尚未交付给猎人的精华颜色。 */
	UPROPERTY(BlueprintReadOnly)
	FGameplayTag KinsectPendingExtractColor;

	/** 猎虫真实携带颜色：普通单色，强化觉虫击去重多色；非角色已有灯。 */
	UPROPERTY(BlueprintReadOnly)
	FGameplayTagContainer KinsectPendingExtractColors;

	/** 当前运行期是否存在可用猎虫实例。 */
	UPROPERTY(BlueprintReadOnly)
	bool bHasKinsect = false;

	/** 玩家当前是否拥有有效红灯。 */
	UPROPERTY(BlueprintReadOnly)
	bool bRed = false;

	/** 玩家当前是否拥有有效白灯。 */
	UPROPERTY(BlueprintReadOnly)
	bool bWhite = false;

	/** 玩家当前是否拥有有效橙灯。 */
	UPROPERTY(BlueprintReadOnly)
	bool bOrange = false;

	/** 玩家红灯剩余有效时长，单位为秒。 */
	UPROPERTY(BlueprintReadOnly)
	float RedRemainingSeconds = 0.0f;

	/** 玩家白灯剩余有效时长，单位为秒。 */
	UPROPERTY(BlueprintReadOnly)
	float WhiteRemainingSeconds = 0.0f;

	/** 玩家橙灯剩余有效时长，单位为秒。 */
	UPROPERTY(BlueprintReadOnly)
	float OrangeRemainingSeconds = 0.0f;

	/** 红灯剩余时长相对其有效总时长的归一化比例。 */
	UPROPERTY(BlueprintReadOnly)
	float RedRatio = 0.0f;

	/** 白灯剩余时长相对其有效总时长的归一化比例。 */
	UPROPERTY(BlueprintReadOnly)
	float WhiteRatio = 0.0f;

	/** 橙灯剩余时长相对其有效总时长的归一化比例。 */
	UPROPERTY(BlueprintReadOnly)
	float OrangeRatio = 0.0f;

	/** 玩家当前是否处于有效三灯强化状态。 */
	UPROPERTY(BlueprintReadOnly)
	bool bTripleUp = false;

	/** 三灯强化剩余有效时长，单位为秒。 */
	UPROPERTY(BlueprintReadOnly)
	float TripleRemainingSeconds = 0.0f;

	/** 三灯强化剩余时长相对总时长的归一化比例。 */
	UPROPERTY(BlueprintReadOnly)
	float TripleRatio = 0.0f;

	/** 当前空中舞踏层数，供界面显示。 */
	UPROPERTY(BlueprintReadOnly)
	int32 DanceStacks = 0;

	/** 最近一次有效舞踏层的来源种类。 */
	UPROPERTY(BlueprintReadOnly)
	EIGDanceSource DanceSource = EIGDanceSource::None;

	/** 当前未被预留、可参与聚爆的粉尘数量。 */
	UPROPERTY(BlueprintReadOnly)
	int32 AvailablePowderCount = 0;

	/** 当前已被聚爆事务预留的粉尘数量。 */
	UPROPERTY(BlueprintReadOnly)
	int32 ReservedPowderCount = 0;
};
