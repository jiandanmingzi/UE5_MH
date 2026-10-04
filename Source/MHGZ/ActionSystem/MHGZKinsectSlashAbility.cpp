// Copyright MHGZ Project. All Rights Reserved.

#include "ActionSystem/MHGZKinsectSlashAbility.h"

#include "ActionSystem/AbilityTask_MHGZKinsectSlashProbe.h"
#include "ActionSystem/AbilityTask_MHGZPlayMontageAndWait.h"
#include "ActionSystem/AbilityTask_MHGZWeaponMovement.h"
#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "ActionSystem/MHGZComboCoordinatorAbility.h"
#include "ActionSystem/MHGZGameplayEffectContext.h"
#include "ActionSystem/MHGZWeaponMovementCurves.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "AttributeSystem/Res_InsectGlaive.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"
#include "InsectGlaive/InsectGlaiveCombatConfig.h"
#include "InsectGlaive/Kinsect/Kinsect.h"
#include "MHGZ.h"
#include "MHGZCharacter.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"
#include "UI/MHGZAimComponent.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

namespace
{
const FGameplayTag& AerialFallingTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Aerial.Falling"));
	return Tag;
}

const FGameplayTag& AerialCantAttackTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Aerial.CantAttack"));
	return Tag;
}

const FGameplayTag& AerialCantDodgeTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Aerial.CantDodge"));
	return Tag;
}

const FGameplayTag& KinsectSlashMovementSourceTag()
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

const FGameplayTag& UnsheathedTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Unsheathed"));
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

const FGameplayTag& WeaponBTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Input.Weapon.B"));
	return Tag;
}

const FGameplayTag& WeaponRTBTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Input.Weapon.RTB"));
	return Tag;
}

bool IsFiniteVector(const FVector& Value)
{
	return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y)
		&& FMath::IsFinite(Value.Z);
}

}

bool FKinsectSlashDanceEntryTuning::IsValid() const
{
	if (!FMath::IsFinite(PrepDuration) || PrepDuration <= 0.0f)
	{
		return false;
	}
	if (bUseCapturedTrajectory)
	{
		if (!FMath::IsFinite(FlightDuration) || FlightDuration <= 0.0f
			|| !FMath::IsFinite(LaunchUpSpeed) || LaunchUpSpeed <= 0.0f
			|| !FMath::IsFinite(LaunchBackwardSpeed) || LaunchBackwardSpeed <= 0.0f
			|| !FMath::IsFinite(GravityMagnitude) || GravityMagnitude <= 0.0f
			|| !FMath::IsFinite(BackwardAcceleration) || BackwardAcceleration <= 0.0f)
		{
			return false;
		}
		const double Time = FlightDuration;
		const float EndHeight = static_cast<float>(LaunchUpSpeed * Time
			- 0.5 * GravityMagnitude * Time * Time);
		const float Distance = static_cast<float>(LaunchBackwardSpeed * Time
			+ 0.5 * BackwardAcceleration * Time * Time);
		const float EndUpSpeed = static_cast<float>(LaunchUpSpeed - GravityMagnitude * Time);
		const float EndBackwardSpeed = static_cast<float>(LaunchBackwardSpeed
			+ BackwardAcceleration * Time);
		return FMath::IsFinite(EndHeight) && FMath::IsFinite(Distance) && Distance > 0.0f
			&& FMath::IsFinite(EndUpSpeed) && EndUpSpeed < 0.0f
			&& FlightDuration > LaunchUpSpeed / GravityMagnitude
			&& FMath::IsFinite(EndBackwardSpeed) && EndBackwardSpeed > 0.0f;
	}

	if (ParameterMode == EBallisticParameterMode::ApexHeightAndDuration)
	{
		return FMath::IsFinite(ApexHeight) && ApexHeight > 0.0f
			&& FMath::IsFinite(Duration) && Duration > 0.0f
			&& FMath::IsFinite(HorizontalDistance) && HorizontalDistance > 0.0f
			&& IsFiniteVector(ExplicitLaunchVelocity)
			&& ExplicitLaunchVelocity.IsNearlyZero();
	}

	return ParameterMode == EBallisticParameterMode::ExplicitLaunchVelocity
		&& IsFiniteVector(ExplicitLaunchVelocity)
		&& ExplicitLaunchVelocity.Z > KINDA_SMALL_NUMBER
		&& FMath::IsFinite(ExplicitLaunchVelocity.X)
		&& FMath::IsFinite(ExplicitLaunchVelocity.Y);
}

FWeaponMovementRequest FKinsectSlashDanceEntryTuning::MakeRequest(
	const FWeaponActionToken& ActionToken, const FVector& InLaunchDirection,
	UObject* CurveOuter) const
{
	(void)CurveOuter;
	FWeaponMovementRequest Request;
	Request.OwnerAction = ActionToken;
	Request.Mode = bUseCapturedTrajectory ? EWeaponMovementMode::AuthoredAerialPhysics
		: EWeaponMovementMode::BallisticVault;
	Request.BallisticMode = ParameterMode;
	Request.DirectionSnapshot = FVector(InLaunchDirection.X, InLaunchDirection.Y, 0.0f)
		.GetSafeNormal();
	Request.RotationPolicy = EActionRotationPolicy::Locked;
	Request.CancelVelocityPolicy = EMovementCancelVelocityPolicy::PreserveVelocity;
	if (!IsValid() || !IsFiniteVector(InLaunchDirection)
		|| Request.DirectionSnapshot.IsNearlyZero())
	{
		return Request;
	}

	if (bUseCapturedTrajectory)
	{
		// The CMC owns this recorded physical arc. Local X points backward while
		// the actor keeps its locked attack yaw.
		Request.DirectionSnapshot *= -1.0f;
		Request.Duration = FlightDuration;
		Request.LaunchVelocity = Request.DirectionSnapshot * LaunchBackwardSpeed
			+ FVector::UpVector * LaunchUpSpeed;
		Request.PlanarAcceleration = Request.DirectionSnapshot * BackwardAcceleration;
		Request.GravityMagnitude = GravityMagnitude;
		return Request;
	}

	if (ParameterMode == EBallisticParameterMode::ApexHeightAndDuration)
	{
		Request.ApexHeight = ApexHeight;
		Request.Duration = Duration;
		Request.MaxDistance = HorizontalDistance;
		Request.LaunchVelocity = FVector::ZeroVector;
	}
	else
	{
		const FRotator EntryYaw = Request.DirectionSnapshot.Rotation();
		Request.LaunchVelocity = EntryYaw.RotateVector(ExplicitLaunchVelocity);
		Request.ApexHeight = 0.0f;
		Request.Duration = 0.0f;
		Request.MaxDistance = 0.0f;
	}
	return Request;
}

UMHGZKinsectSlashAbility::UMHGZKinsectSlashAbility()
{
	InputTag = WeaponBTag();
	DefaultEntrySection = HoverSectionName;

	ActivationRequiredTags.AddTag(AerialFallingTag());
	ActivationRequiredTags.AddTag(UnsheathedTag());
	ActivationBlockedTags.AddTag(AerialCantAttackTag());
	ActivationBlockedTags.AddTag(DeadTag());
	ActivationBlockedTags.AddTag(HitstunTag());
	ActivationBlockedTags.AddTag(KnockdownTag());
}

bool UMHGZKinsectSlashAbility::CanActivateAbility(
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
	return IsLiveAerialSlashStateValid(ASC, Host);
}

bool UMHGZKinsectSlashAbility::IsLiveAerialSlashStateValid(
	const UAbilitySystemComponent* ASC,
	const UMHGZWeaponRuntimeHostComponent* Host) const
{
	return ASC && Host && !Host->IsGrounded() && ASC->HasMatchingGameplayTag(AerialFallingTag())
		&& ASC->HasMatchingGameplayTag(UnsheathedTag())
		&& !ASC->HasMatchingGameplayTag(AerialCantAttackTag())
		&& !ASC->HasMatchingGameplayTag(DeadTag())
		&& !ASC->HasMatchingGameplayTag(HitstunTag())
		&& !ASC->HasMatchingGameplayTag(KnockdownTag());
}

bool UMHGZKinsectSlashAbility::ValidateActionDependencies() const
{
	if (!Super::ValidateActionDependencies())
	{
		return false;
	}

	const FWeaponInputSnapshot& Input = GetWeaponActivationContext().Input;
	if (!Input.ResolvedInputTag.MatchesTagExact(WeaponBTag())
		&& !Input.ResolvedInputTag.MatchesTagExact(WeaponRTBTag()))
	{
		return false;
	}

	FName SelectedEntry;
	if (!SelectAttackMontageStartSection(SelectedEntry)
		|| SelectedEntry != HoverSectionName
		|| !HasRequiredSections(AttackMontage))
	{
		return false;
	}

	const UMHGZAbilitySystemComponent* ASC = Cast<UMHGZAbilitySystemComponent>(
		GetAbilitySystemComponentFromActorInfo());
	const UMHGZWeaponRuntimeHostComponent* Host = ASC ? ASC->GetRuntimeHost() : nullptr;
	const URes_InsectGlaive* Resource = GetIGResourceComponent();
	const UInsectGlaiveCombatConfig* Config = Resource
		? Resource->GetCombatConfig() : nullptr;
	const ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	const AKinsect* Kinsect = Resource ? Resource->GetKinsectActor() : nullptr;
	if (!IsLiveAerialSlashStateValid(ASC, Host) || !Resource || !Config
		|| !Character || !Character->GetMesh() || !Character->GetMesh()->GetAnimInstance()
		|| !Kinsect || !Kinsect->Mesh || !Kinsect->Mesh->GetSkeletalMeshAsset()
		|| (Config->bUseCapturedKinsectSlashDash ? !Config->KinsectSlashDashProfile.IsValid()
			: (!Config->KinsectSlashDistanceCurve || Config->KinsectSlashMaxDistance <= 0.0f
				|| Config->KinsectSlashDashDuration <= 0.0f))
		|| Config->KinsectSlashAttachSocket.IsNone()
		|| !HasRequiredDanceMontage()
		|| !DanceFromDashTuning.IsValid() || !DanceFromMissTuning.IsValid()
		|| !FMath::IsFinite(MissGravityMagnitude) || MissGravityMagnitude <= 0.0f
		|| AttackSegments.Num() != 3
		|| !AttackSegments[0].Damage.DamageEffectClass
		|| AttackSegments[0].Damage.MotionValue.GetValueAtLevel(1.0f) <= 0.0f
		|| AttackSegments[0].Damage.bRequiresHitToContinue
		|| AttackSegments[0].MultiHitCount != 1
		|| AttackSegments[0].MultiHitPolicy != EAttackMultiHitPolicy::ContactOnly
		|| AttackSegments[0].Collision.TraceRegions.Num() != 1
		|| !AttackSegments[0].Collision.TraceRegions[0].StartSocketName.IsNone()
		|| AttackSegments[0].Collision.TraceRegions[0].EndSocketName
			!= Config->KinsectSlashAttachSocket
		|| !FindKinsectSlashWeaponMesh(Config->KinsectSlashAttachSocket))
	{
		return false;
	}
	for (int32 SegmentIndex = 1; SegmentIndex < 3; ++SegmentIndex)
	{
		const FAttackSegmentConfig& Segment = AttackSegments[SegmentIndex];
		if (!Segment.Damage.DamageEffectClass
			|| Segment.Damage.MotionValue.GetValueAtLevel(1.0f) <= 0.0f
			|| Segment.Damage.bRequiresHitToContinue
			|| Segment.MultiHitCount != 1
			|| Segment.MultiHitPolicy != EAttackMultiHitPolicy::ContactOnly
			|| Segment.Collision.TraceRegions.IsEmpty())
		{
			return false;
		}
	}

	float TerminalSlope = 0.0f;
	return Config->bUseCapturedKinsectSlashDash || MHGZWeaponMovementCurves::ValidateNormalizedProgressCurve(
		Config->KinsectSlashDistanceCurve, TerminalSlope);
}

