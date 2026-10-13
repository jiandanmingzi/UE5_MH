// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZUITelemetry.h"

uint64 FMHGZUIEventTelemetryQueue::Record(FMHGZUIEventTelemetry Event)
{
	Event.Serial = NextSerial++;
	if (NextSerial == 0)
	{
		NextSerial = 1;
	}
	Events.Add(MoveTemp(Event));
	if (Events.Num() > MaxEvents)
	{
		Events.RemoveAt(0, Events.Num() - MaxEvents, EAllowShrinking::No);
	}
	return Events.Last().Serial;
}

void FMHGZUIEventTelemetryQueue::GetEventsSince(
	uint64& InOutLastSerial, TArray<FMHGZUIEventTelemetry>& OutEvents) const
{
	OutEvents.Reset();
	for (const FMHGZUIEventTelemetry& Event : Events)
	{
		if (Event.Serial > InOutLastSerial)
		{
			OutEvents.Add(Event);
			InOutLastSerial = Event.Serial;
		}
	}
}
