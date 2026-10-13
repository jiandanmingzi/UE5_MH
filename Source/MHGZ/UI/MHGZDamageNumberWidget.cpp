// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZDamageNumberWidget.h"
#include "Components/TextBlock.h"
#include "Styling/CoreStyle.h"

void UMHGZDamageNumberWidget::SetDamageDisplay_Implementation(
	const float ActualDamage, const bool bCritical)
{
	DisplayedDamage = FMath::IsFinite(ActualDamage) ? FMath::Max(0.0f, ActualDamage) : 0.0f;
	bDisplayedCritical = bCritical;
	SetRenderOpacity(1.0f);
	if (!DamageText) { return; }
	FNumberFormattingOptions Format;
	Format.SetUseGrouping(false).SetMinimumFractionalDigits(0).SetMaximumFractionalDigits(0);
	Format.SetRoundingMode(ERoundingMode::HalfFromZero);
	DamageText->SetText(FText::AsNumber(DisplayedDamage, &Format));
	DamageText->SetColorAndOpacity(FSlateColor(bCritical ? CriticalColor : NormalColor));
	FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),
		bCritical ? CriticalFontSize : NormalFontSize);
	Font.OutlineSettings.OutlineSize = 2;
	Font.OutlineSettings.OutlineColor = FLinearColor::Black;
	DamageText->SetFont(Font);
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

FText UMHGZDamageNumberWidget::GetDisplayedText() const
{
	return DamageText ? DamageText->GetText() : FText::GetEmpty();
}
