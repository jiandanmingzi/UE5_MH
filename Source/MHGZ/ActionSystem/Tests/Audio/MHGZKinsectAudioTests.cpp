// Copyright MHGZ Project. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "ActionSystem/Tests/Shared/MHGZM3TestHarness.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWave.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZKinsectAudioLifecycleTests,
	"MHGZ.Maintenance.Audio.Kinsect.StateAndRuntimeLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZKinsectAudioLifecycleTests::RunTest(const FString&)
{
	FMHGZM3Harness H;
	if (!H.Setup()) { H.Teardown(); return false; }
	USoundWave* Loop = NewObject<USoundWave>();
	Loop->bLooping = true;
	H.CombatConfig->KinsectFlightLoopSound = Loop;
	TestNotNull(TEXT("actor owns audio component"), H.Kinsect->FlightAudio.Get());
	TestFalse(TEXT("no auto activation on arm"), H.Kinsect->FlightAudio->bAutoActivate);
	TestNull(TEXT("attached is silent"), H.Kinsect->ResolveFlightLoopSound());
	H.Kinsect->ActiveRequest.RuntimeToken = H.Host->GetCurrentToken();
	for (const EKinsectState State : { EKinsectState::Flying, EKinsectState::Hovering })
	{
		H.Kinsect->State = State;
		TestEqual(TEXT("committed free state resolves loop"), H.Kinsect->ResolveFlightLoopSound(), static_cast<USoundBase*>(Loop));
	}
	H.Kinsect->ReturnRuntimeToken = H.Host->GetCurrentToken();
	H.Kinsect->State = EKinsectState::Returning;
	TestEqual(TEXT("return uses independent runtime token"), H.Kinsect->ResolveFlightLoopSound(), static_cast<USoundBase*>(Loop));
	H.Kinsect->ReturnRuntimeToken = FWeaponRuntimeToken();
	TestNull(TEXT("stale return cannot retain buzz"), H.Kinsect->ResolveFlightLoopSound());
	H.Kinsect->State = EKinsectState::ActionAttached;
	TestNull(TEXT("weapon attachment is not free flight"), H.Kinsect->ResolveFlightLoopSound());
	H.Kinsect->State = EKinsectState::Flying;
	Loop->bLooping = false;
	TestNull(TEXT("a finite preview cannot masquerade as loop"), H.Kinsect->ResolveFlightLoopSound());
	H.Kinsect->State = EKinsectState::Attached;
	H.Kinsect->RefreshFlightAudio();
	TestNull(TEXT("arm attachment clears component sound"), H.Kinsect->FlightAudio->Sound.Get());
	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZKinsectDeliveryAudioTests,
	"MHGZ.Maintenance.Audio.Kinsect.DeliveryCommandDedup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZKinsectDeliveryAudioTests::RunTest(const FString&)
{
	FMHGZM3Harness H;
	if (!H.Setup()) { H.Teardown(); return false; }
	H.CombatConfig->ExtractCollectedSound = nullptr;
	H.Resource->PlayExtractCollectedSound(11);
	TestEqual(TEXT("unconfigured sound does not claim serial"), H.Resource->LastExtractSoundCommandSerial, uint64(0));
	H.CombatConfig->ExtractCollectedSound = NewObject<USoundWave>();
	H.Resource->PlayExtractCollectedSound(11);
	TestEqual(TEXT("first color claims delivery serial"), H.Resource->LastExtractSoundCommandSerial, uint64(11));
	H.Resource->PlayExtractCollectedSound(11);
	TestEqual(TEXT("additional colors retain same claim"), H.Resource->LastExtractSoundCommandSerial, uint64(11));
	H.Resource->PlayExtractCollectedSound(12);
	TestEqual(TEXT("next return remains independent"), H.Resource->LastExtractSoundCommandSerial, uint64(12));
	H.Resource->PlayExtractCollectedSound(0);
	TestEqual(TEXT("legacy direct grant is not retained across commands"), H.Resource->LastExtractSoundCommandSerial, uint64(0));
	H.Teardown();
	return true;
}
#endif
