// Copyright MHGZ Project. All Rights Reserved.
#include "MHGZWeaponHitSoundCue.h"

#include "ActionSystem/MHGZGameplayEffectContext.h"
#include "Components/AudioComponent.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

namespace
{
TAutoConsoleVariable<int32> CVarHitSoundDebug(TEXT("mhgz.Audio.HitDebug"), 0,
	TEXT("Log resolved weapon contact sound dispatch; 0=off, 1=on."));
}

bool UMHGZWeaponHitSoundCue::ResolveWeaponHitLocation(AActor* Target,
	const FGameplayEffectContextHandle& ContextHandle, FVector& OutLocation)
{
	return ResolveContactLocation(Target, ContextHandle, EMHGZDamageSourceType::Weapon, OutLocation);
}

bool UMHGZWeaponHitSoundCue::ResolveContactLocation(AActor* Target,
	const FGameplayEffectContextHandle& ContextHandle,
	const EMHGZDamageSourceType ExpectedSource, FVector& OutLocation)
{
	const auto* Context = FMHGZGameplayEffectContext::ExtractEffectContext(ContextHandle);
	const FHitResult* Hit = Context ? Context->GetHitResult() : nullptr;
	const UPrimitiveComponent* Component = Hit ? Hit->GetComponent() : nullptr;
	if ((ExpectedSource != EMHGZDamageSourceType::Weapon && ExpectedSource != EMHGZDamageSourceType::Kinsect)
		|| !IsValid(Target) || !Context || Context->DamageSourceType != ExpectedSource
		|| !IsValid(Component) || Component->GetOwner() != Target || Hit->ImpactPoint.ContainsNaN())
	{
		return false;
	}
	// Origin is a valid contact, never replace it with the actor's pivot.
	OutLocation = Hit->ImpactPoint;
	return true;
}

bool UMHGZWeaponHitSoundCue::OnExecute_Implementation(AActor* MyTarget,
	const FGameplayCueParameters& Parameters) const
{
	FVector Location;
	if (!ResolveContactLocation(MyTarget, Parameters.EffectContext, ContactSource, Location)
		|| !MyTarget->GetWorld() || MyTarget->GetNetMode() == NM_DedicatedServer
		|| !HitSound || HitSound->IsLooping()
		|| !FMath::IsFinite(VolumeMultiplier) || VolumeMultiplier <= 0.f
		|| !FMath::IsFinite(PitchMultiplier) || PitchMultiplier <= 0.f)
	{
		return false;
	}

	// Short resident one-shot at the true impact; no load, attachment, damage or traces.
	UAudioComponent* Audio = UGameplayStatics::SpawnSoundAtLocation(MyTarget,
		HitSound, Location, FRotator::ZeroRotator, VolumeMultiplier, PitchMultiplier);
	if (CVarHitSoundDebug.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogTemp, Display, TEXT("[%s] target=%s sound=%s location=%s dispatched=%d"),
			ContactSource == EMHGZDamageSourceType::Weapon ? TEXT("WeaponHitAudio") : TEXT("KinsectHitAudio"),
			*GetNameSafe(MyTarget), *GetPathNameSafe(HitSound), *Location.ToCompactString(), Audio != nullptr);
	}
	return Audio != nullptr;
}
