// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "AttributeSystem/Res_InsectGlaive.h"
#include "ActionSystem/MHGZWeaponComboData.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"
#include "UI/MHGZUITelemetry.h"
#include "UI/MHGZHUD.h"
#include "UI/MHGZMainHUDWidget.h"
#include "UI/MHGZAttributeBarWidget.h"
#include "UI/MHGZCrosshairWidget.h"
#include "UI/MHGZInsectGlaiveResourceWidget.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "Components/Overlay.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/LocalPlayer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Slate/WidgetRenderer.h"
#include "Widgets/Layout/SBorder.h"
#include "Styling/CoreStyle.h"
#include "RenderingThread.h"
#include "Misc/Crc.h"
#include "../../Shared/MHGZM3TestHarness.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM6UIResourceSnapshotAndRuntimeReady,
	"MHGZ.M6.UI.ResourceSnapshotAndRuntimeReady",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM6UIResourceSnapshotAndRuntimeReady::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FMHGZM3Harness H;
	if (!H.Setup())
	{
		AddError(TEXT("M3 runtime harness setup failed"));
		return false;
	}

	TestTrue(TEXT("runtime becomes ready only after the fixture is assembled"),
		H.Host->IsWeaponRuntimeReady());
	TestTrue(TEXT("ready resource provider is the equipped insect-glaive resource"),
		H.Host->GetResourceProvider() == H.Resource);

	const FIGResourceUISnapshot InitialState = H.Resource->GetUIState();
	TestTrue(TEXT("snapshot belongs to the current runtime generation"),
		InitialState.RuntimeToken == H.Host->GetCurrentToken());
	TestTrue(TEXT("snapshot reports the attached kinsect"), InitialState.bHasKinsect);
	TestEqual(TEXT("kinsect starts attached"), InitialState.KinsectState,
		EKinsectState::Attached);
	TestFalse(TEXT("attached kinsect has no pending extract cargo"),
		InitialState.KinsectPendingExtractColor.IsValid());
	TestTrue(TEXT("kinsect starts with its configured stamina"),
		InitialState.KinsectStamina > 0.0f
			&& FMath::IsNearlyEqual(InitialState.KinsectStamina,
				InitialState.MaxKinsectStamina));

	int32 UpdateCount = 0;
	FIGResourceUISnapshot LastState;
	const FDelegateHandle Handle = H.Resource->OnIGResourceUIChanged().AddLambda(
		[&UpdateCount, &LastState](const FIGResourceUISnapshot& State)
		{
			++UpdateCount;
			LastState = State;
		});

	TestTrue(TEXT("adding a dance stack succeeds"),
		H.Resource->AddDanceStack(EIGDanceSource::KinsectSlash));
	TestEqual(TEXT("dance change publishes a resource snapshot"), UpdateCount, 1);
	TestEqual(TEXT("published snapshot carries the authoritative stack count"),
		LastState.DanceStacks, H.Resource->GetDanceStacks());
	TestEqual(TEXT("published snapshot carries the authoritative source"),
		LastState.DanceSource, EIGDanceSource::KinsectSlash);

	TestTrue(TEXT("valid extract can be applied"), H.Resource->ApplyExtract(
		M3::Tag(TEXT("WeaponResource.IG.Extract.White"))));
	TestTrue(TEXT("extract change publishes its active state"), LastState.bWhite);
	TestTrue(TEXT("extract publication is a later update"), UpdateCount >= 2);

	H.Resource->OnIGResourceUIChanged().Remove(Handle);
	H.Resource->ClearDanceStacks(EIGDanceClearReason::Landed);
	TestEqual(TEXT("removed listener receives no further updates"), UpdateCount, 2);

	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM6UITelemetryQueueIsOrderedAndBounded,
	"MHGZ.M6.UI.TelemetryQueueIsOrderedAndBounded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM6UITelemetryQueueIsOrderedAndBounded::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FMHGZUIEventTelemetryQueue Queue;
	FMHGZUIEventTelemetry CreateEvent;
	CreateEvent.EventName = TEXT("HUDCreate");
	Queue.Record(CreateEvent);
	FMHGZUIEventTelemetry BindEvent;
	BindEvent.EventName = TEXT("PawnBind");
	Queue.Record(BindEvent);

	uint64 LastSerial = 0;
	TArray<FMHGZUIEventTelemetry> Events;
	Queue.GetEventsSince(LastSerial, Events);
	TestEqual(TEXT("events preserve their recorded count"), Events.Num(), 2);
	TestTrue(TEXT("events preserve monotonically increasing serials"),
		Events.Num() == 2 && Events[0].Serial == 1 && Events[1].Serial == 2);
	TestEqual(TEXT("cursor advances to the latest consumed event"), LastSerial,
		Queue.GetLatestSerial());

	for (int32 Index = 0; Index < 4100; ++Index)
	{
		FMHGZUIEventTelemetry Event;
		Event.EventName = TEXT("ResourcePanelCreate");
		Queue.Record(MoveTemp(Event));
	}
	LastSerial = 0;
	Queue.GetEventsSince(LastSerial, Events);
	TestEqual(TEXT("bounded queue retains only its newest events"), Events.Num(), 4096);
	TestTrue(TEXT("first retained serial exposes dropped rows to the CSV writer"),
		!Events.IsEmpty() && Events[0].Serial == 7);
	return true;
}

