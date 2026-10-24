// Copyright MHGZ Project. All Rights Reserved.

#include "ActionSystem/MHGZDescendingThrustAbility.h"

#include "AbilitySystemComponent.h"
#include "ActionSystem/AbilityTask_MHGZPlayMontageAndWait.h"
#include "ActionSystem/AbilityTask_MHGZWeaponMovement.h"
#include "ActionSystem/AnimNotify_DescendingThrustEvent.h"
#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "ActionSystem/MHGZComboCoordinatorAbility.h"
#include "InsectGlaive/Kinsect/Kinsect.h"
#include "ActionSystem/MHGZGameplayEffectContext.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimTypes.h"
#include "AttributeSystem/Res_InsectGlaive.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InsectGlaive/InsectGlaiveCombatConfig.h"
#include "MHGZ.h"
#include "MHGZCharacter.h"
#include "Movement/MHGZInstrumentedCharacterMovementComponent.h"
#include "TimerManager.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

namespace
{
const FGameplayTag& WeaponRTTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Input.Weapon.RT"));
	return Tag;
}

const FGameplayTag& PhysicalRTTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Input.Modifier.RT"));
	return Tag;
}

const FGameplayTag& AerialFallingTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Aerial.Falling"));
	return Tag;
}

bool HasEarlyDescendingThrustPermission(const UAbilitySystemComponent* ASC)
{
	const UMHGZAbilitySystemComponent* MHGZASC =
		Cast<UMHGZAbilitySystemComponent>(ASC);
	const UGA_WeaponComboCoordinator* Coordinator = MHGZASC
		? MHGZASC->GetActiveComboCoordinator() : nullptr;
	return Coordinator && Coordinator->IsEarlyDescendingThrustWindowOpen();
}

const FGameplayTag& UnsheathedTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Unsheathed"));
	return Tag;
}

const FGameplayTag& GroundedTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Grounded"));
	return Tag;
}

const FGameplayTag& AerialCantAttackTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Aerial.CantAttack"));
	return Tag;
}

const FGameplayTag& DeadTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Dead"));
	return Tag;
}

const FGameplayTag& HitstunTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Hitstun"));
	return Tag;
}

const FGameplayTag& KnockdownTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Knockdown"));
	return Tag;
}

const FGameplayTag& AirDodgeSourceTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Aerial.Source.IG_AirDodge"));
	return Tag;
}

const FGameplayTag& BackVaultSourceTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Aerial.Source.IG_BackVault"));
	return Tag;
}

const FGameplayTag& AdvancingCounterSourceTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Aerial.Source.IG_AdvancingCounter"));
	return Tag;
}

const FGameplayTag& KinsectPiercerSourceTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Aerial.Source.IG_KinsectPiercer"));
	return Tag;
}

const FGameplayTag& KinsectSlashSourceTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Aerial.Source.IG_KinsectSlash"));
	return Tag;
}

const FGameplayTag& KinsectSlashHitSourceTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Aerial.Source.IG_KinsectSlash.Hit"));
	return Tag;
}

const FGameplayTag& KinsectSlashMissSourceTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Aerial.Source.IG_KinsectSlash.Miss"));
	return Tag;
}

const FGameplayTag& DescendingThrustSourceTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Aerial.Source.IG_DescendingThrust"));
	return Tag;
}

const FGameplayTag& MotionValueTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Damage.MotionValue"));
	return Tag;
}

bool IsFiniteVector(const FVector& Value)
{
	return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y)
		&& FMath::IsFinite(Value.Z);
}

bool ResolvePriorAerialSpeed(const FGameplayTag& SourceTag, float& OutSpeed)
{
	if (SourceTag == AirDodgeSourceTag())
	{
		OutSpeed = 900.0f;
		return true;
	}

	if (SourceTag == BackVaultSourceTag()
		|| SourceTag == AdvancingCounterSourceTag()
		|| SourceTag == KinsectPiercerSourceTag()
		|| SourceTag == KinsectSlashSourceTag()
		|| SourceTag == KinsectSlashHitSourceTag()
		|| SourceTag == KinsectSlashMissSourceTag())
	{
		OutSpeed = 400.0f;
		return true;
	}

	return false;
}

const URes_InsectGlaive* ResolveResourceFromASC(
	const UAbilitySystemComponent* ASC)
{
	const UMHGZAbilitySystemComponent* MHGZASC =
		Cast<UMHGZAbilitySystemComponent>(ASC);
	const UMHGZWeaponRuntimeHostComponent* Host =
		MHGZASC ? MHGZASC->GetRuntimeHost() : nullptr;
	return Host ? Cast<URes_InsectGlaive>(Host->GetResourceProvider()) : nullptr;
}

bool MontageHasDescendingEvent(const UAnimMontage* Montage,
	const EDescendingThrustMontageEvent RequiredEvent)
{
	if (!Montage)
	{
		return false;
	}

	for (const FAnimNotifyEvent& NotifyEvent : Montage->Notifies)
	{
		const UAnimNotify_DescendingThrustEvent* EventNotify =
			Cast<UAnimNotify_DescendingThrustEvent>(NotifyEvent.Notify);
		if (EventNotify && EventNotify->Event == RequiredEvent)
		{
			return true;
		}
	}
	return false;
}
}

UMHGZDescendingThrustAbility::UMHGZDescendingThrustAbility()
{
	InputTag = WeaponRTTag();
	DefaultEntrySection = StartSectionName;
	StaminaCostPolicy = EAbilityStaminaCostPolicy::Instant;
	StaminaCost = FScalableFloat(20.0f);

	ActivationRequiredTags.AddTag(UnsheathedTag());
	ActivationBlockedTags.AddTag(GroundedTag());
	ActivationBlockedTags.AddTag(AerialCantAttackTag());
	ActivationBlockedTags.AddTag(DeadTag());
	ActivationBlockedTags.AddTag(HitstunTag());
	ActivationBlockedTags.AddTag(KnockdownTag());
}

bool UMHGZDescendingThrustAbility::IsDirectionalBoostInputAboveThreshold(
	const FVector2D& RawMoveInput)
{
	return FMath::IsFinite(RawMoveInput.X)
		&& FMath::IsFinite(RawMoveInput.Y)
		&& RawMoveInput.Size() > 0.50f;
}

bool UMHGZDescendingThrustAbility::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags,
		OptionalRelevantTags))
	{
		return false;
	}

	const UAbilitySystemComponent* ASC = ActorInfo
		? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	const UMHGZAbilitySystemComponent* MHGZASC =
		Cast<UMHGZAbilitySystemComponent>(ASC);
	const UMHGZWeaponRuntimeHostComponent* Host =
		MHGZASC ? MHGZASC->GetRuntimeHost() : nullptr;
	float PriorSpeed = 0.0f;
	return Host && ResolveResourceFromASC(ASC)
		&& !Host->IsGrounded()
		&& (ASC->HasMatchingGameplayTag(AerialFallingTag())
			|| HasEarlyDescendingThrustPermission(ASC))
		&& ResolvePriorAerialSpeed(Host->GetAerialMovementSource(), PriorSpeed);
}

bool UMHGZDescendingThrustAbility::ValidateActionDependencies() const
{
	if (!Super::ValidateActionDependencies())
	{
		return false;
	}

	const FWeaponInputSnapshot& Input = GetWeaponActivationContext().Input;
	// The router's release fallback is a new discrete Started snapshot. The
	// duration bit is the identity discriminator; ordinary Started snapshots
	// never set it, and SourceControlTag/SequenceID alone are not sufficient.
	if (!IsReleaseFallbackInput(Input))
	{
		return false;
	}

	FName SelectedEntry;
	const bool bEntrySelected = SelectAttackMontageStartSection(SelectedEntry);
	const bool bSectionsValid = HasRequiredSections(AttackMontage);
	const bool bLandingValid = HasRequiredLandingMontage();
	const bool bEventsValid = HasRequiredMontageEvents();
	const bool bProfileValid = HasUsableTrajectoryProfile();
	if (!bEntrySelected || SelectedEntry != StartSectionName
		|| !bSectionsValid || !bLandingValid || !bEventsValid || !bProfileValid)
	{
		return false;
	}

	const UMHGZAbilitySystemComponent* ASC = Cast<UMHGZAbilitySystemComponent>(
		GetAbilitySystemComponentFromActorInfo());
	const UMHGZWeaponRuntimeHostComponent* Host = ASC
		? ASC->GetRuntimeHost() : nullptr;
	const ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!IsLiveAerialEntry() || !Character || !Character->GetMesh()
		|| !Character->GetMesh()->GetAnimInstance())
	{
		return false;
	}

	if (AttackSegments.Num() != 4)
	{
		return false;
	}
	for (int32 SegmentIndex = 0; SegmentIndex < AttackSegments.Num(); ++SegmentIndex)
	{
		const FAttackSegmentConfig& Segment = AttackSegments[SegmentIndex];
		if (!Segment.Damage.DamageEffectClass
			|| !FMath::IsFinite(Segment.Damage.MotionValue.GetValueAtLevel(1.0f))
			|| Segment.Damage.MotionValue.GetValueAtLevel(1.0f) <= 0.0f
			|| Segment.Damage.bRequiresHitToContinue
			|| Segment.MultiHitCount != 1
			|| Segment.MultiHitPolicy != EAttackMultiHitPolicy::ContactOnly
			|| Segment.Collision.TraceRegions.IsEmpty())
		{
			return false;
		}
	}
	return true;
}

