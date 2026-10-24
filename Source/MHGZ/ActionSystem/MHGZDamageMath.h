// Copyright MHGZ Project. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include <limits>

namespace MHGZDamageMath
{
// Only correct double arithmetic roundoff, never a gameplay-sized epsilon.
inline double FloorIntegerBoundary(const double Value)
{
	const double Base = FMath::FloorToDouble(Value);
	const double Tolerance = 8.0 * std::numeric_limits<double>::epsilon() * FMath::Max(1.0, FMath::Abs(Value));
	return Value > Base && Base + 1.0 - Value <= Tolerance ? Base + 1.0 : Base;
}

inline float PowderDamage(const float Power, const float MotionValue)
{
	if (!FMath::IsFinite(Power) || !FMath::IsFinite(MotionValue) || Power <= 0.f || MotionValue <= 0.f) return 0.f;
	const double Raw = (static_cast<double>(Power) * static_cast<double>(MotionValue)) / 100.0;
	if (!FMath::IsFinite(Raw) || Raw > MAX_flt) return 0.f;
	return static_cast<float>(FMath::Max(1.0, FloorIntegerBoundary(Raw)));
}
}
