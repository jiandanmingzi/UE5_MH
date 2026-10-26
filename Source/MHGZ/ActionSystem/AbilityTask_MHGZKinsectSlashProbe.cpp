// Copyright MHGZ Project. All Rights Reserved.

#include "ActionSystem/AbilityTask_MHGZKinsectSlashProbe.h"
#include "MHGZMath.h"

#include "ActionSystem/MHGZGameplayAbility.h"
#include "AttributeSystem/Res_InsectGlaive.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "InsectGlaive/Kinsect/Kinsect.h"
#include "MHGZ.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

namespace
{
const FQuat KinsectFlightVisualLocalRotation(FVector::UpVector, -UE_HALF_PI);

bool IsValidProbeStartup(UMHGZGameplayAbility* Ability,
	URes_InsectGlaive* Resource, const FWeaponActionToken& Token,
	const FVector& Direction, const float ForwardOffset, const float Radius,
	const float MaxSampleStep, const float LateHitWindowSeconds)
{
	if (!Ability || !Resource || !Token.IsValid()
		|| !Ability->IsActive() || !Ability->IsActionActivationCommitted()
		|| Ability->GetActionToken() != Token
		|| Token.AbilityInstance.Get() != Ability
		|| !MHGZMath::IsFiniteVector(Direction) || Direction.IsNearlyZero()
		|| !FMath::IsFinite(ForwardOffset)
		|| !FMath::IsFinite(Radius) || Radius <= 0.0f
		|| !FMath::IsFinite(MaxSampleStep) || MaxSampleStep <= UE_SMALL_NUMBER
		|| !FMath::IsFinite(LateHitWindowSeconds) || LateHitWindowSeconds < 0.0f)
	{
		return false;
	}

	UMHGZWeaponRuntimeHostComponent* Host = Ability->GetRuntimeHost();
	return Host && Host == Token.RuntimeToken.Host.Get()
		&& Host->IsTokenCurrent(Token.RuntimeToken)
		&& Resource->GetRuntimeContext().RuntimeToken == Token.RuntimeToken
		&& Resource->IsRuntimeRequestCurrent(Token.RuntimeToken);
}
}

UAbilityTask_MHGZKinsectSlashProbe::UAbilityTask_MHGZKinsectSlashProbe(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bTickingTask = true;
}

UAbilityTask_MHGZKinsectSlashProbe*
UAbilityTask_MHGZKinsectSlashProbe::StartProbe(
	UMHGZGameplayAbility* OwningAbility,
	URes_InsectGlaive* InResource,
	const FWeaponActionToken& InActionToken,
	FVector InLaunchDirection,
	const float InForwardOffset,
	const float InRadius,
	const float InMaxSampleStep,
	const FGameplayTag InHitzoneQueryTag,
	const float InLateHitWindowSeconds,
	const bool bInDrawDebug)
{
	if (!IsValidProbeStartup(OwningAbility, InResource, InActionToken,
		InLaunchDirection, InForwardOffset, InRadius, InMaxSampleStep,
		InLateHitWindowSeconds))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[KinsectSlashProbe] Rejected invalid ability, runtime, or probe settings."));
		return nullptr;
	}

	UAbilityTask_MHGZKinsectSlashProbe* Task =
		NewAbilityTask<UAbilityTask_MHGZKinsectSlashProbe>(
			OwningAbility, TEXT("MHGZKinsectSlashProbe"));
	Task->OwnerAbility = OwningAbility;
	Task->Resource = InResource;
	Task->RuntimeHost = OwningAbility->GetRuntimeHost();
	Task->ActionToken = InActionToken;
	Task->LaunchDirection = InLaunchDirection.GetSafeNormal();
	Task->ForwardOffset = InForwardOffset;
	Task->Radius = InRadius;
	Task->MaxSampleStep = InMaxSampleStep;
	Task->HitzoneQueryTag = InHitzoneQueryTag;
	Task->LateHitWindowSeconds = InLateHitWindowSeconds;
	Task->bDrawDebug = bInDrawDebug;
	return Task;
}

void UAbilityTask_MHGZKinsectSlashProbe::Activate()
{
	if (!ValidateCurrentOwner())
	{
		return;
	}
	SetWaitingOnAvatar();
}

