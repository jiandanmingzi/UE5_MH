// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZAwakenedKinsectAttackAbility.h"

#include "ActionSystem/AbilityTask_MHGZWeaponMovement.h"
#include "ActionSystem/AbilityTask_MHGZPlayMontageAndWait.h"
#include "ActionSystem/AnimNotify_IG_AwakenedKinsectEvent.h"
#include "ActionSystem/MHGZDamageGameplayEffect.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "AttributeSystem/Res_InsectGlaive.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InsectGlaive/InsectGlaiveCombatConfig.h"
#include "InsectGlaive/Kinsect/InsectGlaiveKinsectData.h"
#include "InsectGlaive/Kinsect/Kinsect.h"
#include "InputSystem/MHGZWeaponInputRouterComponent.h"
#include "UI/MHGZAimComponent.h"
#include "MHGZCharacter.h"
#include "MHGZPlayerController.h"
#include "MHGZ.h"
#include "Components/SkeletalMeshComponent.h"
#include "Curves/CurveVector.h"
#include "TimerManager.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

namespace
{
// The ordinary miss range matches the current 199 hunter-flight reference:
// 4826 cm/s * 0.38 s = 1833.88 cm. The Montage-authored launch-to-takeoff
// interval is 0.88397956 s, so ordinary speed is 1833.88 / 0.88397956.
// Both branch speeds are hardcoded; runtime only selects one. Keep these
// action-specific speeds out of DA_IG_Combat.
constexpr float AwakenedKinsectOrdinaryFlightSpeed = 2074.573f;
constexpr float AwakenedKinsectTripleUpFlightSpeed = 3111.8595f;
constexpr int32 AwakenedKinsectMaximumHoldLoopPlays = 3;
// Match the shared in-action correction profile used by aerial attacks:
// a short EaseOut turn, rather than a one-frame facing snap.
constexpr float AwakenedFacingCorrectionDurationSeconds = 0.1f;

const FGameplayTag& AwakenedKinsectInputTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Input.Weapon.RTYB"));
	return Tag;
}

const FGameplayTag& TripleUpCostTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Cost.IG.TripleUp"));
	return Tag;
}

const FGameplayTag& AwakenedRedExtractTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("WeaponResource.IG.Extract.Red"));
	return Tag;
}

const FGameplayTag& AwakenedWhiteExtractTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("WeaponResource.IG.Extract.White"));
	return Tag;
}

const FGameplayTag& AwakenedOrangeExtractTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("WeaponResource.IG.Extract.Orange"));
	return Tag;
}

const FGameplayTag& YControlTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Input.Weapon.Y"));
	return Tag;
}

const FGameplayTag& BControlTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Input.Weapon.B"));
	return Tag;
}

const FGameplayTag& AwakenedFallingStyleTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Aerial.Falling.IG_AwakenedKinsectAttack"));
	return Tag;
}

const FGameplayTag& AwakenedKinsectHitSourceTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Aerial.Source.IG_AwakenedKinsectAttack.Hit"));
	return Tag;
}

bool IsFiniteVector(const FVector& Value)
{
	return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y)
		&& FMath::IsFinite(Value.Z);
}

bool HasRealGroundTarget(const ACharacter& Hunter, UCharacterMovementComponent& CMC,
	const FWeaponAimSnapshot& Snapshot)
{
	if (!Snapshot.bHasBlockingHit || Snapshot.bHasHitResult || !CMC.IsWalkable(Snapshot.BlockingHit))
	{
		return false;
	}
	FFindFloorResult Floor;
	CMC.FindFloor(Hunter.GetActorLocation(), Floor, false);
	return Floor.IsWalkableFloor() && Floor.FloorDist <= UCharacterMovementComponent::MAX_FLOOR_DIST;
}

USkeletalMeshComponent* FindAwakenedKinsectWeaponMesh(
	const ACharacter* Character, const FName RequiredSocket)
{
	if (!Character || RequiredSocket.IsNone())
	{
		return nullptr;
	}

	TArray<USkeletalMeshComponent*> MeshComponents;
	Character->GetComponents<USkeletalMeshComponent>(MeshComponents);
	for (USkeletalMeshComponent* Mesh : MeshComponents)
	{
		if (Mesh && Mesh->DoesSocketExist(RequiredSocket))
		{
			return Mesh;
		}
	}
	return nullptr;
}
}

UMHGZAwakenedKinsectAttackAbility::UMHGZAwakenedKinsectAttackAbility()
{
	InputTag = AwakenedKinsectInputTag();
	DefaultEntrySection = StartupSection;
	HoldReleaseControls = {
		FGameplayTag::RequestGameplayTag(TEXT("Input.Weapon.Y")),
		FGameplayTag::RequestGameplayTag(TEXT("Input.Weapon.B"))
	};
	// RT+Y+B faces toward the frozen Action camera aim; correction is capped at 90 degrees.
	MaxCorrectionAngle = 90.0f;
}

void UMHGZAwakenedKinsectAttackAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	bEmpoweredKinsectBranch = false;
	ConsumedExtractSnapshot.Reset();
	bKinsectLaunchCommitted = false;
	bKinsectActionAttached = false;
	bHoldReleaseCheckScheduled = false;
	bHoldReleasePending = false;
	bHoldReleaseRouteApplied = false;
	bHunterFlightStarted = false;
	bHunterFallTransitionCommitted = false;
	HoldLoopPlayCount = 0;
	HunterFlightMovementTask = nullptr;
	LockedKinsectDirection = FVector::ForwardVector;
	LockedHunterFlightDirection = FVector::ForwardVector;
	ReleaseActorForward = FVector::ForwardVector;
	ReleaseAimSnapshot = FWeaponAimSnapshot();
	LockedHunterTargetPoint = FVector::ZeroVector;
	bHasReleaseAimSnapshot = false;
	bReleaseAimLocked = false;
	bReleaseFacingCorrectionStarted = false;
	AttackMontageInstanceID = INDEX_NONE;
	DanceMontageInstanceID = INDEX_NONE;
	SuccessfulHitSourceMontageInstanceID = INDEX_NONE;
	SuccessfulHitSourcePosition = 0.0f;
	SuccessfulHunterHit = FHitResult();
	bDanceTransitionPending = false;
	bDanceEntryCommitted = false;
	bDanceMontageFinished = false;
	bDanceMovementFinished = false;
	bDanceLanded = false;
	bEndingHunterFlightForDance = false;
	bEndingDanceRuntime = false;
	DancePreparationMovementTask = nullptr;
	DanceMovementTask = nullptr;
	DanceMontageTask = nullptr;
	ActiveDanceMontage = nullptr;
	ClearDanceTimers();
	AwakenedAttackInstanceID.Invalidate();
	PendingKinsectRequest = FKinsectFlightRequest();
#if WITH_DEV_AUTOMATION_TESTS
	TestKinsectFlightDurationSeconds = -1.0f;
#endif
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}

void UMHGZAwakenedKinsectAttackAbility::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	bHoldReleaseCheckScheduled = false;
	bHoldReleasePending = false;
	ClearDanceTimers();
	CleanupDanceTasks();
	UnbindAttackMontageSectionChanged();
	const bool bNeedsFallbackFalling = bHunterFlightStarted
		&& !bHunterFallTransitionCommitted;
	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	RecallKinsectForThisAction();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
	StartFallbackFallingAfterActionEnd(Host, bNeedsFallbackFalling);
	bEmpoweredKinsectBranch = false;
	ConsumedExtractSnapshot.Reset();
	bKinsectLaunchCommitted = false;
	bKinsectActionAttached = false;
	bHoldReleaseRouteApplied = false;
	bHunterFlightStarted = false;
	bHunterFallTransitionCommitted = false;
	HoldLoopPlayCount = 0;
	HunterFlightMovementTask = nullptr;
	LockedKinsectDirection = FVector::ForwardVector;
	LockedHunterFlightDirection = FVector::ForwardVector;
	ReleaseActorForward = FVector::ForwardVector;
	ReleaseAimSnapshot = FWeaponAimSnapshot();
	LockedHunterTargetPoint = FVector::ZeroVector;
	bHasReleaseAimSnapshot = false;
	bReleaseAimLocked = false;
	bReleaseFacingCorrectionStarted = false;
	AttackMontageInstanceID = INDEX_NONE;
	DanceMontageInstanceID = INDEX_NONE;
	SuccessfulHitSourceMontageInstanceID = INDEX_NONE;
	SuccessfulHitSourcePosition = 0.0f;
	SuccessfulHunterHit = FHitResult();
	bDanceTransitionPending = false;
	bDanceEntryCommitted = false;
	bDanceMontageFinished = false;
	bDanceMovementFinished = false;
	bDanceLanded = false;
	bEndingHunterFlightForDance = false;
	bEndingDanceRuntime = false;
	DancePreparationMovementTask = nullptr;
	DanceMovementTask = nullptr;
	DanceMontageTask = nullptr;
	ActiveDanceMontage = nullptr;
	AwakenedAttackInstanceID.Invalidate();
	PendingKinsectRequest = FKinsectFlightRequest();
#if WITH_DEV_AUTOMATION_TESTS
	TestKinsectFlightDurationSeconds = -1.0f;
#endif
}

void UMHGZAwakenedKinsectAttackAbility::HandleInputReleased(
	const FWeaponInputSnapshot& Snapshot)
{
	if (!IsActive() || !IsActionActivationCommitted()
		|| Snapshot.Phase != EWeaponInputPhase::Completed
		|| Snapshot.ResolvedInputTag != InputTag)
	{
		return;
	}
	if (bKinsectLaunchCommitted)
	{
		return;
	}

	// The Router's Completed event intentionally preserves the press-time Aim.
	// Awakening is different: the release frame decides both the hunter launch
	// and the kinsect throw, so capture the camera ray synchronously here.
	CaptureReleaseAimSnapshot();

	// Physical controls are removed from HeldControls after Completed is broadcast;
	// query on the next tick so releasing only Y or only B does not release the hold.
	bHoldReleasePending = true;
	ScheduleHoldReleaseCheck();
}

