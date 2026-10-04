// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"
#include "MHGZKinsectActionLayerCommandlet.generated.h"

/** Adds and audits the input-dependent UpperBody_IGAction layer in ABP_MH_Character. */
UCLASS()
class MHGZ_API UMHGZKinsectActionLayerCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMHGZKinsectActionLayerCommandlet();
	virtual int32 Main(const FString& Params) override;
};
