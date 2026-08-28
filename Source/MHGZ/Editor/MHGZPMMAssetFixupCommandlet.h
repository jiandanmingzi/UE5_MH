// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"

#include "MHGZPMMAssetFixupCommandlet.generated.h"

/**
 * Applies the deterministic PMM-3 curve and Pose Search control-notify contract
 * to the formal locomotion sequences currently used by the two motion-matching PSDs.
 */
UCLASS()
class MHGZ_API UMHGZPMMAssetFixupCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMHGZPMMAssetFixupCommandlet();

	virtual int32 Main(const FString& Params) override;
};
