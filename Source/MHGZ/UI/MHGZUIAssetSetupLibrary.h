// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MHGZUIAssetSetupLibrary.generated.h"

/** Editor-only bridge for constructing authored E6.3 Widget Blueprint trees. */
UCLASS()
class MHGZ_API UMHGZUIAssetSetupLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "MHGZ|Editor|UI")
	static bool BuildE63WidgetBlueprint(UObject* WidgetBlueprintAsset,
		FName LayoutName);
};
