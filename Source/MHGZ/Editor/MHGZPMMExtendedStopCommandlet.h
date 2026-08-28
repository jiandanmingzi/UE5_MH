// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"

#include "MHGZPMMExtendedStopCommandlet.generated.h"

/**
 * Builds non-destructive, phase-covered Extended Stop animation sequences for
 * PMM-7. It covers sheathed Walk/Run/Sprint ExtendedStop plus a dedicated
 * FirstStepCommitStop per speed family. Sprint uses its own Start but formal
 * Run Stop L/R because no authored Sprint Stop assets exist.
 */
UCLASS()
class MHGZ_API UMHGZPMMExtendedStopCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMHGZPMMExtendedStopCommandlet();

	virtual int32 Main(const FString& Params) override;
};
