// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "MHGZM5KinsectSlashCurveSetupCommandlet.generated.h"

/** Builds the shared normalized 187 distance curve from the checked-in recorder CSV. */
UCLASS()
class MHGZ_API UMHGZM5KinsectSlashCurveSetupCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMHGZM5KinsectSlashCurveSetupCommandlet();

	virtual int32 Main(const FString& Params) override;
};