/** Test-only observation seam. All widgets are real persistent WBP classes. */
struct FMHGZM6HUDTestAccess
{
	static bool ValidateAuthoredClasses(AMHGZHUD* HUD)
	{
		return HUD->MainHUDClass == LoadClass<UMHGZMainHUDWidget>(nullptr, TEXT("/Game/UI/HUD/WBP_HUD.WBP_HUD_C"))
			&& HUD->HealthBarClass == LoadClass<UMHGZAttributeBarWidget>(nullptr, TEXT("/Game/UI/HUD/WBP_HealthBar.WBP_HealthBar_C"))
			&& HUD->StaminaBarClass == LoadClass<UMHGZAttributeBarWidget>(nullptr, TEXT("/Game/UI/HUD/WBP_StaminaBar.WBP_StaminaBar_C"))
			&& HUD->CrosshairClass == LoadClass<UMHGZCrosshairWidget>(nullptr, TEXT("/Game/UI/HUD/WBP_Crosshair.WBP_Crosshair_C"));
	}
	static void Ready(AMHGZHUD* HUD, const FWeaponRuntimeToken& Token, URes_InsectGlaive* RC)
	{
		HUD->HandleWeaponRuntimeReady(Token, RC);
	}
	static void Invalidate(AMHGZHUD* HUD, const FWeaponRuntimeToken& Token)
	{
		HUD->HandleWeaponRuntimeInvalidated(Token);
	}
	static bool ContextInitialized(AMHGZHUD* HUD) { return HUD->bPawnContextInitialized; }
	static uint64 Generation(AMHGZHUD* HUD) { return HUD->BoundRuntimeToken.Generation; }
	static UMHGZMainHUDWidget* Main(AMHGZHUD* HUD) { return HUD->MainHUDWidget; }
	static UMHGZAttributeBarWidget* Health(AMHGZHUD* HUD) { return HUD->HealthBarWidget; }
	static UMHGZAttributeBarWidget* Stamina(AMHGZHUD* HUD) { return HUD->StaminaBarWidget; }
	static UMHGZCrosshairWidget* Crosshair(AMHGZHUD* HUD) { return HUD->CrosshairWidget; }
	static UMHGZInsectGlaiveResourceWidget* Resource(AMHGZHUD* HUD)
	{
		return Cast<UMHGZInsectGlaiveResourceWidget>(HUD->WeaponResourceWidget);
	}
	static bool Bound(UMHGZAttributeBarWidget* W, UAbilitySystemComponent* ASC) { return W && W->BoundASC == ASC; }
	static float Ratio(UMHGZAttributeBarWidget* W) { return W ? W->DisplayRatio : -1; }
	static float GaugeWidth(UMHGZAttributeBarWidget* W) { return W ? W->GetGaugeWidth() : -1; }
	static bool Visible(UMHGZCrosshairWidget* W) { return W && W->bAimDisplayVisible; }
	static EMHGZAimDisplayMode Mode(UMHGZCrosshairWidget* W) { return W->CurrentAimDisplayMode; }
	static FIGResourceUISnapshot State(UMHGZInsectGlaiveResourceWidget* W) { return W->DisplayState; }
};