void UAbilityTask_MHGZKinsectSlashProbe::TickTask(const float DeltaTime)
{
	Super::TickTask(DeltaTime);
	if (!ValidateCurrentOwner() || bCleanupDone)
	{
		return;
	}

	if (bDashActive)
	{
		UpdateDashProbePosition();
	}
	else if (bLateHitModeActive && bSweepEnabled)
	{
		const float SafeDelta = FMath::IsFinite(DeltaTime)
			? FMath::Max(0.0f, DeltaTime) : 0.0f;
		const float Remaining = FMath::Max(0.0f,
			LateHitWindowSeconds - LateHitElapsedSeconds);
		const float IntegratedDelta = FMath::Min(SafeDelta, Remaining);
		const FVector NextLocation = LastProbeLocation
			+ LateTerminalVelocity * IntegratedDelta;
		FVector ResolvedLocation = NextLocation;
		SweepProbePath(NextLocation, ResolvedLocation);
		LastProbeLocation = ResolvedLocation;
		LateHitElapsedSeconds += IntegratedDelta;

		if (USkeletalMeshComponent* Proxy = VisualProxy.Get())
		{
			Proxy->SetWorldLocation(LastProbeLocation);
		}

		if (LateHitElapsedSeconds >= LateHitWindowSeconds)
		{
			bLateHitModeActive = false;
			bLateWindowClosed = true;
			bSweepEnabled = false;
		}
	}
}

void UAbilityTask_MHGZKinsectSlashProbe::OnDestroy(const bool AbilityIsEnding)
{
	Cleanup();
	Super::OnDestroy(AbilityIsEnding);
}

bool UAbilityTask_MHGZKinsectSlashProbe::BeginDash(
	const FWeaponActionToken& Token)
{
	if (!ValidateMethodToken(Token) || bStarted || bCleanupDone)
	{
		return false;
	}

	ACharacter* AvatarCharacter = Cast<ACharacter>(GetAvatarActor());
	URes_InsectGlaive* CurrentResource = Resource.Get();
	if (!AvatarCharacter || !CurrentResource
		|| !CurrentResource->GetKinsectActor()
		|| !CreateVisualProxy(AvatarCharacter))
	{
		WarnOnce(TEXT("[KinsectSlashProbe] Could not create a visual clone from the live Kinsect."));
		Cleanup();
		EndTask();
		return false;
	}

	Character = AvatarCharacter;
	if (const UCapsuleComponent* Capsule = AvatarCharacter->GetCapsuleComponent())
	{
		LastProbeLocation = Capsule->GetComponentLocation()
			+ LaunchDirection * ForwardOffset;
	}
	else
	{
		LastProbeLocation = AvatarCharacter->GetActorLocation()
			+ LaunchDirection * ForwardOffset;
	}

	if (USkeletalMeshComponent* Proxy = VisualProxy.Get())
	{
		Proxy->SetWorldLocationAndRotation(LastProbeLocation,
			(FQuat(LaunchDirection.Rotation())
				* KinsectFlightVisualLocalRotation).Rotator());
	}
	if (USkeletalMeshComponent* SourceMesh = LiveKinsectMesh.Get())
	{
		bOriginalKinsectVisible = SourceMesh->IsVisible();
		bHasOriginalKinsectVisibility = true;
		SourceMesh->SetVisibility(false, false);
	}

	bStarted = true;
	bDashActive = true;
	bSweepEnabled = true;
	// The hunter may already be beside the target when 187 begins. A short
	// initial sweep catches a Hitzone overlap even before the capsule can move.
	FVector InitialResolvedLocation = LastProbeLocation;
	SweepProbePath(LastProbeLocation + LaunchDirection, InitialResolvedLocation);
	return true;
}

