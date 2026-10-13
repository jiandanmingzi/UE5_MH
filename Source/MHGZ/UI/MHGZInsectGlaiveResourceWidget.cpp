// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZInsectGlaiveResourceWidget.h"
#include "AttributeSystem/Res_InsectGlaive.h"
#include "Blueprint/WidgetTree.h"
#include "MHGZHUDDrawing.h"

void UMHGZInsectGlaiveResourceWidget::NativeConstruct()
{
	Super::NativeConstruct();
	// The persistent WBP keeps its named BP controls for compatibility. The
	// native compact icon panel replaces the old rectangular text dashboard.
	if (WidgetTree)
	{
		WidgetTree->ForEachWidget([this](UWidget* Widget)
		{
			if (Widget != WidgetTree->RootWidget) { Widget->SetVisibility(ESlateVisibility::Hidden); }
		});
	}
	if (URes_InsectGlaive* Resource = BoundInsectGlaiveResource.Get())
	{
		OnResourceUIUpdated(Resource->GetUIState());
	}
}

void UMHGZInsectGlaiveResourceWidget::BindToResourceComponent(UMHGZWeaponResourceComponent* InRC)
{
	UnbindFromResourceComponent();
	Super::BindToResourceComponent(InRC);
	URes_InsectGlaive* IGResource = Cast<URes_InsectGlaive>(InRC);
	if (!IGResource)
	{
		OnResourceUIUpdated(FIGResourceUISnapshot());
		return;
	}
	BoundInsectGlaiveResource = IGResource;
	ResourceUIHandle = IGResource->OnIGResourceUIChanged().AddUObject(
		this, &UMHGZInsectGlaiveResourceWidget::HandleResourceUIChanged);
	OnResourceUIUpdated(IGResource->GetUIState());
}

void UMHGZInsectGlaiveResourceWidget::UnbindFromResourceComponent()
{
	if (URes_InsectGlaive* Resource = BoundInsectGlaiveResource.Get();
		Resource && ResourceUIHandle.IsValid())
	{
		Resource->OnIGResourceUIChanged().Remove(ResourceUIHandle);
	}
	BoundInsectGlaiveResource.Reset();
	ResourceUIHandle.Reset();
	Super::UnbindFromResourceComponent();
	OnResourceUIUpdated(FIGResourceUISnapshot());
}

void UMHGZInsectGlaiveResourceWidget::HandleResourceUIChanged(const FIGResourceUISnapshot& State)
{
	OnResourceUIUpdated(State);
}

void UMHGZInsectGlaiveResourceWidget::OnResourceUIUpdated_Implementation(const FIGResourceUISnapshot& State)
{
	DisplayState = State;
	InvalidateLayoutAndVolatility();
}

TArray<FGameplayTag> UMHGZInsectGlaiveResourceWidget::GetDisplayedCargoColors(
	const FIGResourceUISnapshot& State)
{
	TArray<FGameplayTag> Colors;
	for (const TCHAR* Name : {TEXT("WeaponResource.IG.Extract.Red"),
		TEXT("WeaponResource.IG.Extract.White"), TEXT("WeaponResource.IG.Extract.Orange")})
	{
		const FGameplayTag Color = FGameplayTag::RequestGameplayTag(Name);
		if (State.KinsectPendingExtractColors.HasTagExact(Color)
			|| (State.KinsectPendingExtractColors.IsEmpty()
				&& State.KinsectPendingExtractColor == Color))
		{
			Colors.Add(Color);
		}
	}
	return Colors;
}

