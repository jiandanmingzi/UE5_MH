// Copyright MHGZ Project. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "InputSystem/MHGZInputComponent.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "InputModifiers.h"
#include "ActionSystem/MHGZComboCoordinatorAbility.h"
#include "ActionSystem/MHGZDodgeAbility.h"
#include "ActionSystem/MHGZAirDodgeAbility.h"
#include "ActionSystem/MHGZSheatheAbility.h"
#include "UI/MHGZInputHints.h"
#include "UI/MHGZMainHUDWidget.h"
#include "WeaponRuntime/MHGZWeaponInputProfile.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "RenderingThread.h"
#include "Widgets/Layout/SBorder.h"
#include "Styling/CoreStyle.h"
#include "../../Shared/MHGZM3TestHarness.h"

namespace
{
FString EvidenceDirectory() { return FPaths::ProjectSavedDir() / TEXT("Maintenance/DesktopInput20261111"); }
void SaveRows(const TArray<FMHGZInputHint>& Rows, const FString& Name)
{
	FString Text;
	for (const auto& Row : Rows) Text += Row.Keys + TEXT("\t") + Row.Action + TEXT("\n");
	FFileHelper::SaveStringToFile(Text, *(EvidenceDirectory() / (Name + TEXT(".tsv"))), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZDesktopInputMappingsTest, "MHGZ.Maintenance.DesktopInput.MappingsAndDevice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZDesktopInputMappingsTest::RunTest(const FString&)
{
	auto* Input = NewObject<UMHGZInputComponent>();
	Input->ObserveInputDevice(EKeys::W, IE_Pressed, 1); TestFalse(TEXT("keyboard is recent"), Input->IsUsingGamepad());
	Input->ObserveInputDevice(EKeys::Gamepad_LeftX, IE_Axis, .1f); TestFalse(TEXT("stick drift does not steal device"), Input->IsUsingGamepad());
	Input->ObserveInputDevice(EKeys::Gamepad_LeftX, IE_Axis, .8f); TestTrue(TEXT("active stick selects gamepad"), Input->IsUsingGamepad());
	Input->ObserveInputDevice(EKeys::W, IE_Released, 0); TestTrue(TEXT("old release does not steal device"), Input->IsUsingGamepad());
	Input->ObserveInputDevice(EKeys::MouseX, IE_Axis, 3); TestFalse(TEXT("mouse look selects desktop"), Input->IsUsingGamepad());
	Input->ObserveInputDevice(EKeys::Gamepad_FaceButton_Bottom, IE_Pressed, 1); TestTrue(TEXT("gamepad button selects gamepad"), Input->IsUsingGamepad());
	auto* IMC = LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/Input/Contexts/IMC_MHGZ_Demo.IMC_MHGZ_Demo"));
	auto* Move = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/MHGZ/IA_Move.IA_Move"));
	if (!TestNotNull(TEXT("resident mapping context"), IMC) || !TestNotNull(TEXT("movement action"), Move)) return false;
	TestEqual(TEXT("opposite movement keys cancel"), Move->AccumulationBehavior, EInputActionAccumulationBehavior::Cumulative);
	const TMap<FKey, FString> Buttons = {{EKeys::SpaceBar,TEXT("IA_A")}, {EKeys::LeftMouseButton,TEXT("IA_Y")},
		{EKeys::RightMouseButton,TEXT("IA_B")}, {EKeys::LeftControl,TEXT("IA_RT")}, {EKeys::RightControl,TEXT("IA_RT")},
		{EKeys::R,TEXT("IA_LT")}, {EKeys::LeftShift,TEXT("IA_RB")}, {EKeys::RightShift,TEXT("IA_RB")}};
	for (const auto& Pair : Buttons)
	{
		TestTrue(*FString::Printf(TEXT("%s maps to %s"), *Pair.Key.ToString(), *Pair.Value), IMC->GetMappings().ContainsByPredicate(
			[&](const auto& M) { return M.Key == Pair.Key && M.Action && M.Action->GetName() == Pair.Value; }));
	}
	for (const auto& Pair : TMap<FKey,FVector2D>{{EKeys::W,{0,1}}, {EKeys::S,{0,-1}}, {EKeys::A,{-1,0}}, {EKeys::D,{1,0}}})
	{
		const auto* Mapping = IMC->GetMappings().FindByPredicate([&](const auto& M) { return M.Key == Pair.Key && M.Action == Move; });
		if (!TestNotNull(TEXT("WASD mapping exists"), Mapping)) continue;
		FInputActionValue Value(FVector2D(1,0));
		for (const auto& Modifier : Mapping->Modifiers) Value = Modifier->ModifyRaw(nullptr, Value, .016f);
		TestTrue(*FString::Printf(TEXT("%s movement axis and sign"), *Pair.Key.ToString()), Value.Get<FVector2D>().Equals(Pair.Value));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZDesktopInputHintsTest, "MHGZ.Maintenance.DesktopInput.StateHints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZDesktopInputHintsTest::RunTest(const FString&)
{
	FMHGZM3Harness H;
	if (!H.Setup(false, AMHGZM3TestCharacter::StaticClass())) return false;
	auto* Combo = LoadObject<UMHGZWeaponComboData>(nullptr, TEXT("/Game/Weapons/InsectGlaive/Data/DA_IG_Combo.DA_IG_Combo"));
	auto* Profile = LoadObject<UWeaponInputProfile>(nullptr, TEXT("/Game/Weapons/InsectGlaive/Data/DA_IG_InputProfile.DA_IG_InputProfile"));
	auto* Coordinator = H.ASC->GetActiveComboCoordinator();
	if (!Coordinator)
	{
		const auto Handle = H.GiveAbility(UGA_WeaponComboCoordinator::StaticClass()); H.ASC->TryActivateAbility(Handle);
		Coordinator = H.ASC->GetActiveComboCoordinator();
	}
	if (!TestNotNull(TEXT("real Combo"), Combo) || !TestNotNull(TEXT("real Profile"), Profile) || !TestNotNull(TEXT("coordinator"), Coordinator)) { H.Teardown(); return false; }
	Coordinator->InjectComboData(Combo);
	H.GiveAbility(UMHGZDodgeAbility::StaticClass()); H.GiveAbility(UMHGZAirDodgeAbility::StaticClass()); H.GiveAbility(UMHGZSheatheAbility::StaticClass());
	const auto Check = [&](const TArray<FMHGZInputHint>& Rows, const TCHAR* Keys, const TCHAR* Result)
	{
		TestTrue(*FString::Printf(TEXT("%s -> %s"), Keys, Result), Rows.ContainsByPredicate(
			[&](const auto& R) { return R.Keys == Keys && R.Action == Result; }));
	};
	H.Host->SetSheathed(true);
	auto Rows = MHGZInputHints::Build(*H.ASC, *Profile, true);
	Check(Rows,TEXT("A"),TEXT("翻滚")); Check(Rows,TEXT("Y"),TEXT("拔刀"));
	Check(Rows,TEXT("前+Y"),TEXT("飞身跃入拔刀")); Check(Rows,TEXT("RT"),TEXT("放虫拔刀"));
	Check(Rows,TEXT("前+RB"),TEXT("疾跑"));
	SaveRows(Rows,TEXT("gamepad-sheathed")); SaveRows(MHGZInputHints::Build(*H.ASC,*Profile,false),TEXT("desktop-sheathed"));
	H.Host->SetSheathed(false);
	FGameplayTagContainer Before, After; H.ASC->GetOwnedGameplayTags(Before);
	Rows = MHGZInputHints::Build(*H.ASC,*Profile,true);
	H.ASC->GetOwnedGameplayTags(After); TestTrue(TEXT("preview never grants hypothetical aim tags"), Before == After);
	Check(Rows,TEXT("A"),TEXT("翻滚")); Check(Rows,TEXT("Y"),TEXT("上捞斩")); Check(Rows,TEXT("前+Y"),TEXT("突刺"));
	Check(Rows,TEXT("B"),TEXT("横扫")); Check(Rows,TEXT("前+B"),TEXT("飞身跃入斩")); Check(Rows,TEXT("前+Y+B"),TEXT("突进回旋斩"));
	Check(Rows,TEXT("RT+A"),TEXT("撑杆跳")); Check(Rows,TEXT("RT+Y+B"),TEXT("觉虫击"));
	Check(Rows,TEXT("LT+Y"),TEXT("操虫")); Check(Rows,TEXT("LT+B"),TEXT("收虫")); Check(Rows,TEXT("LT+Y+B"),TEXT("粉尘聚爆"));
	Check(Rows,TEXT("RT（短按松开）"),TEXT("虫印斩"));
	const int32 Send = Rows.IndexOfByPredicate([](const auto& R) { return R.Keys == TEXT("LT+Y"); });
	const int32 Recall = Rows.IndexOfByPredicate([](const auto& R) { return R.Keys == TEXT("LT+B"); });
	TestTrue(TEXT("modifier recipes also keep Y before B"), Send >= 0 && Recall > Send);
	for (int32 I=1; I<Rows.Num(); ++I) TestTrue(TEXT("A/Y/B/RT/LT/RB order"), Rows[I].SortOrder >= Rows[I-1].SortOrder);
	const auto Desktop = MHGZInputHints::Build(*H.ASC,*Profile,false);
	Check(Desktop,TEXT("Ctrl+空格"),TEXT("撑杆跳")); Check(Desktop,TEXT("R+鼠标左键+鼠标右键"),TEXT("粉尘聚爆"));
	SaveRows(Rows,TEXT("gamepad-unsheathed")); SaveRows(Desktop,TEXT("desktop-unsheathed"));
	H.Character->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	H.ASC->AddLooseGameplayTag(M3::Tag(TEXT("Combat.State.Aerial.Falling")));
	Rows = MHGZInputHints::Build(*H.ASC,*Profile,true);
	Check(Rows,TEXT("A"),TEXT("空回"));
	Check(Rows,TEXT("B"),TEXT("操虫斩")); Check(Rows,TEXT("RT（短按松开）"),TEXT("急袭突刺"));
	SaveRows(Rows,TEXT("gamepad-air"));
	H.ASC->AddLooseGameplayTag(M3::Tag(TEXT("Combat.State.Dead")));
	TestTrue(TEXT("dead state hides action results"), MHGZInputHints::Build(*H.ASC,*Profile,true).IsEmpty());
	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZDesktopInputRenderTest, "MHGZ.Maintenance.DesktopInput.RenderPreview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZDesktopInputRenderTest::RunTest(const FString&)
{
	if (!FParse::Param(FCommandLine::Get(),TEXT("MHGZDesktopInputRenderPreview"))) { AddInfo(TEXT("Opt-in GPU rendering")); return true; }
	UClass* Class = LoadClass<UMHGZMainHUDWidget>(nullptr,TEXT("/Game/UI/HUD/WBP_HUD.WBP_HUD_C"));
	if (!TestNotNull(TEXT("resident HUD"),Class)) return false;
	auto* Main=NewObject<UMHGZMainHUDWidget>(GetTransientPackage(),Class); if (!Main->Initialize()) return false;
	auto Root=SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FLinearColor(.07f,.10f,.13f,1)).Padding(0)[Main->TakeWidget()];
	FWidgetRenderer Renderer(true); const FString Out=EvidenceDirectory()/TEXT("Preview");
	IFileManager::Get().MakeDirectory(*Out,true);
	for (const auto* Scenario : {TEXT("gamepad-sheathed"),TEXT("desktop-sheathed"),TEXT("gamepad-unsheathed"),TEXT("desktop-unsheathed"),TEXT("gamepad-air")})
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text,*(EvidenceDirectory()/(FString(Scenario)+TEXT(".tsv"))))) { AddError(TEXT("Run StateHints first to export real Combo scenarios")); return false; }
		TArray<FString> Lines; Text.ParseIntoArrayLines(Lines); TArray<FMHGZInputHint> Rows;
		for (const auto& Line : Lines) { FString Keys,Action; if (Line.Split(TEXT("\t"),&Keys,&Action)) Rows.Add({Keys,Action,0}); }
		Main->SetInputHints(Rows);
		for (int32 W : {1280,1920})
		{
			const int32 H=W*9/16; auto* Target=Renderer.DrawWidget(Root,FVector2D(W,H)); FlushRenderingCommands();
			TArray<FColor> Pixels; FReadSurfaceDataFlags Flags(RCM_MinMax); Flags.SetLinearToGamma(false);
			if (!Target || !Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags)) return false;
			TestTrue(TEXT("action hint background is fully transparent"),Pixels[(H/2)*W+30]==Pixels[(H/2)*W+4]);
			TArray64<uint8> PNG; FImageUtils::PNGCompressImageArray(W,H,TArrayView64<const FColor>(Pixels.GetData(),Pixels.Num()),PNG);
			TUniquePtr<FArchive> Writer(IFileManager::Get().CreateFileWriter(*(Out/FString::Printf(TEXT("%s-%d.png"),Scenario,W))));
			TestTrue(TEXT("preview saved"),Writer && !PNG.IsEmpty()); if (Writer && !PNG.IsEmpty()) Writer->Serialize(PNG.GetData(),PNG.Num());
		}
	}
	return true;
}
#endif
