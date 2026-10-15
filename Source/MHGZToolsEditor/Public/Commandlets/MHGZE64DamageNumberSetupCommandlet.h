// Copyright MHGZ Project. All Rights Reserved.
#pragma once

#include "Commandlets/Commandlet.h"
#include "MHGZE64DamageNumberSetupCommandlet.generated.h"

/** E6.4 numbers only: explicit -Apply creates/wires three persistent assets; default audits. */
UCLASS()
class MHGZTOOLSEDITOR_API UMHGZE64DamageNumberSetupCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UMHGZE64DamageNumberSetupCommandlet();
	virtual int32 Main(const FString& Params) override;
};
