// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "EnhancedInputComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputSystem/MHGZInputComponent.h"
#include "InputSystem/MHGZWeaponInputRouterComponent.h"
#include "WeaponRuntime/MHGZWeaponInputProfile.h"

namespace
{
FGameplayTag Tag(const TCHAR* Name)
{
	return FGameplayTag::RequestGameplayTag(Name);
}

FWeaponChordDefinition* AddChord(
	UWeaponInputProfile* Profile,
	const TCHAR* OutputTag,
	TArray<FGameplayTag> Triggers,
	TArray<FGameplayTag> Modifiers = {},
	int32 Priority = 0,
	bool bExact = true,
	bool bConsume = true,
	const TCHAR* ReleaseControlTag = nullptr)
{
	FWeaponChordDefinition& Chord = Profile->Chords.AddDefaulted_GetRef();
	Chord.OutputTag = Tag(OutputTag);
	Chord.TriggerControls = MoveTemp(Triggers);
	Chord.RequiredHeldModifiers = MoveTemp(Modifiers);
	Chord.Priority = Priority;
	Chord.bRequireExactModifiers = bExact;
	Chord.bConsumeTriggerControls = bConsume;
	if (ReleaseControlTag)
	{
		Chord.ReleaseControlTag = Tag(ReleaseControlTag);
	}
	return &Chord;
}
}

// RT->Y->B and Y->B->RT must converge on the same single chord output (RTYB),
// and Y/B must never surface as singles when consumed by that chord.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM1ChordOrderConvergenceTest,
	"MHGZ.M1.Input.ChordOrdersConvergeOnRTYB",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM1ChordOrderConvergenceTest::RunTest(const FString& Parameters)
{
	const FGameplayTag YTag = Tag(TEXT("Input.Weapon.Y"));
	const FGameplayTag BTag = Tag(TEXT("Input.Weapon.B"));
	const FGameplayTag RTTag = Tag(TEXT("Input.Modifier.RT"));
	const FGameplayTag RTYB = Tag(TEXT("Input.Weapon.RTYB"));
	TestTrue(TEXT("Input.Weapon.Y is registered"), YTag.IsValid());
	TestTrue(TEXT("Input.Weapon.B is registered"), BTag.IsValid());
	TestTrue(TEXT("Input.Modifier.RT is registered"), RTTag.IsValid());
	TestTrue(TEXT("Input.Weapon.RTYB is registered"), RTYB.IsValid());

	auto MakeProfile = []()
	{
		UWeaponInputProfile* Profile = NewObject<UWeaponInputProfile>();
		Profile->ChordGracePeriod = 0.25f;
		AddChord(Profile, TEXT("Input.Weapon.YB"), { Tag(TEXT("Input.Weapon.Y")), Tag(TEXT("Input.Weapon.B")) });
		AddChord(Profile, TEXT("Input.Weapon.RTYB"), { Tag(TEXT("Input.Weapon.Y")), Tag(TEXT("Input.Weapon.B")) }, { Tag(TEXT("Input.Modifier.RT")) });
		return Profile;
	};

	// Order 1: RT first, then Y, then B.
	{
		UMHGZWeaponInputRouterComponent* Router = NewObject<UMHGZWeaponInputRouterComponent>();
		Router->SetInputProfile(MakeProfile());
		Router->HandlePhysicalStarted(RTTag, 0.0);
		Router->HandlePhysicalStarted(YTag, 0.1);
		Router->HandlePhysicalStarted(BTag, 0.2);
		Router->FlushExpiredInputs(1.0);

		const TArray<FWeaponInputSnapshot>& Snapshots = Router->GetCapturedSnapshots();
		TestEqual(TEXT("RT-first order emits exactly one snapshot"), Snapshots.Num(), 1);
		if (Snapshots.Num() == 1)
		{
			TestEqual(TEXT("RT-first resolves to RTYB"), Snapshots[0].ResolvedInputTag, RTYB);
			TestEqual(TEXT("RT-first freezes at last member timestamp"), Snapshots[0].Timestamp, 0.2);
			TestEqual(TEXT("RT-first source is the last required member"), Snapshots[0].SourceControlTag, BTag);
			TestEqual(TEXT("RT-first phase is Started"), Snapshots[0].Phase, EWeaponInputPhase::Started);
			TestTrue(TEXT("RTYB carries the held RT modifier"),
				Snapshots[0].HeldModifierTags.HasTagExact(RTTag));
		}
	}

	// Order 2: Y, then B (deferred), then RT last.
	{
		UMHGZWeaponInputRouterComponent* Router = NewObject<UMHGZWeaponInputRouterComponent>();
		Router->SetInputProfile(MakeProfile());
		Router->HandlePhysicalStarted(YTag, 0.1);
		Router->HandlePhysicalStarted(BTag, 0.2);
		TestEqual(TEXT("Y+B alone emits nothing before the window closes"),
			Router->GetCapturedSnapshots().Num(), 0);
		Router->HandlePhysicalStarted(RTTag, 0.3);
		Router->FlushExpiredInputs(1.0);

		const TArray<FWeaponInputSnapshot>& Snapshots = Router->GetCapturedSnapshots();
		TestEqual(TEXT("Y->B->RT also emits exactly one snapshot"), Snapshots.Num(), 1);
		if (Snapshots.Num() == 1)
		{
			TestEqual(TEXT("Y->B->RT converges on RTYB"), Snapshots[0].ResolvedInputTag, RTYB);
			TestEqual(TEXT("Y->B->RT freezes at the RT (last member) timestamp"), Snapshots[0].Timestamp, 0.3);
			TestEqual(TEXT("Y->B->RT source is RT"), Snapshots[0].SourceControlTag, RTTag);
		}
	}

	return true;
}

