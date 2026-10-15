// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"
#include "MHGZM631MarkConfigCommandlet.generated.h"

/**
 * Audits the saved M6.3.1 mark producers and migrates only the approved mark
 * lifetime/patch fields on DA_IG_Combat. Defaults to read-only; pass -Apply to save.
 */
UCLASS()
class MHGZTOOLSEDITOR_API UMHGZM631MarkConfigCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMHGZM631MarkConfigCommandlet();
	virtual int32 Main(const FString& Params) override;
};
