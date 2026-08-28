// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Editor-only exporter for the native Pose Search trace emitted during an
 * opt-in runtime telemetry session. The implementation is a harmless stub in
 * non-editor targets, so gameplay telemetry never depends on TraceAnalysis.
 */
class MHGZ_API FMHGZPoseSearchTelemetryExport
{
public:
	/**
	 * Converts the captured PoseSearch trace into AI-readable candidate and
	 * channel-cost CSV files. Returns false when the trace cannot be read.
	 */
	static bool ExportTraceToCSV(const FString& TraceFilePath, const FString& OutputDirectory,
		int32 TopCandidatesPerDatabase, const TMap<uint64, FString>& DatabasePathOverrides = {});
};
