// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MHGZUserWidget.h"
#include "MHGZCrosshairWidget.generated.h"

class UMHGZAimComponent;
class UAbilitySystemComponent;
class FPaintArgs;
class FSlateRect;
class FSlateWindowElementList;
class FWidgetStyle;

UENUM(BlueprintType)
enum class EMHGZAimDisplayMode : uint8
{
	None,
	Kinsect,
	Action
};

/**
 * UMHGZCrosshairWidget — 准心 Widget
 * 订阅 UMHGZAimComponent::OnAimTargetChanged
 */
UCLASS(Abstract)
class UMHGZCrosshairWidget : public UMHGZUserWidget
{
	GENERATED_BODY()

public:
	virtual void BindToASC(UAbilitySystemComponent* InASC) override;
	virtual void UnbindFromASC() override;
	void BindToAimComponent(UMHGZAimComponent* InAimComponent);
	void UnbindFromAimComponent();
	virtual void NativeDestruct() override;

	UFUNCTION(BlueprintNativeEvent, Category = "MHGZ|UI")
	void OnAimTargetUpdated(AActor* Target, FGameplayTag HitzoneTag, FGameplayTag ExtractColor);
	virtual void OnAimTargetUpdated_Implementation(AActor* Target, FGameplayTag HitzoneTag, FGameplayTag ExtractColor);

	UFUNCTION(BlueprintNativeEvent, Category = "MHGZ|UI")
	void OnAimVisibilityUpdated(bool bVisible);

	/** Display-only state; never drives camera pose or gameplay aim snapshots. */
	UFUNCTION(BlueprintNativeEvent, Category = "MHGZ|UI")
	void OnAimDisplayStateUpdated(EMHGZAimDisplayMode Mode, AActor* Target,
		FGameplayTag HitzoneTag, FGameplayTag ExtractColor);
	virtual void OnAimDisplayStateUpdated_Implementation(
		EMHGZAimDisplayMode Mode, AActor* Target,
		FGameplayTag HitzoneTag, FGameplayTag ExtractColor);
	virtual void OnAimVisibilityUpdated_Implementation(bool bVisible);
	virtual int32 NativePaint(const FPaintArgs& Args,
		const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

protected:
	void HandleAimingActionTagChanged(const FGameplayTag Tag, int32 NewCount);
	void HandleAimingKinsectTagChanged(const FGameplayTag Tag, int32 NewCount);
	UFUNCTION()
	void HandleAimTargetChanged(AActor* Target, FGameplayTag HitzoneTag,
		FGameplayTag ExtractColor);
	void RefreshAimDisplay();
	EMHGZAimDisplayMode GetAimDisplayMode() const;
	void PublishAimDisplayState(AActor* Target, FGameplayTag HitzoneTag,
		FGameplayTag ExtractColor);

private:
	friend struct FMHGZM6HUDTestAccess;
	TWeakObjectPtr<UMHGZAimComponent> BoundAimComponent;
	FDelegateHandle ActionAimTagHandle;
	FDelegateHandle KinsectAimTagHandle;
	bool bActionAimActive = false;
	bool bKinsectAimActive = false;
	bool bAimDisplayVisible = false;
	bool bAimHasTarget = false;
	EMHGZAimDisplayMode CurrentAimDisplayMode = EMHGZAimDisplayMode::None;
	FGameplayTag CurrentAimExtractColor;
};
