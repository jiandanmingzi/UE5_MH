// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"

#include "MHGZE4TailStartTimeRepairCommandlet.generated.h"

/** Repairs the two malformed local TailStartTime setter pins left by an early E4.2 migration run. */
UCLASS()
class MHGZ_API UMHGZE4TailStartTimeRepairCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMHGZE4TailStartTimeRepairCommandlet();

	virtual int32 Main(const FString& Params) override;
};
