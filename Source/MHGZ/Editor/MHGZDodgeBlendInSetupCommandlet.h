// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"
#include "MHGZDodgeBlendInSetupCommandlet.generated.h"

/**
 * Adds a three-frame static entry pose to every authored dodge Montage slot.
 * Existing sections and Montage-level gameplay notifies move with DodgeCore.
 */
UCLASS()
class MHGZ_API UMHGZDodgeBlendInSetupCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMHGZDodgeBlendInSetupCommandlet();
	virtual int32 Main(const FString& Params) override;
};