FGameplayEffectSpecHandle UMHGZKinsectSlashAbility::MakeDamageSpec(
	const FHitResult& Hit, const int32 SegmentIndex)
{
	FGameplayEffectSpecHandle Spec = Super::MakeDamageSpec(Hit, SegmentIndex);
	if (SegmentIndex != 0 || !Spec.IsValid())
	{
		return Spec;
	}

	const URes_InsectGlaive* Resource = GetIGResourceComponent();
	if (!Resource)
	{
		return FGameplayEffectSpecHandle();
	}

	FGameplayEffectContextHandle ContextHandle = Spec.Data->GetContext();
	FMHGZGameplayEffectContext* Context =
		FMHGZGameplayEffectContext::ExtractEffectContext(ContextHandle);
	if (!Context)
	{
		return FGameplayEffectSpecHandle();
	}
	Context->DamageSourceType = EMHGZDamageSourceType::Kinsect;
	if (!Context->HitCueTag.IsValid())
	{
		Context->HitCueTag = FGameplayTag::RequestGameplayTag(
			TEXT("GameplayCue.Hit.Kinsect"));
	}
	Spec.Data->SetSetByCallerMagnitude(
		FGameplayTag::RequestGameplayTag(TEXT("Damage.AttackPower")),
		Resource->GetModifiedKinsectAttackPower());
	return Spec;
}

bool UMHGZKinsectSlashAbility::PrepareAttackMontage()
{
	FName SelectedEntry;
	if (!SelectAttackMontageStartSection(SelectedEntry)
		|| SelectedEntry != HoverSectionName
		|| !HasRequiredSections(AttackMontage))
	{
		return false;
	}

	URes_InsectGlaive* Resource = GetIGResourceComponent();
	const UInsectGlaiveCombatConfig* Config = Resource
		? Resource->GetCombatConfig() : nullptr;
	USkeletalMeshComponent* WeaponMesh = Config
		? FindKinsectSlashWeaponMesh(Config->KinsectSlashAttachSocket) : nullptr;
	if (!Resource || !Config || !WeaponMesh
		|| !Resource->BeginKinsectActionAttachment(GetActionToken(), WeaponMesh,
			Config->KinsectSlashAttachSocket))
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[KinsectSlash] Could not attach the real Kinsect to the configured weapon socket before montage playback."));
		return false;
	}
	bKinsectActionAttached = true;
	return true;
}

bool UMHGZKinsectSlashAbility::HasRequiredSections(const UAnimMontage* Montage) const
{
	if (!Montage || HoverSectionName.IsNone() || DashSectionName.IsNone()
		|| MissFallSectionName.IsNone())
	{
		return false;
	}

	const FName Sections[] = { HoverSectionName, DashSectionName, MissFallSectionName };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Sections); ++Index)
	{
		if (!Montage->IsValidSectionName(Sections[Index]))
		{
			return false;
		}
		for (int32 PreviousIndex = 0; PreviousIndex < Index; ++PreviousIndex)
		{
			if (Sections[Index] == Sections[PreviousIndex])
			{
				return false;
			}
		}
	}
	return DefaultEntrySection == HoverSectionName;
}

bool UMHGZKinsectSlashAbility::HasRequiredDanceMontage() const
{
	if (!DanceMontage || DanceSectionName.IsNone()
		|| !DanceMontage->IsValidSectionName(DanceSectionName)
		|| DanceMontage->CompositeSections.Num() != 1
		|| !FMath::IsFinite(DanceMontageBlendInTime)
		|| DanceMontageBlendInTime < 0.0f)
	{
		return false;
	}
	return true;
}

USkeletalMeshComponent* UMHGZKinsectSlashAbility::FindKinsectSlashWeaponMesh(
	const FName RequiredSocket) const
{
	if (RequiredSocket.IsNone())
	{
		return nullptr;
	}
	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!Character)
	{
		return nullptr;
	}

	const FName WeaponTag = AttackSegments.IsValidIndex(0)
		? AttackSegments[0].Collision.TraceMeshComponentTag : NAME_None;
	TArray<USkeletalMeshComponent*> MeshComponents;
	Character->GetComponents<USkeletalMeshComponent>(MeshComponents);
	for (USkeletalMeshComponent* Mesh : MeshComponents)
	{
		if (Mesh && (WeaponTag.IsNone() || Mesh->ComponentHasTag(WeaponTag))
			&& Mesh->DoesSocketExist(RequiredSocket))
		{
			return Mesh;
		}
	}
	return nullptr;
}

bool UMHGZKinsectSlashAbility::IsKinsectActionAttachmentCurrent() const
{
	const URes_InsectGlaive* Resource = GetIGResourceComponent();
	return bKinsectActionAttached && Resource
		&& Resource->IsKinsectActionAttachmentCurrent(GetActionToken());
}

void UMHGZKinsectSlashAbility::ReleaseKinsectActionAttachment()
{
	if (!bKinsectActionAttached)
	{
		return;
	}
	if (URes_InsectGlaive* Resource = GetIGResourceComponent())
	{
		if (!Resource->EndKinsectActionAttachmentAndReturn(GetActionToken()))
		{
			UE_LOG(LogMHGZ, Warning,
				TEXT("[KinsectSlash] Temporary Kinsect attachment could not be released by its owning action token."));
		}
	}
	bKinsectActionAttached = false;
}

bool UMHGZKinsectSlashAbility::CanApplyCollisionHit(
	const FHitResult& Hit, const int32 SegmentIndex) const
{
	if (SegmentIndex != 0)
	{
		return true;
	}
	if (bSuccessfulDamage || !IsCurrentAction(SlashActionToken)
		|| !IsKinsectActionAttachmentCurrent()
		|| (Phase != EKinsectSlashPhase::Dash
			&& Phase != EKinsectSlashPhase::MissFall)
		|| !IsCurrentMontage(SlashActionToken, SlashMontageInstanceID))
	{
		return false;
	}

	const ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	const UAnimInstance* AnimInstance = Character && Character->GetMesh()
		? Character->GetMesh()->GetAnimInstance() : nullptr;
	const FName Section = AnimInstance && ActiveSlashMontage
		? AnimInstance->Montage_GetCurrentSection(ActiveSlashMontage) : NAME_None;
	return Section == DashSectionName || Section == MissFallSectionName;
}

bool UMHGZKinsectSlashAbility::ShouldConsumeCollisionOnDamageFailure(
	const int32 SegmentIndex) const
{
	// A rejected/invalid GE must not spend the kinsect's sole contact attempt.
	return SegmentIndex != 0;
}

void UMHGZKinsectSlashAbility::OnAttackCollisionWindowClosed(
	const int32 SegmentIndex)
{
	if (SegmentIndex != 0 || bSuccessfulDamage || bEndingSlash)
	{
		return;
	}

	const ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	const UAnimInstance* AnimInstance = Character && Character->GetMesh()
		? Character->GetMesh()->GetAnimInstance() : nullptr;
	const FName Section = AnimInstance && ActiveSlashMontage
		? AnimInstance->Montage_GetCurrentSection(ActiveSlashMontage) : NAME_None;
	if (Phase == EKinsectSlashPhase::MissFall || Section == MissFallSectionName)
	{
		// The single [0] AttackCollision notify-state ends at Miss188 source frame 14.
		ReleaseActionVisualPitch(EActionVisualPitchReleaseMode::BlendOut);
		ReleaseKinsectActionAttachment();
	}
}

bool UMHGZKinsectSlashAbility::StartAttackMontage(ACharacter& Character,
	UAnimMontage* Montage, const FName StartSection)
{
	USkeletalMeshComponent* Mesh = Character.GetMesh();
	UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (!AnimInstance || !Montage || !HasRequiredSections(Montage)
		|| StartSection != HoverSectionName)
	{
		return false;
	}

	ActiveSlashMontage = Montage;
	ActiveAttackMontage = Montage;
	MontageActionToken = GetActionToken();
	SlashMontageTask = UAbilityTask_MHGZPlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, TEXT("KinsectSlashMontage"), Montage, 1.0f, StartSection,
		ResolveAttackMontageBlendInTime(GetWeaponActivationContext()), false);
	if (!SlashMontageTask)
	{
		return false;
	}

	SlashMontageTask->OnCompleted.AddDynamic(this,
		&UMHGZKinsectSlashAbility::HandleSlashMontageCompleted);
	SlashMontageTask->OnInterrupted.AddDynamic(this,
		&UMHGZKinsectSlashAbility::HandleSlashMontageInterrupted);
	SlashMontageTask->OnCancelled.AddDynamic(this,
		&UMHGZKinsectSlashAbility::HandleSlashMontageInterrupted);
	SlashMontageTask->ReportCompletionAtMontageEnd();
	SlashMontageTask->ReadyForActivation();

	FAnimMontageInstance* MontageInstance = AnimInstance->GetActiveInstanceForMontage(Montage);
	if (!MontageInstance)
	{
		return false;
	}
	SlashMontageInstanceID = MontageInstance->GetInstanceID();
	MontageInstance->bEnableAutoBlendOut = false;
	MontageInstance->PushDisableRootMotion();
	MontageInstance->OnMontageSectionChanged.BindUObject(this,
		&UMHGZKinsectSlashAbility::OnSlashMontageSectionChanged);
	if (!RegisterMontageInstance(Mesh, SlashMontageInstanceID))
	{
		MontageInstance->OnMontageSectionChanged.Unbind();
		return false;
	}
	return true;
}

