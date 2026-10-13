// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"

#include "MHGZM6BaselineAuditCommandlet.generated.h"

/** Read-only audit of the M6.1 Insect Glaive animation source assets. */
UCLASS()
class MHGZ_API UMHGZM6BaselineAuditCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMHGZM6BaselineAuditCommandlet();

	virtual int32 Main(const FString& Params) override;
};