bool UMHGZDescendingThrustAbility::HasRequiredSections(
	const UAnimMontage* Montage) const
{
	if (!Montage || StartSectionName.IsNone()
		|| FallingLoopSectionName.IsNone()
		|| !Montage->IsValidSectionName(StartSectionName)
		|| !Montage->IsValidSectionName(FallingLoopSectionName)
		|| StartSectionName == FallingLoopSectionName
		|| DefaultEntrySection != StartSectionName)
	{
		return false;
	}

	const FCompositeSection* StartSection = nullptr;
	const FCompositeSection* FallingLoopSection = nullptr;
	for (const FCompositeSection& Section : Montage->CompositeSections)
	{
		if (Section.SectionName == StartSectionName)
		{
			StartSection = &Section;
		}
		else if (Section.SectionName == FallingLoopSectionName)
		{
			FallingLoopSection = &Section;
		}
	}
	return StartSection && FallingLoopSection
		&& StartSection->NextSectionName == FallingLoopSectionName
		&& FallingLoopSection->NextSectionName == FallingLoopSectionName;
}

bool UMHGZDescendingThrustAbility::HasRequiredLandingMontage() const
{
	if (!LandingMontage || LandingSectionName.IsNone()
		|| !LandingMontage->IsValidSectionName(LandingSectionName))
	{
		return false;
	}
	return LandingMontage->CompositeSections.Num() == 1
		&& LandingMontage->CompositeSections[0].SectionName == LandingSectionName
		&& LandingMontage->CompositeSections[0].NextSectionName.IsNone()
		&& LandingMontage->HasRootMotion();
}

bool UMHGZDescendingThrustAbility::HasRequiredMontageEvents() const
{
	return MontageHasDescendingEvent(AttackMontage,
		EDescendingThrustMontageEvent::EnterFallingLoop)
		&& MontageHasDescendingEvent(LandingMontage,
			EDescendingThrustMontageEvent::Correction3);
}

const URes_InsectGlaive* UMHGZDescendingThrustAbility::GetUsableResource() const
{
	return GetIGResourceComponent();
}

bool UMHGZDescendingThrustAbility::HasUsableTrajectoryProfile() const
{
	const URes_InsectGlaive* Resource = GetUsableResource();
	const UInsectGlaiveCombatConfig* Config = Resource
		? Resource->GetCombatConfig() : nullptr;
	return Config
		&& DescendingThrustAirProfile.IsValid()
		&& FMath::IsFinite(DescendingThrustAirWatchdogSeconds)
		&& DescendingThrustAirWatchdogSeconds > 0.0f
		&& FMath::IsFinite(DescendingThrustLandingBlendInTime)
		&& DescendingThrustLandingBlendInTime >= 0.0f;
}

bool UMHGZDescendingThrustAbility::IsReleaseFallbackInput(
	const FWeaponInputSnapshot& Input) const
{
	if (!Input.ResolvedInputTag.MatchesTagExact(WeaponRTTag())
		|| !Input.SourceControlTag.MatchesTagExact(PhysicalRTTag())
		|| Input.Phase != EWeaponInputPhase::Started
		|| !Input.bHasPhysicalPressDuration
		|| Input.SequenceID == 0
		|| !FMath::IsFinite(Input.PhysicalPressDurationSeconds)
		|| Input.PhysicalPressDurationSeconds < 0.0f
		|| Input.PhysicalPressDurationSeconds > 0.25f
		|| !FMath::IsFinite(static_cast<float>(Input.Timestamp))
		|| !FMath::IsFinite(static_cast<float>(Input.PhysicalPressStartedAtSeconds))
		|| Input.Timestamp < Input.PhysicalPressStartedAtSeconds)
	{
		return false;
	}

	const float MeasuredDuration = static_cast<float>(
		Input.Timestamp - Input.PhysicalPressStartedAtSeconds);
	return FMath::IsFinite(MeasuredDuration)
		&& FMath::IsNearlyEqual(MeasuredDuration,
			Input.PhysicalPressDurationSeconds, 0.02f);
}

bool UMHGZDescendingThrustAbility::IsLiveAerialEntry() const
{
	const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	const UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	float PriorSpeed = 0.0f;
	return ASC && Host && !Host->IsGrounded()
		&& ASC->HasMatchingGameplayTag(UnsheathedTag())
		&& (ASC->HasMatchingGameplayTag(AerialFallingTag())
			|| HasEarlyDescendingThrustPermission(ASC))
		&& !ASC->HasMatchingGameplayTag(GroundedTag())
		&& !ASC->HasMatchingGameplayTag(AerialCantAttackTag())
		&& !ASC->HasMatchingGameplayTag(DeadTag())
		&& !ASC->HasMatchingGameplayTag(HitstunTag())
		&& !ASC->HasMatchingGameplayTag(KnockdownTag())
		&& ResolvePriorAerialSpeed(Host->GetAerialMovementSource(), PriorSpeed);
}

bool UMHGZDescendingThrustAbility::ReadPreviousFrameSnapshot(
	FActionMoveInputFrameSnapshot& OutSnapshot) const
{
	const AMHGZCharacter* Character = Cast<AMHGZCharacter>(
		GetAvatarActorFromActorInfo());
	if (!Character || GFrameCounter == 0)
	{
		return false;
	}
	return Character->GetActionMoveInputFrameSnapshot(
		GFrameCounter - 1, OutSnapshot);
}

bool UMHGZDescendingThrustAbility::IsUsableMoveSnapshot(
	const FActionMoveInputFrameSnapshot& Snapshot) const
{
	const AMHGZCharacter* Character = Cast<AMHGZCharacter>(
		GetAvatarActorFromActorInfo());
	return Character && Snapshot.Pawn.Get() == Character
		&& Snapshot.RuntimeToken == GetActionToken().RuntimeToken
		&& GFrameCounter > 0
		&& Snapshot.FrameNumber == GFrameCounter - 1
		&& Snapshot.bHasMoveInput
		&& FMath::IsFinite(Snapshot.RawMoveInput.X)
		&& FMath::IsFinite(Snapshot.RawMoveInput.Y)
		&& IsFiniteVector(Snapshot.WorldDirection)
		&& !Snapshot.WorldDirection.IsNearlyZero();
}

bool UMHGZDescendingThrustAbility::ConsumeCorrectionFromPreviousFrame(
	const int32 CorrectionOrdinal, const float MaxCorrectionAngleDegrees,
	const float CorrectionDuration, bool& bOutSnapshotValid, bool& bOutApplied,
	const bool bApplyDirectionCorrectionImmediately)
{
	bOutSnapshotValid = false;
	bOutApplied = false;
	if (CorrectionOrdinal < 0 || CorrectionOrdinal >= 3
		|| bCorrectionConsumed[CorrectionOrdinal])
	{
		return false;
	}

	// Consumption is independent of snapshot validity: a missing/invalid N-1
	// frame is never retried from the current frame or from last non-zero input.
	bCorrectionConsumed[CorrectionOrdinal] = true;
	FActionMoveInputFrameSnapshot Snapshot;
	if (!ReadPreviousFrameSnapshot(Snapshot)
		|| !IsUsableMoveSnapshot(Snapshot))
	{
		return true;
	}

	bOutSnapshotValid = true;
	CorrectionFrameNumbers[CorrectionOrdinal] = Snapshot.FrameNumber;
	CorrectionRawMoveInputs[CorrectionOrdinal] = Snapshot.RawMoveInput;
	CorrectionTargets[CorrectionOrdinal] = Snapshot.WorldDirection;
	if (const ACharacter* Character = Cast<ACharacter>(
		GetAvatarActorFromActorInfo()))
	{
		CorrectionAngles[CorrectionOrdinal] = FMath::FindDeltaAngleDegrees(
			Character->GetActorRotation().Yaw,
			Snapshot.WorldDirection.Rotation().Yaw);
	}
	if (bApplyDirectionCorrectionImmediately)
	{
		bOutApplied = ApplyInActionDirectionCorrectionFromSnapshot(
			GetActionToken(), Snapshot, MaxCorrectionAngleDegrees,
			CorrectionDuration, 0.0f,
			EActionDirectionCorrectionEasing::EaseOut);
	}
	bCorrectionSnapshotValid[CorrectionOrdinal] = true;
	bCorrectionApplied[CorrectionOrdinal] = bOutApplied;
	return true;
}

void UMHGZDescendingThrustAbility::ApplyFirstCorrectionAfterMovementStarts()
{
	if (!bCorrectionConsumed[0] || !bCorrectionSnapshotValid[0]
		|| CorrectionFrameNumbers[0] + 1 != GFrameCounter
		|| !IsCurrentAction(DescendingActionToken))
	{
		return;
	}

	// Keep the already-consumed N-1 direction. Starting the MovementTask first
	// makes this action the Host's movement owner, allowing it to take over a
	// rotation override that is still finishing on the predecessor action.
	bCorrectionApplied[0] = ApplyInActionDirectionCorrectionWithDirection(
		GetActionToken(), CorrectionTargets[0], 180.0f, 0.1f, 0.0f,
		EActionDirectionCorrectionEasing::EaseOut);
}