// Y+B: the chord emits once after the window closes; the Y/B singles never emit;
// release emits only the identity registered under the release control.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM1YBSuppressesSinglesTest,
	"MHGZ.M1.Input.YBSuppressesSinglesAndScopesRelease",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM1YBSuppressesSinglesTest::RunTest(const FString& Parameters)
{
	const FGameplayTag YTag = Tag(TEXT("Input.Weapon.Y"));
	const FGameplayTag BTag = Tag(TEXT("Input.Weapon.B"));
	const FGameplayTag YB = Tag(TEXT("Input.Weapon.YB"));

	UWeaponInputProfile* Profile = NewObject<UWeaponInputProfile>();
	Profile->ChordGracePeriod = 0.25f;
	AddChord(Profile, TEXT("Input.Weapon.YB"),
		{ YTag, BTag }, {}, 0, true, true, TEXT("Input.Weapon.Y"));
	AddChord(Profile, TEXT("Input.Weapon.LTYB"),
		{ YTag, BTag }, { Tag(TEXT("Input.Modifier.LT")) });

	UMHGZWeaponInputRouterComponent* Router = NewObject<UMHGZWeaponInputRouterComponent>();
	Router->SetInputProfile(Profile);

	Router->HandlePhysicalStarted(YTag, 0.1);
	Router->HandlePhysicalStarted(BTag, 0.2);
	TestEqual(TEXT("Y+B defers while a bigger chord could still form"),
		Router->GetCapturedSnapshots().Num(), 0);

	Router->FlushExpiredInputs(0.35); // window closes at 0.1 + 0.25
	const TArray<FWeaponInputSnapshot>& AfterFlush = Router->GetCapturedSnapshots();
	TestEqual(TEXT("window close emits exactly the YB chord"), AfterFlush.Num(), 1);
	if (AfterFlush.Num() == 1)
	{
		TestEqual(TEXT("chord output is YB"), AfterFlush[0].ResolvedInputTag, YB);
		TestEqual(TEXT("chord freezes at the last member (B) timestamp"), AfterFlush[0].Timestamp, 0.2);
		TestEqual(TEXT("chord source control is B"), AfterFlush[0].SourceControlTag, BTag);
		TestTrue(TEXT("no Y or B single was emitted"),
			AfterFlush[0].ResolvedInputTag != YTag && AfterFlush[0].ResolvedInputTag != BTag);
	}

	Router->FlushExpiredInputs(1.0);
	TestEqual(TEXT("later flush adds nothing"), Router->GetCapturedSnapshots().Num(), 1);

	Router->HandlePhysicalCompleted(YTag, 1.1);
	const TArray<FWeaponInputSnapshot>& AfterYRelease = Router->GetCapturedSnapshots();
	TestEqual(TEXT("Y release emits one Completed"), AfterYRelease.Num(), 2);
	if (AfterYRelease.Num() == 2)
	{
		const FWeaponInputSnapshot& Original = AfterFlush[0];
		const FWeaponInputSnapshot& Release = AfterYRelease[1];
		TestEqual(TEXT("release keeps ResolvedInputTag"), Release.ResolvedInputTag, Original.ResolvedInputTag);
		TestEqual(TEXT("release keeps SourceControlTag"), Release.SourceControlTag, Original.SourceControlTag);
		TestEqual(TEXT("release keeps SequenceID"), Release.SequenceID, Original.SequenceID);
		TestEqual(TEXT("release keeps RawMoveInput"), Release.RawMoveInput, Original.RawMoveInput);
		TestEqual(TEXT("release keeps WorldDirection"), Release.WorldDirection, Original.WorldDirection);
		TestEqual(TEXT("release keeps Direction"), Release.Direction, Original.Direction);
		TestEqual(TEXT("release keeps HeldModifierTags"), Release.HeldModifierTags, Original.HeldModifierTags);
		TestEqual(TEXT("release keeps ContextTags"), Release.ContextTags, Original.ContextTags);
		TestEqual(TEXT("release phase is Completed"), Release.Phase, EWeaponInputPhase::Completed);
		TestEqual(TEXT("release timestamp is the release time"), Release.Timestamp, 1.1);
	}

	Router->HandlePhysicalCompleted(BTag, 1.2);
	TestEqual(TEXT("B release emits nothing (no identity registered under B)"),
		Router->GetCapturedSnapshots().Num(), 2);

	return true;
}

