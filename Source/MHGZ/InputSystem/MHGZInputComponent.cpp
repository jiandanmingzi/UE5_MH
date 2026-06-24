// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"

UMHGZInputComponent::UMHGZInputComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UMHGZInputComponent::BeginPlay()
{
	Super::BeginPlay();

	APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (!PC || !PC->IsLocalPlayerController()) return;

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
	{
		for (UInputMappingContext* IMC : DefaultIMCs)
		{
			Subsystem->AddMappingContext(IMC, 0);
		}
	}
}

void UMHGZInputComponent::PushIMC(UInputMappingContext* IMC, int32 Priority)
{
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (!PC || !PC->IsLocalPlayerController()) return;

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
	{
		Subsystem->AddMappingContext(IMC, Priority);
	}
}

void UMHGZInputComponent::PopIMC(UInputMappingContext* IMC)
{
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (!PC || !PC->IsLocalPlayerController()) return;

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
	{
		Subsystem->RemoveMappingContext(IMC);
	}
}
