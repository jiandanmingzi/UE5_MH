// Copyright MHGZ Project. All Rights Reserved.
//
// M2 下坠突刺基础专项：只锁定当前已合入的通用 RecordedLocalTrajectory、
// WeaponMovementTask/CMC 租约和动作方向快照接口；不依赖尚未合入的招式 GA。

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include <limits>

#include "AbilitySystemGlobals.h"
#include "ActionSystem/AbilityTask_MHGZWeaponMovement.h"
#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "ActionSystem/MHGZAttackAbility.h"
#include "ActionSystem/MHGZDescendingThrustAbility.h"
#include "ActionSystem/MHGZRecordedLocalTrajectory.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Curves/CurveVector.h"
#include "Curves/RichCurve.h"
#include "EngineGlobals.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "MHGZPlayerState.h"
#include "../../Shared/MHGZM4TestTypes.h"
#include "../../Shared/MHGZM5KinsectSlashTestTypes.h"
#include "Movement/MHGZInstrumentedCharacterMovementComponent.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

namespace
{
UCurveVector* MakeKnownTrajectoryCurve()
{
	UCurveVector* Curve = NewObject<UCurveVector>(GetTransientPackage());
	if (!Curve)
	{
		return nullptr;
	}

	const TArray<float> Times = { 0.0f, 0.5f, 1.0f };
	const TArray<FVector> Points = {
		FVector::ZeroVector,
		FVector(50.0f, -20.0f, 10.0f),
		FVector(100.0f, 40.0f, -30.0f)
	};
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		for (int32 Index = 0; Index < Times.Num(); ++Index)
		{
			FRichCurveKey& Key = Curve->FloatCurves[Axis].Keys.AddDefaulted_GetRef();
			Key.Time = Times[Index];
			Key.InterpMode = RCIM_Linear;
			Key.TangentMode = RCTM_Auto;
			Key.TangentWeightMode = RCTWM_WeightedNone;
			Key.Value = Points[Index][Axis];
		}
	}
	return Curve;
}

FRecordedLocalTrajectoryProfile MakeKnownTrajectoryProfile()
{
	FRecordedLocalTrajectoryProfile Profile;
	Profile.Curve = MakeKnownTrajectoryCurve();
	Profile.CoveredSeconds = 1.0f;
	Profile.TerminalVelocityLocal = FVector(100.0f, -40.0f, 20.0f);
	Profile.TerminalAccelerationLocal = FVector(0.0f, 0.0f, -100.0f);
	return Profile;
}

FRecordedLocalTrajectoryProfile MakeHighSpeedSurfaceContactProfile()
{
	FRecordedLocalTrajectoryProfile Profile;
	Profile.Curve = NewObject<UCurveVector>(GetTransientPackage());
	if (!Profile.Curve)
	{
		return Profile;
	}

	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const float EndValue = Axis == 0 ? 100.0f : (Axis == 2 ? -2000.0f : 0.0f);
		const FKeyHandle Start = Profile.Curve->FloatCurves[Axis].AddKey(0.0f, 0.0f);
		Profile.Curve->FloatCurves[Axis].GetKey(Start).InterpMode = RCIM_Linear;
		const FKeyHandle End = Profile.Curve->FloatCurves[Axis].AddKey(1.0f, EndValue);
		Profile.Curve->FloatCurves[Axis].GetKey(End).InterpMode = RCIM_Linear;
	}
	Profile.CoveredSeconds = 1.0f;
	Profile.TerminalVelocityLocal = FVector(100.0f, 0.0f, -2000.0f);
	Profile.TerminalAccelerationLocal = FVector::ZeroVector;
	return Profile;
}

