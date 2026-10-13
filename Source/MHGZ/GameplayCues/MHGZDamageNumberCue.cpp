// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZDamageNumberCue.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameplayEffectTypes.h"
#include "MHGZCharacter.h"
#include "MHGZPlayerController.h"
#include "UI/MHGZDamageNumberActor.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

bool UMHGZDamageNumberCue::OnExecute_Implementation(AActor* MyTarget,
	const FGameplayCueParameters& Parameters) const
{
	const float ActualDamage = Parameters.RawMagnitude;
	UWorld* World = MyTarget ? MyTarget->GetWorld() : nullptr;
	if (!World || World->GetNetMode() == NM_DedicatedServer
		|| !DamageNumberActorClass || !FMath::IsFinite(ActualDamage)
		|| ActualDamage <= 0.0f)
	{
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = MyTarget;
	SpawnParams.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	FVector SpawnLocation = Parameters.Location.IsNearlyZero()
		? MyTarget->GetActorLocation() : FVector(Parameters.Location);
	// Simultaneous powder stages remain separate readable numbers, not overlaid text.
	int32 NearbyRecentNumbers = 0;
	for (TActorIterator<AMHGZDamageNumberActor> It(World); It; ++It)
	{
		if (It->GetOwner() == MyTarget && It->GetGameTimeSinceCreation() < 0.15f
			&& FVector::DistSquared(It->GetActorLocation(), SpawnLocation) < FMath::Square(100.0f))
		{
			++NearbyRecentNumbers;
		}
	}
	SpawnLocation.Z += FMath::Min(NearbyRecentNumbers, 4) * 24.0f;
	AMHGZDamageNumberActor* Number = World->SpawnActor<AMHGZDamageNumberActor>(
		DamageNumberActorClass, SpawnLocation, FRotator::ZeroRotator, SpawnParams);
	if (!Number)
	{
		return false;
	}

	AMHGZPlayerController* TelemetryController = nullptr;
	uint64 RuntimeGeneration = 0;
	if (const AMHGZCharacter* SourceCharacter =
		Cast<AMHGZCharacter>(Parameters.Instigator.Get()))
	{
		TelemetryController = Cast<AMHGZPlayerController>(SourceCharacter->GetController());
		if (const UMHGZWeaponRuntimeHostComponent* RuntimeHost =
			SourceCharacter->GetWeaponRuntimeHost())
		{
			RuntimeGeneration = RuntimeHost->GetCurrentToken().Generation;
		}
	}
	if (!Number->InitializeDamageNumber(ActualDamage,
		Parameters.NormalizedMagnitude >= 0.5f, TelemetryController, RuntimeGeneration))
	{
		Number->Destroy();
		return false;
	}
	if (TelemetryController)
	{
		TelemetryController->RecordUITelemetryEvent(TEXT("DamageNumberSpawn"), Number,
			TEXT("PositiveFinalDamage"), RuntimeGeneration);
	}
	return true;
}
