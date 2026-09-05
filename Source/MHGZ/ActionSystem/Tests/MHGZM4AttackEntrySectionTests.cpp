// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Animation/AnimMontage.h"
#include "MHGZM4TestTypes.h"

namespace
{
UAnimMontage* MakeEntryMontage()
{
	UAnimMontage* Montage = NewObject<UAnimMontage>(GetTransientPackage());
	for (const FName SectionName : {
		FName(TEXT("Entry_Transition")),
		FName(TEXT("Entry_Source")),
		FName(TEXT("Entry_Default")) })
	{
		FCompositeSection& Section = Montage->CompositeSections.AddDefaulted_GetRef();
		Section.SectionName = SectionName;
	}
	return Montage;
}

FWeaponAbilityActivationContext MakeEntryContext(FName TransitionID, FName SourceState)
{
	FWeaponAbilityActivationContext Context;
	Context.TransitionID = TransitionID;
	Context.SourceState = SourceState;
	return Context;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM4AttackEntrySectionPrecedence,
	"MHGZ.M4.6.Attack.EntrySection.Precedence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM4AttackEntrySectionPrecedence::RunTest(const FString& Parameters)
{
	UMHGZM4TestEntryAttackAbility* Attack =
		NewObject<UMHGZM4TestEntryAttackAbility>(GetTransientPackage());
	Attack->AttackMontage = MakeEntryMontage();
	Attack->DefaultEntrySection = FName(TEXT("Entry_Default"));
	Attack->EntrySectionByTransitionID.Add(
		FName(TEXT("IG.Slash1.To.Slash2")), FName(TEXT("Entry_Transition")));
	Attack->EntrySectionBySourceState.Add(
		FName(TEXT("Slash1")), FName(TEXT("Entry_Source")));

	auto SelectAndCheck = [this, Attack](FName TransitionID, FName SourceState,
		FName ExpectedSection, const TCHAR* CaseName)
	{
		FName SelectedSection;
		TestTrue(FString::Printf(TEXT("%s has a valid entry"), CaseName),
			Attack->SelectEntrySectionForTest(
				MakeEntryContext(TransitionID, SourceState), SelectedSection));
		TestEqual(FString::Printf(TEXT("%s selects the expected Section"), CaseName),
			SelectedSection, ExpectedSection);
	};

	SelectAndCheck(FName(TEXT("IG.Slash1.To.Slash2")), FName(TEXT("Slash1")),
		FName(TEXT("Entry_Transition")), TEXT("TransitionID mapping"));
	SelectAndCheck(FName(TEXT("UnknownTransition")), FName(TEXT("Slash1")),
		FName(TEXT("Entry_Source")), TEXT("SourceState fallback"));
	SelectAndCheck(FName(TEXT("UnknownTransition")), FName(TEXT("UnknownState")),
		FName(TEXT("Entry_Default")), TEXT("Default fallback"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM4AttackEntrySectionFallbackAndReject,
	"MHGZ.M4.6.Attack.EntrySection.FallbackAndReject",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM4AttackEntrySectionFallbackAndReject::RunTest(const FString& Parameters)
{
	UMHGZM4TestEntryAttackAbility* Attack =
		NewObject<UMHGZM4TestEntryAttackAbility>(GetTransientPackage());
	Attack->AttackMontage = MakeEntryMontage();

	FName SelectedSection;
	TestTrue(TEXT("no entry mapping is a valid Montage-beginning fallback"),
		Attack->SelectEntrySectionForTest(
			MakeEntryContext(FName(TEXT("UnknownTransition")), FName(TEXT("UnknownState"))),
			SelectedSection));
	TestEqual(TEXT("no entry mapping passes None to the Montage task"),
		SelectedSection, NAME_None);

	Attack->EntrySectionByTransitionID.Add(
		FName(TEXT("IG.Slash1.To.Slash2")), FName(TEXT("MissingSection")));
	SelectedSection = NAME_None;
	TestFalse(TEXT("invalid mapped Section is rejected before Montage playback"),
		Attack->SelectEntrySectionForTest(
			MakeEntryContext(FName(TEXT("IG.Slash1.To.Slash2")), FName(TEXT("Slash1"))),
			SelectedSection));
	return true;
}

#endif