bool UMHGZAwakenedKinsectAttackAbility::ValidateActionDependencies() const
{
	const FWeaponInputSnapshot& Input = GetWeaponActivationContext().Input;
	FVector ValidatedAimDirection;
	if (!Super::ValidateActionDependencies()
		|| !InputTag.MatchesTagExact(AwakenedKinsectInputTag())
		|| !GetIGResourceComponent()
		|| Input.Aim.Context != EWeaponAimSnapshotContext::Action
		|| !IsFiniteVector(Input.Aim.TargetPoint)
		|| !ResolveLockedAimDirection(Input.ActorForward, Input.Aim.Direction,
			GetIGResourceComponent()->GetCombatConfig()
				? GetIGResourceComponent()->GetCombatConfig()->AwakenedAimHorizontalCorrectionAngle
				: -1.0f,
			GetIGResourceComponent()->GetCombatConfig()
				? GetIGResourceComponent()->GetCombatConfig()->AwakenedAimCorrectionAngle
				: -1.0f,
			ValidatedAimDirection)
		|| !AttackMontage
		|| !DanceMontage
		|| DanceSectionName.IsNone()
		|| !DanceMontage->IsValidSectionName(DanceSectionName)
		|| DanceMontage->CompositeSections.Num() != 1
		|| DanceMontage->GetSkeleton() != AttackMontage->GetSkeleton()
		|| !FMath::IsFinite(DanceMontageBlendInTime)
		|| DanceMontageBlendInTime < 0.0f
		|| !DanceFromDashTuning.IsValid()
		|| AttackSegments.Num() != 1
		|| !AttackSegments[0].Damage.DamageEffectClass
		|| !AttackSegments[0].Damage.DamageEffectClass->IsChildOf(
			UMHGZDamageGameplayEffect::StaticClass())
		|| AttackSegments[0].Damage.MotionValue.GetValueAtLevel(1.0f) <= 0.0f
		|| !AttackSegments[0].Damage.bUseHitzoneDefense
		|| AttackSegments[0].Damage.bRequiresHitToContinue
		|| AttackSegments[0].MultiHitCount != 1
		|| AttackSegments[0].MultiHitPolicy != EAttackMultiHitPolicy::ContactOnly
		|| AttackSegments[0].Collision.TraceRegions.Num() != 1
		|| AttackSegments[0].Collision.TraceRegions[0].StartSocketName != FName(TEXT("IG_RearTip"))
		|| AttackSegments[0].Collision.TraceRegions[0].EndSocketName != FName(TEXT("Root"))
		|| HoldReleaseControls.Num() != 2
		|| DefaultEntrySection != StartupSection
		|| !AttackMontage->IsValidSectionName(StartupSection)
		|| !AttackMontage->IsValidSectionName(HoldLoopSection)
		|| !AttackMontage->IsValidSectionName(SendSection)
		|| !AttackMontage->IsValidSectionName(PreFlightSection)
		|| !AttackMontage->IsValidSectionName(HunterFlightSection)
		|| !WeaponResourceCosts.IsEmpty())
	{
		return false;
	}

	const URes_InsectGlaive* Resource = GetIGResourceComponent();
	const UInsectGlaiveCombatConfig* Config = Resource->GetCombatConfig();
	const UInsectGlaiveKinsectData* KinsectData = Resource->GetKinsectData();
	const AKinsect* Kinsect = Resource->GetKinsectActor();
	const AMHGZCharacter* Character = Cast<AMHGZCharacter>(GetAvatarActorFromActorInfo());
	if (!Config || !KinsectData || !Kinsect
		|| !Character || Config->KinsectSlashAttachSocket.IsNone()
		|| !FindAwakenedKinsectWeaponMesh(Character, Config->KinsectSlashAttachSocket)
		|| !FMath::IsFinite(Config->AwakenedAimHorizontalCorrectionAngle)
		|| Config->AwakenedAimHorizontalCorrectionAngle < 0.f
		|| Config->AwakenedAimHorizontalCorrectionAngle > 180.f
		|| !FMath::IsFinite(Config->AwakenedAimCorrectionAngle)
		|| Config->AwakenedAimCorrectionAngle < 0.f
		|| Config->AwakenedAimCorrectionAngle > 90.f
		|| !FMath::IsFinite(Config->AwakenedKinsectMotionValue)
		|| Config->AwakenedKinsectMotionValue < 0.f
		|| !FMath::IsFinite(Config->AwakenedRedKinsectMotionValue)
		|| Config->AwakenedRedKinsectMotionValue < Config->AwakenedKinsectMotionValue
		|| !FMath::IsFinite(Config->AwakenedTripleKinsectMotionValue)
		|| Config->AwakenedTripleKinsectMotionValue < Config->AwakenedRedKinsectMotionValue
		|| !FMath::IsFinite(Config->AwakenedHunterFlightDurationSeconds)
		|| Config->AwakenedHunterFlightDurationSeconds <= 0.f
		|| !FMath::IsFinite(Config->AwakenedHunterFlightSpeed)
		|| Config->AwakenedHunterFlightSpeed <= 0.f
		|| !FMath::IsFinite(Config->AwakenedHunterFlightGravityMagnitude)
		|| Config->AwakenedHunterFlightGravityMagnitude <= 0.f
		|| !FMath::IsFinite(Config->AwakenedGroundTargetMinimumDistance)
		|| Config->AwakenedGroundTargetMinimumDistance <= 0.f
		|| !FMath::IsFinite(Config->AwakenedGroundTargetVerticalLaunchSpeed)
		|| Config->AwakenedGroundTargetVerticalLaunchSpeed <= 0.f
		|| !FMath::IsFinite(Config->AwakenedHunterFallGravityMagnitude)
		|| Config->AwakenedHunterFallGravityMagnitude <= 0.f
		|| !IsFiniteVector(Config->AwakenedHunterFallHandoffVelocity)
		|| Config->AwakenedHunterFallHandoffVelocity.Z <= 0.f
		|| !Config->AwakenedHunterFallMontage
		|| !FMath::IsFinite(Config->AwakenedPierceHitInterval)
		|| Config->AwakenedPierceHitInterval <= 0.f
		|| !FMath::IsFinite(Config->KinsectArrivalRadius)
		|| Config->KinsectArrivalRadius <= 0.f
		|| Kinsect->GetState() == EKinsectState::ActionAttached)
	{
		return false;
	}

	const AMHGZPlayerController* Controller = Character
		? Cast<AMHGZPlayerController>(Character->GetController()) : nullptr;
	const UMHGZWeaponInputRouterComponent* Router = Controller
		? Controller->GetWeaponInputRouter() : nullptr;
	if (!Router)
	{
		return false;
	}
	if (!HoldReleaseControls.Contains(YControlTag())
		|| !HoldReleaseControls.Contains(BControlTag()))
	{
		return false;
	}

	for (const FGameplayTag& Control : HoldReleaseControls)
	{
		if (!Control.IsValid())
		{
			return false;
		}
	}
	return true;
}

bool UMHGZAwakenedKinsectAttackAbility::PrepareActivationWeaponResourceCosts(
	TArray<FWeaponResourceCostSpec>& OutCosts)
{
	OutCosts.Reset();
	bEmpoweredKinsectBranch = false;
	ConsumedExtractSnapshot.Reset();
	URes_InsectGlaive* Resource = GetIGResourceComponent();
	if (!Resource)
	{
		return false;
	}

	bEmpoweredKinsectBranch = Resource->IsTripleUpActive();
	for (const FGameplayTag& Color : { AwakenedRedExtractTag(), AwakenedWhiteExtractTag(), AwakenedOrangeExtractTag() })
	{
		if (bEmpoweredKinsectBranch || Resource->HasExtract(Color))
		{
			ConsumedExtractSnapshot.AddTag(Color);
		}
	}
	if (!bEmpoweredKinsectBranch)
	{
		// Preflight only previews the colors. Actual successful consumption below
		// replaces this snapshot, so later resource changes cannot rewrite launch effects.
		return BuildPendingKinsectRequest(GetWeaponActivationContext().Input);
	}

	const FGameplayTag CostTag = TripleUpCostTag();
	if (!CostTag.IsValid())
	{
		return false;
	}

	FWeaponResourceCostSpec TripleCost;
	TripleCost.CostType = CostTag;
	TripleCost.Amount.Value = 1.0f;
	OutCosts.Add(TripleCost);
	bEmpoweredKinsectBranch = true;
	return BuildPendingKinsectRequest(GetWeaponActivationContext().Input);
}

bool UMHGZAwakenedKinsectAttackAbility::PrepareAttackMontage()
{
	if (!Super::PrepareAttackMontage())
	{
		return false;
	}

	URes_InsectGlaive* Resource = GetIGResourceComponent();
	const UInsectGlaiveCombatConfig* Config = Resource
		? Resource->GetCombatConfig() : nullptr;
	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	USkeletalMeshComponent* WeaponMesh = Config
		? FindAwakenedKinsectWeaponMesh(Character, Config->KinsectSlashAttachSocket)
		: nullptr;
	if (!Resource || !Config || !WeaponMesh
		|| !Resource->BeginKinsectActionAttachment(GetActionToken(), WeaponMesh,
			Config->KinsectSlashAttachSocket))
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[AwakenedKinsect] Could not attach the real Kinsect to the configured weapon socket before montage playback."));
		return false;
	}

	bKinsectActionAttached = true;
	FGameplayTagContainer ConsumedSingles;
	if (!Resource->ConsumeSingleExtractsForAction(GetActionToken(), &ConsumedSingles))
	{
		return false;
	}
	if (!bEmpoweredKinsectBranch)
	{
		ConsumedExtractSnapshot = ConsumedSingles;
	}
	ApplyConsumedExtractEffects(*Config);
	return true;
}

bool UMHGZAwakenedKinsectAttackAbility::BuildPendingKinsectRequest(
	const FWeaponInputSnapshot& Input)
{
	URes_InsectGlaive* Resource = GetIGResourceComponent();
	const UInsectGlaiveCombatConfig* Config = Resource ? Resource->GetCombatConfig() : nullptr;
	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	FVector InitialAimDirection;
	if (!Resource || !Config || !Resource->GetKinsectData() || !Host
		|| Input.Aim.Context != EWeaponAimSnapshotContext::Action
		|| !IsFiniteVector(Input.Aim.TargetPoint)
		|| !ResolveLockedAimDirection(Input.ActorForward, Input.Aim.Direction,
			Config->AwakenedAimHorizontalCorrectionAngle,
			Config->AwakenedAimCorrectionAngle, InitialAimDirection))
	{
		return false;
	}

	LockedHunterTargetPoint = Input.Aim.TargetPoint;
	AwakenedAttackInstanceID = FGuid::NewGuid();
	if (!AwakenedAttackInstanceID.IsValid())
	{
		return false;
	}

	PendingKinsectRequest = FKinsectFlightRequest();
	PendingKinsectRequest.RuntimeToken = Host->GetCurrentToken();
	PendingKinsectRequest.TrajectoryMode = EKinsectTrajectoryMode::AlongDirection;
	// Temporary activation-time direction only validates the request. The live
	// release-frame camera ray replaces it before the KinsectLaunch notify commits.
	PendingKinsectRequest.DirectionSnapshot = InitialAimDirection;
	// Request validation only needs a positive placeholder here. The actual
	// distance is derived from the persistent Montage's authored event interval
	// immediately before the KinsectLaunch transaction commits.
	PendingKinsectRequest.MaxDistance = 1.0f;
	PendingKinsectRequest.FlightSpeed = bEmpoweredKinsectBranch
		? AwakenedKinsectTripleUpFlightSpeed
		: AwakenedKinsectOrdinaryFlightSpeed;
	PendingKinsectRequest.ArrivalRadius = Config->KinsectArrivalRadius;
	PendingKinsectRequest.FlightInstanceID = FGuid::NewGuid();
	if (!PendingKinsectRequest.FlightInstanceID.IsValid())
	{
		return false;
	}

	ApplyConsumedExtractEffects(*Config);

	return Resource->CanDeployKinsect(PendingKinsectRequest);
}

void UMHGZAwakenedKinsectAttackAbility::ApplyConsumedExtractEffects(
	const UInsectGlaiveCombatConfig& Config)
{
	const bool bRed = ConsumedExtractSnapshot.HasTagExact(AwakenedRedExtractTag());
	const bool bWhite = ConsumedExtractSnapshot.HasTagExact(AwakenedWhiteExtractTag());
	const bool bOrange = ConsumedExtractSnapshot.HasTagExact(AwakenedOrangeExtractTag());
	PendingKinsectRequest.MotionValue = bEmpoweredKinsectBranch
		? Config.AwakenedTripleKinsectMotionValue
		: (bRed ? Config.AwakenedRedKinsectMotionValue : Config.AwakenedKinsectMotionValue);
	PendingKinsectRequest.DamageMode = bWhite ? EKinsectDamageMode::Piercing : EKinsectDamageMode::SingleHit;
	PendingKinsectRequest.ExtractMode = bWhite ? EKinsectExtractMode::CollectPerValidHit : EKinsectExtractMode::FirstHitOnly;
	PendingKinsectRequest.RehitInterval = bWhite ? Config.AwakenedPierceHitInterval : 0.0f;
	PendingKinsectRequest.PowderSpawnPolicy = bOrange
		? EIGPowderSpawnPolicy::AwakenedEachHit : EIGPowderSpawnPolicy::MarkedContact;
	// Piercing and powder are independent from travel speed and endpoint recall.
	// Non-Triple requests still hover until the hunter's flight-ending recall.
	PendingKinsectRequest.PostFlightPolicy = bEmpoweredKinsectBranch
		? EKinsectPostFlightPolicy::ReturnToOwner : EKinsectPostFlightPolicy::Hover;
	PendingKinsectRequest.SourceAttackInstanceID = AwakenedAttackInstanceID;
}

