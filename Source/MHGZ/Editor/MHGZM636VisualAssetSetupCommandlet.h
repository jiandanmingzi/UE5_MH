// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"
#include "MHGZM636VisualAssetSetupCommandlet.generated.h"

/** Creates/audits the persistent M6.3.6 mark and powder visual assets. */
UCLASS()
class MHGZ_API UMHGZM636VisualAssetSetupCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMHGZM636VisualAssetSetupCommandlet();
	virtual int32 Main(const FString& Params) override;
};
