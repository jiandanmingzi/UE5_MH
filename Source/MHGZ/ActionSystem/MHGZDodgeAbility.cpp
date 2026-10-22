// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZDodgeAbility.h"

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffectTypes.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/MHGZMotionMatchingAnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "MHGZAbilitySystemComponent.h"
#include "MHGZCharacter.h"
#include "MHGZComboCoordinatorAbility.h"
#include "MHGZ.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"
#include "TimerManager.h"

namespace
{
const FName DodgeEntrySectionName(TEXT("DodgeEntry"));

FGameplayTag Tag(const TCHAR* Name)
{
	return FGameplayTag::RequestGameplayTag(Name);
}

bool GetPlanarYawDelta(const FVector& Forward, const FVector& Direction, float& OutDelta)
{
	if (Forward.ContainsNaN() || Direction.ContainsNaN()
		|| Forward.IsNearlyZero() || Direction.IsNearlyZero()) { return false; }
	const FVector From = Forward.GetSafeNormal2D();
	const FVector To = Direction.GetSafeNormal2D();
	if (From.IsNearlyZero() || To.IsNearlyZero()) { return false; }
	OutDelta = FMath::FindDeltaAngleDegrees(From.Rotation().Yaw, To.Rotation().Yaw);
	return FMath::IsFinite(OutDelta);
}

bool IsLiveDodgeStateValid(const UMHGZAbilitySystemComponent* ASC,
	const UMHGZWeaponRuntimeHostComponent* Host)
{
	return ASC && Host && Host->IsGrounded()
		&& ASC->HasMatchingGameplayTag(Tag(TEXT("Combat.State.Grounded")))
		&& !ASC->HasMatchingGameplayTag(Tag(TEXT("Combat.State.Aerial")))
		&& !ASC->HasMatchingGameplayTag(Tag(TEXT("Combat.State.Dead")))
		&& !ASC->HasMatchingGameplayTag(Tag(TEXT("Combat.State.Hitstun")))
		&& !ASC->HasMatchingGameplayTag(Tag(TEXT("Combat.State.Knockdown")));
}
}

UMHGZDodgeAbility::UMHGZDodgeAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerExecution;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalOnly;
	InputTag = FGameplayTag::RequestGameplayTag(TEXT("Input.Dodge"));
	StaminaCostPolicy = EAbilityStaminaCostPolicy::Instant;
	StaminaCost = FScalableFloat(25.f);
	MaxCorrectionAngle = 90.f;
	AllowedMotionMatchingHandoffTypes.Add(EMHGZMotionMatchingHandoffType::DodgeMoveExit);
}

bool UMHGZDodgeAbility::DoesDodgeDirectionRequireAttacking(EDirectionalInput Direction) const
{
	switch (Direction)
	{
	case EDirectionalInput::None:
	case EDirectionalInput::Forward:
		return bForwardDodgeRequiresAttacking;
	case EDirectionalInput::Left:
		return bLeftDodgeRequiresAttacking;
	case EDirectionalInput::Right:
		return bRightDodgeRequiresAttacking;
	case EDirectionalInput::Back:
		return bBackDodgeRequiresAttacking;
	default:
		return true;
	}
}

EDirectionalInput UMHGZDodgeAbility::ResolveDodgeDirection(const FWeaponInputSnapshot& Input,
	const UAbilitySystemComponent* ASC, const UMHGZWeaponRuntimeHostComponent* Host) const
{
	const EDirectionalInput Requested = Input.Direction;
	if (Requested != EDirectionalInput::Left && Requested != EDirectionalInput::Right) { return Requested; }
	const bool bAttacking = ASC && ASC->HasMatchingGameplayTag(Tag(TEXT("Combat.State.Attacking")));
	const bool bSheathed = Host ? Host->IsSheathed()
		: Input.ContextTags.HasTagExact(Tag(TEXT("Combat.State.Sheathed")));
	if (IsDodgeDirectionAllowedForPose(bSheathed, Requested)
		&& (bAttacking || !DoesDodgeDirectionRequireAttacking(Requested))) { return Requested; }
	if ((!bAttacking && bForwardDodgeRequiresAttacking) || !FMath::IsFinite(MaxCorrectionAngle)) { return Requested; }
	float Delta;
	// Use the press-time basis, including buffered inputs; never wait for locomotion
	// to rotate a side-classified input into the global 45-degree Forward bucket.
	const float Cap = FMath::Clamp(MaxCorrectionAngle, 0.f, 90.f);
	return Cap > 0.f && GetPlanarYawDelta(Input.ActorForward, Input.WorldDirection, Delta)
		&& FMath::Abs(Delta) <= Cap + KINDA_SMALL_NUMBER ? EDirectionalInput::Forward : Requested;
}

