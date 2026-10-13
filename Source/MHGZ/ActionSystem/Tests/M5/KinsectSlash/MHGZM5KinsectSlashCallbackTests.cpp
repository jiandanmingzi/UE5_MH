// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "ActionSystem/AbilityTask_MHGZWeaponMovement.h"
#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "ActionSystem/MHGZKinsectSlashAbility.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "AttributeSystem/Res_InsectGlaive.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "MHGZM5KinsectSlashCallbackTestTypes.h"
#include "MHGZM5KinsectSlashDamageTestTypes.h"
#include "TimerManager.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

namespace
{
FGameplayTag CallbackTestTag(const TCHAR* Name)
{
	return FGameplayTag::RequestGameplayTag(FName(Name));
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5KinsectSlashReentrantDanceCommitTest,
	"MHGZ.M5.KinsectSlash.ReentrantDanceCommitCannotReviveCancelledAction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5KinsectSlashReentrantDanceCommitTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	// This is the same compatible mesh/plain AnimInstance setup used by the
	// existing aerial montage tests. Assets are loaded, never modified.
	USkeletalMesh* CompatibleMesh = LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Characters/Demo/Meshes/Body/SKM_Demo_Body.SKM_Demo_Body"));
	UAnimMontage* PrimaryMontage = LoadObject<UAnimMontage>(nullptr,
		TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_CaoChongZhan.AM_IG_CaoChongZhan"));
	UAnimMontage* DanceMontage = LoadObject<UAnimMontage>(nullptr,
		TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_CaoChongZhan_Dance.AM_IG_CaoChongZhan_Dance"));
	if (!TestNotNull(TEXT("real montage coverage requires the compatible demo mesh"), CompatibleMesh)
		|| !TestNotNull(TEXT("real montage coverage requires the production 186/187/188 montage"), PrimaryMontage)
		|| !TestNotNull(TEXT("real montage coverage requires the production 189 montage"), DanceMontage))
	{
		return false;
	}

	const FGameplayTag CantAttack = CallbackTestTag(TEXT("Combat.State.Aerial.CantAttack"));
	const FGameplayTag CantDodge = CallbackTestTag(TEXT("Combat.State.Aerial.CantDodge"));
	const FGameplayTag Hitstun = CallbackTestTag(TEXT("Combat.State.Hitstun"));
	for (const bool bCancelFromResource : { true, false })
	{
		const FString Prefix = bCancelFromResource
			? TEXT("resource broadcast: ") : TEXT("budget removal: ");
		FMHGZM5KinsectSlashDamageTestHarness H;
		if (!TestTrue(*(Prefix + TEXT("equipped IG harness builds")), H.Build()))
		{
			H.Teardown();
			return false;
		}

		USkeletalMeshComponent* Mesh = H.Character->GetMesh();
		if (!TestNotNull(*(Prefix + TEXT("avatar has a skeletal mesh component")), Mesh))
		{
			H.Teardown();
			return false;
		}
		Mesh->SetSkeletalMesh(CompatibleMesh);
		Mesh->SetAnimInstanceClass(UAnimInstance::StaticClass());
		H.ASC->RefreshAbilityActorInfo();
		UAnimInstance* AnimInstance = Mesh->GetAnimInstance();
		if (!TestNotNull(*(Prefix + TEXT("plain AnimInstance initializes for real playback")), AnimInstance)
			|| !TestTrue(*(Prefix + TEXT("equipped resource ability activates")), H.ActivateAbility()))
		{
			H.Teardown();
			return false;
		}

		UMHGZKinsectSlashAbility* Ability = H.Ability;
		Ability->AttackMontage = PrimaryMontage;
		Ability->DefaultEntrySection = Ability->HoverSectionName;
		Ability->DanceMontage = DanceMontage;
		// Explicit qualification bypasses only the harness's playback override.
		// Production creates the real task and registers its exact instance ID.
		if (!TestTrue(*(Prefix + TEXT("production primary montage playback and registration succeed")),
			Ability->UMHGZKinsectSlashAbility::StartAttackMontage(
				*H.Character, PrimaryMontage, Ability->HoverSectionName)))
		{
			H.Teardown();
			return false;
		}
		const FWeaponActionToken Token = Ability->GetActionToken();
		const int32 PrimaryMontageID = Ability->SlashMontageInstanceID;
		if (!TestTrue(*(Prefix + TEXT("production exact-montage guard accepts the primary instance")),
			Ability->IsCurrentMontage(Token, PrimaryMontageID))
			|| !TestTrue(*(Prefix + TEXT("real primary montage accepts AimLock")),
				Ability->HandleMontageEvent(Token, PrimaryMontageID, EKinsectSlashMontageEvent::AimLock)))
		{
			H.Teardown();
			return false;
		}
		AnimInstance->Montage_JumpToSection(Ability->DashSectionName, PrimaryMontage);
		if (!TestTrue(*(Prefix + TEXT("real primary montage starts the production dash task")),
			Ability->HandleMontageEvent(Token, PrimaryMontageID, EKinsectSlashMontageEvent::DashBegin))
			|| !TestTrue(*(Prefix + TEXT("Host spends the dodge budget before the hit")),
				H.Host->MarkAerialDodgeUsed()))
		{
			H.Teardown();
			return false;
		}
		TestEqual(*(Prefix + TEXT("attack budget is spent before the resource callback")),
			H.ASC->GetTagCount(CantAttack), 1);
		TestEqual(*(Prefix + TEXT("dodge budget is spent before the resource callback")),
			H.ASC->GetTagCount(CantDodge), 1);

		UMHGZM5KinsectSlashDanceCancellationObserver* Observer =
			NewObject<UMHGZM5KinsectSlashDanceCancellationObserver>(H.Character);
		Observer->ASC = H.ASC;
		Observer->Ability = Ability;
		Observer->Host = H.Host;
		Observer->Mesh = Mesh;
		Observer->bCancelOnDanceStateChanged = bCancelFromResource;
		H.Resource->OnDanceStateChanged.AddDynamic(Observer,
			&UMHGZM5KinsectSlashDanceCancellationObserver::HandleDanceStateChanged);

		int32 BudgetRemovalCallbacks = 0;
		bool bBudgetCallbackCancelledSynchronously = false;
		FDelegateHandle BudgetRemovalHandle;
		if (!bCancelFromResource)
		{
			BudgetRemovalHandle = H.ASC->RegisterGameplayTagEvent(
				CantAttack, EGameplayTagEventType::NewOrRemoved).AddLambda(
				[&](FGameplayTag Tag, const int32 NewCount)
				{
					(void)Tag;
					if (NewCount == 0)
					{
						++BudgetRemovalCallbacks;
						H.ASC->SetLooseGameplayTagCount(Hitstun, 1);
						bBudgetCallbackCancelledSynchronously = !Ability->IsActive();
					}
				});
		}

		// Seed the successful-hit result at the queue boundary; damage resolution
		// is covered by the damage suite. The transition itself is unmodified.
		Ability->bSuccessfulDamage = true;
		Ability->bPendingDanceTransition = true;
		Ability->PendingDanceFromMiss = false;
		Ability->SuccessfulHitSourceSection = Ability->DashSectionName;
		Ability->SuccessfulHitSourceMontageInstanceID = PrimaryMontageID;
		Ability->SuccessfulHitSourcePosition = AnimInstance->Montage_GetPosition(PrimaryMontage);
		Ability->HandleQueuedDanceTransition(Token, PrimaryMontageID);

		H.Resource->OnDanceStateChanged.RemoveDynamic(Observer,
			&UMHGZM5KinsectSlashDanceCancellationObserver::HandleDanceStateChanged);
		if (BudgetRemovalHandle.IsValid())
		{
			H.ASC->RegisterGameplayTagEvent(CantAttack,
				EGameplayTagEventType::NewOrRemoved).Remove(BudgetRemovalHandle);
		}

		TestEqual(*(Prefix + TEXT("real AddDanceStack broadcasts exactly once at the injection point")),
			Observer->KinsectSlashBroadcastCount, 1);
		TestEqual(*(Prefix + TEXT("callback observes the committed first resource stack")),
			Observer->StacksAtDanceBroadcast, 1);
		TestTrue(*(Prefix + TEXT("callback observes the still-live GA before cancellation")),
			Observer->bObservedActiveAbility);
		TestTrue(*(Prefix + TEXT("callback observes the production Host-registered 189 montage")),
			Observer->bObservedExactRegisteredDanceMontage);
		TestTrue(*(Prefix + TEXT("189 has a new montage instance identity")),
			Observer->DanceMontageInstanceID != INDEX_NONE
				&& Observer->DanceMontageInstanceID != PrimaryMontageID);
		if (bCancelFromResource)
		{
			TestTrue(*(Prefix + TEXT("dynamic resource observer applies Hitstun and ends GA inline")),
				Observer->bAppliedHitstun && Observer->bCancelledSynchronously);
			TestEqual(*(Prefix + TEXT("cancelled resource commit cannot reset the attack budget")),
				H.ASC->GetTagCount(CantAttack), 1);
			TestEqual(*(Prefix + TEXT("cancelled resource commit cannot reset the dodge budget")),
				H.ASC->GetTagCount(CantDodge), 1);
		}
		else
		{
			TestEqual(*(Prefix + TEXT("budget reset reaches the synchronous tag callback once")),
				BudgetRemovalCallbacks, 1);
			TestTrue(*(Prefix + TEXT("budget callback ends the GA before reset returns")),
				bBudgetCallbackCancelledSynchronously);
		}

		FTimerManager& Timers = H.World->GetTimerManager();
		TestFalse(*(Prefix + TEXT("GA remains inactive after the commit stack unwinds")), Ability->IsActive());
		TestEqual(*(Prefix + TEXT("cancellation preserves the ended phase")),
			Ability->GetSlashPhase(), EKinsectSlashPhase::Ended);
		TestFalse(*(Prefix + TEXT("returning commit cannot mark the dance entry committed")),
			Ability->bDanceEntryCommitted);
		TestFalse(*(Prefix + TEXT("returning commit cannot rearm the preparation timer")),
			Ability->DancePreparationTimer.IsValid() || Timers.TimerExists(Ability->DancePreparationTimer));
		TestFalse(*(Prefix + TEXT("no queued transition timer remains after cancellation")),
			Ability->QueuedDanceTransitionTimer.IsValid() || Timers.TimerExists(Ability->QueuedDanceTransitionTimer));
		TestTrue(*(Prefix + TEXT("cancellation retires every slash, dance and movement task")),
			!Ability->SlashMontageTask && !Ability->DanceMontageTask
				&& !Ability->MovementTask && !Ability->HoverMovementTask && !Ability->ProbeTask);
		TestFalse(*(Prefix + TEXT("cancelled action owns no Host movement")), H.Host->IsActionMovementOwned());
		TestFalse(*(Prefix + TEXT("cancelled action is removed from Host registration")), H.Host->HasRegisteredAction());
		FWeaponActionToken ResolvedAction;
		TestFalse(*(Prefix + TEXT("cancellation unregisters the primary montage")),
			H.Host->ResolveMontage(Mesh, PrimaryMontageID, ResolvedAction));
		TestFalse(*(Prefix + TEXT("cancellation unregisters the exact 189 montage")),
			H.Host->ResolveMontage(Mesh, Observer->DanceMontageInstanceID, ResolvedAction));

		Ability->HandleDancePreparationFinished(Token, Observer->DanceMontageInstanceID);
		Ability->HandleQueuedDanceTransition(Token, PrimaryMontageID);
		TestFalse(*(Prefix + TEXT("late DanceBegin rejects the cancelled real montage identity")),
			Ability->HandleMontageEvent(Token, Observer->DanceMontageInstanceID, EKinsectSlashMontageEvent::DanceBegin));
		TestTrue(*(Prefix + TEXT("late callbacks cannot resurrect timers, tasks or commit flags")),
			!Ability->IsActive() && Ability->GetSlashPhase() == EKinsectSlashPhase::Ended
				&& !Ability->bDanceEntryCommitted && !Ability->MovementTask && !Ability->HoverMovementTask
				&& !Ability->DanceMontageTask && !Ability->DancePreparationTimer.IsValid()
				&& !Ability->QueuedDanceTransitionTimer.IsValid() && !H.Host->IsActionMovementOwned());
		H.Teardown();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5KinsectSlashHostBudgetReentrantResetTest,
	"MHGZ.M5.KinsectSlash.HostBudgetReentrantReset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5KinsectSlashHostBudgetReentrantResetTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FMHGZM5KinsectSlashDamageTestHarness H;
	if (!TestTrue(TEXT("equipped IG budget harness builds"), H.Build()))
	{
		H.Teardown();
		return false;
	}
	const FGameplayTag CantAttack = CallbackTestTag(TEXT("Combat.State.Aerial.CantAttack"));
	const FGameplayTag CantDodge = CallbackTestTag(TEXT("Combat.State.Aerial.CantDodge"));
	TestTrue(TEXT("Host initially spends the attack budget"), H.Host->MarkAerialAttackUsed());
	TestTrue(TEXT("Host initially spends the dodge budget"), H.Host->MarkAerialDodgeUsed());
	TestEqual(TEXT("only the initial Host attack owner is counted"), H.ASC->GetTagCount(CantAttack), 1);
	TestEqual(TEXT("only the initial Host dodge owner is counted"), H.ASC->GetTagCount(CantDodge), 1);

	int32 RemovalCallbacks = 0;
	bool bAttackSpentInCallback = false;
	bool bDodgeSpentInCallback = false;
	int32 AttackCountInCallback = 0;
	int32 DodgeCountInCallback = 0;
	const FDelegateHandle RemovalHandle = H.ASC->RegisterGameplayTagEvent(
		CantAttack, EGameplayTagEventType::NewOrRemoved).AddLambda(
		[&](FGameplayTag Tag, const int32 NewCount)
		{
			(void)Tag;
			if (NewCount == 0)
			{
				++RemovalCallbacks;
				bAttackSpentInCallback = H.Host->MarkAerialAttackUsed();
				bDodgeSpentInCallback = H.Host->MarkAerialDodgeUsed();
				AttackCountInCallback = H.ASC->GetTagCount(CantAttack);
				DodgeCountInCallback = H.ASC->GetTagCount(CantDodge);
			}
		});
	TestTrue(TEXT("first Dance reset succeeds while its removal listener spends both budgets"),
		H.Host->ResetAerialActionBudgetOnDance());
	H.ASC->RegisterGameplayTagEvent(CantAttack,
		EGameplayTagEventType::NewOrRemoved).Remove(RemovalHandle);

	TestEqual(TEXT("attack removal callback executes exactly once"), RemovalCallbacks, 1);
	TestTrue(TEXT("both budget spends report success inside the callback"),
		bAttackSpentInCallback && bDodgeSpentInCallback);
	TestEqual(TEXT("cleared attack slot permits a fresh counted token during release"), AttackCountInCallback, 1);
	TestEqual(TEXT("dodge slot is already cleared before the old dodge token is released"), DodgeCountInCallback, 2);
	TestEqual(TEXT("fresh attack tag survives the first reset"), H.ASC->GetTagCount(CantAttack), 1);
	TestEqual(TEXT("fresh dodge tag survives the first reset"), H.ASC->GetTagCount(CantDodge), 1);
	TestTrue(TEXT("fresh attack token remains in its Host pose slot"), H.Host->MarkAerialAttackUsed());
	TestTrue(TEXT("fresh dodge token remains in its Host pose slot"), H.Host->MarkAerialDodgeUsed());
	TestEqual(TEXT("repeat attack spend does not acquire an orphaned second owner"), H.ASC->GetTagCount(CantAttack), 1);
	TestEqual(TEXT("repeat dodge spend does not acquire an orphaned second owner"), H.ASC->GetTagCount(CantDodge), 1);

	TestTrue(TEXT("second Dance reset succeeds with the listener removed"), H.Host->ResetAerialActionBudgetOnDance());
	TestEqual(TEXT("second reset releases the reentrant attack token"), H.ASC->GetTagCount(CantAttack), 0);
	TestEqual(TEXT("second reset releases the reentrant dodge token"), H.ASC->GetTagCount(CantDodge), 0);
	TestEqual(TEXT("second reset leaves no orphaned attack ledger entries"),
		H.Host->ReleaseTagsForOwner(EWeaponTagOwnerKind::Pose, FGameplayAbilitySpecHandle(),
			0, TEXT("Pose.AerialCantAttack")), 0);
	TestEqual(TEXT("second reset leaves no orphaned dodge ledger entries"),
		H.Host->ReleaseTagsForOwner(EWeaponTagOwnerKind::Pose, FGameplayAbilitySpecHandle(),
			0, TEXT("Pose.AerialCantDodge")), 0);

	H.Teardown();
	return true;
}

#endif