bool UMHGZDescendingThrustAbility::CaptureFirstSnapshotAndLockVelocity()
{
	AMHGZCharacter* Character = Cast<AMHGZCharacter>(
		GetAvatarActorFromActorInfo());
	UCharacterMovementComponent* CMC = Character
		? Character->GetCharacterMovement() : nullptr;
	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	if (!Character || !CMC || !Host)
	{
		return false;
	}

	bool bSnapshotValid = false;
	bool bApplied = false;
	if (!ConsumeCorrectionFromPreviousFrame(0, 180.0f, 0.1f,
		bSnapshotValid, bApplied, false))
	{
		return false;
	}
	float PriorSpeed = 0.0f;
	if (!ResolvePriorAerialSpeed(Host->GetAerialMovementSource(), PriorSpeed))
	{
		return false;
	}

	FVector BaseDirection(CMC->Velocity.X, CMC->Velocity.Y, 0.0f);
	BaseDirection = BaseDirection.GetSafeNormal2D();
	if (BaseDirection.IsNearlyZero())
	{
		BaseDirection = Character->GetActorForwardVector().GetSafeNormal2D();
	}
	if (!IsFiniteVector(BaseDirection) || BaseDirection.IsNearlyZero())
	{
		return false;
	}

	LockedPlanarVelocity = BaseDirection * PriorSpeed;
	if (bSnapshotValid
		&& IsDirectionalBoostInputAboveThreshold(CorrectionRawMoveInputs[0])
		&& IsFiniteVector(CorrectionTargets[0])
		&& !CorrectionTargets[0].IsNearlyZero())
	{
		LockedPlanarVelocity += CorrectionTargets[0].GetSafeNormal2D() * 300.0f;
	}
	return IsFiniteVector(LockedPlanarVelocity)
		&& !LockedPlanarVelocity.IsNearlyZero();
}

void UMHGZDescendingThrustAbility::ApplyActivationDirectionCorrection()
{
	// This action consumes the exact N-1 frame in PrepareAttackMontage. The
	// inherited live-input activation correction must not run a second time.
}

bool UMHGZDescendingThrustAbility::PrepareAttackMontage()
{
	// UMHGZGameplayAbility consumes the pending activation context before this
	// hook is reached; retain that exact snapshot for telemetry and #1 locking.
	ActivationInputSnapshot = GetWeaponActivationContext().Input;
	if (!HasRequiredSections(AttackMontage)
		|| !HasRequiredLandingMontage()
		|| !HasRequiredMontageEvents()
		|| !CaptureFirstSnapshotAndLockVelocity())
	{
		return false;
	}

	// Commit has succeeded and the resident Start section is about to play. The
	// inherited aerial-falling visual/state is released before the action owns
	// the protected trajectory; cancellation lets the IG base restore it.
	if (UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost())
	{
		Host->ReleaseAerialFallingState();
	}
	return true;
}

bool UMHGZDescendingThrustAbility::StartAttackMontage(
	ACharacter& Character, UAnimMontage* Montage, const FName StartSection)
{
	USkeletalMeshComponent* Mesh = Character.GetMesh();
	UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (!AnimInstance || !Montage || StartSection != StartSectionName
		|| !HasRequiredSections(Montage))
	{
		return false;
	}

	TSet<int32> ExistingInstances;
	for (const FAnimMontageInstance* Instance : AnimInstance->MontageInstances)
	{
		if (Instance)
		{
			ExistingInstances.Add(Instance->GetInstanceID());
		}
	}

	ActiveMainMontage = Montage;
	ActiveAttackMontage = Montage;
	CurrentMontageSection = StartSection;
	MainMontageTask = UAbilityTask_MHGZPlayMontageAndWait::
		CreatePlayMontageAndWaitProxy(
			this, TEXT("DescendingThrustMainMontage"), Montage, 1.0f,
			StartSection, ResolveAttackMontageBlendInTime(
				GetWeaponActivationContext()), false);
	if (!MainMontageTask)
	{
		return false;
	}

	MainMontageTask->OnCompleted.AddDynamic(this,
		&UMHGZDescendingThrustAbility::HandleMainMontageCompleted);
	MainMontageTask->OnInterrupted.AddDynamic(this,
		&UMHGZDescendingThrustAbility::HandleMainMontageInterrupted);
	MainMontageTask->OnCancelled.AddDynamic(this,
		&UMHGZDescendingThrustAbility::HandleMainMontageInterrupted);
	MainMontageTask->ReportCompletionAtMontageEnd();
	MainMontageTask->ReadyForActivation();

	FAnimMontageInstance* MontageInstance = nullptr;
	for (FAnimMontageInstance* Instance : AnimInstance->MontageInstances)
	{
		if (!Instance || Instance->Montage != Montage
			|| ExistingInstances.Contains(Instance->GetInstanceID()))
		{
			continue;
		}
		FWeaponActionToken RegisteredAction;
		if (UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost())
		{
			if (Host->ResolveMontage(Mesh, Instance->GetInstanceID(),
				RegisteredAction) && RegisteredAction != GetActionToken())
			{
				continue;
			}
		}
		MontageInstance = Instance;
		break;
	}

	if (!MontageInstance || !MainMontageTask || !IsActive())
	{
		return false;
	}

	MainMontageInstanceID = MontageInstance->GetInstanceID();
	MontageInstance->bEnableAutoBlendOut = false;
	MontageInstance->PushDisableRootMotion();
	return RegisterMontageInstance(Mesh, MainMontageInstanceID);
}

void UMHGZDescendingThrustAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	const FVector KinsectOrigin = Avatar ? Avatar->GetActorLocation() : FVector::ZeroVector;
	const URes_InsectGlaive* EntryResource = GetIGResourceComponent();
	const bool bKinsectWasAttached = EntryResource && EntryResource->GetKinsectActor()
		&& EntryResource->GetKinsectActor()->GetState() == EKinsectState::Attached;
	ResetActivationState();
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!IsActionActivationCommitted() || !IsActive())
	{
		return;
	}

	DescendingActionToken = GetActionToken();
	Phase = EDescendingThrustPhase::AirStart;
	CurrentMontageSection = StartSectionName;
	BindLifecycleGuards();
	if (!IsActive() || bEndingDescendingThrust)
	{
		return;
	}

	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	URes_InsectGlaive* Resource = GetIGResourceComponent();
	AMHGZCharacter* Character = Cast<AMHGZCharacter>(
		GetAvatarActorFromActorInfo());
	UCharacterMovementComponent* CMC = Character
		? Character->GetCharacterMovement() : nullptr;
	if (!Host || !Resource || !Character || !CMC
		|| !Host->IsTokenCurrent(DescendingActionToken.RuntimeToken))
	{
		CancelDescendingThrust(EWeaponActionEndReason::Interrupted,
			TEXT("The action requires a live character, CMC, Host, and resource."));
		return;
	}

	PriorAerialSource = Host->GetAerialMovementSource();
	float PriorSourceSpeed = 0.0f;
	if (!ResolvePriorAerialSpeed(PriorAerialSource, PriorSourceSpeed))
	{
		CancelDescendingThrust(EWeaponActionEndReason::Interrupted,
			TEXT("The prior aerial source became unknown after commit."));
		return;
	}

	if (!ConsumeAerialAttackAndDodgeBudgets()
		|| !Host->SetAerialMovementSource(DescendingActionToken,
			DescendingThrustSourceTag()))
	{
		CancelDescendingThrust(EWeaponActionEndReason::Interrupted,
			TEXT("The Host could not claim the aerial attack/dodge budget or source ownership."));
		return;
	}

	FrozenDanceDamageMultiplier = MHGZ::IGActionConfig::DanceDamageMultiplier(DescendingThrustDanceDamageMultipliers, Resource->GetDanceStacks(), Resource->GetCombatConfig()->MaxDanceStacks);
	if (!FMath::IsFinite(FrozenDanceDamageMultiplier)
		|| FrozenDanceDamageMultiplier <= 0.0f)
	{
		CancelDescendingThrust(EWeaponActionEndReason::Interrupted,
			TEXT("The Dance damage multiplier was invalid at Commit."));
		return;
	}
	Resource->ClearDanceStacks(EIGDanceClearReason::DescendingThrust);
	GenerateSegmentDamageInstanceIDs();

	if (!StartRecordedTrajectoryMovement())
	{
		CancelDescendingThrust(EWeaponActionEndReason::Interrupted,
			TEXT("The protected recorded Descending Thrust trajectory could not start."));
		return;
	}

	ApplyFirstCorrectionAfterMovementStarts();
	if (bKinsectWasAttached)
		Resource->BeginDescendingThrustKinsect(DescendingActionToken, KinsectOrigin,
			GetActivationAttackInstanceID(), KinsectDiveMotionValue);
}

