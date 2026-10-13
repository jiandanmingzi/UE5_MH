// Copyright MHGZ Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"

/** Display-only, DPI-aware geometry shared by the persistent HUD widget assets. */
namespace MHGZHUDDrawing
{
inline void Line(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geo,
	const TArray<FVector2D>& Points, const FLinearColor& Color, float Width = 1.0f)
{
	FSlateDrawElement::MakeLines(Out, Layer, Geo.ToPaintGeometry(), Points,
		ESlateDrawEffect::None, Color, true, Width);
}

// All callers supply convex polygons in local widget coordinates.
inline void Polygon(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geo,
	const TArray<FVector2D>& Points, const FLinearColor& Color)
{
	if (Points.Num() < 3 || !FSlateApplication::IsInitialized()) { return; }
	TArray<FSlateVertex> Vertices;
	TArray<SlateIndex> Indices;
	for (const FVector2D& Point : Points)
	{
		Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(
			Geo.GetAccumulatedRenderTransform(), FVector2f(Point),
			FVector2f(0.5f, 0.5f), Color.ToFColor(true)));
	}
	for (int32 I = 1; I + 1 < Points.Num(); ++I)
	{
		Indices.Append({0, static_cast<SlateIndex>(I), static_cast<SlateIndex>(I + 1)});
	}
	const FSlateResourceHandle Handle = FSlateApplication::Get().GetRenderer()
		->GetResourceHandle(*FCoreStyle::Get().GetBrush("WhiteBrush"));
	FSlateDrawElement::MakeCustomVerts(Out, Layer, Handle, Vertices, Indices,
		nullptr, 0, 0);
}

inline TArray<FVector2D> Diamond(FVector2D Center, double Radius)
{
	return {Center + FVector2D(0, -Radius), Center + FVector2D(Radius, 0),
		Center + FVector2D(0, Radius), Center + FVector2D(-Radius, 0)};
}

inline void Outline(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geo,
	TArray<FVector2D> Points, const FLinearColor& Color, float Width = 1.0f)
{
	if (Points.IsEmpty()) { return; }
	const FVector2D First = Points[0]; // Add may reallocate; never alias its array.
	Points.Add(First);
	Line(Out, Layer, Geo, Points, Color, Width);
}

inline void Circle(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geo,
	FVector2D Center, double Radius, const FLinearColor& Color, float Width = 1.0f)
{
	TArray<FVector2D> Points;
	for (int32 I = 0; I <= 48; ++I)
	{
		const double Angle = I * 2.0 * PI / 48.0;
		Points.Add(Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
	}
	Line(Out, Layer, Geo, Points, Color, Width);
}

inline void Text(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geo,
	FVector2D Position, const FString& Value, int32 Size, const FLinearColor& Color)
{
	FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Bold", Size);
	Font.OutlineSettings.OutlineSize = 1;
	Font.OutlineSettings.OutlineColor = FLinearColor(0.01f, 0.01f, 0.01f, 0.9f);
	FSlateDrawElement::MakeText(Out, Layer, Geo.ToPaintGeometry(
		FVector2D(1, 1), FSlateLayoutTransform(Position)), Value, Font,
		ESlateDrawEffect::None, Color);
}

inline void Gauge(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geo,
	FVector2D Position, double Width, double Height, float Ratio,
	const FLinearColor& Color)
{
	const double Bevel = Height * 0.5;
	const TArray<FVector2D> Frame = {Position + FVector2D(Bevel, 0),
		Position + FVector2D(Width - Bevel, 0), Position + FVector2D(Width, Bevel),
		Position + FVector2D(Width - Bevel, Height), Position + FVector2D(Bevel, Height),
		Position + FVector2D(0, Bevel)};
	Polygon(Out, Layer, Geo, Frame, FLinearColor(0.025f, 0.029f, 0.023f, 0.94f));
	Outline(Out, Layer + 1, Geo, Frame, FLinearColor(0.38f, 0.37f, 0.29f, 0.95f));
	const double FilledWidth = (Width - 6) * FMath::Clamp(Ratio, 0.0f, 1.0f);
	if (FilledWidth > 2)
	{
		const FVector2D P = Position + FVector2D(3, 2);
		const double H = Height - 4;
		const double B = FMath::Min(H * 0.5, FilledWidth * 0.5);
		Polygon(Out, Layer + 2, Geo, {P, P + FVector2D(FilledWidth - B, 0),
			P + FVector2D(FilledWidth, H * 0.5), P + FVector2D(FilledWidth - B, H),
			P + FVector2D(0, H)}, Color);
		Line(Out, Layer + 3, Geo, {P + FVector2D(1, 0.5),
			P + FVector2D(FilledWidth - B, 0.5)}, Color * 0.45f +
			FLinearColor(0.55f, 0.55f, 0.55f, 0.55f));
	}
}
}
