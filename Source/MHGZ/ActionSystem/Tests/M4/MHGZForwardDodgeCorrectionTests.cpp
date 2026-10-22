// Copyright MHGZ Project. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "../Shared/MHGZM3TestHarness.h"
#include "../Shared/MHGZM4TestTypes.h"
#include "../Shared/MHGZDodgeTestHarness.h"

namespace
{
template<typename T> T* Active(FMHGZM3Harness& H, FGameplayAbilitySpecHandle Handle)
{
	const auto* Spec = H.ASC->FindAbilitySpecFromHandle(Handle);
	if (!Spec) { return nullptr; }
	for (UGameplayAbility* Instance : Spec->GetAbilityInstances())
	{
		if (auto* Dodge = Cast<T>(Instance); Dodge && Dodge->IsActive()) { return Dodge; }
	}
	return nullptr;
}
FWeaponInputSnapshot Input(bool bSheathed, float Yaw, EDirectionalInput Direction)
{
	auto Result = M3::MakePosedInput(true, bSheathed);
	Result.ResolvedInputTag = M3::Tag(TEXT("Input.Dodge"));
	Result.SourceControlTag = Result.ResolvedInputTag;
	Result.Phase = EWeaponInputPhase::Started;
	Result.SequenceID = 123;
	Result.Direction = Direction;
	Result.WorldDirection = FRotator(0.f, Yaw, 0.f).Vector();
	Result.ActorForward = FVector::ForwardVector;
	Result.RawMoveInput = FVector2D(0.f, 1.f);
	return Result;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZForwardDodgeCorrectionRouting,
	"MHGZ.M4.Dodge.ForwardInstantCorrectionAndNinetyDegreeRouting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZForwardDodgeCorrectionRouting::RunTest(const FString&)
{
	TestEqual(TEXT("native forward correction defaults to 90 degrees"), GetDefault<UMHGZDodgeAbility>()->MaxCorrectionAngle, 90.f);
	const auto* ResidentClass = LoadClass<UMHGZDodgeAbility>(nullptr,
		TEXT("/Game/Blueprints/Ability/Core/GA_Dodge.GA_Dodge_C"));
	if (TestNotNull(TEXT("resident GA_Dodge class loads"), ResidentClass))
	{
		TestEqual(TEXT("resident GA inherits the 90-degree default"), ResidentClass->GetDefaultObject<UMHGZDodgeAbility>()->MaxCorrectionAngle, 90.f);
	}
	for (bool bSheathed : {true, false})
	{
		for (float Angle : {-120.f, -91.f, -90.f, -60.f, -30.f, 30.f, 60.f, 90.f, 91.f, 120.f})
		{
			FMHGZM3Harness H;
			if (!H.Setup()) { H.Teardown(); return false; }
			H.Host->SetSheathed(bSheathed);
			const auto Handle = H.GiveAbility(UMHGZM4TestDodgeAbility::StaticClass());
			const auto Direction = FMath::Abs(Angle) <= 45.f ? EDirectionalInput::Forward
				: Angle < 0.f ? EDirectionalInput::Left : EDirectionalInput::Right;
			const auto Snapshot = Input(bSheathed, Angle, Direction);
			H.TryActivateWithInput(Handle, Snapshot);
			auto* Dodge = Active<UMHGZM4TestDodgeAbility>(H, Handle);
			const bool bExpected = FMath::Abs(Angle) <= 90.f;
			TestEqual(FString::Printf(TEXT("pose %d angle %.0f starts without waiting for a turn"), bSheathed, Angle), Dodge != nullptr, bExpected);
			TestTrue(TEXT("correction is synchronous, no timer or locomotion frame"), FMath::IsNearlyEqual(
				H.Character->GetActorRotation().Yaw, bExpected ? Angle : 0.f, .001f));
			if (Dodge)
			{
				TestTrue(TEXT("gated side uses forward montage family"), Dodge->DoesActiveDodgeAllowMoveExit());
				TestEqual(TEXT("original snapshot direction stays frozen"), Dodge->GetWeaponActivationContext().Input.Direction, Direction);
				Dodge->InterruptForTest();
			}
			H.Teardown();
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZForwardDodgeCorrectionIsolation,
	"MHGZ.M4.Dodge.ForwardCorrectionPreservesDirectionsFailureAndNinetyCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZForwardDodgeCorrectionIsolation::RunTest(const FString&)
{
	for (auto Direction : {EDirectionalInput::Left, EDirectionalInput::Right, EDirectionalInput::Back})
	{
		FMHGZM3Harness H;
		if (!H.Setup()) { H.Teardown(); return false; }
		H.Host->SetSheathed(false);
		const auto Handle = H.GiveAbility(UMHGZM4UnrestrictedDodgeAbility::StaticClass());
		H.TryActivateWithInput(Handle, Input(false, 60.f, Direction));
		const auto* Dodge = Active<UMHGZM4UnrestrictedDodgeAbility>(H, Handle);
		TestTrue(TEXT("available directional roll keeps its own montage family"), Dodge && !Dodge->DoesActiveDodgeAllowMoveExit());
		TestEqual(TEXT("available sides/Back never receive forward correction"), H.Character->GetActorRotation().Yaw, 0.0);
		H.Teardown();
	}
	for (int32 Case = 0; Case < 4; ++Case)
	{
		FMHGZM3Harness H;
		if (!H.Setup()) { H.Teardown(); return false; }
		H.Host->SetSheathed(false);
		const auto Handle = H.GiveAbility(Case == 0 ? UMHGZM4FailingDodgeAbility::StaticClass() : UMHGZM4TestDodgeAbility::StaticClass());
		auto Snapshot = Input(false, 60.f, EDirectionalInput::Right);
		if (Case == 1) { Snapshot.Direction = EDirectionalInput::None; }
		if (Case == 2)
		{
			Snapshot = Input(false, 30.f, EDirectionalInput::Forward);
			H.Character->SetActorRotation(FRotator(0.f, -140.f, 0.f));
		}
		if (Case == 3) { TestNotNull(TEXT("closed attack cancel window source"), M4Dodge::StartCancelableAttack(H, false)); }
		H.TryActivateWithInput(Handle, Snapshot);
		TestTrue(TEXT("failed montage/neutral/closed attack gate do not rotate; delayed basis cannot exceed cap"),
			FMath::IsNearlyEqual(H.Character->GetActorRotation().Yaw, Case == 2 ? -50.f : 0.f, .001f));
		H.Teardown();
	}
	return true;
}
#endif
