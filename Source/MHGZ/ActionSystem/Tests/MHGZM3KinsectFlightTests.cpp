// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include <limits>

#include "GameFramework/ProjectileMovementComponent.h"
#include "InsectGlaive/Kinsect/KinsectCollisionComponent.h"
#include "MHGZM3TestHarness.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"

namespace
{
FKinsectFlightRequest MakeValidAlongDirectionRequest(const FMHGZM3Harness& H)
{
	FKinsectFlightRequest Request;
	Request.RuntimeToken = H.Host->GetCurrentToken();
	Request.TrajectoryMode = EKinsectTrajectoryMode::AlongDirection;
	Request.DirectionSnapshot = FVector(1.f, 0.f, 0.f);
	Request.MaxDistance = 1000.f;
	Request.FlightSpeed = 2000.f;
	Request.ArrivalRadius = 50.f;
	Request.DamageMode = EKinsectDamageMode::SingleHit;
	Request.ExtractMode = EKinsectExtractMode::FirstHitOnly;
	Request.PostFlightPolicy = EKinsectPostFlightPolicy::Hover;
	Request.MotionValue = 0.2f;
	Request.RehitInterval = 0.f;
	Request.FlightInstanceID = FGuid::NewGuid();
	return Request;
}

/** 在目标点放置一个带萃取色的 Hitzone（供代码 Capsule Sweep 命中）。 */
UMHGZMonsterHitzoneComponent* SpawnTestHitzone(
	UWorld* World, const FVector& Location, const FGameplayTag& ExtractColor)
{
	AActor* TargetActor = World->SpawnActor<AActor>();
	if (!TargetActor)
	{
		return nullptr;
	}
	UMHGZMonsterHitzoneComponent* Hitzone =
		NewObject<UMHGZMonsterHitzoneComponent>(TargetActor);
	Hitzone->Radius = 30.f;
	Hitzone->ExtractColorTag = ExtractColor;
	TargetActor->AddInstanceComponent(Hitzone);
	TargetActor->SetRootComponent(Hitzone);
	Hitzone->RegisterComponent();
	Hitzone->InitSphereRadius(30.f);
	Hitzone->SetWorldLocation(Location);
	return Hitzone;
}
}

