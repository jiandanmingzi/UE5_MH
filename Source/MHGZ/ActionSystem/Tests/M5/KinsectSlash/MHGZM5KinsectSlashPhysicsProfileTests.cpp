// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include <limits>

#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "InsectGlaive/InsectGlaiveCombatConfig.h"
#include "MHGZPlayerState.h"
#include "../../Shared/MHGZM5KinsectSlashTestTypes.h"
#include "Movement/MHGZInstrumentedCharacterMovementComponent.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

namespace
{
struct FKinsectSlashPhysicsHarness
{
	UWorld* World = nullptr;
	AMHGZM5KinsectSlashMovementTestCharacter* Character = nullptr;
	AMHGZPlayerState* PlayerState = nullptr;
	UMHGZAbilitySystemComponent* ASC = nullptr;
	UMHGZWeaponRuntimeHostComponent* Host = nullptr;
	UMHGZInstrumentedCharacterMovementComponent* CMC = nullptr;
	UInsectGlaiveCombatConfig* Config = nullptr;
	FWeaponActionToken Owner;

	bool Build()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false);
		if (!World)
		{
			return false;
		}
		Character = World->SpawnActor<AMHGZM5KinsectSlashMovementTestCharacter>(
			FVector(0.0, 0.0, 1000.0), FRotator::ZeroRotator);
		PlayerState = World->SpawnActor<AMHGZPlayerState>();
		if (!Character || !PlayerState)
		{
			return false;
		}
		Character->SetPlayerState(PlayerState);
		ASC = PlayerState->GetMHGZAbilitySystemComponent();
		CMC = Cast<UMHGZInstrumentedCharacterMovementComponent>(
			Character->GetCharacterMovement());
		if (!ASC || !CMC)
		{
			return false;
		}
		ASC->InitAbilityActorInfo(PlayerState, Character);
		ASC->InitializeAbilitySystem();
		Host = NewObject<UMHGZWeaponRuntimeHostComponent>(Character);
		Character->AddInstanceComponent(Host);
		Host->RegisterComponent();
		Host->InitializePawnRuntime(Character, nullptr, ASC,
			PlayerState->GetEquipmentComponent());
		Config = NewObject<UInsectGlaiveCombatConfig>(Host);
		Host->SetCombatConfigForTest(Config);

		// Use the real registration gate, without starting an unrelated movement
		// task: these tests exercise the Host/CMC fall profile after RMS retirement.
		Owner.RuntimeToken = Host->GetCurrentToken();
		Owner.AbilityHandle = ASC->GiveAbility(FGameplayAbilitySpec(
			UMHGZM5KinsectSlashMovementTestAbility::StaticClass(), 1, INDEX_NONE, ASC));
		Owner.ActivationSequenceID = Host->AllocateActivationSequenceID();
		Owner.AbilityInstance = NewObject<UMHGZM5KinsectSlashMovementTestAbility>(ASC);
		if (!Host->RegisterAction(Owner))
		{
			return false;
		}

		CMC->bRunPhysicsWithNoController = true;
		CMC->AirControl = 1.0f;
		CMC->MaxWalkSpeed = 20.0f; // the authored backward speed must exceed this cap
		CMC->SetMovementMode(MOVE_Falling);
		CMC->GravityScale = 1.0f;
		CMC->BrakingDecelerationFalling = 80.0f;
		CMC->FallingLateralFriction = 0.37f;
		return CMC->HasValidData() && !Host->IsGrounded();
	}

	~FKinsectSlashPhysicsHarness()
	{
		if (Host)
		{
			Host->ShutdownRuntime(EWeaponRuntimeEndReason::RuntimeShutdown);
		}
		if (World)
		{
			World->DestroyWorld(false);
		}
	}
};

struct FScopedKinsectSlashTelemetry
{
	IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(
		TEXT("mhgz.Telemetry.Enable"));
	int32 PreviousValue = Variable ? Variable->GetInt() : 0;

	void Set(const bool bEnabled) const
	{
		if (Variable)
		{
			Variable->Set(bEnabled ? 1 : 0, ECVF_SetByCode);
		}
	}

