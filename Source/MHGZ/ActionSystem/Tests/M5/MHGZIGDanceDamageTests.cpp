#include "ActionSystem/Tests/Shared/MHGZIGActionFixtures.h"
// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "ActionSystem/MHGZDescendingThrustAbility.h"
#include "ActionSystem/MHGZGameplayEffectContext.h"
#include "ActionSystem/MHGZKinsectPiercerAbility.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "AttributeSystem/Res_InsectGlaive.h"
#include "InsectGlaive/InsectGlaiveCombatConfig.h"
#include "KinsectSlash/MHGZM5KinsectSlashDamageTestTypes.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZIGDanceDamageTest,
	"MHGZ.Maintenance.IGDanceDamage.ActionMultipliersAndSnapshots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZIGDanceDamageTest::RunTest(const FString& Parameters)
{
	const float SlashExpected[] = { 1.0f, 1.1f, 1.2f, 1.3f };
	const float ThrustExpected[] = { 1.0f, 1.1f, 1.3f, 1.5f };
	const float PiercerExpected[] = { 1.0f, 1.3f, 1.5f, 2.0f };
	const FGameplayTag MotionTag = FGameplayTag::RequestGameplayTag(TEXT("Damage.MotionValue"));
	auto MotionValue = [&MotionTag](const FGameplayEffectSpecHandle& Spec)
	{
		return Spec.IsValid() ? Spec.Data->GetSetByCallerMagnitude(MotionTag, false, -1.0f) : -1.0f;
	};

	for (int32 Stacks = 0; Stacks <= 3; ++Stacks)
	{
		FMHGZM5KinsectSlashDamageTestHarness H;
		if (!TestTrue(TEXT("equipped production resource/damage harness builds"), H.Build()))
		{
			H.Teardown();
			return false;
		}
		TestEqual(TEXT("native maximum is three stacks"), H.CombatConfig->MaxDanceStacks, 3);
		for (int32 Index = 0; Index < Stacks; ++Index)
		{
			H.Resource->AddDanceStack(EIGDanceSource::KinsectSlash);
		}
		TestEqual(TEXT("real resource accumulated the requested stacks"), H.Resource->GetDanceStacks(), Stacks);
		TestEqual(TEXT("unspecified action cannot inherit a damage bonus"), H.Resource->GetDanceDamageMultiplier(), 1.0f);
		TestEqual(TEXT("slash action selects its own array"),
			MHGZ::IGActionConfig::DanceDamageMultiplier(GetDefault<UMHGZKinsectSlashAbility>()->KinsectSlashDanceDamageMultipliers, Stacks, H.CombatConfig->MaxDanceStacks), SlashExpected[Stacks]);
		TestEqual(TEXT("thrust action selects its own array"),
			MHGZ::IGActionConfig::DanceDamageMultiplier(GetDefault<UMHGZDescendingThrustAbility>()->DescendingThrustDanceDamageMultipliers, Stacks, H.CombatConfig->MaxDanceStacks), ThrustExpected[Stacks]);
		TestEqual(TEXT("piercer action selects its own array"),
			MHGZ::IGActionConfig::DanceDamageMultiplier(GetDefault<UMHGZKinsectPiercerAbility>()->KinsectPiercerDanceDamageMultipliers, Stacks, H.CombatConfig->MaxDanceStacks), PiercerExpected[Stacks]);
		if (!TestTrue(TEXT("real slash activation captures its starting multiplier"), H.ActivateAbility()))
		{
			H.Teardown();
			return false;
		}
		TestEqual(TEXT("slash uses activation-time layers"), H.Ability->FrozenDanceDamageMultiplier, SlashExpected[Stacks]);
		IGTest::Action<UMHGZKinsectSlashAbility>(H.CombatConfig)->KinsectSlashDanceDamageMultipliers = { 1.0f, 9.0f, 9.0f, 9.0f };
		TestEqual(TEXT("Blueprint query resolves the granted GA and ignores obsolete Combat values"),
			H.Resource->GetDanceDamageMultiplier(EIGDanceDamageAction::KinsectSlash), SlashExpected[Stacks]);
		FHitResult Hit(H.TargetActor, H.Hitzone, FVector(25.0, 0.0, 0.0), FVector::UpVector);
		Hit.bBlockingHit = true;
		const float KinsectMotion = H.Ability->AttackSegments[0].Damage.MotionValue.GetValueAtLevel(1.0f);
		TestEqual(TEXT("kinsect segment never gains hunter Dance damage"),
			MotionValue(H.Ability->MakeDamageSpec(Hit, 0)), KinsectMotion);
		// Simulate this attack earning another layer: both weapon segments must
		// retain the original snapshot rather than reading the updated resource.
		H.Resource->AddDanceStack(EIGDanceSource::KinsectSlash);
		for (int32 Segment = 1; Segment <= 2; ++Segment)
		{
			const float Base = H.Ability->AttackSegments[Segment].Damage.MotionValue.GetValueAtLevel(1.0f);
			const FGameplayEffectSpecHandle Spec = H.Ability->MakeDamageSpec(Hit, Segment);
			if (!TestTrue(TEXT("weapon spec is valid"), Spec.IsValid()))
			{
				H.Teardown();
				return false;
			}
			TestEqual(TEXT("each slash weapon hit uses the original snapshot"), MotionValue(Spec), Base * SlashExpected[Stacks]);
			const float Before = H.TargetAttributes->GetHealth();
			H.TargetASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
			TestEqual(TEXT("real weapon damage executes MV/100 once with Dance applied once"),
				Before - H.TargetAttributes->GetHealth(), FMath::RoundToFloat(Base * SlashExpected[Stacks]));
		}

		// Exercise production MakeDamageSpec, with only playback/movement state
		// injected. Full thrust commit/clear is checked by its existing lifecycle test.
		UMHGZDescendingThrustAbility* Thrust = NewObject<UMHGZDescendingThrustAbility>(H.ASC);
		Thrust->SetCurrentActorInfo(H.AbilityHandle, H.ASC->AbilityActorInfo.Get());
		Thrust->AttackSegments.Init(H.Ability->AttackSegments[1], 4);
		Thrust->FrozenDanceDamageMultiplier = MHGZ::IGActionConfig::DanceDamageMultiplier(Thrust->DescendingThrustDanceDamageMultipliers, Stacks, H.CombatConfig->MaxDanceStacks);
		Thrust->GenerateSegmentDamageInstanceIDs();
		UMHGZKinsectPiercerAbility* Piercer = NewObject<UMHGZKinsectPiercerAbility>(H.ASC);
		Piercer->SetCurrentActorInfo(H.AbilityHandle, H.ASC->AbilityActorInfo.Get());
		Piercer->AttackSegments.Init(H.Ability->AttackSegments[1], 3);
		Piercer->FrozenDanceDamageMultiplier = MHGZ::IGActionConfig::DanceDamageMultiplier(Piercer->KinsectPiercerDanceDamageMultipliers, Stacks, H.CombatConfig->MaxDanceStacks);
		H.Resource->ClearDanceStacks(EIGDanceClearReason::Landed);
		TestEqual(TEXT("landing clears real resource layers"), H.Resource->GetDanceStacks(), 0);
		for (int32 Segment = 0; Segment < 4; ++Segment)
		{
			const float Base = Thrust->AttackSegments[Segment].Damage.MotionValue.GetValueAtLevel(1.0f);
			TestEqual(TEXT("all four thrust hits retain their frozen bonus after layers clear"),
				MotionValue(Thrust->MakeDamageSpec(Hit, Segment)), Base * ThrustExpected[Stacks]);
		}
		for (const EKinsectPiercerPhase Phase : { EKinsectPiercerPhase::Start200, EKinsectPiercerPhase::Dive201, EKinsectPiercerPhase::Landing203 })
		{
			Piercer->Phase = Phase;
			for (int32 Segment = 0; Segment < 3; ++Segment)
			{
				const float Base = Piercer->AttackSegments[Segment].Damage.MotionValue.GetValueAtLevel(1.0f);
				TestEqual(TEXT("all piercer hits retain the frozen bonus in air and on ground after layers clear"),
					MotionValue(Piercer->MakeDamageSpec(Hit, Segment)),
					Base * PiercerExpected[Stacks]);
			}
		}
		for (int32 Index = 0; Index < 6; ++Index)
		{
			H.Resource->AddDanceStack(EIGDanceSource::KinsectSlash);
		}
		TestEqual(TEXT("repeated dances saturate at three"), H.Resource->GetDanceStacks(), 3);
		H.Teardown();
	}

	UMHGZKinsectSlashAbility* Config = NewObject<UMHGZKinsectSlashAbility>();
	Config->KinsectSlashDanceDamageMultipliers.SetNum(2);
	TestEqual(TEXT("missing layer does not borrow a lower layer"), MHGZ::IGActionConfig::DanceDamageMultiplier(Config->KinsectSlashDanceDamageMultipliers, 3, 3), 1.0f);
	Config->KinsectSlashDanceDamageMultipliers[1] = std::numeric_limits<float>::quiet_NaN();
	TestEqual(TEXT("invalid numeric multiplier is neutral"), MHGZ::IGActionConfig::DanceDamageMultiplier(Config->KinsectSlashDanceDamageMultipliers, 1, 3), 1.0f);
	TestEqual(TEXT("zero layers always remain neutral"), MHGZ::IGActionConfig::DanceDamageMultiplier(Config->KinsectSlashDanceDamageMultipliers, 0, 3), 1.0f);
	TestEqual(TEXT("unknown action is neutral"), MHGZ::IGActionConfig::DanceDamageMultiplier({}, 3, 3), 1.0f);
	return true;
}

#endif
