// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZCrosshairWidget.h"

#include "AbilitySystemComponent.h"
#include "MHGZHUDDrawing.h"
#include "UI/MHGZAimComponent.h"

namespace
{
const FGameplayTag& ActionAimingTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Aiming.Action"), false);
	return Tag;
}

const FGameplayTag& KinsectAimingTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Aiming.Kinsect"), false);
	return Tag;
}
}

void UMHGZCrosshairWidget::BindToASC(UAbilitySystemComponent* InASC)
{
	if (BoundASC == InASC && ActionAimTagHandle.IsValid()
		&& KinsectAimTagHandle.IsValid())
	{
		return;
	}
	UnbindFromASC();
	BoundASC = InASC;
	if (InASC)
	{
		if (ActionAimingTag().IsValid())
		{
			ActionAimTagHandle = InASC->RegisterGameplayTagEvent(
				ActionAimingTag(), EGameplayTagEventType::NewOrRemoved)
				.AddUObject(this, &UMHGZCrosshairWidget::HandleAimingActionTagChanged);
			bActionAimActive = InASC->GetTagCount(ActionAimingTag()) > 0;
		}
		if (KinsectAimingTag().IsValid())
		{
			KinsectAimTagHandle = InASC->RegisterGameplayTagEvent(
				KinsectAimingTag(), EGameplayTagEventType::NewOrRemoved)
				.AddUObject(this, &UMHGZCrosshairWidget::HandleAimingKinsectTagChanged);
			bKinsectAimActive = InASC->GetTagCount(KinsectAimingTag()) > 0;
		}
	}
	UE_LOG(LogTemp, Display,
		TEXT("[E6.3Crosshair] ASC bound=%s ActionTagValid=%d ActionCount=%d "
			"KinsectTagValid=%d KinsectCount=%d"),
		*GetNameSafe(InASC), ActionAimingTag().IsValid(),
		InASC ? InASC->GetTagCount(ActionAimingTag()) : 0,
		KinsectAimingTag().IsValid(),
		InASC ? InASC->GetTagCount(KinsectAimingTag()) : 0);
	RefreshAimDisplay();
}

void UMHGZCrosshairWidget::UnbindFromASC()
{
	if (BoundASC)
	{
		if (ActionAimTagHandle.IsValid() && ActionAimingTag().IsValid())
		{
			BoundASC->UnregisterGameplayTagEvent(ActionAimTagHandle,
				ActionAimingTag(), EGameplayTagEventType::NewOrRemoved);
		}
		if (KinsectAimTagHandle.IsValid() && KinsectAimingTag().IsValid())
		{
			BoundASC->UnregisterGameplayTagEvent(KinsectAimTagHandle,
				KinsectAimingTag(), EGameplayTagEventType::NewOrRemoved);
		}
	}
	ActionAimTagHandle.Reset();
	KinsectAimTagHandle.Reset();
	bActionAimActive = false;
	bKinsectAimActive = false;
	Super::UnbindFromASC();
	RefreshAimDisplay();
}

void UMHGZCrosshairWidget::BindToAimComponent(UMHGZAimComponent* InAimComponent)
{
	if (BoundAimComponent.Get() == InAimComponent)
	{
		RefreshAimDisplay();
		return;
	}
	UnbindFromAimComponent();
	BoundAimComponent = InAimComponent;
	if (InAimComponent)
	{
		InAimComponent->OnAimTargetChanged.AddUniqueDynamic(
			this, &UMHGZCrosshairWidget::HandleAimTargetChanged);
	}
	RefreshAimDisplay();
}

void UMHGZCrosshairWidget::UnbindFromAimComponent()
{
	if (UMHGZAimComponent* Aim = BoundAimComponent.Get())
	{
		Aim->OnAimTargetChanged.RemoveDynamic(
			this, &UMHGZCrosshairWidget::HandleAimTargetChanged);
	}
	BoundAimComponent.Reset();
	RefreshAimDisplay();
}

void UMHGZCrosshairWidget::NativeDestruct()
{
	UnbindFromAimComponent();
	Super::NativeDestruct();
}

void UMHGZCrosshairWidget::HandleAimingActionTagChanged(
	const FGameplayTag Tag, const int32 NewCount)
{
	(void)Tag;
	bActionAimActive = NewCount > 0;
	UE_LOG(LogTemp, Display, TEXT("[E6.3Crosshair] Action aim tag count=%d"),
		NewCount);
	RefreshAimDisplay();
}

void UMHGZCrosshairWidget::HandleAimingKinsectTagChanged(
	const FGameplayTag Tag, const int32 NewCount)
{
	(void)Tag;
	bKinsectAimActive = NewCount > 0;
	UE_LOG(LogTemp, Display, TEXT("[E6.3Crosshair] Kinsect aim tag count=%d"),
		NewCount);
	RefreshAimDisplay();
}

void UMHGZCrosshairWidget::HandleAimTargetChanged(AActor* Target,
	FGameplayTag HitzoneTag, FGameplayTag ExtractColor)
{
	if (bKinsectAimActive || bActionAimActive)
	{
		PublishAimDisplayState(Target, HitzoneTag, ExtractColor);
	}
}

EMHGZAimDisplayMode UMHGZCrosshairWidget::GetAimDisplayMode() const
{
	// LT/Kinsect is the more specific target mode when both aim modifiers are held.
	if (bKinsectAimActive)
	{
		return EMHGZAimDisplayMode::Kinsect;
	}
	return bActionAimActive
		? EMHGZAimDisplayMode::Action : EMHGZAimDisplayMode::None;
}

