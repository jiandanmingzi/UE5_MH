// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "MHGZM3TestHarness.h"
#include "MHGZM4TestTypes.h"

namespace
{
UMHGZM4TestAttackAbility* GetActiveAttack(UMHGZAbilitySystemComponent& ASC,
	const FGameplayAbilitySpecHandle& Handle)
{
	FGameplayAbilitySpec* Spec = ASC.FindAbilitySpecFromHandle(Handle);
	if (!Spec)
	{
		return nullptr;
	}
	for (UGameplayAbility* Ability : Spec->GetAbilityInstances())
	{
		if (UMHGZM4TestAttackAbility* Attack = Cast<UMHGZM4TestAttackAbility>(Ability);
			Attack && Attack->IsActive())
		{
			return Attack;
		}
	}
	return nullptr;
}

FWeaponInputSnapshot MakeDirectionInput(float DirectionYaw)
{
	FWeaponInputSnapshot Input = M3::MakePosedInput(/*bGrounded=*/true,
		/*bSheathed=*/false);
	Input.WorldDirection = FRotator(0.f, DirectionYaw, 0.f).Vector();
	return Input;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM4AttackDirectionCorrection,
	"MHGZ.M4.Attack.DirectionCorrection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM4AttackDirectionCorrection::RunTest(const FString& Parameters)
{
	FMHGZM3Harness H;
	if (!H.Setup())
	{
		AddError(TEXT("M4 direction-correction harness setup failed"));
		H.Teardown();
		return false;
	}

	const FGameplayAbilitySpecHandle Handle =
		H.GiveAbility(UMHGZM4TestAttackAbility::StaticClass());
	TestTrue(TEXT("test attack granted"), Handle.IsValid());

	auto ActivateAndCorrect = [this, &H, &Handle](const FWeaponInputSnapshot& Input,
		float MaxCorrectionAngle)
	{
		if (!TestTrue(TEXT("test attack activation succeeds"),
			H.TryActivateWithInput(Handle, Input)))
		{
			return;
		}
		UMHGZM4TestAttackAbility* Attack = GetActiveAttack(*H.ASC, Handle);
		TestNotNull(TEXT("active test attack exists"), Attack);
		if (Attack)
		{
			Attack->MaxCorrectionAngle = MaxCorrectionAngle;
			Attack->ApplyDirectionCorrectionForTest();
			Attack->RequestEndAction(EWeaponActionEndReason::Normal);
		}
	};

	H.Character->SetActorRotation(FRotator::ZeroRotator);
	ActivateAndCorrect(MakeDirectionInput(20.f), 30.f);
	TestTrue(TEXT("frozen direction within threshold snaps actor yaw"),
		FMath::IsNearlyEqual(H.Character->GetActorRotation().Yaw, 20.f, 0.1f));

	H.Character->SetActorRotation(FRotator::ZeroRotator);
	ActivateAndCorrect(MakeDirectionInput(50.f), 30.f);
	TestTrue(TEXT("direction outside threshold leaves actor yaw unchanged"),
		FMath::IsNearlyZero(H.Character->GetActorRotation().Yaw, 0.1f));

	H.Character->SetActorRotation(FRotator::ZeroRotator);
	FWeaponInputSnapshot NoDirection = MakeDirectionInput(0.f);
	NoDirection.WorldDirection = FVector::ZeroVector;
	ActivateAndCorrect(NoDirection, 30.f);
	TestTrue(TEXT("missing frozen direction leaves actor yaw unchanged"),
		FMath::IsNearlyZero(H.Character->GetActorRotation().Yaw, 0.1f));

	H.Character->SetActorRotation(FRotator::ZeroRotator);
	ActivateAndCorrect(MakeDirectionInput(20.f), 0.f);
	TestTrue(TEXT("zero correction angle leaves actor yaw unchanged"),
		FMath::IsNearlyZero(H.Character->GetActorRotation().Yaw, 0.1f));

	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM4AttackInActionDirectionCorrection,
	"MHGZ.M4.Attack.InActionDirectionCorrection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM4AttackInActionDirectionCorrection::RunTest(const FString& Parameters)
{
	FMHGZM3Harness H;
	if (!H.Setup())
	{
		AddError(TEXT("M4 in-action direction-correction harness setup failed"));
		H.Teardown();
		return false;
	}

	const FGameplayAbilitySpecHandle Handle =
		H.GiveAbility(UMHGZM4TestAttackAbility::StaticClass());
	TestTrue(TEXT("in-action test attack granted"), Handle.IsValid());
	TestTrue(TEXT("in-action test attack activation succeeds"),
		H.TryActivateWithInput(Handle, MakeDirectionInput(0.f)));

	UMHGZM4TestAttackAbility* Attack = GetActiveAttack(*H.ASC, Handle);
	TestNotNull(TEXT("active in-action test attack exists"), Attack);
	if (Attack)
	{
		const FWeaponActionToken ExactToken = Attack->GetActionToken();
		Attack->MaxCorrectionAngle = 30.f;

		H.Character->SetActorRotation(FRotator::ZeroRotator);
		Attack->SetInActionDirectionForTest(FRotator(0.f, 20.f, 0.f).Vector());
		TestTrue(TEXT("exact active token applies the GA fallback threshold"),
			Attack->ApplyInActionDirectionCorrectionForTest(ExactToken));
		TestTrue(TEXT("realtime direction within threshold snaps actor yaw"),
			FMath::IsNearlyEqual(H.Character->GetActorRotation().Yaw, 20.f, 0.1f));

		H.Character->SetActorRotation(FRotator::ZeroRotator);
		FWeaponActionToken WrongToken = ExactToken;
		++WrongToken.ActivationSequenceID;
		TestFalse(TEXT("wrong action token cannot apply direction correction"),
			Attack->ApplyInActionDirectionCorrectionForTest(WrongToken));
		TestTrue(TEXT("wrong action token leaves actor yaw unchanged"),
			FMath::IsNearlyZero(H.Character->GetActorRotation().Yaw, 0.1f));

		H.Character->SetActorRotation(FRotator::ZeroRotator);
		Attack->SetInActionDirectionForTest(FRotator(0.f, 20.f, 0.f).Vector());
		TestFalse(TEXT("zero per-notify override is an intentional no-op"),
			Attack->ApplyInActionDirectionCorrectionForTest(ExactToken, 0.f));
		TestTrue(TEXT("zero override leaves actor yaw unchanged"),
			FMath::IsNearlyZero(H.Character->GetActorRotation().Yaw, 0.1f));

		H.Character->SetActorRotation(FRotator::ZeroRotator);
		Attack->SetInActionDirectionForTest(FRotator(0.f, 50.f, 0.f).Vector());
		TestFalse(TEXT("per-notify threshold rejects an excessive correction"),
			Attack->ApplyInActionDirectionCorrectionForTest(ExactToken, 30.f));
		TestTrue(TEXT("excessive correction leaves actor yaw unchanged"),
			FMath::IsNearlyZero(H.Character->GetActorRotation().Yaw, 0.1f));

		H.Character->SetActorRotation(FRotator::ZeroRotator);
		Attack->SetInActionDirectionForTest(FVector::ZeroVector);
		TestFalse(TEXT("missing realtime direction is a no-op"),
			Attack->ApplyInActionDirectionCorrectionForTest(ExactToken));

		Attack->RequestEndAction(EWeaponActionEndReason::Normal);
		TestFalse(TEXT("ended action token cannot apply direction correction"),
			Attack->ApplyInActionDirectionCorrectionForTest(ExactToken));
	}

	H.Teardown();
	return true;
}

#endif
