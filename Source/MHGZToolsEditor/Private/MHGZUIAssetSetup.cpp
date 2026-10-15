// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZEditorToolImplementations.h"

#if WITH_EDITOR

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Slate/SlateBrushAsset.h"
#include "WidgetBlueprint.h"

namespace
{
UCanvasPanelSlot* PlaceOnCanvas(UCanvasPanel* Canvas, UWidget* Widget,
	const FVector2D Position, const FVector2D Size, const int32 ZOrder = 0,
	const bool bFill = false)
{
	if (!Canvas || !Widget)
	{
		return nullptr;
	}
	UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Widget);
	if (!Slot)
	{
		return nullptr;
	}
	Slot->SetZOrder(ZOrder);
	if (bFill)
	{
		Slot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
		Slot->SetOffsets(FMargin(0.0f));
	}
	else
	{
		Slot->SetAnchors(FAnchors(0.0f, 0.0f));
		Slot->SetPosition(Position);
		Slot->SetSize(Size);
	}
	return Slot;
}

template <typename WidgetType>
WidgetType* MakeWidget(UWidgetTree* Tree, const TCHAR* Name,
	const bool bIsVariable = false)
{
	if (!Tree)
	{
		return nullptr;
	}
	WidgetType* Widget = Tree->ConstructWidget<WidgetType>(
		WidgetType::StaticClass(), FName(Name));
	if (Widget)
	{
		Widget->bIsVariable = bIsVariable;
		Widget->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	return Widget;
}

bool AddBackdrop(UWidgetTree* Tree, UCanvasPanel* Canvas,
	const FVector2D Size, const TCHAR* Name = TEXT("PanelBackdrop"))
{
	UBorder* Backdrop = MakeWidget<UBorder>(Tree, Name);
	if (!Backdrop)
	{
		return false;
	}
	Backdrop->SetBrushColor(FLinearColor(0.012f, 0.024f, 0.034f, 0.80f));
	return PlaceOnCanvas(Canvas, Backdrop, FVector2D::ZeroVector, Size) != nullptr;
}

bool AddProgress(UWidgetTree* Tree, UCanvasPanel* Canvas, const TCHAR* Name,
	const FVector2D Position, const FVector2D Size, const FLinearColor Color,
	const bool bIsVariable = false)
{
	UProgressBar* Bar = MakeWidget<UProgressBar>(Tree, Name, bIsVariable);
	if (!Bar)
	{
		return false;
	}
	Bar->SetPercent(0.0f);
	Bar->SetFillColorAndOpacity(Color);
	return PlaceOnCanvas(Canvas, Bar, Position, Size, 1) != nullptr;
}

bool AddText(UWidgetTree* Tree, UCanvasPanel* Canvas, const TCHAR* Name,
	const TCHAR* Value, const FVector2D Position, const FVector2D Size,
	const int32 ZOrder = 2, const bool bIsVariable = false,
	const FLinearColor Color = FLinearColor(0.90f, 0.94f, 0.96f, 1.0f))
{
	UTextBlock* Text = MakeWidget<UTextBlock>(Tree, Name, bIsVariable);
	if (!Text)
	{
		return false;
	}
	Text->SetText(FText::FromString(Value));
	Text->SetColorAndOpacity(FSlateColor(Color));
	FSlateFontInfo Font = Text->GetFont();
	Font.Size = 12;
	Text->SetFont(Font);
	return PlaceOnCanvas(Canvas, Text, Position, Size, ZOrder) != nullptr;
}

UCanvasPanel* SetRootCanvas(UWidgetBlueprint* Blueprint)
{
	if (!Blueprint || !Blueprint->WidgetTree || Blueprint->WidgetTree->RootWidget)
	{
		return nullptr;
	}
	UCanvasPanel* Root = MakeWidget<UCanvasPanel>(Blueprint->WidgetTree,
		TEXT("RootCanvas"));
	if (!Root)
	{
		return nullptr;
	}
	Blueprint->WidgetTree->RootWidget = Root;
	return Root;
}

bool BuildMainHUD(UWidgetBlueprint* Blueprint, UCanvasPanel* Root)
{
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	struct FSlotSpec
	{
		const TCHAR* Name;
		FVector2D Position;
		FVector2D Size;
		int32 ZOrder;
		bool bFill;
	};
	const FSlotSpec Slots[] = {
		{ TEXT("HealthBarSlot"), FVector2D(68.0f, 48.0f), FVector2D(400.0f, 28.0f), 1, false },
		{ TEXT("StaminaBarSlot"), FVector2D(68.0f, 82.0f), FVector2D(340.0f, 23.0f), 1, false },
		{ TEXT("WeaponResourceSlot"), FVector2D(68.0f, 111.0f), FVector2D(470.0f, 92.0f), 1, false },
		{ TEXT("CrosshairSlot"), FVector2D::ZeroVector, FVector2D::ZeroVector, 10, true },
	};
	for (const FSlotSpec& Spec : Slots)
	{
		UOverlay* Overlay = MakeWidget<UOverlay>(Blueprint->WidgetTree,
			Spec.Name, true);
		if (!Overlay || !PlaceOnCanvas(Root, Overlay, Spec.Position,
			Spec.Size, Spec.ZOrder, Spec.bFill))
		{
			return false;
		}
	}
	return true;
}

bool BuildAttributeBar(UWidgetBlueprint* Blueprint, UCanvasPanel* Root)
{
	if (!AddBackdrop(Blueprint->WidgetTree, Root, FVector2D(400.0f, 28.0f)))
	{
		return false;
	}
	const FLinearColor FillColor = Blueprint->GetName() == TEXT("WBP_HealthBar")
		? FLinearColor(0.22f, 0.78f, 0.36f, 1.0f)
		: FLinearColor(1.0f, 0.56f, 0.12f, 1.0f);
	return AddProgress(Blueprint->WidgetTree, Root, TEXT("ValueBar"),
		FVector2D(6.0f, 6.0f), FVector2D(388.0f, 16.0f), FillColor, true)
		&& AddText(Blueprint->WidgetTree, Root, TEXT("ValueText"), TEXT("0 / 0"),
			FVector2D(10.0f, 2.0f), FVector2D(380.0f, 23.0f), 2, true);
}

bool BuildCrosshair(UCanvasPanel* Root)
{
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	return true;
}

bool BuildInsectGlaivePanel(UWidgetBlueprint* Blueprint, UCanvasPanel* Root)
{
	UWidgetTree* Tree = Blueprint->WidgetTree;
	if (!AddBackdrop(Tree, Root, FVector2D(470.0f, 92.0f))
		|| !AddText(Tree, Root, TEXT("KinsectStateText"), TEXT("猎虫：待命"),
			FVector2D(10.0f, 4.0f), FVector2D(108.0f, 20.0f), 2, true)
		|| !AddProgress(Tree, Root, TEXT("KinsectStaminaBar"),
			FVector2D(120.0f, 7.0f), FVector2D(206.0f, 13.0f),
			FLinearColor(0.18f, 0.72f, 0.92f, 1.0f), true)
		|| !AddText(Tree, Root, TEXT("KinsectStaminaText"), TEXT("-- / --"),
			FVector2D(335.0f, 4.0f), FVector2D(104.0f, 20.0f), 2, true))
	{
		return false;
	}

	struct FExtractSpec
	{
		const TCHAR* BarName;
		const TCHAR* TextName;
		const TCHAR* Label;
		float X;
		FLinearColor Color;
	};
	const FExtractSpec Extracts[] = {
		{ TEXT("RedExtractBar"), TEXT("RedExtractText"), TEXT("RED  --"), 12.0f,
			FLinearColor(0.96f, 0.20f, 0.14f, 1.0f) },
		{ TEXT("WhiteExtractBar"), TEXT("WhiteExtractText"), TEXT("WHITE  --"), 164.0f,
			FLinearColor(0.90f, 0.95f, 1.0f, 1.0f) },
		{ TEXT("OrangeExtractBar"), TEXT("OrangeExtractText"), TEXT("ORANGE  --"), 316.0f,
			FLinearColor(1.0f, 0.54f, 0.10f, 1.0f) },
	};
	for (const FExtractSpec& Extract : Extracts)
	{
		if (!AddProgress(Tree, Root, Extract.BarName,
			FVector2D(Extract.X, 32.0f), FVector2D(132.0f, 10.0f),
			Extract.Color, true)
			|| !AddText(Tree, Root, Extract.TextName, Extract.Label,
				FVector2D(Extract.X, 45.0f), FVector2D(138.0f, 18.0f), 2, true,
				Extract.Color))
		{
			return false;
		}
	}

	UBorder* CarriedExtractIcon = MakeWidget<UBorder>(Tree,
		TEXT("KinsectPendingExtractIcon"), true);
	if (!CarriedExtractIcon)
	{
		return false;
	}
	CarriedExtractIcon->SetBrushColor(FLinearColor(0.82f, 0.87f, 0.90f, 1.0f));
	CarriedExtractIcon->SetVisibility(ESlateVisibility::Collapsed);
	if (!PlaceOnCanvas(Root, CarriedExtractIcon, FVector2D(128.0f, 71.0f),
		FVector2D(12.0f, 12.0f), 2)
		|| !AddText(Tree, Root, TEXT("TripleUpText"), TEXT("TRIPLE  --"),
			FVector2D(10.0f, 68.0f), FVector2D(112.0f, 19.0f), 2, true)
		|| !AddText(Tree, Root, TEXT("KinsectPendingExtractText"), TEXT(""),
			FVector2D(146.0f, 68.0f), FVector2D(112.0f, 19.0f), 2, true)
		|| !AddText(Tree, Root, TEXT("DanceStacksText"), TEXT("舞踏  0"),
			FVector2D(264.0f, 68.0f), FVector2D(84.0f, 19.0f), 2, true)
		|| !AddText(Tree, Root, TEXT("PowderCountText"), TEXT("虫粉  0"),
			FVector2D(354.0f, 68.0f), FVector2D(100.0f, 19.0f), 2, true))
	{
		return false;
	}
	return true;
}
}

