// Copyright MHGZ Project. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "WeaponRuntime/MHGZWeaponRuntimeTypes.h"
#include "IGActionConfigTypes.generated.h"

/**
 * 一个撑杆跳变体的可调标量：**方向 × 灯态**。
 *
 * 「一个变体一条」而不是「两个灯态各一片平铺字段」：四个方向的字段名本来就该一样，
 * 平铺会得到八套同构字段，而它们之间的差异**只有收成一条一条才校验得了**
 * （`ArcDuration >= VaultDuration`、非白灯的残余自由落体必须落在实测 0.18~0.30 s 内）。
 *
 * 由撑杆跳GA的VaultTunings持有；曲线、弦向和clip路径仍在同一GA的FVaultProfile上。
 * 修改这些标量不会重建曲线；原Combat同名字段仅保留加载兼容。
 */
USTRUCT(BlueprintType)
struct MHGZ_API FVaultTuning
{
	GENERATED_BODY()

	/** 本条撑杆跳配置对应的输入方向；与白灯标记共同确定唯一变体。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "撑杆跳变体")
	EDirectionalInput Direction = EDirectionalInput::None;

	/** 本条撑杆跳是否为白灯强化版本；只参与变体选择。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "撑杆跳变体")
	bool bWhite = false;

	/**
	 * **移动窗口** = `Jump + JumpOver`，即 CurvedVault 源的 `Duration`，**不含下坠段**。
	 *
	 * 与 `ArcDuration` 是两个语义不同的量，混用会出事：拿弧时长当窗口等于把
	 * 本该不存在的飞行补出来，实录路径被拉伸（白灯那次实测 +4.7%）。交棒点
	 * 由 `(JumpDuration + JumpOverDuration) / ArcDuration` 算出，所以窗口一变，
	 * 交棒进度与残余自由落体一起变。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "撑杆跳变体", meta = (ClampMin = "0.01"))
	float VaultDuration = 0.0f;

	/**
	 * **整条离地前缀**的时长（含下坠段），**只用于把实录轨迹归一化**。
	 *
	 * 用错这个量会把 Jump 边界从 0.330 挪到 0.375，把本该属于 JumpOver 的约 30 cm
	 * 位移划给 Jump —— 这正是它当初被从 `VaultDuration` 里拆出来的原因。
	 *
	 * 白灯**没有独立的下坠段**（一直覆盖到落地），所以两值相等、`HandoffProgress`
	 * 恰为 1.0，那是合法的「无自由落体窗口」，不是配置错误。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "撑杆跳变体", meta = (ClampMin = "0.01"))
	float ArcDuration = 0.0f;

	/** 该撑杆跳变体曲线的水平总位移（cm），不含交给CMC后的额外自由落体。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "撑杆跳变体", meta = (ClampMin = "0.01"))
	float Distance = 0.0f;

	/** 该撑杆跳变体相对起点的顶点高度（cm）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "撑杆跳变体", meta = (ClampMin = "0.01"))
	float ApexHeight = 0.0f;
};


namespace MHGZ::IGActionConfig
{
/** 生成表提供撑杆跳默认值；GA拥有可编辑实例，旧Combat仅保留加载兼容。 */
MHGZ_API TArray<FVaultTuning> MakeDefaultVaultTunings();
/** 舞踏层数归资源所有，招式倍率归对应GA；缺失/非法层保持1倍。 */
MHGZ_API float DanceDamageMultiplier(const TArray<float>& Multipliers, int32 Stacks, int32 MaxStacks);
}