// A modifier arriving after the trigger deadline cannot join the chord and,
// as held context rather than a discrete action, never emits a fallback single.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM1LateModifierTest,
	"MHGZ.M1.Input.LateModifierDoesNotJoinChord",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM1LateModifierTest::RunTest(const FString& Parameters)
{
	const FGameplayTag YTag = Tag(TEXT("Input.Weapon.Y"));
	const FGameplayTag BTag = Tag(TEXT("Input.Weapon.B"));
	const FGameplayTag LTTag = Tag(TEXT("Input.Modifier.LT"));
	const FGameplayTag YB = Tag(TEXT("Input.Weapon.YB"));
	const FGameplayTag LTYB = Tag(TEXT("Input.Weapon.LTYB"));

	UWeaponInputProfile* Profile = NewObject<UWeaponInputProfile>();
	Profile->ChordGracePeriod = 0.25f;
	AddChord(Profile, TEXT("Input.Weapon.YB"), { YTag, BTag });
	AddChord(Profile, TEXT("Input.Weapon.LTYB"), { YTag, BTag }, { LTTag });

	UMHGZWeaponInputRouterComponent* Router = NewObject<UMHGZWeaponInputRouterComponent>();
	Router->SetInputProfile(Profile);

	Router->HandlePhysicalStarted(YTag, 0.1);
	Router->HandlePhysicalStarted(BTag, 0.2);
	Router->FlushExpiredInputs(0.35); // YB window closes; LT has not arrived
	TestEqual(TEXT("window close emits YB"), Router->GetCapturedSnapshots().Num(), 1);

	Router->HandlePhysicalStarted(LTTag, 0.5); // after the Y/B trigger deadline
	Router->FlushExpiredInputs(1.0);
	Router->HandlePhysicalCompleted(LTTag, 1.1);

	const TArray<FWeaponInputSnapshot>& Snapshots = Router->GetCapturedSnapshots();
	TestEqual(TEXT("late LT never forms LTYB or a modifier single"), Snapshots.Num(), 1);
	if (Snapshots.Num() == 1)
	{
		TestEqual(TEXT("first snapshot is the YB chord"), Snapshots[0].ResolvedInputTag, YB);
		TestTrue(TEXT("LTYB never emitted"), Snapshots[0].ResolvedInputTag != LTYB);
	}

	return true;
}

