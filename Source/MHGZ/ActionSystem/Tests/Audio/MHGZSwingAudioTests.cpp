// Copyright MHGZ Project. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "ActionSystem/AnimNotify_SwingSound.h"
#include "ActionSystem/MHGZKinsectSlashAbility.h"
#include "ActionSystem/MHGZM1PlaceholderAbilities.h"
#include "ActionSystem/Tests/Shared/MHGZM3TestHarness.h"
#include "Sound/SoundWave.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZSwingAudioTests,
	"MHGZ.Maintenance.Audio.Swing.InstanceResolutionAndLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZSwingAudioTests::RunTest(const FString&)
{
	FMHGZM3Harness H;
	if (!H.Setup()) { H.Teardown(); return false; }
	const FGameplayAbilitySpecHandle Handle = H.GiveAbility(UMHGZM1PlaceholderActionA::StaticClass());
	if (!TestTrue(TEXT("real action commits"), H.TryActivateWithInput(Handle, M3::MakePosedInput(true, false))))
	{
		H.Teardown(); return false;
	}
	FGameplayAbilitySpec* Spec = H.ASC->FindAbilitySpecFromHandle(Handle);
	UMHGZGameplayAbility* Action = Spec && !Spec->GetAbilityInstances().IsEmpty()
		? Cast<UMHGZGameplayAbility>(Spec->GetAbilityInstances()[0]) : nullptr;
	if (!TestNotNull(TEXT("per-execution action"), Action)) { H.Teardown(); return false; }
	const FWeaponActionToken Token = Action->GetActionToken();
	constexpr int32 InstanceID = 731;
	TestTrue(TEXT("exact montage registers"), Action->RegisterMontageInstance(H.Character->GetMesh(), InstanceID));
	UAnimNotify_SwingSound* Recipe = NewObject<UAnimNotify_SwingSound>();
	Recipe->EventTag = M3::Tag(TEXT("Audio.Swing.IG.TuCi.First"));
	USoundWave* Default = NewObject<USoundWave>();
	USoundWave* EventOverride = NewObject<USoundWave>();
	USoundWave* ActionOverride = NewObject<USoundWave>();
	Recipe->Sound = Default;
	TestNull(TEXT("not migrated is silent"), Action->ResolveSwingSound(Token, H.Character->GetMesh(), InstanceID, *Recipe));
	Action->bUseAuthoredSwingAudio = true;
	Action->AudioIdentityTag = M3::Tag(TEXT("Audio.Swing.IG.TuCi"));
	UMHGZWeaponDefinition* Weapon = H.Host->GetCurrentContext().WeaponDefinition;
	Weapon->SwingSoundOverrides.Add(Recipe->EventTag, EventOverride);
	Weapon->SwingSoundOverrides.Add(Action->AudioIdentityTag, ActionOverride);
	Action->PrepareSwingSoundOverrides();
	TestFalse(TEXT("already loaded soft overrides need no IO"), Action->SwingSoundLoadHandle.IsValid());
	auto Resolve = [&]() { return Action->ResolveSwingSound(Token, H.Character->GetMesh(), InstanceID, *Recipe); };
	TestEqual(TEXT("specific event wins"), Resolve(), static_cast<USoundBase*>(EventOverride));
	Recipe->Source = EMHGZAnimationSoundSource::GroundContact;
	TestEqual(TEXT("dust/contact cannot be replaced by event or action weapon override"), Resolve(), static_cast<USoundBase*>(Default));
	Recipe->Source = EMHGZAnimationSoundSource::WeaponSocket;
	TestEqual(TEXT("repeated actual passages are not action-wide deduplicated"), Resolve(), static_cast<USoundBase*>(EventOverride));
	Weapon->SwingSoundOverrides.Remove(Recipe->EventTag);
	Action->PrepareSwingSoundOverrides();
	TestEqual(TEXT("action identity fallback"), Resolve(), static_cast<USoundBase*>(ActionOverride));
	Weapon->SwingSoundOverrides.Empty();
	Action->PrepareSwingSoundOverrides();
	TestEqual(TEXT("authored hard default"), Resolve(), static_cast<USoundBase*>(Default));
	// Unloaded/missing override never forces synchronous IO or suppresses default.
	Weapon->SwingSoundOverrides.Add(Recipe->EventTag,
		TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Audio/Absent.Absent"))));
	TestEqual(TEXT("unprepared soft override does not load in Notify"), Resolve(), static_cast<USoundBase*>(Default));
	FWeaponActionToken Stale = Token;
	++Stale.ActivationSequenceID;
	TestNull(TEXT("wrong action rejected"), Action->ResolveSwingSound(Stale, H.Character->GetMesh(), InstanceID, *Recipe));
	TestNull(TEXT("wrong montage instance rejected"), Action->ResolveSwingSound(Token, H.Character->GetMesh(), InstanceID + 1, *Recipe));
	ACharacter* OtherActor = H.World->SpawnActor<ACharacter>();
	TestNull(TEXT("other avatar Mesh rejected"), Action->ResolveSwingSound(Token, OtherActor->GetMesh(), InstanceID, *Recipe));
	Action->SetActionActivationCommittedForTest(false);
	TestNull(TEXT("uncommitted action rejected"), Resolve());
	Action->SetActionActivationCommittedForTest(true);
	Recipe->Sound = nullptr;
	TestFalse(TEXT("missing sound skips without failing action"), Action->HandleSwingSound(Token, H.Character->GetMesh(), InstanceID, *Recipe));
	TestTrue(TEXT("missing audio does not cancel gameplay"), Action->IsActionActivationCommitted());
	Recipe->Sound = Default;
	Default->bLooping = true;
	TestFalse(TEXT("one-shot path refuses unmanaged loop"), Action->HandleSwingSound(Token, H.Character->GetMesh(), InstanceID, *Recipe));
	Default->bLooping = false;
	TestEqual(TEXT("shared recipe not mutated"), Recipe->Sound.Get(), static_cast<USoundBase*>(Default));
	TestEqual(TEXT("shared recipe event unchanged"), Recipe->EventTag, M3::Tag(TEXT("Audio.Swing.IG.TuCi.First")));

	const FGameplayAbilitySpecHandle OtherHandle = H.GiveAbility(UMHGZM1PlaceholderActionB::StaticClass());
	TestTrue(TEXT("second independent action commits"), H.TryActivateWithInput(OtherHandle, M3::MakePosedInput(true, false)));
	FGameplayAbilitySpec* OtherSpec = H.ASC->FindAbilitySpecFromHandle(OtherHandle);
	UMHGZGameplayAbility* Other = OtherSpec && !OtherSpec->GetAbilityInstances().IsEmpty()
		? Cast<UMHGZGameplayAbility>(OtherSpec->GetAbilityInstances()[0]) : nullptr;
	if (TestNotNull(TEXT("second per-execution instance"), Other))
	{
		Other->bUseAuthoredSwingAudio = true;
		Weapon->SwingSoundOverrides.Empty();
		Weapon->SwingSoundOverrides.Add(Recipe->EventTag, EventOverride);
		Other->PrepareSwingSoundOverrides();
		Other->RegisterMontageInstance(H.Character->GetMesh(), InstanceID + 1);
		TestEqual(TEXT("second action has its own loaded override"), Other->ResolveSwingSound(Other->GetActionToken(), H.Character->GetMesh(), InstanceID + 1, *Recipe), static_cast<USoundBase*>(EventOverride));
		TestEqual(TEXT("second action never mutates first's resolution"), Resolve(), static_cast<USoundBase*>(Default));
		TestNull(TEXT("second action cannot consume first's exact montage"), Other->ResolveSwingSound(Other->GetActionToken(), H.Character->GetMesh(), InstanceID, *Recipe));
		Other->RequestEndAction(EWeaponActionEndReason::Interrupted);
		TestEqual(TEXT("other End leaves first action audio intact"), Resolve(), static_cast<USoundBase*>(Default));
	}

	// Explicit legacy index only; -1 never silently maps to the first damage hit.
	UMHGZKinsectSlashAbility* Legacy = NewObject<UMHGZKinsectSlashAbility>(H.ASC);
	Legacy->AttackSegments.SetNum(2);
	Legacy->AttackSegments[1].Damage.SwingSound = Default;
	UMHGZGameplayAbility* LegacyBase = Legacy;
	TestEqual(TEXT("explicit legacy fallback remains usable"), LegacyBase->GetLegacySwingSound(1), static_cast<USoundBase*>(Default));
	TestNull(TEXT("no implicit damage-segment binding"), LegacyBase->GetLegacySwingSound(INDEX_NONE));

	Action->RequestEndAction(EWeaponActionEndReason::Interrupted);
	TestTrue(TEXT("End clears loaded override references"), Action->LoadedSwingSoundOverrides.IsEmpty());
	TestFalse(TEXT("End releases pending handle"), Action->SwingSoundLoadHandle.IsValid());
	TestNull(TEXT("retired notify cannot resolve"), Resolve());
	Action->RefreshLoadedSwingSounds(Token);
	TestTrue(TEXT("late completion cannot repopulate ended action"), Action->LoadedSwingSoundOverrides.IsEmpty());
	TestFalse(TEXT("retired montage registration removed"), H.Host->IsActionRegistered(Token));
	H.Teardown();
	return true;
}
#endif