AActor* SpawnTiltedTrajectoryContactSurface(UWorld& World)
{
	AActor* Surface = World.SpawnActor<AActor>();
	if (!Surface)
	{
		return nullptr;
	}
	UBoxComponent* Box = NewObject<UBoxComponent>(Surface,
		TEXT("RecordedTrajectoryContactSurface"), RF_Transient);
	Surface->AddInstanceComponent(Box);
	Surface->SetRootComponent(Box);
	// A compact tilted top models grazing the edge of the training dummy instead
	// of an effectively infinite ramp that would keep sliding the capsule forever.
	Box->SetBoxExtent(FVector(40.0f, 40.0f, 10.0f));
	Box->SetCollisionProfileName(TEXT("BlockAll"));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Box->SetCollisionObjectType(ECC_WorldStatic);
	Box->SetCollisionResponseToAllChannels(ECR_Ignore);
	Box->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	Box->RegisterComponent();
	Surface->SetActorLocation(FVector(0.0f, 0.0f, -10.0f), false, nullptr,
		ETeleportType::TeleportPhysics);
	Surface->SetActorRotation(FRotator(15.0f, 0.0f, 0.0f), ETeleportType::TeleportPhysics);
	return Surface;
}

FWeaponMovementRequest MakeProtectedTrajectoryRequest()
{
	FWeaponMovementRequest Request;
	Request.Mode = EWeaponMovementMode::RecordedLocalTrajectory;
	Request.DirectionSnapshot = FVector::ForwardVector;
	Request.RecordedTrajectory = MakeKnownTrajectoryProfile();
	Request.RecordedTrajectoryScale = FVector::OneVector;
	Request.Duration = Request.RecordedTrajectory.GetDuration();
	Request.TrajectoryGroundPolicy = ERecordedTrajectoryGroundPolicy::ProtectedFlight;
	Request.TrajectoryTailPolicy = ERecordedTrajectoryTailPolicy::HoldLastKey;
	return Request;
}

/**
 * Uses the existing real test character and the existing production movement task.
 * No fake Host, CMC, or ActionToken is introduced here: activation must create all
 * three through the normal ASC/RuntimeHost path.
 */
struct FDescendingThrustFoundationHarness
{
	UWorld* World = nullptr;
	AMHGZM5KinsectSlashMovementTestCharacter* Character = nullptr;
	AMHGZPlayerState* PlayerState = nullptr;
	UMHGZAbilitySystemComponent* ASC = nullptr;
	UMHGZWeaponRuntimeHostComponent* Host = nullptr;
	UMHGZM5KinsectSlashMovementTestAbility* MovementAbility = nullptr;
	UMHGZM4TestAttackAbility* AttackAbility = nullptr;
	FGameplayAbilitySpecHandle AbilityHandle;
	FString FailureStage;

	bool Build(const FVector& StartLocation)
	{
		const auto Fail = [this](const TCHAR* Stage)
		{
			FailureStage = Stage;
			return false;
		};

		UAbilitySystemGlobals& Globals = UAbilitySystemGlobals::Get();
		if (!Globals.IsAbilitySystemGlobalsInitialized())
		{
			Globals.InitGlobalData();
		}

		World = UWorld::CreateWorld(EWorldType::Game, false);
		if (!World)
		{
			return Fail(TEXT("world"));
		}

		Character = World->SpawnActor<AMHGZM5KinsectSlashMovementTestCharacter>(
			StartLocation, FRotator::ZeroRotator);
		PlayerState = World->SpawnActor<AMHGZPlayerState>();
		if (!Character || !PlayerState)
		{
			return Fail(TEXT("character/player state"));
		}
		Character->SetPlayerState(PlayerState);

		ASC = PlayerState->GetMHGZAbilitySystemComponent();
		if (!ASC)
		{
			return Fail(TEXT("ASC"));
		}
		ASC->InitAbilityActorInfo(PlayerState, Character);
		ASC->InitializeAbilitySystem();

		Host = NewObject<UMHGZWeaponRuntimeHostComponent>(Character);
		if (!Host)
		{
			return Fail(TEXT("Host"));
		}
		Character->AddInstanceComponent(Host);
		Host->RegisterComponent();
		Host->InitializePawnRuntime(Character, nullptr, ASC,
			PlayerState->GetEquipmentComponent());

		if (!Host->GetCurrentToken().IsValid())
		{
			return Fail(TEXT("valid RuntimeToken"));
		}

		UCharacterMovementComponent* CMC = Character->GetCharacterMovement();
		if (!CMC)
		{
			return Fail(TEXT("character movement component"));
		}
		CMC->bRunPhysicsWithNoController = true;
		CMC->SetMovementMode(MOVE_Falling);
		return true;
	}

