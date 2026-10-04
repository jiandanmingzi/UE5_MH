// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include <limits>

#include "../../Shared/MHGZM5KinsectSlashTestTypes.h"
#include "ActionSystem/AbilityTask_MHGZWeaponMovement.h"
#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "ActionSystem/MHGZKinsectSlashAbility.h"
#include "ActionSystem/MHGZWeaponMovementCurves.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Curves/CurveFloat.h"
#include "Curves/RichCurve.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/RootMotionSource.h"
#include "InsectGlaive/InsectGlaiveCombatConfig.h"
#include "MHGZPlayerState.h"
#include "../../Shared/MHGZM3TestTypes.h"
#include "Movement/MHGZInstrumentedCharacterMovementComponent.h"
#include "UI/MHGZAimComponent.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

namespace
{
UCurveFloat* MakeKinsectSlashTestCurve(UObject* Outer)
{
	UCurveFloat* Curve = NewObject<UCurveFloat>(Outer);
	auto AddLinearKey = [Curve](const float Time, const float Value)
	{
		const FKeyHandle Handle = Curve->FloatCurve.AddKey(Time, Value);
		Curve->FloatCurve.GetKey(Handle).InterpMode = RCIM_Linear;
	};
	AddLinearKey(0.0f, 0.0f);
	AddLinearKey(0.25f, 0.6f);
	AddLinearKey(0.75f, 0.9f);
	AddLinearKey(1.0f, 1.0f);
	return Curve;
}

struct FKinsectSlashMovementHarness
{
	UWorld* World = nullptr;
	AMHGZM5KinsectSlashMovementTestCharacter* Character = nullptr;
	AMHGZPlayerState* PlayerState = nullptr;
	UMHGZAbilitySystemComponent* ASC = nullptr;
	UMHGZWeaponRuntimeHostComponent* Host = nullptr;
	UMHGZM5KinsectSlashMovementTestAbility* Ability = nullptr;
	FGameplayAbilitySpecHandle AbilityHandle;

	bool Build(const FVector& StartLocation)
	{
		World = UWorld::CreateWorld(EWorldType::Game, false);
		if (!World)
		{
			return false;
		}
		Character = World->SpawnActor<AMHGZM5KinsectSlashMovementTestCharacter>(
			StartLocation, FRotator::ZeroRotator);
		PlayerState = World->SpawnActor<AMHGZPlayerState>();
		if (!Character || !PlayerState)
		{
			return false;
		}

		Character->SetPlayerState(PlayerState);
		ASC = PlayerState->GetMHGZAbilitySystemComponent();
		if (!ASC)
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
		Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		return Host->GetCurrentToken().IsValid();
	}

	bool Activate(const FVector& Direction, const float Distance, const float Duration,
		UCurveFloat* Curve,
		const EWeaponMovementMode Mode = EWeaponMovementMode::CurvedDirectional,
		const FWeaponMovementRequest* FullRequest = nullptr)
	{
		if (!ASC)
		{
			return false;
		}

		UMHGZM5KinsectSlashMovementTestAbility* CDO =
			GetMutableDefault<UMHGZM5KinsectSlashMovementTestAbility>();
		CDO->ConfigureForTest(Direction, Distance, Duration, Curve, Mode);
		if (FullRequest)
		{
			CDO->ConfigureRequestForTest(*FullRequest);
		}
		AbilityHandle = ASC->GiveAbility(FGameplayAbilitySpec(
			UMHGZM5KinsectSlashMovementTestAbility::StaticClass(), 1, INDEX_NONE, ASC));
		const bool bActivated = AbilityHandle.IsValid() && ASC->TryActivateAbility(AbilityHandle);
		CDO->ResetTestConfiguration();
		if (!bActivated)
		{
			return false;
		}

		if (FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(AbilityHandle))
		{
			for (UGameplayAbility* Instance : Spec->GetAbilityInstances())
			{
				if (UMHGZM5KinsectSlashMovementTestAbility* MovementAbility =
					Cast<UMHGZM5KinsectSlashMovementTestAbility>(Instance))
				{
					Ability = MovementAbility;
					break;
				}
			}
		}
		return Ability && Ability->IsActive();
	}

	void CancelAbility()
	{
		if (ASC && AbilityHandle.IsValid())
		{
			ASC->CancelAbilityHandle(AbilityHandle);
		}
	}