bool UMHGZAwakenedKinsectAttackAbility::ResolveLockedAimDirection(
	const FVector& ActorForward, const FVector& AimDirection,
	const float MaxYawAngleDegrees, const float MaxPitchAngleDegrees,
	FVector& OutLockedDirection)
{
	OutLockedDirection = FVector::ZeroVector;
	if (!IsFiniteVector(ActorForward) || !IsFiniteVector(AimDirection)
		|| !FMath::IsFinite(MaxYawAngleDegrees)
		|| MaxYawAngleDegrees < 0.f || MaxYawAngleDegrees > 180.f
		|| !FMath::IsFinite(MaxPitchAngleDegrees)
		|| MaxPitchAngleDegrees < 0.f || MaxPitchAngleDegrees > 90.f)
	{
		return false;
	}

	const FVector Forward2D(ActorForward.X, ActorForward.Y, 0.f);
	const FVector NormalizedForward = Forward2D.GetSafeNormal();
	const FVector NormalizedAim = AimDirection.GetSafeNormal();
	if (NormalizedForward.IsNearlyZero() || NormalizedAim.IsNearlyZero())
	{
		return false;
	}

	const FVector Up = FVector::UpVector;
	const FVector Right = FVector::CrossProduct(Up, NormalizedForward).GetSafeNormal();
	const float YawDegrees = FMath::RadiansToDegrees(FMath::Atan2(
		FVector::DotProduct(NormalizedAim, Right),
		FVector::DotProduct(NormalizedAim, NormalizedForward)));
	const float HorizontalMagnitude = FVector(NormalizedAim.X,
		NormalizedAim.Y, 0.f).Size();
	const float PitchDegrees = FMath::RadiansToDegrees(FMath::Atan2(
		NormalizedAim.Z, HorizontalMagnitude));
	const float ClampedYaw = FMath::Clamp(YawDegrees,
		-MaxYawAngleDegrees, MaxYawAngleDegrees);
	const float ClampedPitch = FMath::Clamp(PitchDegrees,
		-MaxPitchAngleDegrees, MaxPitchAngleDegrees);

	const FVector ClampedHorizontal = FQuat(Up,
		FMath::DegreesToRadians(ClampedYaw)).RotateVector(NormalizedForward);
	OutLockedDirection = (ClampedHorizontal * FMath::Cos(
		FMath::DegreesToRadians(ClampedPitch))
		+ Up * FMath::Sin(FMath::DegreesToRadians(ClampedPitch))).GetSafeNormal();
	return !OutLockedDirection.IsNearlyZero() && IsFiniteVector(OutLockedDirection);
}

bool UMHGZAwakenedKinsectAttackAbility::ResolveLockedTargetDirection(
	const FVector& ActorForward, const FVector& TargetPoint, const FVector& LaunchOrigin,
	const FVector& FallbackAimDirection, const float MaxYawAngleDegrees,
	const float MaxPitchAngleDegrees, FVector& OutLockedDirection)
{
	if (!IsFiniteVector(TargetPoint) || !IsFiniteVector(LaunchOrigin))
	{
		OutLockedDirection = FVector::ZeroVector;
		return false;
	}
	const FVector ToTarget = (TargetPoint - LaunchOrigin).GetSafeNormal();
	return ResolveLockedAimDirection(ActorForward,
		ToTarget.IsNearlyZero() ? FallbackAimDirection : ToTarget,
		MaxYawAngleDegrees, MaxPitchAngleDegrees, OutLockedDirection);
}

bool UMHGZAwakenedKinsectAttackAbility::GetAuthoredKinsectFlightDuration(
	const UAnimMontage& Montage, float& OutDurationSeconds)
{
	OutDurationSeconds = 0.0f;
	float KinsectLaunchTriggerTime = 0.0f;
	float HunterFlightStartTriggerTime = 0.0f;
	int32 KinsectLaunchCount = 0;
	int32 HunterFlightStartCount = 0;
	for (const FAnimNotifyEvent& NotifyEvent : Montage.Notifies)
	{
		const UAnimNotify_IG_AwakenedKinsectEvent* Notify =
			Cast<UAnimNotify_IG_AwakenedKinsectEvent>(NotifyEvent.Notify);
		if (!Notify)
		{
			continue;
		}
		switch (Notify->Event)
		{
		case EAwakenedKinsectAttackEvent::KinsectLaunch:
			++KinsectLaunchCount;
			KinsectLaunchTriggerTime = NotifyEvent.GetTriggerTime();
			break;
		case EAwakenedKinsectAttackEvent::HunterFlightStart:
			++HunterFlightStartCount;
			HunterFlightStartTriggerTime = NotifyEvent.GetTriggerTime();
			break;
		default:
			break;
		}
	}

	if (KinsectLaunchCount != 1 || HunterFlightStartCount != 1)
	{
		return false;
	}
	OutDurationSeconds = HunterFlightStartTriggerTime - KinsectLaunchTriggerTime;
	return FMath::IsFinite(OutDurationSeconds) && OutDurationSeconds > 0.0f;
}

bool UMHGZAwakenedKinsectAttackAbility::BuildAwakenedKinsectFlightProfile(
	const float FlightDurationSeconds, const bool bEmpowered,
	float& OutMaxDistance, float& OutFlightSpeed)
{
	OutMaxDistance = 0.0f;
	OutFlightSpeed = 0.0f;
	if (!FMath::IsFinite(FlightDurationSeconds) || FlightDurationSeconds <= 0.0f)
	{
		return false;
	}

	OutFlightSpeed = bEmpowered
		? AwakenedKinsectTripleUpFlightSpeed
		: AwakenedKinsectOrdinaryFlightSpeed;
	OutMaxDistance = OutFlightSpeed * FlightDurationSeconds;
	return FMath::IsFinite(OutMaxDistance) && OutMaxDistance > 0.0f
		&& FMath::IsFinite(OutFlightSpeed) && OutFlightSpeed > 0.0f;
}

bool UMHGZAwakenedKinsectAttackAbility::BuildGroundTargetHunterFlightMovementRequest(
	const FVector& CorrectedForward, const FVector& LaunchOrigin, const FVector& GroundTarget,
	const float Speed, const float Duration, const float GravityMagnitude,
	const float MinimumDistance, const float VerticalLaunchSpeed, FWeaponMovementRequest& OutRequest)
{
	OutRequest = FWeaponMovementRequest();
	const FVector Forward = CorrectedForward.GetSafeNormal2D();
	if (!IsFiniteVector(CorrectedForward) || Forward.IsNearlyZero()
		|| !IsFiniteVector(LaunchOrigin) || !IsFiniteVector(GroundTarget)
		|| !FMath::IsFinite(Speed) || Speed <= 0.0f
		|| !FMath::IsFinite(Duration) || Duration <= 0.0f
		|| !FMath::IsFinite(MinimumDistance) || MinimumDistance <= 0.0f
		|| MinimumDistance > Speed * Duration
		|| !FMath::IsFinite(VerticalLaunchSpeed) || VerticalLaunchSpeed <= 0.0f)
	{
		return false;
	}
	// Captures support constant horizontal motion for the complete 199 span and
	// a shallow arc on low-ground shots, not an angle-triggered speed switch.
	// They do not record the native reticle impact point: distance mapping is an
	// explicit demo reconstruction, not a claimed reverse-engineered native formula.
	const float Distance = FMath::Clamp(static_cast<float>(FVector::Dist2D(LaunchOrigin, GroundTarget)),
		MinimumDistance, Speed * Duration);
	const FVector LaunchVelocity = Forward * (Distance / Duration)
		+ FVector::UpVector * VerticalLaunchSpeed;
	return BuildHunterFlightMovementRequest(LaunchVelocity.GetSafeNormal(), LaunchVelocity.Size(),
		Duration, GravityMagnitude, OutRequest);
}

bool UMHGZAwakenedKinsectAttackAbility::BuildHunterFlightMovementRequest(
	const FVector& LockedDirection, const float Speed, const float Duration,
	const float GravityMagnitude, FWeaponMovementRequest& OutRequest)
{
	OutRequest = FWeaponMovementRequest();
	if (!IsFiniteVector(LockedDirection) || LockedDirection.IsNearlyZero()
		|| !FMath::IsFinite(Speed) || Speed <= 0.0f
		|| !FMath::IsFinite(Duration) || Duration <= 0.0f
		|| !FMath::IsFinite(GravityMagnitude) || GravityMagnitude <= 0.0f)
	{
		return false;
	}

	OutRequest.Mode = EWeaponMovementMode::RecordedLocalTrajectory;
	OutRequest.DirectionSnapshot = LockedDirection.GetSafeNormal();
	OutRequest.LaunchVelocity = OutRequest.DirectionSnapshot * Speed;
	if (OutRequest.DirectionSnapshot.GetSafeNormal2D().IsNearlyZero())
	{
		// A vertical launch has no meaningful yaw. The local path is Z-only, so
		// any planar basis preserves the same world trajectory (no aim change).
		OutRequest.DirectionSnapshot = FVector::ForwardVector;
	}
	OutRequest.Duration = Duration;
	OutRequest.GravityMagnitude = GravityMagnitude;
	OutRequest.PlanarAcceleration = FVector::ZeroVector;
	OutRequest.CancelVelocityPolicy = EMovementCancelVelocityPolicy::PreserveVelocity;
	// 20261012 F9: even floor-constrained 199 episodes retain the full 0.375–0.383s
	// phase. Ordinary Falling instead ends a near-horizontal launch on its first
	// floor contact. Reuse the protected swept-curve lease, not a no-collision move.
	// Local X is locked planar forward, Z is WORLD up. Two Hermite keys reproduce
	// p(t)=v0*t - Up*g*t*t/2 exactly, without sample noise or a zero-entry hold.
	const FVector LocalVelocity(OutRequest.LaunchVelocity.Size2D(), 0.0,
		OutRequest.LaunchVelocity.Z);
	const FVector LocalAcceleration(0.0, 0.0, -GravityMagnitude);
	UCurveVector* Curve = NewObject<UCurveVector>(GetTransientPackage());
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		FRichCurve& Component = Curve->FloatCurves[Axis];
		const float V0 = LocalVelocity[Axis];
		const float Acceleration = LocalAcceleration[Axis];
		const FKeyHandle Start = Component.AddKey(0.0f, 0.0f);
		const FKeyHandle End = Component.AddKey(Duration,
			V0 * Duration + 0.5f * Acceleration * Duration * Duration);
		Component.SetKeyInterpMode(Start, RCIM_Cubic);
		Component.SetKeyInterpMode(End, RCIM_Cubic);
		Component.SetKeyTangentMode(Start, RCTM_User);
		Component.SetKeyTangentMode(End, RCTM_User);
		Component.GetKey(Start).ArriveTangent = V0;
		Component.GetKey(Start).LeaveTangent = V0;
		Component.GetKey(End).ArriveTangent = V0 + Acceleration * Duration;
		Component.GetKey(End).LeaveTangent = V0 + Acceleration * Duration;
	}
	OutRequest.RecordedTrajectory.Curve = Curve;
	OutRequest.RecordedTrajectory.CoveredSeconds = Duration;
	OutRequest.RecordedTrajectory.TerminalVelocityLocal =
		LocalVelocity + LocalAcceleration * Duration;
	OutRequest.RecordedTrajectory.TerminalAccelerationLocal = LocalAcceleration;
	OutRequest.TrajectoryGroundPolicy = ERecordedTrajectoryGroundPolicy::ProtectedFlight;
	return OutRequest.HasValidRecordedTrajectoryParameters();
}