bool UMHGZDescendingThrustAbility::ResolveKinsectDiveTarget(ACharacter& Character,
	const UAnimMontage& Montage, const float MontagePosition, FVector& OutTarget)
{
	OutTarget = FVector::ZeroVector;
	USkeletalMeshComponent* Mesh = Character.GetMesh();
	const UCharacterMovementComponent* CMC = Character.GetCharacterMovement();
	if (!Mesh || !CMC || !CMC->IsMovingOnGround() || !FMath::IsFinite(MontagePosition)
		|| MontagePosition < 0.f || MontagePosition > Montage.GetPlayLength()) return false;
	// Extract through the real Montage track, including trims/rates, then use the
	// same mesh-to-world conversion as CMC. +Y in the source is not world +Y/Z.
	FTransform RemainingLocal = Montage.ExtractRootMotionFromTrackRange(
		MontagePosition, Montage.GetPlayLength(), FAnimExtractContext());
	RemainingLocal.ScaleTranslation(Character.GetAnimRootMotionTranslationScale());
	if (RemainingLocal.ContainsNaN()) return false;
	const FTransform RemainingWorld = Mesh->ConvertLocalRootMotionToWorld(RemainingLocal);
	FVector GroundDelta = RemainingWorld.GetTranslation();
	GroundDelta.Z = 0.f; // Walking root motion does not change the capsule's height.
	const FVector FinalForward = RemainingWorld.GetRotation().RotateVector(
		Character.GetActorForwardVector()).GetSafeNormal2D();
	if (RemainingWorld.ContainsNaN() || FinalForward.IsNearlyZero()) return false;
	OutTarget = Character.GetActorLocation() + GroundDelta + FinalForward * 100.f;
	return !OutTarget.ContainsNaN();
}

void UMHGZDescendingThrustAbility::OnAttackCollisionWindowRetired(const int32 SegmentIndex, const uint64 Occurrence)
{
	// Only the actual final ground window, never airborne hits or forced cleanup.
	if (!bEndingDescendingThrust && IsActive() && Phase == EDescendingThrustPhase::GroundOver && SegmentIndex == 3)
	{
		ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
		UAnimInstance* Anim = Character && Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
		const FAnimMontageInstance* Instance = Anim ? Anim->GetMontageInstanceForID(LandingMontageInstanceID) : nullptr;
		URes_InsectGlaive* Resource = GetIGResourceComponent();
		FVector Target;
		// During CMC TickCharacterPose the current animation step has not moved
		// the capsule yet. Include it once; direct/post-movement callbacks use now.
		const float RootMotionBase = Instance && Character->GetMesh()->bIsAutonomousTickPose
			? Instance->GetPreviousPosition() : Instance ? Instance->GetPosition() : 0.f;
		if (Resource && Character && Instance && Instance->Montage == LandingMontage
			&& ResolveKinsectDiveTarget(*Character, *LandingMontage, RootMotionBase, Target)
			&& Resource->StartDescendingThrustKinsectDive(DescendingActionToken, Target))
		{
			UE_LOG(LogMHGZ, Display, TEXT("[DescendingThrustKinsect] Over=%.6f rootBase=%.6f capsule=%s fixedFinalTarget=%s"),
				Instance->GetPosition(), RootMotionBase, *Character->GetActorLocation().ToCompactString(), *Target.ToCompactString());
		}
	}
	Super::OnAttackCollisionWindowRetired(SegmentIndex, Occurrence);
}

FWeaponMovementRequest UMHGZDescendingThrustAbility::MakeMovementRequest() const
{
	FWeaponMovementRequest Request;
	Request.OwnerAction = DescendingActionToken;
	Request.Mode = EWeaponMovementMode::RecordedLocalTrajectory;
	Request.DirectionSnapshot = LockedPlanarVelocity.GetSafeNormal2D();
	Request.RecordedTrajectoryScale = FVector(
		LockedPlanarVelocity.Size(), 1.0f, 1.0f);
	Request.RotationPolicy = EActionRotationPolicy::Locked;
	Request.TrajectoryGroundPolicy = ERecordedTrajectoryGroundPolicy::ProtectedFlight;
	Request.TrajectoryTailPolicy = ERecordedTrajectoryTailPolicy::IntegrateTerminalModel;
	Request.CancelVelocityPolicy = EMovementCancelVelocityPolicy::PreserveVelocity;

	const URes_InsectGlaive* Resource = GetUsableResource();
	const UInsectGlaiveCombatConfig* Config = Resource
		? Resource->GetCombatConfig() : nullptr;
	if (Config)
	{
		Request.RecordedTrajectory = DescendingThrustAirProfile;
		Request.Duration = Request.RecordedTrajectory.GetDuration();
	}
	return Request;
}

bool UMHGZDescendingThrustAbility::StartRecordedTrajectoryMovement()
{
	const FWeaponMovementRequest Request = MakeMovementRequest();
	if (!Request.OwnerAction.IsValid()
		|| !Request.HasValidRecordedTrajectoryParameters()
		|| Request.DirectionSnapshot.IsNearlyZero()
		|| !FMath::IsFinite(Request.RecordedTrajectoryScale.X)
		|| Request.RecordedTrajectoryScale.X <= 0.0f
		|| Request.Duration <= 0.0f)
	{
		return false;
	}

	MovementActionToken = DescendingActionToken;
	UAbilityTask_MHGZWeaponMovement* StartedTask =
		UAbilityTask_MHGZWeaponMovement::StartWeaponMovement(this,
			TEXT("DescendingThrustMovement"), Request);
	if (!StartedTask)
	{
		return false;
	}

	MovementTask = StartedTask;
	StartedTask->OnFinished.AddDynamic(this,
		&UMHGZDescendingThrustAbility::HandleMovementFinished);
	StartedTask->ReadyForActivation();
	if (MovementTask != StartedTask || !StartedTask->DidStartMovement())
	{
		if (MovementTask)
		{
			MovementTask->OnFinished.RemoveAll(this);
			MovementTask->EndTask();
			MovementTask = nullptr;
		}
		return false;
	}

	StartWatchdog();
	return true;
}

void UMHGZDescendingThrustAbility::StartWatchdog()
{
	const URes_InsectGlaive* Resource = GetUsableResource();
	const UInsectGlaiveCombatConfig* Config = Resource
		? Resource->GetCombatConfig() : nullptr;
	UWorld* World = GetWorld();
	if (!Config || !World || !FMath::IsFinite(
		DescendingThrustAirWatchdogSeconds)
		|| DescendingThrustAirWatchdogSeconds <= 0.0f)
	{
		return;
	}
	World->GetTimerManager().SetTimer(WatchdogTimer, this,
		&UMHGZDescendingThrustAbility::HandleWatchdogExpired,
		DescendingThrustAirWatchdogSeconds, false);
}

void UMHGZDescendingThrustAbility::ClearWatchdog()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(WatchdogTimer);
	}
}

void UMHGZDescendingThrustAbility::HandleWatchdogExpired()
{
	if (IsActive() && !bEndingDescendingThrust
		&& (Phase == EDescendingThrustPhase::AirStart
			|| Phase == EDescendingThrustPhase::FallingLoop))
	{
		CancelDescendingThrust(EWeaponActionEndReason::Interrupted,
			TEXT("The Descending Thrust trajectory reached its anomaly watchdog."));
	}
}

void UMHGZDescendingThrustAbility::HandleMovementFinished(
	const FWeaponMovementResult& Result)
{
	const FWeaponActionToken CallbackToken = MovementActionToken;
	if (!IsCurrentAction(CallbackToken) || bEndingDescendingThrust)
	{
		return;
	}

	MovementTask = nullptr;
	if (Phase != EDescendingThrustPhase::FallingLoop
		|| Result.EndReason != EWeaponMovementEndReason::Landed)
	{
		CancelDescendingThrust(EWeaponActionEndReason::Interrupted,
			TEXT("The single recorded trajectory ended without a confirmed landing."));
		return;
	}

	if (!BeginLandingSwitch(Result.BlockingHit))
	{
		CancelDescendingThrust(EWeaponActionEndReason::Interrupted,
			TEXT("The confirmed landing could not enter the resident Over montage."));
	}
}

void UMHGZDescendingThrustAbility::HandleMontageEvent(
	const FWeaponActionToken& ActionToken, const int32 MontageInstanceID,
	const EDescendingThrustMontageEvent Event)
{
	if (!IsCurrentAction(ActionToken) || MontageInstanceID == INDEX_NONE)
	{
		return;
	}

	switch (Event)
	{
	case EDescendingThrustMontageEvent::EnterFallingLoop:
		if (Phase != EDescendingThrustPhase::AirStart
			|| MontageInstanceID != MainMontageInstanceID
			|| bFallingLoopEntered || !MovementTask)
		{
			return;
		}
		if (!MovementTask->EnableRecordedTrajectoryLanding(ActionToken))
		{
			CancelDescendingThrust(EWeaponActionEndReason::Interrupted,
				TEXT("EnterFallingLoop could not enable landing on the active trajectory task."));
			return;
		}
		bFallingLoopEntered = true;
		Phase = EDescendingThrustPhase::FallingLoop;
		CurrentMontageSection = FallingLoopSectionName;
		break;

	case EDescendingThrustMontageEvent::Correction3:
		if (Phase != EDescendingThrustPhase::GroundOver
			|| MontageInstanceID != LandingMontageInstanceID
			|| bCorrectionConsumed[2])
		{
			return;
		}
		{
			bool bSnapshotValid = false;
			bool bApplied = false;
			ConsumeCorrectionFromPreviousFrame(2, 60.0f, 0.2f,
				bSnapshotValid, bApplied);
		}
		break;
	}
}

