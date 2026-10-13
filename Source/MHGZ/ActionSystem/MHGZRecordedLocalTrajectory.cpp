// Copyright MHGZ Project. All Rights Reserved.

#include "ActionSystem/MHGZRecordedLocalTrajectory.h"

#include "Curves/CurveVector.h"
#include "Curves/RichCurve.h"

namespace
{
constexpr float KeyValueTolerance = 0.001f;

/**
 * M1 生成三份规范曲线用的真实时间网格。
 *
 * 覆盖终点**可以**落在末键之后最多一个网格，理由来自生成器自己的两条规则：
 * 键向上取整到 1/120 s 网格，且 201 的换段帧裁剪会再摘掉末尾的键 —— 于是 201 的
 * 规范域 `0.366547` 恰好落在末键 `0.358333` 之后一个网格。这不是自由量：
 * 更宽的差距意味着「实测域」那句话在骗人，而曲线在末键之后本来就会被夹住不再变化。
 */
constexpr float AuthoredKeyGridSeconds = 1.0f / 120.0f;

bool IsFiniteVector(const FVector& Value)
{
	return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y)
		&& FMath::IsFinite(Value.Z);
}

/** 覆盖内的位移直接问曲线；RichCurve 在键域之外自会夹到首末键值。 */
FVector EvaluateCurve(const UCurveVector& Curve, const float Seconds)
{
	return Curve.GetVectorValue(Seconds);
}

/**
 * 单分量在 `Seconds` 处的导数。
 *
 * 录制曲线是**线性键**（M1 的残差就是按线性插值量的，"与运行时一致"）：段斜率就是那一段
 * 的真值，任何差分都会把相邻两段平均掉，包括刚好落在键上时的单侧跳变。所以线性与常量键
 * 直接读段斜率。
 *
 * 其它插值模式（曲线被重新作者化过）用**段内**中心差分：半步长取到相邻键距离的四分之一，
 * 保证不跨出本段去采到折点另一侧的值。
 */
float EvaluateComponentDerivative(const FRichCurve& Curve, const float Seconds)
{
	const TArray<FRichCurveKey>& Keys = Curve.Keys;
	if (Keys.Num() < 2)
	{
		return 0.0f;
	}

	auto ChordSlope = [](const FRichCurveKey& From, const FRichCurveKey& To) -> float
	{
		const float Span = To.Time - From.Time;
		if (Span <= KINDA_SMALL_NUMBER)
		{
			return 0.0f;
		}
		return (To.Value - From.Value) / Span;
	};

	int32 LowerIndex = 0;
	if (Seconds <= Keys[0].Time)
	{
		LowerIndex = 0;
	}
	else if (Seconds >= Keys.Last().Time)
	{
		LowerIndex = Keys.Num() - 2;
	}
	else
	{
		LowerIndex = Keys.Num() - 2;
		for (int32 Index = 0; Index + 1 < Keys.Num(); ++Index)
		{
			if (Seconds >= Keys[Index].Time && Seconds <= Keys[Index + 1].Time)
			{
				LowerIndex = Index;
				break;
			}
		}
	}

	const FRichCurveKey& From = Keys[LowerIndex];
	const FRichCurveKey& To = Keys[LowerIndex + 1];
	if (From.InterpMode == RCIM_Constant)
	{
		return 0.0f;
	}
	if (From.InterpMode == RCIM_Linear || To.Time - From.Time <= KINDA_SMALL_NUMBER)
	{
		return ChordSlope(From, To);
	}

	const float SampleTime = FMath::Clamp(Seconds, From.Time, To.Time);
	if (From.InterpMode == RCIM_Cubic
		&& From.TangentWeightMode == RCTWM_WeightedNone
		&& To.TangentWeightMode == RCTWM_WeightedNone)
	{
		// Unweighted Hermite keys have an exact derivative, including both ends.
		// A clamped 1e-4 finite difference at the first key used to take the chord
		// fallback and erase half a phase's gravity from the launch tangent.
		const float Span = To.Time - From.Time;
		const float Alpha = (SampleTime - From.Time) / Span;
		const float AlphaSquared = Alpha * Alpha;
		return (6.0f * AlphaSquared - 6.0f * Alpha) * (From.Value - To.Value) / Span
			+ (3.0f * AlphaSquared - 4.0f * Alpha + 1.0f) * From.LeaveTangent
			+ (3.0f * AlphaSquared - 2.0f * Alpha) * To.ArriveTangent;
	}
	const float HalfStep = FMath::Max(1.0e-4f,
		FMath::Min(SampleTime - From.Time, To.Time - SampleTime) * 0.25f);
	const float Lower = FMath::Clamp(SampleTime - HalfStep, From.Time, To.Time);
	const float Upper = FMath::Clamp(SampleTime + HalfStep, From.Time, To.Time);
	const float Span = Upper - Lower;
	if (Span <= KINDA_SMALL_NUMBER)
	{
		return ChordSlope(From, To);
	}
	return (Curve.Eval(Upper, To.Value) - Curve.Eval(Lower, From.Value)) / Span;
}

FVector EvaluateCurveDerivative(const UCurveVector& Curve, const float Seconds)
{
	return FVector(
		EvaluateComponentDerivative(Curve.FloatCurves[0], Seconds),
		EvaluateComponentDerivative(Curve.FloatCurves[1], Seconds),
		EvaluateComponentDerivative(Curve.FloatCurves[2], Seconds));
}