bool UAbilityTask_MHGZKinsectSlashProbe::NotifyDashMovementFinished(
	const FWeaponActionToken& Token, const EWeaponMovementEndReason Reason,
	const FVector AnalyticTerminalVelocity)
{
	if (!ValidateMethodToken(Token) || !bStarted || !bDashActive
		|| bDashMovementFinished)
	{
		return false;
	}

	// A capsule block can end the dash inside the movement tick, before this
	// ability task receives its own tick. Resolve that final travelled segment
	// against Hitzones before retiring the attack probe.
	UpdateDashProbePosition();
	if (bCleanupDone || !ValidateCurrentOwner())
	{
		return false;
	}
	bDashMovementFinished = true;
	if (Reason != EWeaponMovementEndReason::Completed)
	{
		bDashActive = false;
		DisableSweepPermanently();
		return true;
	}

	bDashActive = false;
	if (LateHitWindowSeconds <= 0.0f)
	{
		bSweepEnabled = false;
		bSweepPermanentlyDisabled = true;
		bLateWindowClosed = true;
		bLateWindowRequested = false;
		return true;
	}

	if (!MHGZMath::IsFiniteVector(AnalyticTerminalVelocity)
		|| AnalyticTerminalVelocity.IsNearlyZero())
	{
		DisableSweepPermanently();
		return false;
	}

	LateTerminalVelocity = AnalyticTerminalVelocity;
	bSweepEnabled = false;
	TryEnterLateHitMode();
	return true;
}

bool UAbilityTask_MHGZKinsectSlashProbe::OpenLateHitWindow(
	const FWeaponActionToken& Token)
{
	if (!ValidateMethodToken(Token) || !bStarted || bLateWindowRequested
		|| bLateWindowClosed || bSweepPermanentlyDisabled)
	{
		return false;
	}

	bLateWindowRequested = true;
	TryEnterLateHitMode();
	return true;
}

bool UAbilityTask_MHGZKinsectSlashProbe::CloseLateHitWindow(
	const FWeaponActionToken& Token)
{
	if (!ValidateMethodToken(Token) || !bStarted || bLateWindowClosed)
	{
		return false;
	}

	bLateWindowClosed = true;
	bLateWindowRequested = false;
	bLateHitModeActive = false;
	bSweepEnabled = false;
	bSweepPermanentlyDisabled = true;
	return true;
}

bool UAbilityTask_MHGZKinsectSlashProbe::DisableAttackSweep(
	const FWeaponActionToken& Token)
{
	if (!ValidateMethodToken(Token) || !bStarted || bSweepPermanentlyDisabled)
	{
		return false;
	}

	DisableSweepPermanently();
	return true;
}

bool UAbilityTask_MHGZKinsectSlashProbe::AcknowledgeSuccessfulContact(
	const FWeaponActionToken& Token)
{
	if (!ValidateMethodToken(Token) || !bDispatchingContact)
	{
		return false;
	}

	bContactAccepted = true;
	return true;
}

bool UAbilityTask_MHGZKinsectSlashProbe::EndVisual(
	const FWeaponActionToken& Token)
{
	if (!ValidateMethodToken(Token) || !bStarted || bVisualEnded)
	{
		return false;
	}

	DestroyVisualProxyAndRestoreKinsect();
	bVisualEnded = true;
	return true;
}

bool UAbilityTask_MHGZKinsectSlashProbe::DidStartProbe() const
{
	if (!IsCurrentOwnerContext())
	{
		const_cast<UAbilityTask_MHGZKinsectSlashProbe*>(this)->AbortForOwnershipLoss();
		return false;
	}
	return bStarted;
}

bool UAbilityTask_MHGZKinsectSlashProbe::IsCurrentOwnerContext() const
{
	UMHGZGameplayAbility* SlashAbility = OwnerAbility.Get();
	URes_InsectGlaive* CurrentResource = Resource.Get();
	UMHGZWeaponRuntimeHostComponent* Host = RuntimeHost.Get();
	return SlashAbility && CurrentResource && Host && ActionToken.IsValid()
		&& SlashAbility->IsActive() && SlashAbility->IsActionActivationCommitted()
		&& SlashAbility->GetActionToken() == ActionToken
		&& ActionToken.AbilityInstance.Get() == SlashAbility
		&& SlashAbility->GetRuntimeHost() == Host
		&& ActionToken.RuntimeToken.Host.Get() == Host
		&& Host->IsTokenCurrent(ActionToken.RuntimeToken)
		&& CurrentResource->GetRuntimeContext().RuntimeToken == ActionToken.RuntimeToken
		&& CurrentResource->IsRuntimeRequestCurrent(ActionToken.RuntimeToken);
}

bool UAbilityTask_MHGZKinsectSlashProbe::ValidateCurrentOwner()
{
	if (IsCurrentOwnerContext())
	{
		return true;
	}

	AbortForOwnershipLoss();
	return false;
}

