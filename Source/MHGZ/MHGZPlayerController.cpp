// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZPlayerController.h"
#include "Blueprint/UserWidget.h"
#include "Widgets/Input/SVirtualJoystick.h"
#include "InputSystem/MHGZInputComponent.h"
#include "InputSystem/MHGZWeaponInputRouterComponent.h"
#include "InputSystem/MHGZQuickBarComponent.h"
#include "MHGZ.h"

AMHGZPlayerController::AMHGZPlayerController()
{
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