	bool ActivateRecordedTrajectory(const FWeaponMovementRequest& Request)
	{
		if (!ASC)
		{
			FailureStage = TEXT("activate without ASC");
			return false;
		}

		UMHGZM5KinsectSlashMovementTestAbility* CDO =
			GetMutableDefault<UMHGZM5KinsectSlashMovementTestAbility>();
		CDO->ConfigureRequestForTest(Request);
		AbilityHandle = ASC->GiveAbility(FGameplayAbilitySpec(
			UMHGZM5KinsectSlashMovementTestAbility::StaticClass(), 1, INDEX_NONE, ASC));
		const bool bActivated = AbilityHandle.IsValid()
			&& ASC->TryActivateAbility(AbilityHandle);
		CDO->ResetTestConfiguration();
		if (!bActivated)
		{
			FailureStage = TEXT("real movement ability activation");
			return false;
		}

		if (FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(AbilityHandle))
		{
			for (UGameplayAbility* Instance : Spec->GetAbilityInstances())
			{
				if (UMHGZM5KinsectSlashMovementTestAbility* Action =
					Cast<UMHGZM5KinsectSlashMovementTestAbility>(Instance))
				{
					MovementAbility = Action;
					break;
				}
			}
		}
		return MovementAbility && MovementAbility->IsActive();
	}

	bool ActivateAttack()
	{
		if (!ASC || !Host)
		{
			FailureStage = TEXT("activate attack without ASC/Host");
			return false;
		}

		AbilityHandle = ASC->GiveAbility(FGameplayAbilitySpec(
			UMHGZM4TestAttackAbility::StaticClass(), 1, INDEX_NONE, ASC));
		if (!AbilityHandle.IsValid())
		{
			FailureStage = TEXT("grant attack ability");
			return false;
		}

		FWeaponAbilityActivationContext Context;
		Context.RuntimeToken = Host->GetCurrentToken();
		Context.ActivationSequenceID = Host->AllocateActivationSequenceID();
		ASC->PrepareWeaponAbilityActivation(AbilityHandle, Context);
		if (!ASC->TryActivateAbility(AbilityHandle))
		{
			FailureStage = TEXT("real attack ability activation");
			return false;
		}

		if (FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(AbilityHandle))
		{
			for (UGameplayAbility* Instance : Spec->GetAbilityInstances())
			{
				if (UMHGZM4TestAttackAbility* Action =
					Cast<UMHGZM4TestAttackAbility>(Instance))
				{
					AttackAbility = Action;
					break;
				}
			}
		}
		return AttackAbility && AttackAbility->IsActive();
	}

	void Teardown()
	{
		if (ASC && AbilityHandle.IsValid())
		{
			ASC->CancelAbilityHandle(AbilityHandle);
		}
		if (Host)
		{
			Host->ShutdownRuntime(EWeaponRuntimeEndReason::RuntimeShutdown);
		}
		if (World)
		{
			World->DestroyWorld(false);
		}
		World = nullptr;
		Character = nullptr;
		PlayerState = nullptr;
		ASC = nullptr;
		Host = nullptr;
		MovementAbility = nullptr;
		AttackAbility = nullptr;
	}
};

struct FScopedTelemetryCVar
{
	IConsoleVariable* Variable = nullptr;
	FString PreviousValue;

	explicit FScopedTelemetryCVar(IConsoleVariable* InVariable)
		: Variable(InVariable)
		, PreviousValue(InVariable ? InVariable->GetString() : FString())
	{
		if (Variable)
		{
			Variable->Set(TEXT("1"), ECVF_SetByCode);
		}
	}

	~FScopedTelemetryCVar()
	{
		if (Variable)
		{
			Variable->Set(*PreviousValue, ECVF_SetByCode);
		}
	}
};

struct FScopedFrameCounterFloor
{
	const uint64 SavedFrame = GFrameCounter;

