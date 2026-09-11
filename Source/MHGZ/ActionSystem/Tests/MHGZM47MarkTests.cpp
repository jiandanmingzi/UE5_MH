// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "AttributeSystem/Res_InsectGlaive.h"
#include "MHGZM3TestHarness.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"

namespace
{
const FGameplayTag& MarkActiveTag()
{
	static const FGameplayTag Tag = M3::Tag(TEXT("WeaponResource.IG.Mark.Active"));
	return Tag;
}

UMHGZMonsterHitzoneComponent* SpawnTestHitzone(UWorld& World,
	const FVector& Location)
{
	AActor* Target = World.SpawnActor<AActor>(AActor::StaticClass(), Location,
		FRotator::ZeroRotator);
	if (!Target)
	{
		return nullptr;
	}

	UMHGZMonsterHitzoneComponent* Hitzone =
		NewObject<UMHGZMonsterHitzoneComponent>(Target, NAME_None,
		RF_Transient);
	if (!Hitzone)
	{
		return nullptr;
	}
	Target->SetRootComponent(Hitzone);
	Hitzone->RegisterComponent();
	Hitzone->SetWorldLocation(Location);
	return Hitzone;
}

FHitResult MakeMeleeHit(AActor& Target, UMHGZMonsterHitzoneComponent& Hitzone,
	const FVector& ImpactPoint)
{
	FHitResult Hit(&Target, &Hitzone, ImpactPoint, FVector::UpVector);
	Hit.bBlockingHit = true;
	Hit.Location = ImpactPoint;
	Hit.ImpactPoint = ImpactPoint;
	return Hit;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM47MeleeMarkLifecycle,
	"MHGZ.M4.7.Mark.MeleeLifecycleKeepsUniqueResourceOwnedMark",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM47MeleeMarkLifecycle::RunTest(const FString& Parameters)
{
	FMHGZM3Harness H;
	if (!H.Setup())
	{
		AddError(TEXT("M3 runtime harness setup failed"));
		return false;
	}

	UMHGZMonsterHitzoneComponent* FirstHitzone = SpawnTestHitzone(
		*H.World, FVector(240.f, 30.f, 80.f));
	UMHGZMonsterHitzoneComponent* SecondHitzone = SpawnTestHitzone(
		*H.World, FVector(520.f, -40.f, 120.f));
	TestNotNull(TEXT("first test Hitzone exists"), FirstHitzone);
	TestNotNull(TEXT("second test Hitzone exists"), SecondHitzone);
	if (!FirstHitzone || !SecondHitzone)
	{
		H.Teardown();
		return false;
	}

	const FVector FirstImpact = FirstHitzone->GetComponentLocation()
		+ FVector(4.f, 8.f, -3.f);
	TestTrue(TEXT("first valid melee Hitzone creates mark without projectile"),
		H.Resource->SetKinsectMarkFromMeleeHit(
			MakeMeleeHit(*FirstHitzone->GetOwner(), *FirstHitzone, FirstImpact)));
	TestTrue(TEXT("melee mark is valid"), H.Resource->HasValidKinsectMark());
	TestEqual(TEXT("active tag acquired once"), H.ASC->GetTagCount(MarkActiveTag()), 1);

	FVector MarkLocation = FVector::ZeroVector;
	TestTrue(TEXT("melee mark has a world attachment location"),
		H.Resource->GetKinsectMarkWorldLocation(MarkLocation));
	TestTrue(TEXT("first local attachment point is preserved"),
		MarkLocation.Equals(FirstImpact, KINDA_SMALL_NUMBER));

	FHitResult InvalidHit;
	InvalidHit.ImpactPoint = FVector(999.f, 0.f, 0.f);
	TestFalse(TEXT("missing Hitzone cannot erase prior mark"),
		H.Resource->SetKinsectMarkFromMeleeHit(InvalidHit));
	TestTrue(TEXT("old mark survives invalid melee input"),
		H.Resource->GetKinsectMarkWorldLocation(MarkLocation));
	TestTrue(TEXT("old location survives invalid melee input"),
		MarkLocation.Equals(FirstImpact, KINDA_SMALL_NUMBER));

	const FVector SecondImpact = SecondHitzone->GetComponentLocation()
		+ FVector(-6.f, 2.f, 5.f);
	TestTrue(TEXT("later valid melee Hitzone replaces unique mark"),
		H.Resource->SetKinsectMarkFromMeleeHit(
			MakeMeleeHit(*SecondHitzone->GetOwner(), *SecondHitzone, SecondImpact)));
	TestTrue(TEXT("replacement keeps one active mark tag"),
		H.ASC->GetTagCount(MarkActiveTag()) == 1);
	TestTrue(TEXT("replacement location is the second local attachment point"),
		H.Resource->GetKinsectMarkWorldLocation(MarkLocation)
		&& MarkLocation.Equals(SecondImpact, KINDA_SMALL_NUMBER));

	H.Resource->ClearKinsectMark(EIGMarkClearReason::RuntimeShutdown);
	TestFalse(TEXT("explicit clear removes melee mark"), H.Resource->HasValidKinsectMark());
	TestEqual(TEXT("explicit clear releases active tag"), H.ASC->GetTagCount(MarkActiveTag()), 0);

	H.Teardown();
	return true;
}

#endif