	void Teardown()
	{
		CancelAbility();
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

UBoxComponent* AddBlockingTestWall(UWorld* World, const FVector& Location)
{
	if (!World)
	{
		return nullptr;
	}
	AActor* WallActor = World->SpawnActor<AActor>(Location, FRotator::ZeroRotator);
	if (!WallActor)
	{
		return nullptr;
	}
	UBoxComponent* Wall = NewObject<UBoxComponent>(WallActor, TEXT("KinsectSlashTestWall"));
	WallActor->AddInstanceComponent(Wall);
	WallActor->SetRootComponent(Wall);
	Wall->SetBoxExtent(FVector(10.0f, 150.0f, 200.0f));
	Wall->SetCollisionProfileName(TEXT("BlockAll"));
	Wall->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Wall->SetCollisionObjectType(ECC_WorldStatic);
	Wall->SetCollisionResponseToAllChannels(ECR_Ignore);
	Wall->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	Wall->RegisterComponent();
	WallActor->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
	return Wall;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5KinsectSlashUsesActualAttachmentSocketNameTest,
	"MHGZ.M5.KinsectSlash.ConfigUsesActualAttachmentSocketName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5KinsectSlashUsesActualAttachmentSocketNameTest::RunTest(
	const FString& Parameters)
{
	const UInsectGlaiveCombatConfig* Config =
		GetDefault<UInsectGlaiveCombatConfig>();
	TestEqual(TEXT("kinsect slash attachment default matches the authored glaive socket"),
		Config->KinsectSlashAttachSocket, FName(TEXT("IG_Kinsect")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5KinsectSlashProgressCurveTest,
	"MHGZ.M5.KinsectSlash.CurvedDirectionalProgressAndValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5KinsectSlashProgressCurveTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!World)
	{
		return false;
	}

	ACharacter* Character = World->SpawnActor<ACharacter>(
		ACharacter::StaticClass(), FVector(100.0f, 200.0f, 300.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("a character supplies the RMS context"), Character))
	{
		World->DestroyWorld(false);
		return false;
	}
	UCharacterMovementComponent* CMC = Character->GetCharacterMovement();
	if (!TestNotNull(TEXT("the character has a movement component"), CMC))
	{
		World->DestroyWorld(false);
		return false;
	}

	UCurveFloat* Curve = MakeKinsectSlashTestCurve(GetTransientPackage());
	float TerminalSlope = 0.0f;
	TestTrue(TEXT("a normalized monotonic linear curve is accepted"),
		MHGZWeaponMovementCurves::ValidateNormalizedProgressCurve(Curve, TerminalSlope));
	TestTrue(TEXT("the terminal slope is the final authored segment"),
		FMath::IsNearlyEqual(TerminalSlope, 0.4f, 0.001f));
	TestFalse(TEXT("a missing curve is rejected"),
		MHGZWeaponMovementCurves::ValidateNormalizedProgressCurve(nullptr, TerminalSlope));

	UCurveFloat* DecreasingCurve = NewObject<UCurveFloat>(GetTransientPackage());
	for (const TPair<float, float>& KeyValue : {
		TPair<float, float>(0.0f, 0.0f), TPair<float, float>(0.5f, 0.8f),
		TPair<float, float>(0.75f, 0.4f), TPair<float, float>(1.0f, 1.0f) })
	{
		const FKeyHandle Handle = DecreasingCurve->FloatCurve.AddKey(KeyValue.Key, KeyValue.Value);
		DecreasingCurve->FloatCurve.GetKey(Handle).InterpMode = RCIM_Linear;
	}
	TestFalse(TEXT("a decreasing progress curve is rejected"),
		MHGZWeaponMovementCurves::ValidateNormalizedProgressCurve(DecreasingCurve, TerminalSlope));

	// The same production builder used by AbilityTask_MHGZWeaponMovement must
	// retain the curve on the RMS, preserve the requested 3D endpoint, and expose
	// a terminal tangent consistent with the final curve segment.
	const FVector Start = Character->GetActorLocation();
	constexpr float Distance = 1000.0f;
	constexpr float Duration = 1.0f;
	const FVector Directions[] = {
		FVector(1.0f, 0.0f, 0.0f),
		FVector(0.6f, 0.0f, 0.8f),
		FVector(0.6f, 0.0f, -0.8f)
	};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Directions); ++Index)
	{
		FVector TerminalVelocity = FVector::ZeroVector;
		TSharedPtr<FRootMotionSource_MoveToDynamicForce> Source =
			MHGZWeaponMovementCurves::MakeCurvedDirectionalSource(
				Start, Directions[Index], Distance, Duration, Curve,
				FName(*FString::Printf(TEXT("KinsectSlashTest%d"), Index)), TerminalVelocity);
		const FString Label = FString::Printf(TEXT("[trajectory %d] "), Index);
		if (!TestTrue(*(Label + TEXT("the production RMS builder accepts the request")),
			Source.IsValid()))
		{
			continue;
		}

		TestTrue(*(Label + TEXT("the RMS holds the authored progress curve")),
			Source->TimeMappingCurve == Curve);
		TestFalse(*(Label + TEXT("average-speed limiting is disabled")),
			Source->bRestrictSpeedToExpected);
		TestTrue(*(Label + TEXT("the endpoint uses the normalized 3D direction")),
			Source->TargetLocation.Equals(Start + Directions[Index].GetSafeNormal() * Distance, 0.01f));
		TestTrue(*(Label + TEXT("the end velocity follows the final curve tangent")),
			TerminalVelocity.Equals(Directions[Index].GetSafeNormal() * (Distance * 0.4f), 0.01f));

		// UE's actual MoveToDynamicForce evaluates TimeMappingCurve at normalized
		// elapsed time before calculating the root-motion delta. At 25% time the
		// authored curve is already at 60% travel, including its vertical axis.
		Source->PrepareRootMotion(0.25f, 0.25f, *Character, *CMC);
		const FVector RootMotionVelocity =
			Source->RootMotionParams.GetRootMotionTransform().GetTranslation();
		const FVector RootMotionDelta = RootMotionVelocity * 0.25f;
		const FVector ExpectedDelta = Directions[Index].GetSafeNormal()
			* (Distance * Curve->GetFloatValue(0.25f));
		TestTrue(*(Label + TEXT("the engine RMS applies C(u) to all three dimensions")),
			RootMotionDelta.Equals(ExpectedDelta, 0.05f));
	}

	FVector RejectedTerminalVelocity = FVector::OneVector;
	TestFalse(TEXT("invalid distance curve cannot create an RMS"),
		MHGZWeaponMovementCurves::MakeCurvedDirectionalSource(
			Start, FVector::ForwardVector, Distance, Duration, DecreasingCurve,
			TEXT("InvalidKinsectSlash"), RejectedTerminalVelocity).IsValid());
	TestTrue(TEXT("rejected source leaves no stale terminal velocity"),
		RejectedTerminalVelocity.IsNearlyZero());
	TestFalse(TEXT("zero duration cannot create an RMS"),
		MHGZWeaponMovementCurves::MakeCurvedDirectionalSource(
			Start, FVector::ForwardVector, Distance, 0.0f, Curve,
			TEXT("ZeroDuration"), RejectedTerminalVelocity).IsValid());

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5KinsectSlashAimFallbackTest,
	"MHGZ.M5.KinsectSlash.AimLockSafeFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5KinsectSlashAimFallbackTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!World)
	{
		return false;
	}

	ACharacter* Character = World->SpawnActor<ACharacter>(
		ACharacter::StaticClass(), FVector(10.0f, 20.0f, 30.0f), FRotator(0.0f, 90.0f, 0.0f));
	if (!TestNotNull(TEXT("character spawns without a PlayerController"), Character))
	{
		World->DestroyWorld(false);
		return false;
	}
	UMHGZAimComponent* Aim = NewObject<UMHGZAimComponent>(Character);
	Character->AddInstanceComponent(Aim);
	Aim->RegisterComponent();
	Aim->AimMaxDistance = 1500.0f;

	FWeaponAimLockSample Sample;
	TestTrue(TEXT("missing PlayerController safely samples actor forward"),
		Aim->CaptureAimLockSample(Character->GetActorLocation(), Sample));
	TestTrue(TEXT("the fallback is explicitly recorded"), Sample.bUsedActorForwardFallback);
	TestTrue(TEXT("the fallback direction is normalized actor forward"),
		Sample.LaunchDirection.Equals(Character->GetActorForwardVector(), 0.001f));
	TestTrue(TEXT("the fallback target uses the configured range"),
		FMath::IsNearlyEqual(FVector::Dist(Sample.TargetPoint, Character->GetActorLocation()),
			Aim->AimMaxDistance, 0.01f));

	const FVector InvalidOrigin(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f);
	TestFalse(TEXT("a non-finite launch origin is rejected"),
		Aim->CaptureAimLockSample(InvalidOrigin, Sample));

	AMHGZM5KinsectSlashAimTestController* Controller =
		World->SpawnActor<AMHGZM5KinsectSlashAimTestController>();
	if (TestNotNull(TEXT("a controller can supply the live viewpoint"), Controller))
	{
		Controller->SetViewpointForTest(FVector(100.0f, -50.0f, 220.0f),
			FRotator(20.0f, 35.0f, 0.0f));
		Character->PossessedBy(Controller);
		FWeaponAimLockSample FirstLiveSample;
		TestTrue(TEXT("a controlled character captures the current view ray"),
			Aim->CaptureAimLockSample(FVector::ZeroVector, FirstLiveSample));
		TestFalse(TEXT("the live view does not use the actor-forward fallback"),
			FirstLiveSample.bUsedActorForwardFallback);
		TestTrue(TEXT("camera direction comes from the current player viewpoint"),
			FirstLiveSample.CameraDirection.Equals(FRotator(20.0f, 35.0f, 0.0f).Vector(), 0.01f));
		TestTrue(TEXT("third-person parallax resolves from launch origin to the current target"),
			FirstLiveSample.LaunchDirection.Equals(
				(FirstLiveSample.TargetPoint - FVector::ZeroVector).GetSafeNormal(), 0.01f));

		Controller->SetViewpointForTest(FVector(100.0f, -50.0f, 220.0f),
			FRotator(-25.0f, -40.0f, 0.0f));
		FWeaponAimLockSample SecondLiveSample;
		TestTrue(TEXT("a later AimLock call succeeds after camera movement"),
			Aim->CaptureAimLockSample(FVector::ZeroVector, SecondLiveSample));
		TestTrue(TEXT("AimLock samples the changed camera direction instead of a cached snapshot"),
			SecondLiveSample.CameraDirection.Equals(
				FRotator(-25.0f, -40.0f, 0.0f).Vector(), 0.01f)
				&& !SecondLiveSample.CameraDirection.Equals(FirstLiveSample.CameraDirection, 0.01f));
	}
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5KinsectSlashMovementTaskLifecycleTest,
	"MHGZ.M5.KinsectSlash.MovementTaskCollisionHandoffAndCancelCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5KinsectSlashMovementTaskLifecycleTest::RunTest(const FString& Parameters)
{
	UCurveFloat* Curve = MakeKinsectSlashTestCurve(GetTransientPackage());
	if (!TestNotNull(TEXT("test progress curve"), Curve))
	{
		return false;
	}

	// The 186 lead must own stationary flight for multiple CMC ticks. A single
	// activation-time Velocity=0 was insufficient when superseding a vault.
	{
		FKinsectSlashMovementHarness H;
		if (!TestTrue(TEXT("hover harness builds"),
			H.Build(FVector(0.0f, 0.0f, 500.0f))))
		{
			H.Teardown();
			return false;
		}
		UMHGZInstrumentedCharacterMovementComponent* CMC =
			Cast<UMHGZInstrumentedCharacterMovementComponent>(
				H.Character->GetCharacterMovement());
		if (!TestNotNull(TEXT("instrumented hover movement component"), CMC))
		{
			H.Teardown();
			return false;
		}
		CMC->Velocity = FVector(100.0f, 0.0f, -900.0f);
		if (!TestTrue(TEXT("stationary hover acquires movement through GAS"),
			H.Activate(FVector::ZeroVector, 0.0f, 1.0f, nullptr,
				EWeaponMovementMode::StationaryHover)))
		{
			H.Teardown();
			return false;
		}
		const float StartZ = H.Character->GetActorLocation().Z;
		UAbilityTask_MHGZWeaponMovement* Task = H.Ability->GetMovementTaskForTest();
		for (int32 Frame = 0; Frame < 12; ++Frame)
		{
			CMC->PerformMovement(1.0f / 60.0f);
			Task->TickTask(1.0f / 60.0f);
		}
		TestTrue(TEXT("186 stays at one capsule height across the lead"),
			FMath::IsNearlyEqual(H.Character->GetActorLocation().Z, StartZ, 0.5f));
		TestEqual(TEXT("hover keeps gravity disabled"), CMC->MovementMode, MOVE_Flying);
		TestTrue(TEXT("hover still owns its source"), H.Host->IsActionMovementOwned());
		H.CancelAbility();
		CMC->PerformMovement(1.0f / 60.0f);
		TestFalse(TEXT("hover cancellation releases movement ownership"),
			H.Host->IsActionMovementOwned());
		TestTrue(TEXT("hover cancellation restores non-flying movement"),
			CMC->MovementMode != MOVE_Flying);
		H.Teardown();
	}

	// Replacing the 186 hover with the 187 dash must not briefly report
	// Walking/Falling: that transition restores the Host's aerial gravity to 1g.
	{
		FKinsectSlashMovementHarness H;
		if (!TestTrue(TEXT("hover-to-dash handoff harness builds"),
			H.Build(FVector(0.0f, 0.0f, 500.0f)))
			|| !TestTrue(TEXT("handoff hover starts"),
				H.Activate(FVector::ZeroVector, 0.0f, 1.0f, nullptr,
					EWeaponMovementMode::StationaryHover)))
		{
			H.Teardown();
			return false;
		}
		UCharacterMovementComponent* CMC = H.Character->GetCharacterMovement();
		CMC->GravityScale = 2.4246f;
		UAbilityTask_MHGZWeaponMovement* Hover = H.Ability->GetMovementTaskForTest();
		TestTrue(TEXT("hover can hand flying mode directly to the next source"),
			Hover && Hover->EndHoverForImmediateMovementHandoff(
				H.Ability->GetActionToken()));
		TestEqual(TEXT("handoff remains flying"), CMC->MovementMode, MOVE_Flying);
		TestTrue(TEXT("handoff keeps aerial gravity"),
			FMath::IsNearlyEqual(CMC->GravityScale, 2.4246f));
		TestFalse(TEXT("retired hover releases its movement ownership"),
			H.Host->IsActionMovementOwned());
		H.CancelAbility();
		H.Teardown();
	}

	// A hit replaces 187 by the 189 preparation source in the SAME action.
	// Reject stale owners and retire the dash without a fake grounded event.
	{
		FKinsectSlashMovementHarness H;
		if (!TestTrue(TEXT("dash-to-prep harness builds"),
			H.Build(FVector(0.0f, 0.0f, 500.0f)))
			|| !TestTrue(TEXT("dash-to-prep source starts"),
				H.Activate(FVector::ForwardVector, 120.0f, 0.5f, Curve)))
		{
			H.Teardown();
			return false;
		}
		UCharacterMovementComponent* CMC = H.Character->GetCharacterMovement();
		CMC->GravityScale = 3.159184f;
		UAbilityTask_MHGZWeaponMovement* Dash = H.Ability->GetMovementTaskForTest();
		FWeaponActionToken Stale = H.Ability->GetActionToken();
		++Stale.ActivationSequenceID;
		TestFalse(TEXT("stale action cannot retire the source"),
			Dash->EndForImmediateMovementHandoff(Stale));
		TestTrue(TEXT("same action can retire the dash atomically"),
			Dash->EndForImmediateMovementHandoff(H.Ability->GetActionToken()));
		TestEqual(TEXT("dash-to-prep remains flying"), CMC->MovementMode, MOVE_Flying);
		TestTrue(TEXT("dash-to-prep keeps the selected gravity"),
			FMath::IsNearlyEqual(CMC->GravityScale, 3.159184f));
		TestFalse(TEXT("retired dash releases source ownership"), H.Host->IsActionMovementOwned());
		TestFalse(TEXT("retiring a source does not emit natural completion"),
			H.Ability->HasMovementResultForTest());
		H.Teardown();
	}

	// Natural completion: the real GAS ability starts the real task; the CMC
	// advances the RootMotionSource and the task observes/removes it afterward.
	{
		FKinsectSlashMovementHarness H;
		if (!TestTrue(TEXT("natural-handoff harness builds"),
			H.Build(FVector(0.0f, 0.0f, 1000.0f)))
			|| !TestTrue(TEXT("the production movement task activates through GAS"),
				H.Activate(FVector(0.6f, 0.0f, 0.8f), 120.0f, 0.2f, Curve)))
		{
			H.Teardown();
			return false;
		}

		TestTrue(TEXT("captured miss profile applies before source release"),
			H.Host->ApplyAerialFallingPhysicsOverride(H.Ability->GetActionToken(),
				3096.0f, 0.0f));

		UMHGZInstrumentedCharacterMovementComponent* CMC =
			Cast<UMHGZInstrumentedCharacterMovementComponent>(H.Character->GetCharacterMovement());
		if (!TestNotNull(TEXT("instrumented movement component"), CMC))
		{
			H.Teardown();
			return false;
		}
		UAbilityTask_MHGZWeaponMovement* Task = H.Ability->GetMovementTaskForTest();
		TestNotNull(TEXT("the live task is available"), Task);
		TestTrue(TEXT("the task acquired exclusive Host movement ownership"),
			H.Host->IsActionMovementOwnedBy(H.Ability->GetActionToken()));
		TestEqual(TEXT("the authored curve owns the character during travel"),
			CMC->MovementMode, MOVE_Flying);

		constexpr float Step = 0.025f;
		for (int32 Frame = 0; Frame < 16 && !H.Ability->HasMovementResultForTest(); ++Frame)
		{
			CMC->PerformMovement(Step);
			Task->TickTask(Step);
		}

		TestTrue(TEXT("the source reaches its natural completion"),
			H.Ability->HasMovementResultForTest());
		if (H.Ability->HasMovementResultForTest())
		{
			const FWeaponMovementResult& Result = H.Ability->GetMovementResultForTest();
			TestEqual(TEXT("natural completion is reported"), Result.EndReason,
				EWeaponMovementEndReason::Completed);
			TestTrue(TEXT("natural travel reaches the authored 3D endpoint"),
				FMath::IsNearlyEqual(Result.TravelledDistance3D, 120.0f, 2.0f));
			TestTrue(TEXT("the authored terminal tangent is restored"),
				FVector::DotProduct(Result.FinalVelocity.GetSafeNormal(),
					FVector(0.6f, 0.0f, 0.8f)) > 0.99f);
			TestTrue(TEXT("the terminal tangent has the authored final speed"),
				FMath::IsNearlyEqual(Result.FinalVelocity.Size(), 240.0f, 2.0f));
		}
		TestEqual(TEXT("natural finish hands movement to Falling"),
			CMC->MovementMode, MOVE_Falling);
		TestTrue(TEXT("source release does not briefly report grounded and reset miss gravity"),
			FMath::IsNearlyEqual(FMath::Abs(CMC->GetGravityZ()), 3096.0f, 0.1f));
		TestTrue(TEXT("source release keeps zero miss braking"),
			FMath::IsNearlyZero(CMC->BrakingDecelerationFalling));
		TestFalse(TEXT("natural finish releases Host ownership"), H.Host->IsActionMovementOwned());
		CMC->PerformMovement(Step); // flush deferred RMS removal after the finish callback
		TestEqual(TEXT("natural finish removes its root-motion source"),
			CMC->GetCurrentActiveRootMotionSourceCount(), 0);
		H.Teardown();
	}

	// A wall blocks the capsule, not the authored 187 timer or its hit probe.
	// The eventual fall handoff must not restore the into-wall terminal speed.
	{
		FKinsectSlashMovementHarness H;
		if (!TestTrue(TEXT("blocking harness builds"), H.Build(FVector(0.0f, 0.0f, 500.0f)))
			|| !TestNotNull(TEXT("blocking wall is created"), AddBlockingTestWall(
				H.World, FVector(170.0f, 0.0f, 500.0f)))
			|| !TestTrue(TEXT("movement activates toward the wall"),
				H.Activate(FVector::ForwardVector, 500.0f, 0.5f, Curve)))
		{
			H.Teardown();
			return false;
		}

		UMHGZInstrumentedCharacterMovementComponent* CMC =
			Cast<UMHGZInstrumentedCharacterMovementComponent>(H.Character->GetCharacterMovement());
		if (!TestNotNull(TEXT("instrumented movement component"), CMC))
		{
			H.Teardown();
			return false;
		}
		FHitResult ExpectedWallHit;
		FCollisionQueryParams SweepParams(SCENE_QUERY_STAT(KinsectSlashTestWallProbe), false,
			H.Character);
		const FVector CapsuleStart = H.Character->GetActorLocation();
		const FCollisionShape CapsuleShape = H.Character->GetCapsuleComponent()->GetCollisionShape();
		TestTrue(TEXT("the synthetic wall blocks the same capsule sweep used by the CMC"),
			H.World->SweepSingleByChannel(ExpectedWallHit, CapsuleStart,
				CapsuleStart + FVector(500.0f, 0.0f, 0.0f), FQuat::Identity,
				ECC_Pawn, CapsuleShape, SweepParams));
		UAbilityTask_MHGZWeaponMovement* Task = H.Ability->GetMovementTaskForTest();
		constexpr float Step = 0.025f;
		float MaxWallContactZ = H.Character->GetActorLocation().Z;
		for (int32 Frame = 0; Frame < 4; ++Frame)
		{
			CMC->PerformMovement(Step);
			Task->TickTask(Step);
			MaxWallContactZ = FMath::Max(MaxWallContactZ,
				H.Character->GetActorLocation().Z);
		}
		TestFalse(TEXT("wall contact does not end the dash early"),
			H.Ability->HasMovementResultForTest());
		for (int32 Frame = 4; Frame < 24 && !H.Ability->HasMovementResultForTest(); ++Frame)
		{
			CMC->PerformMovement(Step);
			Task->TickTask(Step);
			MaxWallContactZ = FMath::Max(MaxWallContactZ,
				H.Character->GetActorLocation().Z);
		}
		TestTrue(TEXT("horizontal wall slide does not climb"),
			MaxWallContactZ <= 500.5f);

		TestTrue(TEXT("the dash ends at its authored duration"),
			H.Ability->HasMovementResultForTest());
		if (H.Ability->HasMovementResultForTest())
		{
			const FWeaponMovementResult& Result = H.Ability->GetMovementResultForTest();
			AddInfo(FString::Printf(TEXT("Wall movement: reason=%d distance=%.2f location=%s hit=%s"),
				static_cast<int32>(Result.EndReason), Result.TravelledDistance3D,
				*H.Character->GetActorLocation().ToCompactString(),
				*GetNameSafe(Result.BlockingHit.GetActor())));
			TestEqual(TEXT("wall contact preserves natural completion"), Result.EndReason,
				EWeaponMovementEndReason::Completed);
			TestTrue(TEXT("the completion result records the wall contact"),
				Result.BlockingHit.GetActor() != nullptr
					&& Result.BlockingHit.GetActor()->GetRootComponent() != nullptr);
			TestTrue(TEXT("the capsule stops before the requested endpoint"),
				Result.TravelledDistance3D < 200.0f);
			TestTrue(TEXT("a wall must not inherit the dash terminal tangent"),
				Result.FinalVelocity.IsNearlyZero());
		}
		TestFalse(TEXT("wall-limited finish releases Host ownership"), H.Host->IsActionMovementOwned());
		CMC->PerformMovement(Step); // flush deferred RMS removal after the finish callback
		TestEqual(TEXT("wall-limited finish removes its root-motion source"),
			CMC->GetCurrentActiveRootMotionSourceCount(), 0);
		TestTrue(TEXT("wall-limited finish cannot leave the capsule in Flying"),
			CMC->MovementMode != MOVE_Flying);
		TestTrue(TEXT("deferred source removal must not relaunch the capsule"),
			CMC->Velocity.Z <= 0.0f);
		H.Teardown();
	}

	// A stale hover task may retire after a new action has already launched 189.
	// Its deferred RMS finish must not overwrite the replacement's physical arc.
	{
		FKinsectSlashMovementHarness H;
		const FVector Start(0.0f, 0.0f, 1500.0f);
		if (!TestTrue(TEXT("stale-hover replacement harness builds"), H.Build(Start))
			|| !TestTrue(TEXT("action A starts its long hover"),
				H.Activate(FVector::ZeroVector, 0.0f, 2.0f, nullptr,
					EWeaponMovementMode::StationaryHover)))
		{
			H.Teardown();
			return false;
		}
		UMHGZInstrumentedCharacterMovementComponent* CMC =
			CastChecked<UMHGZInstrumentedCharacterMovementComponent>(
				H.Character->GetCharacterMovement());
		CMC->bRunPhysicsWithNoController = true;
		constexpr float Step = 1.0f / 60.0f;
		CMC->PerformMovement(Step); // move A's pending RMS into the active set
		UMHGZM5KinsectSlashMovementTestAbility* OldAbility = H.Ability;
		const FGameplayAbilitySpecHandle OldHandle = H.AbilityHandle;
		UAbilityTask_MHGZWeaponMovement* OldTask = OldAbility->GetMovementTaskForTest();
		const FWeaponActionToken OldToken = OldAbility->GetActionToken();
		if (!TestTrue(TEXT("action A retains its started hover task"),
			OldTask && OldTask->DidStartMovement())
			|| !TestEqual(TEXT("action A has an active hover RMS before replacement"),
				CMC->GetCurrentActiveRootMotionSourceCount(), 1)
			|| !TestTrue(TEXT("action A unregisters without retiring its task"),
				H.Host->UnregisterAction(OldToken)))
		{
			H.Teardown();
			return false;
		}
		TestTrue(TEXT("unregistered action A remains active until explicit cancellation"),
			OldAbility->IsActive());
		const FKinsectSlashDanceEntryTuning Tuning;
		const FWeaponMovementRequest Request = Tuning.MakeRequest(
			FWeaponActionToken(), FVector::ForwardVector, H.Character);
		if (!TestTrue(TEXT("action B activates the authored 189 flight through GAS"),
			H.Activate(Request.DirectionSnapshot, Request.MaxDistance, Request.Duration,
				nullptr, Request.Mode, &Request)))
		{
			H.ASC->CancelAbilityHandle(OldHandle);
			H.Teardown();
			return false;
		}
		UMHGZM5KinsectSlashMovementTestAbility* NewAbility = H.Ability;
		UAbilityTask_MHGZWeaponMovement* NewTask = NewAbility->GetMovementTaskForTest();
		const FWeaponActionToken NewToken = NewAbility->GetActionToken();
		if (!TestTrue(TEXT("action B has its own started physical task"),
			NewAbility != OldAbility && NewTask && NewTask != OldTask && NewTask->DidStartMovement()))
		{
			H.ASC->CancelAbilityHandle(OldHandle);
			H.Teardown();
			return false;
		}
		const FVector FlightStart = H.Character->GetActorLocation();
		TestTrue(TEXT("hover A leaves the replacement at the original launch position"),
			FlightStart.Equals(Start, 0.001));
		TestEqual(TEXT("A's old override still awaits cancellation before B's first tick"),
			CMC->GetCurrentActiveRootMotionSourceCount(), 1);
		H.ASC->CancelAbilityHandle(OldHandle); // cancel A, not H.Ability (now B)
		TestFalse(TEXT("saved action A ends on cancellation"), OldAbility->IsActive());
		TestFalse(TEXT("stale action A cancellation does not report natural completion"),
			OldAbility->HasMovementResultForTest());
		TestTrue(TEXT("action B remains active immediately after A cancellation"), NewAbility->IsActive());
		TestTrue(TEXT("A cancellation preserves B's exact movement owner"),
			H.Host->IsActionMovementOwnedBy(NewToken));
		TestEqual(TEXT("A cancellation preserves B's Falling mode"), CMC->MovementMode, MOVE_Falling);
		TestTrue(TEXT("A cancellation preserves B's measured gravity"),
			FMath::IsNearlyEqual(FMath::Abs(CMC->GetGravityZ()), 1800.0f, 0.1f));
		TestTrue(TEXT("A cancellation preserves B's backward acceleration"),
			CMC->GetAerialPlanarAcceleration().Equals(FVector(-360.0, 0.0, 0.0), 0.001));
		TestTrue(TEXT("A cancellation preserves B's launch velocity immediately"),
			CMC->Velocity.Equals(FVector(-300.0, 0.0, 1080.0), 0.001));
		TestTrue(TEXT("A cancellation preserves B's launch position immediately"),
			H.Character->GetActorLocation().Equals(FlightStart, 0.001));

		CMC->PerformMovement(Step); // flush A's deferred removal during B's real physics
		NewTask->TickTask(Step);
		const double Time = Step;
		const FVector ExpectedDisplacement(-300.0 * Time - 180.0 * Time * Time,
			0.0, 1080.0 * Time - 900.0 * Time * Time);
		const FVector ExpectedVelocity(-300.0 - 360.0 * Time, 0.0, 1080.0 - 1800.0 * Time);
		TestTrue(TEXT("deferred hover removal preserves B's measured physical displacement"),
			(H.Character->GetActorLocation() - FlightStart).Equals(ExpectedDisplacement, 0.05));
		TestTrue(TEXT("deferred hover removal preserves B's measured physical velocity"),
			CMC->Velocity.Equals(ExpectedVelocity, 0.05));
		TestEqual(TEXT("no old override survives the replacement physics tick"),
			CMC->GetCurrentActiveRootMotionSourceCount(), 0);
		TestTrue(TEXT("B remains the active current action after deferred removal"),
			NewAbility->IsActive() && NewAbility->GetActionToken() == NewToken
				&& H.Host->IsTokenCurrent(NewToken.RuntimeToken)
				&& H.Host->IsActionMovementOwnedBy(NewToken));
		TestFalse(TEXT("B's first physics tick does not complete its flight"),
			NewAbility->HasMovementResultForTest());
		TestEqual(TEXT("B remains Falling after deferred removal"), CMC->MovementMode, MOVE_Falling);
		TestTrue(TEXT("B retains its gravity and acceleration after deferred removal"),
			FMath::IsNearlyEqual(FMath::Abs(CMC->GetGravityZ()), 1800.0f, 0.1f)
				&& CMC->GetAerialPlanarAcceleration().Equals(FVector(-360.0, 0.0, 0.0), 0.001));
		H.Teardown();
	}

	// Cancellation: EndAbility destroys the live task, which must remove the
	// RootMotionSource, restore a non-Flying movement mode, and return ownership.
	{
		FKinsectSlashMovementHarness H;
		if (!TestTrue(TEXT("cancel harness builds"), H.Build(FVector(0.0f, 0.0f, 500.0f)))
			|| !TestTrue(TEXT("movement activates before cancellation"),
				H.Activate(FVector(0.0f, 0.0f, 1.0f), 300.0f, 0.5f, Curve)))
		{
			H.Teardown();
			return false;
		}

		UMHGZInstrumentedCharacterMovementComponent* CMC =
			Cast<UMHGZInstrumentedCharacterMovementComponent>(H.Character->GetCharacterMovement());
		if (!TestNotNull(TEXT("instrumented movement component"), CMC))
		{
			H.Teardown();
			return false;
		}
		TestEqual(TEXT("cancel setup owns Flying mode"), CMC->MovementMode, MOVE_Flying);
		TestTrue(TEXT("cancel setup owns movement in Host"), H.Host->IsActionMovementOwned());
		H.CancelAbility();
		TestFalse(TEXT("cancel ends the owning ability"), H.Ability->IsActive());
		TestEqual(TEXT("cancel removes the root-motion source"),
			CMC->GetCurrentActiveRootMotionSourceCount(), 0);
		TestTrue(TEXT("cancel restores movement from Flying"), CMC->MovementMode != MOVE_Flying);
		TestFalse(TEXT("cancel releases Host ownership"), H.Host->IsActionMovementOwned());
		TestFalse(TEXT("cancel does not report natural completion"),
			H.Ability->HasMovementResultForTest());
		H.Teardown();
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5KinsectSlashCapturedCurveTaskTest,
	"MHGZ.M5.KinsectSlash.CapturedCurvedVaultRealCMC",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5KinsectSlashCapturedCurveTaskTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FKinsectSlashDanceEntryTuning Tuning;
	for (const int32 FPS : { 30, 60, 120 })
	{
		const FString Case = FString::Printf(TEXT("captured flight %d FPS"), FPS);
		FKinsectSlashMovementHarness H;
		const FVector Start(0.0f, 0.0f, 1500.0f);
		if (!TestTrue(TEXT("captured curve harness builds"), H.Build(Start)))
		{
			H.Teardown();
			return false;
		}
		UMHGZInstrumentedCharacterMovementComponent* CMC =
			CastChecked<UMHGZInstrumentedCharacterMovementComponent>(
				H.Character->GetCharacterMovement());
		CMC->bRunPhysicsWithNoController = true;
		CMC->AirControl = 1.0f;
		CMC->MaxWalkSpeed = 20.0f; // captured backward motion exceeds the locomotion cap
		const FWeaponMovementRequest Request = Tuning.MakeRequest(
			FWeaponActionToken(), FVector::ForwardVector, H.Character);
		if (!TestTrue(TEXT("captured curve activates through the production task"),
			H.Activate(Request.DirectionSnapshot, Request.MaxDistance, Request.Duration,
				nullptr, Request.Mode, &Request)))
		{
			H.Teardown();
			return false;
		}
		UAbilityTask_MHGZWeaponMovement* Task = H.Ability->GetMovementTaskForTest();
		if (!TestTrue(Case + TEXT(" starts the physical task"), Task && Task->DidStartMovement()))
		{
			H.Teardown();
			return false;
		}
		TestEqual(Case + TEXT(" starts in Falling"), CMC->MovementMode, MOVE_Falling);
		TestTrue(Case + TEXT(" installs the measured gravity through the live Host owner"),
			FMath::IsNearlyEqual(FMath::Abs(CMC->GetGravityZ()), 1800.0f, 0.1f));
		TestTrue(Case + TEXT(" disables horizontal braking and friction"),
			FMath::IsNearlyZero(CMC->BrakingDecelerationFalling)
				&& FMath::IsNearlyZero(CMC->FallingLateralFriction));
		TestTrue(Case + TEXT(" installs the measured backward acceleration"),
			CMC->GetAerialPlanarAcceleration().Equals(FVector(-360.0, 0.0, 0.0), 0.001));
		TestTrue(Case + TEXT(" launches with the measured physical velocity"),
			CMC->Velocity.Equals(FVector(-300.0, 0.0, 1080.0), 0.001));
		TestTrue(Case + TEXT(" owns movement with the live action token"),
			H.Host->IsActionMovementOwnedBy(H.Ability->GetActionToken()));
		TestEqual(Case + TEXT(" uses no root-motion source at launch"),
			CMC->GetCurrentActiveRootMotionSourceCount(), 0);
		const float Step = 1.0f / FPS;
		double SimulatedTime = 0.0;
		float MaxUp = 0.0f;
		double MaxModelError = 0.0;
		for (int32 Frame = 0; Frame < FPS * 2 && !H.Ability->HasMovementResultForTest(); ++Frame)
		{
			CMC->PerformMovement(Step);
			SimulatedTime += Step;
			MaxUp = FMath::Max(MaxUp, static_cast<float>(H.Character->GetActorLocation().Z - Start.Z));
			// Independent measured oracle; CMC owns every integrated position.
			const FVector Measured(-300.0 * SimulatedTime - 180.0 * SimulatedTime * SimulatedTime,
				0.0, 1080.0 * SimulatedTime - 900.0 * SimulatedTime * SimulatedTime);
			MaxModelError = FMath::Max(MaxModelError,
				(H.Character->GetActorLocation() - Start - Measured).Size());
			Task->TickTask(Step);
		}
		if (!TestTrue(Case + TEXT(" completes"), H.Ability->HasMovementResultForTest()))
		{
			H.Teardown();
			return false;
		}
		const FWeaponMovementResult& Result = H.Ability->GetMovementResultForTest();
		TestEqual(Case + TEXT(" reports natural completion"), Result.EndReason,
			EWeaponMovementEndReason::Completed);
		TestTrue(Case + TEXT(" reaches the measured 324 cm apex"),
			FMath::IsNearlyEqual(MaxUp, 324.0f, 2.0f));
		TestTrue(Case + TEXT(" follows the measured parabola throughout flight"), MaxModelError < 0.25);
		// The task observes completion after whole CMC ticks, so physical time may
		// exceed the measured duration by up to one frame. Never rewrite its tangent.
		TestTrue(Case + TEXT(" completes within one timestep of the measured duration"),
			SimulatedTime >= 1.1833333 - 0.00001
				&& SimulatedTime <= 1.1833333 + Step + 0.00001);
		const double Time = SimulatedTime;
		const FVector ExpectedEnd = Start + FVector(-300.0 * Time - 180.0 * Time * Time,
			0.0, 1080.0 * Time - 900.0 * Time * Time);
		TestTrue(Case + TEXT(" reaches the analytic endpoint at the actual physical time"),
			H.Character->GetActorLocation().Equals(ExpectedEnd, 0.25));
		const FVector ExpectedVelocity(-300.0 - 360.0 * Time, 0.0, 1080.0 - 1800.0 * Time);
		TestTrue(Case + TEXT(" reports the integrated physical terminal velocity"),
			Result.FinalVelocity.Equals(ExpectedVelocity, 0.1));
		TestTrue(Case + TEXT(" retains the measured terminal tangent within one timestep"),
			FMath::IsNearlyEqual(Result.FinalVelocity.X, -726.0, 360.0 * Step + 0.1)
				&& FMath::IsNearlyZero(Result.FinalVelocity.Y, 0.01)
				&& FMath::IsNearlyEqual(Result.FinalVelocity.Z, -1050.0, 1800.0 * Step + 0.1));
		TestTrue(Case + TEXT(" leaves CMC velocity unchanged at natural completion"),
			CMC->Velocity.Equals(Result.FinalVelocity, 0.001));
		TestEqual(Case + TEXT(" releases directly to Falling"), CMC->MovementMode, MOVE_Falling);
		TestFalse(Case + TEXT(" releases Host movement ownership"), H.Host->IsActionMovementOwned());
		TestEqual(Case + TEXT(" finishes with no root-motion source"),
			CMC->GetCurrentActiveRootMotionSourceCount(), 0);
		TestTrue(Case + TEXT(" keeps the actor's locked yaw"),
			FMath::IsNearlyZero(H.Character->GetActorRotation().Yaw));
		H.Teardown();
	}
	FWeaponMovementRequest Invalid = Tuning.MakeRequest(FWeaponActionToken(), FVector::ForwardVector);
	Invalid.GravityMagnitude = std::numeric_limits<float>::quiet_NaN();
	TestFalse(TEXT("nonfinite authored gravity is rejected"),
		Invalid.HasValidAuthoredAerialPhysicsParameters());
	return true;
}

#endif
