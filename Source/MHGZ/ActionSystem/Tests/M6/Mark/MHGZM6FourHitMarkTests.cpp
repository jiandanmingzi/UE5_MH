// Copyright MHGZ Project. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "ActionSystem/MHGZDamageGameplayEffect.h"
#include "ActionSystem/Tests/M6/MHGZM6AwakenedKinsectTestTypes.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "Monster/MHGZTrainingDummy.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"
#include "../../Shared/MHGZM3TestHarness.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZM6FourHitSecondDamageMarks,
	"MHGZ.M6.Mark.FourHitOnlySecondSuccessfulDamageEstablishesMark",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM6FourHitSecondDamageMarks::RunTest(const FString& Parameters)
{
	FMHGZM3Harness H;
	if (!TestTrue(TEXT("mark runtime harness builds"), H.Setup())) { H.Teardown(); return false; }
	UClass* TargetClass = LoadClass<AMHGZTrainingDummy>(nullptr,
		TEXT("/Game/Blueprints/Monster/TrainingDummy/BP_TrainingDummy.BP_TrainingDummy_C"));
	AMHGZTrainingDummy* Target = TargetClass
		? H.World->SpawnActor<AMHGZTrainingDummy>(TargetClass, FVector(300, 0, 0), FRotator::ZeroRotator) : nullptr;
	if (!TestNotNull(TEXT("real mark target"), Target)) { H.Teardown(); return false; }
	if (!Target->HasActorBegunPlay()) { Target->DispatchBeginPlay(); }
	Target->GetAbilitySystemComponent()->InitAbilityActorInfo(Target, Target);
	TArray<UMHGZMonsterHitzoneComponent*> Zones;
	Target->GetComponents(Zones);
	if (!TestTrue(TEXT("target has hitzones"), !Zones.IsEmpty())) { H.Teardown(); return false; }
	UMHGZMonsterHitzoneComponent* Zone = Zones[0];
	const FVector Point = Zone->GetComponentLocation() - FVector(75, 0, 0);
	FHitResult Hit(Target, Zone->GetQueryCollisionComponent(), Point, -FVector::ForwardVector);
	Hit.bBlockingHit = true;
	Hit.Location = Hit.ImpactPoint = Point;
	Hit.TraceStart = Point - FVector(200, 0, 0);
	Hit.TraceEnd = Point + FVector(200, 0, 0);
	Hit.Normal = Hit.ImpactNormal = -FVector::ForwardVector;
	const FGameplayAbilitySpecHandle Handle = H.GiveAbility(UMHGZM6FourHitMarkTestAbility::StaticClass());
	FWeaponInputSnapshot Input = M3::MakePosedInput(true, false);
	Input.ResolvedInputTag = M3::Tag(TEXT("Input.Weapon.YB"));
	for (int32 Activation = 0; Activation < 2; ++Activation)
	{
		if (!TestTrue(TEXT("per-execution source activates"), H.TryActivateWithInput(Handle, Input))) { H.Teardown(); return false; }
		UMHGZM6FourHitMarkTestAbility* Source = nullptr;
		for (UGameplayAbility* Instance : H.ASC->FindAbilitySpecFromHandle(Handle)->GetAbilityInstances())
		{
			if (Instance->IsActive()) { Source = Cast<UMHGZM6FourHitMarkTestAbility>(Instance); }
		}
		if (!TestNotNull(TEXT("active source"), Source)) { H.Teardown(); return false; }
		const uint64 Before = H.Resource->GetKinsectMarkSerial();
		TestTrue(TEXT("first damage succeeds"), Source->ApplyDamageForTest(Hit, 0));
		TestEqual(TEXT("first hit does not mark"), H.Resource->GetKinsectMarkSerial(), Before);
		Source->AttackSegments[1].Damage.DamageEffectClass = nullptr;
		TestFalse(TEXT("invalid second damage rejected"), Source->ApplyDamageForTest(Hit, 1));
		TestEqual(TEXT("failed damage does not mark"), H.Resource->GetKinsectMarkSerial(), Before);
		Source->AttackSegments[1].Damage.DamageEffectClass = UMHGZDamageGameplayEffect::StaticClass();
		TestTrue(TEXT("second damage succeeds"), Source->ApplyDamageForTest(Hit, 1));
		TestTrue(TEXT("second successful damage establishes/replaces mark"),
			H.Resource->HasValidKinsectMark() && H.Resource->GetKinsectMarkSerial() > Before);
		TestEqual(TEXT("mark uses actual struck hitzone"), H.Resource->GetKinsectMarkHitzone(), Zone);
		const uint64 Established = H.Resource->GetKinsectMarkSerial();
		for (const int32 Segment : { 1, 2, 3 })
		{
			TestTrue(TEXT("later/repeated damage succeeds"), Source->ApplyDamageForTest(Hit, Segment));
			TestEqual(TEXT("no repeated or third/fourth-hit mark"), H.Resource->GetKinsectMarkSerial(), Established);
		}
		H.ASC->CancelAbilityHandle(Handle);
		TestTrue(TEXT("action end preserves mark"), H.Resource->HasValidKinsectMark());
	}
	H.Teardown();
	return true;
}
#endif