bool UAbilityTask_MHGZKinsectSlashProbe::ValidateMethodToken(
	const FWeaponActionToken& Token)
{
	if (!ValidateCurrentOwner())
	{
		return false;
	}
	return !bCleanupDone && Token == ActionToken;
}

void UAbilityTask_MHGZKinsectSlashProbe::AbortForOwnershipLoss()
{
	if (bCleanupDone)
	{
		return;
	}
	WarnOnce(TEXT("[KinsectSlashProbe] Action or runtime ownership was lost; cleaning up."));
	Cleanup();
	EndTask();
}

void UAbilityTask_MHGZKinsectSlashProbe::WarnOnce(const TCHAR* Message)
{
	if (!bWarningIssued)
	{
		bWarningIssued = true;
		UE_LOG(LogTemp, Warning, TEXT("%s"), Message);
	}
}

bool UAbilityTask_MHGZKinsectSlashProbe::CreateVisualProxy(ACharacter* AvatarCharacter)
{
	URes_InsectGlaive* CurrentResource = Resource.Get();
	AKinsect* KinsectActor = CurrentResource
		? CurrentResource->GetKinsectActor() : nullptr;
	USkeletalMeshComponent* SourceMesh = KinsectActor ? KinsectActor->Mesh : nullptr;
	if (!AvatarCharacter || !SourceMesh || !SourceMesh->GetSkeletalMeshAsset()
		|| !AvatarCharacter->GetRootComponent())
	{
		return false;
	}

	USkeletalMeshComponent* Proxy = NewObject<USkeletalMeshComponent>(
		AvatarCharacter, NAME_None, RF_Transient);
	if (!Proxy)
	{
		return false;
	}

	Proxy->SetSkeletalMesh(SourceMesh->GetSkeletalMeshAsset());
	if (UClass* AnimClass = SourceMesh->GetAnimClass())
	{
		Proxy->SetAnimInstanceClass(AnimClass);
	}
	for (int32 MaterialIndex = 0; MaterialIndex < SourceMesh->GetNumMaterials(); ++MaterialIndex)
	{
		if (UMaterialInterface* Material = SourceMesh->GetMaterial(MaterialIndex))
		{
			Proxy->SetMaterial(MaterialIndex, Material);
		}
	}

	Proxy->SetRelativeScale3D(SourceMesh->GetRelativeScale3D());
	Proxy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Proxy->SetGenerateOverlapEvents(false);
	Proxy->SetVisibility(true, true);
	AvatarCharacter->AddInstanceComponent(Proxy);
	Proxy->SetupAttachment(AvatarCharacter->GetRootComponent());
	Proxy->RegisterComponent();
	if (!Proxy->IsRegistered())
	{
		AvatarCharacter->RemoveInstanceComponent(Proxy);
		Proxy->DestroyComponent();
		return false;
	}

	LiveKinsectMesh = SourceMesh;
	VisualProxy = Proxy;
	return true;
}

void UAbilityTask_MHGZKinsectSlashProbe::UpdateDashProbePosition()
{
	ACharacter* AvatarCharacter = Character.Get();
	if (!AvatarCharacter)
	{
		return;
	}

	FVector CurrentLocation = AvatarCharacter->GetActorLocation();
	if (const UCapsuleComponent* Capsule = AvatarCharacter->GetCapsuleComponent())
	{
		CurrentLocation = Capsule->GetComponentLocation();
	}
	CurrentLocation += LaunchDirection * ForwardOffset;

	if (bSweepEnabled)
	{
		FVector ResolvedLocation = CurrentLocation;
		SweepProbePath(CurrentLocation, ResolvedLocation);
		CurrentLocation = ResolvedLocation;
	}
	LastProbeLocation = CurrentLocation;

	if (USkeletalMeshComponent* Proxy = VisualProxy.Get())
	{
		Proxy->SetWorldLocationAndRotation(CurrentLocation,
			(FQuat(LaunchDirection.Rotation())
				* KinsectFlightVisualLocalRotation).Rotator());
	}
}