void UMHGZKinsectSlashAbility::ApplyActivationDirectionCorrection()
{
	// The aim direction is sampled at the authored 186 lock frame, not from the
	// input snapshot during Ability activation.
}

void UMHGZKinsectSlashAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	ResetActivationState();
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!IsActionActivationCommitted() || !IsActive())
	{
		return;
	}

	SlashActionToken = GetActionToken();
	const FGameplayTag ResolvedInput = GetWeaponActivationContext().Input.ResolvedInputTag;
	AimSource = ResolvedInput.MatchesTagExact(WeaponBTag())
		? EKinsectSlashAimSource::WeaponB : EKinsectSlashAimSource::KinsectAim;
	Phase = EKinsectSlashPhase::Hover;
	BindLifecycleGuards();
	if (!IsActive())
	{
		return;
	}

	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	AMHGZCharacter* Character = Cast<AMHGZCharacter>(GetAvatarActorFromActorInfo());
	UCharacterMovementComponent* CMC = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Host || !Character || !CMC || !Host->MarkAerialAttackUsed())
	{
		CancelSlashAction(EWeaponActionEndReason::Interrupted,
			TEXT("Could not claim the Host-owned aerial attack budget."));
		return;
	}

	if (UMHGZAbilitySystemComponent* ASC = Cast<UMHGZAbilitySystemComponent>(
		GetAbilitySystemComponentFromActorInfo()))
	{
		if (UGA_WeaponComboCoordinator* Coordinator = ASC->GetActiveComboCoordinator())
		{
			Coordinator->SupersedeOtherIGAerialActions(GetActionToken());
		}
	}

	// The primary montage has no aerial hand-off notify. The Falling state remains
	// owned until a successful hit actually starts the separate 189 Dance montage.
	if (!FMath::IsFinite(MissGravityMagnitude) || MissGravityMagnitude <= 0.0f
		|| !Host->ApplyAerialFallingPhysicsOverride(GetActionToken(), MissGravityMagnitude, 0.0f))
	{
		CancelSlashAction(EWeaponActionEndReason::Interrupted,
			TEXT("Captured miss falling physics rejected the action."));
		return;
	}

	if (!StartHoverMovement())
	{
		CancelSlashAction(EWeaponActionEndReason::Interrupted,
			TEXT("Could not acquire the stationary 186 movement source."));
	}
}

bool UMHGZKinsectSlashAbility::StartHoverMovement()
{
	FWeaponMovementRequest Request;
	Request.OwnerAction = GetActionToken();
	Request.Mode = EWeaponMovementMode::StationaryHover;
	Request.Duration = AttackMontage ? AttackMontage->GetPlayLength() + 1.0f : 0.0f;
	Request.CancelVelocityPolicy = EMovementCancelVelocityPolicy::ZeroVelocity;
	if (Request.Duration <= 0.0f)
	{
		return false;
	}
	HoverMovementTask = UAbilityTask_MHGZWeaponMovement::StartWeaponMovement(
		this, TEXT("KinsectSlashHover186"), Request);
	if (!HoverMovementTask)
	{
		return false;
	}
	HoverMovementTask->ReadyForActivation();
	if (!HoverMovementTask->DidStartMovement())
	{
		HoverMovementTask->EndTask();
		HoverMovementTask = nullptr;
		return false;
	}
	if (UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
		!Host || !Host->SetAerialMovementSource(GetActionToken(),
			KinsectSlashMovementSourceTag()))
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[KinsectSlash] RuntimeHost could not publish the aerial source tag; aborting movement."));
		if (HoverMovementTask)
		{
			HoverMovementTask->EndTask();
			HoverMovementTask = nullptr;
		}
		return false;
	}
	return true;
}

bool UMHGZKinsectSlashAbility::IsCurrentAction(
	const FWeaponActionToken& ActionToken) const
{
	const UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	return !bEndingSlash && IsActive() && IsActionActivationCommitted()
		&& SlashActionToken.IsValid() && ActionToken == SlashActionToken
		&& ActionToken == GetActionToken() && Host
		&& Host->IsTokenCurrent(ActionToken.RuntimeToken);
}

bool UMHGZKinsectSlashAbility::IsCurrentMontage(
	const FWeaponActionToken& ActionToken, const int32 MontageInstanceID) const
{
	if (!IsCurrentAction(ActionToken) || MontageInstanceID == INDEX_NONE
		|| MontageInstanceID != SlashMontageInstanceID
		|| ActionToken != MontageActionToken)
	{
		return false;
	}

	const AMHGZCharacter* Character = Cast<AMHGZCharacter>(GetAvatarActorFromActorInfo());
	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	const UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	FWeaponActionToken ResolvedAction;
	if (!Mesh || !Host || !Host->ResolveMontage(Mesh, MontageInstanceID, ResolvedAction)
		|| ResolvedAction != ActionToken)
	{
		return false;
	}

	const UAnimInstance* AnimInstance = Mesh->GetAnimInstance();
	const FAnimMontageInstance* ActiveInstance = AnimInstance && ActiveSlashMontage
		? AnimInstance->GetActiveInstanceForMontage(ActiveSlashMontage) : nullptr;
	return ActiveInstance && ActiveInstance->GetInstanceID() == MontageInstanceID;
}

bool UMHGZKinsectSlashAbility::IsLateHitOpenNotifyPhaseAllowed(
	const EKinsectSlashPhase CurrentPhase, const FName CurrentSection,
	const FName MissSection)
{
	// The montage can enter Miss188 before the movement task's completion
	// callback is dispatched. The probe queues this request until its dash has
	// finished, so accept that exact boundary ordering as well.
	return (CurrentPhase == EKinsectSlashPhase::Dash
			|| CurrentPhase == EKinsectSlashPhase::MissFall)
		&& CurrentSection == MissSection;
}

void UMHGZKinsectSlashAbility::BindLifecycleGuards()
{
	UMHGZAbilitySystemComponent* ASC = Cast<UMHGZAbilitySystemComponent>(
		GetAbilitySystemComponentFromActorInfo());
	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	if (!ASC || !Host)
	{
		return;
	}

	if (!RuntimeInvalidationDelegateHandle.IsValid())
	{
		RuntimeInvalidationDelegateHandle = Host->OnWeaponRuntimeInvalidated.AddUObject(
			this, &UMHGZKinsectSlashAbility::HandleRuntimeInvalidated);
	}

	const FGameplayTag DisqualifyingTags[] = { DeadTag(), HitstunTag(), KnockdownTag() };
	for (const FGameplayTag& Tag : DisqualifyingTags)
	{
		if (!DisqualifyingStateTagDelegateHandles.Contains(Tag))
		{
			const FDelegateHandle Handle = ASC->RegisterGameplayTagEvent(
				Tag, EGameplayTagEventType::NewOrRemoved).AddUObject(
				this, &UMHGZKinsectSlashAbility::HandleDisqualifyingStateTagChanged);
			DisqualifyingStateTagDelegateHandles.Add(Tag, Handle);
		}
	}

	if (!IsLiveContinuationStateValid())
	{
		CancelSlashAction(EWeaponActionEndReason::Interrupted,
			TEXT("A disqualifying state was already present when the action began."));
	}
}

