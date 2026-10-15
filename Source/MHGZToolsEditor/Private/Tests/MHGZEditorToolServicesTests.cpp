// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/MHGZMotionMatchingAnimInstance.h"
#include "Animation/MHGZPoseSearchTelemetryExport.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/SkeletalMeshComponent.h"
#include "EditorServices/MHGZEditorToolServices.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Features/IModularFeatures.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "MHGZCharacter.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/TraceAuxiliary.h"
#include "UI/MHGZUIAssetSetupLibrary.h"
#include "WidgetBlueprint.h"

namespace
{
struct FWithdrawServices
{
	TArray<IMHGZEditorToolServices*> Providers = IModularFeatures::Get()
		.GetModularFeatureImplementations<IMHGZEditorToolServices>(IMHGZEditorToolServices::GetFeatureName());
	FWithdrawServices()
	{
		for (IMHGZEditorToolServices* Provider : Providers)
		{
			IModularFeatures::Get().UnregisterModularFeature(IMHGZEditorToolServices::GetFeatureName(), Provider);
		}
	}
	~FWithdrawServices()
	{
		for (IMHGZEditorToolServices* Provider : Providers)
		{
			IModularFeatures::Get().RegisterModularFeature(IMHGZEditorToolServices::GetFeatureName(), Provider);
		}
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZEditorUIForwardingTest,
	"MHGZ.Maintenance.ToolsEditor.Services.UIForwarding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZEditorUIForwardingTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Exactly one editor provider is registered"), IModularFeatures::Get()
		.GetModularFeatureImplementationCount(IMHGZEditorToolServices::GetFeatureName()), 1);
	const TCHAR* Layouts[] = { TEXT("WBP_HUD"), TEXT("WBP_HealthBar"), TEXT("WBP_StaminaBar"),
		TEXT("WBP_Crosshair"), TEXT("WBP_IG_ResourcePanel") };
	for (const TCHAR* Layout : Layouts)
	{
		UWidgetBlueprint* Asset = NewObject<UWidgetBlueprint>(GetTransientPackage(), NAME_None, RF_Transient);
		Asset->WidgetTree = NewObject<UWidgetTree>(Asset, TEXT("WidgetTree"), RF_Transient);
		TestTrue(Layout, UMHGZUIAssetSetupLibrary::BuildE63WidgetBlueprint(Asset, FName(Layout)));
		TestNotNull(TEXT("Forwarded builder creates its root"), Cast<UCanvasPanel>(Asset->WidgetTree->RootWidget));
	}
	TestEqual(TEXT("Python/Blueprint facade retains its original native path"),
		UMHGZUIAssetSetupLibrary::StaticClass()->GetPathName(), FString(TEXT("/Script/MHGZ.MHGZUIAssetSetupLibrary")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZEditorProviderWithdrawalTest,
	"MHGZ.Maintenance.ToolsEditor.Services.ProviderWithdrawal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZEditorProviderWithdrawalTest::RunTest(const FString& Parameters)
{
	{
		FWithdrawServices Withdrawal;
		TestFalse(TEXT("Provider can be withdrawn without a cached editor pointer"),
			IModularFeatures::Get().IsModularFeatureAvailable(IMHGZEditorToolServices::GetFeatureName()));
		TestFalse(TEXT("UI facade safely fails without a provider"),
			UMHGZUIAssetSetupLibrary::BuildE63WidgetBlueprint(nullptr, TEXT("WBP_HUD")));
		TestFalse(TEXT("Trace facade safely fails without a provider"),
			FMHGZPoseSearchTelemetryExport::ExportTraceToCSV(TEXT("missing.utrace"), TEXT("unused"), 24));
	}
	TestEqual(TEXT("Provider restored after withdrawal scope"), IModularFeatures::Get()
		.GetModularFeatureImplementationCount(IMHGZEditorToolServices::GetFeatureName()), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZEditorTelemetryStopTest,
	"MHGZ.Maintenance.ToolsEditor.Services.TelemetryStop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZEditorTelemetryStopTest::RunTest(const FString& Parameters)
{
	if (FTraceAuxiliary::IsConnected())
	{
		AddError(TEXT("Cannot test capture end while another trace is connected; do not steal it."));
		return false;
	}
	IConsoleVariable* Capture = IConsoleManager::Get().FindConsoleVariable(TEXT("mhgz.Telemetry.Enable"));
	IConsoleVariable* Detail = IConsoleManager::Get().FindConsoleVariable(TEXT("mhgz.Telemetry.PoseSearchDetail"));
	if (!TestNotNull(TEXT("Capture cvar"), Capture) || !TestNotNull(TEXT("Detail cvar"), Detail)) { return false; }
	const int32 OldCapture = Capture->GetInt();
	const int32 OldDetail = Detail->GetInt();
	if (OldCapture != 0)
	{
		AddError(TEXT("An existing telemetry capture is active; leave it untouched."));
		return false;
	}

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Transient capture world"), World)) { return false; }
	// CreateWorld already initializes its WorldSettings (same pattern as the
	// project's existing runtime harness). Do not initialize it a second time.
	UClass* CharacterClass = LoadClass<AMHGZCharacter>(nullptr,
		TEXT("/Game/Blueprints/Characters/Demo/BP_IG_Character.BP_IG_Character_C"));
	AMHGZCharacter* Character = CharacterClass ? World->SpawnActor<AMHGZCharacter>(CharacterClass) : nullptr;
	if (!TestNotNull(TEXT("Transient capture character"), Character))
	{
		World->DestroyWorld(false);
		return false;
	}
	UMHGZMotionMatchingAnimInstance* Anim = NewObject<UMHGZMotionMatchingAnimInstance>(Character->GetMesh());
	Anim->NativeInitializeAnimation();
	const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("RuntimeTelemetry"));
	TArray<FString> ExistingSessions;
	IFileManager::Get().FindFiles(ExistingSessions, *(Root / TEXT("*")), false, true);
	Capture->Set(1, ECVF_SetByCode);
	Detail->Set(1, ECVF_SetByCode);
	Anim->NativeUpdateAnimation(1.0f / 60.0f);
	// Trace connection is asynchronous. A real capture lasts frames; this fixture
	// must allow backend updates, not merely wait for IsConnected(), which also
	// reports pending handles and causes Stop() to reject the request.
	FGuid SessionGuid;
	FGuid TraceGuid;
	const double MinimumCaptureEnd = FPlatformTime::Seconds() + 0.1;
	const double ConnectDeadline = FPlatformTime::Seconds() + 2.0;
	while ((FPlatformTime::Seconds() < MinimumCaptureEnd
		|| !FTraceAuxiliary::IsConnected(SessionGuid, TraceGuid) || !TraceGuid.IsValid())
		&& FPlatformTime::Seconds() < ConnectDeadline)
	{
		FPlatformProcess::Sleep(0.01f);
	}
	TestTrue(TEXT("Capture trace backend produces a valid trace GUID"),
		FTraceAuxiliary::IsConnected(SessionGuid, TraceGuid) && TraceGuid.IsValid());
	// Exercise real stop -> first automatic export. No error suppression or
	// post-stop test-side export is allowed: the production call must succeed.
	Anim->NativeUninitializeAnimation();
	Capture->Set(OldCapture, ECVF_SetByCode);
	Detail->Set(OldDetail, ECVF_SetByCode);
	TestFalse(TEXT("First automatic export waits until its trace is closed"), FTraceAuxiliary::IsConnected());

	// Identify only this test's exact session; archive it outside the user's recordings.
	TArray<FString> Sessions;
	IFileManager::Get().FindFiles(Sessions, *(Root / TEXT("*")), false, true);
	const FString Suffix = FString::Printf(TEXT("-%s-%u"), *Character->GetName(), Anim->GetUniqueID());
	int32 Matches = 0;
	for (const FString& Session : Sessions)
	{
		if (!Session.EndsWith(Suffix) || ExistingSessions.Contains(Session)) { continue; }
		++Matches;
		const FString Source = Root / Session;
		const FString DetailDir = Source / TEXT("MotionMatching");
		TestTrue(TEXT("Raw trace exists after capture end"), FPaths::FileExists(DetailDir / TEXT("PoseSearchDetail.utrace")));
		FString CSV;
		TestTrue(TEXT("First automatic export produces candidates CSV without a test-side retry"),
			FFileHelper::LoadFileToString(CSV, *(DetailDir / TEXT("PoseSearchCandidates.csv"))));
		TestTrue(TEXT("Candidate CSV keeps its native format"), CSV.StartsWith(TEXT("TraceTimeSeconds,")));
		const FString ArchiveRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Maintenance/EditorServices/CaptureTests"));
		IFileManager::Get().MakeDirectory(*ArchiveRoot, true);
		const FString Destination = ArchiveRoot / Session;
		// Validate both absolute targets before moving, not only through assertions.
		if (!Source.StartsWith(Root + TEXT("/")) || !Destination.StartsWith(ArchiveRoot + TEXT("/"))
			|| IFileManager::Get().DirectoryExists(*Destination))
		{
			AddError(TEXT("Capture archive path is unsafe or already exists; do not move it."));
			continue;
		}
		if (!FTraceAuxiliary::IsConnected())
		{
			TestTrue(TEXT("Archive only this new test capture"), IFileManager::Get().Move(*Destination, *Source, false, true));
		}
	}
	TestEqual(TEXT("Exactly one capture belongs to this test instance"), Matches, 1);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZEditorTraceOpenFailureTest,
	"MHGZ.Maintenance.ToolsEditor.Services.TraceOpenFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZEditorTraceOpenFailureTest::RunTest(const FString& Parameters)
{
	if (FTraceAuxiliary::IsConnected())
	{
		AddError(TEXT("Cannot create an unrelated test trace while another trace is connected; leave it untouched."));
		return false;
	}
	const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()
		/ TEXT("Maintenance/TraceCloseFix") / (TEXT("OpenFailure-") + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	TestTrue(TEXT("Create unique failure fixture directory"), IFileManager::Get().MakeDirectory(*Directory, true));
	const FString MissingOutput = Directory / TEXT("MissingOutput");
	AddExpectedMessagePlain(TEXT("[RuntimeTelemetry] Pose Search detail trace was not found:"),
		ELogVerbosity::Error, EAutomationExpectedMessageFlags::Contains, 1);
	TestFalse(TEXT("Missing trace fails without creating CSV output"),
		FMHGZPoseSearchTelemetryExport::ExportTraceToCSV(Directory / TEXT("missing.utrace"), MissingOutput, 24));
	TestFalse(TEXT("Missing trace does not create output directory"), IFileManager::Get().DirectoryExists(*MissingOutput));

	const FString TracePath = Directory / TEXT("locked.utrace");
	TUniquePtr<FArchive> Writer(IFileManager::Get().CreateFileWriter(*TracePath));
	if (!TestNotNull(TEXT("Exclusive writer holds test trace open"), Writer.Get())) { return false; }
	uint32 Payload = 0x12345678;
	Writer->Serialize(&Payload, sizeof(Payload));
	Writer->Flush();
	const FString CandidatePath = Directory / TEXT("PoseSearchCandidates.csv");
	const FString ChannelPath = Directory / TEXT("PoseSearchChannelCosts.csv");
	const FString Sentinel = TEXT("Existing output must survive failed export.");
	TestTrue(TEXT("Seed existing candidates"), FFileHelper::SaveStringToFile(Sentinel, *CandidatePath));
	TestTrue(TEXT("Seed existing channel costs"), FFileHelper::SaveStringToFile(Sentinel, *ChannelPath));

	FTraceAuxiliary::FOptions Options;
	Options.bTruncateFile = true;
	Options.bExcludeTail = true;
	const FString UnrelatedPath = Directory / TEXT("unrelated.utrace");
	const bool bOwnUnrelatedTrace = FTraceAuxiliary::Start(
		FTraceAuxiliary::EConnectionType::File, *UnrelatedPath, TEXT("Object"), &Options);
	if (!TestTrue(TEXT("Start an unrelated trace owned only by this test"), bOwnUnrelatedTrace)) { return false; }
	// Give this trace's start request time to complete; Stop rejects a pending start.
	FPlatformProcess::Sleep(0.1f);
	FGuid SessionGuid;
	FGuid TraceGuid;
	TestTrue(TEXT("Unrelated trace is connected"), FTraceAuxiliary::IsConnected(SessionGuid, TraceGuid));
	AddExpectedMessagePlain(TEXT("[RuntimeTelemetry] Could not open Pose Search detail trace after 2.00s; raw trace retained:"),
		ELogVerbosity::Error, EAutomationExpectedMessageFlags::Contains, 1);
	const double Start = FPlatformTime::Seconds();
	TestFalse(TEXT("A persistently locked file returns failure instead of exporting a partial trace"),
		FMHGZPoseSearchTelemetryExport::ExportTraceToCSV(TracePath, Directory, 24));
	const double Elapsed = FPlatformTime::Seconds() - Start;
	TestTrue(TEXT("Readiness wait has a two-second deadline, with scheduling tolerance"), Elapsed >= 2.0 && Elapsed < 5.0);
	FGuid AfterSessionGuid;
	FGuid AfterTraceGuid;
	TestTrue(TEXT("Failed export does not stop the unrelated trace"), FTraceAuxiliary::IsConnected(AfterSessionGuid, AfterTraceGuid));
	TestEqual(TEXT("Unrelated trace identity is unchanged"), AfterTraceGuid, TraceGuid);
	TestTrue(TEXT("Only the test stops its own unrelated trace"), FTraceAuxiliary::Stop());
	const double CloseDeadline = FPlatformTime::Seconds() + 2.0;
	while (FTraceAuxiliary::IsConnected() && FPlatformTime::Seconds() < CloseDeadline)
	{
		FPlatformProcess::Sleep(0.01f);
	}
	TestFalse(TEXT("Test-owned trace is closed before cleanup"), FTraceAuxiliary::IsConnected());
	Writer.Reset();
	TestTrue(TEXT("Failed export retains its raw trace"), FPaths::FileExists(TracePath));
	FString CSV;
	TestTrue(TEXT("Existing candidates still readable"), FFileHelper::LoadFileToString(CSV, *CandidatePath));
	TestEqual(TEXT("Existing candidates were not truncated"), CSV, Sentinel);
	TestTrue(TEXT("Existing channel costs still readable"), FFileHelper::LoadFileToString(CSV, *ChannelPath));
	TestEqual(TEXT("Existing channel costs were not truncated"), CSV, Sentinel);
	FString Status;
	TestTrue(TEXT("Timeout produces actionable status"),
		FFileHelper::LoadFileToString(Status, *(Directory / TEXT("PoseSearchDetail-Status.txt"))));
	TestTrue(TEXT("Timeout is not reported as successful export"), Status.Contains(TEXT("Status: Failed")));
	return true;
}

#endif