// Pure direction classification relative to character forward/right.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM1DirectionClassifierTest,
	"MHGZ.M1.Input.DirectionClassifier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM1DirectionClassifierTest::RunTest(const FString& Parameters)
{
	const FVector Forward(1.0f, 0.0f, 0.0f);
	const FVector Right(0.0f, 1.0f, 0.0f);
	const float Threshold = 0.5f;
	const float Cone45 = 45.0f;

	using EDir = EDirectionalInput;
	TestEqual(TEXT("forward stick classifies Forward"),
		UMHGZWeaponInputRouterComponent::ClassifyDirection(FVector(1.0f, 0.0f, 0.0f), Forward, Right, 0.8f, Threshold, Cone45),
		EDir::Forward);
	TestEqual(TEXT("backward stick classifies Back"),
		UMHGZWeaponInputRouterComponent::ClassifyDirection(FVector(-1.0f, 0.0f, 0.0f), Forward, Right, 0.8f, Threshold, Cone45),
		EDir::Back);
	TestEqual(TEXT("right stick classifies Right"),
		UMHGZWeaponInputRouterComponent::ClassifyDirection(FVector(0.0f, 1.0f, 0.0f), Forward, Right, 0.8f, Threshold, Cone45),
		EDir::Right);
	TestEqual(TEXT("left stick classifies Left"),
		UMHGZWeaponInputRouterComponent::ClassifyDirection(FVector(0.0f, -1.0f, 0.0f), Forward, Right, 0.8f, Threshold, Cone45),
		EDir::Left);
	TestEqual(TEXT("character facing screen-left plus stick-left is Forward"),
		UMHGZWeaponInputRouterComponent::ClassifyDirection(
			FVector(0.0f, -1.0f, 0.0f), FVector(0.0f, -1.0f, 0.0f),
			FVector(1.0f, 0.0f, 0.0f), 0.8f, Threshold, Cone45),
		EDir::Forward);
	TestEqual(TEXT("sub-threshold stick classifies None"),
		UMHGZWeaponInputRouterComponent::ClassifyDirection(FVector(1.0f, 0.0f, 0.0f), Forward, Right, 0.4f, Threshold, Cone45),
		EDir::None);
	TestEqual(TEXT("zero world direction classifies None"),
		UMHGZWeaponInputRouterComponent::ClassifyDirection(FVector::ZeroVector, Forward, Right, 0.8f, Threshold, Cone45),
		EDir::None);
	TestEqual(TEXT("36.9 degrees from forward stays inside the 45 degree cone"),
		UMHGZWeaponInputRouterComponent::ClassifyDirection(FVector(0.8f, 0.6f, 0.0f), Forward, Right, 0.8f, Threshold, Cone45),
		EDir::Forward);
	TestEqual(TEXT("53 degrees from forward falls to the side"),
		UMHGZWeaponInputRouterComponent::ClassifyDirection(FVector(0.6f, -0.8f, 0.0f), Forward, Right, 0.8f, Threshold, Cone45),
		EDir::Left);
	TestEqual(TEXT("90 degree cone treats the whole front half as Forward"),
		UMHGZWeaponInputRouterComponent::ClassifyDirection(FVector(0.8f, 0.6f, 0.0f), Forward, Right, 0.8f, Threshold, 90.0f),
		EDir::Forward);
	return true;
}

