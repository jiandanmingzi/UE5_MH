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
	/** Delivered lamp warning intensity (not alpha); TripleUp shares one countdown. */
	static float GetExtractWarningIntensity(const FIGResourceUISnapshot& State, int32 ExtractIndex);
	/** Pale/desaturated warning colour, always preserving the supplied opacity. */
	static FLinearColor GetExtractWarningColor(const FIGResourceUISnapshot& State,
		int32 ExtractIndex, const FLinearColor& BaseColor);

protected:
	/** 猎虫当前耐力比例对应的进度条控件。 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> KinsectStaminaBar;

	/** 猎虫耐力数值的可选文字控件。 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> KinsectStaminaText;

	/** 猎虫附着、飞行或悬停状态的可选文字控件。 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> KinsectStateText;

	/** 玩家红灯剩余时长比例的进度控件。 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> RedExtractBar;

	/** 玩家白灯剩余时长比例的进度控件。 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> WhiteExtractBar;

	/** 玩家橙灯剩余时长比例的进度控件。 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> OrangeExtractBar;

	/** 红灯图标或标识使用的文字控件，按剩余时间变淡提示。 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> RedExtractText;

	/** 白灯图标或标识使用的文字控件，按剩余时间变淡提示。 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> WhiteExtractText;

	/** 橙灯图标或标识使用的文字控件，按剩余时间变淡提示。 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> OrangeExtractText;

	/** 三灯强化状态及时间的可选文字控件。 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TripleUpText;

	/** 猎虫已携带、尚未回手交付精华的可选文字显示。 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> KinsectPendingExtractText;

	/** 以竖立椭圆显示猎虫携带精华颜色的图标控件，可按多色快照分区。 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBorder> KinsectPendingExtractIcon;

	/** 当前舞踏层数的文字控件。 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DanceStacksText;

	/** 当前可用及预留粉尘数量的文字控件。 */
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