bool UMHGZDodgeAbility::CanActivateAbility(
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

	const UMHGZAbilitySystemComponent* ASC = ActorInfo
		? Cast<UMHGZAbilitySystemComponent>(ActorInfo->AbilitySystemComponent.Get())
		: nullptr;
	const UMHGZWeaponRuntimeHostComponent* Host = ASC ? ASC->GetRuntimeHost() : nullptr;
	if (!IsLiveDodgeStateValid(ASC, Host))
	{
		return false;
	}

	if (!ASC->HasMatchingGameplayTag(Tag(TEXT("Combat.State.Attacking"))))
	{
		const FWeaponAbilityActivationContext* Context = ASC->FindPendingActivationContext(Handle);
		return !DoesDodgeDirectionRequireAttacking(
			Context ? ResolveDodgeDirection(Context->Input, ASC, Host) : EDirectionalInput::None);
	}
	const UGA_WeaponComboCoordinator* Coordinator = ASC->GetActiveComboCoordinator();
	return Coordinator && Coordinator->CanDodgeSupersedeActiveAction();
}

bool UMHGZDodgeAbility::ValidateActionDependencies() const
{
	const UMHGZAbilitySystemComponent* ASC = Cast<UMHGZAbilitySystemComponent>(
		GetAbilitySystemComponentFromActorInfo());
	const UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	const FWeaponInputSnapshot& Input = GetWeaponActivationContext().Input;
	const bool bHasPose = Input.ContextTags.HasTagExact(
		Tag(TEXT("Combat.State.Sheathed")))
		|| Input.ContextTags.HasTagExact(Tag(TEXT("Combat.State.Unsheathed")));
	if (!IsLiveDodgeStateValid(ASC, Host)
		|| HasPlayerActionInputLock(ASC)
		|| !Input.ContextTags.HasTagExact(Tag(TEXT("Combat.State.Grounded")))
		|| Input.ContextTags.HasTagExact(Tag(TEXT("Combat.State.Aerial")))
		|| !bHasPose)
	{
		return false;
	}

	// Recheck the live state after consuming the frozen input, before any Commit.
	// Both activation gates use the same per-direction Blueprint defaults.
	if (DoesDodgeDirectionRequireAttacking(ResolveDodgeDirection(Input, ASC, Host))
		&& !ASC->HasMatchingGameplayTag(Tag(TEXT("Combat.State.Attacking"))))
	{
		return false;
	}

	if (ASC->HasMatchingGameplayTag(Tag(TEXT("Combat.State.Attacking"))))
	{
		const UGA_WeaponComboCoordinator* Coordinator = ASC->GetActiveComboCoordinator();
		if (!Coordinator || !Coordinator->CanDodgeSupersedeActiveAction())
		{
			return false;
		}
	}
	return ValidateDodgeMontageDependencies();
}

bool UMHGZDodgeAbility::ValidateDodgeMontageDependencies() const
{
	const ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	const FDodgeSelection Selection = SelectDodgeSelection();
	UAnimMontage* Montage = Selection.Montage;
	return Character && Character->GetMesh() && Character->GetMesh()->GetAnimInstance()
		&& Montage
		&& Montage->IsValidSectionName(DodgeCoreSectionName)
		&& Montage->IsValidSectionName(IdleExitSectionName)
		&& (!Selection.bAllowMoveExit
			|| Montage->IsValidSectionName(MoveExitSectionName));
}

void UMHGZDodgeAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!IsActionActivationCommitted())
	{
		return;
	}

	bEndingDodge = false;
	bActionRecoveryOpen = false;
	bOwnsMontageRootMotion = false;
	bPreparedAttackSupersede = false;
	MovementPhase = EMHGZDodgeMovementPhase::LockedRootMotion;
	ChosenExitSection = NAME_None;
	bActiveDodgeAllowsMoveExit = false;
	DodgingToken = FWeaponOwnedTagToken();
	BlockMovementToken = FWeaponOwnedTagToken();
	DodgeWindowTokens.Reset();
	CachedCollisionResponses.Reset();
	DodgeCapsule.Reset();
	const FDodgeSelection Selection = SelectDodgeSelection();
	ActiveDodgeMontage = Selection.Montage;
	bActiveDodgeAllowsMoveExit = Selection.bAllowMoveExit;

	FGameplayTagContainer DodgingTags;
	DodgingTags.AddTag(Tag(TEXT("Combat.State.Dodging")));
	DodgingToken = AcquireActionTags(DodgingTags, FName(TEXT("DodgeActionLock")));
	if (!DodgingToken.IsValid())
	{
		RequestEndAction(EWeaponActionEndReason::Cancelled);
		return;
	}

	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	// The member keeps its original serialized name for existing GA_Dodge
	// assets, but Phase ownership now applies to every dodge direction. The
	// three-frame static DodgeEntry gives an attacking source action time to be
	// superseded before this Dodge's phase begins and acquires the exact owner.
	const bool bUseLegacyMontageRootMotionOwner =
		!bForwardDodgeUsesActionRootMotionPhase;
	if (!Host || (bUseLegacyMontageRootMotionOwner
		&& !Host->AcquireMontageRootMotion(GetActionToken())))
	{
		RequestEndAction(EWeaponActionEndReason::Cancelled);
		return;
	}
	bOwnsMontageRootMotion = bUseLegacyMontageRootMotionOwner;

	FGameplayTagContainer BlockMovementTags;
	BlockMovementTags.AddTag(Tag(TEXT("Combat.State.BlockMovement")));
	BlockMovementToken = AcquireActionTags(
		BlockMovementTags, FName(TEXT("DodgeLockedMovement")));
	if (!BlockMovementToken.IsValid())
	{
		RequestEndAction(EWeaponActionEndReason::Cancelled);
		return;
	}

	const bool bStartedFromAttack = IsAttacking();
	UMHGZAbilitySystemComponent* ASC = Cast<UMHGZAbilitySystemComponent>(
		GetAbilitySystemComponentFromActorInfo());
	UGA_WeaponComboCoordinator* Coordinator = ASC
		? ASC->GetActiveComboCoordinator()
		: nullptr;
	if (bStartedFromAttack)
	{
		bPreparedAttackSupersede = Coordinator
			&& Coordinator->PrepareActiveActionForDodge(GetActionToken());
		if (!bPreparedAttackSupersede)
		{
			RequestEndAction(EWeaponActionEndReason::Cancelled);
			return;
		}
	}

	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	const FName StartSection = ActiveDodgeMontage
		&& ActiveDodgeMontage->IsValidSectionName(DodgeEntrySectionName)
		? DodgeEntrySectionName
		: DodgeCoreSectionName;
	if (!Character
		|| !StartDodgeMontage(*Character, ActiveDodgeMontage, StartSection)
		|| !ConfigureDodgeCoreFallbackExit())
	{
		CancelPreparedAttackSupersede();
		RequestEndAction(EWeaponActionEndReason::Cancelled);
		return;
	}

	if (bPreparedAttackSupersede)
	{
		if (!Coordinator
			|| !Coordinator->CommitActiveActionDodgeSupersede(GetActionToken()))
		{
			CancelPreparedAttackSupersede();
			RequestEndAction(EWeaponActionEndReason::Interrupted);
			return;
		}
		bPreparedAttackSupersede = false;
	}
	// Correct only a successfully started forward roll, without adding a turn task,
	// timer or input delay. Root movement starts using this yaw on the next pose update.
	float CorrectionDelta;
	const auto& FrozenInput = GetWeaponActivationContext().Input;
	if (bActiveDodgeAllowsMoveExit && FrozenInput.Direction != EDirectionalInput::None
		&& FMath::IsFinite(MaxCorrectionAngle)
		&& GetPlanarYawDelta(Character->GetActorForwardVector(), FrozenInput.WorldDirection, CorrectionDelta))
	{
		FRotator Rotation = Character->GetActorRotation();
		const float CorrectionCap = FMath::Clamp(MaxCorrectionAngle, 0.f, 90.f);
		Rotation.Yaw = FRotator::NormalizeAxis(Rotation.Yaw
			+ FMath::Clamp(CorrectionDelta, -CorrectionCap, CorrectionCap));
		Character->SetActorRotation(Rotation);
	}

	if (UAbilitySystemComponent* CueASC = GetAbilitySystemComponentFromActorInfo())
	{
		FGameplayCueParameters CueParameters;
		CueParameters.Instigator = Character;
		CueParameters.EffectCauser = Character;
		CueParameters.Location = Character->GetActorLocation();
		CueASC->ExecuteGameplayCue(Tag(TEXT("GameplayCue.Character.Dodge")),
			CueParameters);
	}
}