void UMHGZCrosshairWidget::PublishAimDisplayState(AActor* Target,
	FGameplayTag HitzoneTag, FGameplayTag ExtractColor)
{
	OnAimTargetUpdated(Target, HitzoneTag, ExtractColor);
	OnAimDisplayStateUpdated(GetAimDisplayMode(), Target,
		HitzoneTag, ExtractColor);
}

void UMHGZCrosshairWidget::RefreshAimDisplay()
{
	const bool bVisible = bActionAimActive || bKinsectAimActive;
	const EMHGZAimDisplayMode DisplayMode = GetAimDisplayMode();
	UE_LOG(LogTemp, Display,
		TEXT("[E6.3Crosshair] Refresh visible=%d mode=%d aim=%s"),
		bVisible, static_cast<int32>(DisplayMode),
		*GetNameSafe(BoundAimComponent.Get()));
	OnAimVisibilityUpdated(bVisible);
	if (bVisible)
	{
		if (const UMHGZAimComponent* Aim = BoundAimComponent.Get())
		{
			PublishAimDisplayState(Aim->GetCurrentAimTarget(),
				Aim->GetCurrentAimHitzoneTag(), Aim->GetCurrentAimExtractColor());
			return;
		}
	}
	PublishAimDisplayState(nullptr, FGameplayTag(), FGameplayTag());
}

void UMHGZCrosshairWidget::OnAimTargetUpdated_Implementation(AActor* Target,
	FGameplayTag HitzoneTag, FGameplayTag ExtractColor)
{
	// 子类在蓝图中覆写——实现准心变色+缩放动画
}

void UMHGZCrosshairWidget::OnAimVisibilityUpdated_Implementation(
	const bool bVisible)
{
	bAimDisplayVisible = bVisible;
	SetVisibility(bVisible
		? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	InvalidateLayoutAndVolatility();
}

void UMHGZCrosshairWidget::OnAimDisplayStateUpdated_Implementation(
	const EMHGZAimDisplayMode Mode, AActor* Target,
	FGameplayTag HitzoneTag, FGameplayTag ExtractColor)
{
	(void)HitzoneTag;
	CurrentAimDisplayMode = Mode;
	bAimHasTarget = IsValid(Target);
	CurrentAimExtractColor = ExtractColor;
	bAimDisplayVisible = Mode != EMHGZAimDisplayMode::None;
	SetVisibility(bAimDisplayVisible
		? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	InvalidateLayoutAndVolatility();
}

int32 UMHGZCrosshairWidget::NativePaint(const FPaintArgs& Args,
	const FGeometry& Geometry, const FSlateRect& CullingRect,
	FSlateWindowElementList& Out, int32 LayerId,
	const FWidgetStyle& Style, bool bParentEnabled) const
{
	using namespace MHGZHUDDrawing;
	const int32 L = Super::NativePaint(Args, Geometry, CullingRect,
		Out, LayerId, Style, bParentEnabled) + 1;
	if (!bAimDisplayVisible || CurrentAimDisplayMode == EMHGZAimDisplayMode::None)
	{
		return L - 1;
	}
	const FVector2D C = Geometry.GetLocalSize() * 0.5;
	const bool bKinsect = CurrentAimDisplayMode == EMHGZAimDisplayMode::Kinsect;
	const FLinearColor Color = (bAimHasTarget
		? (bKinsect ? FLinearColor(1, 0.72f, 0.09f, 1) : FLinearColor(1, 0.35f, 0.045f, 1))
		: FLinearColor(0.77f, 0.79f, 0.77f, 0.9f))
		* Style.GetColorAndOpacityTint() * GetColorAndOpacity();
	const FLinearColor Shadow(0.01f, 0.01f, 0.01f, 0.55f);
	if (bKinsect && !bAimHasTarget)
	{
		// Four separated slanted segments form a broken diamond, not a '+'.
		for (const FVector2D Sign : {FVector2D(-1,-1), FVector2D(1,-1),
			FVector2D(1,1), FVector2D(-1,1)})
		{
			const FVector2D A = C + FVector2D(Sign.X * 3, Sign.Y * 15);
			const FVector2D B = C + FVector2D(Sign.X * 15, Sign.Y * 3);
			Line(Out, L, Geometry, {A, B}, Shadow, 4);
			Line(Out, L + 1, Geometry, {A, B}, Color, 1.5f);
		}
	}
	else
	{
		if (bKinsect)
		{
			Outline(Out, L, Geometry, Diamond(C, 21), Shadow, 3.5f);
			Outline(Out, L + 1, Geometry, Diamond(C, 21), Color, 1.3f);
			for (const FVector2D Direction : {FVector2D(0,-1), FVector2D(1,0),
				FVector2D(0,1), FVector2D(-1,0)})
			{
				const FVector2D Perp(-Direction.Y, Direction.X);
				Polygon(Out, L + 2, Geometry, {C + Direction * 26,
					C + Direction * 37 + Perp * 2.5, C + Direction * 37 - Perp * 2.5}, Color);
			}
		}
		else
		{
			// RT remains circular in both target states.
			Circle(Out, L, Geometry, C, 11, Shadow, 3.5f);
			Circle(Out, L + 1, Geometry, C, 11, Color, 1.3f);
			for (const FVector2D Sign : {FVector2D(-1,-1), FVector2D(1,-1),
				FVector2D(1,1), FVector2D(-1,1)})
			{
				const FVector2D D = Sign.GetSafeNormal();
				const FVector2D P(-D.Y, D.X);
				Polygon(Out, L + 2, Geometry, {C + D * 17,
					C + D * 25 + P * 4, C + D * 25 - P * 4}, Color);
			}
		}
		Circle(Out, L + 2, Geometry, C, 0.8, Color, 1.5f);
	}
	return L + 2;
}