bool UMHGZAwakenedKinsectAttackAbility::StartAttackMontage(
	ACharacter& Character, UAnimMontage* Montage, FName StartSection)
{
	if (!Super::StartAttackMontage(Character, Montage, StartSection))
	{
		return false;
	}
	if (!Configure193HoldLoop())
	{
		return false;
	}

	UAnimInstance* AnimInstance = GetAttackAnimInstance();
	FAnimMontageInstance* MontageInstance = AnimInstance
		? AnimInstance->GetActiveInstanceForMontage(Montage) : nullptr;
	if (!MontageInstance)
	{
		return false;
	}

	AttackMontageInstanceID = MontageInstance->GetInstanceID();
	MontageInstance->OnMontageSectionChanged.BindUObject(this,
		&UMHGZAwakenedKinsectAttackAbility::OnAttackMontageSectionChanged);
	return AttackMontageInstanceID != INDEX_NONE;
}

void UMHGZAwakenedKinsectAttackAbility::ApplyActivationDirectionCorrection()
{
	// RT+Y+B is aimed by the camera, not the left stick. Use the same direct-yaw
	// clamp/tolerance algorithm as ordinary attacks, but source it from the
	// activation-time Action Aim snapshot and this GA's 90-degree cap.
	const FWeaponAbilityActivationContext& Context = GetWeaponActivationContext();
	if (Context.Input.Aim.Context != EWeaponAimSnapshotContext::Action
		|| !IsFiniteVector(Context.Input.Aim.Direction)
		|| Context.Input.Aim.Direction.IsNearlyZero())
	{
		return;
	}

	ApplyActivationDirectionCorrectionFromWorldDirection(
		Context.Input.Aim.Direction,
		ResolveActivationMaxCorrectionAngle(Context));
}

bool UMHGZAwakenedKinsectAttackAbility::HandleAwakenedKinsectEvent(
	const FWeaponActionToken& ActionToken,
	const EAwakenedKinsectAttackEvent Event)
{
	if (ActionToken != GetActionToken())
	{
		return false;
	}

	switch (Event)
	{
	case EAwakenedKinsectAttackEvent::KinsectLaunch:
		return CommitKinsectLaunch(ActionToken);
	case EAwakenedKinsectAttackEvent::HunterFlightStart:
		return StartHunterFlight(ActionToken);
	case EAwakenedKinsectAttackEvent::ReleaseFacingCorrection:
		return ApplyReleaseFacingCorrection();
	default:
		return false;
	}
}

bool UMHGZAwakenedKinsectAttackAbility::IsHunterFlightDamageWindowOpen(
	const bool bActionActive, const bool bExactAttackMontageInstance,
	const bool bInHunterFlight, const bool bDancePending,
	const bool bDanceCommitted, const int32 SegmentIndex,
	const FName CurrentSection, const FName ExpectedSection)
{
	return bActionActive && bExactAttackMontageInstance && bInHunterFlight
		&& !bDancePending && !bDanceCommitted && SegmentIndex == 0
		&& !ExpectedSection.IsNone() && CurrentSection == ExpectedSection;
}

bool UMHGZAwakenedKinsectAttackAbility::CanApplyCollisionHit(
	const FHitResult& Hit, const int32 SegmentIndex) const
{
	(void)Hit;
	const UAnimInstance* AnimInstance = GetAttackAnimInstance();
	const FAnimMontageInstance* MontageInstance = AnimInstance && AttackMontage
		? AnimInstance->GetActiveInstanceForMontage(AttackMontage) : nullptr;
	const bool bExactInstance = MontageInstance
		&& MontageInstance->GetInstanceID() == AttackMontageInstanceID;
	const FName CurrentSection = AnimInstance && AttackMontage
		? AnimInstance->Montage_GetCurrentSection(AttackMontage) : NAME_None;
	const bool bBaseGate = Super::CanApplyCollisionHit(Hit, SegmentIndex);
	const bool bFlightGate = IsHunterFlightDamageWindowOpen(IsActive() && IsActionActivationCommitted(),
			bExactInstance, bHunterFlightStarted, bDanceTransitionPending,
			bDanceEntryCommitted, SegmentIndex, CurrentSection, HunterFlightSection);
	if (Hit.GetActor() && SegmentIndex == 0)
	{
		UE_LOG(LogMHGZ, Display,
			TEXT("[AwakenedHitAudit] action=%u candidate=%s baseGate=%d flightGate=%d section=%s exactInstance=%d pos=%.4f impact=%s"),
			GetActionToken().ActivationSequenceID, *GetNameSafe(Hit.GetActor()), bBaseGate, bFlightGate,
			*CurrentSection.ToString(), bExactInstance,
			AnimInstance && AttackMontage ? AnimInstance->Montage_GetPosition(AttackMontage) : -1.0f,
			*Hit.ImpactPoint.ToCompactString());
	}
	return bBaseGate && bFlightGate;
}

bool UMHGZAwakenedKinsectAttackAbility::ShouldConsumeCollisionOnDamageFailure(
	const int32 SegmentIndex) const
{
	// The first sweep can touch a target before its ASC/GE becomes available. Keep
	// the one successful-damage chance retryable throughout the authored window.
	return SegmentIndex != 0;
}

void UMHGZAwakenedKinsectAttackAbility::OnAttackCollisionWindowClosed(const int32 SegmentIndex)
{
	if (SegmentIndex != 0 || !IsActive() || !AttackSegments.IsValidIndex(0)) { return; }
	const UAnimInstance* Anim = GetAttackAnimInstance();
	const float Position = Anim && AttackMontage ? Anim->Montage_GetPosition(AttackMontage) : -1.0f;
	const FAttackCollisionConfig& CollisionConfig = AttackSegments[0].Collision;
	const USkeletalMeshComponent* TraceMesh = FindTraceMeshComponent(CollisionConfig);
	for (const FWeaponTraceRegion& Region : CollisionConfig.TraceRegions)
	{
		if (!TraceMesh || !TraceMesh->DoesSocketExist(Region.StartSocketName)
			|| !TraceMesh->DoesSocketExist(Region.EndSocketName)) { continue; }
		const FVector Start = TraceMesh->GetSocketLocation(Region.StartSocketName);
		const FVector End = TraceMesh->GetSocketLocation(Region.EndSocketName);
		const float Distance = FMath::PointDistToSegment(LockedHunterTargetPoint, Start, End);
		UE_LOG(LogMHGZ, Display,
			TEXT("[AwakenedHitAudit] action=%u windowClosed pos=%.4f dancePending=%d danceCommitted=%d targetDistanceToFinalRegion=%.2f radius=%.2f start=%s end=%s target=%s"),
			GetActionToken().ActivationSequenceID, Position, bDanceTransitionPending, bDanceEntryCommitted,
			Distance, Region.Radius, *Start.ToCompactString(), *End.ToCompactString(), *LockedHunterTargetPoint.ToCompactString());
	}
}

void UMHGZAwakenedKinsectAttackAbility::HandleSuccessfulAttackDamage(
	const FHitResult& Hit, const int32 SegmentIndex)
{
	if (SegmentIndex != 0 || bDanceTransitionPending || bDanceEntryCommitted
		|| !CanApplyCollisionHit(Hit, SegmentIndex))
	{
		return;
	}

	// 与虫印斩/四连印斩共用成功伤害后的局部建印入口。
	// 必须在199有效冲刺段、伤害GE成功之后执行；猎虫飞行命中不走此武器回调。
	if (URes_InsectGlaive* Resource = GetIGResourceComponent())
	{
		Resource->SetKinsectMarkFromMeleeHit(Hit);
	}

	SuccessfulHunterHit = Hit;
	SuccessfulHitSourceMontageInstanceID = AttackMontageInstanceID;
	const UAnimInstance* AnimInstance = GetAttackAnimInstance();
	SuccessfulHitSourcePosition = AnimInstance && AttackMontage
		? AnimInstance->Montage_GetPosition(AttackMontage) : 0.0f;
	bDanceTransitionPending = true;

	if (UWorld* World = GetWorld())
	{
		QueuedDanceTransitionTimer = World->GetTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateUObject(this,
				&UMHGZAwakenedKinsectAttackAbility::HandleQueuedDanceTransition,
				GetActionToken(), SuccessfulHitSourceMontageInstanceID));
	}
	else
	{
		bDanceTransitionPending = false;
		UE_LOG(LogMHGZ, Warning,
			TEXT("[AwakenedKinsect] GE succeeded but no World was available to queue the 189 handoff."));
	}
}

