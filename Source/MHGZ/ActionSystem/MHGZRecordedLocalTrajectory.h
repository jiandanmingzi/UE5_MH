// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "MHGZRecordedLocalTrajectory.generated.h"

class UCurveVector;

/**
 * 曲线模式生效期间，胶囊怎么对待地面。三相位各用一种，互不混用。
 *
 * 这个策略描述的是**物理**，不是表现：它决定 CMC 的物理子步里要不要 FindFloor、
 * 要不要在有可行走地面时 `ProcessLanded`，以及相位的终点由谁宣布。
 */
UENUM(BlueprintType)
enum class ERecordedTrajectoryGroundPolicy : uint8
{
	/**
	 * 起手段：弧独占胶囊，没有任何接地判定。
	 *
	 * CMC 停在 MOVE_Flying，不跑重力、不 FindFloor、不 ProcessLanded，与舞踏 lead 段
	 * 同一条理由 —— 起手前的胶囊常常已经是 MOVE_Falling 且脚下就是地面，一次误判的触地
	 * 会在起手段就把动作收掉。相位终点**只**由曲线时长宣布。
	 */
	ProtectedFlight UMETA(DisplayName = "保护飞行（起手段，不接地）"),

	/**
	 * 下潜段：弧照旧驱动胶囊，但**允许**真正的 `ProcessLanded`。
	 *
	 * 与上一条唯一的差别就是这一句；本招的「真实触地」正是靠它发生的。
	 */
	LandingAirborne UMETA(DisplayName = "空中下潜（触地即接管）"),

	/**
	 * 落地段：胶囊已经站在地上，曲线驱动的是**水平**路径。
	 *
	 * 按 §5.3，203 的竖直记录是胶囊跟随落地地形的抖动，还原成升降没有意义，
	 * 所以这一段只施加水平位移，Z 完全交给地面。模式保持 MOVE_Walking。
	 */
	Grounded UMETA(DisplayName = "地面驱动（203 落地段）")
};

/**
 * 相位时间走出实录覆盖范围之后怎么办。
 *
 * 这不是调优旋钮，是两种**事实**：有的相位录到了全程（200/203，走完就结束），
 * 有的相位按定义会被真实触地截断，高度一高就必然走出覆盖（201）。
 */
UENUM(BlueprintType)
enum class ERecordedTrajectoryTailPolicy : uint8
{
	/** 覆盖之外不再移动：曲线跑完，相位就该结束。200/203 用。 */
	HoldLastKey UMETA(DisplayName = "覆盖外保持终值"),

	/**
	 * 覆盖之外按**同模型**的终末速度与终末加速度继续积分。
	 *
	 * 201 用。实录覆盖只有 0.3665 s（再长就触地了），飞得更高时这一段必须继续往下走，
	 * 否则胶囊会在半空冻住。速度项是可信的（M1 实测模型与逐次独立拟合差 <1%），
	 * 加速度项的确定度只有几十个百分点 —— 所以它必须配真实触地或独立看门狗作为终止条件，
	 * 不能单独当出口。
	 */
	IntegrateTerminalModel UMETA(DisplayName = "覆盖外续行（速度+加速度模型）")
};

/**
 * 一条从真实录制重建的**局部**路径：冻结水平基下的三分量 CurveVector，
 * 外加同一平滑模型在覆盖末端给出的速度与加速度。
 *
 * 局部基见 §5.2：`X` 是冻结水平前向、`Y` 是右向、`Z` 是**世界上向**。
 * 三份曲线横轴是相位内秒数，纵轴 cm，全部从 `(0,0,0)` 起，末位移不要求回零。
 */
USTRUCT(BlueprintType)
struct MHGZ_API FRecordedLocalTrajectoryProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	TObjectPtr<UCurveVector> Curve;

	/**
	 * 实录覆盖到哪一秒为止（相位内秒数）。超出这一秒就走尾段策略。
	 *
	 * 这是**样本域**的终点，不是最后一个键的时间。两者的差被限制在**一个 1/120 s 网格**
	 * 之内，两个方向都有实例：200/203 的末键落在域终点**之后**（生成器把键向上取整到网格），
	 * 201 的末键落在域终点**之前**一个网格（换段帧裁剪摘掉了末尾那个键）。
	 * 把 201 的规范域 `0.3665` 喂进来是合法配置，不是笔误。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "0.0"))
	float CoveredSeconds = 0.0f;

	/** 覆盖末端（`CoveredSeconds`）的局部终末速度，cm/s，与曲线同一模型。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	FVector TerminalVelocityLocal = FVector::ZeroVector;

	/** 覆盖末端的局部终末加速度，cm/s²，与曲线同一模型。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	FVector TerminalAccelerationLocal = FVector::ZeroVector;

	/** 有限、键序正确、起点为原点、覆盖时长为正。 */
	bool IsValid() const;

	/** 覆盖时长；无效时为 0。 */
	float GetDuration() const { return IsValid() ? CoveredSeconds : 0.0f; }

	/** 已按 `Seconds` 夹到 [0, 覆盖+尾段] 的局部位移，单位 cm。 */
	FVector GetOffset(float Seconds, ERecordedTrajectoryTailPolicy TailPolicy) const;

	/** 同一时刻的局部速度，cm/s。 */
	FVector GetVelocity(float Seconds, ERecordedTrajectoryTailPolicy TailPolicy) const;
};

namespace MHGZRecordedLocalTrajectory
{
	/**
	 * 审计一份曲线的纵轴：分量有限、时间严格递增、首键落在原点。
	 *
	 * 不要求单调、不要求末位移回零 —— 只要求「时间有序、数值有限」，
	 * 这正是 §5.2 给三份 CV 的唯一格式约束。`OutLastKeySeconds` 回末键时间。
	 */
	bool ValidateCurve(const UCurveVector* Curve, float& OutLastKeySeconds);

	/** 局部右向：`WorldUp × D`。D 必须是已归一化的水平方向。 */
	FVector ResolveRightVector(const FVector& LockedYawDirection);

	/** 局部前向/右向/上向 → 世界向量；局部坐标单位与曲线一致。 */
	FVector LocalToWorld(const FVector& LocalVector, const FVector& LockedYawDirection);
}