bool UMHGZDodgeAbility::StartDodgeMontage(ACharacter& Character,
	UAnimMontage* Montage, FName StartSection)
{
	UAnimInstance* AnimInstance = Character.GetMesh()
		? Character.GetMesh()->GetAnimInstance()
		: nullptr;
	if (!AnimInstance || !Montage)
	{
		return false;
	}

	MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, FName(TEXT("DodgeMontage")), Montage, 1.f, StartSection);
	if (!MontageTask)
	{
		return false;
	}
	MontageTask->OnCompleted.AddDynamic(this, &UMHGZDodgeAbility::OnMontageCompleted);
	MontageTask->OnBlendOut.AddDynamic(this, &UMHGZDodgeAbility::OnMontageBlendOut);
	MontageTask->OnInterrupted.AddDynamic(this, &UMHGZDodgeAbility::OnMontageInterrupted);
	MontageTask->OnCancelled.AddDynamic(this, &UMHGZDodgeAbility::OnMontageInterrupted);
	MontageTask->ReadyForActivation();

	FAnimMontageInstance* MontageInstance = AnimInstance->GetActiveInstanceForMontage(Montage);
	if (!MontageInstance)
	{
		return false;
	}

	// Dodge has no hold/release endpoint. It must always complete and release its
	// action lock after its final exit section, regardless of this asset's editor flag.
	MontageInstance->bEnableAutoBlendOut = true;
	// Core-to-exit selection belongs to this specific Ability instance. It must
	// not depend on a Notify rediscovering us through a global Montage registry.
	MontageInstance->OnMontageSectionChanged.BindUObject(this,
		&UMHGZDodgeAbility::OnDodgeMontageSectionChanged);
	return RegisterMontageInstance(Character.GetMesh(), MontageInstance->GetInstanceID());
}

void UMHGZDodgeAbility::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	if (bEndingDodge)
	{
		return;
	}
	bEndingDodge = true;
	const FWeaponActionToken EndingToken = GetActionToken();
	UMHGZAbilitySystemComponent* EndingASC = Cast<UMHGZAbilitySystemComponent>(
		GetAbilitySystemComponentFromActorInfo());
	const bool bConsumePreInputAtEnd = !bWasCancelled && IsActionActivationCommitted()
		&& GetActionEndReason() == EWeaponActionEndReason::Normal;
	if (!bConsumePreInputAtEnd && EndingASC)
	{
		if (UGA_WeaponComboCoordinator* Coordinator = EndingASC->GetActiveComboCoordinator())
		{
			Coordinator->DiscardDodgePreInput(EndingToken);
		}
	}
	// A fading outgoing montage must not contribute its remaining root movement
	// after locomotion or a newly committed action takes over. Never edit the asset.
	const bool bForwardActionTakeover = bActiveDodgeAllowsMoveExit
		&& GetActionEndReason() == EWeaponActionEndReason::Superseded;
	if (bActionRecoveryOpen && ActiveDodgeMontage
		&& (!bActiveDodgeAllowsMoveExit || bForwardActionTakeover))
	{
		const ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
		UAnimInstance* AnimInstance = Character && Character->GetMesh()
			? Character->GetMesh()->GetAnimInstance() : nullptr;
		FAnimMontageInstance* Instance = AnimInstance
			? AnimInstance->GetActiveInstanceForMontage(ActiveDodgeMontage) : nullptr;
		if (Instance)
		{
			Instance->PushDisableRootMotion();
			if (bForwardActionTakeover && MontageTask)
			{
				// Retire this task normally before Ability teardown: otherwise its
				// default stop would shorten the explicit fade to the forward asset's
				// zero BlendOut. Normal completion/interrupt paths are unchanged.
				MontageTask->EndTask();
			}
			AnimInstance->Montage_Stop(FMath::Max(0.f, DirectionalIdleExitBlendTime), ActiveDodgeMontage);
		}
	}
	bActionRecoveryOpen = false;
	ClearMontageEndFailsafe();
	CancelPreparedAttackSupersede();
	CloseAllDodgeWindows();
	ReleaseMontageRootMotionOwnership();
	MontageTask = nullptr;
	ActiveDodgeMontage = nullptr;
	ChosenExitSection = NAME_None;
	bActiveDodgeAllowsMoveExit = false;
	BlockMovementToken = FWeaponOwnedTagToken();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility,
		bWasCancelled);
	DodgingToken = FWeaponOwnedTagToken();
	// Normal completion remains a fallback for an input buffered after the exit
	// opened. Cleanup must precede consumption, before the next movement update.
	if (bConsumePreInputAtEnd && EndingASC)
	{
		if (UGA_WeaponComboCoordinator* Coordinator = EndingASC->GetActiveComboCoordinator())
		{
			Coordinator->OnDodgePreInputWindowOpened(EndingToken);
		}
	}
}

