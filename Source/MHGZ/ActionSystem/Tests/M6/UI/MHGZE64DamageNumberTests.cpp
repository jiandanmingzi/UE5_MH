// Copyright MHGZ Project. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "AbilitySystemGlobals.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "GameplayCueManager.h"
#include "GameplayCueSet.h"
#include "Engine/Level.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "GameplayCues/MHGZDamageNumberCue.h"
#include "MHGZHitFeedbackRouterComponent.h"
#include "Monster/MHGZTrainingDummy.h"
#include "UI/MHGZDamageNumberActor.h"
#include "UI/MHGZDamageNumberWidget.h"
#include "Slate/WidgetRenderer.h"
#include "Components/TextBlock.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "RenderUtils.h"
#include "Styling/CoreStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SBorder.h"
#include "../../Shared/MHGZM3TestHarness.h"

namespace
{
struct FDamageNumberFixture
{
	FMHGZM3Harness H;
	AMHGZTrainingDummy* Target = nullptr;
	bool Setup()
	{
		if (!H.Setup(false, AMHGZM3TestCharacter::StaticClass(), true)) { return false; }
		auto* PC = H.World->SpawnActor<AMHGZM3TestPlayerController>();
		auto* LP = NewObject<ULocalPlayer>(GEngine, GEngine->LocalPlayerClass);
		H.TestGameInstance->AddLocalPlayer(LP, FPlatformUserId::CreateFromInternalId(0));
		PC->SetPlayer(LP); PC->PlayerState = H.PlayerState; PC->SetPawn(H.Character);
		Target = H.World->SpawnActor<AMHGZTrainingDummy>();
		if (!Target) { return false; }
		Target->GetAbilitySystemComponent()->InitAbilityActorInfo(Target, Target);
		return LoadClass<UMHGZDamageNumberCue>(nullptr,
			TEXT("/Game/GameplayCues/GCN_Hit_DamageNumber.GCN_Hit_DamageNumber_C")) != nullptr;
	}
	TArray<AMHGZDamageNumberActor*> Numbers() const
	{
		TArray<AMHGZDamageNumberActor*> Result;
		for (TActorIterator<AMHGZDamageNumberActor> It(H.World); It; ++It)
		{
			if (IsValid(*It) && !It->IsActorBeingDestroyed()) { Result.Add(*It); }
		}
		return Result;
	}
	void Hit(float Damage, bool bCritical)
	{
		FMHGZHitFeedbackResult Result;
		Result.ActualDamage = Damage; Result.bCritical = bCritical;
		Result.SourceActor = H.Character;
		Result.Hit.ImpactPoint = FVector(100, 0, 100);
		Target->FindComponentByClass<UMHGZHitFeedbackRouterComponent>()->RouteHitFeedback(Result);
	}
	~FDamageNumberFixture() { H.Teardown(); }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZE64DamageNumberRoute,
	"MHGZ.M6.UI.DamageNumber.RealCueRouteAndLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZE64DamageNumberRoute::RunTest(const FString& Parameters)
{
	FDamageNumberFixture F;
	if (!TestTrue(TEXT("actual resident number assets load"), F.Setup())) { return false; }
	UGameplayCueManager* Manager = UAbilitySystemGlobals::Get().GetGameplayCueManager();
	Manager->InitializeRuntimeObjectLibrary();
	int32 NumberHandlers = 0;
	for (const FGameplayCueNotifyData& Data : Manager->GetRuntimeCueSet()->GameplayCueData)
	{
		if (Data.GameplayCueTag == M3::Tag(TEXT("GameplayCue.Hit.DamageNumber"))) { ++NumberHandlers; }
	}
	TestEqual(TEXT("configured path registers exactly one handler"), NumberHandlers, 1);
	const float HealthBefore = F.Target->GetAttributeSet()->GetHealth();
	F.Hit(37.6f, false);
	TArray<AMHGZDamageNumberActor*> Numbers = F.Numbers();
	if (!TestEqual(TEXT("real ASC/Cue execution creates one number"), Numbers.Num(), 1)) { return false; }
	UMHGZDamageNumberWidget* Widget = Numbers[0]->GetDamageNumberWidget();
	if (!TestNotNull(TEXT("resident WBP instantiated and bound"), Widget)) { return false; }
	TestEqual(TEXT("final damage passed without recalculation"), Widget->GetDisplayedDamage(), 37.6f);
	TestEqual(TEXT("integer text rounds only for display"), Widget->GetDisplayedText().ToString(), FString(TEXT("38")));
	TestFalse(TEXT("ordinary damage is not critical"), Widget->IsDisplayedCritical());
	F.Hit(5.0f, true);
	Numbers = F.Numbers();
	TestEqual(TEXT("second stage is a separate number"), Numbers.Num(), 2);
	if (Numbers.Num() == 2)
	{
		TestTrue(TEXT("simultaneous stages are offset, not obscured at the same point"),
			!Numbers[0]->GetActorLocation().Equals(Numbers[1]->GetActorLocation()));
		int32 CritNumbers = 0;
		for (auto* Number : Numbers) { CritNumbers += Number->GetDamageNumberWidget()->IsDisplayedCritical() ? 1 : 0; }
		TestEqual(TEXT("critical status comes from its settled result"), CritNumbers, 1);
	}
	F.Hit(0.0f, false); F.Hit(-1.0f, false);
	TestEqual(TEXT("nonpositive damage does not spawn numbers"), F.Numbers().Num(), 2);
	TestEqual(TEXT("visual feedback cannot damage the target"), F.Target->GetAttributeSet()->GetHealth(), HealthBefore);
	for (auto* Number : F.Numbers())
	{
		Number->DispatchBeginPlay();
		const FVector Before = Number->GetActorLocation();
		static_cast<AActor*>(Number)->Tick(0.1f);
		TestTrue(TEXT("number holds position while displaying"), Number->GetActorLocation().Equals(Before));
		TestEqual(TEXT("number stays opaque before fade"), Number->GetDamageNumberWidget()->GetRenderOpacity(), 1.0f);
		static_cast<AActor*>(Number)->Tick(0.65f);
		TestTrue(TEXT("hold phase ends without premature rise"), Number->GetActorLocation().Equals(Before));
		static_cast<AActor*>(Number)->Tick(0.05f);
		TestTrue(TEXT("fade-out starts rising only at the end"), FMath::IsNearlyEqual(Number->GetActorLocation().Z, Before.Z + 3.0f, 0.0001));
		TestTrue(TEXT("fade-out starts gradually"), FMath::IsNearlyEqual(Number->GetDamageNumberWidget()->GetRenderOpacity(), 0.8f, 0.0001f));
		static_cast<AActor*>(Number)->Tick(0.1f);
		TestTrue(TEXT("opacity keeps decreasing while rising"), FMath::IsNearlyEqual(Number->GetDamageNumberWidget()->GetRenderOpacity(), 0.4f, 0.0001f));
		static_cast<AActor*>(Number)->Tick(0.1f);
		TestTrue(TEXT("fade reaches zero opacity"), FMath::IsNearlyZero(Number->GetDamageNumberWidget()->GetRenderOpacity()));
		TestTrue(TEXT("only fade duration contributes displacement"), FMath::IsNearlyEqual(Number->GetActorLocation().Z, Before.Z + 15.0f));
	}
	++GFrameCounter; F.H.World->GetTimerManager().Tick(0.0f);
	++GFrameCounter; F.H.World->GetTimerManager().Tick(1.1f);
	TestEqual(TEXT("bounded lifetime destroys number actors"), F.Numbers().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZDamageNumberOwnedPlacement,
	"MHGZ.Maintenance.DamageNumber.OwnedPlacementMatchesLegacyWorldQuery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZDamageNumberOwnedPlacement::RunTest(const FString& Parameters)
{
	FDamageNumberFixture F;
	if (!TestTrue(TEXT("resident number assets load"), F.Setup())) { return false; }
	UClass* CueClass = LoadClass<UMHGZDamageNumberCue>(nullptr,
		TEXT("/Game/GameplayCues/GCN_Hit_DamageNumber.GCN_Hit_DamageNumber_C"));
	const UMHGZDamageNumberCue* Cue = CueClass->GetDefaultObject<UMHGZDamageNumberCue>();
	const FVector Impact(100.0, 0.0, 100.0);
	auto SpawnNumber = [](UWorld* World, AActor* Owner, const FVector& Location)
	{
		FActorSpawnParameters Spawn;
		Spawn.Owner = Owner;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<AMHGZDamageNumberActor>(
			AMHGZDamageNumberActor::StaticClass(), Location, FRotator::ZeroRotator, Spawn);
	};
	// 独立保留优化前的世界遍历作为位置真值；不复用新的候选来源或层级判断。
	auto LegacyCount = [&]()
	{
		int32 Count = 0;
		for (TActorIterator<AMHGZDamageNumberActor> It(F.H.World); It; ++It)
		{
			if (It->GetOwner() == F.Target && It->GetGameTimeSinceCreation() < 0.15f
				&& FVector::DistSquared(It->GetActorLocation(), Impact) < FMath::Square(100.0f))
			{
				++Count;
			}
		}
		return FMath::Min(Count, 4);
	};
	// 仅用于识别本次新增Actor；隐藏层级也要可见，位置真值仍用上面的原默认筛选。
	auto AllNumbers = [&]()
	{
		TArray<AMHGZDamageNumberActor*> Numbers;
		for (TActorIterator<AMHGZDamageNumberActor> It(F.H.World,
			AMHGZDamageNumberActor::StaticClass(), EActorIteratorFlags::SkipPendingKill); It; ++It)
		{
			Numbers.Add(*It);
		}
		return Numbers;
	};
	auto CheckPlacement = [&](const TCHAR* Context)
	{
		const FVector Expected = Impact + FVector(0, 0, LegacyCount() * 24.0f);
		const TArray<AMHGZDamageNumberActor*> Before = AllNumbers();
		FGameplayCueParameters Params;
		Params.RawMagnitude = 7.0f;
		Params.Location = Impact;
		if (!TestTrue(FString::Printf(TEXT("%s: real Cue creates number"), Context),
			Cue->OnExecute_Implementation(F.Target, Params))) { return false; }
		AMHGZDamageNumberActor* Created = nullptr;
		int32 CreatedCount = 0;
		for (AMHGZDamageNumberActor* Number : AllNumbers())
		{
			if (!Before.Contains(Number)) { Created = Number; ++CreatedCount; }
		}
		bool bMatches = TestEqual(FString::Printf(TEXT("%s: one number per Cue"), Context), CreatedCount, 1);
		if (Created)
		{
			bMatches &= TestTrue(FString::Printf(TEXT("%s: same position as legacy query"), Context),
				Created->GetActorLocation().Equals(Expected, 0.0001));
			Created->Destroy();
		}
		return bMatches;
	};

	AMHGZDamageNumberActor* Aged = SpawnNumber(F.H.World, F.Target, Impact);
	if (!TestNotNull(TEXT("aged owned number exists"), Aged)) { return false; }
	F.H.World->TimeSeconds = 0.15f;
	AActor* OtherOwner = F.H.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("independent owner exists"), OtherOwner)) { return false; }
	for (int32 Index = 0; Index < 64; ++Index)
	{
		if (!TestNotNull(TEXT("unrelated nearby number exists"),
			SpawnNumber(F.H.World, OtherOwner, Impact))) { return false; }
	}
	FActorSpawnParameters OwnedDecoration;
	OwnedDecoration.Owner = F.Target;
	if (!TestNotNull(TEXT("non-number owned actor exists"),
		F.H.World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, OwnedDecoration))) { return false; }
	AMHGZDamageNumberActor* Fresh = SpawnNumber(F.H.World, F.Target, Impact);
	AMHGZDamageNumberActor* Boundary = SpawnNumber(F.H.World, F.Target, Impact + FVector(100, 0, 0));
	AMHGZDamageNumberActor* Near = SpawnNumber(F.H.World, F.Target, Impact + FVector(99.9, 0, 0));
	if (!Fresh || !Boundary || !Near) { AddError(TEXT("owned number fixtures failed")); return false; }
	TestEqual(TEXT("strict age and distance boundaries reject aged and 100cm numbers"), LegacyCount(), 2);
	bool bMatches = CheckPlacement(TEXT("other targets, age/distance boundaries and mixed child types"));
	Near->SetActorLocation(Impact + FVector(300, 0, 0));
	TestEqual(TEXT("current position determines proximity"), LegacyCount(), 1);
	bMatches &= CheckPlacement(TEXT("number moved out of range"));
	Near->SetOwner(OtherOwner);
	Near->SetActorLocation(Impact);
	bMatches &= CheckPlacement(TEXT("number reassigned away"));
	Near->SetOwner(F.Target);
	TestEqual(TEXT("SetOwner transfers the existing number into the new owner query"), LegacyCount(), 2);
	bMatches &= CheckPlacement(TEXT("number reassigned back"));
	Near->SetOwner(nullptr);
	bMatches &= CheckPlacement(TEXT("owner cleared"));
	Fresh->Destroy();
	TestEqual(TEXT("destroyed number no longer contributes"), LegacyCount(), 0);
	bMatches &= CheckPlacement(TEXT("early destruction"));
	UWorld* ForeignWorld = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("independent world exists"), ForeignWorld)) { return false; }
	TestNotNull(TEXT("foreign-world number with the same owner exists"), SpawnNumber(ForeignWorld, F.Target, Impact));
	bMatches &= CheckPlacement(TEXT("foreign world is excluded"));
	ForeignWorld->DestroyWorld(false);

	for (int32 Index = 0; Index < 6; ++Index)
	{
		if (!TestNotNull(TEXT("burst owned number exists"),
			SpawnNumber(F.H.World, F.Target, Impact))) { return false; }
	}
	TestEqual(TEXT("burst reaches existing four-step offset cap"), LegacyCount(), 4);
	bMatches &= CheckPlacement(TEXT("burst offset saturation"));

	ULevel* Level = F.H.World->PersistentLevel;
	const bool bWasVisible = Level->bIsVisible;
	const bool bWasAssociating = Level->bIsAssociatingLevel;
	Level->bIsVisible = false;
	TestEqual(TEXT("legacy iterator excludes a hidden level"), LegacyCount(), 0);
	bMatches &= CheckPlacement(TEXT("hidden level"));
	Level->bIsAssociatingLevel = true;
	TestEqual(TEXT("legacy iterator admits an associating level"), LegacyCount(), 4);
	bMatches &= CheckPlacement(TEXT("associating level"));
	Level->bIsAssociatingLevel = bWasAssociating;
	Level->bIsVisible = bWasVisible;
	return bMatches;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZE64DamageNumberRender,
	"MHGZ.M6.UI.DamageNumber.ActualWidgetRender",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZE64DamageNumberRender::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized() || FParse::Param(FCommandLine::Get(), TEXT("NullRHI"))) { AddError(TEXT("Real number rendering needs RenderOffscreen, not NullRHI.")); return false; }
	FDamageNumberFixture F;
	if (!F.Setup()) { AddError(TEXT("Number render fixture failed")); return false; }
	UClass* Class = LoadClass<UMHGZDamageNumberWidget>(nullptr,
		TEXT("/Game/UI/Feedback/WBP_DamageNumber.WBP_DamageNumber_C"));
	auto* Widget = CreateWidget<UMHGZDamageNumberWidget>(F.H.World, Class);
	if (!Widget) { AddError(TEXT("Authored number widget missing")); return false; }
	const TSharedRef<SWidget> Root = SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
		.BorderBackgroundColor(FLinearColor(0.04f, 0.05f, 0.07f, 1))
		.Padding(0)[Widget->TakeWidget()];
	FWidgetRenderer Renderer(true);
	uint32 PreviousHash = 0;
	for (const bool bCritical : {false, true})
	{
		Widget->SetDamageDisplay(123.4f, bCritical);
		const UTextBlock* Text = Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("DamageText")));
		if (!TestNotNull(TEXT("actual bound damage text"), Text)) { return false; }
		TestTrue(TEXT("smaller font reaches the resident widget"), FMath::IsNearlyEqual(Text->GetFont().Size, bCritical ? 24.0f : 20.0f));
		UTextureRenderTarget2D* Target = Renderer.DrawWidget(Root, FVector2D(180, 72));
		FlushRenderingCommands();
		TArray<FColor> Pixels;
		FReadSurfaceDataFlags Flags(RCM_MinMax); Flags.SetLinearToGamma(false);
		if (!Target || !Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels, Flags)) { AddError(TEXT("Number GPU readback failed")); return false; }
		const uint32 Hash = FCrc::MemCrc32(Pixels.GetData(), Pixels.Num() * sizeof(FColor));
		TestTrue(TEXT("critical changes real rendered pixels"), !bCritical || Hash != PreviousHash);
		int32 BrightPixels = 0;
		for (const FColor& Pixel : Pixels) { BrightPixels += Pixel.R > 170 ? 1 : 0; }
		TestTrue(TEXT("damage text actually visible"), BrightPixels > 20);
		PreviousHash = Hash;
		const FString Dir = FPaths::ProjectSavedDir() / TEXT("M6Audit/E64DamageNumberPreview");
		IFileManager::Get().MakeDirectory(*Dir, true);
		TArray64<uint8> PNG;
		FImageUtils::PNGCompressImageArray(180, 72, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), PNG);
		TUniquePtr<FArchive> Writer(IFileManager::Get().CreateFileWriter(*(Dir / (bCritical ? TEXT("critical.png") : TEXT("normal.png")))));
		if (Writer) { Writer->Serialize(PNG.GetData(), PNG.Num()); }
	}
	return true;
}
#endif
