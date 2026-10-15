// Copyright MHGZ Project. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"

/** Entry crossfades must finish while the outgoing non-loop player can still advance. */
struct FMHGZLocomotionBlendPolicy
{
	static float EntryDuration(float Authored, float Length, float Time, float Delta, float PlayRate = 1.f)
	{
		const float Remaining = FMath::Max(0.f, Length - Time) / FMath::Max(PlayRate, SMALL_NUMBER);
		return FMath::Min(FMath::Max(0.f, Authored), FMath::Max(0.f, Remaining - FMath::Max(Delta, 0.f) - .0001f));
	}
};

/** Phase at the start of an animation update. Search winners never feed this clock. */
struct FMHGZLocomotionPhaseClock
{
	bool bValid = false;
	double Cycle = 0.0;
	float PendingDelta = 0.f;
	float PendingLength = 1.f;

	void Reset() { *this = FMHGZLocomotionPhaseClock(); }
	void Seed(float Time, float Length)
	{
		if (!FMath::IsFinite(Time) || !FMath::IsFinite(Length) || Length <= SMALL_NUMBER) { return; }
		Cycle = FMath::Frac(double(Time) / Length + 1.0);
		bValid = true;
		PendingDelta = 0.f;
	}
	void Advance()
	{
		if (bValid) { Cycle = FMath::Frac(Cycle + double(PendingDelta) / PendingLength); }
	}
	void Schedule(float Delta, float Length)
	{
		PendingDelta = FMath::Max(Delta, 0.f);
		PendingLength = FMath::Max(Length, SMALL_NUMBER);
	}
};