bool UAbilityTask_MHGZKinsectSlashProbe::SweepProbePath(
	const FVector& Destination, FVector& OutResolvedLocation)
{
	OutResolvedLocation = Destination;
	const FVector Delta = Destination - LastProbeLocation;
	const float Distance = Delta.Size();
	if (!FMath::IsFinite(Distance) || Distance <= UE_SMALL_NUMBER)
	{
		return false;
	}

	const int32 SubstepCount = FMath::Max(1,
		FMath::CeilToInt(Distance / MaxSampleStep));
	FVector SegmentStart = LastProbeLocation;
	for (int32 StepIndex = 1; StepIndex <= SubstepCount && bSweepEnabled; ++StepIndex)
	{
		const float Alpha = static_cast<float>(StepIndex)
			/ static_cast<float>(SubstepCount);
		const FVector SegmentEnd = FMath::Lerp(LastProbeLocation, Destination, Alpha);
		FVector SegmentResolvedLocation = SegmentEnd;
		const ESweepSegmentResult Result = SweepProbeSegment(
			SegmentStart, SegmentEnd, SegmentResolvedLocation);
		if (Result != ESweepSegmentResult::Continue)
		{
			OutResolvedLocation = SegmentResolvedLocation;
			return true;
		}
		SegmentStart = SegmentEnd;
	}
	return false;
}

UAbilityTask_MHGZKinsectSlashProbe::ESweepSegmentResult
UAbilityTask_MHGZKinsectSlashProbe::SweepProbeSegment(
	const FVector& Start, const FVector& End, FVector& OutResolvedLocation)
{
	OutResolvedLocation = End;
	UWorld* World = GetWorld();
	ACharacter* AvatarCharacter = Character.Get();
	if (!World || !AvatarCharacter || !bSweepEnabled)
	{
		return ESweepSegmentResult::Continue;
	}

	TArray<FHitResult> HitzoneHits;
	TArray<FHitResult> ObstacleHits;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MHGZKinsectSlashProbe), false,
		AvatarCharacter);
	QueryParams.AddIgnoredActor(AvatarCharacter);
	const FCollisionShape ProbeShape = FCollisionShape::MakeSphere(Radius);

	// Damage identity comes only from the dedicated Hitzone object type. This
	// deliberately excludes the monster's enclosing Pawn/body collision.
	FCollisionObjectQueryParams HitzoneQuery;
	HitzoneQuery.AddObjectTypesToQuery(ECC_GameTraceChannel3);
	World->SweepMultiByObjectType(HitzoneHits, Start, End, FQuat::Identity,
		HitzoneQuery, ProbeShape, QueryParams);

	// Geometry occlusion is independent of Hitzone identity. Query world object
	// channels (which MonsterHitzone explicitly ignores) rather than Visibility,
	// since Hitzones block Visibility and could hide a wall later in a multi-sweep.
	World->SweepMultiByChannel(ObstacleHits, Start, End, FQuat::Identity,
		ECC_WorldStatic, ProbeShape, QueryParams);
	World->SweepMultiByChannel(ObstacleHits, Start, End, FQuat::Identity,
		ECC_WorldDynamic, ProbeShape, QueryParams);

	if (bDrawDebug)
	{
		DrawDebugLine(World, Start, End, FColor::Red, false, 0.1f, 0, 1.25f);
		DrawDebugSphere(World, End, Radius, 8, FColor::Yellow, false, 0.1f);
	}

	struct FProbeCandidate
	{
		FHitResult Hit;
		bool bWorldBlocker = false;
	};
	TArray<FProbeCandidate> Candidates;
	Candidates.Reserve(HitzoneHits.Num() + ObstacleHits.Num());
	for (const FHitResult& Hit : HitzoneHits)
	{
		if (UMHGZMonsterHitzoneComponent::Resolve(Hit.GetComponent()))
		{
			Candidates.Add({Hit, false});
		}
	}
	for (const FHitResult& Hit : ObstacleHits)
	{
		AActor* HitActor = Hit.GetActor();
		const bool bBelongsToHitzoneActor = HitActor
			&& HitActor->FindComponentByClass<UMHGZMonsterHitzoneComponent>();
		if (Hit.bBlockingHit && !bBelongsToHitzoneActor
			&& !UMHGZMonsterHitzoneComponent::Resolve(Hit.GetComponent()))
		{
			Candidates.Add({Hit, true});
		}
	}

	Candidates.Sort([](const FProbeCandidate& Left, const FProbeCandidate& Right)
	{
		if (!FMath::IsNearlyEqual(Left.Hit.Time, Right.Hit.Time, KINDA_SMALL_NUMBER))
		{
			return Left.Hit.Time < Right.Hit.Time;
		}
		// A wall at the same swept time wins ties, preventing edge contacts from
		// resolving damage through geometry.
		return Left.bWorldBlocker && !Right.bWorldBlocker;
	});

	for (const FProbeCandidate& Candidate : Candidates)
	{
		const FHitResult& Hit = Candidate.Hit;
		if (Candidate.bWorldBlocker)
		{
			OutResolvedLocation = Hit.Location;
			bDashActive = false;
			DisableSweepPermanently();
			return ESweepSegmentResult::WorldBlocked;
		}

		UMHGZMonsterHitzoneComponent* Hitzone =
			UMHGZMonsterHitzoneComponent::Resolve(Hit.GetComponent());
		if (!Hitzone || (HitzoneQueryTag.IsValid()
			&& !Hitzone->HitzoneTag.MatchesTagExact(HitzoneQueryTag)))
		{
			continue;
		}

		bContactAccepted = false;
		bDispatchingContact = true;
		OnHitzoneContact.Broadcast(ActionToken, Hit);
		bDispatchingContact = false;
		if (!ValidateCurrentOwner())
		{
			return ESweepSegmentResult::OwnerLost;
		}
		if (bContactAccepted)
		{
			UE_LOG(LogMHGZ, Display,
				TEXT("[KinsectSlashProbe] Accepted Hitzone=%s actor=%s impact=%s"),
				*Hitzone->HitzoneTag.ToString(), *GetNameSafe(Hit.GetActor()),
				*Hit.ImpactPoint.ToCompactString());
			OutResolvedLocation = Hit.Location;
			bDashActive = false;
			bSweepEnabled = false;
			return ESweepSegmentResult::AcceptedContact;
		}
	}
	return ESweepSegmentResult::Continue;
}