void UMHGZKinsectSlashAbility::UnbindLifecycleGuards()
{
	if (UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost())
	{
		if (RuntimeInvalidationDelegateHandle.IsValid())
		{
			Host->OnWeaponRuntimeInvalidated.Remove(RuntimeInvalidationDelegateHandle);
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

void UMHGZKinsectSlashAbility::HandleRuntimeInvalidated(
	const FWeaponRuntimeToken& InvalidatedToken)
{
	if (IsActive() && SlashActionToken.IsValid()
		&& SlashActionToken.RuntimeToken == InvalidatedToken)
	{
		CancelSlashAction(EWeaponActionEndReason::Interrupted,
			TEXT("The owning weapon runtime token was invalidated."));
	}
}

void UMHGZKinsectSlashAbility::HandleDisqualifyingStateTagChanged(
	const FGameplayTag Tag, const int32 NewCount)
{
	if (NewCount > 0 && IsActive()
		&& (Tag == DeadTag() || Tag == HitstunTag() || Tag == KnockdownTag()))
	{
		CancelSlashAction(Tag == DeadTag() ? EWeaponActionEndReason::Death
			: EWeaponActionEndReason::Interrupted,
			TEXT("A disqualifying combat-state tag was added during the action."));
	}
}

bool UMHGZKinsectSlashAbility::IsLiveContinuationStateValid() const
{
	const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	const UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	// The Insect Glaive hand-off contract deliberately removes Aerial.Falling
	// during an authored montage lead and grants it back only at that montage's
	// IG_AerialHandoff notify. Consequently continuation checks must not require
	// Falling before the notify that creates that tag.
	return ASC && Host && !Host->IsGrounded()
		&& ASC->HasMatchingGameplayTag(UnsheathedTag())
		&& !ASC->HasMatchingGameplayTag(DeadTag())
		&& !ASC->HasMatchingGameplayTag(HitstunTag())
		&& !ASC->HasMatchingGameplayTag(KnockdownTag());
}

bool UMHGZKinsectSlashAbility::HandleMontageEvent(
	const FWeaponActionToken& ActionToken, const int32 MontageInstanceID,
	const EKinsectSlashMontageEvent Event)
{
	if (!IsCurrentMontage(ActionToken, MontageInstanceID))
	{
		return false;
	}

	UAnimInstance* AnimInstance = ActiveSlashMontage
		? GetCurrentActorInfo()->GetAnimInstance() : nullptr;
	const FName CurrentSection = AnimInstance && ActiveSlashMontage
		? AnimInstance->Montage_GetCurrentSection(ActiveSlashMontage) : NAME_None;

	switch (Event)
	{
	case EKinsectSlashMontageEvent::AimLock:
		if (Phase != EKinsectSlashPhase::Hover || bAimLocked
			|| CurrentSection != HoverSectionName)
		{
			return false;
		}
		if (!LockAimAndDirection())
		{
			CancelSlashAction(EWeaponActionEndReason::Interrupted,
				TEXT("AimLock could not publish the exact locked direction/visual-pitch state."));
			return false;
		}
		bAimLocked = true;
		UE_LOG(LogMHGZ, Display,
			TEXT("[KinsectSlash] AimLock action=%u montage=%d source=%s direction=(%.3f, %.3f, %.3f)"),
			ActionToken.ActivationSequenceID, MontageInstanceID,
			AimSource == EKinsectSlashAimSource::WeaponB ? TEXT("B") : TEXT("RT+B"),
			LaunchDirection.X, LaunchDirection.Y, LaunchDirection.Z);
		return true;

	case EKinsectSlashMontageEvent::DashBegin:
		if (Phase != EKinsectSlashPhase::Hover || !bAimLocked
			|| CurrentSection != DashSectionName || !StartDashMovement())
		{
			CancelSlashAction(EWeaponActionEndReason::Interrupted,
				TEXT("DashBegin arrived in an invalid phase/section or movement startup failed."));
			return false;
		}
		return true;

	case EKinsectSlashMontageEvent::LateHitOpen:
		// Deprecated serialized notify value: AttackCollision owns the full window.
		return true;

	case EKinsectSlashMontageEvent::LateHitClose:
		// Deprecated serialized notify value: keep old assets harmless during migration.
		return true;

	case EKinsectSlashMontageEvent::HoldMissPose:
		if (Phase != EKinsectSlashPhase::MissFall
			|| CurrentSection != MissFallSectionName || bSuccessfulDamage
			|| bMissPoseHeld || !AnimInstance)
		{
			return false;
		}
		DisableCollision(0);
		ReleaseKinsectActionAttachment();
		AnimInstance->Montage_Pause(ActiveSlashMontage);
		bMissPoseHeld = true;
		UE_LOG(LogMHGZ, Display,
			TEXT("[KinsectSlash] Miss188 terminal pose held; CMC falling remains active."));
		return true;

	case EKinsectSlashMontageEvent::DanceBegin:
		// Retained for serialized notifies. Only the guarded preparation timer
		// owns lift-off; repeated/early DanceBegin notifications are harmless.
		return bDanceMontageActive && bSuccessfulDamage && CurrentSection == DanceSectionName
			&& (Phase == EKinsectSlashPhase::DancePending
				|| Phase == EKinsectSlashPhase::DanceFromDash
				|| Phase == EKinsectSlashPhase::DanceFromMiss);
	default:
		return false;
	}
}

void UMHGZKinsectSlashAbility::OnSlashMontageSectionChanged(
	UAnimMontage* Montage, const FName SectionName, const bool bLooped)
{
	(void)bLooped;
	if (!IsActive() || bEndingSlash || Montage != ActiveSlashMontage
		|| !IsCurrentMontage(MontageActionToken, SlashMontageInstanceID)
		|| SectionName != MissFallSectionName)
	{
		return;
	}
	EnterMissFallPhase();
}

bool UMHGZKinsectSlashAbility::EnterMissFallPhase()
{
	if (!IsCurrentAction(SlashActionToken)
		|| (Phase != EKinsectSlashPhase::Dash
			&& Phase != EKinsectSlashPhase::MissFall))
	{
		return false;
	}

	// The montage section boundary is the semantic start of Miss188 even if the
	// movement task's Completed callback is still pending. Publish phase first so
	// synchronous gameplay-tag listeners observe a coherent ability state.
	Phase = EKinsectSlashPhase::MissFall;
	if (bSuccessfulDamage)
	{
		// A real GE hit is stronger evidence than the montage's default miss route.
		// Keep Hit when a late successful contact occurs before this section edge.
		return true;
	}

	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	if (!Host || !Host->SetAerialMovementSource(SlashActionToken,
		KinsectSlashMissSourceTag()))
	{
		CancelSlashAction(EWeaponActionEndReason::Interrupted,
			TEXT("Miss188 was entered but its source tag could not be recorded."));
		return false;
	}
	return IsCurrentAction(SlashActionToken);
}

bool UMHGZKinsectSlashAbility::NotifyAerialHandoff(
	const FWeaponActionToken& ActionToken, const int32 MontageInstanceID)
{
	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	if (!IsCurrentMontage(ActionToken, MontageInstanceID) || bLanded
		|| !Host || Host->IsGrounded()
		|| !IsLiveContinuationStateValid()
		|| (Phase != EKinsectSlashPhase::DanceFromDash
			&& Phase != EKinsectSlashPhase::DanceFromMiss))
	{
		return false;
	}
	return UMHGZInsectGlaiveAbility::NotifyAerialHandoff();
}

bool UMHGZKinsectSlashAbility::LockAimAndDirection()
{
	AMHGZCharacter* Character = Cast<AMHGZCharacter>(GetAvatarActorFromActorInfo());
	if (!Character)
	{
		return false;
	}

	if (AimSource == EKinsectSlashAimSource::None)
	{
		const FGameplayTag ResolvedInput = GetWeaponActivationContext().Input.ResolvedInputTag;
		AimSource = ResolvedInput.MatchesTagExact(WeaponBTag())
			? EKinsectSlashAimSource::WeaponB
			: EKinsectSlashAimSource::KinsectAim;
	}

	if (AimSource == EKinsectSlashAimSource::WeaponB)
	{
		const FVector Forward = Character->GetActorForwardVector();
		LaunchDirection = FVector(Forward.X, Forward.Y, 0.0f).GetSafeNormal();
	}
	else
	{
		FWeaponAimLockSample Sample;
		UMHGZAimComponent* AimComponent = Character->GetAimComponent();
		const bool bCaptured = AimComponent
			&& AimComponent->CaptureAimLockSample(Character->GetActorLocation(), Sample);
		if (bCaptured && IsFiniteVector(Sample.LaunchDirection)
			&& !Sample.LaunchDirection.IsNearlyZero())
		{
			LaunchDirection = Sample.LaunchDirection.GetSafeNormal();
			if (Sample.bUsedActorForwardFallback)
			{
				UE_LOG(LogMHGZ, Warning,
					TEXT("[KinsectSlash] RT+B AimLock used ActorForward fallback (no valid local view)."));
			}
		}
		else
		{
			const FVector Forward = Character->GetActorForwardVector();
			LaunchDirection = FVector(Forward.X, Forward.Y, 0.0f).GetSafeNormal();
			UE_LOG(LogMHGZ, Warning,
				TEXT("[KinsectSlash] RT+B AimLock failed; falling back to ActorForward."));
		}
	}

	if (!IsFiniteVector(LaunchDirection) || LaunchDirection.IsNearlyZero())
	{
		return false;
	}

	if (AimSource == EKinsectSlashAimSource::KinsectAim)
	{
		const URes_InsectGlaive* Resource = GetIGResourceComponent();
		const UInsectGlaiveCombatConfig* Config = Resource
			? Resource->GetCombatConfig() : nullptr;
		if (!Config || !FMath::IsFinite(Config->KinsectSlashMaxUpwardLaunchPitchDegrees))
		{
			return false;
		}

		const FRotator UnclampedRotation = LaunchDirection.Rotation();
		if (UnclampedRotation.Pitch > Config->KinsectSlashMaxUpwardLaunchPitchDegrees)
		{
			FRotator ClampedRotation = UnclampedRotation;
			ClampedRotation.Pitch = Config->KinsectSlashMaxUpwardLaunchPitchDegrees;
			LaunchDirection = ClampedRotation.Vector().GetSafeNormal();
			UE_LOG(LogMHGZ, Display,
				TEXT("[KinsectSlash] Aim pitch clamped from %.2f to captured upward limit %.2f degrees."),
				UnclampedRotation.Pitch, Config->KinsectSlashMaxUpwardLaunchPitchDegrees);
		}
	}

	const FVector YawDirection = ResolveLaunchYawDirection();
	if (!YawDirection.IsNearlyZero())
	{
		FRotator Rotation = Character->GetActorRotation();
		Rotation.Yaw = YawDirection.Rotation().Yaw;
		Character->SetActorRotation(Rotation);
	}

	if (AimSource == EKinsectSlashAimSource::KinsectAim)
	{
		UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
		if (!Host || SlashMontageInstanceID == INDEX_NONE
			|| !Host->TrySetActionVisualPitchState(SlashActionToken,
				SlashMontageInstanceID, LaunchDirection,
				VisualPitchBlendInTime, VisualPitchBlendOutTime))
		{
			UE_LOG(LogMHGZ, Error,
				TEXT("[KinsectSlash] AimLock could not publish visual-pitch state (action=%u montage=%d)."),
				SlashActionToken.ActivationSequenceID, SlashMontageInstanceID);
			return false;
		}
		VisualPitchSourceMontageInstanceID = SlashMontageInstanceID;
	}
	bAimLocked = true;
	return true;
}

void UMHGZKinsectSlashAbility::ReleaseActionVisualPitch(
	const EActionVisualPitchReleaseMode ReleaseMode)
{
	if (VisualPitchSourceMontageInstanceID == INDEX_NONE
		|| !SlashActionToken.IsValid())
	{
		return;
	}
	if (UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost())
	{
		Host->ReleaseActionVisualPitch(SlashActionToken,
			VisualPitchSourceMontageInstanceID, ReleaseMode);
	}
	if (ReleaseMode == EActionVisualPitchReleaseMode::ImmediateReset)
	{
		VisualPitchSourceMontageInstanceID = INDEX_NONE;
	}
}

bool UMHGZKinsectSlashAbility::ShouldDeferMontageCompletionForPendingDance() const
{
	return (Phase == EKinsectSlashPhase::Dash || Phase == EKinsectSlashPhase::MissFall)
		&& bPendingDanceTransition && bSuccessfulDamage;
}

FVector UMHGZKinsectSlashAbility::ResolveLaunchYawDirection() const
{
	return FVector(LaunchDirection.X, LaunchDirection.Y, 0.0f).GetSafeNormal();
}

FVector UMHGZKinsectSlashAbility::ResolveDashTerminalVelocity() const
{
	const URes_InsectGlaive* Resource = GetIGResourceComponent();
	const UInsectGlaiveCombatConfig* Config = Resource
		? Resource->GetCombatConfig() : nullptr;
	if (Config && Config->bUseCapturedKinsectSlashDash)
	{
		return Config->KinsectSlashDashProfile.GetVelocity(LaunchDirection,
			Config->KinsectSlashDashProfile.GetDuration());
	}
	if (!Config || !Config->KinsectSlashDistanceCurve
		|| Config->KinsectSlashDashDuration <= KINDA_SMALL_NUMBER)
	{
		return FVector::ZeroVector;
	}

	float TerminalSlope = 0.0f;
	if (!MHGZWeaponMovementCurves::ValidateNormalizedProgressCurve(
		Config->KinsectSlashDistanceCurve, TerminalSlope))
	{
		return FVector::ZeroVector;
	}
	return LaunchDirection * (Config->KinsectSlashMaxDistance * TerminalSlope
		/ Config->KinsectSlashDashDuration);
}

FWeaponMovementRequest UMHGZKinsectSlashAbility::MakeDashMovementRequest() const
{
	FWeaponMovementRequest Request;
	const URes_InsectGlaive* Resource = GetIGResourceComponent();
	const UInsectGlaiveCombatConfig* Config = Resource
		? Resource->GetCombatConfig() : nullptr;
	if (!Config)
	{
		return Request;
	}

	Request.OwnerAction = GetActionToken();
	Request.Mode = Config->bUseCapturedKinsectSlashDash ? EWeaponMovementMode::SegmentedDirectional
		: EWeaponMovementMode::CurvedDirectional;
	Request.DirectionSnapshot = LaunchDirection;
	Request.SegmentedProfile = Config->KinsectSlashDashProfile;
	Request.Duration = Config->bUseCapturedKinsectSlashDash ? Request.SegmentedProfile.GetDuration()
		: Config->KinsectSlashDashDuration;
	Request.MaxDistance = Config->bUseCapturedKinsectSlashDash
		? Request.SegmentedProfile.GetOffset(LaunchDirection, Request.Duration).Size()
		: Config->KinsectSlashMaxDistance;
	Request.DistanceCurve = Config->KinsectSlashDistanceCurve;
	Request.RotationPolicy = EActionRotationPolicy::Locked;
	Request.CancelVelocityPolicy = EMovementCancelVelocityPolicy::PreserveVelocity;
	return Request;
}

bool UMHGZKinsectSlashAbility::StartDashMovement()
{
	if (HoverMovementTask)
	{
		if (!HoverMovementTask->EndHoverForImmediateMovementHandoff(GetActionToken()))
		{
			return false;
		}
		HoverMovementTask = nullptr;
	}
	const FWeaponMovementRequest Request = MakeDashMovementRequest();
	if (!Request.OwnerAction.IsValid() || Request.DirectionSnapshot.IsNearlyZero()
		|| (Request.Mode == EWeaponMovementMode::CurvedDirectional && Request.DistanceCurve == nullptr)
		|| !Request.HasValidSegmentedDirectionalParameters() || Request.MaxDistance <= 0.0f
		|| Request.Duration <= 0.0f)
	{
		return false;
	}

	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	const URes_InsectGlaive* Resource = GetIGResourceComponent();
	const UInsectGlaiveCombatConfig* Config = Resource
		? Resource->GetCombatConfig() : nullptr;
	if (!Host || !Resource || !Config)
	{
		return false;
	}

	DashTerminalVelocity = ResolveDashTerminalVelocity();
	if (!IsFiniteVector(DashTerminalVelocity) || DashTerminalVelocity.IsNearlyZero())
	{
		return false;
	}

	MovementActionToken = GetActionToken();
	bMovementTaskIsDance = false;
	bDashMovementFinished = false;
	MovementTask = UAbilityTask_MHGZWeaponMovement::StartWeaponMovement(
		this, TEXT("KinsectSlashDashMovement"), Request);
	if (!MovementTask)
	{
		return false;
	}
	MovementTask->OnFinished.AddDynamic(this,
		&UMHGZKinsectSlashAbility::HandleMovementFinished);
	MovementTask->ReadyForActivation();
	if (!MovementTask->DidStartMovement())
	{
		MovementTask->EndTask();
		MovementTask = nullptr;
		return false;
	}

	Phase = EKinsectSlashPhase::Dash;

	UE_LOG(LogMHGZ, Display,
		TEXT("[KinsectSlash] DashBegin action=%u distance=%.1f duration=%.3f terminal=(%.1f, %.1f, %.1f)"),
		GetActionToken().ActivationSequenceID, Request.MaxDistance, Request.Duration,
		DashTerminalVelocity.X, DashTerminalVelocity.Y, DashTerminalVelocity.Z);
	return true;
}

bool UMHGZKinsectSlashAbility::StartDancePreparationMovement()
{
	const FKinsectSlashDanceEntryTuning& Tuning = PendingDanceFromMiss
		? DanceFromMissTuning : DanceFromDashTuning;
	if (Phase != EKinsectSlashPhase::DancePending || HoverMovementTask
		|| !Tuning.IsValid() || !IsCurrentAction(SlashActionToken)
		|| !IsLiveContinuationStateValid())
	{
		return false;
	}

	FWeaponMovementRequest Request;
	Request.OwnerAction = SlashActionToken;
	Request.Mode = EWeaponMovementMode::StationaryHover;
	// The timer ends the exact prep interval. The source lease has grace so a
	// movement tick cannot retire the hover before that same frame's timer tick.
	Request.Duration = Tuning.PrepDuration + 1.0f;
	Request.CancelVelocityPolicy = EMovementCancelVelocityPolicy::PreserveVelocity;
	HoverMovementTask = UAbilityTask_MHGZWeaponMovement::StartWeaponMovement(
		this, TEXT("KinsectSlashDancePreparation"), Request);
	if (!HoverMovementTask)
	{
		return false;
	}
	HoverMovementTask->ReadyForActivation();
	if (!HoverMovementTask || !HoverMovementTask->DidStartMovement())
	{
		if (HoverMovementTask)
		{
			HoverMovementTask->EndTask();
			HoverMovementTask = nullptr;
		}
		return false;
	}
	return true;
}

bool UMHGZKinsectSlashAbility::StartDanceMovement(const bool bFromMiss)
{
	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	if (Phase != EKinsectSlashPhase::DancePending || !bDanceEntryCommitted
		|| !bDanceMontageActive || !bSuccessfulDamage || bLanded
		|| !IsCurrentMontage(SlashActionToken, SlashMontageInstanceID)
		|| !IsLiveContinuationStateValid() || !Host || Host->IsGrounded()
		|| !HoverMovementTask)
	{
		return false;
	}
	const FKinsectSlashDanceEntryTuning& Tuning = bFromMiss
		? DanceFromMissTuning : DanceFromDashTuning;
	if (!Tuning.IsValid())
	{
		return false;
	}

	FWeaponMovementRequest Request = Tuning.MakeRequest(GetActionToken(), LaunchDirection, this);
	if (!Request.HasValidBallisticParameters()
		|| !Request.HasValidAuthoredAerialPhysicsParameters())
	{
		return false;
	}
	if (!HoverMovementTask->EndHoverForImmediateMovementHandoff(GetActionToken()))
	{
		return false;
	}
	HoverMovementTask = nullptr;
	if (!Host->ApplyAerialFallingPhysicsOverride(GetActionToken(), Tuning.GravityMagnitude, 0.0f,
		Request.PlanarAcceleration))
	{
		return false;
	}
	MovementActionToken = GetActionToken();
	bMovementTaskIsDance = true;
	bDanceMovementFinished = false;
	Phase = bFromMiss ? EKinsectSlashPhase::DanceFromMiss
		: EKinsectSlashPhase::DanceFromDash;
	MovementTask = UAbilityTask_MHGZWeaponMovement::StartWeaponMovement(
		this, bFromMiss ? TEXT("KinsectSlashDanceFromMiss")
			: TEXT("KinsectSlashDanceFromDash"), Request);
	if (!MovementTask)
	{
		return false;
	}
	MovementTask->OnFinished.AddDynamic(this,
		&UMHGZKinsectSlashAbility::HandleMovementFinished);
	MovementTask->ReadyForActivation();
	if (!MovementTask || !MovementTask->DidStartMovement())
	{
		if (MovementTask)
		{
			MovementTask->EndTask();
			MovementTask = nullptr;
		}
		return false;
	}

	UE_LOG(LogMHGZ, Display,
		TEXT("[KinsectSlash] Dance CMC flight source=%s action=%u launch=(%.3f, %.3f, %.3f) accel=(%.3f, %.3f, %.3f) duration=%.3f"),
		bFromMiss ? TEXT("188") : TEXT("187"), GetActionToken().ActivationSequenceID,
		Request.LaunchVelocity.X, Request.LaunchVelocity.Y, Request.LaunchVelocity.Z,
		Request.PlanarAcceleration.X, Request.PlanarAcceleration.Y,
		Request.PlanarAcceleration.Z, Request.Duration);
	return true;
}

void UMHGZKinsectSlashAbility::HandleDancePreparationFinished(
	FWeaponActionToken ActionToken, const int32 MontageInstanceID)
{
	// Check the captured identity before touching the handle: an old callback
	// must not invalidate a new activation's preparation timer.
	if (!IsCurrentMontage(ActionToken, MontageInstanceID)
		|| Phase != EKinsectSlashPhase::DancePending || !bDanceMontageActive
		|| !bDanceEntryCommitted || !bSuccessfulDamage || bLanded)
	{
		return;
	}
	DancePreparationTimer.Invalidate();
	if (!IsLiveContinuationStateValid() || !StartDanceMovement(PendingDanceFromMiss))
	{
		CancelSlashAction(EWeaponActionEndReason::Interrupted,
			TEXT("189 preparation finished but the captured flight could not start."));
	}
}

void UMHGZKinsectSlashAbility::ClearDanceTimers()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(QueuedDanceTransitionTimer);
		World->GetTimerManager().ClearTimer(DancePreparationTimer);
	}
	QueuedDanceTransitionTimer.Invalidate();
	DancePreparationTimer.Invalidate();
}

void UMHGZKinsectSlashAbility::HandleProbeHitzoneContact(
	FWeaponActionToken ActionToken, FHitResult Hit)
{
	if (!IsCurrentAction(ActionToken) || bSuccessfulDamage || bDamageResolveInProgress
		|| (Phase != EKinsectSlashPhase::Dash && Phase != EKinsectSlashPhase::MissFall))
	{
		return;
	}

	UMHGZMonsterHitzoneComponent* Hitzone =
		Cast<UMHGZMonsterHitzoneComponent>(Hit.GetComponent());
	if (!Hitzone || (AttackSegments.IsEmpty()))
	{
		return;
	}
	const FGameplayTag QueryTag = AttackSegments[0].Collision.HitzoneQueryTag;
	if (QueryTag.IsValid() && !Hitzone->HitzoneTag.MatchesTagExact(QueryTag))
	{
		return;
	}

	bDamageResolveInProgress = true;
	bCurrentDamageApplied = false;
	ApplyDamage(Hit, 0);
	if (!IsCurrentAction(ActionToken))
	{
		return;
	}
	if (!bCurrentDamageApplied)
	{
		bDamageResolveInProgress = false;
		return;
	}
	if (ProbeTask)
	{
		ProbeTask->AcknowledgeSuccessfulContact(ActionToken);
	}

	// Keep the first successful GE result immutable. Section changes and probe
	// teardown are queued outside the current Sweep callback stack.
	SuccessfulHit = Hit;
	bSuccessfulDamage = true;
	PendingDanceFromMiss = Phase == EKinsectSlashPhase::MissFall;
	bPendingDanceTransition = true;
	if (UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
		!Host || !Host->SetAerialMovementSource(ActionToken, KinsectSlashHitSourceTag()))
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[KinsectSlash] Damage GE succeeded, but RuntimeHost could not publish the hit source tag; aborting action."));
		CancelSlashAction(EWeaponActionEndReason::Interrupted,
			TEXT("The successful hit source could not be recorded."));
		return;
	}
	if (!IsCurrentAction(ActionToken))
	{
		return;
	}
	const FWeaponActionToken CapturedToken = ActionToken;
	const int32 CapturedMontageID = SlashMontageInstanceID;
	if (UWorld* World = GetWorld())
	{
		QueuedDanceTransitionTimer = World->GetTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateUObject(this,
				&UMHGZKinsectSlashAbility::HandleQueuedDanceTransition,
				CapturedToken, CapturedMontageID));
	}
}

