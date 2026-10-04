// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"
#include "MHGZActionVisualPitchGraphCommandlet.generated.h"

/** Audits or idempotently inserts the final-slot action-pitch layer in ABP_MH_Character. */
UCLASS()
class MHGZ_API UMHGZActionVisualPitchGraphCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMHGZActionVisualPitchGraphCommandlet();
	virtual int32 Main(const FString& Params) override;
};
