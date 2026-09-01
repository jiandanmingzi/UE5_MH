// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"

#include "MHGZE4AnimBlueprintAuditCommandlet.generated.h"

/**
 * Read-only E4.2 diagnostic. Exports selected AnimBP function graph nodes and
 * pin links so editor-only callback wiring can be reviewed without modifying
 * the Blueprint asset.
 */
UCLASS()
class MHGZ_API UMHGZE4AnimBlueprintAuditCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMHGZE4AnimBlueprintAuditCommandlet();

	virtual int32 Main(const FString& Params) override;
};
