// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActionSystem/MHGZGameplayAbility.h"
#include "GameFramework/PlayerController.h"
#include "MHGZCharacter.h"
#include "MHGZM5KinsectSlashTestTypes.generated.h"

class UAbilityTask_MHGZWeaponMovement;
class UCurveFloat;

/** Spawnable test pawn with the public, instrumented CMC movement-step seam. */
UCLASS()
class AMHGZM5KinsectSlashMovementTestCharacter : public AMHGZCharacter
{
	GENERATED_BODY()

public:
	AMHGZM5KinsectSlashMovementTestCharacter(const FObjectInitializer& ObjectInitializer);
};

/** Deterministic test controller whose viewpoint can change between AimLock samples. */
UCLASS()
class AMHGZM5KinsectSlashAimTestController : public APlayerController
{
	GENERATED_BODY()

public:
	void SetViewpointForTest(const FVector& Location, const FRotator& Rotation)
	{
		TestViewLocation = Location;
		TestViewRotation = Rotation;
	}

	virtual void GetPlayerViewPoint(FVector& OutLocation, FRotator& OutRotation) const override
	{
		OutLocation = TestViewLocation;
		OutRotation = TestViewRotation;
	}

private:
	FVector TestViewLocation = FVector::ZeroVector;
	FRotator TestViewRotation = FRotator::ZeroRotator;
};

/** Test-only ability that starts the production weapon-movement task. */
UCLASS()
class UMHGZM5KinsectSlashMovementTestAbility : public UMHGZGameplayAbility
{
	GENERATED_BODY()

public:
	UMHGZM5KinsectSlashMovementTestAbility();

	void ConfigureForTest(const FVector& Direction, float Distance, float Duration,
		UCurveFloat* DistanceCurve,
		EWeaponMovementMode Mode = EWeaponMovementMode::CurvedDirectional);
	void ResetTestConfiguration();
	void ConfigureRequestForTest(const FWeaponMovementRequest& Request);

	UAbilityTask_MHGZWeaponMovement* GetMovementTaskForTest() const
	{
		return MovementTask;
	}
	bool HasMovementResultForTest() const { return bHasMovementResult; }
	const FWeaponMovementResult& GetMovementResultForTest() const { return MovementResult; }

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

private:
	UFUNCTION()
	void HandleMovementFinished(const FWeaponMovementResult& Result);

	/** 当前动作拥有的位移任务；任务结束或取消时释放。 */
	UPROPERTY(Transient)
	TObjectPtr<UAbilityTask_MHGZWeaponMovement> MovementTask;

	FWeaponMovementResult MovementResult;
	bool bHasMovementResult = false;
};
