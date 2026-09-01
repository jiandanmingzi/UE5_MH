// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"
#include "MHGZE4ExitAssetSetupCommandlet.generated.h"

/**
 * Creates the isolated E4.2 ExitTransition Pose Search assets.  It only
 * modifies the three generated ExitTransition sequences and three new Exit
 * databases; existing locomotion assets are never changed.
 */
UCLASS()
class MHGZ_API UMHGZE4ExitAssetSetupCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMHGZE4ExitAssetSetupCommandlet();
	virtual int32 Main(const FString& Params) override;
};
