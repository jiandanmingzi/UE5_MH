// Copyright MHGZ Project. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MHGZAnimationSeamLibrary.h"
#include "ActionSystem/AnimNotify_CategorizedSound.h"
#include "ActionSystem/AnimNotify_SheatheCommit.h"
#include "Animation/AnimMontage.h"
#include "Sound/SoundWave.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZSoundNotifyClassMigrationTest,
	"MHGZ.Maintenance.Audio.Categories.AuthoringPreservesEventAndRecipe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZSoundNotifyClassMigrationTest::RunTest(const FString&)
{
	UAnimMontage* Montage = NewObject<UAnimMontage>();
	Montage->SlotAnimTracks.AddDefaulted();
	Montage->Notifies.SetNum(2);
	FAnimNotifyEvent& Event = Montage->Notifies[0];
	Montage->Notifies[1].Guid = FGuid::NewGuid();
	Montage->Notifies[1].Link(Montage, .1f); // Deliberately not chronological.
	Event.Guid = FGuid::NewGuid();
	Event.NotifyName = TEXT("ExistingAuthorName");
	Event.TrackIndex = 2;
	Event.Link(Montage, .25f);
	UAnimNotify_SwingSound* Old = NewObject<UAnimNotify_SwingSound>(Montage);
	Old->Sound = NewObject<USoundWave>(Montage);
	Old->Source = EMHGZAnimationSoundSource::GroundContact;
	Old->AttachSocket = TEXT("L_Leg_03");
	Old->GroundTraceDistance = 123.f;
	Old->VolumeMultiplier = .7f;
	Old->PitchMultiplier = 1.2f;
	Old->LegacyAttackSegmentIndex = 2;
	Event.Notify = Old;
	const FAnimNotifyEvent Before = Event;
	TestTrue(TEXT("targeted class migration succeeds"), UMHGZAnimationSeamLibrary::ReplaceSoundNotifyClass(
		Montage, Event.Guid, UAnimNotify_DustSound::StaticClass()));
	const auto* Replacement = Cast<UAnimNotify_DustSound>(Event.Notify);
	if (TestNotNull(TEXT("real category class assigned"), Replacement))
	{
		TestEqual(TEXT("GUID retained"), Event.Guid, Before.Guid);
		TestEqual(TEXT("authored name retained"), Event.NotifyName, Before.NotifyName);
		TestEqual(TEXT("time retained"), Event.GetTime(), Before.GetTime());
		TestEqual(TEXT("track retained"), Event.TrackIndex, Before.TrackIndex);
		TestEqual(TEXT("array order not sorted as side effect"), Montage->Notifies[0].Guid, Before.Guid);
		TestEqual(TEXT("sound retained"), Replacement->Sound.Get(), Old->Sound.Get());
		TestEqual(TEXT("anchor retained"), Replacement->AttachSocket, Old->AttachSocket);
		TestEqual(TEXT("trace retained"), Replacement->GroundTraceDistance, Old->GroundTraceDistance);
		TestEqual(TEXT("volume retained"), Replacement->VolumeMultiplier, Old->VolumeMultiplier);
		TestEqual(TEXT("pitch retained"), Replacement->PitchMultiplier, Old->PitchMultiplier);
		TestEqual(TEXT("legacy index retained"), Replacement->LegacyAttackSegmentIndex, Old->LegacyAttackSegmentIndex);
	}
	TestTrue(TEXT("same class is idempotent"), UMHGZAnimationSeamLibrary::ReplaceSoundNotifyClass(
		Montage, Event.Guid, UAnimNotify_DustSound::StaticClass()));
	TestFalse(TEXT("wrong GUID rejected"), UMHGZAnimationSeamLibrary::ReplaceSoundNotifyClass(
		Montage, FGuid::NewGuid(), UAnimNotify_LandingSound::StaticClass()));
	TestFalse(TEXT("arbitrary Notify class rejected"), UMHGZAnimationSeamLibrary::ReplaceSoundNotifyClass(
		Montage, Event.Guid, UAnimNotify::StaticClass()));
	TestTrue(TEXT("legacy dust can migrate to the presentation event"), UMHGZAnimationSeamLibrary::ReplaceSoundNotifyClass(
		Montage, Event.Guid, UAnimNotify_GroundDust::StaticClass()));
	const auto* DustEvent = Cast<UAnimNotify_GroundDust>(Event.Notify);
	if (TestNotNull(TEXT("unified ground event replaces sound-only class"), DustEvent))
	{
		TestEqual(TEXT("dust migration retains timing"), Event.GetTime(), Before.GetTime());
		TestEqual(TEXT("dust migration retains track"), Event.TrackIndex, Before.TrackIndex);
		TestEqual(TEXT("dust migration retains audio"), DustEvent->Sound.Get(), Old->Sound.Get());
		TestEqual(TEXT("dust migration retains gain"), DustEvent->VolumeMultiplier, Old->VolumeMultiplier);
	}
	// Track-only edits retain the exact event object, identity, clock and array order.
	const FGuid Guid = Event.Guid;
	UAnimNotify* SoundBeforeMove = Event.Notify.Get();
	const int32 TracksBefore = Montage->AnimNotifyTracks.Num();
	TestTrue(TEXT("sound can move to a new dedicated track"), UMHGZAnimationSeamLibrary::MoveSoundNotifyToTrack(
		Montage, Guid, TEXT("DustAudio")));
	TestEqual(TEXT("one track appended"), Montage->AnimNotifyTracks.Num(), TracksBefore + 1);
	TestEqual(TEXT("only track index changes"), Event.TrackIndex, TracksBefore);
	TestEqual(TEXT("existing event object retained"), Event.Notify.Get(), SoundBeforeMove);
	TestEqual(TEXT("track move retains time"), Event.GetTime(), Before.GetTime());
	TestEqual(TEXT("track move retains GUID/order"), Montage->Notifies[0].Guid, Guid);
	TestEqual(TEXT("new track cache uses same event"), Montage->AnimNotifyTracks.Last().Notifies[0], &Event);
	TestTrue(TEXT("moving to same track is idempotent"), UMHGZAnimationSeamLibrary::MoveSoundNotifyToTrack(
		Montage, Guid, TEXT("DustAudio")));
	TestEqual(TEXT("idempotence does not duplicate tracks"), Montage->AnimNotifyTracks.Num(), TracksBefore + 1);
	TestFalse(TEXT("track move rejects a wrong GUID"), UMHGZAnimationSeamLibrary::MoveSoundNotifyToTrack(
		Montage, FGuid::NewGuid(), TEXT("DustAudio")));
	TestFalse(TEXT("track move rejects a gameplay event"), UMHGZAnimationSeamLibrary::MoveSoundNotifyToTrack(
		Montage, Montage->Notifies[1].Guid, TEXT("DustAudio")));
	UAnimMontage* LandingMontage = NewObject<UAnimMontage>();
	LandingMontage->SlotAnimTracks.AddDefaulted();
	auto& LandingPoint = LandingMontage->Notifies.AddDefaulted_GetRef();
	LandingPoint.Guid = FGuid::NewGuid();
	LandingPoint.Link(LandingMontage, .125f);
	auto* LandingSound = NewObject<UAnimNotify_LandingSound>(LandingMontage);
	LandingSound->Sound = Old->Sound;
	LandingSound->VolumeMultiplier = .8f;
	LandingPoint.Notify = LandingSound;
	const FGuid LandingGuid = LandingPoint.Guid;
	TestTrue(TEXT("legacy landing migrates without recreating its event"), UMHGZAnimationSeamLibrary::ReplaceSoundNotifyClass(
		LandingMontage, LandingGuid, UAnimNotify_LandingEvent::StaticClass()));
	if (const auto* LandingEvent = Cast<UAnimNotify_LandingEvent>(LandingPoint.Notify))
	{
		TestEqual(TEXT("landing identity retained"), LandingPoint.Guid, LandingGuid);
		TestEqual(TEXT("landing time retained"), LandingPoint.GetTime(), .125f);
		TestEqual(TEXT("landing audio retained"), LandingEvent->Sound.Get(), Old->Sound.Get());
		TestEqual(TEXT("landing gain retained"), LandingEvent->VolumeMultiplier, .8f);
		TestFalse(TEXT("migration does not enable duplicate dust"), LandingEvent->bTriggerGroundDust);
		TestNull(TEXT("no unrequested dust audio is injected"), LandingEvent->GroundDustSound.Get());
	}
	else { AddError(TEXT("LandingEvent class not installed")); }
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZSoundNotifyTimingTest,
	"MHGZ.Maintenance.Audio.Categories.GuardedSourceTimeEdits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZSoundNotifyTimingTest::RunTest(const FString&)
{
	auto* Montage = NewObject<UAnimMontage>();
	Montage->SetCompositeLength(4.f);
	Montage->SlotAnimTracks.AddDefaulted();
	for (int32 I = 0; I < 2; ++I)
	{
		auto& Event = Montage->Notifies.AddDefaulted_GetRef();
		Event.Guid = FGuid::NewGuid();
		Event.Link(Montage, 1.f + I);
		Event.TrackIndex = I;
		Event.Notify = I == 0 ? static_cast<UAnimNotify*>(NewObject<UAnimNotify_GroundImpact>(Montage))
			: NewObject<UAnimNotify_SheatheCommit>(Montage);
	}
	const auto Before = Montage->Notifies;
	const FString Guid = Before[0].Guid.ToString();
	auto Json = [&Guid](float Expected, const TCHAR* Tail)
	{
		return FString::Printf(TEXT("[{\"guid\":\"%s\",\"expected_time\":%f,%s}]"), *Guid, Expected, Tail);
	};
	TestFalse(TEXT("stale time rejected"), UMHGZAnimationSeamLibrary::EditSoundNotifyTimes(Montage,
		Json(.5f, TEXT("\"remove\":false,\"time\":3"))));
	TestEqual(TEXT("rejection leaves original time"), Montage->Notifies[0].GetTime(), 1.f);
	TestTrue(TEXT("sound retime accepted"), UMHGZAnimationSeamLibrary::EditSoundNotifyTimes(Montage,
		Json(1.f, TEXT("\"remove\":false,\"time\":3"))));
	TestEqual(TEXT("sound time updated"), Montage->Notifies[0].GetTime(), 3.f);
	TestEqual(TEXT("sound GUID preserved"), Montage->Notifies[0].Guid, Before[0].Guid);
	TestEqual(TEXT("recipe object preserved"), Montage->Notifies[0].Notify.Get(), Before[0].Notify.Get());
	TestEqual(TEXT("track preserved"), Montage->Notifies[0].TrackIndex, Before[0].TrackIndex);
	TestEqual(TEXT("gameplay time preserved"), Montage->Notifies[1].GetTime(), Before[1].GetTime());
	TestFalse(TEXT("gameplay event cannot be removed"), UMHGZAnimationSeamLibrary::EditSoundNotifyTimes(Montage,
		FString::Printf(TEXT("[{\"guid\":\"%s\",\"expected_time\":2,\"remove\":true}]"), *Before[1].Guid.ToString())));
	TestTrue(TEXT("known cosmetic misattribution can be removed"), UMHGZAnimationSeamLibrary::EditSoundNotifyTimes(Montage,
		Json(3.f, TEXT("\"remove\":true"))));
	TestEqual(TEXT("only one event removed"), Montage->Notifies.Num(), 1);
	TestEqual(TEXT("remaining gameplay event identity preserved"), Montage->Notifies[0].Guid, Before[1].Guid);
	return true;
}
#endif