/** 覆盖之外：以覆盖末端的位置/速度/加速度继续积分。 */
FVector IntegrateTail(const FRecordedLocalTrajectoryProfile& Profile, const float BeyondSeconds)
{
	const FVector& Position = Profile.Curve
		? EvaluateCurve(*Profile.Curve, Profile.CoveredSeconds)
		: FVector::ZeroVector;
	const double Time = BeyondSeconds;
	return Position + Profile.TerminalVelocityLocal * Time
		+ 0.5 * Profile.TerminalAccelerationLocal * Time * Time;
}

FVector IntegrateTailVelocity(const FRecordedLocalTrajectoryProfile& Profile,
	const float BeyondSeconds)
{
	return Profile.TerminalVelocityLocal + Profile.TerminalAccelerationLocal * BeyondSeconds;
}
}

bool FRecordedLocalTrajectoryProfile::IsValid() const
{
	if (!Curve || !FMath::IsFinite(CoveredSeconds) || CoveredSeconds <= 0.0f
		|| !IsFiniteVector(TerminalVelocityLocal)
		|| !IsFiniteVector(TerminalAccelerationLocal))
	{
		return false;
	}

	float LastKeySeconds = 0.0f;
	if (!MHGZRecordedLocalTrajectory::ValidateCurve(Curve, LastKeySeconds))
	{
		return false;
	}
	// 覆盖终点最多可以落在末键之后**一个 1/120 s 网格**（见 AuthoredKeyGridSeconds）。
	// 超出这个范围就不是「取整误差」了：那是把一段根本没有 authored 位移的时间声明成
	// 已覆盖 —— 曲线在那里被夹住不动，相位会原地冻住。
	return CoveredSeconds <= LastKeySeconds + AuthoredKeyGridSeconds;
}

FVector FRecordedLocalTrajectoryProfile::GetOffset(const float Seconds,
	const ERecordedTrajectoryTailPolicy TailPolicy) const
{
	if (!IsValid() || !FMath::IsFinite(Seconds))
	{
		return FVector::ZeroVector;
	}
	if (Seconds <= CoveredSeconds)
	{
		return EvaluateCurve(*Curve, FMath::Max(0.0f, Seconds));
	}
	if (TailPolicy == ERecordedTrajectoryTailPolicy::HoldLastKey)
	{
		return EvaluateCurve(*Curve, CoveredSeconds);
	}
	return IntegrateTail(*this, Seconds - CoveredSeconds);
}

FVector FRecordedLocalTrajectoryProfile::GetVelocity(const float Seconds,
	const ERecordedTrajectoryTailPolicy TailPolicy) const
{
	if (!IsValid() || !FMath::IsFinite(Seconds))
	{
		return FVector::ZeroVector;
	}
	if (Seconds < CoveredSeconds)
	{
		return EvaluateCurveDerivative(*Curve, FMath::Max(0.0f, Seconds));
	}
	if (TailPolicy == ERecordedTrajectoryTailPolicy::HoldLastKey)
	{
		// 曲线跑完就没有位移了。回读末段斜率会把「已经结束」当成「还在冲」。
		return FVector::ZeroVector;
	}
	return IntegrateTailVelocity(*this, Seconds - CoveredSeconds);
}

bool MHGZRecordedLocalTrajectory::ValidateCurve(const UCurveVector* Curve,
	float& OutLastKeySeconds)
{
	OutLastKeySeconds = 0.0f;
	if (!Curve)
	{
		return false;
	}

	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const TArray<FRichCurveKey>& Keys = Curve->FloatCurves[Axis].Keys;
		if (Keys.Num() < 2
			|| !FMath::IsFinite(Keys[0].Time)
			|| FMath::Abs(Keys[0].Time) > KeyValueTolerance
			|| FMath::Abs(Keys[0].Value) > KeyValueTolerance)
		{
			return false;
		}
		for (int32 KeyIndex = 1; KeyIndex < Keys.Num(); ++KeyIndex)
		{
			if (!FMath::IsFinite(Keys[KeyIndex].Time)
				|| !FMath::IsFinite(Keys[KeyIndex].Value)
				|| Keys[KeyIndex].Time <= Keys[KeyIndex - 1].Time)
			{
				return false;
			}
		}
		// 取三条轴的**最小值**：IsValid 用它给覆盖终点定界，若某条轴比另外两条早收尾，
		// 拿最长的那条定界会把那条轴已经夹住的区间算成「已覆盖」。
		if (Axis == 0 || Keys.Last().Time < OutLastKeySeconds)
		{
			OutLastKeySeconds = Keys.Last().Time;
		}
	}

	return FMath::IsFinite(OutLastKeySeconds) && OutLastKeySeconds > 0.0f;
}

FVector MHGZRecordedLocalTrajectory::ResolveRightVector(
	const FVector& LockedYawDirection)
{
	const FVector Forward = FVector(LockedYawDirection.X, LockedYawDirection.Y, 0.0f);
	if (!IsFiniteVector(Forward) || Forward.IsNearlyZero())
	{
		return FVector::ZeroVector;
	}
	// UE 左手系：WorldUp × Forward 给出右向（Forward=(1,0,0) ⇒ Right=(0,1,0)）。
	return FVector::CrossProduct(FVector::UpVector, Forward.GetSafeNormal());
}

FVector MHGZRecordedLocalTrajectory::LocalToWorld(const FVector& LocalVector,
	const FVector& LockedYawDirection)
{
	const FVector Forward = FVector(LockedYawDirection.X, LockedYawDirection.Y, 0.0f)
		.GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		return FVector::ZeroVector;
	}
	const FVector Right = ResolveRightVector(Forward);
	return Forward * LocalVector.X + Right * LocalVector.Y
		+ FVector::UpVector * LocalVector.Z;
}