bool UMHGZAwakenedKinsectAttackAbility::StartHunterFlight(
	const FWeaponActionToken& ActionToken)
{
	if (!IsActive() || !IsActionActivationCommitted()
		|| ActionToken != GetActionToken())
	{
		return false;
	}
	// KinsectLaunch normally locks the release-frame aim first. If it was missed,
	// recover the movement direction here, but never begin facing correction late.
	if (!bReleaseAimLocked && !LockReleaseAim())
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[AwakenedKinsect] Hunter flight could not lock the saved release-frame aim (action=%u hasSnapshot=%d)."),
			ActionToken.ActivationSequenceID, bHasReleaseAimSnapshot ? 1 : 0);
		return false;
	}
	if (bHunterFlightStarted)
	{
		const UAbilityTask_MHGZWeaponMovement* ExistingTask = HunterFlightMovementTask.Get();
		return ExistingTask && ExistingTask->DidStartMovement();
	}

	URes_InsectGlaive* Resource = GetIGResourceComponent();
	const UInsectGlaiveCombatConfig* Config = Resource ? Resource->GetCombatConfig() : nullptr;
	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	UCharacterMovementComponent* CMC = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Config || !Host || !Character || !CMC
		|| LockedHunterFlightDirection.IsNearlyZero())
	{
		return false;
	}

	// Keep the release-frame world target, not a parallel camera ray. No live camera retargeting.
	if (!ResolveLockedTargetDirection(ReleaseActorForward, LockedHunterTargetPoint,
		Character->GetActorLocation(), ReleaseAimSnapshot.Direction,
		Config->AwakenedAimHorizontalCorrectionAngle, Config->AwakenedAimCorrectionAngle,
		LockedHunterFlightDirection))
	{
		return false;
	}
	FWeaponMovementRequest Request;
	const bool bGroundTarget = HasRealGroundTarget(*Character, *CMC, ReleaseAimSnapshot);
	FVector GroundForward;
	const bool bRequestBuilt = bGroundTarget
		? (ResolveLockedAimDirection(ReleaseActorForward, ReleaseAimSnapshot.Direction,
			Config->AwakenedAimHorizontalCorrectionAngle, Config->AwakenedAimCorrectionAngle, GroundForward)
			&& BuildGroundTargetHunterFlightMovementRequest(GroundForward,
				Character->GetActorLocation(), LockedHunterTargetPoint, Config->AwakenedHunterFlightSpeed,
				Config->AwakenedHunterFlightDurationSeconds, Config->AwakenedHunterFlightGravityMagnitude,
				Config->AwakenedGroundTargetMinimumDistance, Config->AwakenedGroundTargetVerticalLaunchSpeed, Request))
		: BuildHunterFlightMovementRequest(LockedHunterFlightDirection, Config->AwakenedHunterFlightSpeed,
			Config->AwakenedHunterFlightDurationSeconds, Config->AwakenedHunterFlightGravityMagnitude, Request);
	if (!bRequestBuilt)
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[AwakenedKinsect] Rejected hunter-flight request (action=%u dir=%s pitch=%.2f speed=%.1f duration=%.3f gravity=%.1f)."),
			ActionToken.ActivationSequenceID,
			*LockedHunterFlightDirection.ToCompactString(),
			LockedHunterFlightDirection.Rotation().Pitch,
			Config->AwakenedHunterFlightSpeed,
			Config->AwakenedHunterFlightDurationSeconds,
			Config->AwakenedHunterFlightGravityMagnitude);
		return false;
	}
	LockedHunterFlightDirection = Request.LaunchVelocity.GetSafeNormal();
	if (!DisableAttackMontageRootMotionForHunterFlight(ActionToken))
	{
		UE_LOG(LogMHGZ, Error,
			TEXT("[AwakenedKinsect] Could not disable root-motion extraction on the exact active attack Montage before CMC flight."));
		return false;
	}
	if (!Host->ApplyAerialFallingPhysicsOverride(ActionToken,
		Config->AwakenedHunterFlightGravityMagnitude, 0.0f))
	{
		UE_LOG(LogMHGZ, Error,
			TEXT("[AwakenedKinsect] Could not apply the hunter-flight gravity override (action=%u movementMode=%d)."),
			ActionToken.ActivationSequenceID,
			static_cast<int32>(CMC->MovementMode));
		return false;
	}
	Request.OwnerAction = ActionToken;
	UE_LOG(LogMHGZ, Display,
		TEXT("[AwakenedKinsect] 199 profile: action=%u groundTarget=%d targetDistance=%.1f travelDistance=%.1f releasePitch=%.2f horizontalSpeed=%.1f verticalSpeed=%.1f."),
		ActionToken.ActivationSequenceID, bGroundTarget ? 1 : 0,
		FVector::Dist2D(Character->GetActorLocation(), LockedHunterTargetPoint),
		Request.LaunchVelocity.Size2D() * Request.Duration, ReleaseAimSnapshot.Direction.Rotation().Pitch,
		Request.LaunchVelocity.Size2D(), Request.LaunchVelocity.Z);

	HunterFlightMovementTask = UAbilityTask_MHGZWeaponMovement::StartWeaponMovement(
		this, TEXT("AwakenedHunterFlight199"), Request);
	UAbilityTask_MHGZWeaponMovement* Task = HunterFlightMovementTask.Get();
	if (!Task)
	{
		UE_LOG(LogMHGZ, Error,
			TEXT("[AwakenedKinsect] Could not create the hunter-flight movement task (action=%u)."),
			ActionToken.ActivationSequenceID);
		Host->ApplyAerialFallingProfile(false);
		return false;
	}
	Task->OnFinished.AddDynamic(this,
		&UMHGZAwakenedKinsectAttackAbility::HandleHunterFlightMovementFinished);
	bHunterFlightStarted = true;
	Task->ReadyForActivation();
	UAbilityTask_MHGZWeaponMovement* ActiveFlightTask = HunterFlightMovementTask.Get();
	if (!IsValid(ActiveFlightTask) || !ActiveFlightTask->DidStartMovement())
	{
		if (IsValid(ActiveFlightTask))
		{
			ActiveFlightTask->EndTask();
		}
		UE_LOG(LogMHGZ, Error,
			TEXT("[AwakenedKinsect] Hunter-flight task failed to start (action=%u taskValid=%d movementMode=%d)."),
			ActionToken.ActivationSequenceID,
			IsValid(ActiveFlightTask) ? 1 : 0,
			static_cast<int32>(CMC->MovementMode));
		HunterFlightMovementTask = nullptr;
		bHunterFlightStarted = false;
		Host->ApplyAerialFallingProfile(false);
		return false;
	}

	UE_LOG(LogMHGZ, Display,
		TEXT("[AwakenedKinsect] Hunter flight 199 started: action=%u dir=%s speed=%.1f duration=%.3f gravity=%.1f"),
		ActionToken.ActivationSequenceID, *LockedHunterFlightDirection.ToCompactString(),
		Config->AwakenedHunterFlightSpeed,
		Config->AwakenedHunterFlightDurationSeconds,
		Config->AwakenedHunterFlightGravityMagnitude);
	return true;
}

void UMHGZAwakenedKinsectAttackAbility::HandleHunterFlightMovementFinished(
	const FWeaponMovementResult& Result)
{
	HunterFlightMovementTask = nullptr;
	if (!IsActive() || !IsActionActivationCommitted()
		|| !bHunterFlightStarted || bHunterFallTransitionCommitted)
	{
		return;
	}
	if (bEndingHunterFlightForDance)
	{
		return;
	}
	if (Result.EndReason == EWeaponMovementEndReason::Completed && !bDanceTransitionPending)
	{
		const FWeaponActionToken CompletingAction = GetActionToken();
		// Resolve only an already-open authored attack window before 199 is retired.
		// A physics impact alone never grants damage or a dance entry.
		TickCollision(0, 0.0f);
		if (!IsActive() || !IsActionActivationCommitted() || bHunterFallTransitionCommitted
			|| GetActionToken() != CompletingAction)
		{
			return;
		}
	}

	// 猎虫回收与 HunterFlight 的结束绑定；命中/未命中不改变召回行为。
	RecallKinsectForThisAction();
	if (bDanceTransitionPending)
	{
		// GE success already won during the 199 sweep. A natural/interrupt task
		// callback may race the queued montage handoff; never route that hit into
		// the ordinary 206 miss fall. The queued transition owns the next phase.
		if (Result.EndReason == EWeaponMovementEndReason::Landed)
		{
			if (UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost())
			{
				Host->HandleLanded();
			}
			bHunterFallTransitionCommitted = true;
			RequestEndAction(EWeaponActionEndReason::Normal);
		}
		return;
	}

	if (Result.EndReason == EWeaponMovementEndReason::Landed)
	{
		RequestEndAction(EWeaponActionEndReason::Normal);
		return;
	}
	if (Result.EndReason != EWeaponMovementEndReason::Completed)
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[AwakenedKinsect] Hunter flight ended unexpectedly (reason=%d); returning to ordinary falling."),
			static_cast<int32>(Result.EndReason));
		RequestEndAction(EWeaponActionEndReason::Interrupted);
		return;
	}

	// Protected 199 contacts clip displacement without ending the phase. At its
	// actual end, inspect a real floor once before choosing ground or 206. Never
	// use stale Host grounded state (it was cleared when the flight lease began).
	ACharacter* FinishedCharacter = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	UCharacterMovementComponent* FinishedCMC = FinishedCharacter
		? FinishedCharacter->GetCharacterMovement() : nullptr;
	FFindFloorResult Floor;
	if (FinishedCMC && FinishedCharacter && FinishedCMC->MovementMode == MOVE_Flying)
	{
		FinishedCMC->FindFloor(FinishedCharacter->GetActorLocation(), Floor, false);
		if (Floor.IsWalkableFloor()
			&& Floor.FloorDist <= UCharacterMovementComponent::MAX_FLOOR_DIST)
		{
			UMHGZWeaponRuntimeHostComponent* FinishedHost = GetRuntimeHost();
			const FWeaponRuntimeToken FinishedRuntime = GetActionToken().RuntimeToken;
			bHunterFallTransitionCommitted = true;
			FinishedCMC->Velocity = FVector::ZeroVector;
			FinishedCMC->SetMovementMode(MOVE_Walking);
			if (FinishedHost)
			{
				// SetMovementMode updates posture but does not run Character::Landed.
				// Use the shared landing transaction to clear dance/budget ownership.
				FinishedHost->HandleLanded(false);
			}
			RequestEndAction(EWeaponActionEndReason::Normal);
			// Start the system-owned pose only after ending the source Montage, and
			// never overwrite a successor action that won during the end callbacks.
			if (FinishedHost && FinishedHost->IsTokenCurrent(FinishedRuntime)
				&& !FinishedHost->HasRegisteredAction() && FinishedHost->IsGrounded())
			{
				FinishedHost->PlayAerialLandingPresentation();
			}
			return;
		}
	}

	URes_InsectGlaive* Resource = GetIGResourceComponent();
	const UInsectGlaiveCombatConfig* Config = Resource ? Resource->GetCombatConfig() : nullptr;
	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	UCharacterMovementComponent* CMC = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Config || !Host || !Character || !CMC || Host->IsGrounded()
		|| !Host->ApplyAerialFallingPhysicsOverride(GetActionToken(),
			Config->AwakenedHunterFallGravityMagnitude, 0.0f))
	{
		RequestEndAction(EWeaponActionEndReason::Interrupted);
		return;
	}

	// 206 的 F9 首样本是状态边界零速；去掉边界样本后，回归初始垂直速度约
	// +821..+986cm/s。以可调候选速度重置垂直分量，并清除 199 的水平惯性。
	CMC->Velocity = Config->AwakenedHunterFallHandoffVelocity;
	CMC->SetMovementMode(MOVE_Falling);
	bHunterFallTransitionCommitted = true;
	RequestEndAction(EWeaponActionEndReason::Normal);

	if (!Host->BeginAerialFalling(false, AwakenedFallingStyleTag(),
		Config->AwakenedHunterFallMontage, true))
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[AwakenedKinsect] Could not start the 206 falling presentation; falling back to the standard IG profile."));
		Host->ApplyAerialFallingProfile(false);
		if (!Host->BeginAerialFalling(false))
		{
			Host->AcquireAerialFallingState();
		}
	}
}

void UMHGZAwakenedKinsectAttackAbility::HandleQueuedDanceTransition(
	const FWeaponActionToken ActionToken, const int32 SourceMontageInstanceID)
{
	QueuedDanceTransitionTimer.Invalidate();
	if (!IsActive() || !IsActionActivationCommitted()
		|| ActionToken != GetActionToken() || !bDanceTransitionPending
		|| bDanceEntryCommitted
		|| SourceMontageInstanceID == INDEX_NONE
		|| SourceMontageInstanceID != AttackMontageInstanceID
		|| SourceMontageInstanceID != SuccessfulHitSourceMontageInstanceID)
	{
		return;
	}

	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	URes_InsectGlaive* Resource = GetIGResourceComponent();
	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	UAnimInstance* AnimInstance = GetAttackAnimInstance();
	UWorld* World = GetWorld();
	if (!Host || !Resource || !Character || !Character->GetMesh()
		|| !AnimInstance || !World || Host->IsGrounded()
		|| !DanceMontage || !DanceFromDashTuning.IsValid()
		|| !Host->SetAerialMovementSource(ActionToken, AwakenedKinsectHitSourceTag()))
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[AwakenedKinsect] Successful 199 damage could not begin the shared 189 handoff (action=%u sourceInstance=%d hitAt=%.4f)."),
			ActionToken.ActivationSequenceID, SourceMontageInstanceID,
			SuccessfulHitSourcePosition);
		RequestEndAction(EWeaponActionEndReason::Interrupted);
		return;
	}

	// Retire only the CMC 199 owner; its last velocity is otherwise preserved.
	// The following prep hover and shared 189 authored arc are still owned by this
	// same ActionToken.
	if (HunterFlightMovementTask)
	{
		bEndingHunterFlightForDance = true;
		const bool bRetired = HunterFlightMovementTask
			->EndForImmediateMovementHandoff(ActionToken);
		bEndingHunterFlightForDance = false;
		if (!bRetired)
		{
			RequestEndAction(EWeaponActionEndReason::Interrupted);
			return;
		}
		HunterFlightMovementTask = nullptr;
	}

	DisableCollision(0);
	RecallKinsectForThisAction();
	if (!StartDancePreparationMovement())
	{
		RequestEndAction(EWeaponActionEndReason::Interrupted);
		return;
	}

	if (!SuspendAttackMontageForFollowup())
	{
		RequestEndAction(EWeaponActionEndReason::Interrupted);
		return;
	}

	bDanceMontageFinished = false;
	bDanceMovementFinished = false;
	bDanceLanded = false;
	ActiveDanceMontage = DanceMontage;
	if (!StartDanceMontage())
	{
		RequestEndAction(EWeaponActionEndReason::Interrupted);
		return;
	}

	const int32 CommittingMontageID = DanceMontageInstanceID;
	const bool bDanceAdded = Resource->AddDanceStack(EIGDanceSource::AwakenedKinsect);
	if (!IsActive() || DanceMontageInstanceID != CommittingMontageID)
	{
		return;
	}
	if (!bDanceAdded || !Host->ResetAerialActionBudgetOnDance())
	{
		RequestEndAction(EWeaponActionEndReason::Interrupted);
		return;
	}

	bDanceEntryCommitted = true;
	bDanceTransitionPending = false;
	World->GetTimerManager().SetTimer(DancePreparationTimer,
		FTimerDelegate::CreateUObject(this,
			&UMHGZAwakenedKinsectAttackAbility::HandleDancePreparationFinished,
			ActionToken, DanceMontageInstanceID),
		DanceFromDashTuning.PrepDuration, false);
	UE_LOG(LogMHGZ, Display,
		TEXT("[AwakenedKinsect] GE success handed 199 to shared 189: action=%u sourceInstance=%d danceInstance=%d section=%s hitAt=%.4f prep=%.3f."),
		ActionToken.ActivationSequenceID, SourceMontageInstanceID,
		DanceMontageInstanceID, *DanceSectionName.ToString(),
		SuccessfulHitSourcePosition, DanceFromDashTuning.PrepDuration);
}

