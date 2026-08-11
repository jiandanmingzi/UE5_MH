// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZInputComponent.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputSystem/MHGZWeaponInputRouterComponent.h"
#include "MHGZCharacter.h"
#include "WeaponRuntime/MHGZWeaponInputProfile.h"

UMHGZInputComponent::UMHGZInputComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UMHGZInputComponent::BeginPlay()
{
	Super::BeginPlay();

	// Legacy path: components configured on the controller with DefaultIMCs are
	// still applied here. InitializeInput is idempotent with this via the per-IMC
	// add counts, and ShutdownInput removes every context exactly once.
	AddDefaultContexts();
}

void UMHGZInputComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ShutdownInput();
	Super::EndPlay(EndPlayReason);
}

void UMHGZInputComponent::InitializeInput(APlayerController* InPC, UMHGZWeaponInputRouterComponent* InRouter)
{
	if (!InPC)
	{
		return;
	}

	if (bInputInitialized)
	{
		if (OwnerPC.Get() == InPC && Router.Get() == InRouter)
		{
			// Already initialized with the same pair. If the controller's
			// InputComponent did not exist on the first call (e.g. called from
			// PossessedBy before InitInputSystem), (re)bind now. RefreshRawBindings
			// is idempotent and never stacks handles.
			RefreshRawBindings();
			return;
		}
		ShutdownInput();
	}

	OwnerPC = InPC;
	Router = InRouter;
	bInputInitialized = true;

	AddDefaultContexts();

	if (Router.IsValid())
	{
		OnRouterProfileChangedHandle = Router->OnInputProfileChanged.AddUObject(
			this, &UMHGZInputComponent::OnRouterProfileChanged);
	}

	if (Router.IsValid())
	{
		Router->AttachToPawn(InPC->GetPawn());
	}

	RefreshRawBindings();
}

void UMHGZInputComponent::RefreshRawBindings()
{
	UnbindAll();

	BoundInputComponent = GetEnhancedInputComponent();
	UEnhancedInputComponent* EIC = BoundInputComponent.Get();
	if (!EIC)
	{
		return;
	}

	APlayerController* PC = OwnerPC.Get();
	if (!PC)
	{
		return;
	}

	// Raw weapon actions -> router callbacks. Started/Triggered/Completed/Canceled
	// are all forwarded; the router decides what is discrete vs held-only.
	if (Router.IsValid())
	{
		if (UWeaponInputProfile* Profile = Router->GetInputProfile())
		{
			for (const TPair<TObjectPtr<UInputAction>, FGameplayTag>& Pair : Profile->RawActionToPhysicalInputTag)
			{
				const UInputAction* Action = Pair.Key.Get();
				if (!Action || !Pair.Value.IsValid())
				{
					continue;
				}

				RawBindingHandles.Add(EIC->BindAction(
					Action, ETriggerEvent::Started, Router.Get(),
					&UMHGZWeaponInputRouterComponent::HandleRawInputStarted).GetHandle());
				RawBindingHandles.Add(EIC->BindAction(
					Action, ETriggerEvent::Triggered, Router.Get(),
					&UMHGZWeaponInputRouterComponent::HandleRawInputTriggered).GetHandle());
				RawBindingHandles.Add(EIC->BindAction(
					Action, ETriggerEvent::Completed, Router.Get(),
					&UMHGZWeaponInputRouterComponent::HandleRawInputCompleted).GetHandle());
				RawBindingHandles.Add(EIC->BindAction(
					Action, ETriggerEvent::Canceled, Router.Get(),
					&UMHGZWeaponInputRouterComponent::HandleRawInputCompleted).GetHandle());
			}
		}
	}

	// Character locomotion bindings (Move/Look/MouseLook/Sprint). The character
	// appends its handles so this component can remove them all on shutdown.
	if (AMHGZCharacter* Character = Cast<AMHGZCharacter>(PC->GetPawn()))
	{
		Character->BindCharacterInput(EIC, CharacterBindingHandles);
	}
}