int32 UMHGZInsectGlaiveResourceWidget::NativePaint(const FPaintArgs& Args,
	const FGeometry& Geometry, const FSlateRect& CullingRect,
	FSlateWindowElementList& Out, int32 LayerId,
	const FWidgetStyle& Style, bool bParentEnabled) const
{
	using namespace MHGZHUDDrawing;
	const int32 L = Super::NativePaint(Args, Geometry, CullingRect,
		Out, LayerId, Style, bParentEnabled) + 1;
	const FLinearColor Tint = Style.GetColorAndOpacityTint() * GetColorAndOpacity();
	const FLinearColor Dark(0.023f, 0.025f, 0.029f, 0.96f);
	const FLinearColor Pale = FLinearColor(0.64f, 0.67f, 0.66f, 1) * Tint;
	const FLinearColor ExtractColors[] = {
		FLinearColor(0.8f, 0.085f, 0.055f, 1) * Tint,
		FLinearColor(0.86f, 0.91f, 0.92f, 1) * Tint,
		FLinearColor(0.98f, 0.36f, 0.07f, 1) * Tint};
	// TripleUp replaces the single-extract GEs; it must still light all three icons.
	const bool Active[] = {DisplayState.bRed || DisplayState.bTripleUp,
		DisplayState.bWhite || DisplayState.bTripleUp, DisplayState.bOrange || DisplayState.bTripleUp};

	// Small beetle silhouette: body, wings, antennae and legs; no font glyph
	// dependency. Its carried-extract dot is independent of delivered buffs.
	const FLinearColor InsectColor = DisplayState.bHasKinsect ? Pale : Pale * 0.45f;
	Polygon(Out, L, Geometry, {FVector2D(9, 22), FVector2D(20, 15),
		FVector2D(20, 32), FVector2D(11, 34)}, InsectColor);
	Polygon(Out, L, Geometry, {FVector2D(24, 15), FVector2D(35, 22),
		FVector2D(33, 34), FVector2D(24, 32)}, InsectColor);
	Polygon(Out, L + 1, Geometry, Diamond(FVector2D(22, 15), 6), InsectColor);
	Line(Out, L + 2, Geometry, {FVector2D(22, 18), FVector2D(22, 34)}, Dark, 2);
	for (const double Sign : {-1.0, 1.0})
	{
		Line(Out, L, Geometry, {FVector2D(22 + Sign * 4, 11),
			FVector2D(22 + Sign * 8, 7)}, InsectColor, 1.5f);
		for (int32 I = 0; I < 3; ++I)
		{
			Line(Out, L, Geometry, {FVector2D(22 + Sign * 10, 22 + I * 5),
				FVector2D(22 + Sign * 15, 25 + I * 5)}, InsectColor, 1.5f);
		}
	}
	const TArray<FGameplayTag> CargoColors = GetDisplayedCargoColors(DisplayState);
	if (!CargoColors.IsEmpty())
	{
		const FVector2D Center(22, 25), Radii(10, 17);
		for (int32 I = 0; I < CargoColors.Num(); ++I)
		{
			TArray<FVector2D> Sector;
			if (CargoColors.Num() > 1) { Sector.Add(Center); }
			for (int32 J = 0; J <= 48; ++J)
			{
				const double Angle = -PI * 0.5 + 2 * PI * (I + J / 48.0) / CargoColors.Num();
				Sector.Add(Center + FVector2D(FMath::Cos(Angle) * Radii.X,
					FMath::Sin(Angle) * Radii.Y));
			}
			const FString Name = CargoColors[I].ToString();
			const int32 Index = Name.EndsWith(TEXT("Red")) ? 0 : Name.EndsWith(TEXT("White")) ? 1 : 2;
			Polygon(Out, L + 3, Geometry, Sector, ExtractColors[Index]);
		}
		TArray<FVector2D> Rim;
		for (int32 I = 0; I < 48; ++I)
		{
			const double Angle = 2 * PI * I / 48;
			Rim.Add(Center + FVector2D(FMath::Cos(Angle) * Radii.X, FMath::Sin(Angle) * Radii.Y));
		}
		Outline(Out, L + 4, Geometry, Rim, Pale, 1);
	}
	for (int32 I = 0; I < 3; ++I)
	{
		const FVector2D Center(66 + I * 39, 24);
		const FLinearColor Color = Active[I] ? ExtractColors[I] : Pale * 0.45f;
		Polygon(Out, L, Geometry, Diamond(Center, 20), Dark);
		Outline(Out, L + 1, Geometry, Diamond(Center, 18), Pale * 0.52f, 2);
		if (Active[I])
		{
			Polygon(Out, L + 2, Geometry, Diamond(Center, 14), Color * 0.45f);
			Outline(Out, L + 3, Geometry, Diamond(Center, 14), Color, 2);
		}
		Text(Out, L + 4, Geometry, Center + FVector2D(-5, -10),
			FString::FromInt(I + 1), 13, Active[I] ? FLinearColor::White : Pale * 0.65f);
	}
	const float StaminaRatio = DisplayState.MaxKinsectStamina > UE_SMALL_NUMBER
		? DisplayState.KinsectStamina / DisplayState.MaxKinsectStamina : 0;
	Gauge(Out, L + 1, Geometry, FVector2D(46, 47), 120, 8, StaminaRatio,
		FLinearColor(0.95f, 0.58f, 0.11f, 1) * Tint);
	return L + 4;
}
