// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace UE::MHGZ::EditorTools
{
bool BuildE63WidgetBlueprint(UObject* WidgetBlueprintAsset, FName LayoutName);
bool ExportTraceToCSV(const FString& TraceFilePath, const FString& OutputDirectory,
	int32 TopCandidatesPerDatabase, const TMap<uint64, FString>& DatabasePathOverrides);
}