UMHGZDodgeAbility::FDodgeSelection UMHGZDodgeAbility::SelectDodgeSelection() const
{
	const FWeaponInputSnapshot& Input = GetWeaponActivationContext().Input;
	const bool bSnapshotSheathed = Input.ContextTags.HasTagExact(
		Tag(TEXT("Combat.State.Sheathed")));
	const bool bSnapshotUnsheathed = Input.ContextTags.HasTagExact(
		Tag(TEXT("Combat.State.Unsheathed")));
	if (!bSnapshotSheathed && !bSnapshotUnsheathed)
	{
		return FDodgeSelection();
	}

	// Direction remains frozen with the input snapshot, but the current Host owns
	// the authoritative weapon pose. This avoids choosing a sheathed montage from
	// a snapshot created across a Draw/Sheathe Commit boundary.
	const UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	const bool bSheathed = Host ? Host->IsSheathed() : bSnapshotSheathed;
	const auto* ASC = GetAbilitySystemComponentFromActorInfo();
	const EDirectionalInput Direction = ResolveDodgeDirection(Input, ASC, Host);
	if (!IsDodgeDirectionAllowedForPose(bSheathed, Direction))
	{
		return FDodgeSelection();
	}

	const bool bForwardRoll = Direction == EDirectionalInput::Forward
		|| Direction == EDirectionalInput::None;
	if (!bForwardRoll)
	{
		const TSoftObjectPtr<UAnimMontage>* DirectionalMontage = nullptr;
		switch (Direction)
		{
		case EDirectionalInput::Left:
			DirectionalMontage = &UnsheathedLeftDodgeMontage;
			break;
		case EDirectionalInput::Right:
			DirectionalMontage = &UnsheathedRightDodgeMontage;
			break;
		case EDirectionalInput::Back:
			DirectionalMontage = &UnsheathedBackDodgeMontage;
			break;
		default:
			break;
		}
		return FDodgeSelection{
			DirectionalMontage && !DirectionalMontage->IsNull()
				? DirectionalMontage->LoadSynchronous()
				: nullptr,
			false};
	}

	const TSoftObjectPtr<UAnimMontage>& DirectMontage = bSheathed
		? SheathedDodgeMontage
		: UnsheathedDodgeMontage;
	if (!DirectMontage.IsNull())
	{
		return FDodgeSelection{DirectMontage.LoadSynchronous(), true};
	}
	return FDodgeSelection{nullptr, true};
}

bool UMHGZDodgeAbility::IsDodgeDirectionAllowedForPose(bool bSheathed,
	EDirectionalInput Direction)
{
	return !bSheathed || Direction == EDirectionalInput::None
		|| Direction == EDirectionalInput::Forward;
}

bool UMHGZDodgeAbility::ConfigureDodgeCoreFallbackExit()
{
	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	UAnimInstance* AnimInstance = Character && Character->GetMesh()
		? Character->GetMesh()->GetAnimInstance()
		: nullptr;
	if (!AnimInstance || !ActiveDodgeMontage
		|| !ActiveDodgeMontage->IsValidSectionName(DodgeCoreSectionName)
		|| !ActiveDodgeMontage->IsValidSectionName(IdleExitSectionName))
	{
		return false;
	}

	// Every variant starts with a deterministic Core -> IdleExit route. The
	// active Ability observes the real section entry and redirects only a
	// forward roll with live raw-stick input to MoveExit. Directional rolls
	// deliberately remain on IdleExit.
	AnimInstance->Montage_SetNextSection(
		DodgeCoreSectionName, IdleExitSectionName, ActiveDodgeMontage);
	return true;
}

bool UMHGZDodgeAbility::IsAttacking() const
{
	const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	return ASC && ASC->HasMatchingGameplayTag(Tag(TEXT("Combat.State.Attacking")));
}

bool UMHGZDodgeAbility::HasLiveMovementInput() const
{
	const AMHGZCharacter* Character = Cast<AMHGZCharacter>(
		GetAvatarActorFromActorInfo());

	// DodgeCore owns BlockMovement, which intentionally clears bHasInput for
	// locomotion. The exit decision instead reads the same raw stick/deadzone
	// signal that was preserved for action input while that lock is active.
	return Character && Character->HasRawMovementInput();
}

