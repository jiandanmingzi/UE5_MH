// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"

#include "MHGZE4SheathedMacroNamingCommandlet.generated.h"

/**
 * One-time E4.2 maintenance commandlet. Renames the four local macro graphs
 * in OnShthMmUpdate from editor-generated names to their verified contracts.
 */
UCLASS()
class MHGZ_API UMHGZE4SheathedMacroNamingCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMHGZE4SheathedMacroNamingCommandlet();

	virtual int32 Main(const FString& Params) override;
};