void UMHGZInputComponent::ShutdownInput()
{
	if (Router.IsValid() && OnRouterProfileChangedHandle.IsValid())
	{
		Router->OnInputProfileChanged.Remove(OnRouterProfileChangedHandle);
	}
	OnRouterProfileChangedHandle.Reset();

	UnbindAll();
	RemoveAllContexts();

	OwnerPC.Reset();
	Router.Reset();
	bInputInitialized = false;
}

void UMHGZInputComponent::PushIMC(UInputMappingContext* IMC, int32 Priority)
{
	if (!IMC)
	{
		return;
	}

	APlayerController* PC = OwnerPC.IsValid() ? OwnerPC.Get() : Cast<APlayerController>(GetOwner());
	if (!PC || !PC->IsLocalPlayerController())
	{
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
	{
		Subsystem->AddMappingContext(IMC, Priority);
		AddedContextCounts.FindOrAdd(IMC)++;
	}
}

void UMHGZInputComponent::PopIMC(UInputMappingContext* IMC)
{
	if (!IMC)
	{
		return;
	}

	APlayerController* PC = OwnerPC.IsValid() ? OwnerPC.Get() : Cast<APlayerController>(GetOwner());
	if (!PC || !PC->IsLocalPlayerController())
	{
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
	{
		Subsystem->RemoveMappingContext(IMC);
		if (int32* Count = AddedContextCounts.Find(IMC))
		{
			if (--(*Count) <= 0)
			{
				AddedContextCounts.Remove(IMC);
			}
		}
	}
}

void UMHGZInputComponent::OnRouterProfileChanged()
{
	if (bInputInitialized)
	{
		RefreshRawBindings();
	}
}

UEnhancedInputComponent* UMHGZInputComponent::GetEnhancedInputComponent() const
{
	APlayerController* PC = OwnerPC.Get();
	if (!PC || !PC->InputComponent)
	{
		return nullptr;
	}
	return Cast<UEnhancedInputComponent>(PC->InputComponent);
}

void UMHGZInputComponent::AddDefaultContexts()
{
	APlayerController* PC = OwnerPC.IsValid() ? OwnerPC.Get() : Cast<APlayerController>(GetOwner());
	if (!PC || !PC->IsLocalPlayerController())
	{
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
	{
		for (UInputMappingContext* IMC : DefaultIMCs)
		{
			if (!IMC || AddedContextCounts.Contains(IMC))
			{
				continue;
			}
			Subsystem->AddMappingContext(IMC, 0);
			AddedContextCounts.Add(IMC, 1);
		}
	}
}

void UMHGZInputComponent::RemoveAllContexts()
{
	APlayerController* PC = OwnerPC.IsValid() ? OwnerPC.Get() : Cast<APlayerController>(GetOwner());
	if (!PC || !PC->IsLocalPlayerController())
	{
		AddedContextCounts.Reset();
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
	{
		for (const TPair<TWeakObjectPtr<UInputMappingContext>, int32>& Pair : AddedContextCounts)
		{
			if (UInputMappingContext* IMC = Pair.Key.Get())
			{
				for (int32 Count = 0; Count < Pair.Value; ++Count)
				{
					Subsystem->RemoveMappingContext(IMC);
				}
			}
		}
	}
	AddedContextCounts.Reset();
}

void UMHGZInputComponent::UnbindAll()
{
	if (UEnhancedInputComponent* EIC = BoundInputComponent.Get())
	{
		for (const uint32 Handle : RawBindingHandles)
		{
			EIC->RemoveBindingByHandle(Handle);
		}
		for (const uint32 Handle : CharacterBindingHandles)
		{
			EIC->RemoveBindingByHandle(Handle);
		}
	}

	RawBindingHandles.Reset();
	CharacterBindingHandles.Reset();
	BoundInputComponent.Reset();
}
