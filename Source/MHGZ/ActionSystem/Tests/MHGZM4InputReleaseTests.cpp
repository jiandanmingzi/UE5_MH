// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "Engine/World.h"
#include "Equipment/MHGZEquipmentComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputAction.h"
#include "InputSystem/MHGZWeaponInputRouterComponent.h"
#include "MHGZPlayerState.h"
#include "MHGZM3TestTypes.h"
#include "Misc/DataValidation.h"
#include "WeaponRuntime/MHGZWeaponInputProfile.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

namespace
{
FGameplayTag Tag(const TCHAR* Name)
{
	return FGameplayTag::RequestGameplayTag(Name);
}

FWeaponChordDefinition& AddReleaseFallback(
	UWeaponInputProfile* Profile, const TCHAR* OutputTag, const TCHAR* TriggerTag)
{
	FWeaponChordDefinition& Chord = Profile->Chords.AddDefaulted_GetRef();
	Chord.OutputTag = Tag(OutputTag);
	Chord.TriggerControls.Add(Tag(TriggerTag));
	Chord.DispatchPolicy = EWeaponChordDispatchPolicy::OnReleaseIfUnconsumed;
	return Chord;
}

FWeaponChordDefinition& AddOnPressChord(UWeaponInputProfile* Profile,
	const TCHAR* OutputTag, TArray<FGameplayTag> Triggers,
	TArray<FGameplayTag> Modifiers = {}, const TCHAR* ReleaseControlTag = nullptr)
{
	FWeaponChordDefinition& Chord = Profile->Chords.AddDefaulted_GetRef();
	Chord.OutputTag = Tag(OutputTag);
	Chord.TriggerControls = MoveTemp(Triggers);
	Chord.RequiredHeldModifiers = MoveTemp(Modifiers);
	if (ReleaseControlTag)
	{
		Chord.ReleaseControlTag = Tag(ReleaseControlTag);
	}
	return Chord;
}

void AddMappedControl(UWeaponInputProfile* Profile, const FGameplayTag& ControlTag)
{
	Profile->RawActionToPhysicalInputTag.Add(NewObject<UInputAction>(Profile), ControlTag);
}

bool HasValidationIssueContaining(const FDataValidationContext& Context, const FString& Needle)
{
	for (const FDataValidationContext::FIssue& Issue : Context.GetIssues())
	{
		if (Issue.Message.ToString().Contains(Needle))
		{
			return true;
		}
	}
	return false;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM4ReleaseFallbackEmitsOnceTest,
	"MHGZ.M4.3.Input.ReleaseFallbackEmitsOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM4ReleaseFallbackEmitsOnceTest::RunTest(const FString& Parameters)
{
	const FGameplayTag RT = Tag(TEXT("Input.Modifier.RT"));
	const FGameplayTag Output = Tag(TEXT("Input.Weapon.RT"));
	UWeaponInputProfile* Profile = NewObject<UWeaponInputProfile>();
	Profile->ChordGracePeriod = 0.25f;
	AddReleaseFallback(Profile, TEXT("Input.Weapon.RT"), TEXT("Input.Modifier.RT"));

	UMHGZWeaponInputRouterComponent* Router = NewObject<UMHGZWeaponInputRouterComponent>();
	Router->SetInputProfile(Profile);
	Router->HandlePhysicalStarted(RT, 0.0);
	Router->FlushExpiredInputs(1.0);
	TestEqual(TEXT("press and grace timeout do not emit a raw RT fallback"),
		Router->GetCapturedSnapshots().Num(), 0);

	Router->HandlePhysicalCompleted(RT, 1.1);
	const TArray<FWeaponInputSnapshot>& Snapshots = Router->GetCapturedSnapshots();
	TestEqual(TEXT("release emits exactly one fallback Started snapshot"), Snapshots.Num(), 1);
	if (Snapshots.Num() == 1)
	{
		const FWeaponInputSnapshot& Snapshot = Snapshots[0];
		TestEqual(TEXT("release fallback output"), Snapshot.ResolvedInputTag, Output);
		TestEqual(TEXT("release fallback source is physical RT"), Snapshot.SourceControlTag, RT);
		TestEqual(TEXT("release fallback reuses the physical press sequence"), Snapshot.SequenceID, 1u);
		TestEqual(TEXT("release fallback timestamp is physical release"), Snapshot.Timestamp, 1.1);
		TestEqual(TEXT("release fallback is Started, not Completed"),
			Snapshot.Phase, EWeaponInputPhase::Started);
	}

	Router->HandlePhysicalCompleted(RT, 1.2);
	TestEqual(TEXT("late duplicate release is inert"), Router->GetCapturedSnapshots().Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM4WinnerConsumesFallbackTest,
	"MHGZ.M4.3.Input.WinnerConsumesFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM4WinnerConsumesFallbackTest::RunTest(const FString& Parameters)
{
	const FGameplayTag RT = Tag(TEXT("Input.Modifier.RT"));
	const FGameplayTag Y = Tag(TEXT("Input.Weapon.Y"));
	const FGameplayTag B = Tag(TEXT("Input.Weapon.B"));
	const FGameplayTag OutputRT = Tag(TEXT("Input.Weapon.RT"));
	const FGameplayTag OutputRTY = Tag(TEXT("Input.Weapon.RTY"));
	UWeaponInputProfile* Profile = NewObject<UWeaponInputProfile>();
	Profile->ChordGracePeriod = 0.25f;
	AddReleaseFallback(Profile, TEXT("Input.Weapon.RT"), TEXT("Input.Modifier.RT"));
	AddOnPressChord(Profile, TEXT("Input.Weapon.RTY"), { Y }, { RT });
	AddOnPressChord(Profile, TEXT("Input.Weapon.RTYB"), { Y, B }, { RT });

	UMHGZWeaponInputRouterComponent* Router = NewObject<UMHGZWeaponInputRouterComponent>();
	Router->SetInputProfile(Profile);
	Router->HandlePhysicalStarted(RT, 0.0);
	Router->HandlePhysicalStarted(Y, 0.1);
	TestEqual(TEXT("RT+Y defers while the larger RT+Y+B chord is still possible"),
		Router->GetCapturedSnapshots().Num(), 0);

	// RT is released before B arrives. HandlePhysicalCompleted must resolve the
	// already-complete deferred OnPress chord before it considers the RT fallback.
	Router->HandlePhysicalCompleted(RT, 0.2);
	TestEqual(TEXT("release-time OnPress winner emits exactly once"),
		Router->GetCapturedSnapshots().Num(), 1);
	if (Router->GetCapturedSnapshots().Num() == 1)
	{
		TestEqual(TEXT("release-time winner output is RTY"),
			Router->GetCapturedSnapshots()[0].ResolvedInputTag, OutputRTY);
	}

	TestEqual(TEXT("release-time winning modifier chord consumes the RT release fallback"),
		Router->GetCapturedSnapshots().Num(), 1);
	TestFalse(TEXT("the raw release-fallback output never appears"),
		Router->GetCapturedSnapshots().ContainsByPredicate([OutputRT](const FWeaponInputSnapshot& Snapshot)
		{
			return Snapshot.ResolvedInputTag == OutputRT;
		}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM4ReleaseFallbackRechecksExactModifiersTest,
	"MHGZ.M4.3.Input.ReleaseFallbackRechecksExactModifiers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM4ReleaseFallbackRechecksExactModifiersTest::RunTest(const FString& Parameters)
{
	const FGameplayTag RT = Tag(TEXT("Input.Modifier.RT"));
	const FGameplayTag LT = Tag(TEXT("Input.Modifier.LT"));
	const FGameplayTag Y = Tag(TEXT("Input.Weapon.Y"));
	UWeaponInputProfile* Profile = NewObject<UWeaponInputProfile>();
	Profile->ChordGracePeriod = 0.25f;
	AddReleaseFallback(Profile, TEXT("Input.Weapon.RT"), TEXT("Input.Modifier.RT"));
	AddOnPressChord(Profile, TEXT("Input.Weapon.LTY"), { Y }, { LT });

	UMHGZWeaponInputRouterComponent* Router = NewObject<UMHGZWeaponInputRouterComponent>();
	Router->SetInputProfile(Profile);
	Router->HandlePhysicalStarted(RT, 0.0);
	Router->HandlePhysicalStarted(LT, 0.1);
	Router->HandlePhysicalCompleted(RT, 0.2);
	TestEqual(TEXT("extra configured modifier added during hold rejects exact release fallback"),
		Router->GetCapturedSnapshots().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM4ReleaseFallbackRechecksContextTest,
	"MHGZ.M4.3.Input.ReleaseFallbackRechecksContext",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM4ReleaseFallbackRechecksContextTest::RunTest(const FString& Parameters)
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
	Host->InitializePawnRuntime(Character, nullptr, ASC, PlayerState->GetEquipmentComponent());

	UMHGZWeaponInputRouterComponent* Router = NewObject<UMHGZWeaponInputRouterComponent>();
	Router->AttachToPawn(Character);
	UWeaponInputProfile* Profile = NewObject<UWeaponInputProfile>();
	FWeaponChordDefinition& Fallback =
		AddReleaseFallback(Profile, TEXT("Input.Weapon.RT"), TEXT("Input.Modifier.RT"));
	Fallback.RequiredContextTags.AddTag(Tag(TEXT("Combat.State.Sheathed")));
	Router->SetInputProfile(Profile);

	const FGameplayTag RT = Tag(TEXT("Input.Modifier.RT"));
	Host->SetSheathed(true);
	Router->HandlePhysicalStarted(RT, 0.0);
	Host->SetSheathed(false);
	Router->HandlePhysicalCompleted(RT, 0.1);
	TestEqual(TEXT("context is checked at release instead of the original press"),
		Router->GetCapturedSnapshots().Num(), 0);

	Host->SetSheathed(true);
	Router->HandlePhysicalStarted(RT, 1.0);
	Router->HandlePhysicalCompleted(RT, 1.1);
	TestEqual(TEXT("current valid release context emits fallback"),
		Router->GetCapturedSnapshots().Num(), 1);

	Router->ShutdownRouter();
	Host->ShutdownRuntime(EWeaponRuntimeEndReason::RuntimeShutdown);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM4OnPressRegressionTest,
	"MHGZ.M4.3.Input.OnPressRegression",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM4OnPressRegressionTest::RunTest(const FString& Parameters)
{
	const FGameplayTag RT = Tag(TEXT("Input.Modifier.RT"));
	const FGameplayTag Output = Tag(TEXT("Input.Weapon.RT"));
	UWeaponInputProfile* Profile = NewObject<UWeaponInputProfile>();
	AddOnPressChord(Profile, TEXT("Input.Weapon.RT"), { RT }, {}, TEXT("Input.Modifier.RT"));

	UMHGZWeaponInputRouterComponent* Router = NewObject<UMHGZWeaponInputRouterComponent>();
	Router->SetInputProfile(Profile);
	Router->HandlePhysicalStarted(RT, 0.0);
	TestEqual(TEXT("OnPress chord still emits at physical press"),
		Router->GetCapturedSnapshots().Num(), 1);
	Router->HandlePhysicalCompleted(RT, 0.1);
	const TArray<FWeaponInputSnapshot>& Snapshots = Router->GetCapturedSnapshots();
	TestEqual(TEXT("OnPress release identity still emits Completed"), Snapshots.Num(), 2);
	if (Snapshots.Num() == 2)
	{
		TestEqual(TEXT("OnPress output remains unchanged"), Snapshots[0].ResolvedInputTag, Output);
		TestEqual(TEXT("OnPress completion keeps the original identity"),
			Snapshots[1].SequenceID, Snapshots[0].SequenceID);
		TestEqual(TEXT("OnPress completion phase"), Snapshots[1].Phase, EWeaponInputPhase::Completed);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM4ReleaseFallbackProfileSwapAndShutdownTest,
	"MHGZ.M4.3.Input.ProfileSwapAndShutdownDoNotLeak",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM4ReleaseFallbackProfileSwapAndShutdownTest::RunTest(const FString& Parameters)
{
	const FGameplayTag RT = Tag(TEXT("Input.Modifier.RT"));
	UWeaponInputProfile* FallbackProfile = NewObject<UWeaponInputProfile>();
	AddReleaseFallback(FallbackProfile, TEXT("Input.Weapon.RT"), TEXT("Input.Modifier.RT"));
	UWeaponInputProfile* EmptyProfile = NewObject<UWeaponInputProfile>();

	UMHGZWeaponInputRouterComponent* Router = NewObject<UMHGZWeaponInputRouterComponent>();
	Router->SetInputProfile(FallbackProfile);
	Router->HandlePhysicalStarted(RT, 0.0);
	Router->SetInputProfile(EmptyProfile);
	Router->HandlePhysicalCompleted(RT, 0.1);
	TestFalse(TEXT("a press that began under the old profile cannot release into a new release-fallback chord"),
		Router->GetCapturedSnapshots().ContainsByPredicate([](const FWeaponInputSnapshot& Snapshot)
		{
			return Snapshot.ResolvedInputTag == Tag(TEXT("Input.Weapon.RT"));
		}));

	Router->SetInputProfile(FallbackProfile);
	Router->HandlePhysicalStarted(RT, 1.0);
	Router->ShutdownRouter();
	Router->HandlePhysicalCompleted(RT, 1.1);
	TestEqual(TEXT("shutdown drops release-fallback state and late release is inert"),
		Router->GetCapturedSnapshots().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM4ReleaseFallbackValidationTest,
	"MHGZ.M4.3.Input.ValidationRejectsIllegalPolicies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM4ReleaseFallbackValidationTest::RunTest(const FString& Parameters)
{
	UWeaponInputProfile* Profile = NewObject<UWeaponInputProfile>();
	AddMappedControl(Profile, Tag(TEXT("Input.Modifier.RT")));
	AddMappedControl(Profile, Tag(TEXT("Input.Modifier.LT")));
	AddMappedControl(Profile, Tag(TEXT("Input.Weapon.Y")));
	AddMappedControl(Profile, Tag(TEXT("Input.Weapon.B")));

	FWeaponChordDefinition& MultiTrigger =
		AddReleaseFallback(Profile, TEXT("Input.Weapon.RT"), TEXT("Input.Modifier.RT"));
	MultiTrigger.TriggerControls.Add(Tag(TEXT("Input.Weapon.Y")));
	FWeaponChordDefinition& WithModifier =
		AddReleaseFallback(Profile, TEXT("Input.Weapon.B"), TEXT("Input.Weapon.B"));
	WithModifier.RequiredHeldModifiers.Add(Tag(TEXT("Input.Modifier.LT")));
	FWeaponChordDefinition& WithReleaseControl =
		AddReleaseFallback(Profile, TEXT("Input.Weapon.Y"), TEXT("Input.Weapon.Y"));
	WithReleaseControl.ReleaseControlTag = Tag(TEXT("Input.Weapon.Y"));
	AddReleaseFallback(Profile, TEXT("Input.Weapon.RTY"), TEXT("Input.Modifier.RT"));
	AddReleaseFallback(Profile, TEXT("Input.Weapon.RTA"), TEXT("Input.Modifier.RT"));

	FDataValidationContext Context;
	TestEqual(TEXT("illegal release policies are rejected"),
		Profile->IsDataValid(Context), EDataValidationResult::Invalid);
	TestTrue(TEXT("validation reports the release policy"),
		HasValidationIssueContaining(Context, TEXT("OnReleaseIfUnconsumed")));
	TestTrue(TEXT("validation reports duplicate release trigger ownership"),
		HasValidationIssueContaining(Context, TEXT("both declare OnReleaseIfUnconsumed")));
	return true;
}

#endif
