// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include <limits>

#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "ActionSystem/MHGZAdvancingCounterAbility.h"
#include "ActionSystem/MHGZBackVaultAbility.h"
#include "ActionSystem/MHGZDodgeAbility.h"
#include "Curves/CurveVector.h"
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

/**
 * Guards the free-fall hand-off contract.
 *
 * The task must never read the hand-off velocity back from CMC->Velocity: the CMC
 * removes a finished MoveToForce source on its own movement update, so by the time
 * the task observes completion the velocity has already collapsed to the source's
 * final partial-frame remainder.  Measured in Saved/RuntimeTelemetry, that cost the
 * back-vault two thirds of its vertical speed at every single hand-off.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5CurvedVaultHandoffTest,
	"MHGZ.M5.Movement.CurvedVaultHandoffTangent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5CurvedVaultHandoffTest::RunTest(const FString& Parameters)
{
	// The recorded keys are linearly interpolated, so a linear recording has an
	// unambiguous tangent independent of how the samples are spaced.
	constexpr float StartProgress = 0.2f;
	constexpr float HandoffProgress = 0.8f;
	constexpr float TotalDistance = 1000.0f;
	constexpr float ApexHeight = 500.0f;
	constexpr float CurveDuration = 2.0f;

	TArray<FBackVaultTrajectoryKey> Keys;
	for (const float Progress : { 0.0f, 0.2f, 0.5f, 0.8f, 1.0f })
	{
		FBackVaultTrajectoryKey Key;
		Key.Time = Progress;
		// Forward 1 and down 1 per unit of recorded progress.
		Key.NormalizedPosition = FVector(Progress, 0.0f, -Progress);
		Keys.Add(Key);
	}

	FVector Velocity;
	TestTrue(TEXT("linear recording yields a tangent"),
		UMHGZBackVaultAbility::ComputeTrajectoryTangent(Keys, StartProgress,
			HandoffProgress, TotalDistance, ApexHeight, CurveDuration, Velocity));

	// The curve's normalised time spans ProgressSpan of recorded progress and the
	// source advances it once over CurveDuration, so the speed scale is
	// ProgressSpan / CurveDuration = 0.6 / 2.0.
	constexpr float ExpectedScale = 0.3f;
	TestEqual(TEXT("forward component follows the recorded slope"),
		static_cast<float>(Velocity.X), TotalDistance * ExpectedScale, 0.01f);
	TestEqual(TEXT("vertical component follows the recorded slope"),
		static_cast<float>(Velocity.Z), -ApexHeight * ExpectedScale, 0.01f);
	TestEqual(TEXT("lateral component stays zero"),
		static_cast<float>(Velocity.Y), 0.0f, 0.01f);

	// A descending path must hand off a descending velocity; the sign is what the
	// regression actually lost.
	TestTrue(TEXT("a descending hand-off produces a descending tangent"), Velocity.Z < 0.0);

	// Degenerate inputs must refuse rather than hand the CMC a silent zero.
	FVector Rejected;
	TestFalse(TEXT("empty recording is rejected"),
		UMHGZBackVaultAbility::ComputeTrajectoryTangent(TArray<FBackVaultTrajectoryKey>(),
			StartProgress, HandoffProgress, TotalDistance, ApexHeight, CurveDuration, Rejected));
	TestFalse(TEXT("zero travel distance is rejected"),
		UMHGZBackVaultAbility::ComputeTrajectoryTangent(Keys, StartProgress,
			HandoffProgress, 0.0f, ApexHeight, CurveDuration, Rejected));
	TestFalse(TEXT("zero duration is rejected"),
		UMHGZBackVaultAbility::ComputeTrajectoryTangent(Keys, StartProgress,
			HandoffProgress, TotalDistance, ApexHeight, 0.0f, Rejected));
	TestFalse(TEXT("an inverted progress range is rejected"),
		UMHGZBackVaultAbility::ComputeTrajectoryTangent(Keys, HandoffProgress,
			StartProgress, TotalDistance, ApexHeight, CurveDuration, Rejected));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5CurvedVaultRequestValidationTest,
	"MHGZ.M5.Movement.CurvedVaultRequiresAuthoredTangent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5CurvedVaultRequestValidationTest::RunTest(const FString& Parameters)
{
	FWeaponMovementRequest Request;
	Request.Mode = EWeaponMovementMode::CurvedVault;
	Request.Duration = 1.08333f;
	Request.MaxDistance = 318.8f;
	Request.PathOffsetCurve = NewObject<UCurveVector>();

	// Exactly the state the back-vault used to launch in: a valid curve with no
	// authored tangent, which is how the collapsed hand-off reached the capsule.
	TestFalse(TEXT("CurvedVault without an authored tangent is rejected"),
		Request.HasValidCurvedVaultParameters());

	Request.CurvedVaultHandoffVelocityLocal = FVector(100.0f, 0.0f, -800.0f);
	TestTrue(TEXT("CurvedVault with an authored tangent is accepted"),
		Request.HasValidCurvedVaultParameters());

	Request.PathOffsetCurve = nullptr;
	TestFalse(TEXT("CurvedVault still requires a path offset curve"),
		Request.HasValidCurvedVaultParameters());

	// Other modes must not be affected by the CurvedVault-only requirement.
	Request.Mode = EWeaponMovementMode::BoundedDirectional;
	TestTrue(TEXT("other modes ignore the CurvedVault tangent requirement"),
		Request.HasValidCurvedVaultParameters());
	return true;
}

// The dance vault is a JumpForce arc whose parabola returns to the launch height
// exactly at f = 1, so it always touches down on the frame the source completes.
// The old classifier accepted only `Completed`, which made every touchdown fall
// through to "no hand-off at all" -- no free fall, no landing. This pins the fix.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5DanceTouchdownClaimsLandingTest,
	"MHGZ.M5.Vault.DanceTouchdownClaimsLandingWithoutFreeFall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5DanceTouchdownClaimsLandingTest::RunTest(const FString& Parameters)
{
	using EVaultExit = UMHGZAdvancingCounterAbility::EVaultExit;
	auto Exit = [](EWeaponMovementEndReason Reason, bool bFalling)
	{
		return UMHGZAdvancingCounterAbility::ResolveVaultExit(Reason, bFalling);
	};

	// The regression itself: a touchdown must claim the landing pose, and the
	// answer must not depend on the CMC mode -- Landed is broadcast from inside
	// ProcessLanded, before SetPostLandedPhysics flips it out of MOVE_Falling.
	TestEqual(TEXT("Landed while still in MOVE_Falling claims the landing pose"),
		Exit(EWeaponMovementEndReason::Landed, true), EVaultExit::LandedPresentation);
	TestEqual(TEXT("Landed after the CMC already grounded claims the landing pose"),
		Exit(EWeaponMovementEndReason::Landed, false), EVaultExit::LandedPresentation);

	// A touchdown is never silently dropped, whichever way the arc reported it.
	TestNotEqual(TEXT("Completed while grounded is not dropped"),
		Exit(EWeaponMovementEndReason::Completed, false), EVaultExit::None);

	// The airborne completion branch must survive this change untouched: it is
	// the shape the back vault relies on.
	TestEqual(TEXT("Completed while airborne still hands off to free fall"),
		Exit(EWeaponMovementEndReason::Completed, true), EVaultExit::FreeFall);

	// Everything that never reached the ground must hand over nothing.
	const EWeaponMovementEndReason NeverGrounded[] = {
		EWeaponMovementEndReason::HitHitzone,
		EWeaponMovementEndReason::BlockingHit,
		EWeaponMovementEndReason::Interrupted,
		EWeaponMovementEndReason::Cancelled,
		EWeaponMovementEndReason::Death,
		EWeaponMovementEndReason::WeaponChanged,
		EWeaponMovementEndReason::RuntimeShutdown,
		EWeaponMovementEndReason::Failed,
	};
	for (const EWeaponMovementEndReason Reason : NeverGrounded)
	{
		TestEqual(TEXT("a move that never reached the ground hands over nothing"),
			Exit(Reason, true), EVaultExit::None);
		TestEqual(TEXT("a move that never reached the ground hands over nothing"),
			Exit(Reason, false), EVaultExit::None);
	}

	// Totality: every reason crossed with both CMC modes resolves to exactly one
	// of the three declared outcomes.
	const EWeaponMovementEndReason AllReasons[] = {
		EWeaponMovementEndReason::Completed,
		EWeaponMovementEndReason::HitHitzone,
		EWeaponMovementEndReason::BlockingHit,
		EWeaponMovementEndReason::Landed,
		EWeaponMovementEndReason::Interrupted,
		EWeaponMovementEndReason::Cancelled,
		EWeaponMovementEndReason::Death,
		EWeaponMovementEndReason::WeaponChanged,
		EWeaponMovementEndReason::RuntimeShutdown,
		EWeaponMovementEndReason::Failed,
	};
	for (const EWeaponMovementEndReason Reason : AllReasons)
	{
		for (const bool bFalling : { false, true })
		{
			const EVaultExit Result = Exit(Reason, bFalling);
			const bool bKnown = Result == EVaultExit::None
				|| Result == EVaultExit::FreeFall
				|| Result == EVaultExit::LandedPresentation;
			TestTrue(TEXT("every end reason resolves to a declared outcome"), bKnown);
		}
	}
	return true;
}

// Claiming a landing pose must never be a back door into the free-fall state,
// and the ordinary touchdown cleanup must still complete on a move that never
// entered it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5DanceTouchdownKeepsHostCleanupTest,
	"MHGZ.M5.Vault.DanceTouchdownKeepsHostCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5DanceTouchdownKeepsHostCleanupTest::RunTest(const FString& Parameters)
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

	UMHGZAbilitySystemComponent* ASC = NewObject<UMHGZAbilitySystemComponent>(Character);
	Character->AddInstanceComponent(ASC);
	ASC->RegisterComponent();
	UMHGZWeaponRuntimeHostComponent* Host =
		NewObject<UMHGZWeaponRuntimeHostComponent>(Character);
	Character->AddInstanceComponent(Host);
	Host->RegisterComponent();
	Host->InitializePawnRuntime(Character, nullptr, ASC, nullptr);

	Host->SetGrounded(false);
	TestFalse(TEXT("the vault never enters the system-owned free-fall state"),
		Host->IsAerialFalling());

	// The harness spawns a bare ACharacter with no skeletal mesh asset, so no
	// landing montage can actually start here. The invariant under test is the
	// one that matters: claiming a landing pose must not open a free fall.
	Host->PlayAerialLandingPresentation();
	TestFalse(TEXT("claiming a landing pose does not open a free fall"),
		Host->IsAerialFalling());

	// The touchdown cleanup must still run end to end on a move that skipped the
	// free-fall state entirely.
	Host->HandleLanded();
	TestTrue(TEXT("touchdown lands the host"), Host->IsGrounded());
	TestFalse(TEXT("touchdown leaves no free-fall state behind"),
		Host->IsAerialFalling());
	TestFalse(TEXT("touchdown leaves no landing state behind"),
		Host->IsAerialLanding());

	World->DestroyWorld(false);
	return true;
}

// The JumpOver clip's root track must be locked and NOT extracted.  Neither
// flag set is the A9 defect (the lead lives in the pose and snaps back in two
// frames); both set is worse, because anim root motion pre-empts the CurvedVault
// source and the analytic path dies. Only "locked, not extracted" is usable.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5BackVaultRootTrackPolicyTest,
	"MHGZ.M5.Vault.BackVaultRootTrackPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5BackVaultRootTrackPolicyTest::RunTest(const FString& Parameters)
{
	using EPolicy = EBackVaultRootTrackPolicy;
	auto Policy = [](bool bEnableRootMotion, bool bForceRootLock)
	{
		return UMHGZBackVaultAbility::ResolveRootTrackPolicy(bEnableRootMotion, bForceRootLock);
	};

	TestEqual(TEXT("locked and not extracted is the JumpOver policy"),
		Policy(false, true), EPolicy::LockedNotExtracted);
	TestNotEqual(TEXT("neither flag set is the dangling A9 state"),
		Policy(false, false), EPolicy::LockedNotExtracted);
	TestEqual(TEXT("neither flag set is reported as unaccounted"),
		Policy(false, false), EPolicy::Unaccounted);
	TestEqual(TEXT("both flags set is a conflict, not merely wrong"),
		Policy(true, true), EPolicy::Conflicting);
	TestEqual(TEXT("extracted and unlocked is the Jump phase policy"),
		Policy(true, false), EPolicy::Extracted);

	// Totality over the four input pairs: exactly one outcome each, and only one
	// of them is acceptable for JumpOver.
	int32 LockedCount = 0;
	for (const bool bEnableRootMotion : { false, true })
	{
		for (const bool bForceRootLock : { false, true })
		{
			const EPolicy Result = Policy(bEnableRootMotion, bForceRootLock);
			const bool bKnown = Result == EPolicy::Unaccounted
				|| Result == EPolicy::LockedNotExtracted
				|| Result == EPolicy::Extracted || Result == EPolicy::Conflicting;
			TestTrue(TEXT("every flag pair resolves to a declared policy"), bKnown);
			if (Result == EPolicy::LockedNotExtracted)
			{
				++LockedCount;
			}
		}
	}
	TestEqual(TEXT("exactly one flag pair is acceptable for JumpOver"), LockedCount, 1);
	return true;
}

// The recovered forward lead is folded into the trajectory curve.  Its shape is
// load-bearing: a non-zero terminal slope would leave the capsule's real exit
// velocity different from the analytic tangent the free fall is launched with,
// which is the exact failure ComputeTrajectoryTangent exists to prevent.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5BackVaultClipDriftRampTest,
	"MHGZ.M5.Vault.BackVaultClipDriftRamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5BackVaultClipDriftRampTest::RunTest(const FString& Parameters)
{
	using FKey = FBackVaultClipDriftKey;
	auto Sample = [](const TArray<FKey>& Table, float Time, float& Out)
	{
		return UMHGZBackVaultAbility::SampleClipDriftForwardFraction(Table, Time, Out);
	};
	auto Make = [](std::initializer_list<FKey> Keys)
	{
		TArray<FKey> Table;
		for (const FKey& Key : Keys)
		{
			Table.Add(Key);
		}
		return Table;
	};
	auto Key = [](float Time, float Fraction)
	{
		FKey Out;
		Out.CurveTime = Time;
		Out.ForwardFraction = Fraction;
		return Out;
	};

	// An empty table is a legitimate configuration: nothing to recover.
	float Fraction = 1.0f;
	TestTrue(TEXT("an empty drift table is accepted"),
		Sample(TArray<FKey>(), 0.5f, Fraction));
	TestEqual(TEXT("an empty drift table recovers nothing"), Fraction, 0.0f);

	const TArray<FKey> Good = Make({ Key(0.0f, 0.0f), Key(0.5f, 0.02f), Key(0.8f, 0.04f),
		Key(1.0f, 0.04f) });
	TestTrue(TEXT("a flat-terminal ramp is accepted"), Sample(Good, 0.5f, Fraction));
	TestEqual(TEXT("the ramp interpolates linearly"), Fraction, 0.02f);
	TestTrue(TEXT("the ramp reaches its terminal value"), Sample(Good, 1.0f, Fraction));
	TestEqual(TEXT("the terminal value is the table's"), Fraction, 0.04f);
	TestTrue(TEXT("the ramp is zero at the window start"), Sample(Good, 0.0f, Fraction));
	TestEqual(TEXT("the ramp starts at zero"), Fraction, 0.0f);

	// A first key with a head start would step the capsule at the window start.
	TestFalse(TEXT("a non-zero first key is rejected"),
		Sample(Make({ Key(0.1f, 0.01f), Key(1.0f, 0.04f) }), 0.5f, Fraction));
	// Not reaching the window end would step it at the handoff instead.
	TestFalse(TEXT("a table that stops short of 1.0 is rejected"),
		Sample(Make({ Key(0.0f, 0.0f), Key(0.8f, 0.04f) }), 0.5f, Fraction));
	// The slope that would desynchronise the free-fall launch.
	TestFalse(TEXT("a non-zero terminal slope is rejected"),
		Sample(Make({ Key(0.0f, 0.0f), Key(0.5f, 0.03f), Key(1.0f, 0.05f) }), 0.5f, Fraction));
	TestFalse(TEXT("a decreasing table is rejected"),
		Sample(Make({ Key(0.0f, 0.0f), Key(0.5f, 0.03f), Key(1.0f, 0.01f) }), 0.5f, Fraction));
	TestFalse(TEXT("a single key is rejected"),
		Sample(Make({ Key(0.0f, 0.0f) }), 0.5f, Fraction));
	TestFalse(TEXT("a NaN fraction is rejected"),
		Sample(Make({ Key(0.0f, 0.0f), Key(1.0f, std::numeric_limits<float>::quiet_NaN()) }),
			0.5f, Fraction));
	TestFalse(TEXT("a NaN sample time is rejected"), Sample(Good,
		std::numeric_limits<float>::quiet_NaN(), Fraction));
	return true;
}

#endif
