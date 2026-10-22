// Copyright MHGZ Project. All Rights Reserved.
#include "AnimNotify_SwingSound.h"
#include "MHGZAnimNotifyActionResolver.h"
#include "MHGZGameplayAbility.h"
#include "HAL/IConsoleManager.h"
#include "MHGZ.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

bool UAnimNotify_SwingSound::ResolveEmission(USkeletalMeshComponent* Mesh, USceneComponent* Weapon,
	USceneComponent*& OutComponent, FVector& OutLocation, FName& OutReason, FVector* OutSurfaceNormal) const
{
	OutComponent = nullptr;
	OutLocation = FVector::ZeroVector;
	if (OutSurfaceNormal) { *OutSurfaceNormal = FVector::UpVector; }
	OutReason = TEXT("MissingMeshOrSocket");
	if (!IsValid(Mesh) || AttachSocket.IsNone()) return false;
	USceneComponent* Component = Source == EMHGZAnimationSoundSource::WeaponSocket ? Weapon : Mesh;
	if (!IsValid(Component) || !Component->DoesSocketExist(AttachSocket)) return false;
	OutLocation = Component->GetSocketLocation(AttachSocket);
	if (OutLocation.ContainsNaN()) { OutReason = TEXT("InvalidLocation"); return false; }
	if (Source == EMHGZAnimationSoundSource::GroundContact)
	{
		if (!Mesh->GetWorld() || !FMath::IsFinite(GroundTraceDistance) || GroundTraceDistance <= 0.f) return false;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(AnimationSoundGround), false, Mesh->GetOwner());
		FHitResult Hit;
		if (!Mesh->GetWorld()->LineTraceSingleByChannel(Hit, OutLocation + FVector(0, 0, 50),
			OutLocation - FVector(0, 0, GroundTraceDistance), ECC_Visibility, Query)
			|| Hit.ImpactNormal.Z < .5f)
		{
			OutReason = TEXT("NoGroundContact"); return false;
		}
		OutLocation = Hit.ImpactPoint;
		if (OutSurfaceNormal) { *OutSurfaceNormal = Hit.ImpactNormal; }
	}
	else OutComponent = Component;
	OutReason = NAME_None;
	return true;
}

bool UAnimNotify_SwingSound::PlayOneShot(USkeletalMeshComponent* Mesh, USceneComponent* Weapon,
	USoundBase* ResolvedSound) const
{
	USceneComponent* Component = nullptr;
	FVector Location;
	FName Reason;
	const bool bSourceValid = ResolveEmission(Mesh, Weapon, Component, Location, Reason);
	if (!bSourceValid && Reason != TEXT("NoGroundContact"))
	{
		UE_LOG(LogMHGZ, Warning, TEXT("[AnimationAudio] source rejected key=%s mesh=%s socket=%s reason=%s; no origin fallback"),
			*EventTag.ToString(), *GetNameSafe(Mesh), *AttachSocket.ToString(), *Reason.ToString());
	}
	UAudioComponent* Audio = nullptr;
	if (bSourceValid && ResolvedSound && !ResolvedSound->IsLooping()
		&& Mesh->GetNetMode() != NM_DedicatedServer
		&& FMath::IsFinite(VolumeMultiplier) && VolumeMultiplier > 0.f
		&& FMath::IsFinite(PitchMultiplier) && PitchMultiplier > 0.f)
	{
		Audio = Component
			? UGameplayStatics::SpawnSoundAttached(ResolvedSound, Component, AttachSocket,
				FVector::ZeroVector, EAttachLocation::KeepRelativeOffset, true, VolumeMultiplier, PitchMultiplier)
			: UGameplayStatics::SpawnSoundAtLocation(Mesh, ResolvedSound, Location,
				FRotator::ZeroRotator, VolumeMultiplier, PitchMultiplier);
	}
	const IConsoleVariable* Debug = IConsoleManager::Get().FindConsoleVariable(TEXT("mhgz.Audio.SwingDebug"));
	if (Debug && Debug->GetInt() != 0)
	{
		UE_LOG(LogMHGZ, Display, TEXT("[AnimationAudio] key=%s sound=%s source=%d socket=%s component=%s position=%s dispatched=%d reason=%s"),
			*EventTag.ToString(), *GetPathNameSafe(ResolvedSound), int32(Source), *AttachSocket.ToString(),
			*GetNameSafe(Component), *Location.ToCompactString(), Audio != nullptr,
			bSourceValid ? (Audio ? TEXT("Created") : TEXT("AudioDeviceOrSoundRejected")) : *Reason.ToString());
	}
	return Audio != nullptr;
}

void UAnimNotify_SwingSound::Notify(USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	FWeaponActionToken Action;
	int32 InstanceID = INDEX_NONE;
	// Cosmetic but still instance-strict: an old blending montage must not play
	// the new action's recipe. Preview/unregistered/asset-only events are ignored.
	if (MHGZ::AnimNotify::ResolveAction(MeshComp, EventReference, Action,
		Animation, &InstanceID, true))
	{
		if (UMHGZGameplayAbility* Ability = Cast<UMHGZGameplayAbility>(Action.AbilityInstance.Get()))
		{
			Ability->HandleSwingSound(Action, MeshComp, InstanceID, *this);
		}
	}
	else if (const IConsoleVariable* Debug = IConsoleManager::Get().FindConsoleVariable(TEXT("mhgz.Audio.SwingDebug"));
		Debug && Debug->GetInt() != 0)
	{
		UE_LOG(LogMHGZ, Display, TEXT("[SwingAudio] ignored key=%s: no registered exact Montage-instance context"), *EventTag.ToString());
	}
}

FString UAnimNotify_SwingSound::GetNotifyName_Implementation() const
{
	return EventTag.IsValid() ? FString::Printf(TEXT("Swing %s"), *EventTag.ToString())
		: TEXT("Weapon Swing Sound");
}
