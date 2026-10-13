// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZDamageNumberActor.h"

#include "Components/WidgetComponent.h"
#include "MHGZPlayerController.h"
#include "UI/MHGZDamageNumberWidget.h"

AMHGZDamageNumberActor::AMHGZDamageNumberActor()
{
	PrimaryActorTick.bCanEverTick = true;
	DamageWidgetComponent = CreateDefaultSubobject<UWidgetComponent>(
		TEXT("DamageNumberWidget"));
	SetRootComponent(DamageWidgetComponent);
	DamageWidgetComponent->SetWidgetSpace(EWidgetSpace::Screen);
	DamageWidgetComponent->SetDrawSize(FVector2D(180.0f, 72.0f));
	DamageWidgetComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DamageWidgetComponent->SetGenerateOverlapEvents(false);
}

void AMHGZDamageNumberActor::BeginPlay()
{
	Super::BeginPlay();
	GetWorldTimerManager().SetTimer(LifetimeTimerHandle, this,
		&AMHGZDamageNumberActor::HandleLifetimeExpired,
		FMath::Max(0.05f, DisplayLifetime), false);
}

void AMHGZDamageNumberActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bLifetimeExpired && EndPlayReason == EEndPlayReason::Destroyed)
	{
		if (AMHGZPlayerController* Controller = TelemetryController.Get())
		{
			Controller->RecordUITelemetryEvent(TEXT("DamageNumberExpire"), this,
				TEXT("ActorDestroyed"), TelemetryRuntimeGeneration);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void AMHGZDamageNumberActor::HandleLifetimeExpired()
{
	bLifetimeExpired = true;
	Destroy();
}

void AMHGZDamageNumberActor::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Lifetime = FMath::Max(0.05f, DisplayLifetime);
	const float FadeDuration = FMath::Clamp(FadeOutDuration, 0.01f, Lifetime);
	const float FadeStart = Lifetime - FadeDuration;
	const float PreviousFadeAge = FMath::Max(0.0f, DisplayAge - FadeStart);
	DisplayAge = FMath::Min(Lifetime, DisplayAge + FMath::Max(0.0f, DeltaSeconds));
	const float FadeAge = FMath::Max(0.0f, DisplayAge - FadeStart);
	// Only integrate the part of a frame after FadeStart, including frames crossing it.
	AddActorWorldOffset(FVector::UpVector * RiseSpeed * (FadeAge - PreviousFadeAge), false);
	if (UMHGZDamageNumberWidget* Widget = GetDamageNumberWidget())
	{
		Widget->SetRenderOpacity(1.0f - FadeAge / FadeDuration);
	}
}

UMHGZDamageNumberWidget* AMHGZDamageNumberActor::GetDamageNumberWidget() const
{
	return DamageWidgetComponent
		? Cast<UMHGZDamageNumberWidget>(DamageWidgetComponent->GetUserWidgetObject()) : nullptr;
}

bool AMHGZDamageNumberActor::InitializeDamageNumber(
	const float ActualDamage, const bool bCritical,
	AMHGZPlayerController* InTelemetryController, const uint64 RuntimeGeneration)
{
	TelemetryController = InTelemetryController;
	TelemetryRuntimeGeneration = RuntimeGeneration;
	if (!DamageWidgetComponent || !DamageWidgetClass || !FMath::IsFinite(ActualDamage)
		|| ActualDamage <= 0.0f)
	{
		return false;
	}
	if (DamageWidgetClass)
	{
		DamageWidgetComponent->SetWidgetClass(DamageWidgetClass);
	}
	DamageWidgetComponent->InitWidget();
	if (UMHGZDamageNumberWidget* Widget = GetDamageNumberWidget())
	{
		Widget->SetDamageDisplay(ActualDamage, bCritical);
		return !Widget->GetDisplayedText().IsEmpty();
	}
	return false;
}
