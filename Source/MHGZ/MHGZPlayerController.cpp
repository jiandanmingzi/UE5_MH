// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZPlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "Blueprint/UserWidget.h"
#include "Widgets/Input/SVirtualJoystick.h"
#include "InputSystem/MHGZInputComponent.h"
#include "InputSystem/MHGZQuickBarComponent.h"
#include "MHGZ.h"

AMHGZPlayerController::AMHGZPlayerController()
{
	InputComponent_MHGZ = CreateDefaultSubobject<UMHGZInputComponent>(TEXT("MHGZInputComponent"));
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

	if (IsLocalPlayerController())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			for (UInputMappingContext* IMC : DefaultMappingContexts)
			{
				Subsystem->AddMappingContext(IMC, 0);
			}

			if (!SVirtualJoystick::ShouldDisplayTouchInterface())
			{
				for (UInputMappingContext* IMC : MobileExcludedMappingContexts)
				{
					Subsystem->AddMappingContext(IMC, 0);
				}
			}
		}
	}
}
