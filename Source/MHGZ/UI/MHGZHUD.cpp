// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZHUD.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "AttributeSystem/MHGZWeaponResourceComponent.h"
#include "Blueprint/UserWidget.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "GameFramework/PlayerController.h"
#include "MHGZCharacter.h"
#include "MHGZPlayerController.h"
#include "UI/MHGZAimComponent.h"
#include "UI/MHGZAttributeBarWidget.h"
#include "UI/MHGZCrosshairWidget.h"
#include "UI/MHGZMainHUDWidget.h"
#include "UI/MHGZWeaponResourceWidget.h"
#include "WeaponRuntime/MHGZWeaponRuntimeDefinition.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

namespace
{
UOverlay* ResolveOverlaySlot(UMHGZMainHUDWidget* MainWidget,
	UOverlay* BoundSlot, const FName SlotName)
{
	if (BoundSlot || !MainWidget)
	{
		return BoundSlot;
	}
	return Cast<UOverlay>(MainWidget->GetWidgetFromName(SlotName));
}
}

AMHGZHUD::AMHGZHUD()
{
}

void AMHGZHUD::BeginPlay()
{
	Super::BeginPlay();
	if (!IsValid(PlayerOwner) || !PlayerOwner->IsLocalPlayerController())
	{
		return;
	}
	PlayerOwner->OnPossessedPawnChanged.AddUniqueDynamic(
		this, &AMHGZHUD::HandlePossessedPawnChanged);
	if (AMHGZPlayerController* MHGZPlayerController =
		Cast<AMHGZPlayerController>(PlayerOwner))
	{
		MHGZPlayerController->RecordUITelemetryEvent(
			TEXT("HUDCreate"), this, TEXT("BeginPlay"));
	}

	if (MainHUDClass)
	{
		MainHUDWidget = CreateWidget<UMHGZMainHUDWidget>(PlayerOwner, MainHUDClass);
		if (MainHUDWidget)
		{
			MainHUDWidget->AddToViewport();
			// A stale/failed BindWidget compile must not silently suppress the
			// runtime UI. Resolve the authored named widgets as a safe fallback.
			MainHUDWidget->HealthBarSlot = ResolveOverlaySlot(MainHUDWidget,
				MainHUDWidget->HealthBarSlot, TEXT("HealthBarSlot"));
			MainHUDWidget->StaminaBarSlot = ResolveOverlaySlot(MainHUDWidget,
				MainHUDWidget->StaminaBarSlot, TEXT("StaminaBarSlot"));
			MainHUDWidget->CrosshairSlot = ResolveOverlaySlot(MainHUDWidget,
				MainHUDWidget->CrosshairSlot, TEXT("CrosshairSlot"));
			MainHUDWidget->WeaponResourceSlot = ResolveOverlaySlot(MainHUDWidget,
				MainHUDWidget->WeaponResourceSlot, TEXT("WeaponResourceSlot"));
			if (UCanvasPanelSlot* StaminaCanvasSlot = MainHUDWidget->StaminaBarSlot
				? Cast<UCanvasPanelSlot>(MainHUDWidget->StaminaBarSlot->Slot) : nullptr)
			{
				StaminaCanvasSlot->SetSize(FVector2D(620.0f, 18.0f));
				StaminaCanvasSlot->SetPosition(FVector2D(68.0f, 81.0f));
			}
			if (UCanvasPanelSlot* HealthCanvasSlot = MainHUDWidget->HealthBarSlot
				? Cast<UCanvasPanelSlot>(MainHUDWidget->HealthBarSlot->Slot) : nullptr)
			{
				HealthCanvasSlot->SetSize(FVector2D(620.0f, 30.0f));
			}
			if (UCanvasPanelSlot* ResourceCanvasSlot = MainHUDWidget->WeaponResourceSlot
				? Cast<UCanvasPanelSlot>(MainHUDWidget->WeaponResourceSlot->Slot) : nullptr)
			{
				ResourceCanvasSlot->SetSize(FVector2D(260.0f, 82.0f));
				ResourceCanvasSlot->SetPosition(FVector2D(105.0f, 108.0f));
			}
			UE_LOG(LogTemp, Display,
				TEXT("[E6.3HUD] PC=%s PawnAtCreate=%s Main=%s "
					"HealthClass=%s HealthSlot=%s StaminaClass=%s StaminaSlot=%s "
					"CrosshairSlot=%s ResourceSlot=%s CrosshairClass=%s"),
				*GetNameSafe(PlayerOwner),
				*GetNameSafe(PlayerOwner->GetPawn()),
				*GetNameSafe(MainHUDWidget),
				*GetNameSafe(HealthBarClass.Get()),
				*GetNameSafe(MainHUDWidget->HealthBarSlot),
				*GetNameSafe(StaminaBarClass.Get()),
				*GetNameSafe(MainHUDWidget->StaminaBarSlot),
				*GetNameSafe(MainHUDWidget->CrosshairSlot),
				*GetNameSafe(MainHUDWidget->WeaponResourceSlot),
				*GetNameSafe(CrosshairClass.Get()));
			if (UCanvasPanelSlot* CrosshairCanvasSlot = MainHUDWidget->CrosshairSlot
				? Cast<UCanvasPanelSlot>(MainHUDWidget->CrosshairSlot->Slot) : nullptr)
			{
				CrosshairCanvasSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
				CrosshairCanvasSlot->SetOffsets(FMargin(0.0f));
				CrosshairCanvasSlot->SetAlignment(FVector2D::ZeroVector);
				MainHUDWidget->CrosshairSlot->SetVisibility(
					ESlateVisibility::SelfHitTestInvisible);
			}
			if (HealthBarClass && MainHUDWidget->HealthBarSlot)
			{
				HealthBarWidget = CreateWidget<UMHGZAttributeBarWidget>(
					PlayerOwner, HealthBarClass);
				if (HealthBarWidget)
				{
					if (UOverlaySlot* BarChildSlot = Cast<UOverlaySlot>(
						MainHUDWidget->HealthBarSlot->AddChild(HealthBarWidget)))
					{
						BarChildSlot->SetHorizontalAlignment(HAlign_Fill);
						BarChildSlot->SetVerticalAlignment(VAlign_Fill);
					}
					UE_LOG(LogTemp, Display,
						TEXT("[E6.3HUD] Health widget created=%s visibility=%d"),
						*GetNameSafe(HealthBarWidget),
						static_cast<int32>(HealthBarWidget->GetVisibility()));
				}
			}
			if (StaminaBarClass && MainHUDWidget->StaminaBarSlot)
			{
				StaminaBarWidget = CreateWidget<UMHGZAttributeBarWidget>(
					PlayerOwner, StaminaBarClass);
				if (StaminaBarWidget)
				{
					if (UOverlaySlot* BarChildSlot = Cast<UOverlaySlot>(
						MainHUDWidget->StaminaBarSlot->AddChild(StaminaBarWidget)))
					{
						BarChildSlot->SetHorizontalAlignment(HAlign_Fill);
						BarChildSlot->SetVerticalAlignment(VAlign_Fill);
					}
					UE_LOG(LogTemp, Display,
						TEXT("[E6.3HUD] Stamina widget created=%s visibility=%d"),
						*GetNameSafe(StaminaBarWidget),
						static_cast<int32>(StaminaBarWidget->GetVisibility()));
				}
			}
			if (CrosshairClass && MainHUDWidget->CrosshairSlot)
			{
				CrosshairWidget = CreateWidget<UMHGZCrosshairWidget>(
					PlayerOwner, CrosshairClass);
				if (CrosshairWidget)
				{
					if (UOverlaySlot* CrosshairChildSlot = Cast<UOverlaySlot>(
						MainHUDWidget->CrosshairSlot->AddChild(CrosshairWidget)))
					{
						CrosshairChildSlot->SetHorizontalAlignment(HAlign_Fill);
						CrosshairChildSlot->SetVerticalAlignment(VAlign_Fill);
					}
					UE_LOG(LogTemp, Display,
						TEXT("[E6.3HUD] Crosshair widget created and attached: %s"),
						*GetNameSafe(CrosshairWidget));
				}
			}
			else
			{
				UE_LOG(LogTemp, Error,
					TEXT("[E6.3HUD] Crosshair not attached: class=%s slot=%s"),
					*GetNameSafe(CrosshairClass.Get()),
					*GetNameSafe(MainHUDWidget->CrosshairSlot));
			}
		}
	}

	bWidgetTreeReady = MainHUDWidget && HealthBarWidget && StaminaBarWidget
		&& CrosshairWidget && MainHUDWidget->WeaponResourceSlot;
	if (!bWidgetTreeReady)
	{
		UE_LOG(LogTemp, Error, TEXT("[E6.3HUD] Widget tree incomplete; cannot bind UI."));
		return;
	}
	UE_LOG(LogTemp, Display, TEXT("[E6.3HUD] Widget tree ready; binding current pawn."));
	RefreshPawnContext();
}

void AMHGZHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bWidgetTreeReady = false;
	if (IsValid(PlayerOwner))
	{
		PlayerOwner->OnPossessedPawnChanged.RemoveDynamic(
			this, &AMHGZHUD::HandlePossessedPawnChanged);
	}
	if (AMHGZPlayerController* MHGZPlayerController =
		Cast<AMHGZPlayerController>(PlayerOwner))
	{
		MHGZPlayerController->RecordUITelemetryEvent(
			TEXT("HUDDestroy"), this, TEXT("EndPlay"), BoundRuntimeToken.Generation);
	}
	UnbindPawnContext();
	if (MainHUDWidget)
	{
		MainHUDWidget->RemoveFromParent();
		MainHUDWidget = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void AMHGZHUD::HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	UE_LOG(LogTemp, Display,
		TEXT("[E6.3HUD] PawnChanged PC=%s old=%s new=%s"),
		*GetNameSafe(PlayerOwner), *GetNameSafe(OldPawn), *GetNameSafe(NewPawn));
	RefreshPawnContext();
}

void AMHGZHUD::RefreshPawnContext()
{
	// Do not consume the context or Ready token while their consumers do not
	// exist. BeginPlay replays the authoritative current context after creation.
	if (!bWidgetTreeReady)
	{
		return;
	}
	if (!IsValid(PlayerOwner) || !PlayerOwner->IsLocalPlayerController())
	{
		UnbindPawnContext();
		return;
	}

	AMHGZCharacter* Character = Cast<AMHGZCharacter>(PlayerOwner->GetPawn());
	IAbilitySystemInterface* AbilitySystemInterface = Character
		? Cast<IAbilitySystemInterface>(Character) : nullptr;
	UAbilitySystemComponent* ASC = AbilitySystemInterface
		? AbilitySystemInterface->GetAbilitySystemComponent() : nullptr;
	UMHGZAimComponent* Aim = Character ? Character->GetAimComponent() : nullptr;
	UMHGZWeaponRuntimeHostComponent* RuntimeHost =
		Character ? Character->GetWeaponRuntimeHost() : nullptr;

	if (bPawnContextInitialized
		&& BoundCharacter.Get() == Character && BoundASC.Get() == ASC
		&& BoundAimComponent.Get() == Aim && BoundRuntimeHost.Get() == RuntimeHost)
	{
		if (RuntimeHost && RuntimeHost->IsWeaponRuntimeReady() && !WeaponResourceWidget)
		{
			HandleWeaponRuntimeReady(RuntimeHost->GetCurrentToken(),
				RuntimeHost->GetResourceProvider());
		}
		return;
	}

	UnbindPawnContext();
	BoundCharacter = Character;
	BoundASC = ASC;
	BoundAimComponent = Aim;
	BoundRuntimeHost = RuntimeHost;
	bPawnContextInitialized = true;
	UE_LOG(LogTemp, Display,
		TEXT("[E6.3HUD] Context PC=%s pawn=%s character=%s ASC=%s Aim=%s Host=%s"),
		*GetNameSafe(PlayerOwner), *GetNameSafe(PlayerOwner->GetPawn()),
		*GetNameSafe(Character), *GetNameSafe(ASC), *GetNameSafe(Aim),
		*GetNameSafe(RuntimeHost));
	if (Character)
	{
		if (AMHGZPlayerController* MHGZPlayerController =
			Cast<AMHGZPlayerController>(PlayerOwner))
		{
			MHGZPlayerController->RecordUITelemetryEvent(
				TEXT("PawnBind"), Character, TEXT("ContextChanged"));
		}
	}

	if (HealthBarWidget)
	{
		HealthBarWidget->BindToASC(ASC);
	}
	if (StaminaBarWidget)
	{
		StaminaBarWidget->BindToASC(ASC);
	}
	if (CrosshairWidget)
	{
		CrosshairWidget->BindToASC(ASC);
		CrosshairWidget->BindToAimComponent(Aim);
	}

	if (RuntimeHost)
	{
		RuntimeReadyHandle = RuntimeHost->OnWeaponRuntimeReady.AddUObject(
			this, &AMHGZHUD::HandleWeaponRuntimeReady);
		RuntimeInvalidatedHandle = RuntimeHost->OnWeaponRuntimeInvalidated.AddUObject(
			this, &AMHGZHUD::HandleWeaponRuntimeInvalidated);
		if (RuntimeHost->IsWeaponRuntimeReady())
		{
			HandleWeaponRuntimeReady(RuntimeHost->GetCurrentToken(),
				RuntimeHost->GetResourceProvider());
		}
	}
}

