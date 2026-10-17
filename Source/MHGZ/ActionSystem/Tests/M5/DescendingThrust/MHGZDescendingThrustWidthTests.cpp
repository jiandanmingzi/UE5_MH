// Copyright MHGZ Project. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "ActionSystem/MHGZAttackAbility.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZDescendingThrustWidthTest,
	"MHGZ.Maintenance.DescendingThrust.HorizontalWidthOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZDescendingThrustWidthTest::RunTest(const FString&)
{
	FWeaponTraceRegion Region;
	Region.Radius = 14.f;
	TestTrue(TEXT("existing unconfigured regions remain spheres"), Region.MakeQueryShape().IsSphere());
	Region.HorizontalHalfWidth = 45.f;
	const FCollisionShape Shape = Region.MakeQueryShape();
	TestTrue(TEXT("optional width uses a lateral capsule"), Shape.IsCapsule());
	TestEqual(TEXT("original radial thickness stays 14 cm"), Shape.GetCapsuleRadius(), 14.f);
	TestEqual(TEXT("lateral half width is 45 cm"), Shape.GetCapsuleHalfHeight(), 45.f);

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("query world"), World)) { return false; }
	AActor* Target = World->SpawnActor<AActor>();
	UBoxComponent* Box = Target ? NewObject<UBoxComponent>(Target) : nullptr;
	if (!Box)
	{
		World->DestroyWorld(false);
		AddError(TEXT("query target setup failed"));
		return false;
	}
	Target->SetRootComponent(Box);
	Box->SetBoxExtent(FVector(1.f));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetCollisionResponseToAllChannels(ECR_Block);
	Box->RegisterComponent();
	World->UpdateWorldComponents(true, false);
	for (const float Yaw : { 0.f, 90.f })
	{
		const FRotator Rotation(0.f, Yaw, 0.f);
		const FVector Right = Rotation.RotateVector(FVector::RightVector);
		const FVector Forward = Rotation.Vector();
		const FQuat ShapeRotation = FQuat::FindBetweenNormals(FVector::UpVector, Right);
		auto Overlaps = [&](const FVector& Position, const FCollisionShape& Query, const FQuat& QueryRotation)
		{
			Box->SetWorldLocation(Position);
			return World->OverlapBlockingTestByChannel(FVector::ZeroVector, QueryRotation, ECC_Visibility, Query);
		};
		TestTrue(TEXT("wider query catches the lateral upper-body edge"), Overlaps(Right * 40.f, Shape, ShapeRotation));
		TestFalse(TEXT("old sphere did not cover that lateral edge"), Overlaps(Right * 40.f, FCollisionShape::MakeSphere(14.f), FQuat::Identity));
		TestFalse(TEXT("vertical thickness is not expanded"), Overlaps(FVector::UpVector * 20.f, Shape, ShapeRotation));
		TestFalse(TEXT("fore-aft thickness is not expanded"), Overlaps(Forward * 20.f, Shape, ShapeRotation));
		TestFalse(TEXT("horizontal extent stays bounded"), Overlaps(Right * 50.f, Shape, ShapeRotation));
	}
	World->DestroyWorld(false);
	return true;
}
#endif
