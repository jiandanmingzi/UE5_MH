// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"
#include "MHGZE4HandoffNotifySetupCommandlet.generated.h"

/**
 * Applies the approved E4.2 Root Motion Phase/Handoff contract to mobile
 * routes and the Phase-only ownership contract to directional Dodge routes.
 */
UCLASS()
class MHGZ_API UMHGZE4HandoffNotifySetupCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMHGZE4HandoffNotifySetupCommandlet();
	virtual int32 Main(const FString& Params) override;
};