	~FScopedKinsectSlashTelemetry()
	{
		if (Variable)
		{
			Variable->Set(PreviousValue, ECVF_SetByCode);
		}
	}
};

bool HasOriginalPhysics(const UMHGZInstrumentedCharacterMovementComponent& CMC)
{
	return FMath::IsNearlyEqual(CMC.GravityScale, 1.0f)
		&& FMath::IsNearlyEqual(CMC.BrakingDecelerationFalling, 80.0f)
		&& FMath::IsNearlyEqual(CMC.FallingLateralFriction, 0.37f)
		&& CMC.GetAerialPlanarAcceleration().IsZero();
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5KinsectSlashFallingProfileTrajectoryTest,
	"MHGZ.M5.KinsectSlash.PhysicsProfile.FallingTrajectoryFrameRates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5KinsectSlashFallingProfileTrajectoryTest::RunTest(const FString& Parameters)
{
	FScopedKinsectSlashTelemetry Telemetry;
	if (!TestNotNull(TEXT("runtime telemetry toggle exists"), Telemetry.Variable))
	{
		return false;
	}
	for (const bool bTelemetryEnabled : { false, true })
	{
		Telemetry.Set(bTelemetryEnabled);
		for (const int32 FramesPerSecond : { 30, 60, 120 })
		{
			for (const bool bWithAirInput : { false, true })
			{
				FKinsectSlashPhysicsHarness H;
				if (!TestTrue(TEXT("real Host and instrumented CMC harness builds"), H.Build()))
				{
					return false;
				}
				H.Character->SetActorEnableCollision(false);
				if (!TestTrue(TEXT("dance falling profile installs"),
					H.Host->ApplyAerialFallingPhysicsOverride(H.Owner, 1800.0f, 0.0f,
						FVector(-360.0, 0.0, 0.0))))
				{
					return false;
				}
				const FVector Start = H.Character->GetActorLocation();
				H.CMC->Velocity = FVector(-300.0, 0.0, 1080.0);
				const float Step = 1.0f / FramesPerSecond;
				const int32 Steps = FMath::RoundToInt(0.6f * FramesPerSecond);
				for (int32 Frame = 0; Frame < Steps; ++Frame)
				{
					if (bWithAirInput)
					{
						H.CMC->AddInputVector(FVector(1.0, 1.0, 0.0));
					}
					H.CMC->TickComponent(Step, LEVELTICK_All, nullptr);
				}
				const FVector Displacement = H.Character->GetActorLocation() - Start;
				const FString Case = FString::Printf(TEXT("%d FPS telemetry=%d input=%d"),
					FramesPerSecond, bTelemetryEnabled, bWithAirInput);
				TestTrue(Case + TEXT(" height at 0.6s is 324 cm"),
					FMath::IsNearlyEqual(Displacement.Z, 324.0, 0.15));
				TestTrue(Case + TEXT(" backward displacement is 244.8 cm"),
					FMath::IsNearlyEqual(Displacement.X, -244.8, 0.15));
				TestTrue(Case + TEXT(" planar input does not alter the authored trajectory"),
					FMath::IsNearlyZero(Displacement.Y, 0.01));
				TestTrue(Case + TEXT(" final backward velocity is -516 cm/s"),
					FMath::IsNearlyEqual(H.CMC->Velocity.X, -516.0, 0.05));
				TestTrue(Case + TEXT(" vertical velocity reaches the apex"),
					FMath::IsNearlyZero(H.CMC->Velocity.Z, 0.05));
				TestEqual(Case + TEXT(" CMC remains falling"), H.CMC->MovementMode, MOVE_Falling);
			}
		}
	}
	for (const int32 FramesPerSecond : { 30, 60, 120 })
	{
		FKinsectSlashPhysicsHarness H;
		if (!TestTrue(TEXT("miss falling harness builds"), H.Build())
			|| !TestTrue(TEXT("3096 cm/s^2 miss profile installs without planar acceleration"),
				H.Host->ApplyAerialFallingPhysicsOverride(H.Owner, 3096.0f, 0.0f)))
		{
			return false;
		}
		H.Character->SetActorEnableCollision(false);
		const FVector Start = H.Character->GetActorLocation();
		H.CMC->Velocity = FVector(-300.0, 0.0, 0.0);
		const float Step = 1.0f / FramesPerSecond;
		const int32 Steps = FMath::RoundToInt(0.6f * FramesPerSecond);
		for (int32 Frame = 0; Frame < Steps; ++Frame)
		{
			H.CMC->PerformMovement(Step);
		}
		const FVector Displacement = H.Character->GetActorLocation() - Start;
		const FString Case = FString::Printf(TEXT("miss %d FPS"), FramesPerSecond);
		TestTrue(Case + TEXT(" retains horizontal inertia"),
			FMath::IsNearlyEqual(Displacement.X, -180.0, 0.15));
		TestTrue(Case + TEXT(" vertical displacement follows 3096 cm/s^2"),
			FMath::IsNearlyEqual(Displacement.Z, -557.28, 0.15));
		TestTrue(Case + TEXT(" vertical velocity follows 3096 cm/s^2"),
			FMath::IsNearlyEqual(H.CMC->Velocity.Z, -1857.6, 0.05));
		TestTrue(Case + TEXT(" has no special planar acceleration"),
			H.CMC->GetAerialPlanarAcceleration().IsZero());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5KinsectSlashFallingProfileTakeoverTest,
	"MHGZ.M5.KinsectSlash.PhysicsProfile.NewActionTakeoverClearsAcceleration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5KinsectSlashFallingProfileTakeoverTest::RunTest(const FString& Parameters)
{
	FKinsectSlashPhysicsHarness H;
	if (!TestTrue(TEXT("takeover harness builds"), H.Build())
		|| !TestTrue(TEXT("old action installs its falling acceleration"),
			H.Host->ApplyAerialFallingPhysicsOverride(H.Owner, 1800.0f, 0.0f,
				FVector(-360.0, 0.0, 0.0))))
	{
		return false;
	}
	const float ExistingScale = H.CMC->GravityScale;
	const FVector ExistingAcceleration = H.CMC->GetAerialPlanarAcceleration();
	H.CMC->Velocity = FVector(-300.0, 0.0, 1080.0);
	const FVector ExistingVelocity = H.CMC->Velocity;
	TestTrue(TEXT("same action can register again for source replacement"),
		H.Host->RegisterAction(H.Owner));
	TestTrue(TEXT("same-token registration keeps its special acceleration"),
		H.CMC->GetAerialPlanarAcceleration().Equals(ExistingAcceleration));
	FWeaponActionToken Stale = H.Owner;
	++Stale.RuntimeToken.Generation;
	TestFalse(TEXT("stale registration cannot take over the profile"),
		H.Host->RegisterAction(Stale));
	TestFalse(TEXT("invalid registration cannot take over the profile"),
		H.Host->RegisterAction(FWeaponActionToken()));
	TestTrue(TEXT("rejected registration leaves the active acceleration unchanged"),
		H.CMC->GetAerialPlanarAcceleration().Equals(ExistingAcceleration));

	FWeaponActionToken Next = H.Owner;
	Next.AbilityHandle = H.ASC->GiveAbility(FGameplayAbilitySpec(
		UMHGZM5KinsectSlashMovementTestAbility::StaticClass(), 1, INDEX_NONE, H.ASC));
	Next.AbilityInstance = NewObject<UMHGZM5KinsectSlashMovementTestAbility>(H.ASC);
	Next.ActivationSequenceID = H.Host->AllocateActivationSequenceID();
	TestTrue(TEXT("a different registered action takes over through the universal Host seam"),
		H.Host->RegisterAction(Next));
	TestTrue(TEXT("cross-action registration clears special acceleration without IG notifications"),
		H.CMC->GetAerialPlanarAcceleration().IsZero());
	TestTrue(TEXT("cross-action registration keeps gravity, braking, friction and velocity"),
		FMath::IsNearlyEqual(H.CMC->GravityScale, ExistingScale)
		&& FMath::IsNearlyZero(H.CMC->BrakingDecelerationFalling)
		&& FMath::IsNearlyZero(H.CMC->FallingLateralFriction)
		&& H.CMC->Velocity.Equals(ExistingVelocity));
	H.Host->UnregisterAction(H.Owner);
	TestTrue(TEXT("old action retirement cannot reintroduce its acceleration"),
		H.CMC->GetAerialPlanarAcceleration().IsZero());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5KinsectSlashFallingProfileOwnershipTest,
	"MHGZ.M5.KinsectSlash.PhysicsProfile.SnapshotAndOwnershipLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5KinsectSlashFallingProfileOwnershipTest::RunTest(const FString& Parameters)
{
	FKinsectSlashPhysicsHarness H;
	if (!TestTrue(TEXT("physics harness builds"), H.Build()))
	{
		return false;
	}
	const FVector EntryVelocity(-300.0, 17.0, 1080.0);
	H.CMC->Velocity = EntryVelocity;
	TestTrue(TEXT("ordinary profile can capture the original values first"),
		H.Host->ApplyAerialFallingProfile(false));
	TestTrue(TEXT("custom dance profile installs after the ordinary profile"),
		H.Host->ApplyAerialFallingPhysicsOverride(H.Owner, 1800.0f, 0.0f,
			FVector(-360.0, 0.0, 0.0)));
	TestTrue(TEXT("a repeated profile uses unscaled world gravity"),
		H.Host->ApplyAerialFallingPhysicsOverride(H.Owner, 3096.0f, 0.0f));
	TestTrue(TEXT("miss gravity is 3096 cm/s^2, not divided by the previous scale"),
		FMath::IsNearlyEqual(FMath::Abs(H.CMC->GetGravityZ()), 3096.0f, 0.01f));
	TestTrue(TEXT("zero-acceleration miss profile clears the dance acceleration"),
		H.CMC->GetAerialPlanarAcceleration().IsZero());
	TestTrue(TEXT("special profile zeroes falling lateral friction"),
		FMath::IsNearlyZero(H.CMC->FallingLateralFriction));
	TestTrue(TEXT("applying profiles does not change entry velocity"),
		H.CMC->Velocity.Equals(EntryVelocity, 0.01));

	TestTrue(TEXT("dance profile can be reinstalled without resnapshotting"),
		H.Host->ApplyAerialFallingPhysicsOverride(H.Owner, 1800.0f, 0.0f,
			FVector(-360.0, 0.0, 0.0)));
	FName WarpTarget;
	TestTrue(TEXT("registered action acquires movement ownership"),
		H.Host->AcquireActionMovement(H.Owner, WarpTarget));
	TestTrue(TEXT("movement ownership releases normally"),
		H.Host->ReleaseActionMovement(H.Owner));
	TestTrue(TEXT("normal action retirement unregisters its token"),
		H.Host->UnregisterAction(H.Owner));
	TestTrue(TEXT("movement and action retirement preserve the Host falling profile"),
		FMath::IsNearlyEqual(FMath::Abs(H.CMC->GetGravityZ()), 1800.0f, 0.01f)
		&& H.CMC->GetAerialPlanarAcceleration().Equals(FVector(-360.0, 0.0, 0.0)));

	const float PreservedScale = H.CMC->GravityScale;
	TestFalse(TEXT("preserved fall presentation rejects a missing montage/anim instance"),
		H.Host->BeginAerialFalling(false, FGameplayTag(), nullptr, true));
	TestTrue(TEXT("missing presentation leaves the existing profile and velocity intact"),
		FMath::IsNearlyEqual(H.CMC->GravityScale, PreservedScale)
		&& FMath::IsNearlyZero(H.CMC->BrakingDecelerationFalling)
		&& FMath::IsNearlyZero(H.CMC->FallingLateralFriction)
		&& H.CMC->GetAerialPlanarAcceleration().Equals(FVector(-360.0, 0.0, 0.0))
		&& H.CMC->Velocity.Equals(EntryVelocity, 0.01));
	TestFalse(TEXT("a retired action cannot replace the preserved profile"),
		H.Host->ApplyAerialFallingPhysicsOverride(H.Owner, 3096.0f, 0.0f));

	H.Host->ReleaseAerialFallingState();
	TestTrue(TEXT("new action lead clears special acceleration while keeping gravity"),
		H.CMC->GetAerialPlanarAcceleration().IsZero()
		&& FMath::IsNearlyEqual(H.CMC->GravityScale, PreservedScale));
	H.Host->HandleLanded();
	TestTrue(TEXT("landing restores the original 1/80/friction snapshot, not an intermediate profile"),
		HasOriginalPhysics(*H.CMC));
	H.Host->HandleLanded();
	TestTrue(TEXT("duplicate landing cleanup is idempotent"), HasOriginalPhysics(*H.CMC));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5KinsectSlashFallingProfileCleanupTest,
	"MHGZ.M5.KinsectSlash.PhysicsProfile.OrdinaryProfileAndTeardownCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5KinsectSlashFallingProfileCleanupTest::RunTest(const FString& Parameters)
{
	for (const bool bWhiteVariant : { false, true })
	{
		FKinsectSlashPhysicsHarness H;
		if (!TestTrue(TEXT("cleanup harness builds"), H.Build()))
		{
			return false;
		}
		TestTrue(TEXT("custom profile installs before ordinary handover"),
			H.Host->ApplyAerialFallingPhysicsOverride(H.Owner, 1800.0f, 0.0f,
				FVector(-360.0, 0.0, 0.0)));
		TestTrue(TEXT("ordinary profile installs"),
			H.Host->ApplyAerialFallingProfile(bWhiteVariant));
		TestTrue(TEXT("ordinary profiles clear custom acceleration"),
			H.CMC->GetAerialPlanarAcceleration().IsZero());
		TestTrue(TEXT("ordinary profiles keep their configured gravity"),
			FMath::IsNearlyEqual(H.CMC->GravityScale, bWhiteVariant
				? H.Config->WhiteAerialFallGravityScale : H.Config->AerialFallGravityScale));
		TestTrue(TEXT("ordinary profile restores its original lateral friction"),
			FMath::IsNearlyEqual(H.CMC->FallingLateralFriction, 0.37f));
		TestTrue(TEXT("custom profile can replace the ordinary profile again"),
			H.Host->ApplyAerialFallingPhysicsOverride(H.Owner, 3096.0f, 0.0f,
				FVector(-360.0, 0.0, 0.0)));
		H.Host->ShutdownRuntime(EWeaponRuntimeEndReason::RuntimeShutdown);
		TestTrue(TEXT("runtime teardown restores all original physics and clears acceleration"),
			HasOriginalPhysics(*H.CMC));
	}

	// Cleanup must clear an acceleration even when no Host gravity snapshot exists.
	FKinsectSlashPhysicsHarness H;
	if (!TestTrue(TEXT("no-snapshot cleanup harness builds"), H.Build()))
	{
		return false;
	}
	H.CMC->SetAerialPlanarAcceleration(FVector(-360.0, 0.0, 0.0));
	H.Host->HandleLanded();
	TestTrue(TEXT("landing clears special acceleration without a physics snapshot"),
		HasOriginalPhysics(*H.CMC));
	H.CMC->SetAerialPlanarAcceleration(FVector(-360.0, 0.0, 0.0));
	H.Host->ShutdownRuntime(EWeaponRuntimeEndReason::RuntimeShutdown);
	TestTrue(TEXT("teardown also clears special acceleration without a physics snapshot"),
		HasOriginalPhysics(*H.CMC));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5KinsectSlashFallingProfileValidationTest,
	"MHGZ.M5.KinsectSlash.PhysicsProfile.ValidationIsAtomic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5KinsectSlashFallingProfileValidationTest::RunTest(const FString& Parameters)
{
	FKinsectSlashPhysicsHarness H;
	if (!TestTrue(TEXT("validation harness builds"), H.Build()))
	{
		return false;
	}
	TestFalse(TEXT("preserved fall requires an existing Host physics override"),
		H.Host->BeginAerialFalling(false, FGameplayTag(), nullptr, true));
	const float NaN = std::numeric_limits<float>::quiet_NaN();
	const float Infinity = std::numeric_limits<float>::infinity();
	for (const float InvalidGravity : { 0.0f, -1800.0f, NaN, Infinity })
	{
		TestFalse(TEXT("gravity must be positive and finite"),
			H.Host->ApplyAerialFallingPhysicsOverride(H.Owner, InvalidGravity, 0.0f));
	}
	for (const float InvalidBraking : { -1.0f, NaN, Infinity })
	{
		TestFalse(TEXT("braking must be nonnegative and finite"),
			H.Host->ApplyAerialFallingPhysicsOverride(H.Owner, 1800.0f, InvalidBraking));
	}
	for (const FVector InvalidAcceleration : {
		FVector(0.0, 0.0, 0.001), FVector(NaN, 0.0, 0.0),
		FVector(0.0, Infinity, 0.0), FVector(0.0, 0.0, NaN) })
	{
		TestFalse(TEXT("acceleration must be finite and exactly planar"),
			H.Host->ApplyAerialFallingPhysicsOverride(H.Owner, 1800.0f, 0.0f,
				InvalidAcceleration));
	}
	FWeaponActionToken Stale = H.Owner;
	++Stale.RuntimeToken.Generation;
	TestFalse(TEXT("stale runtime token is rejected"),
		H.Host->ApplyAerialFallingPhysicsOverride(Stale, 1800.0f, 0.0f));
	FWeaponActionToken Unregistered = H.Owner;
	++Unregistered.ActivationSequenceID;
	TestFalse(TEXT("an unregistered action token is rejected"),
		H.Host->ApplyAerialFallingPhysicsOverride(Unregistered, 1800.0f, 0.0f));
	TestFalse(TEXT("invalid action token is rejected"),
		H.Host->ApplyAerialFallingPhysicsOverride(FWeaponActionToken(), 1800.0f, 0.0f));
	TestTrue(TEXT("rejected requests leave the original CMC state untouched"),
		HasOriginalPhysics(*H.CMC));

	AMHGZM5KinsectSlashMovementTestCharacter* OtherAvatar =
		H.World->SpawnActor<AMHGZM5KinsectSlashMovementTestCharacter>(
			FVector(1000.0, 0.0, 1000.0), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("alternate avatar spawns"), OtherAvatar))
	{
		return false;
	}
	H.ASC->InitAbilityActorInfo(H.PlayerState, OtherAvatar);
	TestFalse(TEXT("current ASC avatar must match the Host pawn"),
		H.Host->ApplyAerialFallingPhysicsOverride(H.Owner, 1800.0f, 0.0f));
	TestTrue(TEXT("avatar mismatch leaves physics untouched"), HasOriginalPhysics(*H.CMC));
	H.ASC->InitAbilityActorInfo(H.PlayerState, H.Character);
	TestTrue(TEXT("a valid request still succeeds after rejected requests"),
		H.Host->ApplyAerialFallingPhysicsOverride(H.Owner, 3096.0f, 0.0f));
	H.Host->ShutdownRuntime(EWeaponRuntimeEndReason::RuntimeShutdown);
	TestFalse(TEXT("shutdown Host rejects custom profiles"),
		H.Host->ApplyAerialFallingPhysicsOverride(H.Owner, 1800.0f, 0.0f));
	TestTrue(TEXT("shutdown rejection preserves restored physics"), HasOriginalPhysics(*H.CMC));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5KinsectSlashFallingProfileWallRecomputeTest,
	"MHGZ.M5.KinsectSlash.PhysicsProfile.WallNoAirControlRecompute",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5KinsectSlashFallingProfileWallRecomputeTest::RunTest(const FString& Parameters)
{
	FScopedKinsectSlashTelemetry Telemetry;
	if (!TestNotNull(TEXT("wall recompute telemetry toggle exists"), Telemetry.Variable))
	{
		return false;
	}
	Telemetry.Set(true);
	FKinsectSlashPhysicsHarness H;
	if (!TestTrue(TEXT("wall recompute harness builds"), H.Build()))
	{
		return false;
	}
	AActor* Wall = H.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("blocking wall actor spawns"), Wall))
	{
		return false;
	}
	UBoxComponent* WallBox = NewObject<UBoxComponent>(Wall);
	Wall->AddInstanceComponent(WallBox);
	Wall->SetRootComponent(WallBox);
	WallBox->SetBoxExtent(FVector(10.0, 5000.0, 5000.0));
	WallBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	WallBox->SetCollisionObjectType(ECC_WorldStatic);
	WallBox->SetCollisionResponseToAllChannels(ECR_Block);
	WallBox->RegisterComponent();
	Wall->SetActorLocation(FVector(-180.0, 0.0, 1000.0));
	if (!TestTrue(TEXT("wall-case fall profile installs"),
		H.Host->ApplyAerialFallingPhysicsOverride(H.Owner, 1800.0f, 0.0f,
			FVector(-360.0, -360.0, 0.0))))
	{
		return false;
	}
	H.CMC->Velocity = FVector(-300.0, -300.0, 1080.0);
	bool bSawBlockingWall = false;
	bool bSawIndependentRecompute = false;
	constexpr float Step = 1.0f / 60.0f;
	for (int32 Frame = 0; Frame < 36; ++Frame)
	{
		const uint64 BeforeSerial = H.CMC->GetLatestAerialMovementSampleSerial();
		// Nonzero ordinary input selects UE's VelocityNoAirControl wall branch.
		H.CMC->AddInputVector(FVector(-1.0, 1.0, 0.0));
		H.CMC->TickComponent(Step, LEVELTICK_All, nullptr);
		TArray<FMHGZAerialMovementPhaseSample> Samples;
		H.CMC->GetAerialMovementSamplesSince(BeforeSerial, Samples);
		const FMHGZAerialMovementPhaseSample* FirstCalc = nullptr;
		bool bFrameHadImpact = false;
		for (const FMHGZAerialMovementPhaseSample& Sample : Samples)
		{
			if (Sample.Phase == FName(TEXT("CMC.HandleImpact.Pre"))
				&& Sample.bImpactBlockingHit)
			{
				bFrameHadImpact = true;
				bSawBlockingWall = true;
			}
			if (Sample.Phase == FName(TEXT("CMC.CalcVelocity.Post")))
			{
				if (!FirstCalc)
				{
					FirstCalc = &Sample;
				}
				else if (bFrameHadImpact
					&& FMath::IsNearlyEqual(Sample.DeltaSeconds, FirstCalc->DeltaSeconds)
					&& FMath::IsNearlyEqual(Sample.SubPhaseEntryVelocity.Y,
						FirstCalc->SubPhaseEntryVelocity.Y, 0.001))
				{
					TestTrue(TEXT("wall no-air-control calculation starts from the same old velocity"),
						FMath::IsNearlyEqual(Sample.Velocity.Y, FirstCalc->Velocity.Y, 0.001));
					bSawIndependentRecompute = true;
				}
			}
		}
	}
	TestTrue(TEXT("the real CMC encounters a blocking wall"), bSawBlockingWall);
	TestTrue(TEXT("the real wall path recomputes an independent no-air-control baseline"),
		bSawIndependentRecompute);
	TestTrue(TEXT("wall recomputation applies tangential acceleration once per step"),
		FMath::IsNearlyEqual(H.CMC->Velocity.Y, -516.0, 0.5));
	TestTrue(TEXT("collision prevents the unconstrained into-wall displacement"),
		H.Character->GetActorLocation().X > -180.0);
	TestEqual(TEXT("wall contact does not end Falling"), H.CMC->MovementMode, MOVE_Falling);
	return true;
}

#endif