// ── 5. FKinsectFlightRequest 非法请求拒绝且状态不变；合法请求可提交 ─────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM3KinsectFlightValidation,
	"MHGZ.M3.Kinsect.FlightRequestValidationIsAtomic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM3KinsectFlightValidation::RunTest(const FString& Parameters)
{
	FMHGZM3Harness H;
	if (!H.Setup())
	{
		return false;
	}

	AKinsect* Kinsect = H.Kinsect;
	UProjectileMovementComponent* Movement = Kinsect->Movement;
	TestEqual(TEXT("kinsect starts attached"), Kinsect->GetState(), EKinsectState::Attached);
	TestFalse(TEXT("movement inactive before flight"), Movement->IsActive());

	const FKinsectFlightRequest Base = MakeValidAlongDirectionRequest(H);
	const FVector StartLocation = Kinsect->GetActorLocation();

	auto TryInvalid = [&](const TCHAR* Label, FKinsectFlightRequest Request)
	{
		const EKinsectState StateBefore = Kinsect->GetState();
		const bool bAccepted = Kinsect->BeginFlight(Request);
		TestFalse(FString::Printf(TEXT("%s rejected"), Label), bAccepted);
		TestEqual(FString::Printf(TEXT("%s keeps state"), Label),
			Kinsect->GetState(), StateBefore);
		TestFalse(FString::Printf(TEXT("%s keeps movement off"), Label),
			Movement->IsActive());
	};

	FKinsectFlightRequest R = Base;
	R.RuntimeToken = FWeaponRuntimeToken();
	TryInvalid(TEXT("empty runtime token"), R);

	R = Base;
	R.RuntimeToken = H.Host->GetCurrentToken();
	R.RuntimeToken.Generation += 1;
	TryInvalid(TEXT("stale runtime token"), R);

	R = Base;
	R.FlightSpeed = 0.f;
	TryInvalid(TEXT("zero flight speed"), R);

	R = Base;
	R.FlightSpeed = -100.f;
	TryInvalid(TEXT("negative flight speed"), R);

	R = Base;
	R.FlightSpeed = std::numeric_limits<float>::quiet_NaN();
	TryInvalid(TEXT("NaN flight speed"), R);

	R = Base;
	R.MaxDistance = 0.f;
	TryInvalid(TEXT("zero max distance"), R);

	R = Base;
	R.MaxDistance = std::numeric_limits<float>::quiet_NaN();
	TryInvalid(TEXT("NaN max distance"), R);

	R = Base;
	R.ArrivalRadius = -1.f;
	TryInvalid(TEXT("negative arrival radius"), R);

	R = Base;
	R.ArrivalRadius = std::numeric_limits<float>::quiet_NaN();
	TryInvalid(TEXT("NaN arrival radius"), R);

	R = Base;
	R.RehitInterval = -1.f;
	TryInvalid(TEXT("negative rehit interval"), R);

	R = Base;
	R.RehitInterval = std::numeric_limits<float>::quiet_NaN();
	TryInvalid(TEXT("NaN rehit interval"), R);

	R = Base;
	R.MotionValue = -1.f;
	TryInvalid(TEXT("negative motion value"), R);

	R = Base;
	R.MotionValue = std::numeric_limits<float>::quiet_NaN();
	TryInvalid(TEXT("NaN motion value"), R);

	R = Base;
	R.FlightInstanceID = FGuid();
	TryInvalid(TEXT("invalid attack instance id"), R);

	R = Base;
	R.DirectionSnapshot = FVector(std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f);
	TryInvalid(TEXT("NaN along-direction snapshot"), R);

	R = Base;
	R.DirectionSnapshot = FVector::ZeroVector;
	TryInvalid(TEXT("zero along-direction snapshot"), R);

	R = Base;
	R.TrajectoryMode = EKinsectTrajectoryMode::ToPoint;
	R.TargetPointSnapshot = FVector(std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f);
	TryInvalid(TEXT("NaN to-point target"), R);

	R = Base;
	R.TrajectoryMode = static_cast<EKinsectTrajectoryMode>(99);
	TryInvalid(TEXT("invalid trajectory mode"), R);

	R = Base;
	R.TrajectoryMode = EKinsectTrajectoryMode::ToPoint;
	R.TargetPointSnapshot = StartLocation;
	TryInvalid(TEXT("coincident to-point target"), R);

	// 全部非法请求之后：仍 Attached、Movement 未激活、位置未移动。
	TestEqual(TEXT("all rejections leave state attached"),
		Kinsect->GetState(), EKinsectState::Attached);
	TestFalse(TEXT("all rejections leave movement off"), Movement->IsActive());
	TestTrue(TEXT("all rejections leave location unchanged"),
		Kinsect->GetActorLocation().Equals(StartLocation));

	// 合法 AlongDirection 请求：原子提交。
	const FKinsectFlightRequest Valid = Base;
	TestTrue(TEXT("valid along-direction flight starts"), Kinsect->BeginFlight(Valid));
	TestEqual(TEXT("state is flying"), Kinsect->GetState(), EKinsectState::Flying);
	TestTrue(TEXT("movement active after commit"), Movement->IsActive());
	TestTrue(TEXT("velocity matches direction snapshot"),
		Movement->Velocity.Equals(Valid.DirectionSnapshot * Valid.FlightSpeed));
	TestEqual(TEXT("active request stores trajectory mode"),
		Kinsect->ActiveRequest.TrajectoryMode, EKinsectTrajectoryMode::AlongDirection);
	TestTrue(TEXT("active request stores direction snapshot"),
		Kinsect->ActiveRequest.DirectionSnapshot.Equals(Valid.DirectionSnapshot));
	TestEqual(TEXT("active request stores max distance"),
		Kinsect->ActiveRequest.MaxDistance, Valid.MaxDistance);
	TestTrue(TEXT("collision query enabled during flight"),
		Kinsect->Collision->IsQueryCollisionEnabled());

	// 合法 ToPoint 请求：飞行中可重新提交。
	FKinsectFlightRequest ToPoint = Base;
	ToPoint.TrajectoryMode = EKinsectTrajectoryMode::ToPoint;
	ToPoint.TargetPointSnapshot = StartLocation + FVector(1000.f, 0.f, 0.f);
	TestTrue(TEXT("valid to-point flight commits"), Kinsect->BeginFlight(ToPoint));
	TestEqual(TEXT("to-point stores trajectory mode"),
		Kinsect->ActiveRequest.TrajectoryMode, EKinsectTrajectoryMode::ToPoint);
	TestTrue(TEXT("to-point stores target snapshot"),
		Kinsect->ActiveRequest.TargetPointSnapshot.Equals(ToPoint.TargetPointSnapshot));
	TestEqual(TEXT("to-point still flying"), Kinsect->GetState(), EKinsectState::Flying);

	H.Teardown();
	return true;
}

