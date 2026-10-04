// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Animation/MHGZMotionMatchingAnimInstance.h"
#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "Engine/World.h"
#include "MHGZPlayerCameraManager.h"
#include "MHGZPlayerState.h"
#include "../Shared/MHGZM5KinsectSlashTestTypes.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

namespace
{
struct FActionVisualPitchHostHarness
{
	UWorld* World = nullptr;
	AMHGZM5KinsectSlashMovementTestCharacter* Character = nullptr;
	AMHGZPlayerState* PlayerState = nullptr;
	UMHGZAbilitySystemComponent* ASC = nullptr;
	UMHGZWeaponRuntimeHostComponent* Host = nullptr;

	bool Build()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false);
		if (!World)
		{
			return false;
		}
		Character = World->SpawnActor<AMHGZM5KinsectSlashMovementTestCharacter>();
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
		return Host->GetCurrentToken().IsValid();
	}

	FWeaponActionToken RegisterAction()
	{
		FWeaponActionToken Action;
		Action.RuntimeToken = Host->GetCurrentToken();
		Action.AbilityHandle = ASC->GiveAbility(FGameplayAbilitySpec(
			UMHGZM5KinsectSlashMovementTestAbility::StaticClass(), 1, INDEX_NONE, ASC));
		Action.ActivationSequenceID = Host->AllocateActivationSequenceID();
		Action.AbilityInstance = NewObject<UMHGZM5KinsectSlashMovementTestAbility>(ASC);
		if (!Action.IsValid() || !Host->RegisterAction(Action)
			|| !Host->RegisterMontage(Action, Character->GetMesh(),
				static_cast<int32>(Action.ActivationSequenceID + 100)))
		{
			return FWeaponActionToken();
		}
		return Action;
	}

	int32 MontageID(const FWeaponActionToken& Action) const
	{
		return static_cast<int32>(Action.ActivationSequenceID + 100);
	}

	void Teardown()
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZActionVisualPitchCoordinateTest,
	"MHGZ.M4.KinsectSlash.ActionVisualPitch.ComponentSpaceCoordinates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZActionVisualPitchCoordinateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const AMHGZPlayerCameraManager* CameraDefaults =
		AMHGZPlayerCameraManager::StaticClass()->GetDefaultObject<AMHGZPlayerCameraManager>();
	TestNotNull(TEXT("Camera manager CDO exists"), CameraDefaults);
	if (!CameraDefaults)
	{
		return false;
	}
	TestTrue(TEXT("Camera lower pitch limit matches the measured value"),
		FMath::IsNearlyEqual(CameraDefaults->ViewPitchMin, -66.09f, 0.001f));
	TestTrue(TEXT("Camera upper pitch limit matches the measured value"),
		FMath::IsNearlyEqual(CameraDefaults->ViewPitchMax, 56.25f, 0.001f));

	const float Yaws[] = { 0.0f, 90.0f, 180.0f, -90.0f };
	const float Pitches[] = { -60.0f, -20.0f, 0.0f, 35.0f, 55.0f };
	const FRotator MeshBaselines[] = {
		FRotator::ZeroRotator,
		FRotator(17.0f, 33.0f, -11.0f),
		FRotator(-8.0f, 147.0f, 24.0f)
	};
	for (const float Yaw : Yaws)
	{
		for (const float Pitch : Pitches)
		{
			const FVector Facing = FRotator(0.0f, Yaw, 0.0f).Vector();
			const FVector Target = FRotator(Pitch, Yaw, 0.0f).Vector();
			for (const FRotator& MeshBaseline : MeshBaselines)
			{
				const FQuat MeshWorld = MeshBaseline.Quaternion();
				FQuat ComponentCorrection = FQuat::Identity;
				const FString Case = FString::Printf(
					TEXT("yaw %.0f pitch %.0f mesh %s"), Yaw, Pitch,
					*MeshBaseline.ToString());
				TestTrue(*FString::Printf(TEXT("Build correction (%s)"), *Case),
					UMHGZMotionMatchingAnimInstance::BuildActionVisualPitchComponentRotation(
						Facing, Target, MeshWorld, ComponentCorrection));

				const FVector ReconstructedWorld = MeshWorld.RotateVector(
					ComponentCorrection.RotateVector(MeshWorld.UnrotateVector(Facing)));
				TestTrue(*FString::Printf(
					TEXT("Component correction maps facing to locked direction (%s)"), *Case),
					ReconstructedWorld.Equals(Target, 0.001f));
				TestTrue(*FString::Printf(
					TEXT("Correction is a normalized finite quaternion (%s)"), *Case),
					!ComponentCorrection.ContainsNaN()
					&& FMath::IsNearlyEqual(ComponentCorrection.Size(), 1.0f, 0.001f));
			}
		}
	}

	FQuat InvalidResult = FQuat::Identity;
	TestFalse(TEXT("Zero facing is rejected"),
		UMHGZMotionMatchingAnimInstance::BuildActionVisualPitchComponentRotation(
			FVector::ZeroVector, FVector::UpVector, FQuat::Identity, InvalidResult));
	TestTrue(TEXT("Rejected input leaves identity correction"),
		InvalidResult.Equals(FQuat::Identity, 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZActionVisualPitchHostOwnershipTest,
	"MHGZ.M4.KinsectSlash.ActionVisualPitch.HostOwnershipAndRetirement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZActionVisualPitchHostOwnershipTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FActionVisualPitchHostHarness H;
	if (!TestTrue(TEXT("Host harness builds"), H.Build()))
	{
		H.Teardown();
		return false;
	}

	const FWeaponActionToken FirstAction = H.RegisterAction();
	const FWeaponActionToken SecondAction = H.RegisterAction();
	if (!TestTrue(TEXT("two exact actions are registered"),
		FirstAction.IsValid() && SecondAction.IsValid()))
	{
		H.Teardown();
		return false;
	}

	const int32 FirstMontageID = H.MontageID(FirstAction);
	const int32 SecondMontageID = H.MontageID(SecondAction);
	TestFalse(TEXT("zero direction is rejected"),
		H.Host->TrySetActionVisualPitchState(FirstAction, FirstMontageID,
			FVector::ZeroVector, 0.1f, 0.1f));
	TestFalse(TEXT("unregistered Montage instance is rejected"),
		H.Host->TrySetActionVisualPitchState(FirstAction, FirstMontageID + 1,
			FVector::UpVector, 0.1f, 0.1f));

	TestTrue(TEXT("exact active action/montage can publish pitch"),
		H.Host->TrySetActionVisualPitchState(FirstAction, FirstMontageID,
			FVector(0.2f, 0.3f, 0.9f), 0.1f, 0.1f));
	FWeaponActionVisualPitchState State;
	H.Host->GetActionVisualPitchState(State);
	TestTrue(TEXT("published state is owned by the exact first action"),
		State.bValid && State.OwnerAction == FirstAction
		&& State.SourceMontageInstanceID == FirstMontageID);
	TestTrue(TEXT("published locked direction is normalized"),
		FMath::IsNearlyEqual(State.LockedDirection.Size(), 1.0f, 0.001f));

	TestFalse(TEXT("a different active action cannot steal a live pitch state"),
		H.Host->TrySetActionVisualPitchState(SecondAction, SecondMontageID,
			FVector::ForwardVector, 0.1f, 0.1f));
	TestFalse(TEXT("stale action cannot release the first action state"),
		H.Host->ReleaseActionVisualPitch(SecondAction, SecondMontageID,
			EActionVisualPitchReleaseMode::ImmediateReset));
	H.Host->GetActionVisualPitchState(State);
	TestTrue(TEXT("rejected takeover/release leaves owner unchanged"),
		State.bValid && !State.bReleasing && State.OwnerAction == FirstAction);

	const uint64 ActiveRevision = State.StateRevision;
	TestTrue(TEXT("matching action can begin visual blend-out"),
		H.Host->ReleaseActionVisualPitch(FirstAction, FirstMontageID,
			EActionVisualPitchReleaseMode::BlendOut));
	H.Host->GetActionVisualPitchState(State);
	const uint64 RetiringRevision = State.StateRevision;
	TestTrue(TEXT("blend-out retains only a zero-target retirement snapshot"),
		State.bValid && State.bReleasing && State.TargetWeight == 0.0f
		&& RetiringRevision != ActiveRevision);
	TestFalse(TEXT("stale AnimInstance acknowledgement cannot retire newer revision"),
		H.Host->AcknowledgeActionVisualPitchRetirement(ActiveRevision));
	TestTrue(TEXT("new action may take over a retiring visual state"),
		H.Host->TrySetActionVisualPitchState(SecondAction, SecondMontageID,
			FVector::ForwardVector, 0.1f, 0.1f));
	H.Host->GetActionVisualPitchState(State);
	const uint64 SecondRevision = State.StateRevision;
	TestTrue(TEXT("new action owns the replacement state"),
		State.bValid && !State.bReleasing && State.OwnerAction == SecondAction);
	TestFalse(TEXT("old retirement acknowledgement cannot clear replacement owner"),
		H.Host->AcknowledgeActionVisualPitchRetirement(RetiringRevision));
	TestTrue(TEXT("matching revision retires a completed blend-out"),
		H.Host->ReleaseActionVisualPitch(SecondAction, SecondMontageID,
			EActionVisualPitchReleaseMode::BlendOut));
	H.Host->GetActionVisualPitchState(State);
	TestTrue(TEXT("replacement state has its own later revision"),
		State.StateRevision != SecondRevision);
	TestTrue(TEXT("only current retirement revision can acknowledge cleanup"),
		H.Host->AcknowledgeActionVisualPitchRetirement(State.StateRevision));
	H.Host->GetActionVisualPitchState(State);
	TestFalse(TEXT("acknowledged retirement clears the visual state"), State.bValid);

	const FWeaponActionToken ThirdAction = H.RegisterAction();
	TestTrue(TEXT("third registered action is valid"), ThirdAction.IsValid());
	TestTrue(TEXT("third action can publish after retirement"),
		H.Host->TrySetActionVisualPitchState(ThirdAction, H.MontageID(ThirdAction),
			FVector::UpVector, 0.1f, 0.1f));
	TestTrue(TEXT("exceptional cancellation supports immediate reset"),
		H.Host->ReleaseActionVisualPitch(ThirdAction, H.MontageID(ThirdAction),
			EActionVisualPitchReleaseMode::ImmediateReset));
	H.Host->GetActionVisualPitchState(State);
	TestFalse(TEXT("immediate reset does not leave a retiring snapshot"), State.bValid);

	uint64 LastSerial = 0;
	TArray<FWeaponActionVisualPitchTelemetryEvent> Events;
	H.Host->GetActionVisualPitchTelemetrySince(LastSerial, Events);
	TestTrue(TEXT("sparse lifecycle telemetry contains ordered state changes"),
		Events.Num() >= 7 && LastSerial == Events.Last().Serial);
	for (int32 Index = 1; Index < Events.Num(); ++Index)
	{
		TestTrue(TEXT("telemetry serial strictly increases"),
			Events[Index].Serial > Events[Index - 1].Serial);
	}

	H.Teardown();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
