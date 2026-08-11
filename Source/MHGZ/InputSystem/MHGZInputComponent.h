// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MHGZInputComponent.generated.h"

class APlayerController;
class UEnhancedInputComponent;
class UInputMappingContext;
class UMHGZWeaponInputRouterComponent;

/**
 * UMHGZInputComponent - input component (mounted on the local PlayerController).
 *
 * Sole owner of Enhanced Input mapping-context additions and raw-action binding
 * handles. Every IMC added and every BindAction/BindCharacterInput handle is
 * stored here and removed exactly once by ShutdownInput, so repeated
 * Setup/UnPossess/EndPlay never stack bindings.
 */
UCLASS(ClassGroup = (MHGZ))
class UMHGZInputComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMHGZInputComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TArray<UInputMappingContext*> DefaultIMCs;

	/**
	 * Idempotent initialization: adds the default IMCs (once per context) and
	 * (re)binds every raw weapon action plus the character locomotion actions.
	 * Repeated calls (repeat Setup / repeat Possess) do not stack; if already
	 * initialized with a different PC/router, the previous state is shut down first.
	 */
	void InitializeInput(APlayerController* InPC, UMHGZWeaponInputRouterComponent* InRouter);

	/** Re-binds the raw weapon actions from the router's current input profile (idempotent). */
	void RefreshRawBindings();

	/** Idempotent teardown: removes every stored binding handle and added IMC. */
	void ShutdownInput();

	/** Compatible wrappers: Push/Pop a runtime overlay IMC, tracked for teardown. */
	void PushIMC(UInputMappingContext* IMC, int32 Priority);
	void PopIMC(UInputMappingContext* IMC);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UEnhancedInputComponent* GetEnhancedInputComponent() const;
	void AddDefaultContexts();
	void RemoveAllContexts();
	void UnbindAll();
	void OnRouterProfileChanged();

	UPROPERTY()
	TWeakObjectPtr<APlayerController> OwnerPC;

	UPROPERTY()
	TWeakObjectPtr<UMHGZWeaponInputRouterComponent> Router;

	/** The native EnhancedInputComponent the stored handles were bound on (weak: stale after re-init). */
	UPROPERTY()
	TWeakObjectPtr<UEnhancedInputComponent> BoundInputComponent;

	/** Binding handles for raw weapon actions owned by this component. */
	TArray<uint32> RawBindingHandles;

	/** Binding handles appended by AMHGZCharacter::BindCharacterInput (Move/Look/Sprint). */
	TArray<uint32> CharacterBindingHandles;

	/** Per-IMC add count; Shutdown removes each context that many times (net zero). */
	TMap<TWeakObjectPtr<UInputMappingContext>, int32> AddedContextCounts;

	FDelegateHandle OnRouterProfileChangedHandle;
	bool bInputInitialized = false;
};