void AMHGZHUD::UnbindPawnContext()
{
	if (AMHGZCharacter* Character = BoundCharacter.Get())
	{
		if (AMHGZPlayerController* MHGZPlayerController =
			Cast<AMHGZPlayerController>(PlayerOwner))
		{
			MHGZPlayerController->RecordUITelemetryEvent(TEXT("PawnUnbind"), Character,
				TEXT("ContextChanged"), BoundRuntimeToken.Generation);
		}
	}
	if (UMHGZWeaponRuntimeHostComponent* RuntimeHost = BoundRuntimeHost.Get())
	{
		if (RuntimeReadyHandle.IsValid())
		{
			RuntimeHost->OnWeaponRuntimeReady.Remove(RuntimeReadyHandle);
		}
		if (RuntimeInvalidatedHandle.IsValid())
		{
			RuntimeHost->OnWeaponRuntimeInvalidated.Remove(RuntimeInvalidatedHandle);
		}
	}
	RuntimeReadyHandle.Reset();
	RuntimeInvalidatedHandle.Reset();

	if (HealthBarWidget)
	{
		HealthBarWidget->UnbindFromASC();
	}
	if (StaminaBarWidget)
	{
		StaminaBarWidget->UnbindFromASC();
	}
	if (CrosshairWidget)
	{
		CrosshairWidget->UnbindFromAimComponent();
		CrosshairWidget->UnbindFromASC();
	}
	RemoveWeaponResourceWidget(TEXT("PawnContextChanged"));
	BoundRuntimeToken = FWeaponRuntimeToken();
	BoundCharacter.Reset();
	BoundASC.Reset();
	BoundAimComponent.Reset();
	BoundRuntimeHost.Reset();
	bPawnContextInitialized = false;
}

