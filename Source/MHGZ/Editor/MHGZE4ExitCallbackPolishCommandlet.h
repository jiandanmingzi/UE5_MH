// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"

#include "MHGZE4ExitCallbackPolishCommandlet.generated.h"

/**
 * One-time E4.2 cleanup for the two Motion Matching State Updated callbacks.
 * Repairs the unsheathed handoff acknowledgement, adds visual function blocks,
 * and moves callback-only snapshots to function-local variables.
 */
UCLASS()
class MHGZ_API UMHGZE4ExitCallbackPolishCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMHGZE4ExitCallbackPolishCommandlet();

	virtual int32 Main(const FString& Params) override;
};
