// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/Tasks/AbilityTask.h"
#include "ActionSystem/MHGZActionDirectionCorrectionTypes.h"
#include "WeaponRuntime/MHGZWeaponRuntimeTypes.h"
#include "AbilityTask_MHGZActionDirectionCorrection.generated.h"

class ACharacter;
class UMHGZWeaponRuntimeHostComponent;

/**
 * Applies one action-owned yaw correction over time. The target yaw is frozen
 * when the originating Montage Notify fires; the RuntimeHost lock prevents the
 * same Action's movement task from writing a competing rotation.
 */
UCLASS()
class MHGZ_API UAbilityTask_MHGZActionDirectionCorrection : public UAbilityTask
{
	GENERATED_BODY()

public:
	UAbilityTask_MHGZActionDirectionCorrection(const FObjectInitializer& ObjectInitializer);

	static UAbilityTask_MHGZActionDirectionCorrection* StartCorrection(
		UGameplayAbility* OwningAbility,
		const FWeaponActionToken& ActionToken,
		ACharacter* Character,
		float StartYaw,
		float DeltaYaw,
		float RequestedDuration,
		float MaxYawSpeedDegreesPerSecond,
		EActionDirectionCorrectionEasing Easing);

	bool DidAcquireRotationOverride() const { return bDidAcquireRotationOverride; }
	virtual void Activate() override;
	virtual void TickTask(float DeltaTime) override;
	virtual void OnDestroy(bool AbilityIsEnding) override;

private:
	float EvaluateEasing(float Alpha) const;
	float GetMaximumEasingSlope() const;
	bool HasValidOwner() const;

	FWeaponActionToken ActionToken;
	TWeakObjectPtr<ACharacter> Character;
	TWeakObjectPtr<UMHGZWeaponRuntimeHostComponent> RuntimeHost;
	float StartYaw = 0.0f;
	float DeltaYaw = 0.0f;
	float EffectiveDuration = 0.0f;
	float MaxYawSpeedDegreesPerSecond = 0.0f;
	float ElapsedTime = 0.0f;
	EActionDirectionCorrectionEasing Easing = EActionDirectionCorrectionEasing::Linear;
	bool bDidAcquireRotationOverride = false;
};
