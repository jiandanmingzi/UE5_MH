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

	UPROPERTY(BlueprintReadOnly)
	FWeaponRuntimeToken RuntimeToken;

	UPROPERTY(BlueprintReadOnly)
	float KinsectStamina = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float MaxKinsectStamina = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	EKinsectState KinsectState = EKinsectState::Attached;

	/** Extract color currently carried by the deployed kinsect, before delivery to the hunter. */
	UPROPERTY(BlueprintReadOnly)
	FGameplayTag KinsectPendingExtractColor;

	/** 猎虫真实携带颜色：普通单色，强化觉虫击去重多色；非角色已有灯。 */
	UPROPERTY(BlueprintReadOnly)
	FGameplayTagContainer KinsectPendingExtractColors;

	UPROPERTY(BlueprintReadOnly)
	bool bHasKinsect = false;

	UPROPERTY(BlueprintReadOnly)
	bool bRed = false;

	UPROPERTY(BlueprintReadOnly)
	bool bWhite = false;

	UPROPERTY(BlueprintReadOnly)
	bool bOrange = false;

	UPROPERTY(BlueprintReadOnly)
	float RedRemainingSeconds = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float WhiteRemainingSeconds = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float OrangeRemainingSeconds = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float RedRatio = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float WhiteRatio = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float OrangeRatio = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	bool bTripleUp = false;

	UPROPERTY(BlueprintReadOnly)
	float TripleRemainingSeconds = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float TripleRatio = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	int32 DanceStacks = 0;

	UPROPERTY(BlueprintReadOnly)
	EIGDanceSource DanceSource = EIGDanceSource::None;

	UPROPERTY(BlueprintReadOnly)
	int32 AvailablePowderCount = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 ReservedPowderCount = 0;
};
