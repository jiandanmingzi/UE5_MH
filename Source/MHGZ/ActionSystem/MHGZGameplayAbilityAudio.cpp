// Copyright MHGZ Project. All Rights Reserved.
#include "MHGZGameplayAbility.h"
#include "AnimNotify_SwingSound.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Equipment/MHGZEquipmentDefinition.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "MHGZ.h"
#include "Sound/SoundBase.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

namespace
{
TAutoConsoleVariable<int32> CVarSwingAudioDebug(TEXT("mhgz.Audio.SwingDebug"), 0,
	TEXT("1: log resolved one-shot dispatch/missing audio. Not proof of audible output."));
}

bool UMHGZGameplayAbility::IsSwingSoundActionCurrent(const FWeaponActionToken& Action) const
{
	const UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	return bUseAuthoredSwingAudio && IsActionActivationCommitted()
		&& Action == GetActionToken() && Host && Host->IsActionRegistered(Action)
		&& Host->IsTokenCurrent(Action.RuntimeToken);
}

void UMHGZGameplayAbility::PrepareSwingSoundOverrides()
{
	ResetSwingSoundOverrides();
	if (!IsSwingSoundActionCurrent(GetActionToken())) return;
	PreparedSwingWeapon = GetRuntimeHost()->GetCurrentContext().WeaponDefinition;
	if (!PreparedSwingWeapon) return;
	RefreshLoadedSwingSounds(GetActionToken());
	TArray<FSoftObjectPath> Paths;
	for (const auto& Pair : PreparedSwingWeapon->SwingSoundOverrides)
	{
		if (Pair.Key.IsValid() && !Pair.Value.IsNull() && !Pair.Value.Get()) Paths.AddUnique(Pair.Value.ToSoftObjectPath());
	}
	if (Paths.IsEmpty()) return;
	const FWeaponActionToken Expected = GetActionToken();
	SwingSoundLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths,
		FStreamableDelegate::CreateWeakLambda(this, [this, Expected]()
		{
			RefreshLoadedSwingSounds(Expected);
		}));
}

void UMHGZGameplayAbility::RefreshLoadedSwingSounds(const FWeaponActionToken& ExpectedAction)
{
	if (!IsSwingSoundActionCurrent(ExpectedAction) || !PreparedSwingWeapon) return;
	// Get() only; never TryLoad/LoadSynchronous in a Notify or completion callback.
	for (const auto& Pair : PreparedSwingWeapon->SwingSoundOverrides)
	{
		if (Pair.Key.IsValid())
		{
			if (USoundBase* Loaded = Pair.Value.Get()) LoadedSwingSoundOverrides.Add(Pair.Key, Loaded);
		}
	}
}

void UMHGZGameplayAbility::ResetSwingSoundOverrides()
{
	if (SwingSoundLoadHandle) SwingSoundLoadHandle->CancelHandle();
	SwingSoundLoadHandle.Reset();
	LoadedSwingSoundOverrides.Reset();
	PreparedSwingWeapon = nullptr;
	MissingSwingSoundKeys.Reset();
}

USoundBase* UMHGZGameplayAbility::ResolveSwingSound(const FWeaponActionToken& Action,
	USkeletalMeshComponent* Mesh, int32 MontageInstanceID, const UAnimNotify_SwingSound& Recipe) const
{
	if (!IsSwingSoundActionCurrent(Action) || !Mesh
		|| Mesh->GetOwner() != GetAvatarActorFromActorInfo()) return nullptr;
	FWeaponActionToken Registered;
	if (!GetRuntimeHost()->ResolveMontage(Mesh, MontageInstanceID, Registered)
		|| Registered != Action) return nullptr;
	// Ground/character action layers must not inherit a GA-level weapon-wind override.
	if (Recipe.Source != EMHGZAnimationSoundSource::WeaponSocket) return Recipe.Sound.Get();
	const auto FindLoaded = [this](FGameplayTag Key) -> USoundBase*
	{
		const TObjectPtr<USoundBase>* Found = Key.IsValid() ? LoadedSwingSoundOverrides.Find(Key) : nullptr;
		return Found ? Found->Get() : nullptr;
	};
	if (USoundBase* Found = FindLoaded(Recipe.EventTag)) return Found;
	if (USoundBase* Found = FindLoaded(AudioIdentityTag)) return Found;
	return Recipe.Sound ? Recipe.Sound.Get() : GetLegacySwingSound(Recipe.LegacyAttackSegmentIndex);
}

bool UMHGZGameplayAbility::HandleSwingSound(const FWeaponActionToken& Action,
	USkeletalMeshComponent* Mesh, int32 MontageInstanceID, const UAnimNotify_SwingSound& Recipe)
{
	USoundBase* Sound = ResolveSwingSound(Action, Mesh, MontageInstanceID, Recipe);
	if (!Sound || Sound->IsLooping())
	{
		if (CVarSwingAudioDebug.GetValueOnGameThread() != 0)
		{
			UE_LOG(LogMHGZ, Display, TEXT("[SwingAudio] skipped action=%u montage=%d key=%s current=%d sound=%s (missing/context mismatch/looping)"),
				Action.ActivationSequenceID, MontageInstanceID, *Recipe.EventTag.ToString(),
				IsSwingSoundActionCurrent(Action), *GetNameSafe(Sound));
		}
		if (IsSwingSoundActionCurrent(Action) && !MissingSwingSoundKeys.Contains(Recipe.EventTag))
		{
			MissingSwingSoundKeys.Add(Recipe.EventTag);
			UE_LOG(LogMHGZ, Verbose, TEXT("[SwingAudio] %s key=%s missing, rejected context or looping (one-shot only)"),
				*GetName(), *Recipe.EventTag.ToString());
		}
		return false;
	}
	return Recipe.PlayOneShot(Mesh, GetRuntimeHost()->GetWeaponVisualComponent(), Sound);
}
