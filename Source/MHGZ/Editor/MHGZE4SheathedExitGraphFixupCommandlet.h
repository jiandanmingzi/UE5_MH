// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"

#include "MHGZE4SheathedExitGraphFixupCommandlet.generated.h"

/**
 * One-time E4.2 editor migration. Repairs the sheathed Exit duration writes
 * and adds non-functional visual groups to the thread-safe MM node callback.
 */
UCLASS()
class MHGZ_API UMHGZE4SheathedExitGraphFixupCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMHGZE4SheathedExitGraphFixupCommandlet();

	virtual int32 Main(const FString& Params) override;
};