void UMHGZKinsectSlashAbility::HandleSuccessfulAttackDamage(
	const FHitResult& Hit, const int32 SegmentIndex)
{
	if (SegmentIndex != 0 || bSuccessfulDamage)
	{
		return;
	}

	const bool bLegacyProbeCallback = bDamageResolveInProgress;
	if (bLegacyProbeCallback)
	{
		bCurrentDamageApplied = true;
	}
	else if (bEndingSlash || !IsCurrentAction(SlashActionToken)
		|| !IsKinsectActionAttachmentCurrent()
		|| (Phase != EKinsectSlashPhase::Dash
			&& Phase != EKinsectSlashPhase::MissFall)
		|| !IsCurrentMontage(SlashActionToken, SlashMontageInstanceID))
	{
		return;
	}

	// Compatibility path for tests/old callers only; production no longer creates
	// the probe task. The AttackCollision path below owns its own queued handoff.
	if (bLegacyProbeCallback)
	{
		if (!bExtractApplied)
		{
			if (URes_InsectGlaive* Resource = GetIGResourceComponent())
			{
				const UMHGZMonsterHitzoneComponent* Hitzone =
					Cast<UMHGZMonsterHitzoneComponent>(Hit.GetComponent());
				if (Hitzone)
				{
					Resource->ApplyExtractFromHitzone(Hitzone);
				}
			}
			bExtractApplied = true;
		}
		return;
	}

	const UAnimInstance* AnimInstance = GetCurrentActorInfo()
		? GetCurrentActorInfo()->GetAnimInstance() : nullptr;
	const FName SourceSection = AnimInstance && ActiveSlashMontage
		? AnimInstance->Montage_GetCurrentSection(ActiveSlashMontage) : NAME_None;
	const bool bCanEnterDance = SourceSection == DashSectionName
		|| SourceSection == MissFallSectionName;
	bSuccessfulDamage = true;
	if (bCanEnterDance)
	{
		SuccessfulHit = Hit;
		SuccessfulHitSourceSection = SourceSection;
		SuccessfulHitSourceMontageInstanceID = SlashMontageInstanceID;
		SuccessfulHitSourcePosition = AnimInstance->Montage_GetPosition(ActiveSlashMontage);
		PendingDanceFromMiss = SourceSection == MissFallSectionName;
		bPendingDanceTransition = true;
	}
	if (UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
		!Host || !Host->SetAerialMovementSource(SlashActionToken,
			KinsectSlashHitSourceTag()))
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[KinsectSlash] Damage GE succeeded, but RuntimeHost could not publish the hit source tag; aborting action."));
		CancelSlashAction(EWeaponActionEndReason::Interrupted,
			TEXT("The successful hit source could not be recorded."));
		return;
	}
	if (!IsCurrentAction(SlashActionToken))
	{
		return;
	}
	if (SourceSection != DashSectionName && SourceSection != MissFallSectionName)
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[KinsectSlash] Segment 0 GE succeeded outside Dash187/Miss188; no dance handoff was queued."));
		return;
	}

	if (!bExtractApplied)
	{
		bExtractApplied = true;
		if (URes_InsectGlaive* Resource = GetIGResourceComponent())
		{
			const UMHGZMonsterHitzoneComponent* Hitzone =
				Cast<UMHGZMonsterHitzoneComponent>(Hit.GetComponent());
			if (Hitzone)
			{
				const bool bExtractGranted = Resource->ApplyExtractFromHitzone(Hitzone);
				UE_LOG(LogMHGZ, Display,
					TEXT("[KinsectSlash] Damage GE succeeded; extract=%s color=%s"),
					bExtractGranted ? TEXT("applied") : TEXT("not-applicable"),
					*Hitzone->ExtractColorTag.ToString());
			}
		}
	}
	const FWeaponActionToken CapturedToken = SlashActionToken;
	const int32 CapturedMontageID = SuccessfulHitSourceMontageInstanceID;
	if (UWorld* World = GetWorld())
	{
		QueuedDanceTransitionTimer = World->GetTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateUObject(this,
				&UMHGZKinsectSlashAbility::HandleQueuedDanceTransition,
				CapturedToken, CapturedMontageID));
	}
}

