// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"

#include "MHGZPoseSearchTelemetryExportCommandlet.generated.h"

/**
 * Replays the project telemetry exporter against an already-finalized
 * PoseSearch .utrace file. This is intentionally read-only: it never changes
 * animation, PoseSearch, or Blueprint assets.
 */
UCLASS()
class MHGZ_API UMHGZPoseSearchTelemetryExportCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMHGZPoseSearchTelemetryExportCommandlet();

	virtual int32 Main(const FString& Params) override;
};
