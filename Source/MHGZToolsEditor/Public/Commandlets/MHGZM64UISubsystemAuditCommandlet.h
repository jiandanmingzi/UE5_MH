// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"
#include "MHGZM64UISubsystemAuditCommandlet.generated.h"

/** Read-only on-disk AssetRegistry audit for the legacy MHGZ UI subsystem. */
UCLASS()
class MHGZTOOLSEDITOR_API UMHGZM64UISubsystemAuditCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMHGZM64UISubsystemAuditCommandlet();
	virtual int32 Main(const FString& Params) override;
};