void AMHGZHUD::RemoveWeaponResourceWidget(const FName Reason)
{
	if (!WeaponResourceWidget)
	{
		return;
	}
	if (AMHGZPlayerController* MHGZPlayerController =
		Cast<AMHGZPlayerController>(PlayerOwner))
	{
		MHGZPlayerController->RecordUITelemetryEvent(TEXT("ResourcePanelRemove"),
			WeaponResourceWidget, Reason, BoundRuntimeToken.Generation);
	}
	WeaponResourceWidget->UnbindFromResourceComponent();
	WeaponResourceWidget->RemoveFromParent();
	WeaponResourceWidget = nullptr;
}

void AMHGZHUD::HandleWeaponRuntimeReady(const FWeaponRuntimeToken& Token,
	UMHGZWeaponResourceComponent* Resource)
{
	if (!bWidgetTreeReady)
	{
		return;
	}
	UMHGZWeaponRuntimeHostComponent* RuntimeHost = BoundRuntimeHost.Get();
	if (!RuntimeHost || !RuntimeHost->IsWeaponRuntimeReady()
		|| !RuntimeHost->IsTokenCurrent(Token))
	{
		if (AMHGZPlayerController* MHGZPlayerController =
			Cast<AMHGZPlayerController>(PlayerOwner))
		{
			MHGZPlayerController->RecordUITelemetryEvent(TEXT("StaleEventRejected"),
				RuntimeHost, TEXT("WeaponRuntimeReady"), Token.Generation);
		}
		return;
	}
	if (BoundRuntimeToken == Token && WeaponResourceWidget)
	{
		return;
	}
	const UWeaponRuntimeDefinition* RuntimeDefinition =
		RuntimeHost->GetCurrentWeaponSnapshot().RuntimeDefinition.Get();
	const TSubclassOf<UMHGZWeaponResourceWidget> WidgetClass = RuntimeDefinition
		? RuntimeDefinition->ResourceWidgetClass : nullptr;
	if (!WidgetClass || !Resource || !MainHUDWidget
		|| !MainHUDWidget->WeaponResourceSlot || !PlayerOwner)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[E6.3HUD] Resource panel deferred class=%s resource=%s slot=%s"),
			*GetNameSafe(WidgetClass.Get()), *GetNameSafe(Resource),
			*GetNameSafe(MainHUDWidget ? MainHUDWidget->WeaponResourceSlot.Get() : nullptr));
		return;
	}

	RemoveWeaponResourceWidget(TEXT("RuntimeChanged"));
	BoundRuntimeToken = FWeaponRuntimeToken();
	WeaponResourceWidget = CreateWidget<UMHGZWeaponResourceWidget>(
		PlayerOwner, WidgetClass);
	if (!WeaponResourceWidget)
	{
		return;
	}
	if (UOverlaySlot* ResourceChildSlot = Cast<UOverlaySlot>(
		MainHUDWidget->WeaponResourceSlot->AddChild(WeaponResourceWidget)))
	{
		ResourceChildSlot->SetHorizontalAlignment(HAlign_Fill);
		ResourceChildSlot->SetVerticalAlignment(VAlign_Fill);
	}
	else
	{
		RemoveWeaponResourceWidget(TEXT("AttachmentFailed"));
		return;
	}
	WeaponResourceWidget->BindToResourceComponent(Resource);
	BoundRuntimeToken = Token;
	UE_LOG(LogTemp, Display, TEXT("[E6.3HUD] Resource panel created=%s resource=%s generation=%llu"),
		*GetNameSafe(WeaponResourceWidget), *GetNameSafe(Resource), Token.Generation);
	if (AMHGZPlayerController* MHGZPlayerController =
		Cast<AMHGZPlayerController>(PlayerOwner))
	{
		MHGZPlayerController->RecordUITelemetryEvent(TEXT("RuntimeReady"),
			RuntimeHost, TEXT("ResourcePanelBound"), Token.Generation);
		MHGZPlayerController->RecordUITelemetryEvent(TEXT("ResourcePanelCreate"),
			WeaponResourceWidget, TEXT("RuntimeReady"), Token.Generation);
	}
}

void AMHGZHUD::HandleWeaponRuntimeInvalidated(const FWeaponRuntimeToken& Token)
{
	if (BoundRuntimeToken == Token)
	{
		if (AMHGZPlayerController* MHGZPlayerController =
			Cast<AMHGZPlayerController>(PlayerOwner))
		{
			MHGZPlayerController->RecordUITelemetryEvent(
				TEXT("RuntimeInvalidated"), BoundRuntimeHost.Get(),
				TEXT("MatchingToken"), Token.Generation);
		}
		RemoveWeaponResourceWidget(TEXT("RuntimeInvalidated"));
		BoundRuntimeToken = FWeaponRuntimeToken();
	}
	else if (AMHGZPlayerController* MHGZPlayerController =
		Cast<AMHGZPlayerController>(PlayerOwner))
	{
		MHGZPlayerController->RecordUITelemetryEvent(TEXT("StaleEventRejected"),
			BoundRuntimeHost.Get(), TEXT("WeaponRuntimeInvalidated"), Token.Generation);
	}
}
