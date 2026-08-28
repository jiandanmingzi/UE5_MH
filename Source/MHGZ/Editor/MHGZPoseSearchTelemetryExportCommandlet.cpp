// Copyright MHGZ Project. All Rights Reserved.

#include "Editor/MHGZPoseSearchTelemetryExportCommandlet.h"

#include "Animation/MHGZPoseSearchTelemetryExport.h"
#include "HAL/PlatformString.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

UMHGZPoseSearchTelemetryExportCommandlet::UMHGZPoseSearchTelemetryExportCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMHGZPoseSearchTelemetryExportCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	FString TraceFilePath;
	if (!FParse::Value(*Params, TEXT("Trace="), TraceFilePath) || TraceFilePath.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("[PoseSearchTelemetryExport] Missing required -Trace=<PoseSearchDetail.utrace>."));
		return 1;
	}

	TraceFilePath = FPaths::ConvertRelativePathToFull(TraceFilePath);
	FString OutputDirectory;
	FParse::Value(*Params, TEXT("Output="), OutputDirectory);
	if (OutputDirectory.IsEmpty())
	{
		OutputDirectory = FPaths::GetPath(TraceFilePath);
	}
	else
	{
		OutputDirectory = FPaths::ConvertRelativePathToFull(OutputDirectory);
	}

	int32 TopCandidates = 24;
	FParse::Value(*Params, TEXT("TopN="), TopCandidates);
	TopCandidates = FMath::Clamp(TopCandidates, 1, 256);

	TMap<uint64, FString> DatabasePathOverrides;
	for (int32 OverrideIndex = 0; OverrideIndex < 4; ++OverrideIndex)
	{
		FString TraceDatabaseIdText;
		FString DatabasePath;
		const bool bHasTraceDatabaseId = FParse::Value(*Params,
			*FString::Printf(TEXT("TraceDatabaseId%d="), OverrideIndex), TraceDatabaseIdText);
		const bool bHasDatabasePath = FParse::Value(*Params,
			*FString::Printf(TEXT("Database%d="), OverrideIndex), DatabasePath);
		if (bHasTraceDatabaseId != bHasDatabasePath)
		{
			UE_LOG(LogTemp, Error, TEXT("[PoseSearchTelemetryExport] TraceDatabaseId%d and Database%d must be supplied together."),
				OverrideIndex, OverrideIndex);
			return 1;
		}
		if (bHasTraceDatabaseId)
		{
			const uint64 TraceDatabaseId = FCString::Strtoui64(*TraceDatabaseIdText, nullptr, 10);
			if (TraceDatabaseId == 0 || DatabasePath.IsEmpty())
			{
				UE_LOG(LogTemp, Error, TEXT("[PoseSearchTelemetryExport] Invalid database override %d."), OverrideIndex);
				return 1;
			}
			DatabasePathOverrides.Add(TraceDatabaseId, DatabasePath);
		}
	}

	const bool bExported = FMHGZPoseSearchTelemetryExport::ExportTraceToCSV(TraceFilePath,
		OutputDirectory, TopCandidates, DatabasePathOverrides);
	if (bExported)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[PoseSearchTelemetryExport] Completed Trace=%s Output=%s TopN=%d"),
			*TraceFilePath, *OutputDirectory, TopCandidates);
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[PoseSearchTelemetryExport] Failed Trace=%s Output=%s TopN=%d"),
			*TraceFilePath, *OutputDirectory, TopCandidates);
	}
	return bExported ? 0 : 1;
#else
	UE_LOG(LogTemp, Error, TEXT("[PoseSearchTelemetryExport] This commandlet requires an editor build."));
	return 1;
#endif
}