void UMHGZKinsectSlashAbility::HandleQueuedDanceTransition(
	FWeaponActionToken ActionToken, const int32 MontageInstanceID)
{
	if (!IsCurrentMontage(ActionToken, MontageInstanceID)
		|| !bPendingDanceTransition || !bSuccessfulDamage
		|| !IsLiveContinuationStateValid())
	{
		return;
	}
	QueuedDanceTransitionTimer.Invalidate();
	// 189 owns its own movement/pose. Retire only the 186/187/188 correction,
	// before stopping the outgoing montage, and let the AnimInstance blend it out.
	ReleaseActionVisualPitch(EActionVisualPitchReleaseMode::BlendOut);
	UE_LOG(LogMHGZ, Display,
		TEXT("[KinsectSlash] Queued hit source montage=%d section=%s position=%.4f s entry=%s"),
		SuccessfulHitSourceMontageInstanceID,
		*SuccessfulHitSourceSection.ToString(), SuccessfulHitSourcePosition,
		PendingDanceFromMiss ? TEXT("Miss188") : TEXT("Dash187"));

	// End every old montage collision segment only after the current sweep has
	// unwound, so an overlapping weapon segment may still commit on this tick.
	DisableCollision();
	ReleaseKinsectActionAttachment();

	if (Phase == EKinsectSlashPhase::Dash && MovementTask)
	{
		bEndingDashForDance = true;
		const bool bRetiredDash = MovementTask->EndForImmediateMovementHandoff(ActionToken);
		bEndingDashForDance = false;
		if (!bRetiredDash)
		{
			CancelSlashAction(EWeaponActionEndReason::Interrupted,
				TEXT("Could not retire the dash for the same-action 189 preparation hover."));
			return;
		}
		MovementTask = nullptr;
		bDashMovementFinished = true;
	}
	else if (Phase != EKinsectSlashPhase::Dash
		&& Phase != EKinsectSlashPhase::MissFall)
	{
		CancelSlashAction(EWeaponActionEndReason::Interrupted,
			TEXT("Successful hit arrived outside the Dash/MissFall phases."));
		return;
	}
	if (HoverMovementTask)
	{
		if (!HoverMovementTask->EndForImmediateMovementHandoff(ActionToken))
		{
			CancelSlashAction(EWeaponActionEndReason::Interrupted,
				TEXT("Could not retire the old hover for 189 preparation."));
			return;
		}
		HoverMovementTask = nullptr;
	}

	UAnimInstance* AnimInstance = GetCurrentActorInfo()
		? GetCurrentActorInfo()->GetAnimInstance() : nullptr;
	UWorld* World = GetWorld();
	if (!ActiveSlashMontage || !AnimInstance || !World || !HasRequiredDanceMontage())
	{
		CancelSlashAction(EWeaponActionEndReason::Interrupted,
			TEXT("Separate 189 Dance montage or its entry section is missing."));
		return;
	}

	Phase = EKinsectSlashPhase::DancePending;
	if (!StartDancePreparationMovement())
	{
		CancelSlashAction(EWeaponActionEndReason::Interrupted,
			TEXT("Could not start the stationary 189 preparation hover."));
		return;
	}
	DanceMontageTask = nullptr;
	bDanceMontageActive = true;
	bMontageFinished = false;
	DetectAerialHandoffNotify(DanceMontage);

	// The outgoing 186/187/188 pose blends out as the single, shared frame-0
	// 189 montage plays throughout preparation. The capture does not establish
	// a skeletal freeze frame, and both hit origins use the same captured motion.
	UAnimMontage* OutgoingMontage = ActiveSlashMontage;
	if (SlashMontageTask)
	{
		SlashMontageTask->EndTask();
		SlashMontageTask = nullptr;
	}
	if (OutgoingMontage)
	{
		AnimInstance->Montage_Stop(0.1f, OutgoingMontage);
	}

	ActiveSlashMontage = DanceMontage;
	ActiveAttackMontage = DanceMontage;
	MontageActionToken = ActionToken;
	DanceMontageTask = UAbilityTask_MHGZPlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, TEXT("KinsectSlash189Dance"), DanceMontage, 1.0f, DanceSectionName,
		DanceMontageBlendInTime, false);
	if (!DanceMontageTask)
	{
		CancelSlashAction(EWeaponActionEndReason::Interrupted,
			TEXT("Could not create the 189 Dance montage task."));
		return;
	}
	DanceMontageTask->OnCompleted.AddDynamic(this,
		&UMHGZKinsectSlashAbility::HandleDanceMontageCompleted);
	DanceMontageTask->OnInterrupted.AddDynamic(this,
		&UMHGZKinsectSlashAbility::HandleDanceMontageInterrupted);
	DanceMontageTask->OnCancelled.AddDynamic(this,
		&UMHGZKinsectSlashAbility::HandleDanceMontageInterrupted);
	DanceMontageTask->ReportCompletionAtMontageEnd();
	DanceMontageTask->ReadyForActivation();
	if (!IsCurrentAction(ActionToken))
	{
		return;
	}

	FAnimMontageInstance* DanceInstance =
		AnimInstance->GetActiveInstanceForMontage(DanceMontage);
	if (!DanceInstance
		|| AnimInstance->Montage_GetCurrentSection(DanceMontage) != DanceSectionName)
	{
		CancelSlashAction(EWeaponActionEndReason::Interrupted,
			TEXT("Montage refused the shared source-frame-0 Dance entry."));
		return;
	}
	SlashMontageInstanceID = DanceInstance->GetInstanceID();
	DanceInstance->bEnableAutoBlendOut = false;
	DanceInstance->PushDisableRootMotion();
	AMHGZCharacter* Character = Cast<AMHGZCharacter>(GetAvatarActorFromActorInfo());
	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	if (!Mesh || !RegisterMontageInstance(Mesh, SlashMontageInstanceID))
	{
		CancelSlashAction(EWeaponActionEndReason::Interrupted,
			TEXT("Could not register the 189 Dance montage instance with RuntimeHost."));
		return;
	}
	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	URes_InsectGlaive* Resource = GetIGResourceComponent();
	if (!IsCurrentMontage(ActionToken, SlashMontageInstanceID)
		|| !IsLiveContinuationStateValid() || !HoverMovementTask
		|| !HoverMovementTask->DidStartMovement() || !Host || !Resource
		|| bDanceEntryCommitted)
	{
		CancelSlashAction(EWeaponActionEndReason::Interrupted,
			TEXT("The confirmed 189 montage and preparation could not commit the Dance entry."));
		return;
	}
	const int32 CommittingMontageID = SlashMontageInstanceID;
	const bool bStackAdded = Resource->AddDanceStack(EIGDanceSource::KinsectSlash);
	// Resource and ASC delegates may cancel/supersede this GA synchronously.
	// Never restore flags/budgets/timers after EndAbility has just cleared them.
	if (!IsCurrentMontage(ActionToken, CommittingMontageID))
	{
		return;
	}
	if (!bStackAdded || !IsLiveContinuationStateValid())
	{
		CancelSlashAction(EWeaponActionEndReason::Interrupted,
			TEXT("189 Dance resource commit failed or invalidated the action."));
		return;
	}
	const bool bBudgetReset = Host->ResetAerialActionBudgetOnDance();
	if (!IsCurrentMontage(ActionToken, CommittingMontageID))
	{
		return;
	}
	if (!bBudgetReset || !IsLiveContinuationStateValid())
	{
		CancelSlashAction(EWeaponActionEndReason::Interrupted,
			TEXT("189 Dance budget reset failed or invalidated the action."));
		return;
	}
	bDanceEntryCommitted = true;
	bPendingDanceTransition = false;
	bDamageResolveInProgress = false;
	const FKinsectSlashDanceEntryTuning& Tuning = PendingDanceFromMiss
		? DanceFromMissTuning : DanceFromDashTuning;
	World->GetTimerManager().SetTimer(DancePreparationTimer,
		FTimerDelegate::CreateUObject(this,
			&UMHGZKinsectSlashAbility::HandleDancePreparationFinished,
			ActionToken, SlashMontageInstanceID), Tuning.PrepDuration, false);
	UE_LOG(LogMHGZ, Display,
		TEXT("[KinsectSlash] GE success queued shared 189 section=%s entry_frame=0 source=%s"),
		*DanceSectionName.ToString(), PendingDanceFromMiss ? TEXT("188") : TEXT("187"));
}

