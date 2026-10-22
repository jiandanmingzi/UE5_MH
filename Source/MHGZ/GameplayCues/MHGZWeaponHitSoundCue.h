// Copyright MHGZ Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "ActionSystem/MHGZGameplayEffectContext.h"
#include "GameplayCueNotify_Static.h"
#include "MHGZWeaponHitSoundCue.generated.h"

class USoundBase;

/** Resident contact sound leaf. Defaults to weapon; kinsect reuses its existing hit Cue. */
UCLASS(Blueprintable)
class MHGZ_API UMHGZWeaponHitSoundCue : public UGameplayCueNotify_Static
{
	GENERATED_BODY()

public:
	virtual bool OnExecute_Implementation(AActor* MyTarget,
		const FGameplayCueParameters& Parameters) const override;

	/** No HP test or action-wide dedupe: zero-damage contacts and multi-hits remain valid. */
	static bool ResolveWeaponHitLocation(AActor* Target,
		const FGameplayEffectContextHandle& ContextHandle, FVector& OutLocation);

	static bool ResolveContactLocation(AActor* Target,
		const FGameplayEffectContextHandle& ContextHandle,
		EMHGZDamageSourceType ExpectedSource, FVector& OutLocation);

protected:
	/** Existing weapon assets keep Weapon. A kinsect Cue accepts only Kinsect contexts. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MHGZ|Audio")
	EMHGZDamageSourceType ContactSource = EMHGZDamageSourceType::Weapon;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MHGZ|Audio")
	TObjectPtr<USoundBase> HitSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MHGZ|Audio", meta = (ClampMin = "0"))
	float VolumeMultiplier = 1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MHGZ|Audio", meta = (ClampMin = "0.01"))
	float PitchMultiplier = 1.f;
};
