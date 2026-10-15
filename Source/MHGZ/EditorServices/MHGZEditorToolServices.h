// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Features/IModularFeature.h"

/** Runtime-owned contract implemented by the editor module. No editor types or
 * hard module loads belong here. Callers must not cache the provider.
 */
class IMHGZEditorToolServices : public IModularFeature
{
public:
	static FName GetFeatureName() { return FName(TEXT("MHGZEditorToolServices")); }
	virtual bool BuildE63WidgetBlueprint(UObject* Asset, FName LayoutName) = 0;
	virtual bool ExportTraceToCSV(const FString& TraceFilePath, const FString& OutputDirectory,
		int32 TopCandidatesPerDatabase, const TMap<uint64, FString>& DatabasePathOverrides) = 0;
};
