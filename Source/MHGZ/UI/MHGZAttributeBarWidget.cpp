// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZAttributeBarWidget.h"
#include "AbilitySystemComponent.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "MHGZHUDDrawing.h"

void UMHGZAttributeBarWidget::NativeConstruct()
{
	Super::NativeConstruct();
	ResolveWidgetBindings();
	ResolveDefaultAttributes();
	// Retain named BP controls, but hide the rectangular placeholder skin.
	if (WidgetTree)
	{
		WidgetTree->ForEachWidget([this](UWidget* Widget)
		{
			if (Widget != WidgetTree->RootWidget) { Widget->SetVisibility(ESlateVisibility::Hidden); }
		});
	}
	// Binding can precede Slate construction. Never reset a live ratio here.
	PublishCurrentValues();
}

void UMHGZAttributeBarWidget::ResolveWidgetBindings()
{
	if (!ValueBar) { ValueBar = Cast<UProgressBar>(GetWidgetFromName(TEXT("ValueBar"))); }
	if (!ValueText) { ValueText = Cast<UTextBlock>(GetWidgetFromName(TEXT("ValueText"))); }
}

void UMHGZAttributeBarWidget::BindToASC(UAbilitySystemComponent* InASC)
{
	UnbindFromASC();
	ResolveWidgetBindings();
	ResolveDefaultAttributes();
	BoundASC = InASC;
	if (!InASC || !CurrentAttribute.IsValid() || !MaxAttribute.IsValid())
	{
		OnValueUpdated(0.0f, 0.0f);
		return;
	}
	CurrentAttributeHandle = InASC->GetGameplayAttributeValueChangeDelegate(
		CurrentAttribute).AddUObject(this, &UMHGZAttributeBarWidget::HandleAttributeChanged);
	MaxAttributeHandle = InASC->GetGameplayAttributeValueChangeDelegate(
		MaxAttribute).AddUObject(this, &UMHGZAttributeBarWidget::HandleAttributeChanged);
	PublishCurrentValues();
	UE_LOG(LogTemp, Display, TEXT("[E6.3HUD] Gauge bound=%s ASC=%s ratio=%.3f"),
		*GetNameSafe(this), *GetNameSafe(InASC), DisplayRatio);
}

void UMHGZAttributeBarWidget::ResolveDefaultAttributes()
{
	if (CurrentAttribute.IsValid() && MaxAttribute.IsValid()) { return; }
	if (BarKind == EMHGZAttributeBarKind::Stamina)
	{
		CurrentAttribute = UMHGZAttributeSet::GetStaminaAttribute();
		MaxAttribute = UMHGZAttributeSet::GetMaxStaminaAttribute();
	}
	else
	{
		CurrentAttribute = UMHGZAttributeSet::GetHealthAttribute();
		MaxAttribute = UMHGZAttributeSet::GetMaxHealthAttribute();
	}
}

void UMHGZAttributeBarWidget::OnValueUpdated_Implementation(float Current, float Max)
{
	DisplayRatio = Max > UE_SMALL_NUMBER ? FMath::Clamp(Current / Max, 0.0f, 1.0f) : 0;
	if (ValueBar) { ValueBar->SetPercent(DisplayRatio); }
	InvalidateLayoutAndVolatility();
}

int32 UMHGZAttributeBarWidget::NativePaint(const FPaintArgs& Args,
	const FGeometry& Geometry, const FSlateRect& CullingRect,
	FSlateWindowElementList& Out, int32 LayerId,
	const FWidgetStyle& Style, bool bParentEnabled) const
{
	using namespace MHGZHUDDrawing;
	const int32 Layer = Super::NativePaint(Args, Geometry, CullingRect,
		Out, LayerId, Style, bParentEnabled) + 1;
	const bool bStamina = BarKind == EMHGZAttributeBarKind::Stamina;
	const FVector2D Size = Geometry.GetLocalSize();
	const double Height = bStamina ? 8.0 : 13.0;
	const double Y = (Size.Y - Height) * 0.5;
	const FLinearColor Tint = Style.GetColorAndOpacityTint() * GetColorAndOpacity();
	const FLinearColor Fill = (bStamina ? FLinearColor(0.86f, 0.66f, 0.06f, 1)
		: FLinearColor(0.14f, 0.58f, 0.24f, 1)) * Tint;
	Gauge(Out, Layer, Geometry, FVector2D(36, Y), FMath::Max(1.0, Size.X - 38),
		Height, DisplayRatio, Fill);
	const FVector2D Emblem(17, Size.Y * 0.5);
	if (bStamina)
	{
		Polygon(Out, Layer, Geometry, Diamond(Emblem, 6), FLinearColor(0.09f, 0.08f, 0.03f, 1));
		Outline(Out, Layer + 1, Geometry, Diamond(Emblem, 6), Fill, 1.5f);
	}
	else
	{
		Circle(Out, Layer + 1, Geometry, Emblem, 12, FLinearColor(0.04f, 0.11f, 0.18f, 1), 5);
		Circle(Out, Layer + 2, Geometry, Emblem, 10, FLinearColor(0.19f, 0.38f, 0.49f, 1) * Tint, 1.5f);
		Circle(Out, Layer + 3, Geometry, Emblem + FVector2D(0, 1), 6,
			FLinearColor(0.28f, 0.54f, 0.64f, 1) * Tint, 1);
	}
	return Layer + 3;
}

void UMHGZAttributeBarWidget::UnbindFromASC()
{
	if (BoundASC)
	{
		if (CurrentAttributeHandle.IsValid() && CurrentAttribute.IsValid())
		{
			BoundASC->GetGameplayAttributeValueChangeDelegate(CurrentAttribute).Remove(CurrentAttributeHandle);
		}
		if (MaxAttributeHandle.IsValid() && MaxAttribute.IsValid())
		{
			BoundASC->GetGameplayAttributeValueChangeDelegate(MaxAttribute).Remove(MaxAttributeHandle);
		}
	}
	CurrentAttributeHandle.Reset();
	MaxAttributeHandle.Reset();
	Super::UnbindFromASC();
	OnValueUpdated(0.0f, 0.0f);
}

void UMHGZAttributeBarWidget::HandleAttributeChanged(const FOnAttributeChangeData& ChangeData)
{
	PublishCurrentValues();
}

void UMHGZAttributeBarWidget::PublishCurrentValues()
{
	if (!BoundASC || !CurrentAttribute.IsValid() || !MaxAttribute.IsValid())
	{
		OnValueUpdated(0.0f, 0.0f);
		return;
	}
	OnValueUpdated(BoundASC->GetNumericAttribute(CurrentAttribute),
		BoundASC->GetNumericAttribute(MaxAttribute));
}