bool UMHGZDescendingThrustAbility::AcquireLandingRootMotion()
{
	AMHGZCharacter* Character = Cast<AMHGZCharacter>(
		GetAvatarActorFromActorInfo());
	UCharacterMovementComponent* CMC = Character
		? Character->GetCharacterMovement() : nullptr;
	UAnimInstance* Anim = Character && Character->GetMesh()
		? Character->GetMesh()->GetAnimInstance() : nullptr;
	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	if (bLandingRootMotionModeCaptured)
	{
		return Host && Host->IsMontageRootMotionOwnedBy(DescendingActionToken)
			&& LandingRootMotionAnimInstance.Get() == Anim;
	}
	if (!CMC || !CMC->IsMovingOnGround() || !Anim || !Host
		|| !IsCurrentAction(DescendingActionToken)
		|| !Host->AcquireMontageRootMotion(DescendingActionToken))
	{
		return false;
	}

	LandingRootMotionAnimInstance = Anim;
	SavedLandingRootMotionMode = static_cast<uint8>(
		Anim->RootMotionMode.GetValue());
	bLandingRootMotionModeCaptured = true;
	Anim->SetRootMotionMode(ERootMotionMode::RootMotionFromMontagesOnly);
	CMC->StopMovementImmediately();
	return true;
}

void UMHGZDescendingThrustAbility::ReleaseLandingRootMotion()
{
	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	const bool bOwnsRootMotion = Host
		&& Host->IsMontageRootMotionOwnedBy(DescendingActionToken);
	if (bLandingRootMotionModeCaptured)
	{
		if (UAnimInstance* Anim = LandingRootMotionAnimInstance.Get())
		{
			if (Anim->RootMotionMode == ERootMotionMode::RootMotionFromMontagesOnly)
			{
				Anim->SetRootMotionMode(static_cast<ERootMotionMode::Type>(
					SavedLandingRootMotionMode));
			}
		}
	}

	if (bOwnsRootMotion)
	{
		ACharacter* Character = Cast<ACharacter>(
			GetAvatarActorFromActorInfo());
		UCharacterMovementComponent* CMC = Character
			? Character->GetCharacterMovement() : nullptr;
		if (CMC && CMC->IsMovingOnGround()
			&& GetActionEndReason() != EWeaponActionEndReason::Superseded)
		{
			CMC->StopMovementImmediately();
		}
		Host->ReleaseMontageRootMotion(DescendingActionToken);
	}

	LandingRootMotionAnimInstance.Reset();
	bLandingRootMotionModeCaptured = false;
}

bool UMHGZDescendingThrustAbility::PlayLandingMontage()
{
	AMHGZCharacter* Character = Cast<AMHGZCharacter>(
		GetAvatarActorFromActorInfo());
	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (!AnimInstance || !HasRequiredLandingMontage())
	{
		return false;
	}

	const URes_InsectGlaive* Resource = GetUsableResource();
	const UInsectGlaiveCombatConfig* Config = Resource
		? Resource->GetCombatConfig() : nullptr;
	const float BlendInTime = Config
		&& FMath::IsFinite(DescendingThrustLandingBlendInTime)
		? FMath::Max(0.0f, DescendingThrustLandingBlendInTime) : 0.1f;

	TSet<int32> ExistingInstances;
	for (const FAnimMontageInstance* Instance : AnimInstance->MontageInstances)
	{
		if (Instance)
		{
			ExistingInstances.Add(Instance->GetInstanceID());
		}
	}

	LandingMontageTask = UAbilityTask_MHGZPlayMontageAndWait::
		CreatePlayMontageAndWaitProxy(
			this, TEXT("DescendingThrustLandingMontage"), LandingMontage,
			1.0f, LandingSectionName, BlendInTime, false);
	if (!LandingMontageTask)
	{
		return false;
	}
	LandingMontageTask->OnCompleted.AddDynamic(this,
		&UMHGZDescendingThrustAbility::HandleLandingMontageCompleted);
	LandingMontageTask->OnInterrupted.AddDynamic(this,
		&UMHGZDescendingThrustAbility::HandleLandingMontageInterrupted);
	LandingMontageTask->OnCancelled.AddDynamic(this,
		&UMHGZDescendingThrustAbility::HandleLandingMontageInterrupted);
	LandingMontageTask->ReportCompletionAtMontageEnd();
	LandingMontageTask->ReadyForActivation();

	FAnimMontageInstance* MontageInstance = nullptr;
	for (FAnimMontageInstance* Instance : AnimInstance->MontageInstances)
	{
		if (!Instance || Instance->Montage != LandingMontage
			|| ExistingInstances.Contains(Instance->GetInstanceID()))
		{
			continue;
		}
		FWeaponActionToken RegisteredAction;
		if (UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost())
		{
			if (Host->ResolveMontage(Mesh, Instance->GetInstanceID(),
				RegisteredAction) && RegisteredAction != DescendingActionToken)
			{
				continue;
			}
		}
		MontageInstance = Instance;
		break;
	}

	if (!IsActive() || bEndingDescendingThrust || !LandingMontageTask)
	{
		if (MontageInstance)
		{
			MontageInstance->PushDisableRootMotion();
			MontageInstance->Stop(FAlphaBlend(0.0f));
		}
		return false;
	}
	if (!MontageInstance)
	{
		return false;
	}

	LandingMontageInstanceID = MontageInstance->GetInstanceID();
	MontageInstance->bEnableAutoBlendOut = false;
	ActiveAttackMontage = LandingMontage;
	CurrentMontageSection = LandingSectionName;
	if (!RegisterMontageInstance(Mesh, LandingMontageInstanceID))
	{
		return false;
	}
	bLandingMontageActive = true;
	return true;
}

bool UMHGZDescendingThrustAbility::BeginLandingSwitch(const FHitResult& Hit)
{
	if (Phase != EDescendingThrustPhase::FallingLoop
		|| bLandingLatched || bSwitchingToLanding)
	{
		return false;
	}

	bSwitchingToLanding = true;
	bLandingLatched = true;
	DisableCollision();
	ClearWatchdog();

	if (MovementTask)
	{
		MovementTask->OnFinished.RemoveAll(this);
		MovementTask->EndTask();
		MovementTask = nullptr;
	}

	AMHGZCharacter* Character = Cast<AMHGZCharacter>(
		GetAvatarActorFromActorInfo());
	UCharacterMovementComponent* CMC = Character
		? Character->GetCharacterMovement() : nullptr;
	if (!Character || !CMC || !Hit.IsValidBlockingHit()
		|| !CMC->IsValidLandingSpot(Character->GetActorLocation(), Hit))
	{
		return false;
	}
	CMC->SetMovementMode(MOVE_Walking);

	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	if (!Host || !IsCurrentAction(DescendingActionToken))
	{
		return false;
	}

	// This is the only landing handoff. The parent Host API performs its normal
	// state/tag/physics cleanup while explicitly skipping the generic 337 cm/s
	// reset and system landing Montage.
	Host->HandleLanded(false);

	if (MainMontageTask)
	{
		MainMontageTask->OnCompleted.RemoveAll(this);
		MainMontageTask->OnInterrupted.RemoveAll(this);
		MainMontageTask->OnCancelled.RemoveAll(this);
		MainMontageTask->EndTask();
		MainMontageTask = nullptr;
	}

	USkeletalMeshComponent* Mesh = Character->GetMesh();
	UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (Host->UnregisterMontages(DescendingActionToken) > 0
		&& AnimInstance && ActiveMainMontage
		&& MainMontageInstanceID != INDEX_NONE)
	{
		if (FAnimMontageInstance* ActiveInstance =
			AnimInstance->GetMontageInstanceForID(MainMontageInstanceID))
		{
			if (ActiveInstance->Montage == ActiveMainMontage)
			{
				const UInsectGlaiveCombatConfig* Config =
					GetUsableResource()
					? GetUsableResource()->GetCombatConfig() : nullptr;
				const float BlendOutTime = Config
					&& FMath::IsFinite(
						DescendingThrustLandingBlendInTime)
					? FMath::Max(0.0f,
						DescendingThrustLandingBlendInTime) : 0.1f;
				AnimInstance->Montage_Stop(BlendOutTime, ActiveMainMontage);
			}
		}
	}
	MainMontageInstanceID = INDEX_NONE;
	ActiveMainMontage = nullptr;

	bool bSnapshotValid = false;
	bool bApplied = false;
	ConsumeCorrectionFromPreviousFrame(1, 180.0f, 0.1f,
		bSnapshotValid, bApplied);
	Phase = EDescendingThrustPhase::GroundOver;
	if (!AcquireLandingRootMotion() || !PlayLandingMontage())
	{
		return false;
	}

	bSwitchingToLanding = false;
	return true;
}

void UMHGZDescendingThrustAbility::HandleMainMontageCompleted()
{
	if (Phase != EDescendingThrustPhase::AirStart
		&& Phase != EDescendingThrustPhase::FallingLoop)
	{
		return;
	}
	MainMontageTask = nullptr;
	UE_LOG(LogMHGZ, Warning,
		TEXT("[DescendingThrust] the Start/FallingLoop montage completed unexpectedly; the GA remains governed by the trajectory task."));
}

