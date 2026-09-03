// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"
#include "MHGZM421LoopCandidateAssetSetupCommandlet.generated.h"

/**
 * Creates and audits the isolated M4.2.1 sheathed Run/Sprint LoopOnly Pose
 * Search databases. It never modifies the source locomotion sequences or the
 * existing full-move database.
 */
UCLASS()
class MHGZ_API UMHGZM421LoopCandidateAssetSetupCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMHGZM421LoopCandidateAssetSetupCommandlet();
	virtual int32 Main(const FString& Params) override;
};