void UMHGZKinsectSlashAbility::HandleMovementFinished(
	const FWeaponMovementResult& Result)
{
	const FWeaponActionToken CallbackToken = MovementActionToken;
	if (!IsCurrentAction(CallbackToken) || bEndingSlash || bEndingDashForDance)
	{
		return;
	}

	const bool bWasDanceMovement = bMovementTaskIsDance;
	MovementTask = nullptr;
	if (bWasDanceMovement)
	{
		if (Result.EndReason == EWeaponMovementEndReason::Completed
			|| Result.EndReason == EWeaponMovementEndReason::Landed)
		{
			bDanceMovementFinished = true;
			// Natural 189 ->157 only changes presentation. The captured -360
			// planar acceleration and 1800 gravity continue until landing/takeover.
			if (Result.EndReason == EWeaponMovementEndReason::Landed)
			{
				bLanded = true;
				// Dance may touch down before its authored montage ends. Clear the
				// Host-owned aerial state now, but keep the 189 action montage alive;
				// its own end callback decides when the GA may finish.
				FinalizeHostLanding(false);
			}
			if (bMontageFinished)
			{
				CompleteSlashAction();
			}
		}
		else
		{
			CancelSlashAction(EWeaponActionEndReason::Interrupted,
				TEXT("Dance physical movement ended before its authored completion."));
		}
		return;
	}

	const bool bDashWasActive = Phase == EKinsectSlashPhase::Dash
		|| Phase == EKinsectSlashPhase::MissFall;
	if (!bDashWasActive)
	{
		return;
	}

	if (Result.EndReason == EWeaponMovementEndReason::Completed)
	{
		UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
		ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
		UCharacterMovementComponent* CMC = Character ? Character->GetCharacterMovement() : nullptr;
		if (!Host || !CMC || !IsFiniteVector(Result.FinalVelocity)
			|| !Host->ApplyAerialFallingPhysicsOverride(CallbackToken, MissGravityMagnitude, 0.0f))
		{
			CancelSlashAction(EWeaponActionEndReason::Interrupted,
				TEXT("Natural dash completion could not install the captured Miss188 physics."));
			return;
		}
		DashTerminalVelocity = Result.FinalVelocity;
		CMC->Velocity = Result.FinalVelocity;
		bDashMovementFinished = true;
		if (!EnterMissFallPhase())
		{
			if (IsActive())
			{
				CancelSlashAction(EWeaponActionEndReason::Interrupted,
					TEXT("Natural dash completion could not commit the Miss188 phase/source."));
			}
			return;
		}
	}
	else if (Result.EndReason == EWeaponMovementEndReason::Landed)
	{
		HandleLanded(Result.BlockingHit);
	}
	else
	{
		CancelSlashAction(EWeaponActionEndReason::Interrupted,
			TEXT("Dash movement ended by cancellation or invalid runtime."));
	}
	UE_LOG(LogMHGZ, Display,
		TEXT("[KinsectSlash] DashMovementEnd action=%u reason=%d location=(%.1f, %.1f, %.1f) terminal=(%.1f, %.1f, %.1f)"),
		CallbackToken.ActivationSequenceID, static_cast<int32>(Result.EndReason),
		GetAvatarActorFromActorInfo() ? GetAvatarActorFromActorInfo()->GetActorLocation().X : 0.0f,
		GetAvatarActorFromActorInfo() ? GetAvatarActorFromActorInfo()->GetActorLocation().Y : 0.0f,
		GetAvatarActorFromActorInfo() ? GetAvatarActorFromActorInfo()->GetActorLocation().Z : 0.0f,
		Result.FinalVelocity.X, Result.FinalVelocity.Y, Result.FinalVelocity.Z);
}

void UMHGZKinsectSlashAbility::HandleSlashMontageCompleted()
{
	SlashMontageTask = nullptr;
	if (bDanceMontageActive || Phase == EKinsectSlashPhase::DancePending)
	{
		return;
	}
	HandleMontageCompleted();
}

void UMHGZKinsectSlashAbility::OnMontageCompleted()
{
	if (IsActive() && !bDanceMontageActive)
	{
		HandleMontageCompleted();
	}
}

void UMHGZKinsectSlashAbility::HandleMontageCompleted()
{
	if (!IsCurrentAction(MontageActionToken))
	{
		return;
	}
	bMontageFinished = true;

	if (Phase == EKinsectSlashPhase::MissFall)
	{
		// A successful 188 hit queues the 189 Montage change for the next tick so
		// that the sweep callback can unwind safely. Otherwise, completion is not
		// an action exit: keep the terminal pose until landing or supersession.
		if (ShouldDeferMontageCompletionForPendingDance())
		{
			return;
		}
		bMissPoseHeld = true;
		return;
	}
	if (ShouldDeferMontageCompletionForPendingDance())
	{
		return;
	}

	if ((Phase == EKinsectSlashPhase::DancePending
			|| Phase == EKinsectSlashPhase::DanceFromDash
			|| Phase == EKinsectSlashPhase::DanceFromMiss)
		&& bDanceEntryCommitted)
	{
		if (bDanceMovementFinished)
		{
			CompleteSlashAction();
		}
		return;
	}

	CancelSlashAction(EWeaponActionEndReason::Interrupted,
		TEXT("The Montage ended before a valid MissFall or committed Dance exit."));
}

void UMHGZKinsectSlashAbility::HandleSlashMontageInterrupted()
{
	SlashMontageTask = nullptr;
	if (!bEndingSlash && IsActive() && !bDanceMontageActive
		&& Phase != EKinsectSlashPhase::DancePending)
	{
		CancelSlashAction(EWeaponActionEndReason::Interrupted,
			TEXT("The action Montage was interrupted."));
	}
}

void UMHGZKinsectSlashAbility::HandleDanceMontageCompleted()
{
	DanceMontageTask = nullptr;
	if (bDanceMontageActive)
	{
		HandleMontageCompleted();
	}
}

void UMHGZKinsectSlashAbility::HandleDanceMontageInterrupted()
{
	DanceMontageTask = nullptr;
	if (bDanceMontageActive && !bEndingSlash && IsActive())
	{
		CancelSlashAction(EWeaponActionEndReason::Interrupted,
			TEXT("The 189 Dance montage was interrupted before normal completion."));
	}
}

void UMHGZKinsectSlashAbility::CompleteSlashAction()
{
	if (!IsActive() || bEndingSlash)
	{
		return;
	}
	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	const FWeaponRuntimeToken RuntimeToken = GetActionToken().RuntimeToken;
	const bool bCompletedDance = bDanceEntryCommitted
		&& (Phase == EKinsectSlashPhase::DanceFromDash
			|| Phase == EKinsectSlashPhase::DanceFromMiss);
	const URes_InsectGlaive* Resource = GetIGResourceComponent();
	const UInsectGlaiveCombatConfig* Config = Resource ? Resource->GetCombatConfig() : nullptr;
	UAnimMontage* DanceFallMontage = Config ? Config->GetAerialDodgeFallMontage() : nullptr;
	RequestEndAction(EWeaponActionEndReason::Normal);
	if (Host && Host->IsTokenCurrent(RuntimeToken) && !Host->IsGrounded()
		&& !Host->HasRegisteredAction())
	{
		const bool bStartedFalling = bCompletedDance
			? Host->BeginAerialFalling(false, FGameplayTag(), DanceFallMontage, true)
			: Host->BeginAerialFalling(false);
		if (!bStartedFalling)
		{
			UE_LOG(LogMHGZ, Warning,
				TEXT("[KinsectSlash] Host could not start free-fall presentation after the action."));
		}
	}
}