	FScopedFrameCounterFloor()
	{
		if (GFrameCounter < 2)
		{
			GFrameCounter = 2;
		}
	}

	~FScopedFrameCounterFloor()
	{
		GFrameCounter = SavedFrame;
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM2DescendingThrustRecordedTrajectoryScaleTest,
	"MHGZ.M2.DescendingThrust.Foundation.RecordedTrajectoryScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM2DescendingThrustRecordedTrajectoryScaleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FWeaponMovementRequest Request;
	Request.Mode = EWeaponMovementMode::RecordedLocalTrajectory;
	Request.DirectionSnapshot = FVector::ForwardVector;
	Request.RecordedTrajectory = MakeKnownTrajectoryProfile();
	Request.RecordedTrajectoryScale = FVector(2.0f, 1.0f, 1.0f);
	Request.Duration = Request.RecordedTrajectory.GetDuration();

	TestTrue(TEXT("the known-key RecordedLocalTrajectory request is valid"),
		Request.HasValidRecordedTrajectoryParameters());
	const float AuthoredDuration = Request.RecordedTrajectory.GetDuration();
	const float RequestDuration = Request.Duration;

	TestTrue(TEXT("scale multiplies only the authored local X offset when Y/Z scale is one"),
		Request.GetRecordedTrajectoryOffset(0.75f).Equals(
			FVector(150.0f, 10.0f, -10.0f), 0.001f));
	TestTrue(TEXT("scale multiplies only the authored local X velocity when Y/Z scale is one"),
		Request.GetRecordedTrajectoryVelocity(0.75f).Equals(
			FVector(200.0f, 120.0f, -80.0f), 0.001f));
	TestTrue(TEXT("trajectory duration is unchanged by component scaling"),
		FMath::IsNearlyEqual(Request.RecordedTrajectory.GetDuration(), AuthoredDuration)
			&& FMath::IsNearlyEqual(Request.Duration, RequestDuration));

	const TCHAR* AxisNames[] = { TEXT("X"), TEXT("Y"), TEXT("Z") };
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		FWeaponMovementRequest NegativeScale = Request;
		NegativeScale.RecordedTrajectoryScale[Axis] = -0.001f;
		TestFalse(*FString::Printf(TEXT("negative %s scale is invalid"), AxisNames[Axis]),
			NegativeScale.HasValidRecordedTrajectoryParameters());

		FWeaponMovementRequest NaNScale = Request;
		NaNScale.RecordedTrajectoryScale[Axis] =
			std::numeric_limits<float>::quiet_NaN();
		TestFalse(*FString::Printf(TEXT("NaN %s scale is invalid"), AxisNames[Axis]),
			NaNScale.HasValidRecordedTrajectoryParameters());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM2DescendingThrustDirectionalBoostThresholdTest,
	"MHGZ.M2.DescendingThrust.Foundation.DirectionalBoostThreshold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM2DescendingThrustDirectionalBoostThresholdTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	TestFalse(TEXT("exactly 0.50 does not enable the directional boost"),
		UMHGZDescendingThrustAbility::IsDirectionalBoostInputAboveThreshold(
			FVector2D(0.50f, 0.0f)));
	TestFalse(TEXT("below threshold does not enable the directional boost"),
		UMHGZDescendingThrustAbility::IsDirectionalBoostInputAboveThreshold(
			FVector2D(0.3f, 0.3f)));
	TestTrue(TEXT("magnitude strictly above 0.50 enables the directional boost"),
		UMHGZDescendingThrustAbility::IsDirectionalBoostInputAboveThreshold(
			FVector2D(0.5f, 0.1f)));
	TestFalse(TEXT("non-finite input is rejected"),
		UMHGZDescendingThrustAbility::IsDirectionalBoostInputAboveThreshold(
			FVector2D(std::numeric_limits<float>::quiet_NaN(), 1.0f)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM2DescendingThrustMovementTaskLandingLeaseTest,
	"MHGZ.M2.DescendingThrust.Foundation.MovementTaskLandingLease",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM2DescendingThrustMovementTaskLandingLeaseTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	IConsoleVariable* TelemetryVariable = IConsoleManager::Get().FindConsoleVariable(
		TEXT("mhgz.Telemetry.Enable"));
	if (!TestNotNull(TEXT("the existing sparse aerial telemetry switch is available"),
		TelemetryVariable))
	{
		return false;
	}
	FScopedTelemetryCVar TelemetryGuard(TelemetryVariable);

	FDescendingThrustFoundationHarness H;
	if (!TestTrue(*FString::Printf(TEXT("real Host/Action harness builds (%s)"),
		*H.FailureStage), H.Build(FVector(0.0f, 0.0f, 1000.0f))))
	{
		H.Teardown();
		return false;
	}

	const uint64 SerialBeforeActivation =
		CastChecked<UMHGZInstrumentedCharacterMovementComponent>(
			H.Character->GetCharacterMovement())->GetLatestAerialMovementSampleSerial();
	if (!TestTrue(TEXT("the real production movement task activates"),
		H.ActivateRecordedTrajectory(MakeProtectedTrajectoryRequest())))
	{
		H.Teardown();
		return false;
	}

	UMHGZInstrumentedCharacterMovementComponent* CMC =
		Cast<UMHGZInstrumentedCharacterMovementComponent>(
			H.Character->GetCharacterMovement());
	UAbilityTask_MHGZWeaponMovement* Task =
		H.MovementAbility ? H.MovementAbility->GetMovementTaskForTest() : nullptr;
	if (!TestNotNull(TEXT("the live task is a real UAbilityTask_MHGZWeaponMovement"), Task)
		|| !TestNotNull(TEXT("the live CMC is instrumented"), CMC)
		|| !TestTrue(TEXT("the task has started its production movement"),
			Task && Task->DidStartMovement()))
	{
		H.Teardown();
		return false;
	}

	const FWeaponActionToken ActionToken = H.MovementAbility->GetActionToken();
	TestTrue(TEXT("the active action token is valid"), ActionToken.IsValid());
	TestTrue(TEXT("Host owns movement for the exact live action"),
		H.Host->IsActionMovementOwnedBy(ActionToken));

	FMHGZRecordedTrajectoryTelemetry InitialTelemetry;
	CMC->GetRecordedTrajectoryTelemetry(ActionToken, InitialTelemetry);
	if (!TestTrue(TEXT("ProtectedFlight acquired a non-zero recorded-trajectory lease"),
		InitialTelemetry.Lease != 0))
	{
		H.Teardown();
		return false;
	}
	const uint64 Lease = InitialTelemetry.Lease;
	TestTrue(TEXT("the new lease is active"), InitialTelemetry.bActive);
	TestTrue(TEXT("the new lease starts at zero elapsed seconds"),
		FMath::IsNearlyZero(InitialTelemetry.ElapsedSeconds));
	TestEqual(TEXT("ProtectedFlight starts in MOVE_Flying"), CMC->MovementMode, MOVE_Flying);

	CMC->PerformMovement(0.05f);
	FMHGZRecordedTrajectoryTelemetry BeforeSwitch;
	CMC->GetRecordedTrajectoryTelemetry(ActionToken, BeforeSwitch);
	TestEqual(TEXT("the physical step keeps the same lease"), BeforeSwitch.Lease, Lease);
	TestTrue(TEXT("the physical step advances the lease clock before handoff"),
		BeforeSwitch.ElapsedSeconds > 0.0f);

	FWeaponActionToken WrongAction = ActionToken;
	++WrongAction.ActivationSequenceID;
	TestFalse(TEXT("the task rejects a wrong action identity"),
		Task->EnableRecordedTrajectoryLanding(WrongAction));
	const uint64 WrongLease = Lease + 1;
	TestFalse(TEXT("CMC rejects an incorrect recorded-trajectory lease"),
		CMC->EnableRecordedTrajectoryLanding(WrongLease));

	TestTrue(TEXT("the real task changes ProtectedFlight to LandingAirborne"),
		Task->EnableRecordedTrajectoryLanding(ActionToken));
	FMHGZRecordedTrajectoryTelemetry AfterSwitch;
	CMC->GetRecordedTrajectoryTelemetry(ActionToken, AfterSwitch);
	TestEqual(TEXT("the landing handoff preserves the exact lease"), AfterSwitch.Lease, Lease);
	TestTrue(TEXT("the landing handoff keeps the movement active"), AfterSwitch.bActive);
	TestTrue(TEXT("the landing handoff does not reset elapsed time"),
		FMath::IsNearlyEqual(AfterSwitch.ElapsedSeconds,
			BeforeSwitch.ElapsedSeconds, 0.0001f));

	// The CMC's public lease transition is deliberately idempotent. The task has
	// already changed its request-side policy, so repeat semantics are checked at
	// the actual lease owner rather than by fabricating a second task.
	TestTrue(TEXT("repeating the same CMC lease transition is idempotent"),
		CMC->EnableRecordedTrajectoryLanding(Lease));
	FMHGZRecordedTrajectoryTelemetry AfterRepeat;
	CMC->GetRecordedTrajectoryTelemetry(ActionToken, AfterRepeat);
	TestEqual(TEXT("an idempotent repeat keeps the lease"), AfterRepeat.Lease, Lease);
	TestTrue(TEXT("an idempotent repeat does not reset elapsed time"),
		FMath::IsNearlyEqual(AfterRepeat.ElapsedSeconds,
			BeforeSwitch.ElapsedSeconds, 0.0001f));

	CMC->PerformMovement(0.05f);
	FMHGZRecordedTrajectoryTelemetry AfterNextStep;
	CMC->GetRecordedTrajectoryTelemetry(ActionToken, AfterNextStep);
	TestTrue(TEXT("LandingAirborne continues the same lease clock after handoff"),
		AfterNextStep.Lease == Lease
			&& AfterNextStep.ElapsedSeconds > AfterRepeat.ElapsedSeconds);

	TArray<FMHGZAerialMovementPhaseSample> Samples;
	CMC->GetAerialMovementSamplesSince(SerialBeforeActivation, Samples);
	const auto HasPhase = [&Samples](const FName Phase)
	{
		return Samples.ContainsByPredicate([Phase](const FMHGZAerialMovementPhaseSample& Sample)
		{
			return Sample.Phase == Phase;
		});
	};
	TestTrue(TEXT("existing sparse telemetry exposes the real trajectory begin event"),
		HasPhase(FName(TEXT("RecordedTrajectory.Begin"))));
	TestTrue(TEXT("existing sparse telemetry exposes the real landing handoff event"),
		HasPhase(FName(TEXT("RecordedTrajectory.LandingEnabled"))));

	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM2DescendingThrustRecordedTrajectoryContactPreservesAuthoredVelocityTest,
	"MHGZ.M2.DescendingThrust.Foundation.RecordedTrajectoryContactPreservesAuthoredVelocity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM2DescendingThrustRecordedTrajectoryContactPreservesAuthoredVelocityTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;

	IConsoleVariable* TelemetryVariable = IConsoleManager::Get().FindConsoleVariable(
		TEXT("mhgz.Telemetry.Enable"));
	if (!TestNotNull(TEXT("the existing sparse aerial telemetry switch is available"),
		TelemetryVariable))
	{
		return false;
	}
	FScopedTelemetryCVar TelemetryGuard(TelemetryVariable);

	FDescendingThrustFoundationHarness H;
	if (!TestTrue(*FString::Printf(TEXT("real Host/Action harness builds (%s)"),
		*H.FailureStage), H.Build(FVector(0.0f, 0.0f, 1000.0f))))
	{
		H.Teardown();
		return false;
	}

	UMHGZInstrumentedCharacterMovementComponent* CMC =
		Cast<UMHGZInstrumentedCharacterMovementComponent>(
			H.Character->GetCharacterMovement());
	if (!TestNotNull(TEXT("the real instrumented CMC is available"), CMC))
	{
		H.Teardown();
		return false;
	}

	const float CapsuleHalfHeight = H.Character->GetCapsuleComponent()
		->GetScaledCapsuleHalfHeight();
	const float CapsuleRadius = H.Character->GetCapsuleComponent()
		->GetScaledCapsuleRadius();
	H.Character->SetActorLocation(FVector(0.0f, 0.0f, CapsuleHalfHeight + 5.0f),
		false, nullptr, ETeleportType::TeleportPhysics);
	AActor* ContactSurface = SpawnTiltedTrajectoryContactSurface(*H.World);
	if (!TestNotNull(TEXT("the tilted, blocking contact surface is spawned"), ContactSurface))
	{
		H.Teardown();
		return false;
	}

	FHitResult GeometryProbeHit;
	FCollisionQueryParams GeometryProbeParams(
		SCENE_QUERY_STAT(RecordedTrajectorySurfaceProbe), false, H.Character);
	const FVector GeometryProbeStart = H.Character->GetActorLocation();
	const bool bGeometryBlocksPawn = H.World->SweepSingleByChannel(
		GeometryProbeHit, GeometryProbeStart,
		GeometryProbeStart + FVector(0.0f, 0.0f, -40.0f), FQuat::Identity,
		ECC_Pawn, FCollisionShape::MakeCapsule(CapsuleRadius, CapsuleHalfHeight),
		GeometryProbeParams);
	TestTrue(TEXT("the test surface blocks a capsule sweep on the Pawn channel"),
		bGeometryBlocksPawn && GeometryProbeHit.GetActor() == ContactSurface);

	FWeaponMovementRequest Request = MakeProtectedTrajectoryRequest();
	Request.RecordedTrajectory = MakeHighSpeedSurfaceContactProfile();
	Request.Duration = Request.RecordedTrajectory.GetDuration();
	Request.TrajectoryGroundPolicy = ERecordedTrajectoryGroundPolicy::ProtectedFlight;
	if (!TestTrue(TEXT("the steep authored trajectory request is valid"),
		Request.HasValidRecordedTrajectoryParameters())
		|| !TestTrue(TEXT("the production movement task acquires the trajectory lease"),
			H.ActivateRecordedTrajectory(Request)))
	{
		H.Teardown();
		return false;
	}

	const FVector PositionBeforeMovement = H.Character->GetActorLocation();
	const uint64 SerialBeforeMovement = CMC->GetLatestAerialMovementSampleSerial();
	constexpr float StepSeconds = 1.0f / 60.0f;
	for (int32 Frame = 0; Frame < 12; ++Frame)
	{
		CMC->PerformMovement(StepSeconds);
	}

	FMHGZRecordedTrajectoryTelemetry RuntimeTelemetry;
	CMC->GetRecordedTrajectoryTelemetry(H.MovementAbility->GetActionToken(), RuntimeTelemetry);
	TestTrue(TEXT("the protected trajectory remains active after grazing the surface"),
		RuntimeTelemetry.bActive && CMC->MovementMode == MOVE_Flying);
	const FVector ActualTravel = H.Character->GetActorLocation() - PositionBeforeMovement;
	TestTrue(TEXT("leaving the compact surface does not preserve its old normal as a trajectory ramp"),
		FVector(ActualTravel.X, ActualTravel.Y, 0.0f).Size() < 110.0f);

	TArray<FMHGZAerialMovementPhaseSample> Samples;
	CMC->GetAerialMovementSamplesSince(SerialBeforeMovement, Samples);
	const bool bSawTiltedBlockingContact = Samples.ContainsByPredicate(
		[](const FMHGZAerialMovementPhaseSample& Sample)
		{
			return Sample.Phase == FName(TEXT("CMC.HandleImpact.Pre"))
				&& Sample.bImpactBlockingHit
				&& FMath::Abs(Sample.ImpactNormal.X) > 0.1f
				&& Sample.ImpactNormal.Z > 0.9f;
		});
	TestTrue(TEXT("the test trajectory actually grazes the tilted top surface"),
		bSawTiltedBlockingContact);

	const FVector ExpectedVelocity = MHGZRecordedLocalTrajectory::LocalToWorld(
		Request.GetRecordedTrajectoryVelocity(RuntimeTelemetry.ElapsedSeconds),
		Request.DirectionSnapshot);
	TestTrue(TEXT("surface contact does not convert the authored downward speed into lateral launch speed"),
		CMC->Velocity.Equals(ExpectedVelocity, 1.0f));

	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM2DescendingThrustDirectionSnapshotFrameGateTest,
	"MHGZ.M2.DescendingThrust.Foundation.DirectionSnapshotFrameGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM2DescendingThrustDirectionSnapshotFrameGateTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FScopedFrameCounterFloor FrameGuard;

	FDescendingThrustFoundationHarness H;
	if (!TestTrue(*FString::Printf(TEXT("real attack harness builds (%s)"),
		*H.FailureStage), H.Build(FVector::ZeroVector))
		|| !TestTrue(TEXT("the existing test attack activates through GAS"), H.ActivateAttack()))
	{
		H.Teardown();
		return false;
	}

	UMHGZM4TestAttackAbility* Attack = H.AttackAbility;
	if (!TestNotNull(TEXT("the active attack instance is available"), Attack))
	{
		H.Teardown();
		return false;
	}

	const FWeaponActionToken ActionToken = Attack->GetActionToken();
	const uint64 CurrentFrame = GFrameCounter;
	FActionMoveInputFrameSnapshot ValidSnapshot;
	ValidSnapshot.FrameNumber = CurrentFrame - 1;
	ValidSnapshot.Pawn = H.Character;
	ValidSnapshot.RuntimeToken = ActionToken.RuntimeToken;
	ValidSnapshot.bHasMoveInput = true;
	ValidSnapshot.WorldDirection = FRotator(0.0f, 20.0f, 0.0f).Vector();

	H.Character->SetActorRotation(FRotator::ZeroRotator);
	TestTrue(TEXT("the exact N-1 snapshot is accepted"),
		Attack->ApplyInActionDirectionCorrectionFromSnapshot(ActionToken,
			ValidSnapshot, 30.0f));
	TestTrue(TEXT("the accepted N-1 snapshot applies its direction"),
		FMath::IsNearlyEqual(H.Character->GetActorRotation().Yaw, 20.0f, 0.1f));

	auto ExpectRejected = [this, &H, Attack, &ActionToken](
		const TCHAR* Description, const FActionMoveInputFrameSnapshot& Snapshot)
	{
		H.Character->SetActorRotation(FRotator::ZeroRotator);
		TestFalse(Description,
			Attack->ApplyInActionDirectionCorrectionFromSnapshot(ActionToken,
				Snapshot, 30.0f));
		TestTrue(TEXT("a rejected snapshot leaves the actor yaw unchanged"),
			FMath::IsNearlyZero(H.Character->GetActorRotation().Yaw, 0.1f));
	};

	FActionMoveInputFrameSnapshot OldFrame = ValidSnapshot;
	OldFrame.FrameNumber = CurrentFrame >= 2 ? CurrentFrame - 2 : 0;
	ExpectRejected(TEXT("an older-than-N-1 snapshot is rejected"), OldFrame);

	FActionMoveInputFrameSnapshot CurrentFrameSnapshot = ValidSnapshot;
	CurrentFrameSnapshot.FrameNumber = CurrentFrame;
	ExpectRejected(TEXT("a current-frame snapshot is rejected"), CurrentFrameSnapshot);

	AMHGZM5KinsectSlashMovementTestCharacter* OtherPawn =
		H.World->SpawnActor<AMHGZM5KinsectSlashMovementTestCharacter>();
	if (!TestNotNull(TEXT("a second pawn is available for the wrong-Pawn negative"), OtherPawn))
	{
		H.Teardown();
		return false;
	}
	FActionMoveInputFrameSnapshot WrongPawn = ValidSnapshot;
	WrongPawn.Pawn = OtherPawn;
	ExpectRejected(TEXT("a snapshot belonging to another Pawn is rejected"), WrongPawn);

	FActionMoveInputFrameSnapshot WrongRuntime = ValidSnapshot;
	++WrongRuntime.RuntimeToken.Generation;
	ExpectRejected(TEXT("a snapshot from another Runtime generation is rejected"), WrongRuntime);

	H.Teardown();
	return true;
}

#endif
