// Copyright MHGZ Project. All Rights Reserved.
//
// `MHGZVaultCurveTables.h` 的**手写**配对：把生成出来的 constexpr POD 曲线表
// 转成引擎能用的 USTRUCT 数组，并按 (方向, 灯态) 查表。
//
// **为什么不把这一段也生成出来**：转换要 `FVaultTrajectoryKey`，而它定义在
// `MHGZPoleVaultAbility.h` 里 —— 那个头会把反射、AbilitySystem 一整套牵进来。
// 生成头故意只依赖 `CoreMinimal.h`，保持「纯数据、零项目依赖」，谁 include 它
// 都不用付出编译代价。所以这条边界**必须**留在手写侧。

#include "Generated/MHGZVaultCurveTables.h"

#include "ActionSystem/MHGZPoleVaultAbility.h"

namespace MHGZ::VaultCurves
{
namespace
{
/** 一组变体在生成表里的全部句柄。 */
struct FVariantPOD
{
	EDirectionalInput Direction;
	bool bWhite;
	const FKeyPOD* Keys;
	int32 KeyCount;
	const FDriftPOD* Drift;
	int32 DriftCount;
	const FMeta* Meta;
};

/**
 * 顺序与生成器的 `CPP_NAMES` 一致。**新增变体要同时改生成器和这里。**
 *
 * 漏配不会变成一张空曲线：`FindVariant` 返回空，调用方一律**显式拒绝**这次移动，
 * 而不是拿零位移往下跑 —— 后者会表现成「跳了一下但没动」，没有任何报错。
 */
const FVariantPOD GVariants[] = {
	{ EDirectionalInput::Forward, false, Forward_NoWhite, Forward_NoWhite_Count,
		Forward_NoWhite_Drift, Forward_NoWhite_Drift_Count, &Forward_NoWhite_Meta },
	{ EDirectionalInput::Left, false, Left_NoWhite, Left_NoWhite_Count,
		Left_NoWhite_Drift, Left_NoWhite_Drift_Count, &Left_NoWhite_Meta },
	{ EDirectionalInput::Right, false, Right_NoWhite, Right_NoWhite_Count,
		Right_NoWhite_Drift, Right_NoWhite_Drift_Count, &Right_NoWhite_Meta },
	{ EDirectionalInput::Forward, true, Forward_White, Forward_White_Count,
		Forward_White_Drift, Forward_White_Drift_Count, &Forward_White_Meta },
	{ EDirectionalInput::Left, true, Left_White, Left_White_Count,
		Left_White_Drift, Left_White_Drift_Count, &Left_White_Meta },
	{ EDirectionalInput::Right, true, Right_White, Right_White_Count,
		Right_White_Drift, Right_White_Drift_Count, &Right_White_Meta },
};

const FVariantPOD* FindVariant(const EDirectionalInput Direction, const bool bWhite)
{
	for (const FVariantPOD& Variant : GVariants)
	{
		if (Variant.Direction == Direction && Variant.bWhite == bWhite)
		{
			return &Variant;
		}
	}
	return nullptr;
}
} // namespace

bool IsGenerated(const EDirectionalInput Direction, const bool bWhite)
{
	return FindVariant(Direction, bWhite) != nullptr;
}

bool BuildTrajectory(const EDirectionalInput Direction, const bool bWhite,
	TArray<FVaultTrajectoryKey>& Out)
{
	const FVariantPOD* Variant = FindVariant(Direction, bWhite);
	if (!Variant || !Variant->Keys || Variant->KeyCount < 2)
	{
		return false;
	}
	// 先一次分配到位，别让 25 次 Add 反复扩容。
	Out.SetNum(Variant->KeyCount);
	for (int32 Index = 0; Index < Variant->KeyCount; ++Index)
	{
		const FKeyPOD& Key = Variant->Keys[Index];
		Out[Index].Time = Key.Time;
		Out[Index].NormalizedPosition = FVector(Key.X, Key.Y, Key.Z);
	}
	return true;
}

bool BuildClipDrift(const EDirectionalInput Direction, const bool bWhite,
	TArray<FVaultClipDriftKey>& Out)
{
	const FVariantPOD* Variant = FindVariant(Direction, bWhite);
	if (!Variant || !Variant->Drift)
	{
		return false;
	}
	Out.SetNum(Variant->DriftCount);
	for (int32 Index = 0; Index < Variant->DriftCount; ++Index)
	{
		Out[Index].CurveTime = Variant->Drift[Index].CurveTime;
		Out[Index].ForwardFraction = Variant->Drift[Index].ForwardFraction;
	}
	return true;
}

bool FindMeta(const EDirectionalInput Direction, const bool bWhite, FVaultCurveMeta& Out)
{
	const FVariantPOD* Variant = FindVariant(Direction, bWhite);
	if (!Variant || !Variant->Meta)
	{
		return false;
	}
	Out.ArcDuration = Variant->Meta->ArcDuration;
	Out.VaultDuration = Variant->Meta->VaultDuration;
	Out.Distance = Variant->Meta->Distance;
	Out.Apex = Variant->Meta->Apex;
	Out.ChordYawDegrees = Variant->Meta->ChordYawDeg;
	Out.Trial = Variant->Meta->Trial;
	return true;
}
} // namespace MHGZ::VaultCurves
