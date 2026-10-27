// Copyright MHGZ Project. All Rights Reserved.
#include "MHGZMainHUDWidget.h"
#include "MHGZHUDDrawing.h"

void UMHGZMainHUDWidget::SetRecentDPS(double Value)
{
	const double Sanitized = FMath::IsFinite(Value) ? FMath::Max(Value, 0.0) : 0.0;
	if (RecentDPS != Sanitized)
	{
		RecentDPS = Sanitized;
		InvalidateLayoutAndVolatility();
	}
}

int32 UMHGZMainHUDWidget::NativePaint(const FPaintArgs& Args, const FGeometry& Geometry,
	const FSlateRect& CullingRect, FSlateWindowElementList& Out, int32 LayerId,
	const FWidgetStyle& Style, bool bParentEnabled) const
{
	using namespace MHGZHUDDrawing;
	const int32 L = Super::NativePaint(Args, Geometry, CullingRect, Out, LayerId, Style, bParentEnabled) + 1;
	const FVector2D Size = Geometry.GetLocalSize();
	const FVector2D P(FMath::Max(0.0, Size.X - 220.0 - 24.0), FMath::Max(0.0, Size.Y * .5 - 40.0));
	const TArray<FVector2D> Frame = {P, P + FVector2D(212, 0), P + FVector2D(220, 8),
		P + FVector2D(220, 80), P + FVector2D(8, 80), P + FVector2D(0, 72)};
	const FLinearColor Tint = Style.GetColorAndOpacityTint() * GetColorAndOpacity();
	Polygon(Out, L, Geometry, Frame, FLinearColor(.018f, .022f, .026f, .8f) * Tint);
	Outline(Out, L + 1, Geometry, Frame, FLinearColor(.45f, .42f, .28f, .9f) * Tint);
	Text(Out, L + 2, Geometry, P + FVector2D(16, 8), TEXT("DPS (10s)"), 13,
		FLinearColor(.72f, .75f, .74f, 1) * Tint);
	Text(Out, L + 2, Geometry, P + FVector2D(16, 29), FString::Printf(TEXT("%.1f"), RecentDPS), 25,
		FLinearColor(1.f, .78f, .30f, 1) * Tint);
	return L + 2;
}
