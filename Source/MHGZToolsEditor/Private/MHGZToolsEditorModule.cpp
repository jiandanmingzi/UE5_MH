// Copyright MHGZ Project. All Rights Reserved.

#include "EditorServices/MHGZEditorToolServices.h"
#include "Features/IModularFeatures.h"
#include "MHGZEditorToolImplementations.h"
#include "Modules/ModuleManager.h"

class FMHGZToolsEditorModule final : public IModuleInterface, public IMHGZEditorToolServices
{
public:
	virtual void StartupModule() override
	{
		IModularFeatures::Get().RegisterModularFeature(GetFeatureName(), this);
	}

	virtual void ShutdownModule() override
	{
		// Drop the provider before unloading the DLL. Facades never cache it.
		IModularFeatures::Get().UnregisterModularFeature(GetFeatureName(), this);
	}

	virtual bool BuildE63WidgetBlueprint(UObject* Asset, const FName LayoutName) override
	{
		return UE::MHGZ::EditorTools::BuildE63WidgetBlueprint(Asset, LayoutName);
	}

	virtual bool ExportTraceToCSV(const FString& TraceFilePath, const FString& OutputDirectory,
		const int32 TopCandidatesPerDatabase, const TMap<uint64, FString>& Overrides) override
	{
		return UE::MHGZ::EditorTools::ExportTraceToCSV(
			TraceFilePath, OutputDirectory, TopCandidatesPerDatabase, Overrides);
	}
};

IMPLEMENT_MODULE(FMHGZToolsEditorModule, MHGZToolsEditor);
