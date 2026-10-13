// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "ActionSystem/MHGZAirDodgeAbility.h"
#include "ActionSystem/AbilityTask_MHGZPlayMontageAndWait.h"
#include "ActionSystem/AbilityTask_MHGZWeaponMovement.h"
#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "ActionSystem/MHGZDamageGameplayEffect.h"
#include "ActionSystem/MHGZGameplayEffectContext.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "AttributeSystem/Res_InsectGlaive.h"
#include "ActionSystem/AbilityTask_MHGZKinsectSlashProbe.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "MHGZPlayerState.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"
#include "Movement/MHGZInstrumentedCharacterMovementComponent.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"
#include "MHGZM5KinsectSlashDamageTestTypes.h"

namespace
{
FGameplayTag DamageTestTag(const TCHAR* Name)
{
	return FGameplayTag::RequestGameplayTag(FName(Name));
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5KinsectSlashDamageResourceTest,
	"MHGZ.M5.KinsectSlash.DamageResourceSuccessGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5KinsectSlashDamageResourceTest::RunTest(const FString& Parameters)
{
	FMHGZM5KinsectSlashDamageTestHarness Harness;
	if (!TestTrue(TEXT("transient equipped IG damage harness builds"), Harness.Build()))
	{
		Harness.Teardown();
		return false;
	}
	FGameplayTagContainer ActivationFailureTags;
	const FGameplayAbilitySpec* ActivationSpec = Harness.ASC
		? Harness.ASC->FindAbilitySpecFromHandle(Harness.AbilityHandle) : nullptr;
	const bool bCanActivate = ActivationSpec && ActivationSpec->Ability
		&& ActivationSpec->Ability->CanActivateAbility(Harness.AbilityHandle,
			Harness.ASC->AbilityActorInfo.Get(), nullptr, nullptr, &ActivationFailureTags);
	AddInfo(FString::Printf(
		TEXT("CanActivate=%s Grounded=%s Falling=%s Actionable=%s Unsheathed=%s CantAttack=%s failures=%s"),
		bCanActivate ? TEXT("true") : TEXT("false"),
		Harness.Host->IsGrounded() ? TEXT("true") : TEXT("false"),
		Harness.ASC->HasMatchingGameplayTag(DamageTestTag(TEXT("Combat.State.Aerial.Falling")))
			? TEXT("true") : TEXT("false"),
		Harness.ASC->HasMatchingGameplayTag(DamageTestTag(TEXT("Combat.State.Aerial.Actionable")))
			? TEXT("true") : TEXT("false"),
		Harness.ASC->HasMatchingGameplayTag(DamageTestTag(TEXT("Combat.State.Unsheathed")))
			? TEXT("true") : TEXT("false"),
		Harness.ASC->HasMatchingGameplayTag(DamageTestTag(TEXT("Combat.State.Aerial.CantAttack")))
			? TEXT("true") : TEXT("false"), *ActivationFailureTags.ToString()));
	TestTrue(TEXT("production GA's live activation gates pass before TryActivateAbility"),
		bCanActivate);
	TestFalse(TEXT("the damage harness activates without an injected Actionable tag"),
		Harness.ASC->HasMatchingGameplayTag(DamageTestTag(TEXT("Combat.State.Aerial.Actionable"))));
	if (!TestTrue(TEXT("production-derived Kinsect Slash ability activates"),
		Harness.ActivateAbility()))
	{
		Harness.Teardown();
		return false;
	}

	TestTrue(TEXT("source is a real MHGZ character with the PlayerState ASC"),
		Harness.Character->GetPlayerState<AMHGZPlayerState>() == Harness.PlayerState
		&& Harness.Character->GetAbilitySystemComponent() == Harness.ASC);
	TestTrue(TEXT("Host owns the equipped IG resource"),
		Harness.Host->GetResourceProvider() == Harness.Resource
		&& Harness.Character->FindComponentByClass<URes_InsectGlaive>() == Harness.Resource);
	TestNotNull(TEXT("equipped resource spawned a real Kinsect actor"),
		Harness.Resource->GetKinsectActor());

	const FGameplayTag RedExtractTag =
		DamageTestTag(TEXT("WeaponResource.IG.Extract.Red"));
	const FGameplayTag CantAttackTag =
		DamageTestTag(TEXT("Combat.State.Aerial.CantAttack"));
	const FGameplayTag CantDodgeTag =
		DamageTestTag(TEXT("Combat.State.Aerial.CantDodge"));
	const FGameplayTag FallingTag =
		DamageTestTag(TEXT("Combat.State.Aerial.Falling"));
	TestTrue(TEXT("activation spends the Host-owned CantAttack budget"),
		Harness.ASC->HasMatchingGameplayTag(CantAttackTag));
	TestTrue(TEXT("activation also spends Host-owned CantDodge through the later miss-fall phase"),
		Harness.ASC->HasMatchingGameplayTag(CantDodgeTag));
	const UMHGZAirDodgeAbility* AirDodgeCDO = GetDefault<UMHGZAirDodgeAbility>();
	TestNotNull(TEXT("production Air Dodge CDO exists"), AirDodgeCDO);
	if (AirDodgeCDO)
	{
		TestFalse(TEXT("spent CantDodge blocks Air Dodge while the aerial attack's fall continues"),
			AirDodgeCDO->DoesAbilitySatisfyTagRequirements(*Harness.ASC));
	}
	TestFalse(TEXT("red extract starts absent"), Harness.Resource->HasExtract(RedExtractTag));

	FHitResult Hit(Harness.TargetActor, Harness.Hitzone,
		FVector(25.0f, 0.0f, 0.0f), FVector::UpVector);
	Hit.bBlockingHit = true;
	Hit.Location = Hit.ImpactPoint;
	Hit.ImpactNormal = FVector::UpVector;

	const FGameplayEffectSpecHandle KinsectSpec = Harness.Ability->MakeDamageSpec(Hit, 0);
	const FGameplayEffectSpecHandle FirstWeaponSpec = Harness.Ability->MakeDamageSpec(Hit, 1);
	const FGameplayEffectSpecHandle SecondWeaponSpec = Harness.Ability->MakeDamageSpec(Hit, 2);
	if (!TestTrue(TEXT("all three hit segments make valid GE specs"),
		KinsectSpec.IsValid() && FirstWeaponSpec.IsValid() && SecondWeaponSpec.IsValid()))
	{
		Harness.Teardown();
		return false;
	}
	const FMHGZGameplayEffectContext* KinsectContext =
		FMHGZGameplayEffectContext::ExtractEffectContext(KinsectSpec.Data->GetContext());
	const FMHGZGameplayEffectContext* FirstWeaponContext =
		FMHGZGameplayEffectContext::ExtractEffectContext(FirstWeaponSpec.Data->GetContext());
	const FMHGZGameplayEffectContext* SecondWeaponContext =
		FMHGZGameplayEffectContext::ExtractEffectContext(SecondWeaponSpec.Data->GetContext());
	TestTrue(TEXT("segment 0 is real kinsect damage"), KinsectContext
		&& KinsectContext->DamageSourceType == EMHGZDamageSourceType::Kinsect);
	TestTrue(TEXT("segments 1 and 2 are distinct weapon damage entries"),
		FirstWeaponContext && SecondWeaponContext
		&& FirstWeaponContext->DamageSourceType == EMHGZDamageSourceType::Weapon
		&& SecondWeaponContext->DamageSourceType == EMHGZDamageSourceType::Weapon);
	TestEqual(TEXT("segment 0 uses the kinsect's attack power"),
		KinsectSpec.Data->GetSetByCallerMagnitude(
			DamageTestTag(TEXT("Damage.AttackPower")), true, -1.0f),
		Harness.Resource->GetModifiedKinsectAttackPower());

	const float HealthBeforeWeaponHits = Harness.TargetAttributes->GetHealth();
	Harness.Ability->ApplyDamage(Hit, 1);
	const float HealthAfterFirstWeaponHit = Harness.TargetAttributes->GetHealth();
	Harness.Ability->ApplyDamage(Hit, 2);
	const float HealthAfterSecondWeaponHit = Harness.TargetAttributes->GetHealth();
	TestTrue(TEXT("each weapon segment damages the same target independently"),
		HealthAfterFirstWeaponHit < HealthBeforeWeaponHits
		&& HealthAfterSecondWeaponHit < HealthAfterFirstWeaponHit);
	TestFalse(TEXT("weapon contacts do not award kinsect extract"),
		Harness.Resource->HasExtract(RedExtractTag));
	TestFalse(TEXT("weapon contacts do not enter the kinsect-hit Dance branch"),
		Harness.Ability->bSuccessfulDamage || Harness.Ability->bPendingDanceTransition);

	int32 RedExtractApplyCount = 0;
	const FDelegateHandle RedExtractApplyHandle =
		Harness.ASC->OnActiveGameplayEffectAddedDelegateToSelf.AddLambda(
			[&RedExtractApplyCount](UAbilitySystemComponent*, const FGameplayEffectSpec& Spec,
				FActiveGameplayEffectHandle)
			{
				if (Spec.Def
					&& Spec.Def->GetClass() == UMHGZM5KinsectSlashDamageRedExtractGE::StaticClass())
				{
					++RedExtractApplyCount;
				}
			});

	Harness.Ability->Phase = EKinsectSlashPhase::Dash;
	Harness.Ability->AttackSegments[0].Damage.DamageEffectClass = nullptr;
	Harness.Ability->HandleProbeHitzoneContact(Harness.Ability->GetActionToken(), Hit);

	TestFalse(TEXT("missing damage GE does not latch successful damage"),
		Harness.Ability->bSuccessfulDamage);
	TestFalse(TEXT("missing damage GE does not queue a Dance transition"),
		Harness.Ability->bPendingDanceTransition);
	TestFalse(TEXT("missing damage GE leaves no queued transition timer"),
		Harness.Ability->QueuedDanceTransitionTimer.IsValid());
	TestFalse(TEXT("missing damage GE grants no red extract"),
		Harness.Resource->HasExtract(RedExtractTag));
	TestEqual(TEXT("missing damage GE causes no red extract GE application"),
		RedExtractApplyCount, 0);
	TestTrue(TEXT("failed damage resolution clears its in-progress guard"),
		!Harness.Ability->bDamageResolveInProgress);

	const float HealthBeforeDamage = Harness.TargetAttributes->GetHealth();
	Harness.Ability->AttackSegments[0].Damage.DamageEffectClass =
		UMHGZDamageGameplayEffect::StaticClass();
	Harness.Ability->HandleProbeHitzoneContact(Harness.Ability->GetActionToken(), Hit);
	const float HealthAfterFirstHit = Harness.TargetAttributes->GetHealth();

	TestTrue(TEXT("real damage GE changes target health"),
		HealthAfterFirstHit < HealthBeforeDamage);
	TestTrue(TEXT("real damage GE latches successful damage"),
		Harness.Ability->bSuccessfulDamage);
	TestTrue(TEXT("real damage GE latches the pending Dance transition"),
		Harness.Ability->bPendingDanceTransition);
	TestTrue(TEXT("successful contact queues the transition for a later tick"),
		Harness.Ability->QueuedDanceTransitionTimer.IsValid());
	TestTrue(TEXT("successful contact grants red extract"),
		Harness.Resource->HasExtract(RedExtractTag));
	TestEqual(TEXT("successful contact grants red extract exactly once"),
		RedExtractApplyCount, 1);
	TestTrue(TEXT("CantAttack remains spent before any Dance commit"),
		Harness.ASC->HasMatchingGameplayTag(CantAttackTag));

	Harness.Ability->HandleProbeHitzoneContact(Harness.Ability->GetActionToken(), Hit);
	TestTrue(TEXT("duplicate contact does not apply damage again"),
		FMath::IsNearlyEqual(Harness.TargetAttributes->GetHealth(), HealthAfterFirstHit));
	TestEqual(TEXT("duplicate contact does not grant red extract again"),
		RedExtractApplyCount, 1);
	TestTrue(TEXT("duplicate contact leaves the success latch set"),
		Harness.Ability->bSuccessfulDamage && Harness.Ability->bPendingDanceTransition);

	// Exercise the real dance-movement Landed callback with test-owned phase setup.
	// This does not invoke DanceBegin or mark the dance entry committed.
	const bool bDodgeBudgetAcquired = Harness.Host->MarkAerialDodgeUsed();
	const bool bDanceStackSeeded = Harness.Resource->AddDanceStack(EIGDanceSource::KinsectSlash);
	TestTrue(TEXT("Host owns CantDodge before landing"), bDodgeBudgetAcquired
		&& Harness.ASC->HasMatchingGameplayTag(CantDodgeTag));
	TestTrue(TEXT("resource has a Dance stack before landing"), bDanceStackSeeded
		&& Harness.Resource->GetDanceStacks() > 0);
	TestTrue(TEXT("Host Falling state is present before landing"),
		Harness.ASC->HasMatchingGameplayTag(FallingTag));

	Harness.Ability->Phase = EKinsectSlashPhase::DanceFromDash;
	Harness.Ability->MovementActionToken = Harness.Ability->GetActionToken();
	Harness.Ability->bMovementTaskIsDance = true;
	Harness.Ability->bMontageFinished = false;
	Harness.Ability->bDanceEntryCommitted = false;
	FWeaponMovementResult LandedResult;
	LandedResult.EndReason = EWeaponMovementEndReason::Landed;
	Harness.Ability->HandleMovementFinished(LandedResult);

	TestTrue(TEXT("Dance movement landing finalizes the Host grounded state"),
		Harness.Host->IsGrounded());
	TestFalse(TEXT("Dance movement landing releases Host Falling"),
		Harness.ASC->HasMatchingGameplayTag(FallingTag));
	TestFalse(TEXT("Dance movement landing releases Host CantAttack"),
		Harness.ASC->HasMatchingGameplayTag(CantAttackTag));
	TestFalse(TEXT("Dance movement landing releases Host CantDodge"),
		Harness.ASC->HasMatchingGameplayTag(CantDodgeTag));
	TestEqual(TEXT("Dance movement landing clears resource Dance stacks"),
		Harness.Resource->GetDanceStacks(), 0);
	TestTrue(TEXT("ability remains active until montage completion"),
		Harness.Ability->IsActive() && !Harness.Ability->bMontageFinished);
	TestFalse(TEXT("landing branch did not commit a Dance entry"),
		Harness.Ability->bDanceEntryCommitted);

	Harness.ASC->OnActiveGameplayEffectAddedDelegateToSelf.Remove(RedExtractApplyHandle);
	Harness.Teardown();

	// Reuse the real-montage setup from the callback suite. Drive the production
	// movement and montage tasks to their natural endpoints without changing physics.
	USkeletalMesh* CompatibleMesh = LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Characters/Demo/Meshes/Body/SKM_Demo_Body.SKM_Demo_Body"));
	UAnimMontage* PrimaryMontage = LoadObject<UAnimMontage>(nullptr,
		TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_CaoChongZhan.AM_IG_CaoChongZhan"));
	UAnimMontage* DanceMontage = LoadObject<UAnimMontage>(nullptr,
		TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_CaoChongZhan_Dance.AM_IG_CaoChongZhan_Dance"));
	const UInsectGlaiveCombatConfig* ProductionConfig = LoadObject<UInsectGlaiveCombatConfig>(nullptr,
		TEXT("/Game/Weapons/InsectGlaive/Data/DA_IG_Combat.DA_IG_Combat"));
	UAnimMontage* FallMontage = ProductionConfig ? ProductionConfig->GetAerialDodgeFallMontage() : nullptr;
	if (!TestNotNull(TEXT("natural Dance reactivation requires the compatible demo mesh"), CompatibleMesh)
		|| !TestNotNull(TEXT("natural Dance reactivation requires the primary montage"), PrimaryMontage)
		|| !TestNotNull(TEXT("natural Dance reactivation requires the 189 montage"), DanceMontage)
		|| !TestNotNull(TEXT("natural Dance reactivation requires the production 157 fall montage"), FallMontage))
	{
		return false;
	}

	const FGameplayTag ReactivationInputs[] = {
		DamageTestTag(TEXT("Input.Weapon.B")),
		DamageTestTag(TEXT("Input.Weapon.RTB"))
	};
	for (const FGameplayTag& ReactivationInput : ReactivationInputs)
	{
		AddInfo(FString::Printf(TEXT("natural Dance reactivation input=%s"), *ReactivationInput.ToString()));
		FMHGZM5KinsectSlashDamageTestHarness NaturalHarness;
		if (!TestTrue(TEXT("natural Dance reactivation harness builds"), NaturalHarness.Build()))
		{
			NaturalHarness.Teardown();
			return false;
		}
		NaturalHarness.CombatConfig->WhiteAerialFallMontage = FallMontage;
		USkeletalMeshComponent* Mesh = NaturalHarness.Character->GetMesh();
		Mesh->SetSkeletalMesh(CompatibleMesh);
		Mesh->SetAnimInstanceClass(UAnimInstance::StaticClass());
		NaturalHarness.ASC->RefreshAbilityActorInfo();
		UAnimInstance* AnimInstance = Mesh->GetAnimInstance();
		if (!TestNotNull(TEXT("natural Dance reactivation initializes the real montage AnimInstance"), AnimInstance)
			|| !TestTrue(TEXT("natural Dance action activates without Actionable"), NaturalHarness.ActivateAbility()))
		{
			NaturalHarness.Teardown();
			return false;
		}
		UMHGZKinsectSlashAbility* NaturalAbility = NaturalHarness.Ability;
		NaturalAbility->AttackMontage = PrimaryMontage;
		NaturalAbility->DefaultEntrySection = NaturalAbility->HoverSectionName;
		NaturalAbility->DanceMontage = DanceMontage;
		if (!TestTrue(TEXT("production primary montage playback registers the exact instance"),
			NaturalAbility->UMHGZKinsectSlashAbility::StartAttackMontage(
				*NaturalHarness.Character, PrimaryMontage, NaturalAbility->HoverSectionName)))
		{
			NaturalHarness.Teardown();
			return false;
		}
		const FWeaponActionToken NaturalToken = NaturalAbility->GetActionToken();
		const int32 PrimaryMontageID = NaturalAbility->SlashMontageInstanceID;
		if (!TestTrue(TEXT("natural Dance setup locks the production dash direction"),
			NaturalAbility->HandleMontageEvent(NaturalToken, PrimaryMontageID, EKinsectSlashMontageEvent::AimLock)))
		{
			NaturalHarness.Teardown();
			return false;
		}
		AnimInstance->Montage_JumpToSection(NaturalAbility->DashSectionName, PrimaryMontage);
		if (!TestTrue(TEXT("natural Dance setup starts the production dash task"),
			NaturalAbility->HandleMontageEvent(NaturalToken, PrimaryMontageID, EKinsectSlashMontageEvent::DashBegin)))
		{
			NaturalHarness.Teardown();
			return false;
		}
		FHitResult NaturalHit(NaturalHarness.TargetActor, NaturalHarness.Hitzone,
			FVector(25.0f, 0.0f, 0.0f), FVector::UpVector);
		NaturalHit.bBlockingHit = true;
		NaturalAbility->HandleProbeHitzoneContact(NaturalToken, NaturalHit);
		NaturalAbility->HandleQueuedDanceTransition(NaturalToken, PrimaryMontageID);
		if (!TestTrue(TEXT("successful damage commits the real Dance entry and resets its budget"),
			NaturalAbility->IsActive() && NaturalAbility->bDanceEntryCommitted
				&& !NaturalHarness.ASC->HasMatchingGameplayTag(CantAttackTag)
				&& !NaturalHarness.ASC->HasMatchingGameplayTag(CantDodgeTag)))
		{
			NaturalHarness.Teardown();
			return false;
		}
		const int32 DanceMontageID = NaturalAbility->SlashMontageInstanceID;
		NaturalAbility->HandleDancePreparationFinished(NaturalToken, DanceMontageID);
		UAbilityTask_MHGZWeaponMovement* DanceMovement = NaturalAbility->MovementTask;
		if (!TestNotNull(TEXT("natural Dance owns the production movement task"), DanceMovement)
			|| !TestTrue(TEXT("the real Dance handoff grants its action-owned legacy window"),
				NaturalAbility->NotifyAerialHandoff(NaturalToken, DanceMontageID)))
		{
			NaturalHarness.Teardown();
			return false;
		}
		const FGameplayTag ActionableTag = DamageTestTag(TEXT("Combat.State.Aerial.Actionable"));
		TestTrue(TEXT("Actionable exists only during the active Dance handoff window"),
			NaturalHarness.ASC->HasMatchingGameplayTag(ActionableTag));
		UMHGZInstrumentedCharacterMovementComponent* CMC =
			Cast<UMHGZInstrumentedCharacterMovementComponent>(NaturalHarness.Character->GetCharacterMovement());
		if (!TestNotNull(TEXT("natural Dance uses the instrumented movement component's public driver"), CMC))
		{
			NaturalHarness.Teardown();
			return false;
		}
		constexpr float Step = 1.0f / 60.0f;
		const int32 MaxFlightSteps = FMath::CeilToInt(NaturalAbility->DanceFromDashTuning.FlightDuration / Step) + 2;
		for (int32 Frame = 0; Frame < MaxFlightSteps && !NaturalAbility->bDanceMovementFinished
			&& NaturalAbility->IsActive(); ++Frame)
		{
			CMC->PerformMovement(Step);
			DanceMovement->TickTask(Step);
		}
		if (!TestTrue(TEXT("natural Dance flight completes without ending the unfinished montage"),
			NaturalAbility->IsActive() && NaturalAbility->bDanceMovementFinished)
			|| !TestNotNull(TEXT("natural Dance retains its real montage completion task"),
				NaturalAbility->DanceMontageTask.Get()))
		{
			NaturalHarness.Teardown();
			return false;
		}
		AnimInstance->Montage_SetPosition(DanceMontage, DanceMontage->GetPlayLength());
		NaturalAbility->DanceMontageTask->DriveTickForTest(Step);
		TestFalse(TEXT("natural 189 completion ends the Slash action"), NaturalAbility->IsActive());
		TestFalse(TEXT("natural completion removes the registered action"), NaturalHarness.Host->HasRegisteredAction());
		TestTrue(TEXT("Host owns the airborne 157 fall after natural Dance completion"),
			!NaturalHarness.Host->IsGrounded() && CMC->MovementMode == MOVE_Falling
				&& NaturalHarness.Host->IsAerialFalling() && AnimInstance->Montage_IsActive(FallMontage));
		TestTrue(TEXT("Host-owned Falling survives natural Slash completion"),
			NaturalHarness.ASC->HasMatchingGameplayTag(FallingTag));
		TestFalse(TEXT("natural completion releases the action-owned Actionable window"),
			NaturalHarness.ASC->HasMatchingGameplayTag(ActionableTag));
		const FGameplayAbilitySpec* ReactivationSpec =
			NaturalHarness.ASC->FindAbilitySpecFromHandle(NaturalHarness.AbilityHandle);
		TestTrue(TEXT("Slash can activate from Host-owned fall without Actionable or an artificial budget reset"),
			ReactivationSpec && ReactivationSpec->Ability
				&& ReactivationSpec->Ability->CanActivateAbility(NaturalHarness.AbilityHandle,
					NaturalHarness.ASC->AbilityActorInfo.Get(), nullptr, nullptr, nullptr));
		if (!TestTrue(FString::Printf(
			TEXT("natural Host-owned fall permits actual %s reactivation without Actionable"),
			*ReactivationInput.ToString()), NaturalHarness.ActivateAbility(ReactivationInput)))
		{
			NaturalHarness.Teardown();
			return false;
		}
		const FWeaponActionToken ReactivatedToken = NaturalHarness.Ability->GetActionToken();
		TestTrue(TEXT("the second Slash commits a fresh action in the same weapon runtime"),
			ReactivatedToken.RuntimeToken == NaturalToken.RuntimeToken
				&& ReactivatedToken.ActivationSequenceID != NaturalToken.ActivationSequenceID
				&& NaturalHarness.Host->HasRegisteredAction());
		const EKinsectSlashAimSource ExpectedAimSource =
			ReactivationInput.MatchesTagExact(ReactivationInputs[0])
				? EKinsectSlashAimSource::WeaponB : EKinsectSlashAimSource::KinsectAim;
		TestEqual(TEXT("the second Slash preserves the requested production input branch"),
			NaturalHarness.Ability->GetAimSource(), ExpectedAimSource);
		TestFalse(TEXT("actual reactivation still has no artificial Actionable tag"),
			NaturalHarness.ASC->HasMatchingGameplayTag(ActionableTag));
		TestTrue(TEXT("actual reactivation spends the next Host-owned attack budget"),
			NaturalHarness.ASC->HasMatchingGameplayTag(CantAttackTag));
		TestTrue(TEXT("actual reactivation also spends the next Host-owned dodge budget"),
			NaturalHarness.ASC->HasMatchingGameplayTag(CantDodgeTag));
		NaturalHarness.Teardown();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5KinsectSlashProbeSweepTest,
	"MHGZ.M5.KinsectSlash.ProbeContinuesUntilSuccessfulHitzoneDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5KinsectSlashProbeSweepTest::RunTest(const FString& Parameters)
{
	FMHGZM5KinsectSlashDamageTestHarness Harness;
	if (!TestTrue(TEXT("transient equipped IG probe harness builds"), Harness.Build())
		|| !Harness.ActivateAbility())
	{
		Harness.Teardown();
		return false;
	}

	const FGameplayTag HeadTag = DamageTestTag(TEXT("Hitzone.Head"));
	const FGameplayTag LegTag = DamageTestTag(TEXT("Hitzone.LeftLeg"));
	const FGameplayTag RedExtractTag =
		DamageTestTag(TEXT("WeaponResource.IG.Extract.Red"));
	const FGameplayTag WhiteExtractTag =
		DamageTestTag(TEXT("WeaponResource.IG.Extract.White"));
	const FGameplayTag OrangeExtractTag =
		DamageTestTag(TEXT("WeaponResource.IG.Extract.Orange"));
	const FVector Start = Harness.Character->GetCapsuleComponent()->GetComponentLocation();
	const FVector Forward = Harness.Character->GetActorForwardVector();
	Harness.Ability->AttackSegments[0].Collision.HitzoneQueryTag = HeadTag;
	Harness.Ability->AttackSegments[0].Damage.DamageEffectClass =
		UMHGZDamageGameplayEffect::StaticClass();
	Harness.Ability->Phase = EKinsectSlashPhase::Dash;

	// First candidate has the wrong part tag. The second has the right tag but
	// no target ASC, so its GE cannot succeed. The final candidate is valid.
	AActor* WrongTagActor = Harness.World->SpawnActor<AActor>();
	AActor* FailedGEActor = Harness.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("wrong-tag candidate actor spawns"), WrongTagActor)
		|| !TestNotNull(TEXT("GE-failure candidate actor spawns"), FailedGEActor))
	{
		Harness.Teardown();
		return false;
	}

	auto AddHitzone = [this](AActor* Actor, const FName ComponentName,
		const FGameplayTag& HitzoneTag, const FGameplayTag& ExtractTag)
	{
		UMHGZMonsterHitzoneComponent* Zone = NewObject<UMHGZMonsterHitzoneComponent>(
			Actor, ComponentName, RF_Transient);
		if (!Zone)
		{
			return static_cast<UMHGZMonsterHitzoneComponent*>(nullptr);
		}
		Actor->AddInstanceComponent(Zone);
		if (Actor->GetRootComponent())
		{
			Zone->SetupAttachment(Actor->GetRootComponent());
		}
		else
		{
			Actor->SetRootComponent(Zone);
		}
		Zone->HitzoneTag = HitzoneTag;
		Zone->ExtractColorTag = ExtractTag;
		Zone->RegisterComponent();
		Zone->SetSphereRadius(12.0f);
		return Zone;
	};

	UMHGZMonsterHitzoneComponent* WrongTagZone = AddHitzone(
		WrongTagActor, TEXT("WrongTagZone"), LegTag, WhiteExtractTag);
	UMHGZMonsterHitzoneComponent* FailedGEZone = AddHitzone(
		FailedGEActor, TEXT("FailedGEZone"), HeadTag, OrangeExtractTag);
	if (!TestNotNull(TEXT("wrong-tag Hitzone registers"), WrongTagZone)
		|| !TestNotNull(TEXT("failed-GE Hitzone registers"), FailedGEZone))
	{
		Harness.Teardown();
		return false;
	}

	WrongTagActor->SetActorLocation(Start + Forward * 45.0f);
	FailedGEActor->SetActorLocation(Start + Forward * 100.0f);
	Harness.TargetActor->SetActorLocation(Start + Forward * 165.0f);
	Harness.Hitzone->HitzoneTag = HeadTag;
	Harness.Hitzone->ExtractColorTag = RedExtractTag;

	AActor* WallActor = Harness.World->SpawnActor<AActor>();
	UBoxComponent* Wall = WallActor
		? NewObject<UBoxComponent>(WallActor, TEXT("ProbeOcclusionWall"), RF_Transient)
		: nullptr;
	if (!TestNotNull(TEXT("world blocker actor spawns"), WallActor)
		|| !TestNotNull(TEXT("world blocker component is created"), Wall))
	{
		Harness.Teardown();
		return false;
	}
	WallActor->AddInstanceComponent(Wall);
	WallActor->SetRootComponent(Wall);
	Wall->SetBoxExtent(FVector(5.0f, 30.0f, 30.0f));
	Wall->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Wall->SetCollisionObjectType(ECC_WorldStatic);
	Wall->SetCollisionResponseToAllChannels(ECR_Block);
	Wall->RegisterComponent();
	WallActor->SetActorLocation(Start + Forward * 65.0f);

	UAbilityTask_MHGZKinsectSlashProbe* BlockProbe =
		UAbilityTask_MHGZKinsectSlashProbe::StartProbe(Harness.Ability,
			Harness.Resource, Harness.Ability->GetActionToken(), Forward,
			0.0f, 5.0f, 80.0f, HeadTag, 0.1f, false);
	if (!TestNotNull(TEXT("wall occlusion probe task is created"), BlockProbe))
	{
		Harness.Teardown();
		return false;
	}
	BlockProbe->ReadyForActivation();
	BlockProbe->Character = Harness.Character;
	BlockProbe->LastProbeLocation = Start;
	BlockProbe->bStarted = true;
	BlockProbe->bSweepEnabled = true;
	Harness.Ability->ProbeTask = BlockProbe;
	const float HealthBeforeBlockedSweep = Harness.TargetAttributes->GetHealth();
	const FVector BlockedDestination = Start + Forward * 220.0f;
	FVector WallResolvedLocation = BlockedDestination;
	TestTrue(TEXT("wall occlusion terminates the sweep"),
		BlockProbe->SweepProbePath(BlockedDestination, WallResolvedLocation));
	TestTrue(TEXT("visual probe endpoint is clamped before the wall"),
		FVector::DotProduct(WallResolvedLocation - Start, Forward) < 62.0f);
	TestTrue(TEXT("wall occlusion prevents damage to the Hitzone behind it"),
		FMath::IsNearlyEqual(Harness.TargetAttributes->GetHealth(), HealthBeforeBlockedSweep));
	Harness.Ability->ProbeTask = nullptr;
	BlockProbe->EndTask();
	Harness.World->DestroyActor(WallActor);

	UAbilityTask_MHGZKinsectSlashProbe* Probe =
		UAbilityTask_MHGZKinsectSlashProbe::StartProbe(Harness.Ability,
			Harness.Resource, Harness.Ability->GetActionToken(), Forward,
			0.0f, 5.0f, 80.0f, HeadTag, 0.1f, false);
	if (!TestNotNull(TEXT("probe task object is created"), Probe))
	{
		Harness.Teardown();
		return false;
	}
	Probe->ReadyForActivation();
	Probe->Character = Harness.Character;
	Probe->LastProbeLocation = Start;
	Probe->bStarted = true;
	Probe->bSweepEnabled = true;
	Harness.Ability->ProbeTask = Probe;
	Probe->OnHitzoneContact.AddDynamic(Harness.Ability,
		&UMHGZKinsectSlashAbility::HandleProbeHitzoneContact);
	TestTrue(TEXT("probe sees the transient harness world"), Probe->GetWorld() == Harness.World);
	TestTrue(TEXT("probe owner token remains current"), Probe->IsCurrentOwnerContext());
	TArray<FHitResult> DirectHitzoneHits;
	FCollisionObjectQueryParams DirectHitzoneQuery;
	DirectHitzoneQuery.AddObjectTypesToQuery(ECC_GameTraceChannel3);
	FCollisionQueryParams DirectQueryParams(SCENE_QUERY_STAT(M5ProbeSweepTest), false,
		Harness.Character);
	Harness.World->SweepMultiByObjectType(DirectHitzoneHits, Start,
		Start + Forward * 220.0f, FQuat::Identity, DirectHitzoneQuery,
		FCollisionShape::MakeSphere(5.0f), DirectQueryParams);
	AddInfo(FString::Printf(TEXT("Direct Hitzone object sweep found %d components"),
		DirectHitzoneHits.Num()));
	TestTrue(TEXT("direct dedicated Hitzone object query finds the candidates"),
		DirectHitzoneHits.Num() >= 3);

	const float HealthBefore = Harness.TargetAttributes->GetHealth();
	const FVector Destination = Start + Forward * 220.0f;
	FVector ResolvedLocation = Destination;
	const bool bSweepTerminated = Probe->SweepProbePath(
		Destination, ResolvedLocation);

	TestTrue(TEXT("sweep terminates only after a successful contact"), bSweepTerminated);
	TestTrue(TEXT("the later legal Hitzone contact is acknowledged"), Probe->bContactAccepted);
	TestTrue(TEXT("failed earlier contacts do not prevent a later GE success"),
		Harness.TargetAttributes->GetHealth() < HealthBefore);
	TestTrue(TEXT("extract corresponds to the successful later Hitzone"),
		Harness.Resource->HasExtract(RedExtractTag));
	TestFalse(TEXT("the earlier wrong-tag Hitzone did not grant its extract"),
		Harness.Resource->HasExtract(WhiteExtractTag));
	TestFalse(TEXT("the earlier GE-failure Hitzone did not grant its extract"),
		Harness.Resource->HasExtract(OrangeExtractTag));

	UAbilityTask_MHGZKinsectSlashProbe* QueuedLateWindowProbe =
		UAbilityTask_MHGZKinsectSlashProbe::StartProbe(Harness.Ability,
			Harness.Resource, Harness.Ability->GetActionToken(), Forward,
			0.0f, 5.0f, 80.0f, HeadTag, 0.1f, false);
	if (!TestNotNull(TEXT("queued late-window probe task is created"), QueuedLateWindowProbe))
	{
		Harness.Ability->ProbeTask = nullptr;
		Probe->OnHitzoneContact.Clear();
		Harness.Teardown();
		return false;
	}
	QueuedLateWindowProbe->ReadyForActivation();
	QueuedLateWindowProbe->Character = Harness.Character;
	QueuedLateWindowProbe->LastProbeLocation = Start;
	QueuedLateWindowProbe->bStarted = true;
	QueuedLateWindowProbe->bDashActive = true;
	QueuedLateWindowProbe->bSweepEnabled = true;
	Harness.Ability->ProbeTask = QueuedLateWindowProbe;
	TestTrue(TEXT("188 notify queues its late-hit window before dash completion"),
		QueuedLateWindowProbe->OpenLateHitWindow(Harness.Ability->GetActionToken()));
	TestFalse(TEXT("queued window does not enter late-hit mode while dash is still active"),
		QueuedLateWindowProbe->bLateHitModeActive);
	TestTrue(TEXT("dash completion accepts the queued window request"),
		QueuedLateWindowProbe->NotifyDashMovementFinished(
			Harness.Ability->GetActionToken(), EWeaponMovementEndReason::Completed,
			FVector(0.0f, 0.0f, 100.0f)));
	TestTrue(TEXT("queued request enters late-hit mode after dash completion"),
		QueuedLateWindowProbe->bLateHitModeActive
			&& QueuedLateWindowProbe->bSweepEnabled);
	QueuedLateWindowProbe->EndTask();

	Harness.Ability->ProbeTask = nullptr;
	Probe->OnHitzoneContact.Clear();
	Harness.Teardown();

	// The movement source may finish during the CMC tick, before the probe's
	// own tick. Its final travelled segment must still hit the target Hitzone.
	{
		FMHGZM5KinsectSlashDamageTestHarness FinalSweepHarness;
		if (!TestTrue(TEXT("final-segment harness builds"), FinalSweepHarness.Build())
			|| !TestTrue(TEXT("final-segment ability activates"),
				FinalSweepHarness.ActivateAbility()))
		{
			FinalSweepHarness.Teardown();
			return false;
		}
		const FVector FinalStart = FinalSweepHarness.Character
			->GetCapsuleComponent()->GetComponentLocation();
		const FVector FinalForward = FinalSweepHarness.Character->GetActorForwardVector();
		FinalSweepHarness.Ability->Phase = EKinsectSlashPhase::Dash;
		FinalSweepHarness.TargetActor->SetActorLocation(
			FinalStart + FinalForward * 165.0f);
		UAbilityTask_MHGZKinsectSlashProbe* FinalProbe =
			UAbilityTask_MHGZKinsectSlashProbe::StartProbe(
				FinalSweepHarness.Ability, FinalSweepHarness.Resource,
				FinalSweepHarness.Ability->GetActionToken(), FinalForward,
				60.0f, 5.0f, 20.0f, HeadTag, 0.1f, false);
		if (!TestNotNull(TEXT("final-segment probe exists"), FinalProbe))
		{
			FinalSweepHarness.Teardown();
			return false;
		}
		FinalProbe->ReadyForActivation();
		FinalProbe->Character = FinalSweepHarness.Character;
		FinalProbe->LastProbeLocation = FinalStart + FinalForward * 60.0f;
		FinalProbe->bStarted = true;
		FinalProbe->bDashActive = true;
		FinalProbe->bSweepEnabled = true;
		FinalSweepHarness.Ability->ProbeTask = FinalProbe;
		FinalProbe->OnHitzoneContact.AddDynamic(FinalSweepHarness.Ability,
			&UMHGZKinsectSlashAbility::HandleProbeHitzoneContact);
		FinalSweepHarness.Character->SetActorLocation(
			FinalStart + FinalForward * 115.0f, false, nullptr,
			ETeleportType::TeleportPhysics);
		const float FinalHealthBefore =
			FinalSweepHarness.TargetAttributes->GetHealth();
		TestTrue(TEXT("natural completion sweeps the final travel segment"),
			FinalProbe->NotifyDashMovementFinished(
				FinalSweepHarness.Ability->GetActionToken(),
				EWeaponMovementEndReason::Completed, FVector(100.0f, 0.0f, 0.0f)));
		TestTrue(TEXT("final travel segment acknowledges Hitzone damage"),
			FinalProbe->bContactAccepted);
		TestTrue(TEXT("final travel segment actually applies the damage GE"),
			FinalSweepHarness.TargetAttributes->GetHealth() < FinalHealthBefore);
		FinalProbe->OnHitzoneContact.Clear();
		FinalSweepHarness.Ability->ProbeTask = nullptr;
		FinalSweepHarness.Teardown();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5KinsectSlashLifecycleCancellationTest,
	"MHGZ.M5.KinsectSlash.ActiveActionCancelsOnDisqualifyingStateOrRuntimeInvalidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5KinsectSlashLifecycleCancellationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FMHGZM5KinsectSlashDamageTestHarness Harness;
	if (!TestTrue(TEXT("transient equipped IG lifecycle harness builds"), Harness.Build())
		|| !Harness.ActivateAbility())
	{
		Harness.Teardown();
		return false;
	}

	auto EndWithTag = [this, &Harness](const TCHAR* TagName, const TCHAR* Label)
	{
		if (!Harness.Ability || !Harness.Ability->IsActive())
		{
			AddError(FString::Printf(TEXT("%s requires an active production ability"), Label));
			return false;
		}
		const FGameplayTag StateTag = DamageTestTag(TagName);
		Harness.ASC->SetLooseGameplayTagCount(StateTag, 1);
		const bool bEnded = !Harness.Ability->IsActive();
		TestTrue(FString::Printf(TEXT("active GA ends when %s is added"), Label), bEnded);
		Harness.ASC->SetLooseGameplayTagCount(StateTag, 0);
		return bEnded;
	};
	auto Reactivate = [this, &Harness](const TCHAR* Label)
	{
		Harness.Host->ResetAerialActionBudgetOnDance();
		TestFalse(FString::Printf(TEXT("%s reactivation has no injected Actionable tag"), Label),
			Harness.ASC->HasMatchingGameplayTag(DamageTestTag(TEXT("Combat.State.Aerial.Actionable"))));
		const bool bActivated = Harness.ActivateAbility();
		TestTrue(FString::Printf(TEXT("GA can be activated for %s coverage"), Label), bActivated);
		return bActivated;
	};

	if (!EndWithTag(TEXT("Combat.State.Hitstun"), TEXT("Hitstun"))
		|| !Reactivate(TEXT("Knockdown"))
		|| !EndWithTag(TEXT("Combat.State.Knockdown"), TEXT("Knockdown"))
		|| !Reactivate(TEXT("Dead"))
		|| !EndWithTag(TEXT("Combat.State.Dead"), TEXT("Dead"))
		|| !Reactivate(TEXT("runtime invalidation delegate")))
	{
		Harness.Teardown();
		return false;
	}

	Harness.Host->OnWeaponRuntimeInvalidated.Broadcast(Harness.Host->GetCurrentToken());
	TestFalse(TEXT("matching Host runtime invalidation delegate ends the active slash GA"),
		Harness.Ability->IsActive());

	if (!Reactivate(TEXT("weapon-runtime teardown")))
	{
		Harness.Teardown();
		return false;
	}
	Harness.Host->ShutdownRuntime(EWeaponRuntimeEndReason::WeaponChanged);
	TestFalse(TEXT("weapon teardown removes and ends the granted slash GA"),
		Harness.Ability->IsActive());
	Harness.Teardown();
	return true;
}

#endif
