// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"
#include "MHGZM5KinsectSlashAssetSetupCommandlet.generated.h"

/** Audits/migrates Kinsect Slash assets; legacy full generation is opt-in only. */
UCLASS()
class MHGZ_API UMHGZM5KinsectSlashAssetSetupCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMHGZM5KinsectSlashAssetSetupCommandlet();
	virtual int32 Main(const FString& Params) override;
};
