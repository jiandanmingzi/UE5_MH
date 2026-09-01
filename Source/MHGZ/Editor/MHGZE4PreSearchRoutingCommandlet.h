// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"
#include "MHGZE4PreSearchRoutingCommandlet.generated.h"

/**
 * Rebinds ABP_MH_Character's Motion Matching callbacks so candidate routing runs
 * in the node's generic On Update hook (before search), while state-updated only
 * observes the real selected result. It does not alter AnimGraph pose wires.
 */
UCLASS()
class MHGZ_API UMHGZE4PreSearchRoutingCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMHGZE4PreSearchRoutingCommandlet();
	virtual int32 Main(const FString& Params) override;
};