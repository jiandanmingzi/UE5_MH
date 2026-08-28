// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"

#include "MHGZPMM4AssetReportCommandlet.generated.h"

/**
 * Read-only report of the PMM-4 Pose Search schema and databases.
 * It exists to capture the exact existing asset state before the deterministic
 * PMM-4 fixup writes any Pose Search asset.
 */
UCLASS()
class MHGZ_API UMHGZPMM4AssetReportCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMHGZPMM4AssetReportCommandlet();

	virtual int32 Main(const FString& Params) override;
};
