// Copyright MHGZ Project. All Rights Reserved.
#include "AnimNotify_CategorizedSound.h"
#include "MHGZAnimNotifyActionResolver.h"
#include "MHGZ.h"

void UAnimNotify_GroundDust::Notify(USkeletalMeshComponent* Mesh, UAnimSequenceBase*,
	const FAnimNotifyEventReference& EventReference)
{
	const auto* Context = EventReference.GetContextData<UE::Anim::FAnimNotifyMontageInstanceContext>();
	if (Context) { DispatchGroundDust(Mesh, Context->MontageInstanceID); }
}

bool UAnimNotify_GroundDust::DispatchGroundDust(USkeletalMeshComponent* Mesh, int32 InstanceID) const
{
	if (!IsCurrentPresentation(Mesh, InstanceID) || Mesh->GetNetMode() == NM_DedicatedServer
		|| Source != EMHGZAnimationSoundSource::GroundContact) { return false; }
	auto* Host = CastChecked<AMHGZCharacter>(Mesh->GetOwner())->GetWeaponRuntimeHost();
	USkeletalMeshComponent* Weapon = Host->GetWeaponVisualComponent();
	USceneComponent* Component = nullptr;
	FVector Location, Normal;
	FName Reason;
	if (!ResolveEmission(Mesh, Weapon, Component, Location, Reason, &Normal))
	{
		return false;
	}
	// No Sound prerequisite: subscribers receive the effect event even without
	// audio/media or when concurrency rejects playback. No Niagara asset yet.
	Host->OnGroundDust.Broadcast(Mesh, Location, Normal);
	PlayOneShot(Mesh, Weapon, Sound);
	return true;
}

void UAnimNotify_GroundImpact::Notify(USkeletalMeshComponent* Mesh, UAnimSequenceBase*,
	const FAnimNotifyEventReference& EventReference)
{
	const auto* Context = EventReference.GetContextData<UE::Anim::FAnimNotifyMontageInstanceContext>();
	if (Context) { DispatchGroundImpact(Mesh, Context->MontageInstanceID); }
}

bool UAnimNotify_GroundImpact::DispatchGroundImpact(USkeletalMeshComponent* Mesh, int32 InstanceID) const
{
	if (!IsCurrentPresentation(Mesh, InstanceID) || Mesh->GetNetMode() == NM_DedicatedServer
		|| Source != EMHGZAnimationSoundSource::GroundContact) { return false; }
	auto* Host = CastChecked<AMHGZCharacter>(Mesh->GetOwner())->GetWeaponRuntimeHost();
	USkeletalMeshComponent* Weapon = Host->GetWeaponVisualComponent();
	USceneComponent* Component = nullptr;
	FVector Location, Normal;
	FName Reason;
	if (!ResolveEmission(Mesh, Weapon, Component, Location, Reason, &Normal)) { return false; }
	// Independent presentation hook, even with no audio or enemy hit. Existing
	// separately timed dust points are not broadcast again by a ground strike.
	Host->OnGroundImpact.Broadcast(Mesh, Location, Normal);
	PlayOneShot(Mesh, Weapon, Sound);
	return true;
}

void UAnimNotify_LandingEvent::Notify(USkeletalMeshComponent* Mesh, UAnimSequenceBase*,
	const FAnimNotifyEventReference& EventReference)
{
	const auto* Context = EventReference.GetContextData<UE::Anim::FAnimNotifyMontageInstanceContext>();
	if (Context) { DispatchLanding(Mesh, Context->MontageInstanceID); }
}

bool UAnimNotify_LandingEvent::DispatchLanding(USkeletalMeshComponent* Mesh, int32 InstanceID) const
{
	if (!IsCurrentPresentation(Mesh, InstanceID) || Mesh->GetNetMode() == NM_DedicatedServer
		|| Source != EMHGZAnimationSoundSource::GroundContact) { return false; }
	auto* Host = CastChecked<AMHGZCharacter>(Mesh->GetOwner())->GetWeaponRuntimeHost();
	USkeletalMeshComponent* Weapon = Host->GetWeaponVisualComponent();
	USceneComponent* Component = nullptr;
	FVector Location, Normal;
	FName Reason;
	if (!ResolveEmission(Mesh, Weapon, Component, Location, Reason, &Normal)) { return false; }
	Host->OnLanding.Broadcast(Mesh, Location, Normal);
	// Listeners may destroy the pawn. Never access an invalid Host after a callback.
	if (bTriggerGroundDust && IsValid(Host) && IsValid(Mesh))
	{
		Host->OnGroundDust.Broadcast(Mesh, Location, Normal);
	}
	PlayOneShot(Mesh, Weapon, Sound);
	if (bTriggerGroundDust) { PlayOneShot(Mesh, Weapon, GroundDustSound); }
	return true;
}

UAnimNotify_ClothSound::UAnimNotify_ClothSound()
{
	Source = EMHGZAnimationSoundSource::CharacterSocket;
	AttachSocket = TEXT("Spine_00");
}