bool UMHGZDodgeAbility::JumpToDodgeSection(FName SectionName)
{
	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	UAnimInstance* AnimInstance = Character && Character->GetMesh()
		? Character->GetMesh()->GetAnimInstance()
		: nullptr;
	if (!AnimInstance || !ActiveDodgeMontage
		|| !ActiveDodgeMontage->IsValidSectionName(SectionName))
	{
		return false;
	}

	// If called while Core is active, select its following section. At the actual
	// Core -> IdleExit boundary, the SectionChanged callback calls this method
	// again to redirect a live-input forward roll into MoveExit.
	if (AnimInstance->Montage_GetCurrentSection(ActiveDodgeMontage)
		== DodgeCoreSectionName)
	{
		AnimInstance->Montage_SetNextSection(
			DodgeCoreSectionName, SectionName, ActiveDodgeMontage);
	}
	else
	{
		AnimInstance->Montage_JumpToSection(SectionName, ActiveDodgeMontage);
	}
	return true;
}

bool UMHGZDodgeAbility::DecideDodgeExit(
	const FWeaponActionToken& ActionToken)
{
	if (!IsActive() || bEndingDodge || !IsActionActivationCommitted()
		|| ActionToken != GetActionToken()
		|| MovementPhase != EMHGZDodgeMovementPhase::LockedRootMotion)
	{
		return false;
	}
	const AMHGZCharacter* Character = Cast<AMHGZCharacter>(
		GetAvatarActorFromActorInfo());
	const bool bHasLiveInput = HasLiveMovementInput();
	const FName TargetSection = bActiveDodgeAllowsMoveExit && bHasLiveInput
		? MoveExitSectionName
		: IdleExitSectionName;
	if (!ChosenExitSection.IsNone())
	{
		return ChosenExitSection == TargetSection;
	}
	if (!JumpToDodgeSection(TargetSection))
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[DodgeExitSelection] Failed to select %s (AllowsMoveExit=%d, RawMove=(%.3f, %.3f))."),
			*TargetSection.ToString(), bActiveDodgeAllowsMoveExit ? 1 : 0,
			Character ? Character->GetRawMoveInput().X : 0.f,
			Character ? Character->GetRawMoveInput().Y : 0.f);
		return false;
	}
	ChosenExitSection = TargetSection;
	UE_LOG(LogMHGZ, Log,
		TEXT("[DodgeExitSelection] Selected %s (AllowsMoveExit=%d, HasLiveInput=%d, RawMove=(%.3f, %.3f))."),
		*TargetSection.ToString(), bActiveDodgeAllowsMoveExit ? 1 : 0,
		bHasLiveInput ? 1 : 0,
		Character ? Character->GetRawMoveInput().X : 0.f,
		Character ? Character->GetRawMoveInput().Y : 0.f);
	return true;
}

void UMHGZDodgeAbility::OnDodgeMontageSectionChanged(UAnimMontage* Montage,
	FName SectionName, bool bLooped)
{
	(void)bLooped;
	if (!IsActive() || bEndingDodge || Montage != ActiveDodgeMontage)
	{
		return;
	}
	HandleDodgeSectionEntered(SectionName);
}

void UMHGZDodgeAbility::HandleDodgeSectionEntered(FName SectionName)
{
	if (!IsActive() || bEndingDodge || !IsActionActivationCommitted())
	{
		return;
	}

	if (SectionName == IdleExitSectionName)
	{
		// Core -> Idle is only the deterministic engine fallback. Select from the
		// raw stick at the actual section boundary, not from the activation snapshot.
		if (bActiveDodgeAllowsMoveExit && HasLiveMovementInput())
		{
			if (JumpToDodgeSection(MoveExitSectionName))
			{
				ChosenExitSection = MoveExitSectionName;
			}
			return;
		}
		ChosenExitSection = IdleExitSectionName;
		OpenActionRecovery();
		// A committed successor may have synchronously ended this source.
		if (!IsActive() || bEndingDodge) { return; }
		TryCompleteDirectionalRecoveryForMovement();
		return;
	}

	if (SectionName == MoveExitSectionName)
	{
		ChosenExitSection = MoveExitSectionName;
		if (EnterMoveExit(GetActionToken()))
		{
			OpenActionRecovery();
		}
	}
}

void UMHGZDodgeAbility::OpenActionRecovery()
{
	if (bActionRecoveryOpen) { return; }
	// Without a committed successor the original exit, tags and root phase
	// continue. In particular forward movement must not use directional Start.
	bActionRecoveryOpen = true;
	ReleaseActionTag(DodgingToken);
	if (auto* ASC = Cast<UMHGZAbilitySystemComponent>(GetAbilitySystemComponentFromActorInfo()))
	{
		if (UGA_WeaponComboCoordinator* Coordinator = ASC->GetActiveComboCoordinator())
		{
			Coordinator->OnDodgePreInputWindowOpened(GetActionToken());
		}
	}
}

