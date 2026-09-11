// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "AbilityTask_MHGZPlayMontageAndWait.generated.h"

/**
 * GAS montage task with a per-play Blend In override. It deliberately keeps
 * UAbilityTask_PlayMontageAndWait's delegate and teardown contract while
 * routing playback through UMHGZAbilitySystemComponent.
 */
UCLASS()
class MHGZ_API UAbilityTask_MHGZPlayMontageAndWait : public UAbilityTask_PlayMontageAndWait
{
	GENERATED_BODY()

public:
	static UAbilityTask_MHGZPlayMontageAndWait* CreatePlayMontageAndWaitProxy(
		UGameplayAbility* OwningAbility, FName TaskInstanceName,
		UAnimMontage* MontageToPlay, float Rate, FName StartSection,
		float BlendInTime, bool bStopWhenAbilityEnds = true,
		float AnimRootMotionTranslationScale = 1.0f,
		float StartTimeSeconds = 0.0f,
		bool bAllowInterruptAfterBlendOut = false);

	virtual void Activate() override;

private:
	/** Negative is never stored here: callers resolve asset fallback first. */
	float BlendInTime = 0.0f;
};