// Completed/Canceled releases only identities registered under the releasing
// control; a chord that lost the sort never registers and can never release.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM1ReleaseIdentityTest,
	"MHGZ.M1.Input.ReleaseIdentityIsScopedToRegisteredControl",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM1ReleaseIdentityTest::RunTest(const FString& Parameters)
{
	const FGameplayTag YTag = Tag(TEXT("Input.Weapon.Y"));
	const FGameplayTag BTag = Tag(TEXT("Input.Weapon.B"));
	const FGameplayTag RTTag = Tag(TEXT("Input.Modifier.RT"));
	const FGameplayTag LTTag = Tag(TEXT("Input.Modifier.LT"));
	const FGameplayTag RTYB = Tag(TEXT("Input.Weapon.RTYB"));
	const FGameplayTag LTY = Tag(TEXT("Input.Weapon.LTY"));

	UWeaponInputProfile* Profile = NewObject<UWeaponInputProfile>();
	Profile->ChordGracePeriod = 0.25f;
	AddChord(Profile, TEXT("Input.Weapon.YB"),
		{ YTag, BTag }, {}, 0, true, true, TEXT("Input.Weapon.Y"));
	AddChord(Profile, TEXT("Input.Weapon.LTY"),
		{ YTag }, { LTTag }, 0, false, true, TEXT("Input.Weapon.Y"));
	AddChord(Profile, TEXT("Input.Weapon.RTYB"),
		{ YTag, BTag }, { RTTag }, 0, false, true, TEXT("Input.Weapon.Y"));

	UMHGZWeaponInputRouterComponent* Router = NewObject<UMHGZWeaponInputRouterComponent>();
	Router->SetInputProfile(Profile);

	Router->HandlePhysicalStarted(RTTag, 0.0);
	Router->HandlePhysicalStarted(LTTag, 0.05);
	Router->HandlePhysicalStarted(YTag, 0.1);
	Router->HandlePhysicalStarted(BTag, 0.2);

	const TArray<FWeaponInputSnapshot>& Snapshots = Router->GetCapturedSnapshots();
	TestEqual(TEXT("RTYB wins the sort; LTY is discarded, not emitted"), Snapshots.Num(), 1);
	if (Snapshots.Num() == 1)
	{
		TestEqual(TEXT("winner is RTYB"), Snapshots[0].ResolvedInputTag, RTYB);
	}

	Router->HandlePhysicalCompleted(YTag, 1.0);
	const TArray<FWeaponInputSnapshot>& AfterY = Router->GetCapturedSnapshots();
	TestEqual(TEXT("Y release emits exactly one Completed"), AfterY.Num(), 2);
	if (AfterY.Num() == 2)
	{
		TestEqual(TEXT("release identity is the registered RTYB"),
			AfterY[1].ResolvedInputTag, RTYB);
		TestEqual(TEXT("release preserves the original SequenceID"),
			AfterY[1].SequenceID, Snapshots[0].SequenceID);
		TestEqual(TEXT("release preserves the original SourceControlTag"),
			AfterY[1].SourceControlTag, Snapshots[0].SourceControlTag);
		TestEqual(TEXT("release phase is Completed"), AfterY[1].Phase, EWeaponInputPhase::Completed);
	}

	Router->HandlePhysicalCompleted(BTag, 1.1);
	Router->HandlePhysicalCompleted(RTTag, 1.2);
	Router->HandlePhysicalCompleted(LTTag, 1.3);
	TestEqual(TEXT("B/RT/LT releases emit nothing (no identity registered under them)"),
		Router->GetCapturedSnapshots().Num(), 2);
	TestTrue(TEXT("the discarded LTY never surfaced as a release"),
		Router->GetCapturedSnapshots()[1].ResolvedInputTag != LTY);

	return true;
}

