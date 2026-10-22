// Copyright MHGZ Project. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "ActionSystem/AnimNotify_CategorizedSound.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZSoundCategoryDefaultsTest,
	"MHGZ.Maintenance.Audio.Categories.DefaultsAndSharedBase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZSoundCategoryDefaultsTest::RunTest(const FString&)
{
	const auto* Dust = GetDefault<UAnimNotify_DustSound>();
	const auto* Landing = GetDefault<UAnimNotify_LandingSound>();
	const auto* Cloth = GetDefault<UAnimNotify_ClothSound>();
	const auto* Impact = GetDefault<UAnimNotify_GroundImpact>();
	TestTrue(TEXT("three distinct editor-addable types"), Dust->GetClass() != Landing->GetClass()
		&& Cloth->GetClass() != Dust->GetClass() && Cloth->GetClass() != Landing->GetClass());
	TestEqual(TEXT("dust defaults to ground contact"), Dust->Source, EMHGZAnimationSoundSource::GroundContact);
	TestEqual(TEXT("landing defaults to ground contact"), Landing->Source, EMHGZAnimationSoundSource::GroundContact);
	TestEqual(TEXT("cloth defaults to character, not ground/weapon"), Cloth->Source, EMHGZAnimationSoundSource::CharacterSocket);
	TestEqual(TEXT("cloth defaults to torso anchor"), Cloth->AttachSocket, FName(TEXT("Spine_00")));
	TestNull(TEXT("unknown cloth media is not guessed"), Cloth->Sound.Get());
	TestEqual(TEXT("impact defaults to real ground contact"), Impact->Source, EMHGZAnimationSoundSource::GroundContact);
	TestEqual(TEXT("impact defaults to ground projection of Root"), Impact->AttachSocket, FName(TEXT("Root")));
	TestTrue(TEXT("impact shares exact presentation validation"), Impact->IsA<UAnimNotify_ActionSound>());
	TestNotEqual(TEXT("impact and hunter landing are different events"), Impact->GetNotifyName(), Landing->GetNotifyName());
	TestTrue(TEXT("all categories share exact-instance playback"), Dust->IsA<UAnimNotify_ActionSound>()
		&& Landing->IsA<UAnimNotify_ActionSound>() && Cloth->IsA<UAnimNotify_ActionSound>());
	TestNotEqual(TEXT("dust and landing labels differ"), Dust->GetNotifyName(), Landing->GetNotifyName());
	return true;
}
#endif