bool UMHGZAwakenedKinsectAttackAbility::StartDancePreparationMovement()
{
	if (DancePreparationMovementTask || !DanceFromDashTuning.IsValid()
		|| !IsActive() || !IsActionActivationCommitted())
	{
		return false;
	}
	FWeaponMovementRequest Request;
	Request.OwnerAction = GetActionToken();
	Request.Mode = EWeaponMovementMode::StationaryHover;
	Request.Duration = DanceFromDashTuning.PrepDuration + 1.0f;
	Request.CancelVelocityPolicy = EMovementCancelVelocityPolicy::PreserveVelocity;
	DancePreparationMovementTask = UAbilityTask_MHGZWeaponMovement::StartWeaponMovement(
		this, TEXT("AwakenedKinsect189Preparation"), Request);
	if (!DancePreparationMovementTask)
	{
		return false;
	}
	DancePreparationMovementTask->ReadyForActivation();
	if (!DancePreparationMovementTask
		|| !DancePreparationMovementTask->DidStartMovement())
	{
		if (DancePreparationMovementTask)
		{
			DancePreparationMovementTask->EndTask();
			DancePreparationMovementTask = nullptr;
		}
		return false;
	}
	return true;
}

bool UMHGZAwakenedKinsectAttackAbility::StartDanceMontage()
{
	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (!Mesh || !AnimInstance || !DanceMontage
		|| !DanceMontage->IsValidSectionName(DanceSectionName))
	{
		return false;
	}

	DanceMontageTask = UAbilityTask_MHGZPlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, TEXT("AwakenedKinsect189Dance"), DanceMontage, 1.0f,
		DanceSectionName, DanceMontageBlendInTime, false);
	if (!DanceMontageTask)
	{
		return false;
	}
	DanceMontageTask->OnCompleted.AddDynamic(this,
		&UMHGZAwakenedKinsectAttackAbility::HandleDanceMontageCompleted);
	DanceMontageTask->OnInterrupted.AddDynamic(this,
		&UMHGZAwakenedKinsectAttackAbility::HandleDanceMontageInterrupted);
	DanceMontageTask->OnCancelled.AddDynamic(this,
		&UMHGZAwakenedKinsectAttackAbility::HandleDanceMontageInterrupted);
	DanceMontageTask->ReportCompletionAtMontageEnd();
	DetectAerialHandoffNotify(DanceMontage);
	DanceMontageTask->ReadyForActivation();

	FAnimMontageInstance* Instance = AnimInstance->GetActiveInstanceForMontage(DanceMontage);
	if (!Instance || AnimInstance->Montage_GetCurrentSection(DanceMontage)
		!= DanceSectionName)
	{
		return false;
	}
	DanceMontageInstanceID = Instance->GetInstanceID();
	Instance->bEnableAutoBlendOut = false;
	Instance->PushDisableRootMotion();
	ActiveAttackMontage = DanceMontage;
	return DanceMontageInstanceID != INDEX_NONE
		&& RegisterMontageInstance(Mesh, DanceMontageInstanceID);
}

bool UMHGZAwakenedKinsectAttackAbility::StartDanceMovement()
{
	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	if (!Host || !DancePreparationMovementTask
		|| DanceMontageInstanceID == INDEX_NONE
		|| bDanceLanded || Host->IsGrounded()
		|| !DanceFromDashTuning.IsValid())
	{
		return false;
	}
	FWeaponMovementRequest Request = DanceFromDashTuning.MakeRequest(
		GetActionToken(), LockedHunterFlightDirection, this);
	if (!Request.HasValidBallisticParameters()
		|| !Request.HasValidAuthoredAerialPhysicsParameters())
	{
		return false;
	}
	if (!DancePreparationMovementTask->EndHoverForImmediateMovementHandoff(
		GetActionToken()))
	{
		return false;
	}
	DancePreparationMovementTask = nullptr;
	if (!Host->ApplyAerialFallingPhysicsOverride(GetActionToken(),
		DanceFromDashTuning.GravityMagnitude, 0.0f, Request.PlanarAcceleration))
	{
		return false;
	}
	DanceMovementTask = UAbilityTask_MHGZWeaponMovement::StartWeaponMovement(
		this, TEXT("AwakenedKinsect189Flight"), Request);
	if (!DanceMovementTask)
	{
		return false;
	}
	DanceMovementTask->OnFinished.AddDynamic(this,
		&UMHGZAwakenedKinsectAttackAbility::HandleDanceMovementFinished);
	DanceMovementTask->ReadyForActivation();
	if (!DanceMovementTask || !DanceMovementTask->DidStartMovement())
	{
		if (DanceMovementTask)
		{
			DanceMovementTask->EndTask();
			DanceMovementTask = nullptr;
		}
		return false;
	}
	return true;
}

void UMHGZAwakenedKinsectAttackAbility::HandleDancePreparationFinished(
	const FWeaponActionToken ActionToken, const int32 InDanceMontageInstanceID)
{
	if (ActionToken != GetActionToken() || !IsActive()
		|| !bDanceEntryCommitted || InDanceMontageInstanceID != DanceMontageInstanceID)
	{
		return;
	}
	DancePreparationTimer.Invalidate();
	if (!StartDanceMovement())
	{
		RequestEndAction(EWeaponActionEndReason::Interrupted);
	}
}

void UMHGZAwakenedKinsectAttackAbility::HandleDanceMovementFinished(
	const FWeaponMovementResult& Result)
{
	DanceMovementTask = nullptr;
	if (!IsActive() || bEndingDanceRuntime)
	{
		return;
	}
	if (Result.EndReason == EWeaponMovementEndReason::Completed
		|| Result.EndReason == EWeaponMovementEndReason::Landed)
	{
		bDanceMovementFinished = true;
		if (Result.EndReason == EWeaponMovementEndReason::Landed)
		{
			bDanceLanded = true;
			if (UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost())
			{
				Host->HandleLanded();
			}
		}
		TryCompleteDanceAction();
		return;
	}
	RequestEndAction(EWeaponActionEndReason::Interrupted);
}

void UMHGZAwakenedKinsectAttackAbility::HandleDanceMontageCompleted()
{
	DanceMontageTask = nullptr;
	if (!IsActive() || bEndingDanceRuntime)
	{
		return;
	}
	bDanceMontageFinished = true;
	TryCompleteDanceAction();
}

void UMHGZAwakenedKinsectAttackAbility::HandleDanceMontageInterrupted()
{
	DanceMontageTask = nullptr;
	if (IsActive() && !bEndingDanceRuntime)
	{
		RequestEndAction(EWeaponActionEndReason::Interrupted);
	}
}

void UMHGZAwakenedKinsectAttackAbility::TryCompleteDanceAction()
{
	if (!IsActive() || !bDanceEntryCommitted
		|| !bDanceMontageFinished || !bDanceMovementFinished)
	{
		return;
	}
	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	const FWeaponRuntimeToken RuntimeToken = GetActionToken().RuntimeToken;
	const URes_InsectGlaive* Resource = GetIGResourceComponent();
	const UInsectGlaiveCombatConfig* Config = Resource
		? Resource->GetCombatConfig() : nullptr;
	UAnimMontage* FallMontage = Config ? Config->GetAerialDodgeFallMontage() : nullptr;
	bHunterFallTransitionCommitted = true;
	RequestEndAction(EWeaponActionEndReason::Normal);
	if (Host && Host->IsTokenCurrent(RuntimeToken) && !Host->IsGrounded()
		&& !Host->HasRegisteredAction())
	{
		if (!Host->BeginAerialFalling(false, FGameplayTag(), FallMontage, true))
		{
			UE_LOG(LogMHGZ, Warning,
				TEXT("[AwakenedKinsect] Shared 189 completed but ordinary aerial falling handoff failed."));
		}
	}
}

void UMHGZAwakenedKinsectAttackAbility::ClearDanceTimers()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(QueuedDanceTransitionTimer);
		World->GetTimerManager().ClearTimer(DancePreparationTimer);
	}
	QueuedDanceTransitionTimer.Invalidate();
	DancePreparationTimer.Invalidate();
}

void UMHGZAwakenedKinsectAttackAbility::CleanupDanceTasks()
{
	bEndingDanceRuntime = true;
	if (ActiveDanceMontage && DanceMontageInstanceID != INDEX_NONE)
	{
		if (UAnimInstance* AnimInstance = GetAttackAnimInstance())
		{
			FAnimMontageInstance* Instance =
				AnimInstance->GetActiveInstanceForMontage(ActiveDanceMontage);
			if (Instance && Instance->GetInstanceID() == DanceMontageInstanceID)
			{
				AnimInstance->Montage_Stop(0.05f, ActiveDanceMontage);
			}
		}
	}
	if (DancePreparationMovementTask)
	{
		DancePreparationMovementTask->EndTask();
		DancePreparationMovementTask = nullptr;
	}
	if (DanceMovementTask)
	{
		DanceMovementTask->EndTask();
		DanceMovementTask = nullptr;
	}
	if (DanceMontageTask)
	{
		DanceMontageTask->EndTask();
		DanceMontageTask = nullptr;
	}
	DanceMontageInstanceID = INDEX_NONE;
	ActiveDanceMontage = nullptr;
}

void UMHGZAwakenedKinsectAttackAbility::StartFallbackFallingAfterActionEnd(
	UMHGZWeaponRuntimeHostComponent* Host, const bool bShouldStartFallback)
{
	if (!bShouldStartFallback || !Host || Host->IsGrounded()
		|| Host->HasRegisteredAction())
	{
		return;
	}

	// Montage/任务若在 199 计时完成前被打断，保留当前 CMC 速度，撤掉 1190
	// 的 199 专用重力并回到项目普通 IG 下落档；不把中断伪装成 206。
	Host->ApplyAerialFallingProfile(false);
	if (!Host->BeginAerialFalling(false))
	{
		Host->AcquireAerialFallingState();
	}
}