// Two full formations on one router: no leaked identities, no leaked singles,
// shutdown is idempotent and late callbacks after shutdown are inert.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM1NoLeakageAcrossFormationsTest,
	"MHGZ.M1.Input.NoLeakageAcrossFormationsAndShutdown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM1NoLeakageAcrossFormationsTest::RunTest(const FString& Parameters)
{
	const FGameplayTag YTag = Tag(TEXT("Input.Weapon.Y"));
	const FGameplayTag BTag = Tag(TEXT("Input.Weapon.B"));
	const FGameplayTag RTTag = Tag(TEXT("Input.Modifier.RT"));
	const FGameplayTag RTYB = Tag(TEXT("Input.Weapon.RTYB"));
	const FGameplayTag YB = Tag(TEXT("Input.Weapon.YB"));

	UWeaponInputProfile* Profile = NewObject<UWeaponInputProfile>();
	Profile->ChordGracePeriod = 0.25f;
	AddChord(Profile, TEXT("Input.Weapon.YB"),
		{ YTag, BTag }, {}, 0, true, true, TEXT("Input.Weapon.Y"));
	AddChord(Profile, TEXT("Input.Weapon.RTYB"),
		{ YTag, BTag }, { RTTag }, 0, true, true, TEXT("Input.Modifier.RT"));

	UMHGZWeaponInputRouterComponent* Router = NewObject<UMHGZWeaponInputRouterComponent>();
	Router->SetInputProfile(Profile);

	// Formation 1: RT+Y+B -> RTYB, released via RT.
	Router->HandlePhysicalStarted(RTTag, 0.0);
	Router->HandlePhysicalStarted(YTag, 0.1);
	Router->HandlePhysicalStarted(BTag, 0.2);
	TestEqual(TEXT("formation 1 emits RTYB once"), Router->GetCapturedSnapshots().Num(), 1);
	Router->HandlePhysicalCompleted(RTTag, 1.0);
	TestEqual(TEXT("formation 1 RT release emits RTYB Completed"),
		Router->GetCapturedSnapshots().Num(), 2);
	Router->HandlePhysicalCompleted(YTag, 1.1);
	Router->HandlePhysicalCompleted(BTag, 1.2);
	TestEqual(TEXT("formation 1 Y/B releases emit nothing"),
		Router->GetCapturedSnapshots().Num(), 2);

	// Formation 2: Y+B without RT -> YB (deferred, then emitted at window close).
	Router->HandlePhysicalStarted(YTag, 1.5);
	Router->HandlePhysicalStarted(BTag, 1.6);
	Router->FlushExpiredInputs(1.75);
	TestEqual(TEXT("formation 2 emits YB at window close"),
		Router->GetCapturedSnapshots().Num(), 3);
	if (Router->GetCapturedSnapshots().Num() == 3)
	{
		TestEqual(TEXT("formation 2 output is YB"),
			Router->GetCapturedSnapshots()[2].ResolvedInputTag, YB);
	}
	Router->HandlePhysicalCompleted(YTag, 2.0);
	TestEqual(TEXT("formation 2 Y release emits YB Completed"),
		Router->GetCapturedSnapshots().Num(), 4);
	Router->HandlePhysicalCompleted(BTag, 2.1);
	TestEqual(TEXT("formation 2 B release emits nothing"),
		Router->GetCapturedSnapshots().Num(), 4);

	// Shutdown: idempotent, drops state, and late callbacks are inert.
	const TArray<FWeaponInputSnapshot> PreShutdown = Router->GetCapturedSnapshots();
	TestEqual(TEXT("pre-shutdown log holds four snapshots"), PreShutdown.Num(), 4);
	Router->ShutdownRouter();
	Router->ShutdownRouter();
	TestEqual(TEXT("shutdown clears the capture log"),
		Router->GetCapturedSnapshots().Num(), 0);
	Router->HandlePhysicalStarted(YTag, 3.0);
	Router->HandlePhysicalCompleted(YTag, 3.1);
	TestEqual(TEXT("inputs after shutdown are inert (no crash, no emission)"),
		Router->GetCapturedSnapshots().Num(), 0);
	TestEqual(TEXT("RTYB identity never leaked to formation 2"),
		PreShutdown[0].ResolvedInputTag, RTYB);
	TestEqual(TEXT("YB identity never leaked to formation 1"),
		PreShutdown[2].ResolvedInputTag, YB);

	return true;
}

