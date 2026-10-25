// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectTypes.h"
#include "MHGZUserWidget.h"
#include "MHGZAttributeBarWidget.generated.h"

class UAbilitySystemComponent;
class UProgressBar;
class UTextBlock;
struct FOnAttributeChangeData;

UENUM(BlueprintType)
enum class EMHGZAttributeBarKind : uint8
{
	Health,
	Stamina
};

/** ASC驱动的属性显示基类；具体属性由血条/耐力条蓝图类默认值指定。 */
UCLASS(Abstract)
class MHGZ_API UMHGZAttributeBarWidget : public UMHGZUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;

	/** 该属性条显示生命、耐力或其他属性的种类。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MHGZ|UI|Attribute")
	EMHGZAttributeBarKind BarKind = EMHGZAttributeBarKind::Health;

	/** 属性条当前值绑定的GAS属性。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MHGZ|UI|Attribute")
	FGameplayAttribute CurrentAttribute;

	/** 属性条上限绑定的GAS属性。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MHGZ|UI|Attribute")
	FGameplayAttribute MaxAttribute;

	/** 此上限对应现有完整条长；只影响显示，不限制 GAS 属性。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MHGZ|UI|Attribute", meta = (ClampMin = "1.0"))
	float ReferenceMaxValue = 200.0f;

	/** 上限为 ReferenceMaxValue 时的条身长度（Slate单位，不含左侧图标）。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MHGZ|UI|Attribute", meta = (ClampMin = "1.0"))
	float ReferenceGaugeWidth = 582.0f;

	virtual void BindToASC(UAbilitySystemComponent* InASC) override;
	virtual void UnbindFromASC() override;
	virtual void OnValueUpdated_Implementation(float Current, float Max) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& Geometry,
		const FSlateRect& CullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& Style, bool bParentEnabled) const override;

protected:
	/** 蓝图中绑定的数值进度条控件。 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> ValueBar;

	/** 蓝图中绑定的数值文字控件。 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ValueText;

	void HandleAttributeChanged(const FOnAttributeChangeData& ChangeData);
	void PublishCurrentValues();
	void ResolveDefaultAttributes();
	void ResolveWidgetBindings();
	float GetGaugeWidth() const;
	void UpdateGaugeLayout();

	FDelegateHandle CurrentAttributeHandle;
	FDelegateHandle MaxAttributeHandle;
	friend struct FMHGZM6HUDTestAccess;
	float DisplayRatio = 0.0f;
	float DisplayMaxValue = 0.0f;
};