bool UMHGZDodgeAbility::IsActionRecoveryOpen() const
{
	const UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	return bActionRecoveryOpen
		&& !bEndingDodge && IsActive() && IsActionActivationCommitted()
		&& (ChosenExitSection == IdleExitSectionName
			|| (bActiveDodgeAllowsMoveExit && ChosenExitSection == MoveExitSectionName)) && Host
		&& Host->IsActionRegistered(GetActionToken());
}

bool UMHGZDodgeAbility::IsDirectionalRecoveryOpen() const
{
	return !bActiveDodgeAllowsMoveExit && IsActionRecoveryOpen();
}

UMHGZDodgeAbility* UMHGZDodgeAbility::FindOpenActionRecovery(
	UAbilitySystemComponent* ASC, const UMHGZGameplayAbility* Exclude)
{
	if (!ASC) { return nullptr; }
	for (FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		for (UGameplayAbility* Instance : Spec.GetAbilityInstances())
		{
			UMHGZDodgeAbility* Dodge = Cast<UMHGZDodgeAbility>(Instance);
			if (Dodge && Dodge != Exclude && Dodge->IsActionRecoveryOpen()) { return Dodge; }
		}
	}
	return nullptr;
}

UMHGZDodgeAbility* UMHGZDodgeAbility::FindOpenDirectionalRecovery(
	UAbilitySystemComponent* ASC, const UMHGZGameplayAbility* Exclude)
{
	if (!ASC) { return nullptr; }
	for (FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		for (UGameplayAbility* Instance : Spec.GetAbilityInstances())
		{
			UMHGZDodgeAbility* Dodge = Cast<UMHGZDodgeAbility>(Instance);
			if (Dodge && Dodge != Exclude && Dodge->IsDirectionalRecoveryOpen()) { return Dodge; }
		}
	}
	return nullptr;
}

bool UMHGZDodgeAbility::TryCompleteDirectionalRecoveryForMovement()
{
	if (!IsDirectionalRecoveryOpen() || !HasLiveMovementInput()) { return false; }
	const ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (UMHGZMotionMatchingAnimInstance* Anim = Character && Character->GetMesh()
		? Cast<UMHGZMotionMatchingAnimInstance>(Character->GetMesh()->GetAnimInstance()) : nullptr)
	{
		// Snapshot the exact source before EndAbility unregisters it. Only movement,
		// never the attack/forward-roll path, requests this fresh Start query.
		Anim->RequestDirectionalDodgeLocomotionStart(GetActionToken());
	}
	MovementPhase = EMHGZDodgeMovementPhase::MotionMatching;
	RequestEndAction(EWeaponActionEndReason::Normal);
	return true;
}

void UMHGZDodgeAbility::CompleteDirectionalRecoveryForAction()
{
	// Keep this existing C++ entry point; action recovery now includes forward exits.
	if (IsActionRecoveryOpen()) { RequestEndAction(EWeaponActionEndReason::Superseded); }
}

bool UMHGZDodgeAbility::EnterMoveExit(
	const FWeaponActionToken& ActionToken)
{
	if (!IsActive() || bEndingDodge || !IsActionActivationCommitted()
		|| ActionToken != GetActionToken()
		|| MovementPhase != EMHGZDodgeMovementPhase::LockedRootMotion
		|| !bActiveDodgeAllowsMoveExit
		|| ChosenExitSection != MoveExitSectionName)
	{
		return false;
	}
	if (!ReleaseActionTag(BlockMovementToken))
	{
		return false;
	}
	MovementPhase = EMHGZDodgeMovementPhase::SteeringRootMotion;
	return true;
}

void UMHGZDodgeAbility::ReleaseMontageRootMotionOwnership()
{
	if (!bOwnsMontageRootMotion)
	{
		return;
	}
	bOwnsMontageRootMotion = false;
	if (UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost())
	{
		Host->ReleaseMontageRootMotion(GetActionToken());
	}
}

void UMHGZDodgeAbility::CancelPreparedAttackSupersede()
{
	if (!bPreparedAttackSupersede)
	{
		return;
	}
	bPreparedAttackSupersede = false;
	if (UMHGZAbilitySystemComponent* ASC = Cast<UMHGZAbilitySystemComponent>(
		GetAbilitySystemComponentFromActorInfo()))
	{
		if (UGA_WeaponComboCoordinator* Coordinator = ASC->GetActiveComboCoordinator())
		{
			Coordinator->CancelActiveActionDodgeSupersede(GetActionToken());
		}
	}
}

