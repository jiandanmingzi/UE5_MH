// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"
#include "MHGZPMM7AssetFixupCommandlet.generated.h"

/**
 * Applies the PMM-7 asset contract after generated Stop assets are rebuilt:
 * installs the native MM_StopGait channel, writes explicit lane curves, makes
 * the formal database membership deterministic, and rebuilds both indices.
 */
UCLASS()
class MHGZ_API UMHGZPMM7AssetFixupCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMHGZPMM7AssetFixupCommandlet();

	virtual int32 Main(const FString& Params) override;
};
