// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InsectGlaive/IGResourceUITypes.h"
#include "UI/MHGZWeaponResourceWidget.h"
#include "MHGZInsectGlaiveResourceWidget.generated.h"

class URes_InsectGlaive;
class UBorder;
class UProgressBar;
class UTextBlock;

/** 虫棍资源只读面板；WBP资产承载原生World风格显示，战斗状态仍由Resource持有。 */
UCLASS(Abstract)
class MHGZ_API UMHGZInsectGlaiveResourceWidget : public UMHGZWeaponResourceWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& Geometry,
		const FSlateRect& CullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual void BindToResourceComponent(
		UMHGZWeaponResourceComponent* InRC) override;
	virtual void UnbindFromResourceComponent() override;

	UFUNCTION(BlueprintNativeEvent, Category = "MHGZ|UI")
	void OnResourceUIUpdated(const FIGResourceUISnapshot& State);
	virtual void OnResourceUIUpdated_Implementation(
		const FIGResourceUISnapshot& State);
	/** Read-only display adapter; never grants or accumulates gameplay extracts. */
	static TArray<FGameplayTag> GetDisplayedCargoColors(const FIGResourceUISnapshot& State);

protected:
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> KinsectStaminaBar;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> KinsectStaminaText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> KinsectStateText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> RedExtractBar;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> WhiteExtractBar;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> OrangeExtractBar;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> RedExtractText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> WhiteExtractText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> OrangeExtractText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TripleUpText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> KinsectPendingExtractText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBorder> KinsectPendingExtractIcon;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DanceStacksText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> PowderCountText;

	void HandleResourceUIChanged(const FIGResourceUISnapshot& State);

private:
	friend struct FMHGZM6HUDTestAccess;
	// Render cache only; never used by gameplay or resource consumption.
	FIGResourceUISnapshot DisplayState;
	TWeakObjectPtr<URes_InsectGlaive> BoundInsectGlaiveResource;
	FDelegateHandle ResourceUIHandle;
};
