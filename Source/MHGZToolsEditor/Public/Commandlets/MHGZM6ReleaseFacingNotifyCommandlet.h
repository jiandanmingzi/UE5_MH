// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"
#include "MHGZM6ReleaseFacingNotifyCommandlet.generated.h"

/** Audit or place the frame-230 release-facing point notify on the persistent M6 Montage. */
UCLASS()
class MHGZTOOLSEDITOR_API UMHGZM6ReleaseFacingNotifyCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMHGZM6ReleaseFacingNotifyCommandlet();
	virtual int32 Main(const FString& Params) override;
};
