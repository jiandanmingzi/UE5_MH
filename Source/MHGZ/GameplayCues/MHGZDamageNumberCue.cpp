// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZDamageNumberCue.h"

#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameplayEffectTypes.h"
#include "MHGZCharacter.h"
#include "MHGZPlayerController.h"
#include "UI/MHGZDamageNumberActor.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

namespace
{
// 保留原TActorIterator的OnlyActiveLevels筛选；只改变候选来源，不改变可见层级语义。
bool IsDamageNumberLevelActive(const AMHGZDamageNumberActor& Number, const UWorld& World)
{
	const ULevel* Level = Number.GetLevel();
	if (!Level || Level->GetWorld() != &World)
	{
		return false;
	}
	const bool bVisibleOrAssociating = (Level->bIsVisible && !Level->bIsBeingRemoved)
		|| Level->bIsAssociatingLevel || Level->bIsDisassociatingLevel;
	const FLevelCollection* Collection = Level->GetCachedLevelCollection();
	const FLevelCollection* ActiveCollection = World.GetActiveLevelCollection();
	return bVisibleOrAssociating && (!ActiveCollection || Collection == ActiveCollection
		|| !Collection || Collection->GetType() == ELevelCollectionType::StaticLevels);
}
}

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
	// Spawn/SetOwner/Destroy由引擎维护Children，不再为每次命中构建全世界数字列表。
	for (AActor* OwnedActor : MyTarget->Children)
	{
		const AMHGZDamageNumberActor* ExistingNumber = Cast<AMHGZDamageNumberActor>(OwnedActor);
		if (IsValid(ExistingNumber) && ExistingNumber->GetOwner() == MyTarget
			&& IsDamageNumberLevelActive(*ExistingNumber, *World)
			&& ExistingNumber->GetGameTimeSinceCreation() < 0.15f
			&& FVector::DistSquared(ExistingNumber->GetActorLocation(), SpawnLocation) < FMath::Square(100.0f))
		{
			++NearbyRecentNumbers;
			if (NearbyRecentNumbers == 4)
			{
				break;
			}
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
