// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"

#include "MHGZE4ExitTransitionCommandlet.generated.h"

/**
 * Generates the three E4.2-approved ExitTransition sequences from visually
 * approved Montage frame boundaries. It only creates dedicated assets and
 * never alters their source sequences or Montages.
 */
UCLASS()
class MHGZ_API UMHGZE4ExitTransitionCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMHGZE4ExitTransitionCommandlet();

	virtual int32 Main(const FString& Params) override;
};
