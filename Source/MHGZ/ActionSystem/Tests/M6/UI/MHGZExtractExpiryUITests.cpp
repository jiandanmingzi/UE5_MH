// Copyright MHGZ Project. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "UI/MHGZInsectGlaiveResourceWidget.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZExtractExpiryCadenceTest,
	"MHGZ.Maintenance.UI.ExtractExpiry.ThresholdsAndCadence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZExtractExpiryCadenceTest::RunTest(const FString&)
{
	FIGResourceUISnapshot State;
	State.bRed = true;
	const auto Opacity = [&State](float Seconds)
	{
		State.RedRemainingSeconds = Seconds;
		return UMHGZInsectGlaiveResourceWidget::GetExtractWarningIntensity(State, 0);
	};
	TestEqual(TEXT("above 30s stays steady"), Opacity(30.01f), 1.f);
	TestEqual(TEXT("30s begins slow warning"), Opacity(30.f), 0.2f);
	TestEqual(TEXT("slow pulse lasts 0.2s"), Opacity(29.9f), 0.2f);
	TestEqual(TEXT("slow pulse returns to bright"), Opacity(29.5f), 1.f);
	TestEqual(TEXT("slow pulse repeats after 2s"), Opacity(27.9f), 0.2f);
	TestEqual(TEXT("above 10s is still slow"), Opacity(10.01f), 1.f);
	TestEqual(TEXT("10s begins fast warning"), Opacity(10.f), 0.2f);
	TestEqual(TEXT("fast pulse dims"), Opacity(9.9f), 0.2f);
	TestEqual(TEXT("fast pulse returns to bright"), Opacity(9.7f), 1.f);
	TestEqual(TEXT("fast pulse repeats after 0.5s"), Opacity(9.4f), 0.2f);

	// Count dim pulses in four seconds away from exact floating-point boundaries.
	for (const bool bUrgent : {false, true})
	{
		int32 Pulses = 0;
		bool bWasDim = false;
		for (int32 I = 0; I < 80; ++I)
		{
			const bool bDim = Opacity((bUrgent ? 9.975f : 29.975f) - I * 0.05f) < 1.f;
			if (bDim && !bWasDim) { ++Pulses; }
			bWasDim = bDim;
		}
		TestEqual(bUrgent ? TEXT("eight fast pulses in four seconds")
			: TEXT("two slow pulses in four seconds"), Pulses, bUrgent ? 8 : 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZExtractExpiryChannelsTest,
	"MHGZ.Maintenance.UI.ExtractExpiry.IndependentLampsAndTripleUp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZExtractExpiryChannelsTest::RunTest(const FString&)
{
	FIGResourceUISnapshot State;
	State.bRed = State.bWhite = State.bOrange = true;
	State.RedRemainingSeconds = 29.9f;
	State.WhiteRemainingSeconds = 9.7f;
	State.OrangeRemainingSeconds = 60.f;
	TestEqual(TEXT("red uses red countdown"),
		UMHGZInsectGlaiveResourceWidget::GetExtractWarningIntensity(State, 0), 0.2f);
	TestEqual(TEXT("white uses white countdown"),
		UMHGZInsectGlaiveResourceWidget::GetExtractWarningIntensity(State, 1), 1.f);
	TestEqual(TEXT("orange uses orange countdown"),
		UMHGZInsectGlaiveResourceWidget::GetExtractWarningIntensity(State, 2), 1.f);
	State.OrangeRemainingSeconds = 9.9f;
	TestEqual(TEXT("orange independently enters fast warning"),
		UMHGZInsectGlaiveResourceWidget::GetExtractWarningIntensity(State, 2), 0.2f);

	// TripleUp removes individual GEs: false single flags/zero timers are normal.
	State.bTripleUp = true;
	State.bRed = State.bWhite = State.bOrange = false;
	State.RedRemainingSeconds = State.WhiteRemainingSeconds = State.OrangeRemainingSeconds = 0.f;
	for (const float Seconds : {60.f, 29.9f, 29.5f, 9.9f, 9.7f})
	{
		State.TripleRemainingSeconds = Seconds;
		const float Expected = (Seconds == 29.9f || Seconds == 9.9f) ? 0.2f : 1.f;
		for (int32 I = 0; I < 3; ++I)
		{
			TestEqual(TEXT("all triple lamps share the triple countdown"),
				UMHGZInsectGlaiveResourceWidget::GetExtractWarningIntensity(State, I), Expected);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZExtractExpiryLifecycleTest,
	"MHGZ.Maintenance.UI.ExtractExpiry.RefreshExpiryAndInvalidState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZExtractExpiryLifecycleTest::RunTest(const FString&)
{
	FIGResourceUISnapshot State;
	const auto Opacity = [&State](int32 I = 0)
	{
		return UMHGZInsectGlaiveResourceWidget::GetExtractWarningIntensity(State, I);
	};
	TestEqual(TEXT("unbound/default state does not flash"), Opacity(), 1.f);
	State.RedRemainingSeconds = 9.9f;
	TestEqual(TEXT("inactive stale timer cannot flash"), Opacity(), 1.f);
	State.bRed = true;
	TestEqual(TEXT("active timer flashes"), Opacity(), 0.2f);
	TestEqual(TEXT("repeated snapshot does not reset phase"), Opacity(), 0.2f);
	TestEqual(TEXT("invalid negative index is safe"), Opacity(-1), 1.f);
	TestEqual(TEXT("invalid high index is safe"), Opacity(3), 1.f);
	State.RedRemainingSeconds = 60.f;
	TestEqual(TEXT("duration refresh clears warning immediately"), Opacity(), 1.f);
	for (const float Seconds : {0.f, -1.f, std::numeric_limits<float>::quiet_NaN(),
		std::numeric_limits<float>::infinity()})
	{
		State.RedRemainingSeconds = Seconds;
		TestEqual(TEXT("expired or invalid timer does not invent a warning"), Opacity(), 1.f);
	}
	State.bRed = false;
	State.RedRemainingSeconds = 9.9f;
	TestEqual(TEXT("consumed extract stops flashing"), Opacity(), 1.f);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZExtractExpiryPaleColorTest,
	"MHGZ.Maintenance.UI.ExtractExpiry.PaleColorPreservesOpacity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZExtractExpiryPaleColorTest::RunTest(const FString&)
{
	FIGResourceUISnapshot State;
	State.bTripleUp = true;
	State.TripleRemainingSeconds = 9.9f;
	const FLinearColor Colors[] = {FLinearColor(.8f, .085f, .055f, .7f),
		FLinearColor(.86f, .91f, .92f, .7f), FLinearColor(.98f, .36f, .07f, .7f)};
	for (int32 I = 0; I < 3; ++I)
	{
		const FLinearColor Muted = UMHGZInsectGlaiveResourceWidget::GetExtractWarningColor(State, I, Colors[I]);
		TestEqual(TEXT("warning never lowers opacity"), Muted.A, Colors[I].A);
		TestFalse(TEXT("all three colours visibly change"), Muted.Equals(Colors[I]));
		TestTrue(TEXT("muted lamp remains visible"), Muted.GetLuminance() > .1f);
		TestTrue(TEXT("reduced colour contrast, not disappearance"),
			FMath::Abs(Muted.R - Muted.B) < FMath::Abs(Colors[I].R - Colors[I].B));
	}
	State.TripleRemainingSeconds = 9.7f;
	TestTrue(TEXT("normal colour returns between pulses"),
		UMHGZInsectGlaiveResourceWidget::GetExtractWarningColor(State, 0, Colors[0]).Equals(Colors[0]));
	return true;
}
#endif