#endif // WITH_EDITOR

bool UE::MHGZ::EditorTools::BuildE63WidgetBlueprint(
	UObject* WidgetBlueprintAsset, const FName LayoutName)
{
#if WITH_EDITOR
	UWidgetBlueprint* Blueprint = Cast<UWidgetBlueprint>(WidgetBlueprintAsset);
	UCanvasPanel* Root = SetRootCanvas(Blueprint);
	if (!Blueprint || !Root)
	{
		UE_LOG(LogTemp, Error, TEXT("E6.3 widget setup requires an empty Widget Blueprint tree."));
		return false;
	}

	bool bBuilt = false;
	if (LayoutName == TEXT("WBP_HUD"))
	{
		bBuilt = BuildMainHUD(Blueprint, Root);
	}
	else if (LayoutName == TEXT("WBP_HealthBar")
		|| LayoutName == TEXT("WBP_StaminaBar"))
	{
		bBuilt = BuildAttributeBar(Blueprint, Root);
	}
	else if (LayoutName == TEXT("WBP_Crosshair"))
	{
		bBuilt = BuildCrosshair(Root);
	}
	else if (LayoutName == TEXT("WBP_IG_ResourcePanel"))
	{
		bBuilt = BuildInsectGlaivePanel(Blueprint, Root);
	}

	if (!bBuilt)
	{
		UE_LOG(LogTemp, Error, TEXT("Unsupported or incomplete E6.3 layout: %s"),
			*LayoutName.ToString());
		return false;
	}

	Blueprint->MarkPackageDirty();
	return true;
#else
	(void)WidgetBlueprintAsset;
	(void)LayoutName;
	return false;
#endif
}
