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

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MHGZ|UI|Attribute")
	EMHGZAttributeBarKind BarKind = EMHGZAttributeBarKind::Health;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MHGZ|UI|Attribute")
	FGameplayAttribute CurrentAttribute;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MHGZ|UI|Attribute")
	FGameplayAttribute MaxAttribute;

	virtual void BindToASC(UAbilitySystemComponent* InASC) override;
	virtual void UnbindFromASC() override;
	virtual void OnValueUpdated_Implementation(float Current, float Max) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& Geometry,
		const FSlateRect& CullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& Style, bool bParentEnabled) const override;

protected:
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> ValueBar;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ValueText;

	void HandleAttributeChanged(const FOnAttributeChangeData& ChangeData);
	void PublishCurrentValues();
	void ResolveDefaultAttributes();
	void ResolveWidgetBindings();

	FDelegateHandle CurrentAttributeHandle;
	FDelegateHandle MaxAttributeHandle;
	friend struct FMHGZM6HUDTestAccess;
	float DisplayRatio = 0.0f;
};
