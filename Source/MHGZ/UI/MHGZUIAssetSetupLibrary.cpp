// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZUIAssetSetupLibrary.h"

#if WITH_EDITOR
#include "EditorServices/MHGZEditorToolServices.h"
#include "Features/IModularFeatures.h"
#endif

bool UMHGZUIAssetSetupLibrary::BuildE63WidgetBlueprint(UObject* WidgetBlueprintAsset, const FName LayoutName)
{
#if WITH_EDITOR
	IModularFeatures& Features = IModularFeatures::Get();
	if (Features.IsModularFeatureAvailable(IMHGZEditorToolServices::GetFeatureName()))
	{
		return Features.GetModularFeature<IMHGZEditorToolServices>(
			IMHGZEditorToolServices::GetFeatureName()).BuildE63WidgetBlueprint(WidgetBlueprintAsset, LayoutName);
	}
#endif
	return false;
}
