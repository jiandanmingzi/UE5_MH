// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"
#include "MHGZM5DescendingThrustAssetSetupCommandlet.generated.h"

/** Generates and audits the persistent Descending Thrust assets and routes. */
UCLASS()
class MHGZ_API UMHGZM5DescendingThrustAssetSetupCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMHGZM5DescendingThrustAssetSetupCommandlet();
	virtual int32 Main(const FString& Params) override;
};
