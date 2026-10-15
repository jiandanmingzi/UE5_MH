// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"
#include "MHGZM63PowderVortexAssetSetupCommandlet.generated.h"

/** Creates or audits the persistent M6.3 Powder Vortex Montage, GA, and Combo edge. */
UCLASS()
class MHGZTOOLSEDITOR_API UMHGZM63PowderVortexAssetSetupCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMHGZM63PowderVortexAssetSetupCommandlet();
	virtual int32 Main(const FString& Params) override;
};