void UMHGZDescendingThrustAbility::HandleMainMontageInterrupted()
{
	MainMontageTask = nullptr;
	if (bEndingDescendingThrust || bSwitchingToLanding)
	{
		return;
	}
	if (IsActive() && (Phase == EDescendingThrustPhase::AirStart
		|| Phase == EDescendingThrustPhase::FallingLoop))
	{
		CancelDescendingThrust(EWeaponActionEndReason::Interrupted,
			TEXT("The primary Start/FallingLoop montage was interrupted."));
	}
}

void UMHGZDescendingThrustAbility::HandleLandingMontageCompleted()
{
	if (Phase != EDescendingThrustPhase::GroundOver
		|| bEndingDescendingThrust)
	{
		return;
	}
	LandingMontageTask = nullptr;
	bLandingMontageFinished = true;
	CompleteDescendingThrust();
}

void UMHGZDescendingThrustAbility::HandleLandingMontageInterrupted()
{
	if (HasPreparedDodgeSupersede())
	{
		// 与普通攻击同一交接合同：暂缓同步中断，任务留给提交/回滚清理。
		return;
	}
	LandingMontageTask = nullptr;
	if (bEndingDescendingThrust || !bLandingMontageActive)
	{
		return;
	}
	if (IsActive())
	{
		CancelDescendingThrust(EWeaponActionEndReason::Interrupted,
			TEXT("The resident Over landing montage was interrupted."));
	}
}

void UMHGZDescendingThrustAbility::OnMontageCompleted()
{
	// The main task is owned by this class and its natural completion is not an
	// action completion. Over completion is handled by the dedicated callback.
}

bool UMHGZDescendingThrustAbility::IsCurrentAction(
	const FWeaponActionToken& ActionToken) const
{
	const UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	return !bEndingDescendingThrust && IsActive()
		&& IsActionActivationCommitted()
		&& DescendingActionToken.IsValid()
		&& ActionToken == DescendingActionToken
		&& ActionToken == GetActionToken()
		&& Host && Host->IsTokenCurrent(ActionToken.RuntimeToken);
}

void UMHGZDescendingThrustAbility::GenerateSegmentDamageInstanceIDs()
{
	for (int32 Index = 0; Index < 4; ++Index)
	{
		FGuid NewID;
		do
		{
			NewID = FGuid::NewGuid();
		}
		while (!NewID.IsValid()
			|| [&]()
			{
				for (int32 Previous = 0; Previous < Index; ++Previous)
				{
					if (SegmentDamageInstanceIDs[Previous] == NewID)
					{
						return true;
					}
				}
				return false;
			}());
		SegmentDamageInstanceIDs[Index] = NewID;
	}
}

bool UMHGZDescendingThrustAbility::HasDamagedTarget(
	const int32 SegmentIndex, const AActor* Target) const
{
	return Target && DamagedTargetsPerSegment.IsValidIndex(SegmentIndex)
		&& DamagedTargetsPerSegment[SegmentIndex].Contains(
			TWeakObjectPtr<const AActor>(Target));
}

void UMHGZDescendingThrustAbility::RecordDamagedTarget(
	const int32 SegmentIndex, const AActor* Target)
{
	if (Target && DamagedTargetsPerSegment.IsValidIndex(SegmentIndex))
	{
		DamagedTargetsPerSegment[SegmentIndex].Add(
			TWeakObjectPtr<const AActor>(Target));
	}
}

bool UMHGZDescendingThrustAbility::CanApplyCollisionHit(
	const FHitResult& Hit, const int32 SegmentIndex) const
{
	return SegmentIndex >= 0 && SegmentIndex < 4
		&& !HasDamagedTarget(SegmentIndex, Hit.GetActor());
}

void UMHGZDescendingThrustAbility::HandleSuccessfulAttackDamage(
	const FHitResult& Hit, const int32 SegmentIndex)
{
	RecordDamagedTarget(SegmentIndex, Hit.GetActor());
	LastSuccessfulSegmentIndex = SegmentIndex;
	if (SegmentIndex == MarkSegmentIndex && !bEstablishedMarkThisActivation
		&& Phase == EDescendingThrustPhase::GroundOver)
	{
		if (URes_InsectGlaive* Resource = GetIGResourceComponent();
			Resource && Resource->SetKinsectMarkFromMeleeHit(Hit))
		{
			bEstablishedMarkThisActivation = true;
		}
	}
}

FGameplayEffectSpecHandle UMHGZDescendingThrustAbility::MakeDamageSpec(
	const FHitResult& Hit, const int32 SegmentIndex)
{
	if (!AttackSegments.IsValidIndex(SegmentIndex)
		|| SegmentIndex < 0 || SegmentIndex >= 4)
	{
		return FGameplayEffectSpecHandle();
	}
	FGameplayEffectSpecHandle Spec = Super::MakeDamageSpec(Hit, SegmentIndex);
	if (!Spec.IsValid() || !Spec.Data.IsValid()
		|| !SegmentDamageInstanceIDs[SegmentIndex].IsValid())
	{
		return FGameplayEffectSpecHandle();
	}

	FGameplayEffectContextHandle MutableContextHandle =
		Spec.Data->GetEffectContext();
	FMHGZGameplayEffectContext* Context =
		FMHGZGameplayEffectContext::ExtractEffectContext(MutableContextHandle);
	if (!Context || !AttackSegments.IsValidIndex(SegmentIndex)
		|| !FMath::IsFinite(FrozenDanceDamageMultiplier)
		|| FrozenDanceDamageMultiplier <= 0.0f)
	{
		return FGameplayEffectSpecHandle();
	}

	Context->AttackInstanceID = SegmentDamageInstanceIDs[SegmentIndex];
	const float MotionValue = AttackSegments[SegmentIndex].Damage.MotionValue
		.GetValueAtLevel(GetAbilityLevel()) * FrozenDanceDamageMultiplier;
	if (!FMath::IsFinite(MotionValue) || MotionValue <= 0.0f)
	{
		return FGameplayEffectSpecHandle();
	}
	Spec.Data->SetSetByCallerMagnitude(MotionValueTag(), MotionValue);
	return Spec;
}

void UMHGZDescendingThrustAbility::CompleteDescendingThrust()
{
	if (!IsActive() || bEndingDescendingThrust
		|| Phase != EDescendingThrustPhase::GroundOver
		|| !bLandingMontageFinished)
	{
		return;
	}
	bEndingDescendingThrust = true;
	DisableCollision();
	ClearWatchdog();
	RequestEndAction(EWeaponActionEndReason::Normal);
}

void UMHGZDescendingThrustAbility::CancelDescendingThrust(
	const EWeaponActionEndReason EndReason, const TCHAR* Diagnostic)
{
	if (!IsActive() || bEndingDescendingThrust)
	{
		return;
	}
	UE_LOG(LogMHGZ, Warning,
		TEXT("[DescendingThrust] action=%u phase=%d reason=%d: %s"),
		GetActionToken().ActivationSequenceID, static_cast<int32>(Phase),
		static_cast<int32>(EndReason), Diagnostic);
	bEndingDescendingThrust = true;
	ClearWatchdog();
	RequestEndAction(EndReason);
}

void UMHGZDescendingThrustAbility::BindLifecycleGuards()
{
	UMHGZAbilitySystemComponent* ASC = Cast<UMHGZAbilitySystemComponent>(
		GetAbilitySystemComponentFromActorInfo());
	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	if (!ASC || !Host)
	{
		return;
	}

	RuntimeInvalidationDelegateHandle = Host->OnWeaponRuntimeInvalidated.AddUObject(
		this, &UMHGZDescendingThrustAbility::HandleRuntimeInvalidated);
	const FGameplayTag DisqualifyingTags[] = { DeadTag(), HitstunTag(), KnockdownTag() };
	for (const FGameplayTag& Tag : DisqualifyingTags)
	{
		const FDelegateHandle DelegateHandle = ASC->RegisterGameplayTagEvent(
			Tag, EGameplayTagEventType::NewOrRemoved).AddUObject(
			this, &UMHGZDescendingThrustAbility::HandleDisqualifyingStateTagChanged);
		DisqualifyingStateTagDelegateHandles.Add(Tag, DelegateHandle);
	}

	if (!IsLiveContinuationStateValid())
	{
		CancelDescendingThrust(EWeaponActionEndReason::Interrupted,
			TEXT("A disqualifying state was already present when the action began."));
	}
}

void UMHGZDescendingThrustAbility::UnbindLifecycleGuards()
{
	if (UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost())
	{
		if (RuntimeInvalidationDelegateHandle.IsValid())
		{
			Host->OnWeaponRuntimeInvalidated.Remove(
				RuntimeInvalidationDelegateHandle);
		}
	}
	RuntimeInvalidationDelegateHandle.Reset();

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		for (const TPair<FGameplayTag, FDelegateHandle>& Pair :
			DisqualifyingStateTagDelegateHandles)
		{
			ASC->RegisterGameplayTagEvent(Pair.Key,
				EGameplayTagEventType::NewOrRemoved).Remove(Pair.Value);
		}
	}
	DisqualifyingStateTagDelegateHandles.Reset();
}

bool UMHGZDescendingThrustAbility::IsLiveContinuationStateValid() const
{
	const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	return ASC && GetRuntimeHost()
		&& ASC->HasMatchingGameplayTag(UnsheathedTag())
		&& !ASC->HasMatchingGameplayTag(DeadTag())
		&& !ASC->HasMatchingGameplayTag(HitstunTag())
		&& !ASC->HasMatchingGameplayTag(KnockdownTag());
}