bool UMHGZAwakenedKinsectAttackAbility::CommitKinsectLaunch(
	const FWeaponActionToken& ActionToken)
{
	if (!IsActive() || !IsActionActivationCommitted()
		|| ActionToken != GetActionToken())
	{
		return false;
	}
	if (bKinsectLaunchCommitted)
	{
		return true;
	}
	if (!LockReleaseAim())
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[AwakenedKinsect] Release-frame aim was unavailable or invalid; cancelling this action (action=%u hasSnapshot=%d)."),
			ActionToken.ActivationSequenceID, bHasReleaseAimSnapshot ? 1 : 0);
		RequestEndAction(EWeaponActionEndReason::Cancelled);
		return false;
	}
	if (!bReleaseFacingCorrectionStarted)
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[AwakenedKinsect] Release-facing correction notify was not triggered before KinsectLaunch (action=%u montageInstance=%d)."),
			ActionToken.ActivationSequenceID, AttackMontageInstanceID);
	}
	if (!UpdatePendingKinsectFlightProfile())
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[AwakenedKinsect] Could not derive the fixed flight interval from the authored KinsectLaunch/HunterFlightStart events; cancelling this action (action=%u montageInstance=%d)."),
			ActionToken.ActivationSequenceID, AttackMontageInstanceID);
		RequestEndAction(EWeaponActionEndReason::Cancelled);
		return false;
	}

	URes_InsectGlaive* Resource = GetIGResourceComponent();
	AKinsect* Kinsect = Resource ? Resource->GetKinsectActor() : nullptr;
	const UInsectGlaiveCombatConfig* Config = Resource ? Resource->GetCombatConfig() : nullptr;
	if (!Kinsect || !Config || !ResolveLockedTargetDirection(ReleaseActorForward,
		LockedHunterTargetPoint, Kinsect->GetActorLocation(), ReleaseAimSnapshot.Direction,
		Config->AwakenedAimHorizontalCorrectionAngle, Config->AwakenedAimCorrectionAngle,
		LockedKinsectDirection))
	{
		RequestEndAction(EWeaponActionEndReason::Cancelled);
		return false;
	}
	PendingKinsectRequest.DirectionSnapshot = LockedKinsectDirection;
	PendingKinsectRequest.TargetPointSnapshot = LockedHunterTargetPoint;
	if (!Resource || !bKinsectActionAttached
		|| !Resource->IsKinsectActionAttachmentCurrent(ActionToken)
		|| PendingKinsectRequest.RuntimeToken != ActionToken.RuntimeToken
		|| !PendingKinsectRequest.FlightInstanceID.IsValid()
		|| !Resource->DeployKinsectFromActionAttachment(ActionToken,
			PendingKinsectRequest))
	{
		RequestEndAction(EWeaponActionEndReason::Cancelled);
		return false;
	}

	bKinsectActionAttached = false;
	bKinsectLaunchCommitted = true;
	UE_LOG(LogMHGZ, Display,
		TEXT("[AwakenedKinsect] Kinsect launch locked to release aim: action=%u empowered=%d dir=%s range=%.1f speed=%.1f cm/s origin=%s target=%s."),
		ActionToken.ActivationSequenceID, bEmpoweredKinsectBranch ? 1 : 0,
		*PendingKinsectRequest.DirectionSnapshot.ToCompactString(),
		PendingKinsectRequest.MaxDistance, PendingKinsectRequest.FlightSpeed,
		*Kinsect->GetActorLocation().ToCompactString(), *LockedHunterTargetPoint.ToCompactString());
	return true;
}

void UMHGZAwakenedKinsectAttackAbility::RecallKinsectForThisAction()
{
	URes_InsectGlaive* Resource = GetIGResourceComponent();
	AKinsect* Kinsect = Resource ? Resource->GetKinsectActor() : nullptr;
	if (!Resource || !Kinsect)
	{
		bKinsectActionAttached = false;
		return;
	}

	if (bKinsectActionAttached)
	{
		if (Resource->IsKinsectActionAttachmentCurrent(GetActionToken())
			&& !Resource->EndKinsectActionAttachmentAndReturn(GetActionToken()))
		{
			UE_LOG(LogMHGZ, Warning,
				TEXT("[AwakenedKinsect] Could not return the pre-launch ActionAttached Kinsect."));
		}
		bKinsectActionAttached = false;
	}

	if (!bKinsectLaunchCommitted
		|| Kinsect->ActiveRequest.FlightInstanceID != PendingKinsectRequest.FlightInstanceID
		|| Kinsect->GetState() == EKinsectState::Attached
		|| Kinsect->GetState() == EKinsectState::Returning)
	{
		return;
	}

	if (!Resource->RecallKinsect(EKinsectCommandSource::Action))
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[AwakenedKinsect] Could not recall the Kinsect at the end of its owned flight."));
	}
}

void UMHGZAwakenedKinsectAttackAbility::OnMontageCompleted()
{
	// The source 199 Montage can finish while a GE-success callback is queued,
	// or after the persistent 189 task has taken over. Only its separate montage
	// task owns dance completion; do not let the original Attack task end the GA.
	if (bDanceTransitionPending || bDanceEntryCommitted)
	{
		return;
	}
	if (!bKinsectLaunchCommitted)
	{
		RequestEndAction(EWeaponActionEndReason::Cancelled);
		return;
	}
	Super::OnMontageCompleted();
}

void UMHGZAwakenedKinsectAttackAbility::HandleLanded(const FHitResult& Hit)
{
	if (bDanceEntryCommitted)
	{
		bDanceLanded = true;
		DisableCollision();
		RecallKinsectForThisAction();
		if (UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost())
		{
			Host->HandleLanded();
		}
		TryCompleteDanceAction();
		return;
	}
	Super::HandleLanded(Hit);
}

bool UMHGZAwakenedKinsectAttackAbility::Configure193HoldLoop()
{
	UAnimInstance* AnimInstance = GetAttackAnimInstance();
	if (!AnimInstance || !AttackMontage
		|| !AttackMontage->IsValidSectionName(StartupSection)
		|| !AttackMontage->IsValidSectionName(HoldLoopSection)
		|| !AttackMontage->IsValidSectionName(SendSection)
		|| !AttackMontage->IsValidSectionName(PreFlightSection)
		|| !AttackMontage->IsValidSectionName(HunterFlightSection))
	{
		return false;
	}

	HoldLoopPlayCount = 0;
	AnimInstance->Montage_SetNextSection(StartupSection, HoldLoopSection, AttackMontage);
	AnimInstance->Montage_SetNextSection(HoldLoopSection, HoldLoopSection, AttackMontage);
	AnimInstance->Montage_SetNextSection(SendSection, PreFlightSection, AttackMontage);
	AnimInstance->Montage_SetNextSection(PreFlightSection, HunterFlightSection, AttackMontage);
	return true;
}

bool UMHGZAwakenedKinsectAttackAbility::AreAllHoldReleaseControlsUp() const
{
	const AMHGZCharacter* Character = Cast<AMHGZCharacter>(GetAvatarActorFromActorInfo());
	const AMHGZPlayerController* Controller = Character
		? Cast<AMHGZPlayerController>(Character->GetController()) : nullptr;
	const UMHGZWeaponInputRouterComponent* Router = Controller
		? Controller->GetWeaponInputRouter() : nullptr;
	if (!Router || HoldReleaseControls.IsEmpty())
	{
		return false;
	}

	for (const FGameplayTag& Control : HoldReleaseControls)
	{
		if (Router->IsPhysicalInputHeld(Control))
		{
			return false;
		}
	}
	return true;
}

bool UMHGZAwakenedKinsectAttackAbility::CaptureReleaseAimSnapshot()
{
	bHasReleaseAimSnapshot = false;
	const AMHGZCharacter* Character =
		Cast<AMHGZCharacter>(GetAvatarActorFromActorInfo());
	const UMHGZAimComponent* AimComponent = Character
		? Character->GetAimComponent() : nullptr;
	if (!Character || !AimComponent)
	{
		return false;
	}

	const FWeaponAimSnapshot Snapshot = AimComponent->CaptureAimSnapshot(
		EWeaponAimSnapshotContext::Action);
	const FVector ActorForward = Character->GetActorForwardVector();
	if (Snapshot.Context != EWeaponAimSnapshotContext::Action
		|| !IsFiniteVector(Snapshot.Direction) || Snapshot.Direction.IsNearlyZero()
		|| !IsFiniteVector(Snapshot.TargetPoint)
		|| !IsFiniteVector(ActorForward) || ActorForward.IsNearlyZero())
	{
		return false;
	}

	ReleaseAimSnapshot = Snapshot;
	ReleaseActorForward = ActorForward.GetSafeNormal();
	bHasReleaseAimSnapshot = true;
	return true;
}

bool UMHGZAwakenedKinsectAttackAbility::LockReleaseAim()
{
	if (bReleaseAimLocked)
	{
		return true;
	}
	if (!bHasReleaseAimSnapshot)
	{
		return false;
	}

	URes_InsectGlaive* Resource = GetIGResourceComponent();
	const UInsectGlaiveCombatConfig* Config = Resource
		? Resource->GetCombatConfig() : nullptr;
	const AActor* Hunter = GetAvatarActorFromActorInfo();
	if (!Config || !Hunter
		|| !ResolveLockedTargetDirection(ReleaseActorForward, ReleaseAimSnapshot.TargetPoint,
			Hunter->GetActorLocation(), ReleaseAimSnapshot.Direction,
			Config->AwakenedAimHorizontalCorrectionAngle,
			Config->AwakenedAimCorrectionAngle,
			LockedKinsectDirection))
	{
		return false;
	}

	LockedHunterFlightDirection = LockedKinsectDirection;
	ACharacter* HunterCharacter = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	UCharacterMovementComponent* HunterCMC = HunterCharacter ? HunterCharacter->GetCharacterMovement() : nullptr;
	if (HunterCharacter && HunterCMC && HasRealGroundTarget(*HunterCharacter, *HunterCMC, ReleaseAimSnapshot)
		&& !ResolveLockedAimDirection(ReleaseActorForward, ReleaseAimSnapshot.Direction,
			Config->AwakenedAimHorizontalCorrectionAngle, Config->AwakenedAimCorrectionAngle,
			LockedHunterFlightDirection))
	{
		return false;
	}
	LockedHunterTargetPoint = ReleaseAimSnapshot.TargetPoint;
	PendingKinsectRequest.DirectionSnapshot = LockedKinsectDirection;

	const float TargetYaw = LockedHunterFlightDirection.Rotation().Yaw;
	const float ReleaseYaw = ReleaseActorForward.Rotation().Yaw;
	const float ReleaseCorrectionDelta = FMath::FindDeltaAngleDegrees(
		ReleaseYaw, TargetYaw);
	if (FMath::Abs(ReleaseCorrectionDelta)
		> Config->AwakenedAimHorizontalCorrectionAngle + 0.01f)
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[AwakenedKinsect] Resolved release yaw exceeded its configured cap (delta=%.2f cap=%.2f)."),
			ReleaseCorrectionDelta, Config->AwakenedAimHorizontalCorrectionAngle);
		return false;
	}

	bReleaseAimLocked = true;
	UE_LOG(LogMHGZ, Display,
		TEXT("[AwakenedKinsect] Release aim locked: action=%u actorForward=%s aim=%s locked=%s yawLimit=%.1f pitchLimit=%.1f; facing correction awaits Montage frame 230."),
		GetActionToken().ActivationSequenceID,
		*ReleaseActorForward.ToCompactString(),
		*ReleaseAimSnapshot.Direction.ToCompactString(),
		*LockedKinsectDirection.ToCompactString(),
		Config->AwakenedAimHorizontalCorrectionAngle,
		Config->AwakenedAimCorrectionAngle);
	return true;
}