void UAbilityTask_MHGZKinsectSlashProbe::TryEnterLateHitMode()
{
	if (!bStarted || !bLateWindowRequested || !bDashMovementFinished
		|| bLateWindowClosed || bSweepPermanentlyDisabled
		|| LateHitWindowSeconds <= 0.0f)
	{
		if (LateHitWindowSeconds <= 0.0f && bLateWindowRequested
			&& bDashMovementFinished)
		{
			bLateWindowClosed = true;
			bLateWindowRequested = false;
		}
		return;
	}

	bDashActive = false;
	bLateHitModeActive = true;
	bSweepEnabled = true;
	LateHitElapsedSeconds = 0.0f;
	if (USkeletalMeshComponent* Proxy = VisualProxy.Get())
	{
		Proxy->SetWorldLocation(LastProbeLocation);
		Proxy->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	}
}

void UAbilityTask_MHGZKinsectSlashProbe::DisableSweepPermanently()
{
	bSweepEnabled = false;
	bSweepPermanentlyDisabled = true;
	bLateWindowClosed = true;
	bLateWindowRequested = false;
	bLateHitModeActive = false;
}

void UAbilityTask_MHGZKinsectSlashProbe::DestroyVisualProxyAndRestoreKinsect()
{
	if (USkeletalMeshComponent* Proxy = VisualProxy.Get())
	{
		if (AActor* ProxyOwner = Proxy->GetOwner())
		{
			ProxyOwner->RemoveInstanceComponent(Proxy);
		}
		Proxy->DestroyComponent();
	}
	VisualProxy.Reset();

	if (bHasOriginalKinsectVisibility)
	{
		if (USkeletalMeshComponent* SourceMesh = LiveKinsectMesh.Get())
		{
			SourceMesh->SetVisibility(bOriginalKinsectVisible, false);
		}
		bHasOriginalKinsectVisibility = false;
	}
	LiveKinsectMesh.Reset();
}

void UAbilityTask_MHGZKinsectSlashProbe::Cleanup()
{
	if (bCleanupDone)
	{
		return;
	}
	bCleanupDone = true;
	bDashActive = false;
	bLateHitModeActive = false;
	bSweepEnabled = false;
	bLateWindowClosed = true;
	bLateWindowRequested = false;
	DestroyVisualProxyAndRestoreKinsect();
}
