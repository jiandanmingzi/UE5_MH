// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"

#include "MHGZE4ActionExitAuditCommandlet.generated.h"

/**
 * Read-only E4.2 audit for the small, explicitly allowed Action Exit source
 * set. It never modifies assets: it only loads Montage/Sequence data and
 * writes a Markdown report under Saved/ActionExitAudit for frame selection.
 */
UCLASS()
class MHGZ_API UMHGZE4ActionExitAuditCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMHGZE4ActionExitAuditCommandlet();

	virtual int32 Main(const FString& Params) override;
};
