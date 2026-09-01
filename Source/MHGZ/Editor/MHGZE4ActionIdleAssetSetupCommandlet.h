// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"
#include "MHGZE4ActionIdleAssetSetupCommandlet.generated.h"

/**
 * Creates the two isolated Idle-only Pose Search databases used after a
 * non-handoff functional action ends without locomotion input.  It never
 * changes the source idle sequences or either normal locomotion database.
 */
UCLASS()
class MHGZ_API UMHGZE4ActionIdleAssetSetupCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMHGZE4ActionIdleAssetSetupCommandlet();
	virtual int32 Main(const FString& Params) override;
};
