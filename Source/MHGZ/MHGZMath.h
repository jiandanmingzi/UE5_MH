// Copyright MHGZ Project. All Rights Reserved.
#pragma once

#include "Math/Vector.h"

namespace MHGZMath
{
/** 三个分量均为有限数；不归一化、不修改数值，不把零向量判为无效。 */
inline bool IsFiniteVector(const FVector& Value)
{
	return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y)
		&& FMath::IsFinite(Value.Z);
}
}
