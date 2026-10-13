// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MHGZDamageNumberWidget.generated.h"

class UTextBlock;

/** 伤害数字的表现端；输入是已结算伤害，不拥有也不修改战斗数值。 */
UCLASS(Abstract)
class MHGZ_API UMHGZDamageNumberWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, Category = "MHGZ|Feedback")
	void SetDamageDisplay(float ActualDamage, bool bCritical);
	virtual void SetDamageDisplay_Implementation(float ActualDamage, bool bCritical);

	float GetDisplayedDamage() const { return DisplayedDamage; }
	bool IsDisplayedCritical() const { return bDisplayedCritical; }
	FText GetDisplayedText() const;

protected:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> DamageText;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MHGZ|Feedback")
	FLinearColor NormalColor = FLinearColor(0.95f, 0.95f, 0.91f, 1.0f);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MHGZ|Feedback")
	FLinearColor CriticalColor = FLinearColor(1.0f, 0.68f, 0.15f, 1.0f);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MHGZ|Feedback", meta = (ClampMin = "1"))
	int32 NormalFontSize = 20;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MHGZ|Feedback", meta = (ClampMin = "1"))
	int32 CriticalFontSize = 24;
	float DisplayedDamage = 0.0f;
	bool bDisplayedCritical = false;
};
