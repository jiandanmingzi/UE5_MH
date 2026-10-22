// Copyright MHGZ Project. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "ActionSystem/AnimNotify_ActionSound.h"
#include "ActionSystem/Tests/Shared/MHGZM3TestHarness.h"
#include "Components/BoxComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Sound/SoundBase.h"
#include "AudioDeviceManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZAnimationSoundEmissionTests,
	"MHGZ.Maintenance.Audio.Animation.StrictSocketsAndGroundContact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZAnimationSoundEmissionTests::RunTest(const FString&)
{
	FMHGZM3Harness H;
	if (!H.Setup(false, AMHGZM3TestCharacter::StaticClass())) { H.Teardown(); return false; }
	USkeletalMeshComponent* Mesh = H.Character->GetMesh();
	Mesh->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Characters/Demo/Meshes/Body/SKM_Demo_Body.SKM_Demo_Body")));
	Mesh->SetRelativeLocation(FVector::ZeroVector);
	USkeletalMeshComponent* Weapon = NewObject<USkeletalMeshComponent>(H.Character);
	Weapon->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Weapons/InsectGlaive/Meshes/Glaive/SKM_IG_Glaive.SKM_IG_Glaive")));
	UAnimNotify_SwingSound* Recipe = NewObject<UAnimNotify_SwingSound>();
	USceneComponent* Source;
	FVector Location;
	FName Reason;
	TestFalse(TEXT("None cannot silently become component origin"), Recipe->ResolveEmission(Mesh, Weapon, Source, Location, Reason));
	for (const FName Socket : {FName(TEXT("IG_FrontTip")), FName(TEXT("IG_RearTip"))})
	{
		Recipe->AttachSocket = Socket;
		TestTrue(TEXT("real weapon endpoint resolves"), Recipe->ResolveEmission(Mesh, Weapon, Source, Location, Reason));
		TestEqual(TEXT("weapon source is not character mesh"), Source, static_cast<USceneComponent*>(Weapon));
		TestTrue(TEXT("correct endpoint position"), Location.Equals(Weapon->GetSocketLocation(Socket), .01));
	}
	Recipe->AttachSocket = TEXT("MissingSocket");
	TestFalse(TEXT("invalid named socket cannot fall back"), Recipe->ResolveEmission(Mesh, Weapon, Source, Location, Reason));
	Recipe->Source = EMHGZAnimationSoundSource::CharacterSocket;
	Recipe->AttachSocket = TEXT("Root");
	TestTrue(TEXT("character source does not require weapon"), Recipe->ResolveEmission(Mesh, nullptr, Source, Location, Reason));
	AActor* Floor = H.World->SpawnActor<AActor>();
	UBoxComponent* Box = NewObject<UBoxComponent>(Floor);
	Floor->SetRootComponent(Box);
	Box->SetBoxExtent(FVector(1000, 1000, 5));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetCollisionResponseToAllChannels(ECR_Block);
	Box->RegisterComponent();
	const float GroundZ = Mesh->GetSocketLocation(TEXT("Root")).Z;
	Box->SetWorldLocation(FVector(0, 0, GroundZ - 5));
	H.World->UpdateWorldComponents(true, false);
	Recipe->Source = EMHGZAnimationSoundSource::GroundContact;
	TestTrue(TEXT("ground query resolves real floor"), Recipe->ResolveEmission(Mesh, nullptr, Source, Location, Reason));
	TestNull(TEXT("ground sound is a world one-shot, not attached to weapon/foot"), Source);
	TestTrue(TEXT("ground point lies on surface"), FMath::Abs(Location.Z - GroundZ) < .1);
	H.World->bAllowAudioPlayback = true;
	FAudioDeviceParams AudioParams;
	AudioParams.AssociatedWorld = H.World;
	AudioParams.Scope = EAudioDeviceScope::Shared;
	H.World->SetAudioDevice(FAudioDeviceManager::Get()->RequestAudioDevice(AudioParams));
	Recipe->Sound = LoadObject<USoundBase>(nullptr,
		TEXT("/Game/Weapons/InsectGlaive/Audio/Cues/MuGu/SC_IG_MuGu_3777612407_afd40217.SC_IG_MuGu_3777612407_afd40217"));
	if (TestNotNull(TEXT("resident current-weapon dust Cue"), Recipe->Sound.Get()))
	{
		TestTrue(TEXT("real dust Cue dispatches at valid ground with an audio device"), Recipe->PlayOneShot(Mesh, nullptr, Recipe->Sound));
	}
	Box->SetWorldLocation(FVector(0, 0, -1000));
	TestFalse(TEXT("no distant floor invented in air"), Recipe->ResolveEmission(Mesh, nullptr, Source, Location, Reason));
	TestEqual(TEXT("missing ground is distinguishable from audio/concurrency"), Reason, FName(TEXT("NoGroundContact")));
	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZActionSoundOwnershipTests,
	"MHGZ.Maintenance.Audio.Animation.HostLandingExactInstance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZActionSoundOwnershipTests::RunTest(const FString&)
{
	FMHGZM3Harness H;
	if (!H.Setup(false, AMHGZM3TestCharacter::StaticClass())) { H.Teardown(); return false; }
	USkeletalMeshComponent* Mesh = H.Character->GetMesh();
	Mesh->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Characters/Demo/Meshes/Body/SKM_Demo_Body.SKM_Demo_Body")));
	Mesh->SetAnimInstanceClass(UAnimInstance::StaticClass());
	UAnimMontage* Landing = LoadObject<UAnimMontage>(nullptr,
		TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_AerialLanding.AM_IG_AerialLanding"));
	H.CombatConfig->AerialLandingMontage = Landing;
	UAnimNotify_ActionSound* Recipe = NewObject<UAnimNotify_ActionSound>();
	if (TestTrue(TEXT("real Host landing starts without any attack GA"), H.Host->PlayAerialLandingVisual()))
	{
		FAnimMontageInstance* Instance = Mesh->GetAnimInstance()->GetActiveInstanceForMontage(Landing);
		if (TestNotNull(TEXT("real landing instance"), Instance))
		{
			const int32 OldID = Instance->GetInstanceID();
			TestTrue(TEXT("system-owned presentation accepted"), Recipe->IsCurrentPresentation(Mesh, OldID));
			TestFalse(TEXT("wrong ID rejected"), Recipe->IsCurrentPresentation(Mesh, OldID + 777));
			TestTrue(TEXT("same asset can start another landing"), H.Host->PlayAerialLandingVisual());
			TestFalse(TEXT("old same-asset instance cannot borrow new ownership"), Recipe->IsCurrentPresentation(Mesh, OldID));
			const auto* Current = Mesh->GetAnimInstance()->GetActiveInstanceForMontage(Landing);
			if (TestNotNull(TEXT("new exact instance"), Current))
				TestTrue(TEXT("new landing remains accepted"), Recipe->IsCurrentPresentation(Mesh, Current->GetInstanceID()));
			H.Host->StopAerialLandingVisual(0.f);
			TestFalse(TEXT("stopped presentation rejects late notify"), Recipe->IsCurrentPresentation(Mesh, OldID));
		}
	}
	H.Teardown();
	return true;
}
#endif