bool UMHGZDodgeAbility::BeginDodgeWindow(FName NotifyEventID)
{
	if (!IsActionActivationCommitted() || NotifyEventID.IsNone()
		|| DodgeWindowTokens.Contains(NotifyEventID))
	{
		return false;
	}
	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	const FWeaponActionToken& Token = GetActionToken();
	if (!Capsule || !Host || !Token.IsValid()) return false;

	if (DodgeWindowTokens.IsEmpty())
	{
		DodgeCapsule = Capsule;
		for (const ECollisionChannel Channel : { ECC_GameTraceChannel1, ECC_GameTraceChannel2 })
		{
			CachedCollisionResponses.Add(Channel, Capsule->GetCollisionResponseToChannel(Channel));
			Capsule->SetCollisionResponseToChannel(Channel, ECR_Ignore);
		}
	}

	FGameplayTagContainer Tags;
	Tags.AddTag(Tag(TEXT("Combat.State.Invincible")));
	FWeaponOwnedTagToken WindowToken = Host->AcquireTags(EWeaponTagOwnerKind::NotifyWindow,
		Token.AbilityHandle, Token.ActivationSequenceID, NotifyEventID, Tags);
	if (!WindowToken.IsValid())
	{
		if (DodgeWindowTokens.IsEmpty()) RestoreDodgeCollisionResponses();
		return false;
	}
	DodgeWindowTokens.Add(NotifyEventID, WindowToken);
	return true;
}

void UMHGZDodgeAbility::EndDodgeWindow(FName NotifyEventID)
{
	FWeaponOwnedTagToken* WindowToken = DodgeWindowTokens.Find(NotifyEventID);
	if (!WindowToken) return;
	if (UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost())
	{
		Host->ReleaseTags(*WindowToken);
	}
	DodgeWindowTokens.Remove(NotifyEventID);
	if (DodgeWindowTokens.IsEmpty()) RestoreDodgeCollisionResponses();
}

void UMHGZDodgeAbility::CloseAllDodgeWindows()
{
	if (UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost())
	{
		for (TPair<FName, FWeaponOwnedTagToken>& Pair : DodgeWindowTokens)
		{
			Host->ReleaseTags(Pair.Value);
		}
	}
	DodgeWindowTokens.Reset();
	RestoreDodgeCollisionResponses();
}

void UMHGZDodgeAbility::RestoreDodgeCollisionResponses()
{
	if (UCapsuleComponent* Capsule = DodgeCapsule.Get())
	{
		for (const TPair<TEnumAsByte<ECollisionChannel>, ECollisionResponse>& Pair
			: CachedCollisionResponses)
		{
			Capsule->SetCollisionResponseToChannel(Pair.Key, Pair.Value);
		}
	}
	CachedCollisionResponses.Reset();
	DodgeCapsule.Reset();
}

void UMHGZDodgeAbility::OnMontageCompleted()
{
	if (IsActive() && !bEndingDodge)
	{
		ClearMontageEndFailsafe();
		ReleaseMontageRootMotionOwnership();
		RequestEndAction(EWeaponActionEndReason::Normal);
	}
}

void UMHGZDodgeAbility::OnMontageBlendOut()
{
	if (!IsActive() || bEndingDodge)
	{
		return;
	}
	ReleaseMontageRootMotionOwnership();
	MovementPhase = EMHGZDodgeMovementPhase::MotionMatching;
	ArmMontageEndFailsafe();
}

void UMHGZDodgeAbility::OnMontageInterrupted()
{
	if (IsActive() && !bEndingDodge)
	{
		ClearMontageEndFailsafe();
		CancelPreparedAttackSupersede();
		RequestEndAction(EWeaponActionEndReason::Interrupted);
	}
}

void UMHGZDodgeAbility::ArmMontageEndFailsafe()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// A normal blend-out should always be followed by OnCompleted. Some montage
	// assets nevertheless fail to emit it at runtime; do not leave Dodging and
	// its stamina/input lock behind indefinitely in that case.
	const float BlendTime = ActiveDodgeMontage
		? ActiveDodgeMontage->BlendOut.GetBlendTime()
		: 0.f;
	World->GetTimerManager().SetTimer(MontageEndFailsafeTimer, this,
		&UMHGZDodgeAbility::OnMontageEndFailsafeExpired,
		FMath::Max(BlendTime + 0.05f, 0.1f), false);
}

void UMHGZDodgeAbility::ClearMontageEndFailsafe()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(MontageEndFailsafeTimer);
	}
}

void UMHGZDodgeAbility::OnMontageEndFailsafeExpired()
{
	if (IsActive() && !bEndingDodge)
	{
		RequestEndAction(EWeaponActionEndReason::Normal);
	}
}
