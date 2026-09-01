// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"
#include "MHGZE4ActionIdleRoutingCommandlet.generated.h"

/**
 * Installs E4.2 Action-Idle and input-aware Exit tail routing in
 * ABP_MH_Character. The commandlet edits only the two Motion Matching state
 * updated callback graphs and saves only that AnimBlueprint.
 */
UCLASS()
class MHGZ_API UMHGZE4ActionIdleRoutingCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMHGZE4ActionIdleRoutingCommandlet();
	virtual int32 Main(const FString& Params) override;
};
