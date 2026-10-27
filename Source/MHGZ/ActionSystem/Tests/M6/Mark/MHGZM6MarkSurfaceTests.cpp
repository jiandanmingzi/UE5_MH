// Copyright MHGZ Project. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "AttributeSystem/Res_InsectGlaive.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Monster/MHGZDummyConfig.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"
#include "Monster/MHGZTrainingDummy.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "StaticMeshCompiler.h"
#include "../../Shared/MHGZM3TestHarness.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZMarkHemisphereSurfaceTest,
	"MHGZ.Maintenance.MarkSurface.HemisphereRejectsThroughBodyProjection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZMarkHemisphereSurfaceTest::RunTest(const FString&)
{
	FMHGZM3Harness H;
	if (!TestTrue(TEXT("runtime fixture"), H.Setup())) return false;
	AMHGZTrainingDummy* Dummy = H.World->SpawnActor<AMHGZTrainingDummy>(
		FVector(450, 1400, 200), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("target"), Dummy)) { H.Teardown(); return false; }
	Dummy->DummyConfig = LoadObject<UMHGZDummyConfig>(nullptr,
		TEXT("/Game/Monster/TrainingDummy/Data/DA_TrainingDummy_Hemisphere.DA_TrainingDummy_Hemisphere"));
	if (!TestNotNull(TEXT("actual hemisphere config"), Dummy->DummyConfig.Get())) { H.Teardown(); return false; }
	TArray<UStaticMesh*> Meshes;
	for (const TCHAR* Color : {TEXT("White"), TEXT("Orange"), TEXT("Red")})
	{
		for (const TCHAR* Prefix : {TEXT("SM_TD_Hemisphere_"), TEXT("SM_TD_Query_Hemisphere_")})
		{
			const FString Name = FString(Prefix) + Color;
			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr,
				*FString::Printf(TEXT("/Game/Monster/TrainingDummy/Meshes/%s.%s"), *Name, *Name));
			if (!TestNotNull(TEXT("real hemisphere mesh"), Mesh)) { H.Teardown(); return false; }
			Meshes.Add(Mesh);
		}
	}
	FStaticMeshCompilingManager::Get().FinishCompilation(Meshes);
	UStaticMeshComponent* Visuals[] = {Dummy->WhiteSegmentVisual, Dummy->OrangeSegmentVisual, Dummy->RedSegmentVisual};
	for (int32 I = 0; I < 3; ++I)
	{
		Visuals[I]->SetStaticMesh(Meshes[I * 2]);
		Visuals[I]->SetRelativeScale3D(FVector::OneVector);
		Visuals[I]->SetRelativeLocation(FVector(0, 0, -200 + I * 400.0 / 3));
	}
	Dummy->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (!Dummy->HasActorBegunPlay()) Dummy->DispatchBeginPlay();
	Dummy->ApplyConfig(Dummy->DummyConfig);
	TArray<UMHGZMonsterHitzoneComponent*> Zones;
	Dummy->GetComponents(Zones);
	Zones.Sort([](const auto& A, const auto& B) { return A.GetComponentLocation().Z < B.GetComponentLocation().Z; });
	if (!TestEqual(TEXT("three real parts"), Zones.Num(), 3)) { H.Teardown(); return false; }
	H.CombatConfig->KinsectMarkEstablishedEffect = LoadObject<UParticleSystem>(nullptr,
		TEXT("/Game/Weapons/InsectGlaive/VFX/PS_IG_KinsectMarkCloud.PS_IG_KinsectMarkCloud"));
	if (!TestNotNull(TEXT("actual mark effect"), H.CombatConfig->KinsectMarkEstablishedEffect.Get()))
	{ H.Teardown(); return false; }

	// 第一例取最新录制的命中点与错误投影点；两点之差重建错误投影射线，
	// 不声称录制保存了原始扫掠起点或法线。后续覆盖正常方向、分色接缝和顶部。
	const FVector Center(450, 1400, 0);
	const FVector Points[] = {FVector(405.1227, 1774.8102, 138.5400),
		FVector(845.6115, 1377.7952, 69.8888),
		Center + FVector(FMath::Sqrt(160000.0 - FMath::Square(400.0 / 3)), 0, 400.0 / 3),
		Center + FVector(0, 0, 401)};
	for (int32 I = 0; I < UE_ARRAY_COUNT(Points); ++I)
	{
		const FVector Point = Points[I];
		const FVector Outward = (Point - Center).GetSafeNormal();
		const FVector Preferred = I == 0 ? FVector(443.5458, 1384.8557, 0) - Point : Outward;
		UMHGZMonsterHitzoneComponent* Zone = Zones[I == 0 || I == 2 ? 1 : I == 3 ? 2 : 0];
		FHitResult Expected;
		const FCollisionQueryParams Params(SCENE_QUERY_STAT(MarkSurfaceControl), true);
		bool bExpected = false;
		for (UStaticMeshComponent* Visual : Visuals)
		{
			FHitResult Candidate;
			if (Visual->LineTraceComponent(Candidate, Point + Outward * 1000, Point - Outward * 1000, Params)
				&& FVector::DotProduct(Candidate.ImpactNormal, Outward) > 0
				&& (!bExpected || FVector::DistSquared(Candidate.ImpactPoint, Point) < FVector::DistSquared(Expected.ImpactPoint, Point)))
			{ Expected = Candidate; bExpected = true; }
		}
		if (!TestTrue(TEXT("independent actual outer mesh contact"), bExpected)) continue;
		FVector Resolved, Normal;
		TestTrue(TEXT("generic surface query succeeds"), Dummy->ResolveHitEffectVisualSurface(
			Zone, Point, Outward, Preferred, Resolved, Normal));
		TestTrue(FString::Printf(TEXT("sample %d stays near the hit, not across the body or below the floor (z=%.3f)"), I, Resolved.Z),
			FVector::Distance(Resolved, Expected.ImpactPoint) < 5);
		TestTrue(TEXT("surface normal faces outward"), FVector::DotProduct(Normal, Outward) > .9);
		TestTrue(TEXT("real mark path accepts the hit"), H.Resource->SetKinsectMark(Zone, Point, nullptr, Preferred));
		FVector Anchor;
		TestTrue(TEXT("mark anchor exists"), H.Resource->GetKinsectMarkWorldLocation(Anchor));
		TestTrue(TEXT("stored mark anchor remains at the local outer contact"), FVector::Distance(Anchor, Expected.ImpactPoint) < 5);
		TArray<UParticleSystemComponent*> Effects;
		Dummy->GetComponents(Effects);
		UParticleSystemComponent* Effect = nullptr;
		for (UParticleSystemComponent* P : Effects)
			if (P->Template == H.CombatConfig->KinsectMarkEstablishedEffect) Effect = P;
		if (TestNotNull(TEXT("real mark particle component exists"), Effect))
		{
			TestTrue(TEXT("particle component is on the outer side of its anchor"),
				FVector::Distance(Effect->GetComponentLocation(), Expected.ImpactPoint) < 5);
			// NullRHI不运行实际粒子渲染，检查组件注册/可见而不冒称目视验收。
			TestTrue(TEXT("mark particle is registered and visible"), Effect->IsRegistered() && Effect->IsVisible());
		}
		AddInfo(FString::Printf(TEXT("Sample %d: hit=%s resolved=%s expected=%s anchor=%s"),
			I, *Point.ToString(), *Resolved.ToString(), *Expected.ImpactPoint.ToString(), *Anchor.ToString()));
		H.Resource->ClearKinsectMark(EIGMarkClearReason::Replaced);
	}
	H.Teardown();
	return true;
}
#endif
