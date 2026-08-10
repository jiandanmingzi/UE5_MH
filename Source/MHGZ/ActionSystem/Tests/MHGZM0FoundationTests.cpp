// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "ActionSystem/MHGZGameplayEffectContext.h"
#include "ActionSystem/MHGZWeaponComboData.h"
#include "InsectGlaive/InsectGlaiveCombatConfig.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/DataValidation.h"
#include "WeaponRuntime/MHGZWeaponRuntimeTypes.h"

namespace
{
bool HasIssueContaining(const FDataValidationContext& Context, const FString& Needle)
{
	for (const FDataValidationContext::FIssue& Issue : Context.GetIssues())
	{
		if (Issue.Message.ToString().Contains(Needle))
		{
			return true;
		}
	}
	return false;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZComboValidationTest,
	"MHGZ.M0.Validation.ComboRejectsDuplicateTransitionID",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZComboValidationTest::RunTest(const FString& Parameters)
{
	UMHGZWeaponComboData* Combo = NewObject<UMHGZWeaponComboData>();
	FComboTransition Transition;
	Transition.TransitionID = TEXT("Duplicate.Edge");
	Transition.SourceState = TEXT("Idle");
	const FGameplayTag InputYTag = FGameplayTag::RequestGameplayTag(TEXT("Input.Weapon.Y"));
	TestTrue(TEXT("Input.Weapon.Y is registered"), InputYTag.IsValid());
	Transition.InputTag = InputYTag;
	Transition.AbilityClass = UGameplayAbility::StaticClass();
	Transition.TargetState = TEXT("Test.A");
	Combo->Transitions.Add(Transition);
	Combo->Transitions.Add(Transition);

	FDataValidationContext Context;
	TestEqual(TEXT("duplicate ID is invalid"), Combo->IsDataValid(Context), EDataValidationResult::Invalid);
	TestTrue(TEXT("duplicate ID emits a stable diagnostic"), HasIssueContaining(Context, TEXT("duplicated")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZInsectGlaiveCombatValidationTest,
	"MHGZ.M0.Validation.CombatConfigRejectsDanceArrayMismatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZInsectGlaiveCombatValidationTest::RunTest(const FString& Parameters)
{
	UInsectGlaiveCombatConfig* Config = NewObject<UInsectGlaiveCombatConfig>();
	Config->MaxDanceStacks = 2;
	Config->DanceDamageMultipliers = { 1.0f };

	FDataValidationContext Context;
	TestEqual(TEXT("dance array mismatch is invalid"), Config->IsDataValid(Context), EDataValidationResult::Invalid);
	TestTrue(TEXT("dance mismatch names the field"), HasIssueContaining(Context, TEXT("DanceDamageMultipliers")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZBallisticParameterValidationTest,
	"MHGZ.M0.Validation.BallisticParametersAreMutuallyExclusive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZBallisticParameterValidationTest::RunTest(const FString& Parameters)
{
	FWeaponMovementRequest Request;
	Request.Mode = EWeaponMovementMode::BallisticVault;
	Request.Duration = 0.5f;
	Request.ApexHeight = 200.f;
	Request.LaunchVelocity = FVector(0.f, 0.f, 600.f);
	TestFalse(TEXT("two active ballistic parameter groups are rejected"), Request.HasValidBallisticParameters());

	Request.LaunchVelocity = FVector::ZeroVector;
	Request.BallisticMode = EBallisticParameterMode::ApexHeightAndDuration;
	TestTrue(TEXT("apex plus duration is accepted"), Request.HasValidBallisticParameters());
	Request.BallisticMode = EBallisticParameterMode::ExplicitLaunchVelocity;
	TestFalse(TEXT("apex parameters with explicit-velocity mode are rejected"), Request.HasValidBallisticParameters());

	Request.ApexHeight = 0.f;
	Request.LaunchVelocity = FVector(100.f, 0.f, 600.f);
	Request.BallisticMode = EBallisticParameterMode::ExplicitLaunchVelocity;
	TestTrue(TEXT("explicit launch velocity is accepted"), Request.HasValidBallisticParameters());
	Request.BallisticMode = EBallisticParameterMode::ApexHeightAndDuration;
	TestFalse(TEXT("launch velocity with apex mode is rejected"), Request.HasValidBallisticParameters());

	Request.LaunchVelocity = FVector::ZeroVector;
	TestFalse(TEXT("missing ballistic parameters are rejected"), Request.HasValidBallisticParameters());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZGameplayEffectContextTest,
	"MHGZ.M0.GameplayEffectContext.AllocationAndDuplicate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZGameplayEffectContextTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	AActor* OwnerActor = World->SpawnActor<AActor>();
	UAbilitySystemComponent* ASC = NewObject<UAbilitySystemComponent>(OwnerActor);
	OwnerActor->AddInstanceComponent(ASC);
	ASC->RegisterComponent();
	ASC->InitAbilityActorInfo(OwnerActor, OwnerActor);
	FGameplayEffectContextHandle Handle = ASC->MakeEffectContext();
	FMHGZGameplayEffectContext* Context = FMHGZGameplayEffectContext::ExtractEffectContext(Handle);
	if (!TestNotNull(TEXT("MakeEffectContext allocates FMHGZGameplayEffectContext"), Context))
	{
		World->DestroyWorld(false);
		return false;
	}

	Context->AttackInstanceID = FGuid::NewGuid();
	Context->SourceActionTag = FGameplayTag::RequestGameplayTag(TEXT("Input.Weapon.Y"));
	Context->HitzoneTag = FGameplayTag::RequestGameplayTag(TEXT("Hitzone.Head"));
	Context->DamageSourceType = EMHGZDamageSourceType::Kinsect;
	Context->HitCueTag = FGameplayTag::RequestGameplayTag(TEXT("GameplayCue.Hit.Kinsect"));
	TestTrue(TEXT("SourceActionTag is registered"), Context->SourceActionTag.IsValid());
	TestTrue(TEXT("HitzoneTag is registered"), Context->HitzoneTag.IsValid());
	TestTrue(TEXT("HitCueTag is registered"), Context->HitCueTag.IsValid());

	FHitResult Hit;
	Hit.TraceStart = FVector(1.f, 2.f, 3.f);
	Hit.ImpactPoint = FVector(10.f, 20.f, 30.f);
	Context->AddHitResult(Hit, true);

	const FGuid ExpectedID = Context->AttackInstanceID;
	FGameplayEffectContextHandle DuplicateHandle = Handle.Duplicate();
	const FMHGZGameplayEffectContext* Duplicate = FMHGZGameplayEffectContext::ExtractEffectContext(DuplicateHandle);
	if (!TestNotNull(TEXT("Duplicate preserves the project context type"), Duplicate))
	{
		World->DestroyWorld(false);
		return false;
	}

	TestEqual(TEXT("AttackInstanceID survives Duplicate"), Duplicate->AttackInstanceID, ExpectedID);
	TestEqual(TEXT("SourceActionTag survives Duplicate"), Duplicate->SourceActionTag, Context->SourceActionTag);
	TestEqual(TEXT("HitzoneTag survives Duplicate"), Duplicate->HitzoneTag, Context->HitzoneTag);
	TestEqual(TEXT("DamageSourceType survives Duplicate"), Duplicate->DamageSourceType, EMHGZDamageSourceType::Kinsect);
	TestNotNull(TEXT("HitResult survives Duplicate"), Duplicate->GetHitResult());
	if (Duplicate->GetHitResult())
	{
		TestEqual(TEXT("HitResult values survive Duplicate"), Duplicate->GetHitResult()->ImpactPoint, Hit.ImpactPoint);
	}

	Context->GetHitResult()->ImpactPoint = FVector::ZeroVector;
	TestEqual(TEXT("Duplicate owns a deep HitResult copy"), Duplicate->GetHitResult()->ImpactPoint, Hit.ImpactPoint);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZTagLedgerOwnershipTest,
	"MHGZ.M0.TagLedger.OverlappingOwnersReleaseExactlyOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZTagLedgerOwnershipTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	AActor* OwnerActor = World->SpawnActor<AActor>();
	UAbilitySystemComponent* ASC = NewObject<UAbilitySystemComponent>(OwnerActor);
	OwnerActor->AddInstanceComponent(ASC);
	ASC->RegisterComponent();

	UMHGZWeaponRuntimeHostComponent* Host =
		NewObject<UMHGZWeaponRuntimeHostComponent>(OwnerActor);
	OwnerActor->AddInstanceComponent(Host);
	Host->RegisterComponent();

	FWeaponRuntimeToken RuntimeToken;
	RuntimeToken.Host = Host;
	RuntimeToken.Generation = 1;

	const FGameplayTag TestTag = FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Invincible"));
	TestTrue(TEXT("Combat.State.Invincible is registered"), TestTag.IsValid());
	FGameplayTagContainer Tags;
	Tags.AddTag(TestTag);

	FWeaponRuntimeTagLedger Ledger;
	Ledger.Initialize(RuntimeToken, ASC);

	FWeaponTagOwnerID FirstOwner;
	FirstOwner.RuntimeToken = RuntimeToken;
	FirstOwner.Kind = EWeaponTagOwnerKind::NotifyWindow;
	FirstOwner.LocalID = TEXT("Window.First");
	FWeaponTagOwnerID SecondOwner = FirstOwner;
	SecondOwner.LocalID = TEXT("Window.Second");

	const FWeaponOwnedTagToken FirstToken = Ledger.Acquire(FirstOwner, Tags);
	const FWeaponOwnedTagToken SecondToken = Ledger.Acquire(SecondOwner, Tags);
	TestTrue(TEXT("first token is valid"), FirstToken.IsValid());
	TestTrue(TEXT("second token is valid"), SecondToken.IsValid());
	TestEqual(TEXT("two owners contribute two loose counts"), ASC->GetTagCount(TestTag), 2);

	TestTrue(TEXT("first release succeeds"), Ledger.Release(FirstToken));
	TestEqual(TEXT("second owner keeps the tag active"), ASC->GetTagCount(TestTag), 1);
	TestFalse(TEXT("duplicate release is idempotent"), Ledger.Release(FirstToken));
	TestEqual(TEXT("duplicate release changes no count"), ASC->GetTagCount(TestTag), 1);
	TestEqual(TEXT("release owner removes one token"), Ledger.ReleaseOwner(SecondOwner), 1);
	TestEqual(TEXT("all owned counts are gone"), ASC->GetTagCount(TestTag), 0);

	World->DestroyWorld(false);
	return true;
}

#endif
