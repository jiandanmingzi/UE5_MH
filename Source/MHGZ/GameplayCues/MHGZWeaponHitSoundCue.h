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
	/** 该Cue接受的伤害来源；武器保持Weapon，猎虫Cue只接受Kinsect上下文。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MHGZ|Audio")
	EMHGZDamageSourceType ContactSource = EMHGZDamageSourceType::Weapon;

	/** 该命中Cue使用的接触声音资源。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MHGZ|Audio")
	TObjectPtr<USoundBase> HitSound;

	/** 在音源与其他混音设置基础上叠加的音量倍率。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MHGZ|Audio", meta = (ClampMin = "0"))
	float VolumeMultiplier = 1.f;

	/** 声音播放音高倍率；同时影响该次音源的播放速度。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MHGZ|Audio", meta = (ClampMin = "0.01"))
	float PitchMultiplier = 1.f;
};
