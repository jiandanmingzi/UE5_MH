// Copyright MHGZ Project. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "ActionSystem/AnimNotify_CategorizedSound.h"
#include "ActionSystem/Tests/Shared/MHGZM3TestHarness.h"
#include "MHGZGroundDustTestReceiver.h"
#include "Components/BoxComponent.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZGroundDustEventTest,
	"MHGZ.Maintenance.Audio.GroundDust.EventIndependentOfAudio",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZGroundDustEventTest::RunTest(const FString&)
{
	FMHGZM3Harness H;
	if (!H.Setup(false, AMHGZM3TestCharacter::StaticClass())) { H.Teardown(); return false; }
	USkeletalMeshComponent* Mesh = H.Character->GetMesh();
	Mesh->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Characters/Demo/Meshes/Body/SKM_Demo_Body.SKM_Demo_Body")));
	Mesh->SetAnimInstanceClass(UAnimInstance::StaticClass());
	H.CombatConfig->AerialLandingMontage = LoadObject<UAnimMontage>(nullptr,
		TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_AerialLanding.AM_IG_AerialLanding"));
	auto* Event = NewObject<UAnimNotify_GroundDust>();
	TStrongObjectPtr<UMHGZGroundDustTestReceiver> Receiver(NewObject<UMHGZGroundDustTestReceiver>());
	H.Host->OnGroundDust.AddDynamic(Receiver.Get(), &UMHGZGroundDustTestReceiver::ReceiveDust);
	TestFalse(TEXT("null mesh is not an effect event"), Event->DispatchGroundDust(nullptr, 1));
	if (TestTrue(TEXT("exact system presentation starts"), H.Host->PlayAerialLandingVisual()))
	{
		const auto* Instance = Mesh->GetAnimInstance()->GetActiveInstanceForMontage(H.CombatConfig->AerialLandingMontage);
		if (TestNotNull(TEXT("real presentation instance"), Instance))
		{
			const int32 ID = Instance->GetInstanceID();
			TestFalse(TEXT("air does not invent ground dust"), Event->DispatchGroundDust(Mesh, ID));
			AActor* Floor = H.World->SpawnActor<AActor>();
			auto* Box = NewObject<UBoxComponent>(Floor);
			Floor->SetRootComponent(Box);
			Box->SetBoxExtent(FVector(1000, 1000, 5));
			Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Box->SetCollisionResponseToAllChannels(ECR_Block);
			Box->RegisterComponent();
			Box->SetWorldLocation(Mesh->GetSocketLocation(TEXT("Root")) - FVector(0, 0, 5));
			H.World->UpdateWorldComponents(true, false);
			TestNull(TEXT("test deliberately has no audio"), Event->Sound.Get());
			TestTrue(TEXT("effect event fires without audio"), Event->DispatchGroundDust(Mesh, ID));
			TestEqual(TEXT("exactly one event received"), Receiver->Count, 1);
			TestTrue(TEXT("real surface normal is provided"), Receiver->LastNormal.Equals(FVector::UpVector));
			TestFalse(TEXT("wrong instance cannot emit"), Event->DispatchGroundDust(Mesh, ID + 777));
			H.Host->StopAerialLandingVisual(0.f);
			TestFalse(TEXT("stopped montage cannot emit late dust"), Event->DispatchGroundDust(Mesh, ID));
			TestEqual(TEXT("rejected calls emit nothing"), Receiver->Count, 1);
		}
	}
	H.Host->OnGroundDust.RemoveAll(Receiver.Get());
	H.Teardown();
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZLandingEventTest,
	"MHGZ.Maintenance.Audio.LandingEvent.OptionalDustAndExactInstance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZLandingEventTest::RunTest(const FString&)
{
	FMHGZM3Harness H;
	if (!H.Setup(false, AMHGZM3TestCharacter::StaticClass())) { H.Teardown(); return false; }
	auto* Mesh = H.Character->GetMesh();
	Mesh->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Characters/Demo/Meshes/Body/SKM_Demo_Body.SKM_Demo_Body")));
	Mesh->SetAnimInstanceClass(UAnimInstance::StaticClass());
	H.CombatConfig->AerialLandingMontage = LoadObject<UAnimMontage>(nullptr,
		TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_AerialLanding.AM_IG_AerialLanding"));
	auto* Event = NewObject<UAnimNotify_LandingEvent>();
	TStrongObjectPtr<UMHGZGroundDustTestReceiver> Receiver(NewObject<UMHGZGroundDustTestReceiver>());
	H.Host->OnLanding.AddDynamic(Receiver.Get(), &UMHGZGroundDustTestReceiver::ReceiveLanding);
	H.Host->OnGroundDust.AddDynamic(Receiver.Get(), &UMHGZGroundDustTestReceiver::ReceiveDust);
	TestFalse(TEXT("legacy separate dust timing is the default"), Event->bTriggerGroundDust);
	TestNull(TEXT("landing can be a pure event without audio"), Event->Sound.Get());
	TestFalse(TEXT("null context is rejected"), Event->DispatchLanding(nullptr, 1));
	if (TestTrue(TEXT("real landing presentation starts"), H.Host->PlayAerialLandingVisual()))
	{
		const auto* Instance = Mesh->GetAnimInstance()->GetActiveInstanceForMontage(H.CombatConfig->AerialLandingMontage);
		if (TestNotNull(TEXT("exact presentation instance"), Instance))
		{
			const int32 ID = Instance->GetInstanceID();
			TestFalse(TEXT("no floor cannot invent a landing event"), Event->DispatchLanding(Mesh, ID));
			AActor* Floor = H.World->SpawnActor<AActor>();
			auto* Box = NewObject<UBoxComponent>(Floor);
			Floor->SetRootComponent(Box);
			Box->SetBoxExtent(FVector(1000, 1000, 5));
			Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Box->SetCollisionResponseToAllChannels(ECR_Block);
			Box->RegisterComponent();
			Box->SetWorldLocation(Mesh->GetSocketLocation(TEXT("Root")) - FVector(0, 0, 5));
			H.World->UpdateWorldComponents(true, false);
			TestTrue(TEXT("landing event works without audio"), Event->DispatchLanding(Mesh, ID));
			TestEqual(TEXT("one landing event"), Receiver->LandingCount, 1);
			TestEqual(TEXT("independent dust is not duplicated"), Receiver->Count, 0);
			Event->bTriggerGroundDust = true;
			TestTrue(TEXT("optional dust participates in the same event"), Event->DispatchLanding(Mesh, ID));
			TestEqual(TEXT("one optional dust request"), Receiver->Count, 1);
			TestTrue(TEXT("surface normal retained"), Receiver->LastNormal.Equals(FVector::UpVector));
			TestFalse(TEXT("wrong instance rejected"), Event->DispatchLanding(Mesh, ID + 777));
			H.Host->StopAerialLandingVisual(0.f);
			TestFalse(TEXT("late event after stop rejected"), Event->DispatchLanding(Mesh, ID));
			TestEqual(TEXT("rejections add no landing events"), Receiver->LandingCount, 2);
			TestEqual(TEXT("rejections add no dust events"), Receiver->Count, 1);
		}
	}
	H.Host->OnLanding.RemoveAll(Receiver.Get());
	H.Host->OnGroundDust.RemoveAll(Receiver.Get());
	H.Teardown();
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZGroundImpactEventTest,
	"MHGZ.Maintenance.Audio.GroundImpact.ExactPresentationWithoutDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZGroundImpactEventTest::RunTest(const FString&)
{
	FMHGZM3Harness H;
	if (!H.Setup(false, AMHGZM3TestCharacter::StaticClass())) { H.Teardown(); return false; }
	auto* Mesh = H.Character->GetMesh();
	Mesh->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Characters/Demo/Meshes/Body/SKM_Demo_Body.SKM_Demo_Body")));
	Mesh->SetAnimInstanceClass(UAnimInstance::StaticClass());
	H.CombatConfig->AerialLandingMontage = LoadObject<UAnimMontage>(nullptr,
		TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_AerialLanding.AM_IG_AerialLanding"));
	auto* Event = NewObject<UAnimNotify_GroundImpact>();
	TStrongObjectPtr<UMHGZGroundDustTestReceiver> Receiver(NewObject<UMHGZGroundDustTestReceiver>());
	H.Host->OnGroundImpact.AddDynamic(Receiver.Get(), &UMHGZGroundDustTestReceiver::ReceiveImpact);
	H.Host->OnLanding.AddDynamic(Receiver.Get(), &UMHGZGroundDustTestReceiver::ReceiveLanding);
	H.Host->OnGroundDust.AddDynamic(Receiver.Get(), &UMHGZGroundDustTestReceiver::ReceiveDust);
	TestFalse(TEXT("null context rejected"), Event->DispatchGroundImpact(nullptr, 1));
	if (TestTrue(TEXT("exact presentation starts"), H.Host->PlayAerialLandingVisual()))
	{
		const auto* Instance = Mesh->GetAnimInstance()->GetActiveInstanceForMontage(H.CombatConfig->AerialLandingMontage);
		if (TestNotNull(TEXT("real presentation instance"), Instance))
		{
			const int32 ID = Instance->GetInstanceID();
			TestFalse(TEXT("no floor cannot invent a strike"), Event->DispatchGroundImpact(Mesh, ID));
			AActor* Floor = H.World->SpawnActor<AActor>();
			auto* Box = NewObject<UBoxComponent>(Floor);
			Floor->SetRootComponent(Box);
			Box->SetBoxExtent(FVector(1000, 1000, 5));
			Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Box->SetCollisionResponseToAllChannels(ECR_Block);
			Box->RegisterComponent();
			Box->SetWorldLocation(Mesh->GetSocketLocation(TEXT("Root")) - FVector(0, 0, 5));
			H.World->UpdateWorldComponents(true, false);
			TestNull(TEXT("sound is deliberately empty"), Event->Sound.Get());
			TestTrue(TEXT("no damage or sound required"), Event->DispatchGroundImpact(Mesh, ID));
			TestEqual(TEXT("one independent impact"), Receiver->ImpactCount, 1);
			TestTrue(TEXT("real floor normal"), Receiver->LastNormal.Equals(FVector::UpVector));
			Event->Source = EMHGZAnimationSoundSource::WeaponSocket;
			TestFalse(TEXT("not an unchecked weapon emitter"), Event->DispatchGroundImpact(Mesh, ID));
			Event->Source = EMHGZAnimationSoundSource::GroundContact;
			TestFalse(TEXT("foreign instance rejected"), Event->DispatchGroundImpact(Mesh, ID + 777));
			H.Host->StopAerialLandingVisual(0.f);
			TestFalse(TEXT("stopped instance rejected"), Event->DispatchGroundImpact(Mesh, ID));
			TestEqual(TEXT("rejections add nothing"), Receiver->ImpactCount, 1);
			TestEqual(TEXT("not a hunter landing"), Receiver->LandingCount, 0);
			TestEqual(TEXT("does not duplicate independent dust"), Receiver->Count, 0);
		}
	}
	H.Host->OnGroundImpact.RemoveAll(Receiver.Get());
	H.Host->OnLanding.RemoveAll(Receiver.Get());
	H.Host->OnGroundDust.RemoveAll(Receiver.Get());
	H.Teardown();
	return true;
}
#endif
