// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"
#include "MHGZM5KinsectPiercerAssetSetupCommandlet.generated.h"

/** Creates and audits the persistent Enhanced Kinsect Piercer editor assets. */
UCLASS()
class MHGZTOOLSEDITOR_API UMHGZM5KinsectPiercerAssetSetupCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMHGZM5KinsectPiercerAssetSetupCommandlet();
	virtual int32 Main(const FString& Params) override;
};
