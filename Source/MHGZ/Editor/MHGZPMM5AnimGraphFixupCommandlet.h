// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"

#include "MHGZPMM5AnimGraphFixupCommandlet.generated.h"

/**
 * Applies the non-visual PMM-5 search-coherence settings to the two existing
 * Motion Matching nodes in ABP_MH_Character. It does not alter AnimGraph wires.
 */
UCLASS()
class MHGZ_API UMHGZPMM5AnimGraphFixupCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMHGZPMM5AnimGraphFixupCommandlet();

	virtual int32 Main(const FString& Params) override;
};
