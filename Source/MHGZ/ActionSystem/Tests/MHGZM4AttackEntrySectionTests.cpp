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

FWeaponAbilityActivationContext MakeBlendContext(float BlendInTime)
{
	FWeaponAbilityActivationContext Context;
	Context.MontageBlendInTime = BlendInTime;
	return Context;
}

FWeaponAbilityActivationContext MakeCorrectionContext(float MaxCorrectionAngle)
{
	FWeaponAbilityActivationContext Context;
	Context.MaxCorrectionAngle = MaxCorrectionAngle;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM4AttackTransitionBlendIn,
	"MHGZ.M4.7.Attack.TransitionBlendIn.OverrideAndFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM4AttackTransitionBlendIn::RunTest(const FString& Parameters)
{
	UMHGZM4TestEntryAttackAbility* Attack =
		NewObject<UMHGZM4TestEntryAttackAbility>(GetTransientPackage());
	Attack->AttackMontage = MakeEntryMontage();
	Attack->AttackMontage->BlendIn.SetBlendTime(3.0f / 60.0f);

	TestEqual(TEXT("negative Combo value inherits the Montage default"),
		Attack->ResolveBlendInTimeForTest(MakeBlendContext(-1.0f)), 3.0f / 60.0f);
	TestEqual(TEXT("Combo edge may override Blend In to six 60fps frames"),
		Attack->ResolveBlendInTimeForTest(MakeBlendContext(6.0f / 60.0f)), 6.0f / 60.0f);
	TestEqual(TEXT("zero Blend In remains an explicit hard cut"),
		Attack->ResolveBlendInTimeForTest(MakeBlendContext(0.0f)), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM4AttackTransitionCorrectionAngle,
	"MHGZ.M4.7.Attack.TransitionCorrectionAngle.OverrideAndFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM4AttackTransitionCorrectionAngle::RunTest(const FString& Parameters)
{
	UMHGZM4TestEntryAttackAbility* Attack =
		NewObject<UMHGZM4TestEntryAttackAbility>(GetTransientPackage());
	Attack->MaxCorrectionAngle = 30.0f;

	TestEqual(TEXT("negative Combo value inherits the GA correction cap"),
		Attack->ResolveActivationMaxCorrectionAngleForTest(MakeCorrectionContext(-1.0f)),
		30.0f);
	TestEqual(TEXT("Combo edge may allow an exact 180 degree rear correction"),
		Attack->ResolveActivationMaxCorrectionAngleForTest(MakeCorrectionContext(180.0f)),
		180.0f);
	TestEqual(TEXT("zero Combo correction cap remains an explicit no-op"),
		Attack->ResolveActivationMaxCorrectionAngleForTest(MakeCorrectionContext(0.0f)),
		0.0f);
	return true;
}

#endif