void UMHGZDescendingThrustAbility::HandleRuntimeInvalidated(
	const FWeaponRuntimeToken& InvalidatedToken)
{
	if (IsActive() && DescendingActionToken.IsValid()
		&& DescendingActionToken.RuntimeToken == InvalidatedToken)
	{
		CancelDescendingThrust(EWeaponActionEndReason::Interrupted,
			TEXT("The owning weapon runtime token was invalidated."));
	}
}

void UMHGZDescendingThrustAbility::HandleDisqualifyingStateTagChanged(
	const FGameplayTag Tag, const int32 NewCount)
{
	if (NewCount > 0 && IsActive()
		&& (Tag == DeadTag() || Tag == HitstunTag() || Tag == KnockdownTag()))
	{
		CancelDescendingThrust(Tag == DeadTag()
			? EWeaponActionEndReason::Death
			: EWeaponActionEndReason::Interrupted,
			TEXT("A disqualifying combat-state tag was added during the action."));
	}
}

void UMHGZDescendingThrustAbility::GetRuntimeTelemetrySnapshot(
	FMHGZDescendingThrustTelemetrySnapshot& OutSnapshot) const
{
	OutSnapshot = FMHGZDescendingThrustTelemetrySnapshot();
	OutSnapshot.ActionToken = DescendingActionToken;
	OutSnapshot.AbilityConsumer = DescendingActionToken.AbilityHandle;
	OutSnapshot.ActivationSequenceID = DescendingActionToken.ActivationSequenceID;
	OutSnapshot.Phase = Phase;
	OutSnapshot.InputTag = ActivationInputSnapshot.ResolvedInputTag;
	OutSnapshot.InputSourceControlTag = ActivationInputSnapshot.SourceControlTag;
	OutSnapshot.InputSequenceID = ActivationInputSnapshot.SequenceID;
	OutSnapshot.PhysicalPressDurationSeconds =
		ActivationInputSnapshot.PhysicalPressDurationSeconds;
	OutSnapshot.bInputReleaseFallback =
		IsReleaseFallbackInput(ActivationInputSnapshot);
	OutSnapshot.PriorAerialSource = PriorAerialSource;
	OutSnapshot.CurrentMontageSection = CurrentMontageSection;
	OutSnapshot.MainMontagePath = ActiveMainMontage
		? ActiveMainMontage->GetPathName() : FString();
	OutSnapshot.LandingMontagePath = LandingMontage
		? LandingMontage->GetPathName() : FString();
	OutSnapshot.MainMontageInstanceID = MainMontageInstanceID;
	OutSnapshot.LandingMontageInstanceID = LandingMontageInstanceID;
	OutSnapshot.LockedPlanarVelocity = LockedPlanarVelocity;
	OutSnapshot.FrozenDanceDamageMultiplier = FrozenDanceDamageMultiplier;
	OutSnapshot.bLandingRootMotionModeCaptured =
		bLandingRootMotionModeCaptured;
	OutSnapshot.SavedAnimRootMotionMode =
		static_cast<int32>(SavedLandingRootMotionMode);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		OutSnapshot.CorrectionFrameNumbers[Index] = CorrectionFrameNumbers[Index];
		OutSnapshot.bCorrectionSnapshotValid[Index] =
			bCorrectionSnapshotValid[Index];
		OutSnapshot.bCorrectionApplied[Index] = bCorrectionApplied[Index];
		OutSnapshot.bCorrectionConsumed[Index] = bCorrectionConsumed[Index];
		OutSnapshot.CorrectionAngles[Index] = CorrectionAngles[Index];
		OutSnapshot.CorrectionRawMoveInputs[Index] =
			CorrectionRawMoveInputs[Index];
		OutSnapshot.CorrectionTargets[Index] = CorrectionTargets[Index];
	}
	for (int32 Index = 0; Index < 4; ++Index)
	{
		OutSnapshot.SegmentDamageInstanceIDs[Index] =
			SegmentDamageInstanceIDs[Index];
		OutSnapshot.SuccessfulTargetCounts[Index] =
			DamagedTargetsPerSegment.IsValidIndex(Index)
			? DamagedTargetsPerSegment[Index].Num() : 0;
	}
	OutSnapshot.LastSuccessfulSegmentIndex = LastSuccessfulSegmentIndex;

	const UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	OutSnapshot.PublishedAerialSource = Host
		? Host->GetAerialMovementSource() : FGameplayTag();
	OutSnapshot.bAbilityActive = IsActive() && Host
		&& DescendingActionToken.IsValid()
		&& Host->IsTokenCurrent(DescendingActionToken.RuntimeToken);
	OutSnapshot.bActionMovementOwned = Host
		&& Host->IsActionMovementOwnedBy(DescendingActionToken);
	OutSnapshot.bLandingRootMotionOwned = Host
		&& Host->IsMontageRootMotionOwnedBy(DescendingActionToken);

	const AMHGZCharacter* Character = Cast<AMHGZCharacter>(
		GetAvatarActorFromActorInfo());
	const UCharacterMovementComponent* CMC = Character
		? Character->GetCharacterMovement() : nullptr;
	if (const UMHGZInstrumentedCharacterMovementComponent* InstrumentedCMC =
		Cast<UMHGZInstrumentedCharacterMovementComponent>(CMC))
	{
		FMHGZRecordedTrajectoryTelemetry MovementTelemetry;
		InstrumentedCMC->GetRecordedTrajectoryTelemetry(DescendingActionToken,
			MovementTelemetry);
		OutSnapshot.MovementLease = MovementTelemetry.Lease;
		OutSnapshot.ActionClockSeconds = MovementTelemetry.ElapsedSeconds;
		OutSnapshot.bRecordedTrajectoryActive = MovementTelemetry.bActive;
	}

	const UAnimInstance* AnimInstance = Character && Character->GetMesh()
		? Character->GetMesh()->GetAnimInstance() : nullptr;
	if (AnimInstance)
	{
		OutSnapshot.CurrentAnimRootMotionMode = static_cast<int32>(
			AnimInstance->RootMotionMode.GetValue());
	}
}

void UMHGZDescendingThrustAbility::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const bool bReplicateEndAbility, const bool bWasCancelled)
{
	bEndingDescendingThrust = true;
	if (URes_InsectGlaive* Resource = GetIGResourceComponent())
	{
		const auto Reason = GetActionEndReason();
		Resource->FinishDescendingThrustKinsect(DescendingActionToken,
			bWasCancelled && Reason == EWeaponActionEndReason::Normal ? EWeaponActionEndReason::Interrupted : Reason);
	}
	DisableCollision();
	ClearWatchdog();
	Phase = EDescendingThrustPhase::Ended;
	UnbindLifecycleGuards();

	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	const FWeaponRuntimeToken RuntimeToken = DescendingActionToken.RuntimeToken;
	if (bWasCancelled && Host && !Host->IsGrounded()
		&& DescendingActionToken.IsValid()
		&& Host->GetAerialMovementSource() == DescendingThrustSourceTag())
	{
		float PriorSourceSpeed = 0.0f;
		if (ResolvePriorAerialSpeed(PriorAerialSource, PriorSourceSpeed)
			&& !Host->SetAerialMovementSource(DescendingActionToken,
				PriorAerialSource))
		{
			UE_LOG(LogMHGZ, Warning,
				TEXT("[DescendingThrust] cancelled airborne action could not restore its prior aerial source %s"),
				*PriorAerialSource.ToString());
		}
	}
	if (Host && DescendingActionToken.IsValid())
	{
		// Invalidate notify resolution before stopping either outgoing instance.
		Host->UnregisterMontages(DescendingActionToken);
	}

	if (MovementTask)
	{
		MovementTask->OnFinished.RemoveAll(this);
		MovementTask->EndTask();
		MovementTask = nullptr;
	}
	if (MainMontageTask)
	{
		MainMontageTask->OnCompleted.RemoveAll(this);
		MainMontageTask->OnInterrupted.RemoveAll(this);
		MainMontageTask->OnCancelled.RemoveAll(this);
		MainMontageTask->EndTask();
		MainMontageTask = nullptr;
	}
	if (LandingMontageTask)
	{
		LandingMontageTask->OnCompleted.RemoveAll(this);
		LandingMontageTask->OnInterrupted.RemoveAll(this);
		LandingMontageTask->OnCancelled.RemoveAll(this);
		LandingMontageTask->EndTask();
		LandingMontageTask = nullptr;
	}

	AMHGZCharacter* Character = Cast<AMHGZCharacter>(
		GetAvatarActorFromActorInfo());
	UAnimInstance* AnimInstance = Character && Character->GetMesh()
		? Character->GetMesh()->GetAnimInstance() : nullptr;
	if (AnimInstance)
	{
		if (ActiveMainMontage && MainMontageInstanceID != INDEX_NONE)
		{
			if (FAnimMontageInstance* Instance =
				AnimInstance->GetMontageInstanceForID(MainMontageInstanceID))
			{
				if (Instance->Montage == ActiveMainMontage)
				{
					Instance->Stop(FAlphaBlend(0.05f));
				}
			}
		}
		if (LandingMontage && LandingMontageInstanceID != INDEX_NONE)
		{
			if (FAnimMontageInstance* Instance =
				AnimInstance->GetMontageInstanceForID(LandingMontageInstanceID))
			{
				if (Instance->Montage == LandingMontage)
				{
					Instance->PushDisableRootMotion();
					Instance->Stop(FAlphaBlend(0.05f));
				}
			}
		}
	}

	ReleaseLandingRootMotion();
	UCharacterMovementComponent* CMC = Character
		? Character->GetCharacterMovement() : nullptr;
	const bool bNeedsMovementModeRestore = CMC
		&& CMC->MovementMode == MOVE_Flying;
	ActiveMainMontage = nullptr;
	ActiveAttackMontage = nullptr;
	MainMontageInstanceID = INDEX_NONE;
	LandingMontageInstanceID = INDEX_NONE;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo,
		bReplicateEndAbility, bWasCancelled);

	if (bNeedsMovementModeRestore && Host
		&& Host->IsTokenCurrent(RuntimeToken) && !Host->IsGrounded()
		&& !Host->HasRegisteredAction() && CMC
		&& CMC->MovementMode == MOVE_Flying && Character)
	{
		FFindFloorResult Floor;
		CMC->FindFloor(Character->GetActorLocation(), Floor, false);
		const bool bOnFloor = Floor.IsWalkableFloor()
			&& Floor.FloorDist <= UCharacterMovementComponent::MAX_FLOOR_DIST;
		CMC->SetMovementMode(bOnFloor ? MOVE_Walking : MOVE_Falling);
	}
}