namespace
{
struct FHUDWidgetFixture
{
	FMHGZM3Harness H;
	AMHGZM3TestPlayerController* PC = nullptr;
	AMHGZHUD* HUD = nullptr;
	bool Setup(bool bPawnInitiallyPresent, bool bResourceClassPresent)
	{
		if (!H.Setup(false, AMHGZM3TestCharacter::StaticClass(), true)) { return false; }
		PC = H.World->SpawnActor<AMHGZM3TestPlayerController>();
		// The headless fixture has no game viewport. CreateLocalPlayer requires
		// one and emits an ensure; register a local player directly instead.
		ULocalPlayer* LP = NewObject<ULocalPlayer>(GEngine, GEngine->LocalPlayerClass);
		if (!PC || !LP) { return false; }
		H.TestGameInstance->AddLocalPlayer(LP, FPlatformUserId::CreateFromInternalId(0));
		PC->SetPlayer(LP);
		PC->PlayerState = H.PlayerState;
		PC->SetPawn(bPawnInitiallyPresent ? H.Character : nullptr);
		if (bResourceClassPresent && !SetResourceClass()) { return false; }
		UClass* HUDClass = LoadClass<AMHGZHUD>(nullptr, TEXT("/Game/Blueprints/HUD/BP_MHGZHUD.BP_MHGZHUD_C"));
		if (!HUDClass) { return false; }
		HUD = H.World->SpawnActorDeferred<AMHGZHUD>(HUDClass, FTransform::Identity, PC);
		if (!HUD) { return false; }
		HUD->PlayerOwner = PC;
		return FMHGZM6HUDTestAccess::ValidateAuthoredClasses(HUD);
	}
	bool SetResourceClass()
	{
		UWeaponRuntimeDefinition* Definition = H.Host->GetCurrentWeaponSnapshot().RuntimeDefinition.Get();
		Definition->ResourceWidgetClass = LoadClass<UMHGZWeaponResourceWidget>(nullptr,
			TEXT("/Game/UI/InsectGlaive/WBP_IG_ResourcePanel.WBP_IG_ResourcePanel_C"));
		return Definition->ResourceWidgetClass != nullptr;
	}
	void Start() { HUD->FinishSpawning(FTransform::Identity); }
	~FHUDWidgetFixture()
	{
		if (HUD) { HUD->Destroy(); }
		H.Teardown();
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZM6UIEarlyContext,
	"MHGZ.M6.UI.ActualHUD.EarlyContextAndReady", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZM6UIEarlyContext::RunTest(const FString& Parameters)
{
	FHUDWidgetFixture F;
	if (!F.Setup(true, true)) { AddError(TEXT("Real HUD fixture setup failed")); return false; }
	using Access = FMHGZM6HUDTestAccess;
	F.HUD->RefreshPawnContext();
	Access::Ready(F.HUD, F.H.Host->GetCurrentToken(), F.H.Resource);
	TestFalse(TEXT("Pre-widget context must not be consumed"), Access::ContextInitialized(F.HUD));
	TestEqual(TEXT("Pre-widget ready token must not be consumed"), Access::Generation(F.HUD), uint64(0));
	F.Start();
	UMHGZMainHUDWidget* Main = Access::Main(F.HUD);
	if (!Main || !Access::Health(F.HUD) || !Access::Stamina(F.HUD) || !Access::Crosshair(F.HUD))
	{
		AddError(TEXT("HUD did not instantiate actual WBP children")); return false;
	}
	// Force real Slate construction, including the bind-before-construct order.
	const TSharedRef<SWidget> SlateRoot = Main->TakeWidget();
	TestTrue(TEXT("Actual health WBP bound to ASC"), Access::Bound(Access::Health(F.HUD), F.H.ASC));
	TestTrue(TEXT("Initial health is not reset by NativeConstruct"), Access::Ratio(Access::Health(F.HUD)) > 0);
	TestTrue(TEXT("Actual stamina WBP bound to ASC"), Access::Bound(Access::Stamina(F.HUD), F.H.ASC));
	UMHGZInsectGlaiveResourceWidget* Panel = Access::Resource(F.HUD);
	if (!Panel) { AddError(TEXT("Ready resource WBP missing")); return false; }
	TestTrue(TEXT("Panel has current resource snapshot"), Access::State(Panel).bHasKinsect);
	F.H.Resource->ApplyExtract(M3::Tag(TEXT("WeaponResource.IG.Extract.White")));
	TestTrue(TEXT("Panel receives live extract update"), Access::State(Panel).bWhite);
	TestEqual(TEXT("health at max 200 retains the existing 582 body width"), Access::GaugeWidth(Access::Health(F.HUD)), 582.f);
	TestEqual(TEXT("stamina at max 200 retains the existing body width"), Access::GaugeWidth(Access::Stamina(F.HUD)), 582.f);
	F.H.ASC->SetNumericAttributeBase(UMHGZAttributeSet::GetMaxHealthAttribute(), 100);
	F.H.ASC->SetNumericAttributeBase(UMHGZAttributeSet::GetHealthAttribute(), 53);
	TestTrue(TEXT("Gauge receives live attribute update"), FMath::IsNearlyEqual(Access::Ratio(Access::Health(F.HUD)), 0.53f));
	TestEqual(TEXT("max 100 halves the body width"), Access::GaugeWidth(Access::Health(F.HUD)), 291.f);
	TestEqual(TEXT("layout follows the shorter body without scaling the emblem"), CastChecked<UCanvasPanelSlot>(Main->HealthBarSlot->Slot)->GetSize().X, 329.0);
	F.H.ASC->SetNumericAttributeBase(UMHGZAttributeSet::GetMaxHealthAttribute(), 200);
	TestTrue(TEXT("Gauge also receives max-attribute update"), FMath::IsNearlyEqual(Access::Ratio(Access::Health(F.HUD)), 0.265f));
	TestEqual(TEXT("current health changes fill, not total body length"), Access::GaugeWidth(Access::Health(F.HUD)), 582.f);
	F.H.ASC->SetNumericAttributeBase(UMHGZAttributeSet::GetMaxStaminaAttribute(), 300);
	TestEqual(TEXT("max 300 extends the stamina body to 1.5 times"), Access::GaugeWidth(Access::Stamina(F.HUD)), 873.f);
	TestEqual(TEXT("max above 200 expands the actual Canvas layout"), CastChecked<UCanvasPanelSlot>(Main->StaminaBarSlot->Slot)->GetSize().X, 911.0);
	F.H.ASC->SetNumericAttributeBase(UMHGZAttributeSet::GetStaminaAttribute(), 40);
	TestEqual(TEXT("spending stamina does not shorten the total bar"), Access::GaugeWidth(Access::Stamina(F.HUD)), 873.f);
	TestTrue(TEXT("spending stamina still updates the fill"), FMath::IsNearlyEqual(Access::Ratio(Access::Stamina(F.HUD)), 40.f / 300.f));
	F.HUD->RefreshPawnContext();
	Access::Ready(F.HUD, F.H.Host->GetCurrentToken(), F.H.Resource);
	TestTrue(TEXT("Idempotent refresh retains the panel"), Access::Resource(F.HUD) == Panel);
	TestEqual(TEXT("No duplicate resource panel"), Main->WeaponResourceSlot->GetChildrenCount(), 1);
	UMHGZCrosshairWidget* Crosshair = Access::Crosshair(F.HUD);
	TestFalse(TEXT("No LT/RT means no crosshair"), Access::Visible(Crosshair));
	const FGameplayTag RT = M3::Tag(TEXT("Combat.State.Aiming.Action"));
	const FGameplayTag LT = M3::Tag(TEXT("Combat.State.Aiming.Kinsect"));
	F.H.ASC->AddLooseGameplayTag(RT);
	TestTrue(TEXT("RT tag displays actual crosshair"), Access::Visible(Crosshair));
	TestEqual(TEXT("RT mode"), Access::Mode(Crosshair), EMHGZAimDisplayMode::Action);
	F.H.ASC->AddLooseGameplayTag(LT);
	TestEqual(TEXT("LT wins when both held"), Access::Mode(Crosshair), EMHGZAimDisplayMode::Kinsect);
	F.H.ASC->RemoveLooseGameplayTag(LT);
	TestEqual(TEXT("Release LT falls back to RT"), Access::Mode(Crosshair), EMHGZAimDisplayMode::Action);
	F.H.ASC->RemoveLooseGameplayTag(RT);
	TestFalse(TEXT("Release both hides crosshair"), Access::Visible(Crosshair));
	// A stale generation must not remove the current panel.
	FWeaponRuntimeToken Stale = F.H.Host->GetCurrentToken();
	++Stale.Generation;
	Access::Invalidate(F.HUD, Stale);
	TestTrue(TEXT("Stale invalidation rejected"), Access::Resource(F.HUD) == Panel);
	F.PC->SetPawn(nullptr);
	F.HUD->RefreshPawnContext();
	TestNull(TEXT("Unpossess removes resource panel"), Access::Resource(F.HUD));
	F.H.ASC->AddLooseGameplayTag(RT);
	TestFalse(TEXT("Unbound crosshair no longer receives old ASC events"), Access::Visible(Crosshair));
	F.H.ASC->RemoveLooseGameplayTag(RT);
	F.PC->SetPawn(F.H.Character);
	F.HUD->RefreshPawnContext();
	TestNotNull(TEXT("Rebind recreates resource panel"), Access::Resource(F.HUD));
	F.HUD->Destroy();
	TestNull(TEXT("EndPlay removes root widget"), Access::Main(F.HUD));
	TestFalse(TEXT("EndPlay clears context initialization"), Access::ContextInitialized(F.HUD));
	TestFalse(TEXT("EndPlay clears health ASC subscription"), Access::Bound(Access::Health(F.HUD), F.H.ASC));
	F.HUD = nullptr;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZM6UILateContext,
	"MHGZ.M6.UI.ActualHUD.LatePawnAndResourceClass", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZM6UILateContext::RunTest(const FString& Parameters)
{
	FHUDWidgetFixture F;
	if (!F.Setup(false, false)) { AddError(TEXT("Real HUD fixture setup failed")); return false; }
	using Access = FMHGZM6HUDTestAccess;
	F.Start();
	TestNotNull(TEXT("HUD created without a pawn"), Access::Main(F.HUD));
	F.PC->SetPawn(F.H.Character);
	F.HUD->RefreshPawnContext();
	TestNull(TEXT("Missing resource class defers panel"), Access::Resource(F.HUD));
	TestEqual(TEXT("Failed creation does not consume token"), Access::Generation(F.HUD), uint64(0));
	if (!F.SetResourceClass()) { AddError(TEXT("Resource WBP unavailable")); return false; }
	F.HUD->RefreshPawnContext();
	TestNotNull(TEXT("Same context and token can retry creation"), Access::Resource(F.HUD));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZM6UIRenderPreview,
	"MHGZ.M6.UI.ActualHUD.RenderPreview", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZM6UIRenderPreview::RunTest(const FString& Parameters)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("MHGZUIRenderPreview")))
	{
		AddInfo(TEXT("Opt-in GPU preview: run with -MHGZUIRenderPreview (without -NullRHI)"));
		return true;
	}
	FHUDWidgetFixture F;
	if (!F.Setup(true, true)) { AddError(TEXT("Real HUD fixture setup failed")); return false; }
	F.Start();
	using Access = FMHGZM6HUDTestAccess;
	UMHGZMainHUDWidget* Main = Access::Main(F.HUD);
	UMHGZCrosshairWidget* Crosshair = Access::Crosshair(F.HUD);
	if (!Main || !Crosshair || !Access::Resource(F.HUD)) { AddError(TEXT("Render tree incomplete")); return false; }
	const TSharedRef<SWidget> Root = SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FLinearColor(0.07f, 0.10f, 0.13f, 1))
		.Padding(0) [Main->TakeWidget()];
	FIGResourceUISnapshot State = F.H.Resource->GetUIState();
	State.bRed = State.bWhite = State.bOrange = true;
	State.KinsectPendingExtractColor = M3::Tag(TEXT("WeaponResource.IG.Extract.Red"));
	Access::Resource(F.HUD)->OnResourceUIUpdated(State);
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("M6Audit/E63HUDRepairPreview");
	IFileManager::Get().MakeDirectory(*Directory, true);
	FWidgetRenderer Renderer(true);
	const EMHGZAimDisplayMode Modes[] = {EMHGZAimDisplayMode::None, EMHGZAimDisplayMode::Kinsect,
		EMHGZAimDisplayMode::Kinsect, EMHGZAimDisplayMode::Action, EMHGZAimDisplayMode::Action};
	const TCHAR* Names[] = {TEXT("00-idle"), TEXT("01-lt-no-target"), TEXT("02-lt-target"), TEXT("03-rt-no-target"), TEXT("04-rt-target")};
	uint32 PreviousHash = 0;
	for (int32 I = 0; I < 5; ++I)
	{
		Crosshair->OnAimDisplayStateUpdated(Modes[I], (I == 2 || I == 4) ? F.H.Character : nullptr, {}, {});
		UTextureRenderTarget2D* Target = Renderer.DrawWidget(Root, FVector2D(1920, 1080));
		FlushRenderingCommands();
		if (!Target) { AddError(TEXT("GPU render target unavailable")); return false; }
		// Slate's gamma-correct renderer already produces display-space bytes.
		// Save those bytes directly, without a second image-export conversion.
		FReadSurfaceDataFlags Flags(RCM_MinMax);
		Flags.SetLinearToGamma(false);
		TArray<FColor> Pixels;
		if (!Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels, Flags))
		{
			AddError(TEXT("GPU readback failed")); return false;
		}
		const uint32 Hash = FCrc::MemCrc32(Pixels.GetData(), Pixels.Num() * sizeof(FColor));
		TestTrue(TEXT("Each aim state changes real rendered pixels"), I == 0 || Hash != PreviousHash);
		PreviousHash = Hash;
		const FColor Background = Pixels[500 * 1920 + 1000];
		int32 CenterPixels = 0;
		for (int32 Y = 490; Y < 590; ++Y)
		{
			for (int32 X = 910; X < 1010; ++X)
			{
				CenterPixels += Pixels[Y * 1920 + X] != Background ? 1 : 0;
			}
		}
		TestTrue(TEXT("Reticle only paints in aim states"), I == 0 ? CenterPixels == 0 : CenterPixels > 10);
		TArray64<uint8> PNG;
		FImageUtils::PNGCompressImageArray(1920, 1080, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), PNG);
		TUniquePtr<FArchive> Writer(IFileManager::Get().CreateFileWriter(*(Directory / (FString(Names[I]) + TEXT(".png")))));
		TestTrue(*FString::Printf(TEXT("Rendered %s"), Names[I]), Writer && !PNG.IsEmpty());
		if (Writer && !PNG.IsEmpty()) { Writer->Serialize(PNG.GetData(), PNG.Num()); }
	}
	// Verify actual pixels, not only snapshot fields, for TripleUp replacing single GEs.
	Crosshair->OnAimDisplayStateUpdated(EMHGZAimDisplayMode::None, nullptr, {}, {});
	const auto RenderResource = [&](const FIGResourceUISnapshot& S, const TCHAR* Name)
	{
		Access::Resource(F.HUD)->OnResourceUIUpdated(S);
		UTextureRenderTarget2D* Target = Renderer.DrawWidget(Root, FVector2D(1920,1080));
		FlushRenderingCommands();
		TArray<FColor> Pixels;
		FReadSurfaceDataFlags Flags(RCM_MinMax); Flags.SetLinearToGamma(false);
		if (!Target || !Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags))
		{ AddError(TEXT("Resource GPU readback failed")); return Pixels; }
		TArray64<uint8> PNG;
		FImageUtils::PNGCompressImageArray(1920,1080,TArrayView64<const FColor>(Pixels.GetData(),Pixels.Num()),PNG);
		TUniquePtr<FArchive> Writer(IFileManager::Get().CreateFileWriter(*(Directory/(FString(Name)+TEXT(".png")))));
		if (Writer && !PNG.IsEmpty()) { Writer->Serialize(PNG.GetData(),PNG.Num()); }
		return Pixels;
	};
	const TArray<FColor> SinglePixels=RenderResource(State,TEXT("05-single-lamps"));
	State.bTripleUp=true; State.bRed=State.bWhite=State.bOrange=false;
	const TArray<FColor> TriplePixels=RenderResource(State,TEXT("06-triple-lamps"));
	TestTrue(TEXT("TripleUp lights identical three colored icons without single effects"),
		!SinglePixels.IsEmpty() && SinglePixels==TriplePixels);
	State.bTripleUp=false;
	const TArray<FColor> DarkPixels=RenderResource(State,TEXT("07-expired-lamps"));
	TestTrue(TEXT("Triple expiry actually darkens rendered icons"),!TriplePixels.IsEmpty() && TriplePixels!=DarkPixels);
	uint32 PreviousCargoHash=0;
	const TCHAR* Colors[]={TEXT("WeaponResource.IG.Extract.Red"),TEXT("WeaponResource.IG.Extract.White"),TEXT("WeaponResource.IG.Extract.Orange")};
	for (int32 I=0; I<3; ++I)
	{
		State.KinsectPendingExtractColors.AddTag(M3::Tag(Colors[I]));
		const TArray<FColor> Pixels=RenderResource(State,*FString::Printf(TEXT("08-cargo-%d"),I+1));
		const uint32 Hash=FCrc::MemCrc32(Pixels.GetData(),Pixels.Num()*sizeof(FColor));
		TestTrue(TEXT("One/two/three cargo sectors change actual rendered pixels"),!Pixels.IsEmpty() && (I==0 || Hash!=PreviousCargoHash));
		PreviousCargoHash=Hash;
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
