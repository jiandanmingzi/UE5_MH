// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "ActionSystem/AbilityTask_MHGZWeaponMovement.h"
#include "ActionSystem/MHGZKinsectSlashAbility.h"
#include "AttributeSystem/Res_InsectGlaive.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MHGZPlayerState.h"
#include "../../Shared/MHGZM5KinsectSlashTestTypes.h"
#include "MHGZM5KinsectSlashDamageTestTypes.h"
#include "Misc/DataValidation.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

namespace
{
FGameplayTag KinsectSlashTestTag(const TCHAR* Name)
{
	return FGameplayTag::RequestGameplayTag(Name);
}

struct FKinsectSlashGateHarness
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
	FMHGZM5KinsectSlashHostBudgetOwnershipTest,
	"MHGZ.M5.KinsectSlash.HostBudgetOwnershipAndLandingCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5KinsectSlashHostBudgetOwnershipTest::RunTest(const FString& Parameters)
{
	FKinsectSlashGateHarness H;
	if (!TestTrue(TEXT("runtime harness builds"), H.Build()))
	{
		H.Teardown();
		return false;
	}

	const FGameplayTag CantAttack = KinsectSlashTestTag(
		TEXT("Combat.State.Aerial.CantAttack"));
	const FGameplayTag CantDodge = KinsectSlashTestTag(
		TEXT("Combat.State.Aerial.CantDodge"));

	// An external owner already holds both tags. Dance reset must release only
	// the Host's own token, not clear another system's counted loose tag.
	H.ASC->SetLooseGameplayTagCount(CantAttack, 1);
	H.ASC->SetLooseGameplayTagCount(CantDodge, 1);
	TestTrue(TEXT("Host acquires aerial attack budget"), H.Host->MarkAerialAttackUsed());
	TestTrue(TEXT("aerial attack acquire is idempotent"), H.Host->MarkAerialAttackUsed());
	TestTrue(TEXT("Host acquires aerial dodge budget"), H.Host->MarkAerialDodgeUsed());
	TestTrue(TEXT("aerial dodge acquire is idempotent"), H.Host->MarkAerialDodgeUsed());
	TestEqual(TEXT("external plus Host CantAttack owner are both counted"),
		H.ASC->GetTagCount(CantAttack), 2);
	TestEqual(TEXT("external plus Host CantDodge owner are both counted"),
		H.ASC->GetTagCount(CantDodge), 2);

	TestTrue(TEXT("Dance budget reset succeeds"), H.Host->ResetAerialActionBudgetOnDance());
	TestEqual(TEXT("Dance reset releases only Host CantAttack ownership"),
		H.ASC->GetTagCount(CantAttack), 1);
	TestEqual(TEXT("Dance reset releases only Host CantDodge ownership"),
		H.ASC->GetTagCount(CantDodge), 1);
	TestTrue(TEXT("repeated Dance reset is safe"), H.Host->ResetAerialActionBudgetOnDance());
	TestEqual(TEXT("repeated reset leaves external CantAttack intact"),
		H.ASC->GetTagCount(CantAttack), 1);

	H.ASC->SetLooseGameplayTagCount(CantAttack, 0);
	H.ASC->SetLooseGameplayTagCount(CantDodge, 0);
	TestTrue(TEXT("Host can spend each budget again after reset"),
		H.Host->MarkAerialAttackUsed() && H.Host->MarkAerialDodgeUsed());
	TestEqual(TEXT("spent attack budget is present"), H.ASC->GetTagCount(CantAttack), 1);
	TestEqual(TEXT("spent dodge budget is present"), H.ASC->GetTagCount(CantDodge), 1);

	// Cancellation is intentionally not a refund: only landing releases these
	// Host-owned tokens. The test models an action ending without touchdown.
	TestTrue(TEXT("a canceled airborne action leaves the attack budget spent"),
		H.ASC->HasMatchingGameplayTag(CantAttack));
	H.Host->SetGrounded(false);
	TestTrue(TEXT("Host enters its falling state before landing"),
		H.Host->AcquireAerialFallingState());
	H.Host->HandleLanded();
	TestTrue(TEXT("Host landing cleanup restores the grounded state"), H.Host->IsGrounded());
	TestFalse(TEXT("landing releases Host Falling ownership"), H.ASC->HasMatchingGameplayTag(
		KinsectSlashTestTag(TEXT("Combat.State.Aerial.Falling"))));
	TestFalse(TEXT("landing releases Host CantAttack"),
		H.ASC->HasMatchingGameplayTag(CantAttack));
	TestFalse(TEXT("landing releases Host CantDodge"),
		H.ASC->HasMatchingGameplayTag(CantDodge));

	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5KinsectSlashLiveActivationGateTest,
	"MHGZ.M5.KinsectSlash.LiveAirFallingUnsheathedGateWithoutActionable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5KinsectSlashLiveActivationGateTest::RunTest(const FString& Parameters)
{
	FKinsectSlashGateHarness H;
	if (!TestTrue(TEXT("runtime harness builds"), H.Build()))
	{
		H.Teardown();
		return false;
	}

	FGameplayAbilityActorInfo ActorInfo;
	ActorInfo.InitFromActor(H.Character, H.Character, H.ASC);
	const FGameplayAbilitySpecHandle AbilityHandle = H.ASC->GiveAbility(
		FGameplayAbilitySpec(UMHGZKinsectSlashAbility::StaticClass(), 1, INDEX_NONE, H.ASC));
	if (!TestTrue(TEXT("production Kinsect Slash ability is granted"), AbilityHandle.IsValid()))
	{
		H.Teardown();
		return false;
	}
	const UMHGZKinsectSlashAbility* Ability =
		UMHGZKinsectSlashAbility::StaticClass()->GetDefaultObject<UMHGZKinsectSlashAbility>();
	const auto CanActivate = [&]()
	{
		return Ability->CanActivateAbility(AbilityHandle, &ActorInfo,
			nullptr, nullptr, nullptr);
	};

	H.Host->SetGrounded(false);
	TestFalse(TEXT("airborne without Unsheathed is rejected"), CanActivate());
	H.ASC->SetLooseGameplayTagCount(KinsectSlashTestTag(
		TEXT("Combat.State.Unsheathed")), 1);
	TestTrue(TEXT("Host acquires the Falling state after the action becomes airborne"),
		H.Host->AcquireAerialFallingState());
	TestTrue(TEXT("Host is bound to the test ASC"), H.ASC->GetRuntimeHost() == H.Host);
	TestFalse(TEXT("Host reports airborne"), H.Host->IsGrounded());
	TestTrue(TEXT("Falling tag is live"), H.ASC->HasMatchingGameplayTag(
		KinsectSlashTestTag(TEXT("Combat.State.Aerial.Falling"))));
	TestFalse(TEXT("activation does not need the legacy Actionable tag"), H.ASC->HasMatchingGameplayTag(
		KinsectSlashTestTag(TEXT("Combat.State.Aerial.Actionable"))));
	TestTrue(TEXT("Unsheathed tag is live"), H.ASC->HasMatchingGameplayTag(
		KinsectSlashTestTag(TEXT("Combat.State.Unsheathed"))));
	TestTrue(TEXT("GAS tag requirements pass with the authored live tags"),
		Ability->DoesAbilitySatisfyTagRequirements(*H.ASC));
	TestTrue(TEXT("live Falling + Unsheathed permits activation without Actionable or a pending snapshot"),
		CanActivate());

	// An authored movement lead releases Falling until its handoff notify.
	// A stale legacy Actionable tag must not reopen that lead's activation gate.
	H.Host->ReleaseAerialFallingState();
	H.ASC->SetLooseGameplayTagCount(KinsectSlashTestTag(
		TEXT("Combat.State.Aerial.Actionable")), 1);
	TestFalse(TEXT("the movement lead has no Falling tag"), H.ASC->HasMatchingGameplayTag(
		KinsectSlashTestTag(TEXT("Combat.State.Aerial.Falling"))));
	TestTrue(TEXT("the stale Actionable tag remains during the movement lead"),
		H.ASC->HasMatchingGameplayTag(KinsectSlashTestTag(TEXT("Combat.State.Aerial.Actionable"))));
	TestFalse(TEXT("GAS requires Falling even when Actionable is present"),
		Ability->DoesAbilitySatisfyTagRequirements(*H.ASC));
	TestFalse(TEXT("missing Falling blocks activation even when Actionable is present"),
		CanActivate());
	H.ASC->SetLooseGameplayTagCount(KinsectSlashTestTag(
		TEXT("Combat.State.Aerial.Actionable")), 0);
	TestTrue(TEXT("handoff restores the Host-owned Falling gate"),
		H.Host->AcquireAerialFallingState());
	TestTrue(TEXT("handoff permits activation without Actionable"), CanActivate());

	TestTrue(TEXT("Host spends CantAttack"), H.Host->MarkAerialAttackUsed());
	TestFalse(TEXT("CantAttack blocks a second aerial attack"), CanActivate());
	TestTrue(TEXT("successful Dance resets Host-owned action budgets"),
		H.Host->ResetAerialActionBudgetOnDance());
	TestTrue(TEXT("activation gate reopens after Dance reset"), CanActivate());

	H.ASC->SetLooseGameplayTagCount(KinsectSlashTestTag(TEXT("Combat.State.Dead")), 1);
	TestFalse(TEXT("Dead blocks activation"), CanActivate());
	H.ASC->SetLooseGameplayTagCount(KinsectSlashTestTag(TEXT("Combat.State.Dead")), 0);
	H.ASC->SetLooseGameplayTagCount(KinsectSlashTestTag(TEXT("Combat.State.Hitstun")), 1);
	TestFalse(TEXT("Hitstun blocks activation"), CanActivate());
	H.ASC->SetLooseGameplayTagCount(KinsectSlashTestTag(TEXT("Combat.State.Hitstun")), 0);
	H.ASC->SetLooseGameplayTagCount(KinsectSlashTestTag(TEXT("Combat.State.Knockdown")), 1);
	TestFalse(TEXT("Knockdown blocks activation"), CanActivate());
	H.ASC->SetLooseGameplayTagCount(KinsectSlashTestTag(TEXT("Combat.State.Knockdown")), 0);

	H.Host->SetGrounded(true);
	TestFalse(TEXT("grounded state blocks activation while Unsheathed remains"),
		CanActivate());

	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5KinsectSlashDeprecatedActionableValidationTest,
	"MHGZ.M5.KinsectSlash.ValidationRejectsDeprecatedActionableRequirement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5KinsectSlashDeprecatedActionableValidationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UMHGZKinsectSlashAbility* Ability = NewObject<UMHGZKinsectSlashAbility>();
	FStructProperty* RequiredTagsProperty = FindFProperty<FStructProperty>(
		Ability->GetClass(), TEXT("ActivationRequiredTags"));
	if (!TestNotNull(TEXT("activation requirements are available for a transient stale-asset fixture"),
		RequiredTagsProperty))
	{
		return false;
	}
	FGameplayTagContainer* RequiredTags =
		RequiredTagsProperty->ContainerPtrToValuePtr<FGameplayTagContainer>(Ability);
	const FGameplayTag ActionableTag = KinsectSlashTestTag(TEXT("Combat.State.Aerial.Actionable"));
	const auto HasDeprecatedGateDiagnostic = [](const FDataValidationContext& Context)
	{
		for (const FDataValidationContext::FIssue& Issue : Context.GetIssues())
		{
			const FString Message = Issue.Message.ToString();
			if (Message.Contains(TEXT("ActivationRequiredTags"))
				&& Message.Contains(TEXT("Combat.State.Aerial.Actionable")))
			{
				return true;
			}
		}
		return false;
	};

	FDataValidationContext NativeContext;
	Ability->IsDataValid(NativeContext);
	TestFalse(TEXT("native defaults have no deprecated Actionable requirement diagnostic"),
		HasDeprecatedGateDiagnostic(NativeContext));
	RequiredTags->AddTag(ActionableTag);
	FDataValidationContext StaleAssetContext;
	TestEqual(TEXT("a stale serialized Actionable requirement is invalid"),
		Ability->IsDataValid(StaleAssetContext), EDataValidationResult::Invalid);
	TestTrue(TEXT("the stale-asset diagnostic names ActivationRequiredTags and Actionable"),
		HasDeprecatedGateDiagnostic(StaleAssetContext));
	RequiredTags->RemoveTag(ActionableTag);
	FDataValidationContext MigratedContext;
	Ability->IsDataValid(MigratedContext);
	TestFalse(TEXT("removing the stale requirement clears its diagnostic"),
		HasDeprecatedGateDiagnostic(MigratedContext));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5KinsectSlashDanceEntryTuningTest,
	"MHGZ.M5.KinsectSlash.LegacyDanceEntryMovementOptOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5KinsectSlashDanceEntryTuningTest::RunTest(const FString& Parameters)
{
	const FWeaponActionToken Token;
	FKinsectSlashDanceEntryTuning Tuning;
	Tuning.bUseCapturedTrajectory = false;
	Tuning.ApexHeight = 85.0f;
	Tuning.Duration = 0.42f;
	Tuning.HorizontalDistance = 140.0f;
	TestTrue(TEXT("apex/duration dance entry requires independent positive truth"),
		Tuning.IsValid());
	const FWeaponMovementRequest DashEntry = Tuning.MakeRequest(Token, FVector::ForwardVector);
	TestEqual(TEXT("explicit legacy opt-out still uses the owned BallisticVault executor"),
		DashEntry.Mode, EWeaponMovementMode::BallisticVault);
	TestTrue(TEXT("first dance entry has its authored apex/duration/distance"),
		FMath::IsNearlyEqual(DashEntry.ApexHeight, 85.0f)
			&& FMath::IsNearlyEqual(DashEntry.Duration, 0.42f)
			&& FMath::IsNearlyEqual(DashEntry.MaxDistance, 140.0f));

	Tuning.ParameterMode = EBallisticParameterMode::ExplicitLaunchVelocity;
	Tuning.ExplicitLaunchVelocity = FVector(100.0f, 0.0f, 300.0f);
	TestTrue(TEXT("explicit velocity entry validates"), Tuning.IsValid());
	const FVector Left = FVector::LeftVector;
	const FWeaponMovementRequest MissEntry = Tuning.MakeRequest(Token, Left);
	TestTrue(TEXT("second dance entry rotates its local impulse by the locked yaw"),
		MissEntry.LaunchVelocity.Equals(FVector(0.0f, -100.0f, 300.0f), 0.01f));
	Tuning.ExplicitLaunchVelocity.Z = 0.0f;
	TestFalse(TEXT("explicit dance entry without positive lift is rejected"), Tuning.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5KinsectSlashDancePreparationGuardTest,
	"MHGZ.M5.KinsectSlash.DancePreparationRejectsStaleCallbacksAndClearsTimers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5KinsectSlashDancePreparationGuardTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FMHGZM5KinsectSlashDamageTestHarness H;
	if (!TestTrue(TEXT("equipped IG timer harness builds"), H.Build())
		|| !TestTrue(TEXT("production-derived action activates"), H.ActivateAbility()))
	{
		H.Teardown();
		return false;
	}

	UMHGZKinsectSlashAbility* Ability = H.Ability;
	UCharacterMovementComponent* CMC = H.Character->GetCharacterMovement();
	const FWeaponActionToken Token = Ability->GetActionToken();
	TestTrue(TEXT("initial hover has the captured miss gravity after takeover"),
		FMath::IsNearlyEqual(FMath::Abs(CMC->GetGravityZ()), 3096.0f, 0.1f));
	TestEqual(TEXT("initial hover does not introduce uniform miss braking"),
		CMC->BrakingDecelerationFalling, 0.0f);

	if (!Ability->HoverMovementTask
		|| !TestTrue(TEXT("test retires the initial hover with the same-action contract"),
			Ability->HoverMovementTask->EndHoverForImmediateMovementHandoff(Token)))
	{
		H.Teardown();
		return false;
	}
	Ability->HoverMovementTask = nullptr;
	CMC->SetMovementMode(MOVE_Falling);
	Ability->Phase = EKinsectSlashPhase::Dash;
	Ability->MovementActionToken = Token;
	TestTrue(TEXT("MissFall phase/source can commit before the dash task completion callback"),
		Ability->EnterMissFallPhase());
	TestEqual(TEXT("pre-completion Miss188 transition publishes the Miss source"),
		H.Host->GetAerialMovementSource(), KinsectSlashTestTag(
			TEXT("Combat.State.Aerial.Source.IG_KinsectSlash.Miss")));
	FWeaponMovementResult DashResult;
	DashResult.EndReason = EWeaponMovementEndReason::Completed;
	DashResult.FinalVelocity = FVector(220.0f, 30.0f, 180.0f);
	Ability->HandleMovementFinished(DashResult);
	TestEqual(TEXT("natural dash completion enters MissFall"),
		Ability->Phase, EKinsectSlashPhase::MissFall);
	TestTrue(TEXT("Miss188 inherits all three components, including upward terminal velocity"),
		CMC->Velocity.Equals(DashResult.FinalVelocity, 0.001f));
	TestTrue(TEXT("natural dash completion reapplies measured miss gravity"),
		FMath::IsNearlyEqual(FMath::Abs(CMC->GetGravityZ()), 3096.0f, 0.1f));
	TestEqual(TEXT("natural miss has no speculative global braking"),
		CMC->BrakingDecelerationFalling, 0.0f);

	Ability->Phase = EKinsectSlashPhase::DancePending;
	H.Character->SetActorRotation(FRotator(0.0f, 37.0f, 0.0f));
	if (!TestTrue(TEXT("189 preparation starts the real stationary-hover task"),
		Ability->StartDancePreparationMovement()))
	{
		H.Teardown();
		return false;
	}
	UAbilityTask_MHGZWeaponMovement* PrepHover = Ability->HoverMovementTask;
	Ability->bDanceMontageActive = true;
	Ability->bDanceEntryCommitted = true;
	Ability->bSuccessfulDamage = true;
	Ability->bPendingDanceTransition = true;
	Ability->MontageActionToken = Token;
	const int32 UnregisteredMontageID = 91;
	FTimerManager& Timers = H.World->GetTimerManager();
	const auto ScheduleTimers = [&]()
	{
		Timers.SetTimer(Ability->DancePreparationTimer,
			FTimerDelegate::CreateUObject(Ability,
				&UMHGZKinsectSlashAbility::HandleDancePreparationFinished,
				Token, UnregisteredMontageID), Ability->DanceFromDashTuning.PrepDuration, false);
		Timers.SetTimer(Ability->QueuedDanceTransitionTimer,
			FTimerDelegate::CreateUObject(Ability,
				&UMHGZKinsectSlashAbility::HandleQueuedDanceTransition,
				Token, UnregisteredMontageID), 1.0f, false);
	};
	ScheduleTimers();
	const FTimerHandle PrepTimer = Ability->DancePreparationTimer;
	const FTimerHandle QueuedTimer = Ability->QueuedDanceTransitionTimer;
	const int32 DanceStacks = H.Resource->GetDanceStacks();
	FWeaponActionToken StaleToken = Token;
	++StaleToken.ActivationSequenceID;
	Ability->HandleDancePreparationFinished(StaleToken, UnregisteredMontageID);
	Ability->HandleQueuedDanceTransition(StaleToken, UnregisteredMontageID);
	Ability->HandleDancePreparationFinished(Token, UnregisteredMontageID);
	TestTrue(TEXT("stale action and unregistered montage callbacks preserve the current prep timer"),
		Ability->DancePreparationTimer == PrepTimer && Timers.TimerExists(PrepTimer));
	TestTrue(TEXT("stale queued transition cannot consume a newer transition timer"),
		Ability->QueuedDanceTransitionTimer == QueuedTimer && Timers.TimerExists(QueuedTimer));
	TestFalse(TEXT("an early serialized DanceBegin with no exact montage cannot start flight"),
		Ability->HandleMontageEvent(Token, UnregisteredMontageID, EKinsectSlashMontageEvent::DanceBegin));
	TestTrue(TEXT("rejected callbacks keep the preparation hover and phase"),
		Ability->HoverMovementTask == PrepHover && Ability->MovementTask == nullptr
			&& Ability->Phase == EKinsectSlashPhase::DancePending);
	TestEqual(TEXT("rejected callbacks cannot add another Dance stack"),
		H.Resource->GetDanceStacks(), DanceStacks);
	TestTrue(TEXT("preparation keeps the actor's original yaw"),
		FMath::IsNearlyEqual(H.Character->GetActorRotation().Yaw, 37.0f, 0.001f));

	Ability->HandleMontageCompleted();
	TestTrue(TEXT("a short montage finishing during prep keeps the committed action alive"),
		Ability->IsActive() && Ability->bMontageFinished && Timers.TimerExists(PrepTimer));
	H.ASC->SetLooseGameplayTagCount(KinsectSlashTestTag(TEXT("Combat.State.Hitstun")), 1);
	TestFalse(TEXT("lifecycle cancellation ends the pending Dance"), Ability->IsActive());
	TestFalse(TEXT("cancellation removes the scheduled preparation callback"),
		Timers.TimerExists(PrepTimer) || Ability->DancePreparationTimer.IsValid());
	TestFalse(TEXT("cancellation removes the queued transition callback"),
		Timers.TimerExists(QueuedTimer) || Ability->QueuedDanceTransitionTimer.IsValid());
	TestTrue(TEXT("airborne cleanup uses Falling without a false Walking bounce"),
		!H.Host->IsGrounded() && CMC->MovementMode == MOVE_Falling);
	TestTrue(TEXT("unowned cancellation restores the normal configured fall profile"),
		FMath::IsNearlyEqual(CMC->GravityScale, 2.0f, 0.001f));
	Ability->HandleDancePreparationFinished(Token, UnregisteredMontageID);
	TestTrue(TEXT("a callback arriving after cancellation cannot resurrect tasks"),
		!Ability->IsActive() && Ability->Phase == EKinsectSlashPhase::Ended
			&& !Ability->MovementTask && !Ability->HoverMovementTask);

	ScheduleTimers();
	const FTimerHandle ResetPrepTimer = Ability->DancePreparationTimer;
	const FTimerHandle ResetQueuedTimer = Ability->QueuedDanceTransitionTimer;
	Ability->ResetActivationState();
	TestFalse(TEXT("activation reset clears both old timer entries"),
		Timers.TimerExists(ResetPrepTimer) || Timers.TimerExists(ResetQueuedTimer));
	TestFalse(TEXT("activation reset invalidates both local timer handles"),
		Ability->DancePreparationTimer.IsValid() || Ability->QueuedDanceTransitionTimer.IsValid());
	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5KinsectSlashMontageCompletionRaceTest,
	"MHGZ.M5.KinsectSlash.PendingLateHitSurvivesMontageCompletion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5KinsectSlashMontageCompletionRaceTest::RunTest(const FString& Parameters)
{
	UMHGZKinsectSlashAbility* Ability = NewObject<UMHGZKinsectSlashAbility>();
	if (!TestNotNull(TEXT("production GA test instance exists"), Ability))
	{
		return false;
	}

	Ability->Phase = EKinsectSlashPhase::MissFall;
	Ability->bPendingDanceTransition = false;
	Ability->bSuccessfulDamage = false;
	TestFalse(TEXT("ordinary MissFall completion is not deferred"),
		Ability->ShouldDeferMontageCompletionForPendingDance());

	Ability->bPendingDanceTransition = true;
	TestFalse(TEXT("a queued transition without successful GE is not deferred"),
		Ability->ShouldDeferMontageCompletionForPendingDance());

	Ability->bSuccessfulDamage = true;
	TestTrue(TEXT("successful 188 hit keeps the GA alive for its queued 189 transition"),
		Ability->ShouldDeferMontageCompletionForPendingDance());
	Ability->Phase = EKinsectSlashPhase::Dash;
	TestTrue(TEXT("successful 187 hit also survives a montage-end callback before next-tick routing"),
		Ability->ShouldDeferMontageCompletionForPendingDance());

	Ability->Phase = EKinsectSlashPhase::DancePending;
	TestFalse(TEXT("once 189 routing starts, MissFall deferral no longer applies"),
		Ability->ShouldDeferMontageCompletionForPendingDance());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5KinsectSlashLateHitOpenBoundaryTest,
	"MHGZ.M5.KinsectSlash.LateHitOpenAcceptsDashToMissBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5KinsectSlashLateHitOpenBoundaryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestTrue(TEXT("Miss188 notify can arrive while the action phase still reports Dash"),
		UMHGZKinsectSlashAbility::IsLateHitOpenNotifyPhaseAllowed(
			EKinsectSlashPhase::Dash, TEXT("Miss188"), TEXT("Miss188")));
	TestTrue(TEXT("Miss188 notify remains valid after movement completion"),
		UMHGZKinsectSlashAbility::IsLateHitOpenNotifyPhaseAllowed(
			EKinsectSlashPhase::MissFall, TEXT("Miss188"), TEXT("Miss188")));
	TestFalse(TEXT("late-hit open is not accepted during Hover"),
		UMHGZKinsectSlashAbility::IsLateHitOpenNotifyPhaseAllowed(
			EKinsectSlashPhase::Hover, TEXT("Miss188"), TEXT("Miss188")));
	TestFalse(TEXT("Dash phase does not permit opening before the montage enters Miss188"),
		UMHGZKinsectSlashAbility::IsLateHitOpenNotifyPhaseAllowed(
			EKinsectSlashPhase::Dash, TEXT("Dash187"), TEXT("Miss188")));
	return true;
}

#endif