void UMHGZDescendingThrustAbility::ResetActivationState()
{
	ClearWatchdog();
	Phase = EDescendingThrustPhase::Inactive;
	DescendingActionToken = FWeaponActionToken();
	MovementActionToken = FWeaponActionToken();
	ActivationInputSnapshot = FWeaponInputSnapshot();
	PriorAerialSource = FGameplayTag();
	MainMontageTask = nullptr;
	LandingMontageTask = nullptr;
	MovementTask = nullptr;
	ActiveMainMontage = nullptr;
	CurrentMontageSection = NAME_None;
	MainMontageInstanceID = INDEX_NONE;
	LandingMontageInstanceID = INDEX_NONE;
	LockedPlanarVelocity = FVector::ZeroVector;
	FrozenDanceDamageMultiplier = 1.0f;
	LastSuccessfulSegmentIndex = INDEX_NONE;
	bEstablishedMarkThisActivation = false;
	for (FGuid& ID : SegmentDamageInstanceIDs)
	{
		ID.Invalidate();
	}
	DamagedTargetsPerSegment.Reset();
	DamagedTargetsPerSegment.SetNum(4);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		CorrectionFrameNumbers[Index] = 0;
		bCorrectionSnapshotValid[Index] = false;
		bCorrectionApplied[Index] = false;
		bCorrectionConsumed[Index] = false;
		CorrectionAngles[Index] = 0.0f;
		CorrectionRawMoveInputs[Index] = FVector2D::ZeroVector;
		CorrectionTargets[Index] = FVector::ZeroVector;
	}
	bFallingLoopEntered = false;
	bLandingLatched = false;
	bSwitchingToLanding = false;
	bLandingMontageActive = false;
	bLandingMontageFinished = false;
	bEndingDescendingThrust = false;
	LandingRootMotionAnimInstance.Reset();
	SavedLandingRootMotionMode = 0;
	bLandingRootMotionModeCaptured = false;
}

#if WITH_EDITOR
EDataValidationResult UMHGZDescendingThrustAbility::IsDataValid(
	FDataValidationContext& Context) const
{
	const EDataValidationResult Result = Super::IsDataValid(Context);
	if (ValidateOwnedActionConfig(Context) == EDataValidationResult::Invalid) return EDataValidationResult::Invalid;
	bool bInvalid = false;
	auto AddError = [&Context, &bInvalid](const FString& Message)
	{
		Context.AddError(FText::FromString(Message));
		bInvalid = true;
	};

	if (!AttackMontage)
	{
		AddError(TEXT("Descending Thrust requires a resident AttackMontage; dynamic montage construction is forbidden."));
	}
	else if (!HasRequiredSections(AttackMontage))
	{
		AddError(TEXT("AttackMontage must contain Start -> FallingLoop and FallingLoop -> FallingLoop sections."));
	}
	if (!LandingMontage)
	{
		AddError(TEXT("Descending Thrust requires a resident LandingMontage with an Over section."));
	}
	else if (!HasRequiredLandingMontage())
	{
		AddError(TEXT("LandingMontage must contain exactly one root-motion Over section with no next section."));
	}
	if (!HasRequiredMontageEvents())
	{
		AddError(TEXT("Resident montages must author EnterFallingLoop and Correction3 UAnimNotify_DescendingThrustEvent events."));
	}
	if (!InputTag.MatchesTagExact(WeaponRTTag()))
	{
		AddError(TEXT("Descending Thrust InputTag must be Input.Weapon.RT."));
	}
	if (AttackSegments.Num() != 4)
	{
		AddError(TEXT("Descending Thrust requires exactly four independent attack segments."));
	}
	else
	{
		for (int32 SegmentIndex = 0; SegmentIndex < 4; ++SegmentIndex)
		{
			const FAttackSegmentConfig& Segment = AttackSegments[SegmentIndex];
			if (!Segment.Damage.DamageEffectClass
				|| Segment.Damage.MotionValue.GetValueAtLevel(1.0f) <= 0.0f
				|| Segment.Damage.bRequiresHitToContinue
				|| Segment.MultiHitCount != 1
				|| Segment.MultiHitPolicy != EAttackMultiHitPolicy::ContactOnly)
			{
				AddError(FString::Printf(
					TEXT("AttackSegments[%d] requires a GE, positive MotionValue, and one ContactOnly hit."),
					SegmentIndex));
			}
			if (Segment.Collision.TraceRegions.IsEmpty())
			{
				AddError(FString::Printf(
					TEXT("AttackSegments[%d] requires TraceRegions."), SegmentIndex));
			}
		}
	}
	if (MarkSegmentIndex < 0 || MarkSegmentIndex >= AttackSegments.Num())
	{
		AddError(TEXT("MarkSegmentIndex must identify one of the four descending-thrust segments."));
	}

	return bInvalid ? EDataValidationResult::Invalid
		: (Result == EDataValidationResult::NotValidated
			? EDataValidationResult::Valid : Result);
}
#endif

#if WITH_EDITOR
#undef LOCTEXT_NAMESPACE
#define LOCTEXT_NAMESPACE "MHGZDescendingThrustConfig"
EDataValidationResult UMHGZDescendingThrustAbility::ValidateOwnedActionConfig(FDataValidationContext& Context) const
{
	const EDataValidationResult Result = EDataValidationResult::Valid;
	bool bInvalid = false;
	auto AddError = [&Context, &bInvalid](const FText& Error)
	{
		Context.AddError(Error);
		bInvalid = true;
	};

	auto CheckPositive = [&AddError](const TCHAR* FieldName, float Value)
	{
		if (!FMath::IsFinite(Value) || Value <= 0.f)
		{
			AddError(FText::Format(
				LOCTEXT("NotPositive", "{0} must be > 0 (current {1})."),
				FText::FromString(FieldName), FText::AsNumber(Value)));
		}
	};

	auto CheckNonNegative = [&AddError](const TCHAR* FieldName, float Value)
	{
		if (!FMath::IsFinite(Value) || Value < 0.f)
		{
			AddError(FText::Format(
				LOCTEXT("Negative", "{0} must be >= 0 (current {1})."),
				FText::FromString(FieldName), FText::AsNumber(Value)));
		}
	};

	CheckPositive(TEXT("DescendingThrustAirWatchdogSeconds"),
		DescendingThrustAirWatchdogSeconds);
	CheckNonNegative(TEXT("DescendingThrustLandingBlendInTime"),
		DescendingThrustLandingBlendInTime);
	CheckNonNegative(TEXT("DescendingThrustAirControl"), DescendingThrustAirControl);
	auto CheckTrajectoryProfile = [&AddError](const TCHAR* FieldName,
		const FRecordedLocalTrajectoryProfile& Profile)
	{
		if (!Profile.Curve)
		{
			return;
		}
		if (!Profile.IsValid())
		{
			AddError(FText::Format(
				LOCTEXT("InvalidPiercerTrajectory",
					"{0} is assigned but not self-consistent: it needs strictly increasing finite keys starting at the origin, a positive CoveredSeconds no later than its last key, and finite terminal velocity/acceleration."),
				FText::FromString(FieldName)));
		}
	};
	CheckTrajectoryProfile(TEXT("DescendingThrustAirProfile"), DescendingThrustAirProfile);

    if (DescendingThrustDanceDamageMultipliers.IsEmpty() || !FMath::IsNearlyEqual(DescendingThrustDanceDamageMultipliers[0], 1.0f)) AddError(FText::FromString(TEXT("舞踏倍率第0层必须为1。")));
    for (float Value : DescendingThrustDanceDamageMultipliers) if (!FMath::IsFinite(Value) || Value < 1.0f) AddError(FText::FromString(TEXT("舞踏倍率必须有限且不小于1。")));

	return bInvalid ? EDataValidationResult::Invalid : (Result == EDataValidationResult::NotValidated ? EDataValidationResult::Valid : Result);
}
#undef LOCTEXT_NAMESPACE
#endif
