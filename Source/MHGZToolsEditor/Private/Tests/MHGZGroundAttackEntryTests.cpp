// Copyright MHGZ Project. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ActionSystem/MHGZAdvancingChargeAbility.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/MHGZMontageSegmentBlend.h"
#include "AnimationBlueprintLibrary.h"

namespace
{
UAnimMontage* LoadMontage(const TCHAR* Name)
{
	return LoadObject<UAnimMontage>(nullptr,
		*(FString(TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/")) + Name));
}
FAnimSegment const* EntryClip(UAnimMontage* Montage, FName Section, float& SourceTime)
{
	const int32 Index = Montage ? Montage->GetSectionIndex(Section) : INDEX_NONE;
	if (Index == INDEX_NONE || Montage->SlotAnimTracks.Num() != 1) { return nullptr; }
	const float Position = Montage->CompositeSections[Index].GetTime();
	const FAnimSegment* Clip = Montage->SlotAnimTracks[0].AnimTrack.GetSegmentAtTime(Position);
	if (Clip) { SourceTime = Clip->ConvertTrackPosToAnimPos(Position); }
	return Clip;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroundEntryAssetTest,
	"MHGZ.Maintenance.Animation.GroundEntries.Assets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGroundEntryAssetTest::RunTest(const FString& Parameters)
{
	UAnimMontage* Shang = LoadMontage(TEXT("AM_IG_ShangLao"));
	UAnimMontage* Jia = LoadMontage(TEXT("AM_IG_JiaSha"));
	UAnimMontage* Draw = LoadMontage(TEXT("AM_IG_BaDaoZhan"));
	if (!TestNotNull(TEXT("ShangLao"), Shang) || !TestNotNull(TEXT("JiaSha"), Jia)
		|| !TestNotNull(TEXT("BaDaoZhan"), Draw)) { return false; }
	float SourceTime;
	for (const auto& Spec : { TPair<FName, float>(TEXT("Core"), 0.0f),
		TPair<FName, float>(TEXT("Core_From_Bridge"), 7.0f / 60.0f) })
	{
		const FAnimSegment* Clip = EntryClip(Shang, Spec.Key, SourceTime);
		if (!TestNotNull(TEXT("Actual source-specific Core"), Clip)) { return false; }
		TestTrue(TEXT("Correct source frame"), FMath::IsNearlyEqual(SourceTime, Spec.Value, 0.00002f));
		TestEqual(TEXT("ShangLao effective speed"), Clip->GetValidPlayRate(), 2.0f);
	}
	const FAnimSegment* Bridge = EntryClip(Shang, TEXT("Entry_From_FeiShenYueRu"), SourceTime);
	if (!TestNotNull(TEXT("308 bridge"), Bridge)) { return false; }
	TestEqual(TEXT("308 enabled copy instead of hold"), Bridge->GetAnimReference()->GetName(),
		FString(TEXT("AS_IG_FeiShenYueRuShangLao_RM")));
	TestEqual(TEXT("308 effective speed"), Bridge->GetValidPlayRate(), 2.0f);
	Bridge = EntryClip(Shang, TEXT("Entry_From_TuJinHuiXuanZhan_Back"), SourceTime);
	TestTrue(TEXT("Back source keeps 305"), Bridge && Bridge->GetAnimReference()->GetName() == TEXT("AS_Unsh_R_HengSaoShangLao"));
	TestEqual(TEXT("Back route enters frame-zero Core"),
		Shang->CompositeSections[Shang->GetSectionIndex(TEXT("Entry_From_TuJinHuiXuanZhan_Back"))].NextSectionName, FName(TEXT("Core")));
	TestEqual(TEXT("307 and synthetic EntryHold no longer in JiaSha"), Jia->SlotAnimTracks[0].AnimTrack.AnimSegments.Num(), 2);
	const FAnimSegment* Clip = EntryClip(Jia, TEXT("Entry_From_TuCi"), SourceTime);
	TestTrue(TEXT("ShangLao/JiaSha route starts119 at frame zero"), Clip && FMath::IsNearlyZero(SourceTime));
	TestTrue(TEXT("Draw keeps300 full speed"), Draw->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference()->GetName() == TEXT("AS_Unsh_BaDaoFeiShenYueRu"));
	TestTrue(TEXT("Draw102 entry at67 source frames"), FMath::IsNearlyEqual(
		Draw->SlotAnimTracks[0].AnimTrack.AnimSegments[1].AnimStartTime, 67.0f/60.0f, 0.00002f));
	for (UAnimMontage* Configured : { Shang, Draw })
	{
		const auto* Blend = Configured->FindMetaDataByClass<UMHGZMontageSegmentBlendSettings>();
		TestTrue(TEXT("Internal mix metadata is persisted at0.05s"), Blend && Blend->bEnabled
			&& FMath::IsNearlyEqual(Blend->Duration, 0.05f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroundChargeEntryTest,
	"MHGZ.Maintenance.Animation.GroundEntries.ChargeCompatibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGroundChargeEntryTest::RunTest(const FString& Parameters)
{
	UMHGZAdvancingChargeAbility* Ability = NewObject<UMHGZAdvancingChargeAbility>();
	TestEqual(TEXT("Default release behavior stays Core"), Ability->ResolveUnchargedExitSection(TEXT("Entry")), FName(TEXT("Core")));
	Ability->UnchargedExitSectionByEntry.Add(TEXT("AlternateEntry"), TEXT("NormalContinuation"));
	TestEqual(TEXT("Optional continuation remains supported independently of this asset"),
		Ability->ResolveUnchargedExitSection(TEXT("AlternateEntry")), FName(TEXT("NormalContinuation")));
	TestEqual(TEXT("Other entries retain original release behavior"), Ability->ResolveUnchargedExitSection(TEXT("Entry")), FName(TEXT("Core")));
	UAnimMontage* Montage = LoadMontage(TEXT("AM_IG_TuJinHuiXuan"));
	if (!TestNotNull(TEXT("TuJinHuiXuan"), Montage)) { return false; }
	const auto& Clips = Montage->SlotAnimTracks[0].AnimTrack.AnimSegments;
	if (!TestEqual(TEXT("Shared front Entry and no appended idle clips"), Clips.Num(), 4)) { return false; }
	TestTrue(TEXT("Existing charge source55..67 and two-second stretch preserved"),
		FMath::IsNearlyEqual(Clips[2].AnimStartTime, 55.0f/60.0f, 0.00002f)
		&& FMath::IsNearlyEqual(Clips[2].AnimEndTime, 67.0f/60.0f, 0.00002f)
		&& FMath::IsNearlyEqual(Clips[2].GetLength(), 2.0f, 0.00002f));
	TestTrue(TEXT("Front Entry uses original306 from frame zero"),
		Clips[0].GetAnimReference()->GetName() == TEXT("AS_Unsh_IdleTuJinHuiXuan")
		&& FMath::IsNearlyZero(Clips[0].AnimStartTime));
	TestEqual(TEXT("Original Entry is2x"), Clips[0].GetValidPlayRate(), 2.0f);
	TestEqual(TEXT("No obsolete Idle section"), Montage->GetSectionIndex(TEXT("Entry_From_Idle")), INDEX_NONE);
	TestEqual(TEXT("No obsolete normal-release section"), Montage->GetSectionIndex(TEXT("Release_From_Idle")), INDEX_NONE);
	UClass* SavedClass = LoadClass<UMHGZAdvancingChargeAbility>(nullptr,
		TEXT("/Game/Blueprints/Ability/InsectGlaive/GA_IG_TuJinHuiXuanZhan.GA_IG_TuJinHuiXuanZhan_C"));
	if (!TestNotNull(TEXT("Saved TuJin GA class"), SavedClass)) { return false; }
	const auto* SavedAbility = SavedClass->GetDefaultObject<UMHGZAdvancingChargeAbility>();
	TestEqual(TEXT("Idle uses default shared Entry"), SavedAbility->DefaultEntrySection, FName(TEXT("Entry")));
	TestFalse(TEXT("No obsolete Idle transition override"), SavedAbility->EntrySectionByTransitionID.Contains(TEXT("ForwardYB_TuJinHuiXuanZhan")));
	TestFalse(TEXT("No obsolete Idle release override"), SavedAbility->UnchargedExitSectionByEntry.Contains(TEXT("Entry_From_Idle")));
	FAnimMontageInstance Instance;
	Instance.Initialize(Montage);
	const int32 Entry = Montage->GetSectionIndex(TEXT("Entry"));
	const int32 Charge = Montage->GetSectionIndex(TEXT("Charge"));
	const int32 Core = Montage->GetSectionIndex(TEXT("Core"));
	TestEqual(TEXT("Held shared entry naturally reaches Charge"), Instance.GetNextSectionID(Entry), Charge);
	Instance.SetNextSectionName(TEXT("Entry"), TEXT("Core"));
	TestEqual(TEXT("Released shared entry follows existing Core route"), Instance.GetNextSectionID(Entry), Core);
	TestEqual(TEXT("Maximum charge still naturally reaches Core"), Instance.GetNextSectionID(Charge), Core);
	TestEqual(TEXT("Core exits at the Montage end"), Instance.GetNextSectionID(Core), INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroundRootCopyTest,
	"MHGZ.Maintenance.Animation.GroundEntries.RootCopy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGroundRootCopyTest::RunTest(const FString& Parameters)
{
	UAnimSequence* Source = LoadObject<UAnimSequence>(nullptr,
		TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_FeiShenYueRuShangLao"));
	UAnimSequence* Copy = LoadObject<UAnimSequence>(nullptr,
		TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Generated/AS_IG_FeiShenYueRuShangLao_RM"));
	if (!TestNotNull(TEXT("Original308"), Source) || !TestNotNull(TEXT("Persistent308 copy"), Copy)) { return false; }
	TestFalse(TEXT("Original root flag remains unchanged"), Source->bEnableRootMotion);
	TestTrue(TEXT("Copy supports root motion"), Copy->bEnableRootMotion && !Copy->bForceRootLock);
	TestEqual(TEXT("Length is preserved"), Copy->GetPlayLength(), Source->GetPlayLength());
	TestEqual(TEXT("RateScale is preserved"), Copy->RateScale, Source->RateScale);
	TArray<FName> Names;
	Source->GetDataModel()->GetBoneTrackNames(Names);
	for (float Time : { 0.0f, Source->GetPlayLength() * 0.5f, Source->GetPlayLength() })
	{
		TArray<FTransform> Before, After;
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		UAnimationBlueprintLibrary::GetBonePosesForTime(Source, Names, Time, false, Before);
		UAnimationBlueprintLibrary::GetBonePosesForTime(Copy, Names, Time, false, After);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
		if (!TestEqual(TEXT("All bone tracks preserved"), Before.Num(), After.Num())) { return false; }
		for (int32 Index = 0; Index < Before.Num(); ++Index)
		{ TestTrue(TEXT("No root/bone reorientation or artificial pose generation"), Before[Index].Equals(After[Index], 0.00002f)); }
	}
	return true;
}
#endif
