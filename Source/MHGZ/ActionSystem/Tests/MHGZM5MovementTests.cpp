// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "ActionSystem/MHGZDodgeAbility.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5MovementOwnershipTest,
	"MHGZ.M5.Movement.OwnershipAndCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5MovementOwnershipTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!World)
	{
		return false;
	}

	ACharacter* Character = World->SpawnActor<ACharacter>(
		ACharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
	if (!Character)
	{
		World->DestroyWorld(false);
		return false;
	}
	Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);

	UMHGZAbilitySystemComponent* ASC = NewObject<UMHGZAbilitySystemComponent>(Character);
	Character->AddInstanceComponent(ASC);
	ASC->RegisterComponent();
	UMHGZWeaponRuntimeHostComponent* Host =
		NewObject<UMHGZWeaponRuntimeHostComponent>(Character);
	Character->AddInstanceComponent(Host);
	Host->RegisterComponent();
	Host->InitializePawnRuntime(Character, nullptr, ASC, nullptr);

	const FGameplayAbilitySpecHandle AbilityHandle = ASC->GiveAbility(
		FGameplayAbilitySpec(UMHGZDodgeAbility::StaticClass(), 1, INDEX_NONE, ASC));
	UMHGZDodgeAbility* Ability = NewObject<UMHGZDodgeAbility>(ASC);
	FWeaponActionToken Action;
	Action.RuntimeToken = Host->GetCurrentToken();
	Action.AbilityHandle = AbilityHandle;
	Action.ActivationSequenceID = Host->AllocateActivationSequenceID();
	Action.AbilityInstance = Ability;
	if (!TestTrue(TEXT("valid action registers"), Host->RegisterAction(Action)))
	{
		World->DestroyWorld(false);
		return false;
	}

	FName WarpTarget;
	TestTrue(TEXT("registered action acquires movement ownership"),
		Host->AcquireActionMovement(Action, WarpTarget));
	TestFalse(TEXT("movement target name is unique and non-empty"), WarpTarget.IsNone());
	TestTrue(TEXT("host reports exact movement owner"),
		Host->IsActionMovementOwnedBy(Action));
	TestFalse(TEXT("movement owner blocks montage root-motion ownership"),
		Host->AcquireMontageRootMotion(Action));

	UMHGZDodgeAbility* CompetingAbility = NewObject<UMHGZDodgeAbility>(ASC);
	FWeaponActionToken Competing = Action;
	Competing.ActivationSequenceID = Host->AllocateActivationSequenceID();
	Competing.AbilityInstance = CompetingAbility;
	TestTrue(TEXT("competing action registers"), Host->RegisterAction(Competing));
	FName CompetingWarpTarget;
	TestFalse(TEXT("competing action cannot steal movement ownership"),
		Host->AcquireActionMovement(Competing, CompetingWarpTarget));
	TestFalse(TEXT("competing action cannot release movement ownership"),
		Host->ReleaseActionMovement(Competing));
	TestTrue(TEXT("exact action releases movement ownership"),
		Host->ReleaseActionMovement(Action));
	TestFalse(TEXT("movement ownership is released"), Host->IsActionMovementOwned());

	TestTrue(TEXT("montage root motion can acquire after movement release"),
		Host->AcquireMontageRootMotion(Action));
	TestTrue(TEXT("exact montage root motion release succeeds"),
		Host->ReleaseMontageRootMotion(Action));

	TestTrue(TEXT("action can reacquire movement before action cleanup"),
		Host->AcquireActionMovement(Action, WarpTarget));
	TestTrue(TEXT("unregistering owner releases movement ownership"),
		Host->UnregisterAction(Action));
	TestFalse(TEXT("unregister leaves no stale movement owner"),
		Host->IsActionMovementOwned());
	Host->UnregisterAction(Competing);

	Host->ShutdownRuntime(EWeaponRuntimeEndReason::RuntimeShutdown);
	TestFalse(TEXT("runtime shutdown leaves no movement owner"),
		Host->IsActionMovementOwned());
	World->DestroyWorld(false);
	return true;
}

#endif
