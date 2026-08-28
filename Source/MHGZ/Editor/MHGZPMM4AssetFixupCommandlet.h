// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"

#include "MHGZPMM4AssetFixupCommandlet.generated.h"

/** Applies the deterministic PMM-4 Pose Search Schema and Database contract. */
UCLASS()
class MHGZ_API UMHGZPMM4AssetFixupCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMHGZPMM4AssetFixupCommandlet();

	virtual int32 Main(const FString& Params) override;
};
