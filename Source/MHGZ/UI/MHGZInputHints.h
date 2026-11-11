// Copyright MHGZ Project. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"

class UMHGZAbilitySystemComponent;
class UWeaponInputProfile;

/** 操作提示的一行，只保存显示内容，不派发输入或修改战斗状态。 */
struct FMHGZInputHint
{
	FString Keys;
	FString Action;
	int32 SortOrder = 0;
	bool operator==(const FMHGZInputHint& Other) const
	{ return Keys == Other.Keys && Action == Other.Action && SortOrder == Other.SortOrder; }
};

namespace MHGZInputHints
{
/** 只读查询实际Combo/姿态/窗口及已授予直接能力，按A、Y、B、RT、LT、RB排序。 */
MHGZ_API TArray<FMHGZInputHint> Build(UMHGZAbilitySystemComponent& ASC,
	const UWeaponInputProfile& Profile, bool bGamepad);
/** 同一逻辑按钮的手柄/键鼠名称，供提示与定向检查共用。 */
MHGZ_API FString KeyName(const FString& Control, bool bGamepad);
}