// A one-member chord overrides the raw fallback single immediately.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM1OneMemberChordOverrideTest,
	"MHGZ.M1.Input.OneMemberChordOverridesRawFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM1OneMemberChordOverrideTest::RunTest(const FString& Parameters)
{
	const FGameplayTag RTTag = Tag(TEXT("Input.Modifier.RT"));
	const FGameplayTag RTWeapon = Tag(TEXT("Input.Weapon.RT"));

	UWeaponInputProfile* Profile = NewObject<UWeaponInputProfile>();
	Profile->ChordGracePeriod = 0.25f;
	AddChord(Profile, TEXT("Input.Weapon.RT"),
		{ RTTag }, {}, 0, true, true, TEXT("Input.Modifier.RT"));

	UMHGZWeaponInputRouterComponent* Router = NewObject<UMHGZWeaponInputRouterComponent>();
	Router->SetInputProfile(Profile);

	Router->HandlePhysicalStarted(RTTag, 0.0);
	const TArray<FWeaponInputSnapshot>& Snapshots = Router->GetCapturedSnapshots();
	TestEqual(TEXT("one-member chord emits immediately"), Snapshots.Num(), 1);
	if (Snapshots.Num() == 1)
	{
		TestEqual(TEXT("output is the chord tag, not the raw modifier tag"),
			Snapshots[0].ResolvedInputTag, RTWeapon);
		TestEqual(TEXT("frozen at the started moment"), Snapshots[0].Timestamp, 0.0);
	}

	Router->HandlePhysicalCompleted(RTTag, 0.5);
	TestEqual(TEXT("release emits the chord's Completed"),
		Router->GetCapturedSnapshots().Num(), 2);
	if (Router->GetCapturedSnapshots().Num() == 2)
	{
		const FWeaponInputSnapshot& Release = Router->GetCapturedSnapshots()[1];
		TestEqual(TEXT("release identity is the one-member chord"),
			Release.ResolvedInputTag, RTWeapon);
		TestEqual(TEXT("release preserves the chord SequenceID"),
			Release.SequenceID, Snapshots[0].SequenceID);
		TestEqual(TEXT("release phase is Completed"), Release.Phase, EWeaponInputPhase::Completed);
	}

	return true;
}

