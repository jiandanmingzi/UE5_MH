// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Sparse local-player UI lifecycle event consumed by RuntimeTelemetry. */
struct FMHGZUIEventTelemetry
{
	uint64 Serial = 0;
	uint64 Frame = 0;
	double WorldTimeSeconds = 0.0;
	FName EventName = NAME_None;
	uint64 RuntimeGeneration = 0;
	FName Reason = NAME_None;
	FString SubjectClass;
	FName PawnName = NAME_None;
	uint32 DroppedCount = 0;
};

/** Bounded, game-thread-only queue so UI events survive HUD teardown until CSV drain. */
class FMHGZUIEventTelemetryQueue
{
public:
	uint64 Record(FMHGZUIEventTelemetry Event);
	void GetEventsSince(uint64& InOutLastSerial,
		TArray<FMHGZUIEventTelemetry>& OutEvents) const;
	uint64 GetLatestSerial() const { return NextSerial > 0 ? NextSerial - 1 : 0; }

private:
	static constexpr int32 MaxEvents = 4096;
	uint64 NextSerial = 1;
	TArray<FMHGZUIEventTelemetry> Events;
};
