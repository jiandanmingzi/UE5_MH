// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZPlayerController.h"
#include "MHGZPlayerCameraManager.h"
#include "Blueprint/UserWidget.h"
#include "Widgets/Input/SVirtualJoystick.h"
#include "InputSystem/MHGZInputComponent.h"
#include "InputSystem/MHGZWeaponInputRouterComponent.h"
#include "InputSystem/MHGZQuickBarComponent.h"
#include "HAL/IConsoleManager.h"
#include "MHGZHUD.h"
#include "MHGZ.h"

AMHGZPlayerController::AMHGZPlayerController()
{
	PlayerCameraManagerClass = AMHGZPlayerCameraManager::StaticClass();
	InputComponent_MHGZ = CreateDefaultSubobject<UMHGZInputComponent>(TEXT("MHGZInputComponent"));
	WeaponInputRouterComponent = CreateDefaultSubobject<UMHGZWeaponInputRouterComponent>(TEXT("WeaponInputRouter"));
	QuickBarComponent = CreateDefaultSubobject<UMHGZQuickBarComponent>(TEXT("QuickBarComponent"));
}

void AMHGZPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (SVirtualJoystick::ShouldDisplayTouchInterface() && IsLocalPlayerController())
	{
		MobileControlsWidget = CreateWidget<UUserWidget>(this, MobileControlsWidgetClass);
		if (MobileControlsWidget)
		{
			MobileControlsWidget->AddToPlayerScreen(0);
		}
		else
		{
			UE_LOG(LogMHGZ, Error, TEXT("Could not spawn mobile controls widget."));
		}
	}
	RefreshMHGZHUDContext();
}

void AMHGZPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	if (InputComponent_MHGZ && WeaponInputRouterComponent)
	{
		WeaponInputRouterComponent->AttachToPawn(GetPawn());
		InputComponent_MHGZ->InitializeInput(this, WeaponInputRouterComponent);
	}
}

void AMHGZPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	if (WeaponInputRouterComponent)
	{
		WeaponInputRouterComponent->AttachToPawn(InPawn);
	}
	if (InputComponent_MHGZ && WeaponInputRouterComponent)
	{
		InputComponent_MHGZ->InitializeInput(this, WeaponInputRouterComponent);
	}
	RefreshMHGZHUDContext();
}

void AMHGZPlayerController::OnUnPossess()
{
	if (InputComponent_MHGZ)
	{
		InputComponent_MHGZ->ShutdownInput();
	}
	if (WeaponInputRouterComponent)
	{
		WeaponInputRouterComponent->ShutdownRouter();
	}
	Super::OnUnPossess();
	RefreshMHGZHUDContext();
}

void AMHGZPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (InputComponent_MHGZ)
	{
		InputComponent_MHGZ->ShutdownInput();
	}
	if (WeaponInputRouterComponent)
	{
		WeaponInputRouterComponent->ShutdownRouter();
	}
	Super::EndPlay(EndPlayReason);
}

void AMHGZPlayerController::RefreshMHGZHUDContext()
{
	if (AMHGZHUD* MHGZHUD = Cast<AMHGZHUD>(GetHUD()))
	{
		MHGZHUD->RefreshPawnContext();
	}
}

void AMHGZPlayerController::RecordUITelemetryEvent(const FName EventName,
	const UObject* Subject, const FName Reason, const uint64 RuntimeGeneration)
{
	if (EventName.IsNone() || !IsLocalPlayerController())
	{
		return;
	}
	IConsoleVariable* CaptureVariable = IConsoleManager::Get().FindConsoleVariable(
		TEXT("mhgz.Telemetry.Enable"));
	if (!CaptureVariable || CaptureVariable->GetInt() == 0)
	{
		return;
	}

	FMHGZUIEventTelemetry Event;
	Event.Frame = GFrameCounter;
	Event.WorldTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	Event.EventName = EventName;
	Event.RuntimeGeneration = RuntimeGeneration;
	Event.Reason = Reason;
	Event.SubjectClass = IsValid(Subject) ? Subject->GetClass()->GetPathName() : FString();
	Event.PawnName = GetPawn() ? GetPawn()->GetFName() : NAME_None;
	UITelemetryEvents.Record(MoveTemp(Event));
}

void AMHGZPlayerController::GetUITelemetryEventsSince(uint64& InOutLastSerial,
	TArray<FMHGZUIEventTelemetry>& OutEvents) const
{
	UITelemetryEvents.GetEventsSince(InOutLastSerial, OutEvents);
}