// ── 6. Pending 萃取原子清空与返回交付契约 ──────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM3KinsectPendingExtractAndReturn,
	"MHGZ.M3.Kinsect.PendingExtractAtomicTakeAndReturnDelivery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM3KinsectPendingExtractAndReturn::RunTest(const FString& Parameters)
{
	FMHGZM3Harness H;
	if (!H.Setup())
	{
		return false;
	}

	AKinsect* Kinsect = H.Kinsect;
	const FGameplayTag White = M3::Tag(TEXT("WeaponResource.IG.Extract.White"));
	const FGameplayTag KinsectActive = M3::Tag(TEXT("WeaponResource.IG.Kinsect.Active"));
	TestNotNull(TEXT("hitzone spawned on flight path"),
		SpawnTestHitzone(H.World, FVector(400.f, 0.f, 0.f), White));

	FKinsectFlightRequest Request;
	Request.RuntimeToken = H.Host->GetCurrentToken();
	Request.TrajectoryMode = EKinsectTrajectoryMode::ToPoint;
	Request.TargetPointSnapshot = FVector(460.f, 0.f, 0.f);
	Request.MaxDistance = 1000.f;
	Request.FlightSpeed = 2000.f;
	Request.ArrivalRadius = 50.f;
	Request.DamageMode = EKinsectDamageMode::SingleHit;
	Request.ExtractMode = EKinsectExtractMode::FirstHitOnly;
	Request.PostFlightPolicy = EKinsectPostFlightPolicy::Hover;
	Request.MotionValue = 0.2f;
	Request.RehitInterval = 0.f;
	Request.FlightInstanceID = FGuid::NewGuid();

	TestTrue(TEXT("flight toward hitzone starts through resource"),
		H.Resource->DeployKinsect(Request));
	Kinsect->SetActorLocation(FVector(460.f, 0.f, 0.f));
	Kinsect->Tick(0.016f);

	const bool bPendingCaptured = Kinsect->GetState() == EKinsectState::Hovering
		&& Kinsect->HasPendingExtract();
	if (bPendingCaptured)
	{
		TestTrue(TEXT("pending extract color is White"),
			Kinsect->GetPendingExtractColor() == White);
		TestEqual(TEXT("single-hit flight hovers"), Kinsect->GetState(),
			EKinsectState::Hovering);

		// Interrupt 不清除 Pending。
		Kinsect->Interrupt();
		TestTrue(TEXT("interrupt keeps pending extract"), Kinsect->HasPendingExtract());
		TestEqual(TEXT("interrupt keeps hovering"), Kinsect->GetState(),
			EKinsectState::Hovering);

		// TakePendingExtractColor 原子取出并清空。
		const FGameplayTag Taken = Kinsect->TakePendingExtractColor();
		TestTrue(TEXT("TakePendingExtractColor returns White"), Taken == White);
		TestFalse(TEXT("pending cleared after take"), Kinsect->HasPendingExtract());
		const FGameplayTag TakenAgain = Kinsect->TakePendingExtractColor();
		TestFalse(TEXT("second take is empty and atomic"), TakenAgain.IsValid());
	}
	else
	{
		TestTrue(TEXT("high-speed Previous-to-Current capsule sweep must capture pending"), false);
	}

	// 返回契约：Returning → 到达 → Attach；空萃取不产生灯。
	Kinsect->StartReturn();
	TestEqual(TEXT("returning state"), Kinsect->GetState(), EKinsectState::Returning);
	Kinsect->SetActorLocation(FVector(10.f, 0.f, 0.f));
	Kinsect->Tick(0.016f);
	TestEqual(TEXT("kinsect reattached after return"),
		Kinsect->GetState(), EKinsectState::Attached);
	TestFalse(TEXT("no extract delivered for empty pending"),
		H.Resource->HasExtract(White));
	TestEqual(TEXT("kinsect active tag released after return"),
		H.ASC->GetTagCount(KinsectActive), 0);

	// 第二轮：Pending 完整走返回交付链 → 资源建立白灯。
	TestTrue(TEXT("second flight starts through resource"),
		H.Resource->DeployKinsect(Request));
	Kinsect->SetActorLocation(FVector(460.f, 0.f, 0.f));
	Kinsect->Tick(0.016f);
	if (Kinsect->HasPendingExtract())
	{
		Kinsect->StartReturn();
		TestEqual(TEXT("return with pending starts"),
			Kinsect->GetState(), EKinsectState::Returning);
		Kinsect->SetActorLocation(FVector(10.f, 0.f, 0.f));
		Kinsect->Tick(0.016f);
		TestEqual(TEXT("delivery reattaches kinsect"),
			Kinsect->GetState(), EKinsectState::Attached);
		TestTrue(TEXT("pending extract delivered as White light"),
			H.Resource->HasExtract(White));
		TestEqual(TEXT("delivery releases kinsect active tag"),
			H.ASC->GetTagCount(KinsectActive), 0);
	}
	else
	{
		TestTrue(TEXT("second high-speed sweep must capture pending for delivery"), false);
	}

	H.Teardown();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
