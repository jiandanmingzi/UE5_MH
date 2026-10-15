// Copyright MHGZ Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "MHGZM5KinsectPiercerCurveSetupCommandlet.generated.h"

/** Requires -AuditOnly or -Apply; imports the audited 200/201/203 local trajectories. */
UCLASS()
class MHGZTOOLSEDITOR_API UMHGZM5KinsectPiercerCurveSetupCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UMHGZM5KinsectPiercerCurveSetupCommandlet();
	virtual int32 Main(const FString& Params) override;
};