// Releasing inside the grace period closes the current formation. A quick
// single tap must not vanish, and a complete quick chord must beat its singles.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM1QuickReleaseTest,
	"MHGZ.M1.Input.QuickReleaseClosesGraceWindow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM1QuickReleaseTest::RunTest(const FString& Parameters)
{
	const FGameplayTag YTag = Tag(TEXT("Input.Weapon.Y"));
	const FGameplayTag BTag = Tag(TEXT("Input.Weapon.B"));
	const FGameplayTag YBTag = Tag(TEXT("Input.Weapon.YB"));
	auto MakeProfile = [YTag, BTag]()
	{
		UWeaponInputProfile* Profile = NewObject<UWeaponInputProfile>();
		Profile->ChordGracePeriod = 0.25f;
		AddChord(Profile, TEXT("Input.Weapon.YB"), { YTag, BTag }, {}, 0, true, true,
			TEXT("Input.Weapon.Y"));
		return Profile;
	};

	{
		UMHGZWeaponInputRouterComponent* Router =
			NewObject<UMHGZWeaponInputRouterComponent>();
		Router->SetInputProfile(MakeProfile());
		Router->HandlePhysicalStarted(YTag, 0.0);
		Router->HandlePhysicalCompleted(YTag, 0.05);
		const TArray<FWeaponInputSnapshot>& Snapshots = Router->GetCapturedSnapshots();
		TestEqual(TEXT("quick single emits Started and Completed"), Snapshots.Num(), 2);
		if (Snapshots.Num() == 2)
		{
			TestEqual(TEXT("quick single Started keeps Y"), Snapshots[0].ResolvedInputTag, YTag);
			TestEqual(TEXT("quick single release keeps Y"), Snapshots[1].ResolvedInputTag, YTag);
			TestEqual(TEXT("quick single release keeps identity"),
				Snapshots[1].SequenceID, Snapshots[0].SequenceID);
			TestEqual(TEXT("quick single release is Completed"),
				Snapshots[1].Phase, EWeaponInputPhase::Completed);
		}
	}

	{
		UMHGZWeaponInputRouterComponent* Router =
			NewObject<UMHGZWeaponInputRouterComponent>();
		Router->SetInputProfile(MakeProfile());
		Router->HandlePhysicalStarted(YTag, 0.0);
		Router->HandlePhysicalStarted(BTag, 0.05);
		Router->HandlePhysicalCompleted(YTag, 0.1);
		const TArray<FWeaponInputSnapshot>& Snapshots = Router->GetCapturedSnapshots();
		TestEqual(TEXT("quick chord emits only its Started and Completed"), Snapshots.Num(), 2);
		if (Snapshots.Num() == 2)
		{
			TestEqual(TEXT("quick chord wins over Y/B singles"),
				Snapshots[0].ResolvedInputTag, YBTag);
			TestEqual(TEXT("quick chord release keeps chord identity"),
				Snapshots[1].ResolvedInputTag, YBTag);
			TestEqual(TEXT("quick chord release is Completed"),
				Snapshots[1].Phase, EWeaponInputPhase::Completed);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM1InputOwnershipRebindTest,
	"MHGZ.M1.Input.OwnershipRebindIsIdempotent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM1InputOwnershipRebindTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!World)
	{
		return false;
	}
	APlayerController* PC = World->SpawnActor<APlayerController>();
	if (!PC)
	{
		World->DestroyWorld(false);
		return false;
	}

	UEnhancedInputComponent* EIC = NewObject<UEnhancedInputComponent>(PC);
	PC->AddInstanceComponent(EIC);
	EIC->RegisterComponent();
	PC->InputComponent = EIC;

	UMHGZWeaponInputRouterComponent* Router =
		NewObject<UMHGZWeaponInputRouterComponent>(PC);
	PC->AddInstanceComponent(Router);
	Router->RegisterComponent();
	UWeaponInputProfile* Profile = NewObject<UWeaponInputProfile>(Router);
	UInputAction* RawY = NewObject<UInputAction>(Profile);
	Profile->RawActionToPhysicalInputTag.Add(RawY, Tag(TEXT("Input.Weapon.Y")));
	Router->SetInputProfile(Profile);

	UMHGZInputComponent* Owner = NewObject<UMHGZInputComponent>(PC);
	PC->AddInstanceComponent(Owner);
	Owner->RegisterComponent();
	Owner->InitializeInput(PC, Router);
	TestEqual(TEXT("one raw action owns exactly four phase bindings"),
		EIC->GetActionEventBindings().Num(), 4);
	Owner->InitializeInput(PC, Router);
	TestEqual(TEXT("repeated Setup does not stack bindings"),
		EIC->GetActionEventBindings().Num(), 4);

	Owner->ShutdownInput();
	TestEqual(TEXT("UnPossess-style shutdown removes every owned binding"),
		EIC->GetActionEventBindings().Num(), 0);
	Router->AttachToPawn(nullptr);
	Owner->InitializeInput(PC, Router);
	TestEqual(TEXT("re-Possess rebuilds one clean binding set"),
		EIC->GetActionEventBindings().Num(), 4);
	Owner->ShutdownInput();
	Router->ShutdownRouter();
	TestEqual(TEXT("final teardown leaves no binding"),
		EIC->GetActionEventBindings().Num(), 0);

	World->DestroyWorld(false);
	return true;
}

#endif
