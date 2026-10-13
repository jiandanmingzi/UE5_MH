// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"
#include "MHGZM6AssetSetupCommandlet.generated.h"

/** Idempotent setup/audit for persistent M6 action assets. */
UCLASS()
class MHGZ_API UMHGZM6AssetSetupCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMHGZM6AssetSetupCommandlet();
	virtual int32 Main(const FString& Params) override;
};
