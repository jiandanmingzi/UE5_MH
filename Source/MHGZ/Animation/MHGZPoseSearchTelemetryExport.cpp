// Copyright MHGZ Project. All Rights Reserved.

#include "Animation/MHGZPoseSearchTelemetryExport.h"

#if WITH_EDITOR
#include "EditorServices/MHGZEditorToolServices.h"
#include "Features/IModularFeatures.h"
#endif

bool FMHGZPoseSearchTelemetryExport::ExportTraceToCSV(const FString& TraceFilePath,
	const FString& OutputDirectory, const int32 TopCandidatesPerDatabase,
	const TMap<uint64, FString>& DatabasePathOverrides)
{
#if WITH_EDITOR
	IModularFeatures& Features = IModularFeatures::Get();
	if (Features.IsModularFeatureAvailable(IMHGZEditorToolServices::GetFeatureName()))
	{
		return Features.GetModularFeature<IMHGZEditorToolServices>(
			IMHGZEditorToolServices::GetFeatureName()).ExportTraceToCSV(
				TraceFilePath, OutputDirectory, TopCandidatesPerDatabase, DatabasePathOverrides);
	}
#endif
	return false;
}
