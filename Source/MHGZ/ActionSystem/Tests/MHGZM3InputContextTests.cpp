// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "Engine/World.h"
#include "Equipment/MHGZEquipmentComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputSystem/MHGZWeaponInputRouterComponent.h"
#include "MHGZPlayerState.h"
#include "MHGZM3TestTypes.h"
#include "WeaponRuntime/MHGZWeaponInputProfile.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

namespace
{
FGameplayTag InputTag(const TCHAR* Name)
{
	return FGameplayTag::RequestGameplayTag(Name);
}

FWeaponChordDefinition& AddContextChord(UWeaponInputProfile* Profile,
	const TCHAR* Output, const TCHAR* Trigger, const TCHAR* Modifier,
	std::initializer_list<const TCHAR*> RequiredContexts,
	EWeaponAimSnapshotContext AimContext = EWeaponAimSnapshotContext::None)
{
	FWeaponChordDefinition& Chord = Profile->Chords.AddDefaulted_GetRef();
	Chord.OutputTag = InputTag(Output);
	Chord.TriggerControls.Add(InputTag(Trigger));
	if (Modifier)
	{
		Chord.RequiredHeldModifiers.Add(InputTag(Modifier));
	}
	for (const TCHAR* Context : RequiredContexts)
	{
		Chord.RequiredContextTags.AddTag(InputTag(Context));
	}
	Chord.ReleaseControlTag = InputTag(Trigger);
	Chord.AimSnapshotContext = AimContext;
	return Chord;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM3ContextChordRoutingTest,
	"MHGZ.M3.Input.ContextChordsRouteRTLTRTAndSheathe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM3ContextChordRoutingTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("world created"), World)) return false;
	AMHGZM3TestCharacter* Character = World->SpawnActor<AMHGZM3TestCharacter>();
	AMHGZPlayerState* PlayerState = World->SpawnActor<AMHGZPlayerState>();
	if (!TestNotNull(TEXT("character created"), Character)
		|| !TestNotNull(TEXT("player state created"), PlayerState))
	{
		World->DestroyWorld(false);
		return false;
	}
	Character->SetPlayerState(PlayerState);
	Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	UMHGZAbilitySystemComponent* ASC = PlayerState->GetMHGZAbilitySystemComponent();
	ASC->InitAbilityActorInfo(PlayerState, Character);

	UMHGZWeaponRuntimeHostComponent* Host =
		NewObject<UMHGZWeaponRuntimeHostComponent>(Character);
	Character->AddInstanceComponent(Host);
	Host->RegisterComponent();
	Host->InitializePawnRuntime(Character, nullptr, ASC,
		PlayerState->GetEquipmentComponent());

	UMHGZWeaponInputRouterComponent* Router =
		NewObject<UMHGZWeaponInputRouterComponent>();
	Router->AttachToPawn(Character);
	UWeaponInputProfile* Profile = NewObject<UWeaponInputProfile>();
	Profile->ChordGracePeriod = 0.25f;
	AddContextChord(Profile, TEXT("Input.Weapon.RT"), TEXT("Input.Modifier.RT"), nullptr,
		{ TEXT("Combat.State.Sheathed"), TEXT("Combat.State.Grounded") });
	AddContextChord(Profile, TEXT("Input.Weapon.LTRT"), TEXT("Input.Modifier.RT"),
		TEXT("Input.Modifier.LT"),
		{ TEXT("Combat.State.Unsheathed"), TEXT("Combat.State.Grounded") },
		EWeaponAimSnapshotContext::Kinsect);
	AddContextChord(Profile, TEXT("Input.Sheathe"), TEXT("Input.Modifier.Sheathed"), nullptr,
		{ TEXT("Combat.State.Unsheathed") });
	Router->SetInputProfile(Profile);

	const FGameplayTag RT = InputTag(TEXT("Input.Modifier.RT"));
	const FGameplayTag LT = InputTag(TEXT("Input.Modifier.LT"));
	const FGameplayTag RB = InputTag(TEXT("Input.Modifier.Sheathed"));
	const FGameplayTag OutputRT = InputTag(TEXT("Input.Weapon.RT"));
	const FGameplayTag OutputLTRT = InputTag(TEXT("Input.Weapon.LTRT"));

	Router->HandlePhysicalStarted(RT, 0.0);
	TestEqual(TEXT("sheathed RT emits immediately"), Router->GetCapturedSnapshots().Num(), 1);
	if (Router->GetCapturedSnapshots().Num() > 0)
	{
		TestEqual(TEXT("sheathed RT output"),
			Router->GetCapturedSnapshots()[0].ResolvedInputTag, OutputRT);
	}
	Router->HandlePhysicalCompleted(RT, 0.01);

	Host->SetSheathed(false);
	const int32 BeforeUnsheathedRT = Router->GetCapturedSnapshots().Num();
	Router->HandlePhysicalStarted(RT, 1.0);
	TestEqual(TEXT("unsheathed RT waits for LT"),
		Router->GetCapturedSnapshots().Num(), BeforeUnsheathedRT);
	Router->HandlePhysicalStarted(LT, 1.1);
	TestTrue(TEXT("RT then LT emits one new LTRT"),
		Router->GetCapturedSnapshots().Num() > BeforeUnsheathedRT);
	if (Router->GetCapturedSnapshots().Num() > BeforeUnsheathedRT)
	{
		TestEqual(TEXT("RT then LT emits LTRT"),
			Router->GetCapturedSnapshots().Last().ResolvedInputTag, OutputLTRT);
		TestTrue(TEXT("modifier-last snapshot includes kinsect aim context tag"),
			Router->GetCapturedSnapshots().Last().ContextTags.HasTagExact(
				InputTag(TEXT("Combat.State.Aiming.Kinsect"))));
	}
	Router->HandlePhysicalCompleted(LT, 1.2);
	Router->HandlePhysicalCompleted(RT, 1.2);

	const int32 BeforeReverse = Router->GetCapturedSnapshots().Num();
	Router->HandlePhysicalStarted(LT, 2.0);
	Router->HandlePhysicalStarted(RT, 2.1);
	TestTrue(TEXT("LT then RT emits one new LTRT"),
		Router->GetCapturedSnapshots().Num() > BeforeReverse);
	if (Router->GetCapturedSnapshots().Num() > BeforeReverse)
	{
		TestEqual(TEXT("LT then RT output"),
			Router->GetCapturedSnapshots().Last().ResolvedInputTag, OutputLTRT);
	}
	Router->HandlePhysicalCompleted(RT, 2.2);
	Router->HandlePhysicalCompleted(LT, 2.2);

	const int32 BeforeRB = Router->GetCapturedSnapshots().Num();
	Router->HandlePhysicalStarted(RB, 3.0);
	TestEqual(TEXT("unsheathed RB emits Sheathe immediately"),
		Router->GetCapturedSnapshots().Num(), BeforeRB + 1);
	if (Router->GetCapturedSnapshots().Num() > BeforeRB)
	{
		TestEqual(TEXT("RB output is Input.Sheathe"),
			Router->GetCapturedSnapshots().Last().ResolvedInputTag,
			InputTag(TEXT("Input.Sheathe")));
	}
	Router->HandlePhysicalCompleted(RB, 3.1);

	Host->SetSheathed(true);
	const int32 BeforeSheathedRB = Router->GetCapturedSnapshots().Num();
	Router->HandlePhysicalStarted(RB, 4.0);
	Router->FlushExpiredInputs(5.0);
	TestEqual(TEXT("sheathed RB stays silent for Character sprint path"),
		Router->GetCapturedSnapshots().Num(), BeforeSheathedRB);
	Router->HandlePhysicalCompleted(RB, 5.1);

	Router->ShutdownRouter();
	Host->ShutdownRuntime(EWeaponRuntimeEndReason::RuntimeShutdown);
	World->DestroyWorld(false);
	return true;
}

#endif
