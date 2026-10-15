// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"
#include "MHGZM6RecordedHunterFlightSetupCommandlet.generated.h"

/** Record-driven setup for the 196 -> 199 HunterFlightStart boundary. */
UCLASS()
class MHGZTOOLSEDITOR_API UMHGZM6RecordedHunterFlightSetupCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMHGZM6RecordedHunterFlightSetupCommandlet();
	virtual int32 Main(const FString& Params) override;
};