void UMHGZKinsectSlashAbility::CancelSlashAction(
	const EWeaponActionEndReason EndReason, const TCHAR* Diagnostic)
{
	if (!IsActive() || bEndingSlash)
	{
		return;
	}
	UE_LOG(LogMHGZ, Warning,
		TEXT("[KinsectSlash] action=%u phase=%d ending=%d: %s"),
		GetActionToken().ActivationSequenceID, static_cast<int32>(Phase),
		static_cast<int32>(EndReason), Diagnostic);
	ClearDanceTimers();
	RequestEndAction(EndReason);
}

void UMHGZKinsectSlashAbility::HandleLanded(const FHitResult& Hit)
{
	if (!IsCurrentAction(GetActionToken()))
	{
		return;
	}
	bLanded = true;
	DisableCollision();
	ReleaseKinsectActionAttachment();
	FinalizeHostLanding(true);
	(void)Hit;
	RequestEndAction(EWeaponActionEndReason::Normal);
}

void UMHGZKinsectSlashAbility::FinalizeHostLanding(
	const bool bPlayLandingPresentation)
{
	if (UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost())
	{
		// CurvedDirectional can report a walkable collision before CMC emits
		// ACharacter::Landed (and MOVE_Flying paths do not always emit it at all).
		// Make Host cleanup synchronous with this task callback so aerial budget
		// tokens and Dance stacks cannot survive the landing.
		Host->HandleLanded();
		if (bPlayLandingPresentation && !Host->IsAerialLanding())
		{
			Host->PlayAerialLandingPresentation();
		}
	}
}

void UMHGZKinsectSlashAbility::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const bool bReplicateEndAbility, const bool bWasCancelled)
{
	bEndingSlash = true;
	DisableCollision();
	ReleaseKinsectActionAttachment();
	const EWeaponActionEndReason EndReason = GetActionEndReason();
	ReleaseActionVisualPitch(!bWasCancelled && EndReason == EWeaponActionEndReason::Normal
		? EActionVisualPitchReleaseMode::BlendOut
		: EActionVisualPitchReleaseMode::ImmediateReset);
	Phase = EKinsectSlashPhase::Ended;
	UnbindLifecycleGuards();
	ClearDanceTimers();

	// Safe retirement of a task left by a hot-reloaded pre-migration instance;
	// the current production path never creates this legacy task.
	if (ProbeTask)
	{
		ProbeTask->EndTask();
		ProbeTask = nullptr;
	}
	if (MovementTask)
	{
		MovementTask->EndTask();
		MovementTask = nullptr;
	}
	if (HoverMovementTask)
	{
		HoverMovementTask->EndTask();
		HoverMovementTask = nullptr;
	}

	AMHGZCharacter* Character = Cast<AMHGZCharacter>(GetAvatarActorFromActorInfo());
	UAnimInstance* AnimInstance = Character && Character->GetMesh()
		? Character->GetMesh()->GetAnimInstance() : nullptr;
	if (AnimInstance && ActiveSlashMontage && SlashMontageInstanceID != INDEX_NONE)
	{
		FAnimMontageInstance* ActiveInstance =
			AnimInstance->GetActiveInstanceForMontage(ActiveSlashMontage);
		if (ActiveInstance && ActiveInstance->GetInstanceID() == SlashMontageInstanceID)
		{
			ActiveInstance->OnMontageSectionChanged.Unbind();
			AnimInstance->Montage_Stop(0.05f, ActiveSlashMontage);
		}
	}
	if (SlashMontageTask)
	{
		SlashMontageTask->EndTask();
		SlashMontageTask = nullptr;
	}
	if (DanceMontageTask)
	{
		DanceMontageTask->EndTask();
		DanceMontageTask = nullptr;
	}
	ActiveSlashMontage = nullptr;
	ActiveAttackMontage = nullptr;
	VisualPitchSourceMontageInstanceID = INDEX_NONE;

	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	const FWeaponRuntimeToken RuntimeToken = SlashActionToken.RuntimeToken;
	UCharacterMovementComponent* CMC = Character ? Character->GetCharacterMovement() : nullptr;
	const bool bNeedsMovementModeRestore = CMC && CMC->MovementMode == MOVE_Flying;
	const bool bRestoreNormalFallProfile = bWasCancelled
		&& GetActionEndReason() != EWeaponActionEndReason::Superseded;
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility,
		bWasCancelled);

	if (bNeedsMovementModeRestore && Host && Host->IsTokenCurrent(RuntimeToken)
		&& !Host->IsGrounded() && !Host->HasRegisteredAction() && CMC
		&& CMC->MovementMode == MOVE_Flying)
	{
		FFindFloorResult Floor;
		CMC->FindFloor(Character->GetActorLocation(), Floor, false);
		const bool bOnFloor = Floor.IsWalkableFloor()
			&& Floor.FloorDist <= UCharacterMovementComponent::MAX_FLOOR_DIST;
		CMC->SetMovementMode(bOnFloor ? MOVE_Walking : MOVE_Falling);
	}
	// Natural 189 completion keeps its 1800/-360 profile for 157. Only an
	// unsuperseded cancellation back to unowned free-fall selects the normal one.
	if (bRestoreNormalFallProfile && Host && Host->IsTokenCurrent(RuntimeToken)
		&& !Host->IsGrounded() && !Host->HasRegisteredAction()
		&& !Host->ApplyAerialFallingProfile(false))
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[KinsectSlash] Cancellation could not restore the normal aerial falling profile."));
	}
}

void UMHGZKinsectSlashAbility::ResetActivationState()
{
	ClearDanceTimers();
	SlashActionToken = FWeaponActionToken();
	MontageActionToken = FWeaponActionToken();
	MovementActionToken = FWeaponActionToken();
	SlashMontageTask = nullptr;
	DanceMontageTask = nullptr;
	MovementTask = nullptr;
	HoverMovementTask = nullptr;
	ActiveSlashMontage = nullptr;
	SlashMontageInstanceID = INDEX_NONE;
	VisualPitchSourceMontageInstanceID = INDEX_NONE;
	AimSource = EKinsectSlashAimSource::None;
	Phase = EKinsectSlashPhase::Inactive;
	LaunchDirection = FVector::ForwardVector;
	DashTerminalVelocity = FVector::ZeroVector;
	SuccessfulHit = FHitResult();
	SuccessfulHitSourceSection = NAME_None;
	SuccessfulHitSourcePosition = 0.0f;
	SuccessfulHitSourceMontageInstanceID = INDEX_NONE;
	bKinsectActionAttached = false;
	bAimLocked = false;
	bDashMovementFinished = false;
	bDanceMovementFinished = false;
	bMontageFinished = false;
	bPendingDanceTransition = false;
	bDamageResolveInProgress = false;
	bCurrentDamageApplied = false;
	bSuccessfulDamage = false;
	bExtractApplied = false;
	bDanceEntryCommitted = false;
	bDanceMontageActive = false;
	bMissPoseHeld = false;
	bEndingSlash = false;
	bEndingDashForDance = false;
	bMovementTaskIsDance = false;
	bLanded = false;
	PendingDanceFromMiss = false;
}

#if WITH_EDITOR
EDataValidationResult UMHGZKinsectSlashAbility::IsDataValid(
	FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	bool bInvalid = false;
	auto AddError = [&Context, &bInvalid](const FString& Message)
	{
		Context.AddError(FText::FromString(Message));
		bInvalid = true;
	};

	if (ActivationRequiredTags.HasTag(FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Aerial.Actionable"))))
	{
		AddError(TEXT("Kinsect Slash ActivationRequiredTags must not require deprecated Combat.State.Aerial.Actionable; use Combat.State.Aerial.Falling for aerial eligibility."));
	}

	if (AttackSegments.Num() != 3)
	{
		AddError(TEXT("Kinsect Slash requires three AttackSegments: [0] attached-kinsect contact, [1] rear-tip-to-root weapon contact, and [2] independent weapon contact."));
	}
	else
	{
		for (int32 SegmentIndex = 0; SegmentIndex < 3; ++SegmentIndex)
		{
			const FAttackSegmentConfig& Segment = AttackSegments[SegmentIndex];
			if (!Segment.Damage.DamageEffectClass
				|| Segment.Damage.MotionValue.GetValueAtLevel(1.0f) <= 0.0f
				|| Segment.Damage.bRequiresHitToContinue
				|| Segment.MultiHitCount != 1
				|| Segment.MultiHitPolicy != EAttackMultiHitPolicy::ContactOnly)
			{
				AddError(FString::Printf(TEXT("AttackSegments[%d] requires a damage GE, positive MotionValue, no hit-required cancellation, and exactly one ContactOnly hit."), SegmentIndex));
			}
			if (Segment.Collision.TraceRegions.IsEmpty())
			{
				AddError(FString::Printf(TEXT("AttackSegments[%d] requires TraceRegions configured for its AttackCollision window."), SegmentIndex));
			}
			if (SegmentIndex == 0
				&& (Segment.Collision.TraceRegions.Num() != 1
					|| !Segment.Collision.TraceRegions[0].StartSocketName.IsNone()
					|| Segment.Collision.TraceRegions[0].EndSocketName.IsNone()))
			{
				AddError(TEXT("AttackSegments[0] must be one socket-sphere region: empty StartSocketName and EndSocketName=IG_Kinsect from DA_IG_Combat."));
			}
		}
	}

	if (!DanceFromDashTuning.IsValid() || !DanceFromMissTuning.IsValid())
	{
		AddError(TEXT("Both Dance entries require positive finite preparation/trajectory tuning and a descending finite flight endpoint; legacy movement requires an explicit opt-out."));
	}
	if (!FMath::IsFinite(MissGravityMagnitude) || MissGravityMagnitude <= 0.0f)
	{
		AddError(TEXT("MissGravityMagnitude must be finite and positive (captured default: 3096 cm/s^2)."));
	}
	if (!HasRequiredSections(AttackMontage))
	{
		AddError(TEXT("AttackMontage must contain Hover186, Dash187 and terminal Miss188 sections."));
	}
	if (!HasRequiredDanceMontage())
	{
		AddError(TEXT("DanceMontage must contain the single HitDance189 section and a non-negative blend-in time."));
	}

	return bInvalid ? EDataValidationResult::Invalid
		: (Result == EDataValidationResult::NotValidated
			? EDataValidationResult::Valid : Result);
}
#endif