bool UMHGZAwakenedKinsectAttackAbility::ApplyReleaseFacingCorrection()
{
	if (bReleaseFacingCorrectionStarted)
	{
		return true;
	}
	if (!LockReleaseAim())
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[AwakenedKinsect] Frame-230 release-facing correction has no valid release-frame aim (action=%u hasSnapshot=%d)."),
			GetActionToken().ActivationSequenceID, bHasReleaseAimSnapshot ? 1 : 0);
		return false;
	}

	AMHGZCharacter* Character = Cast<AMHGZCharacter>(
		GetAvatarActorFromActorInfo());
	if (!Character)
	{
		return false;
	}

	const float TargetYaw = LockedHunterFlightDirection.Rotation().Yaw;
	const float CurrentYaw = Character->GetActorRotation().Yaw;
	// Start only from the dedicated Montage point notify. The aim target was
	// clamped around release-frame ActorForward when LockReleaseAim ran.
	if (!ApplyInActionDirectionCorrectionToYaw(GetActionToken(), TargetYaw,
		AwakenedFacingCorrectionDurationSeconds, 0.0f,
		EActionDirectionCorrectionEasing::EaseOut))
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[AwakenedKinsect] Could not start frame-230 action-owned facing correction (action=%u currentYaw=%.2f targetYaw=%.2f actorScale=%s)."),
			GetActionToken().ActivationSequenceID, CurrentYaw, TargetYaw,
			*Character->GetActorScale3D().ToCompactString());
		return false;
	}

	bReleaseFacingCorrectionStarted = true;
	UE_LOG(LogMHGZ, Display,
		TEXT("[AwakenedKinsect] Frame-230 release-facing correction started: action=%u currentYaw=%.2f targetYaw=%.2f duration=%.3fs easing=EaseOut actorScale=%s meshRelativeScale=%s."),
		GetActionToken().ActivationSequenceID, CurrentYaw, TargetYaw,
		AwakenedFacingCorrectionDurationSeconds,
		*Character->GetActorScale3D().ToCompactString(),
		Character->GetMesh()
			? *Character->GetMesh()->GetRelativeScale3D().ToCompactString()
			: TEXT("NoMesh"));
	return true;
}

bool UMHGZAwakenedKinsectAttackAbility::UpdatePendingKinsectFlightProfile()
{
	float FlightDurationSeconds = 0.0f;
	float MaxDistance = 0.0f;
	float FlightSpeed = 0.0f;
	if (!bReleaseAimLocked
		|| !GetKinsectFlightDuration(FlightDurationSeconds)
		|| !BuildAwakenedKinsectFlightProfile(FlightDurationSeconds,
			bEmpoweredKinsectBranch, MaxDistance, FlightSpeed))
	{
		return false;
	}

	PendingKinsectRequest.MaxDistance = MaxDistance;
	PendingKinsectRequest.FlightSpeed = FlightSpeed;
	UE_LOG(LogMHGZ, Display,
		TEXT("[AwakenedKinsect] Fixed flight profile: notifyInterval=%.6fs kinsectRange=%.1fcm kinsectSpeed=%.1fcm/s tripleUp=%d."),
		FlightDurationSeconds, MaxDistance, FlightSpeed,
		bEmpoweredKinsectBranch ? 1 : 0);
	return true;
}

bool UMHGZAwakenedKinsectAttackAbility::GetKinsectFlightDuration(
	float& OutSeconds) const
{
	OutSeconds = 0.0f;
#if WITH_DEV_AUTOMATION_TESTS
	if (FMath::IsFinite(TestKinsectFlightDurationSeconds)
		&& TestKinsectFlightDurationSeconds > 0.0f)
	{
		OutSeconds = TestKinsectFlightDurationSeconds;
		return true;
	}
#endif
	return AttackMontage
		&& GetAuthoredKinsectFlightDuration(*AttackMontage, OutSeconds);
}

void UMHGZAwakenedKinsectAttackAbility::OnAttackMontageSectionChanged(
	UAnimMontage* Montage, const FName SectionName, const bool bLooped)
{
	if (!IsActive() || !IsActionActivationCommitted() || Montage != AttackMontage)
	{
		return;
	}

	UAnimInstance* AnimInstance = GetAttackAnimInstance();
	const FAnimMontageInstance* MontageInstance = AnimInstance
		? AnimInstance->GetActiveInstanceForMontage(AttackMontage) : nullptr;
	if (!MontageInstance
		|| MontageInstance->GetInstanceID() != AttackMontageInstanceID)
	{
		return;
	}

	if (SectionName == HoldLoopSection)
	{
		// bLooped is true for the self-link transition; the first entry from Startup
		// is play one. Set the outgoing link on each entry so play three flows to Send.
		HoldLoopPlayCount = bLooped
			? FMath::Min(HoldLoopPlayCount + 1, AwakenedKinsectMaximumHoldLoopPlays)
			: 1;
		const FName NextSection = ResolveHoldLoopNextSection(
			HoldLoopPlayCount, HoldLoopSection, SendSection);
		AnimInstance->Montage_SetNextSection(
			HoldLoopSection, NextSection, AttackMontage);
		UE_LOG(LogMHGZ, Verbose,
			TEXT("[AwakenedKinsect] Hold193Loop play %d/%d; next=%s (montageInstance=%d)."),
			HoldLoopPlayCount, AwakenedKinsectMaximumHoldLoopPlays,
			*NextSection.ToString(), AttackMontageInstanceID);
		return;
	}
	if (SectionName != HunterFlightSection)
	{
		return;
	}

	// The asset Notify remains the authored timing source. This callback is an
	// idempotent fallback bound to this exact Montage instance, so a missed Notify
	// cannot leave the hunter frozen when the 199 section is visibly entered.
	if (!StartHunterFlight(GetActionToken()))
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[AwakenedKinsect] 199 section fallback could not start hunter movement (action=%u instance=%d releaseAimLocked=%d releaseSnapshot=%d)."),
			GetActionToken().ActivationSequenceID, AttackMontageInstanceID,
			bReleaseAimLocked ? 1 : 0, bHasReleaseAimSnapshot ? 1 : 0);
	}
}

void UMHGZAwakenedKinsectAttackAbility::UnbindAttackMontageSectionChanged()
{
	if (AttackMontageInstanceID == INDEX_NONE)
	{
		return;
	}

	UAnimInstance* AnimInstance = GetAttackAnimInstance();
	FAnimMontageInstance* MontageInstance = AnimInstance && AttackMontage
		? AnimInstance->GetActiveInstanceForMontage(AttackMontage) : nullptr;
	if (MontageInstance
		&& MontageInstance->GetInstanceID() == AttackMontageInstanceID)
	{
		MontageInstance->OnMontageSectionChanged.Unbind();
	}
	AttackMontageInstanceID = INDEX_NONE;
}

UAnimInstance* UMHGZAwakenedKinsectAttackAbility::GetAttackAnimInstance() const
{
	const ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	return Character && Character->GetMesh()
		? Character->GetMesh()->GetAnimInstance() : nullptr;
}

void UMHGZAwakenedKinsectAttackAbility::ScheduleHoldReleaseCheck()
{
	if (bHoldReleaseCheckScheduled)
	{
		return;
	}
	bHoldReleaseCheckScheduled = true;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateUObject(this,
				&UMHGZAwakenedKinsectAttackAbility::ResolveHoldReleaseCheck));
	}
	else
	{
		bHoldReleaseCheckScheduled = false;
	}
}

void UMHGZAwakenedKinsectAttackAbility::ResolveHoldReleaseCheck()
{
	bHoldReleaseCheckScheduled = false;
	ApplyHoldReleaseRouteIfReady();
}

EAwakenedKinsectReleaseRoute UMHGZAwakenedKinsectAttackAbility::ResolveReleaseRoute(
	const FName CurrentSection, const FName InStartupSection,
	const FName InHoldLoopSection)
{
	if (CurrentSection == InStartupSection)
	{
		return EAwakenedKinsectReleaseRoute::QueueSendAfterStartup;
	}
	if (CurrentSection == InHoldLoopSection)
	{
		return EAwakenedKinsectReleaseRoute::JumpToSendNow;
	}
	return EAwakenedKinsectReleaseRoute::None;
}

FName UMHGZAwakenedKinsectAttackAbility::ResolveHoldLoopNextSection(
	const int32 EnteredLoopPlayCount, const FName InHoldLoopSection,
	const FName InSendSection)
{
	return EnteredLoopPlayCount >= AwakenedKinsectMaximumHoldLoopPlays
		? InSendSection : InHoldLoopSection;
}

void UMHGZAwakenedKinsectAttackAbility::ApplyHoldReleaseRouteIfReady()
{
	if (!IsActive() || !IsActionActivationCommitted() || !bHoldReleasePending
		|| bHoldReleaseRouteApplied)
	{
		return;
	}
	// Y and B may be released in either order. Wait for the release callback from
	// the final held control before making the hold-release decision.
	if (!AreAllHoldReleaseControlsUp())
	{
		return;
	}

	UAnimInstance* AnimInstance = GetAttackAnimInstance();
	if (!AnimInstance || !AttackMontage)
	{
		return;
	}

	const FName CurrentSection = AnimInstance->Montage_GetCurrentSection(AttackMontage);
	const EAwakenedKinsectReleaseRoute Route = ResolveReleaseRoute(
		CurrentSection, StartupSection, HoldLoopSection);
	// Freeze the release-frame aim now, but defer actor yaw correction to the
	// dedicated frame-230 Montage notify. This keeps route selection independent
	// from visual turn timing and never snaps at KinsectLaunch.
	if (!LockReleaseAim())
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[AwakenedKinsect] Could not capture valid live aim at final Y/B release."));
		RequestEndAction(EWeaponActionEndReason::Cancelled);
		return;
	}
	switch (Route)
	{
	case EAwakenedKinsectReleaseRoute::QueueSendAfterStartup:
		// A release during the authored startup must not jump over its wind-up.
		// Queue Send195 as the next section; the current startup plays to completion.
		AnimInstance->Montage_SetNextSection(StartupSection, SendSection, AttackMontage);
		bHoldReleasePending = false;
		bHoldReleaseRouteApplied = true;
		return;
	case EAwakenedKinsectReleaseRoute::JumpToSendNow:
		// Only release while actually holding the loop exits it immediately.
		AnimInstance->Montage_JumpToSection(SendSection, AttackMontage);
		bHoldReleasePending = false;
		bHoldReleaseRouteApplied = true;
		return;
	case EAwakenedKinsectReleaseRoute::None:
	default:
		// Send195 or an unknown section must never be rewound by a stale callback.
		bHoldReleasePending = false;
		return;
	}
}

bool UMHGZAwakenedKinsectAttackAbility::DisableAttackMontageRootMotionForHunterFlight(
	const FWeaponActionToken& ActionToken)
{
	if (ActionToken != GetActionToken() || !AttackMontage)
	{
		return false;
	}

	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	FAnimMontageInstance* Instance = AnimInstance
		? AnimInstance->GetActiveInstanceForMontage(AttackMontage) : nullptr;
	if (!Mesh || !Host || !Instance)
	{
		return false;
	}

	FWeaponActionToken RegisteredAction;
	if (!Host->ResolveMontage(Mesh, Instance->GetInstanceID(), RegisteredAction)
		|| RegisteredAction != ActionToken)
	{
		return false;
	}

	// This Montage legitimately owns root motion during its authored ground/startup
	// portions. At the 199 handoff, CMC must own flight; otherwise its animation-root
	// constraint projects the launch velocity onto the section's zero-planar root
	// velocity. Suppress extraction on this exact registered instance only. The pose
	// continues, and future Montage plays create fresh instances with normal root motion.
	if (AttackMontage->HasRootMotion() && !Instance->IsRootMotionDisabled())
	{
		Instance->PushDisableRootMotion();
	}
	return !AttackMontage->HasRootMotion() || Instance->IsRootMotionDisabled();
}
