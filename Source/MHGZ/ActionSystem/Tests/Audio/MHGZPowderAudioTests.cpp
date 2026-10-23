// Copyright MHGZ Project. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "ActionSystem/Tests/Shared/MHGZM3TestHarness.h"
#include "Components/AudioComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZPowderAudioLifecycleTests,
	"MHGZ.Maintenance.Audio.Powder.GatherCommitCancelAndTeardown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZPowderAudioLifecycleTests::RunTest(const FString&)
{
	FMHGZM3Harness H;
	if (!H.Setup()) { H.Teardown(); return false; }
	FKinsectFlightRequest Request;
	Request.RuntimeToken = H.Host->GetCurrentToken();
	Request.ActionActivationSequenceID = H.Host->AllocateActivationSequenceID();
	Request.CommandSource = EKinsectCommandSource::PowderVortex;
	Request.CommandSerial = H.Resource->AdvanceKinsectCommand(EKinsectCommandSource::PowderVortex);
	H.Resource->ActivePowderVortexCommandSerial = Request.CommandSerial;
	TestTrue(TEXT("actual arrival creates gather transaction with optional sounds empty"),
		H.Resource->BeginPowderVortexGathering(Request, FVector(700, 0, 100)));
	// Headless tests do not claim an audible device. Inject a component to test
	// ownership/lifetime without depending on XAudio hardware or a decoded Wave.
	UAudioComponent* Gather = NewObject<UAudioComponent>(H.Character);
	H.Resource->PowderVortexGatherAudio.Add(Gather);
	H.Resource->CommitPowderVortexTransaction(Request.CommandSerial);
	TestEqual(TEXT("early timer does not stop gathering"), H.Resource->PowderVortexGatherAudio.Num(), 1);
	auto* Reservation = H.Resource->PowderReservations.Find(H.Resource->ActivePowderVortexReservationID);
	if (!TestNotNull(TEXT("gather reservation exists before deadline"), Reservation))
	{
		H.Teardown(); return false;
	}
	Reservation->GatherDeadlineWorldTime = H.World->GetTimeSeconds();
	H.Resource->CommitPowderVortexTransaction(Request.CommandSerial);
	TestTrue(TEXT("commit stops gather audio even with zero powder"), H.Resource->PowderVortexGatherAudio.IsEmpty());
	TestEqual(TEXT("commit retires exact command"), H.Resource->ActivePowderVortexCommandSerial, uint64(0));

	const uint64 NextSerial = H.Resource->AdvanceKinsectCommand(EKinsectCommandSource::PowderVortex);
	H.Resource->ActivePowderVortexCommandSerial = NextSerial;
	H.Resource->PowderVortexGatherAudio.Add(Gather);
	H.Resource->CancelPowderVortexTransaction(Request.CommandSerial);
	TestEqual(TEXT("stale cancellation cannot stop newer gather audio"), H.Resource->PowderVortexGatherAudio.Num(), 1);
	H.Resource->CancelPowderVortexTransaction(NextSerial);
	TestTrue(TEXT("matching cancellation stops audio"), H.Resource->PowderVortexGatherAudio.IsEmpty());
	H.Resource->PowderVortexGatherAudio.Add(Gather);
	H.Resource->ClearOwnedPowders(EWeaponRuntimeEndReason::WeaponChanged);
	TestTrue(TEXT("runtime teardown stops pending gather sound"), H.Resource->PowderVortexGatherAudio.IsEmpty());
	H.Teardown();
	return true;
}
#endif
