// Copyright MHGZ Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

/** Frozen socket -> camera-ray path. Arc length keeps speed independent of parallax.
 * The initial curve and subsequent ray have the same terminal tangent. No assets,
 * live camera tracking, collision, command ownership or movement writers live here.
 */
struct FKinsectAimRayPath
{
	bool Build(const FVector& Start, const FVector& Origin, const FVector& Direction,
		const FVector& Target, const float MergeDistance)
	{
		if (Start.ContainsNaN() || Origin.ContainsNaN() || Direction.ContainsNaN()
			|| Target.ContainsNaN() || !FMath::IsFinite(MergeDistance) || MergeDistance <= 0.f)
			return false;
		RayDirection = Direction.GetSafeNormal();
		if (RayDirection.IsNearlyZero()) return false;
		const double StartOnRay = FVector::DotProduct(Start - Origin, RayDirection);
		const double TargetOnRay = FVector::DotProduct(Target - Origin, RayDirection);
		if (TargetOnRay <= UE_KINDA_SMALL_NUMBER) return false;
		const double Remaining = TargetOnRay - StartOnRay;
		// Join before the frozen target, reducing the curve for close surfaces.
		// A target behind the socket's ray projection is approached continuously;
		// the socket itself is never teleported backward through the hunter.
		const double Advance = Remaining > 0.0 ? FMath::Min<double>(MergeDistance, Remaining * 0.5) : Remaining;
		P0 = Start;
		P3 = Origin + RayDirection * (StartOnRay + Advance);
		const double Chord = FVector::Distance(P0, P3);
		const double Handle = FMath::Max((Remaining > 0.0 ? FMath::Min(Chord, Advance) : Chord) / 3.0, 0.001);
		const FVector InitialTangent = Remaining > 0.0 ? RayDirection : (P3 - P0).GetSafeNormal();
		P1 = P0 + InitialTangent * Handle;
		P2 = P3 - RayDirection * Handle;
		Lengths[0] = 0.0;
		FVector Previous = P0;
		for (int32 Index = 1; Index <= Samples; ++Index)
		{
			const FVector Point = Evaluate(static_cast<double>(Index) / Samples);
			Lengths[Index] = Lengths[Index - 1] + FVector::Distance(Previous, Point);
			Previous = Point;
		}
		return FMath::IsFinite(Lengths[Samples]);
	}

	void Sample(const double Distance, FVector& Position, FVector& Tangent) const
	{
		if (Distance >= Lengths[Samples])
		{
			Position = P3 + RayDirection * (Distance - Lengths[Samples]);
			Tangent = RayDirection;
			return;
		}
		int32 Low = 0, High = Samples;
		while (High - Low > 1)
		{
			const int32 Mid = (Low + High) / 2;
			if (Lengths[Mid] < Distance) Low = Mid; else High = Mid;
		}
		const double Span = Lengths[High] - Lengths[Low];
		const double Fraction = Span > UE_SMALL_NUMBER ? (Distance - Lengths[Low]) / Span : 0.0;
		const double T = (Low + FMath::Clamp(Fraction, 0.0, 1.0)) / Samples;
		Position = Evaluate(T);
		const double U = 1.0 - T;
		Tangent = ((P1-P0)*(U*U) + (P2-P1)*(2.0*U*T) + (P3-P2)*(T*T)).GetSafeNormal();
		if (Tangent.IsNearlyZero()) Tangent = RayDirection;
	}

	double GetMergeLength() const { return Lengths[Samples]; }

private:
	FVector Evaluate(const double T) const
	{
		const double U = 1.0-T;
		return P0*(U*U*U) + P1*(3.0*U*U*T) + P2*(3.0*U*T*T) + P3*(T*T*T);
	}
	static constexpr int32 Samples = 64;
	FVector P0 = FVector::ZeroVector, P1 = FVector::ZeroVector;
	FVector P2 = FVector::ZeroVector, P3 = FVector::ZeroVector;
	FVector RayDirection = FVector::ForwardVector;
	double Lengths[Samples+1] = {};
};
