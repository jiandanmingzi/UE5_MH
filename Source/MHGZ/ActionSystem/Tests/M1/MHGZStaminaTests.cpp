// Copyright MHGZ Project. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "ActionSystem/Tests/Shared/MHGZM3TestHarness.h"
#include "ActionSystem/MHGZM1PlaceholderAbilities.h"
#include "ActionSystem/MHGZKinsectSlashAbility.h"
#include "ActionSystem/MHGZKinsectPiercerAbility.h"
#include "ActionSystem/MHGZAirDodgeAbility.h"
#include "ActionSystem/MHGZDescendingThrustAbility.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "GameplayEffect.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZStaminaRecoveryTest, "MHGZ.M1.Stamina.RecoveryLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZStaminaRecoveryTest::RunTest(const FString&)
{
	FMHGZM3Harness H;
	if (!H.Setup()) { H.Teardown(); return false; }
	const UMHGZAttributeSet* A = H.ASC->GetSet<UMHGZAttributeSet>();
	TestTrue(TEXT("ASC retains idle Tick after actor info initialization"), H.ASC->IsComponentTickEnabled());
	TestEqual(TEXT("initial maximum stamina is 200"), A->GetMaxStamina(), 200.f);
	TestEqual(TEXT("initial stamina starts full"), A->GetStamina(), 200.f);
	TestEqual(TEXT("initial maximum health is 200"), A->GetMaxHealth(), 200.f);
	TestEqual(TEXT("initial health starts full"), A->GetHealth(), 200.f);
	auto Set = [&](float Value) { H.ASC->SetNumericAttributeBase(UMHGZAttributeSet::GetStaminaAttribute(), Value); };
	auto Step = [&](float Dt) { H.ASC->TickComponent(Dt, LEVELTICK_All, nullptr); };
	Set(50.f); Step(.25f);
	TestEqual(TEXT("ground recovers 20/s"), A->GetStamina(), 55.f);
	H.Character->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	Step(.5f); TestEqual(TEXT("free air recovers 10/s"), A->GetStamina(), 60.f);
	H.ASC->SetNumericAttributeBase(UMHGZAttributeSet::GetStaminaRegenRateAttribute(), 2.f);
	Step(.5f); TestEqual(TEXT("recovery attribute remains a multiplier"), A->GetStamina(), 70.f);
	H.ASC->SetNumericAttributeBase(UMHGZAttributeSet::GetStaminaRegenRateAttribute(), 1.f);
	Set(199.f); Step(1.f); TestEqual(TEXT("saturates without hidden excess base stamina"), A->GetStamina(), 200.f);
	TestEqual(TEXT("base also saturates"), H.ASC->GetNumericAttributeBase(UMHGZAttributeSet::GetStaminaAttribute()), 200.f);
	Set(50.f); Step(0.f); TestEqual(TEXT("pause has no recovery"), A->GetStamina(), 50.f);
	H.ASC->SetNumericAttributeBase(UMHGZAttributeSet::GetHealthAttribute(), 0.f);
	Step(1.f); TestEqual(TEXT("dead does not recover"), A->GetStamina(), 50.f);
	H.ASC->SetNumericAttributeBase(UMHGZAttributeSet::GetHealthAttribute(), 100.f);
	H.ASC->InitAbilityActorInfo(H.PlayerState, nullptr);
	Step(1.f); TestEqual(TEXT("no avatar does not recover"), A->GetStamina(), 50.f);
	H.ASC->InitAbilityActorInfo(H.PlayerState, H.Character);
	Set(50.f); Step(.1f); Step(.1f); Step(.1f); Step(.1f); Step(.1f);
	TestEqual(TEXT("time slicing is independent of frame count"), A->GetStamina(), 55.f);
	H.Teardown(); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZStaminaCostsTest, "MHGZ.M1.Stamina.CostTimingAndPhases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZStaminaCostsTest::RunTest(const FString&)
{
	FMHGZM3Harness H;
	if (!H.Setup()) { H.Teardown(); return false; }
	const UMHGZAttributeSet* A = H.ASC->GetSet<UMHGZAttributeSet>();
	H.ASC->SetNumericAttributeBase(UMHGZAttributeSet::GetStaminaAttribute(), 60.f);
	FGameplayAbilityActorInfo Info;
	Info.InitFromActor(H.PlayerState, H.Character, H.ASC);
	UMHGZKinsectSlashAbility* Slash = NewObject<UMHGZKinsectSlashAbility>(H.PlayerState);
	const FGameplayAbilitySpecHandle QueryHandle = H.GiveAbility(UMHGZM1PlaceholderActionB::StaticClass());
	FWeaponAbilityActivationContext Context;
	Context.Input.ResolvedInputTag = M3::Tag(TEXT("Input.Weapon.RTB"));
	H.ASC->PrepareWeaponAbilityActivation(QueryHandle, Context);
	TestEqual(TEXT("RTB preflight reads snapshot without consuming it"), Slash->GetInstantStaminaCost(QueryHandle, &Info), 50.f);
	TestNotNull(TEXT("cost query retains pending context"), H.ASC->FindPendingActivationContext(QueryHandle));
	Context.Input.ResolvedInputTag = M3::Tag(TEXT("Input.Weapon.B"));
	H.ASC->PrepareWeaponAbilityActivation(QueryHandle, Context);
	TestEqual(TEXT("unmeasured B retains prior cost rather than borrowing RTB cost"), Slash->GetInstantStaminaCost(QueryHandle, &Info), 0.f);
	TestEqual(TEXT("186 has no recovery"), Slash->GetStaminaRecoveryPerSecond(), 0.f);
	Slash->Phase = EKinsectSlashPhase::DanceFromDash;
	TestEqual(TEXT("189 recovers 4/s"), Slash->GetStaminaRecoveryPerSecond(), 4.f);
	Slash->Phase = EKinsectSlashPhase::MissFall;
	TestEqual(TEXT("188 recovery stays paused"), Slash->GetStaminaRecoveryPerSecond(), 0.f);
	TestEqual(TEXT("200/201 permit 20/s even while airborne"), GetDefault<UMHGZKinsectPiercerAbility>()->GetStaminaRecoveryPerSecond(), 20.f);
	TestEqual(TEXT("air dodge has measured 25 cost"), GetDefault<UMHGZAirDodgeAbility>()->StaminaCost.GetValue(), 25.f);
	TestEqual(TEXT("air dodge blocks recovery"), GetDefault<UMHGZAirDodgeAbility>()->GetStaminaRecoveryPerSecond(), 0.f);
	TestEqual(TEXT("descending thrust costs 20 at Commit"), GetDefault<UMHGZDescendingThrustAbility>()->StaminaCost.GetValue(), 20.f);
	TestEqual(TEXT("descending thrust uses the existing instant cost policy"), GetDefault<UMHGZDescendingThrustAbility>()->StaminaCostPolicy, EAbilityStaminaCostPolicy::Instant);

	// Existing lightweight action exercises actual Commit/GE/lifecycle, no test-only gameplay path.
	const FGameplayAbilitySpecHandle Handle = H.GiveAbility(UMHGZM1PlaceholderActionA::StaticClass());
	TestTrue(TEXT("deferred action activates"), H.TryActivateWithInput(Handle, M3::MakePosedInput(true, false)));
	FGameplayAbilitySpec* Spec = H.ASC->FindAbilitySpecFromHandle(Handle);
	UMHGZGameplayAbility* Action = Spec && !Spec->GetAbilityInstances().IsEmpty()
		? Cast<UMHGZGameplayAbility>(Spec->GetAbilityInstances()[0]) : nullptr;
	if (TestNotNull(TEXT("action instance"), Action))
	{
		// Native placeholder constructors intentionally reset to None. Configure
		// this isolated active instance, not the shared native CDO/archetype.
		Action->StaminaCostPolicy = EAbilityStaminaCostPolicy::Instant;
		Action->StaminaCost = FScalableFloat(25.f);
		Action->bDeferInstantStaminaCost = true;
		Action->ApplyCost(Handle, &Info, FGameplayAbilityActivationInfo());
		TestEqual(TEXT("Commit ApplyCost defers payment"), A->GetStamina(), 60.f);
		TestTrue(TEXT("pays at semantic action boundary"), Action->TryPayDeferredStaminaCost());
		TestEqual(TEXT("paid exactly 25"), A->GetStamina(), 35.f);
		TestTrue(TEXT("repeat boundary is idempotent"), Action->TryPayDeferredStaminaCost());
		TestEqual(TEXT("repeat did not pay again"), A->GetStamina(), 35.f);
		H.ASC->TickComponent(.5f, LEVELTICK_All, nullptr);
		TestEqual(TEXT("active cost action pauses recovery"), A->GetStamina(), 35.f);
		Action->K2_EndAbility();
		H.ASC->TickComponent(.5f, LEVELTICK_All, nullptr);
		TestEqual(TEXT("ending action immediately restores recovery"), A->GetStamina(), 45.f);
	}
	H.Teardown(); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZIndependentStaminaRequirementTest,
	"MHGZ.M1.Stamina.IndependentActivationRequirement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZIndependentStaminaRequirementTest::RunTest(const FString&)
{
	FMHGZM3Harness H;
	if (!H.Setup()) { H.Teardown(); return false; }
	const FGameplayAbilitySpecHandle Handle = H.GiveAbility(UMHGZM1PlaceholderActionA::StaticClass());
	if (!TestTrue(TEXT("isolated cost action activates"), H.TryActivateWithInput(Handle, M3::MakePosedInput(true,false))))
	{
		H.Teardown(); return false;
	}
	FGameplayAbilitySpec* Spec = H.ASC->FindAbilitySpecFromHandle(Handle);
	UMHGZGameplayAbility* Action = Cast<UMHGZGameplayAbility>(Spec->GetAbilityInstances()[0]);
	FGameplayAbilityActorInfo Info;
	Info.InitFromActor(H.PlayerState,H.Character,H.ASC);
	const UMHGZAttributeSet* Attributes = H.ASC->GetSet<UMHGZAttributeSet>();
	auto SetStamina = [&](float Value) { H.ASC->SetNumericAttributeBase(UMHGZAttributeSet::GetStaminaAttribute(),Value); };
	Action->StaminaCostPolicy = EAbilityStaminaCostPolicy::Instant;
	Action->StaminaCost = FScalableFloat(20.f);
	Action->bDeferInstantStaminaCost = false;
	SetStamina(5.f);
	TestFalse(TEXT("legacy sentinel still requires the full authored cost"), Action->CheckCost(Handle,&Info));
	Action->StaminaActivationRequirement = 0.f;
	TestTrue(TEXT("zero threshold allows less stamina than the cost"), Action->CheckCost(Handle,&Info));
	Action->ApplyCost(Handle,&Info,FGameplayAbilityActivationInfo());
	TestEqual(TEXT("actual instant GE spends remaining stamina and clamps at zero"), Attributes->GetStamina(),0.f);
	TestTrue(TEXT("zero really permits zero stamina"), Action->CheckCost(Handle,&Info));
	Action->StaminaActivationRequirement = 10.f;
	SetStamina(9.f);
	TestFalse(TEXT("independent positive threshold is enforced"), Action->CheckCost(Handle,&Info));
	SetStamina(10.f);
	H.ASC->SetNumericAttributeBase(UMHGZAttributeSet::GetStaminaDeductionRateAttribute(),2.f);
	TestTrue(TEXT("deduction multiplier does not multiply explicit admission"), Action->CheckCost(Handle,&Info));
	Action->ApplyCost(Handle,&Info,FGameplayAbilityActivationInfo());
	TestEqual(TEXT("higher actual payment still clamps correctly"), Attributes->GetStamina(),0.f);
	Action->StaminaActivationRequirement = -1.f;
	SetStamina(39.f);
	TestFalse(TEXT("legacy gate retains multiplied payment requirement"), Action->CheckCost(Handle,&Info));
	SetStamina(40.f);
	TestTrue(TEXT("legacy gate accepts exact multiplied payment"), Action->CheckCost(Handle,&Info));
	UMHGZDescendingThrustAbility* Dive = NewObject<UMHGZDescendingThrustAbility>(H.PlayerState);
	SetStamina(0.f);
	TestTrue(TEXT("descending thrust has no implicit payment gate"), Dive->CheckCost(Handle,&Info));
	TestEqual(TEXT("descending thrust still authors a 20 stamina payment"), Dive->StaminaCost.GetValue(),20.f);
	H.ASC->CancelAbilityHandle(Handle);
	H.Teardown(); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZStaminaSprintTest, "MHGZ.M1.Stamina.SprintDrainAndExhaustion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZStaminaSprintTest::RunTest(const FString&)
{
	FMHGZM3Harness H;
	if (!H.Setup(false, AMHGZM3TestCharacter::StaticClass())) { H.Teardown(); return false; }
	AMHGZCharacter* Hunter = CastChecked<AMHGZCharacter>(H.Character);
	H.Host->SetSheathed(true);
	Hunter->bSprintHeld = true;
	Hunter->RawMoveInput = FVector2D(0.f, 1.f);
	Hunter->GetCharacterMovement()->Velocity = FVector(500.f,0.f,0.f);
	const UMHGZAttributeSet* A = H.ASC->GetSet<UMHGZAttributeSet>();
	H.ASC->SetNumericAttributeBase(UMHGZAttributeSet::GetStaminaAttribute(), 20.f);
	TestTrue(TEXT("sheathed actual sprint consumes stamina"), Hunter->ShouldConsumeSprintStamina());
	H.ASC->TickComponent(.5f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("8 points per second"), A->GetStamina(), 16.f);
	Hunter->RawMoveInput = FVector2D::ZeroVector;
	TestFalse(TEXT("standing RB does not drain"), Hunter->ShouldConsumeSprintStamina());
	Hunter->RawMoveInput = FVector2D(0.f, 1.f);
	H.Host->SetSheathed(false);
	TestFalse(TEXT("drawn locomotion does not drain"), Hunter->ShouldConsumeSprintStamina());
	H.Host->SetSheathed(true);
	H.ASC->SetNumericAttributeBase(UMHGZAttributeSet::GetStaminaAttribute(), 1.f);
	H.ASC->TickComponent(.5f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("drain clamps at zero"), A->GetStamina(), 0.f);
	TestFalse(TEXT("exhausted held RB cannot request Sprint"), Hunter->IsSprintHeld());
	H.ASC->TickComponent(.5f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("can recover while exhausted"), A->GetStamina(), 10.f);
	TestFalse(TEXT("recovery does not oscillate back into sprint"), Hunter->IsSprintHeld());
	Hunter->ClearSprintHeld();
	Hunter->bSprintHeld = true;
	TestTrue(TEXT("new RB press can sprint again"), Hunter->IsSprintHeld());
	H.Teardown(); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZPlayerInitialAttributesTest,
	"MHGZ.Maintenance.PlayerInitialAttributes.OnceAndBuffs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZPlayerInitialAttributesTest::RunTest(const FString&)
{
	FMHGZM3Harness H;
	if (!H.Setup()) { H.Teardown(); return false; }
	UClass* PSClass = LoadClass<AMHGZPlayerState>(nullptr,
		TEXT("/Game/Blueprints/Characters/Demo/BP_PlayerState.BP_PlayerState_C"));
	UClass* GEClass = LoadClass<UGameplayEffect>(nullptr,
		TEXT("/Game/GameplayEffects/Core/GE_PlayerInitialAttributes.GE_PlayerInitialAttributes_C"));
	AMHGZPlayerState* PS = PSClass ? H.World->SpawnActor<AMHGZPlayerState>(PSClass) : nullptr;
	if (!TestNotNull(TEXT("authored player state spawns"), PS)
		|| !TestNotNull(TEXT("authored initialization GE exists"), GEClass))
	{
		H.Teardown(); return false;
	}
	UMHGZAbilitySystemComponent* ASC = PS->GetMHGZAbilitySystemComponent();
	const UMHGZAttributeSet* A = PS->GetAttributeSet();
	ASC->InitAbilityActorInfo(PS, H.Character);
	int32 InitialGECount = 0;
	for (const TSubclassOf<UGameplayEffect>& EffectClass : ASC->CoreAttributeEffects)
	{
		if (EffectClass.Get() == GEClass) { ++InitialGECount; }
	}
	TestEqual(TEXT("initialization GE is wired once"), InitialGECount, 1);
	const UGameplayEffect* Init = GEClass->GetDefaultObject<UGameplayEffect>();
	TestEqual(TEXT("initial values are instant, not a lasting override"), Init->DurationPolicy, EGameplayEffectDurationType::Instant);
	TestEqual(TEXT("four independent editable initial values"), Init->Modifiers.Num(), 4);
	ASC->SetNumericAttributeBase(UMHGZAttributeSet::GetMaxHealthAttribute(), 80.f);
	ASC->SetNumericAttributeBase(UMHGZAttributeSet::GetMaxStaminaAttribute(), 90.f);
	ASC->SetNumericAttributeBase(UMHGZAttributeSet::GetHealthAttribute(), 11.f);
	ASC->SetNumericAttributeBase(UMHGZAttributeSet::GetStaminaAttribute(), 17.f);
	ASC->InitializeAbilitySystem();
	TestEqual(TEXT("actual authored GE initializes MaxHealth"), A->GetMaxHealth(), 200.f);
	TestEqual(TEXT("actual authored GE initializes MaxStamina"), A->GetMaxStamina(), 200.f);
	TestEqual(TEXT("actual authored GE fills initial Health"), A->GetHealth(), 200.f);
	TestEqual(TEXT("actual authored GE fills initial Stamina"), A->GetStamina(), 200.f);
	ASC->SetNumericAttributeBase(UMHGZAttributeSet::GetHealthAttribute(), 75.f);
	ASC->SetNumericAttributeBase(UMHGZAttributeSet::GetStaminaAttribute(), 60.f);
	ASC->InitializeAbilitySystem();
	TestEqual(TEXT("reinitialization does not heal"), A->GetHealth(), 75.f);
	TestEqual(TEXT("reinitialization does not refill stamina"), A->GetStamina(), 60.f);

	UGameplayEffect* Buff = NewObject<UGameplayEffect>(GetTransientPackage());
	Buff->DurationPolicy = EGameplayEffectDurationType::Infinite;
	for (const FGameplayAttribute& Attribute : {
		UMHGZAttributeSet::GetMaxHealthAttribute(), UMHGZAttributeSet::GetMaxStaminaAttribute() })
	{
		FGameplayModifierInfo& Modifier = Buff->Modifiers.AddDefaulted_GetRef();
		Modifier.Attribute = Attribute;
		Modifier.ModifierOp = EGameplayModOp::Additive;
		Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(50.f));
	}
	const FActiveGameplayEffectHandle BuffHandle = ASC->ApplyGameplayEffectSpecToSelf(
		FGameplayEffectSpec(Buff, ASC->MakeEffectContext(), 1.f));
	TestTrue(TEXT("runtime max-attribute buff applies"), BuffHandle.IsValid());
	TestEqual(TEXT("runtime GE can increase MaxHealth beyond the initial value"), A->GetMaxHealth(), 250.f);
	TestEqual(TEXT("runtime GE can increase MaxStamina beyond the initial value"), A->GetMaxStamina(), 250.f);
	ASC->InitializeAbilitySystem();
	TestEqual(TEXT("initialization does not overwrite the buff"), A->GetMaxHealth(), 250.f);
	TestEqual(TEXT("buff does not implicitly heal"), A->GetHealth(), 75.f);
	TestEqual(TEXT("buff does not implicitly refill"), A->GetStamina(), 60.f);
	ASC->RemoveActiveGameplayEffect(BuffHandle);
	TestEqual(TEXT("removing buff restores configured initial max health"), A->GetMaxHealth(), 200.f);
	TestEqual(TEXT("removing buff restores configured initial max stamina"), A->GetMaxStamina(), 200.f);
	TestEqual(TEXT("removing buff preserves current health"), A->GetHealth(), 75.f);
	TestEqual(TEXT("removing buff preserves current stamina"), A->GetStamina(), 60.f);
	H.Teardown(); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZMaximumPoolClampTest,
	"MHGZ.M1.Stamina.MaximumChangesClampCurrentPools",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZMaximumPoolClampTest::RunTest(const FString&)
{
	FMHGZM3Harness H;
	if (!H.Setup()) { H.Teardown(); return false; }
	const UMHGZAttributeSet* A = H.ASC->GetSet<UMHGZAttributeSet>();
	H.ASC->SetNumericAttributeBase(UMHGZAttributeSet::GetMaxHealthAttribute(), 100.f);
	H.ASC->SetNumericAttributeBase(UMHGZAttributeSet::GetMaxStaminaAttribute(), 80.f);
	TestEqual(TEXT("lower health maximum clamps immediately without taking a hit"), A->GetHealth(), 100.f);
	TestEqual(TEXT("lower stamina maximum clamps without waiting for Tick"), A->GetStamina(), 80.f);
	TestEqual(TEXT("health base does not retain hidden excess"),
		H.ASC->GetNumericAttributeBase(UMHGZAttributeSet::GetHealthAttribute()), 100.f);
	TestEqual(TEXT("stamina base does not retain hidden excess"),
		H.ASC->GetNumericAttributeBase(UMHGZAttributeSet::GetStaminaAttribute()), 80.f);
	H.ASC->SetNumericAttributeBase(UMHGZAttributeSet::GetMaxHealthAttribute(), 200.f);
	H.ASC->SetNumericAttributeBase(UMHGZAttributeSet::GetMaxStaminaAttribute(), 200.f);
	TestEqual(TEXT("increasing maximum does not heal"), A->GetHealth(), 100.f);
	TestEqual(TEXT("increasing maximum does not refill stamina"), A->GetStamina(), 80.f);

	UGameplayEffect* Buff = NewObject<UGameplayEffect>(GetTransientPackage());
	Buff->DurationPolicy = EGameplayEffectDurationType::Infinite;
	for (const FGameplayAttribute& Attribute : {
		UMHGZAttributeSet::GetMaxHealthAttribute(), UMHGZAttributeSet::GetMaxStaminaAttribute() })
	{
		FGameplayModifierInfo& Modifier = Buff->Modifiers.AddDefaulted_GetRef();
		Modifier.Attribute = Attribute;
		Modifier.ModifierOp = EGameplayModOp::Additive;
		Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(50.f));
	}
	const FActiveGameplayEffectHandle Handle = H.ASC->ApplyGameplayEffectSpecToSelf(
		FGameplayEffectSpec(Buff, H.ASC->MakeEffectContext(), 1.f));
	TestTrue(TEXT("temporary pool maximum buff applies"), Handle.IsValid());
	H.ASC->SetNumericAttributeBase(UMHGZAttributeSet::GetHealthAttribute(), 240.f);
	H.ASC->SetNumericAttributeBase(UMHGZAttributeSet::GetStaminaAttribute(), 230.f);
	H.ASC->RemoveActiveGameplayEffect(Handle);
	TestEqual(TEXT("GE removal clamps health to restored maximum"), A->GetHealth(), 200.f);
	TestEqual(TEXT("GE removal clamps stamina to restored maximum"), A->GetStamina(), 200.f);
	H.ASC->SetNumericAttributeBase(UMHGZAttributeSet::GetHealthAttribute(), 75.f);
	H.ASC->SetNumericAttributeBase(UMHGZAttributeSet::GetStaminaAttribute(), 60.f);
	H.ASC->SetNumericAttributeBase(UMHGZAttributeSet::GetMaxHealthAttribute(), 150.f);
	H.ASC->SetNumericAttributeBase(UMHGZAttributeSet::GetMaxStaminaAttribute(), 150.f);
	TestEqual(TEXT("lowering maximum above current health does not heal or scale"), A->GetHealth(), 75.f);
	TestEqual(TEXT("lowering maximum above current stamina does not refill or scale"), A->GetStamina(), 60.f);
	H.Teardown(); return true;
}
#endif
