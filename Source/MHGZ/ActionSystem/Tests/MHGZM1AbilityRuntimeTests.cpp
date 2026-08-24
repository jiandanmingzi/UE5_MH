// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "AbilitySystemComponent.h"
#include "ActionSystem/AbilityTask_MHGZStaminaDrain.h"
#include "ActionSystem/MHGZAbilityCostGameplayEffects.h"
#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "ActionSystem/MHGZComboCoordinatorAbility.h"
#include "ActionSystem/MHGZDodgeAbility.h"
#include "ActionSystem/MHGZGameplayAbility.h"
#include "ActionSystem/MHGZM1PlaceholderAbilities.h"
#include "ActionSystem/MHGZWeaponComboData.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "AttributeSystem/MHGZWeaponResourceComponent.h"
#include "AttributeSystem/Res_InsectGlaive.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"
#include "WeaponRuntime/MHGZWeaponRuntimeTypes.h"

namespace
{
UWorld* CreateM1TestWorld()
{
	return UWorld::CreateWorld(EWorldType::Game, false);
}

void DestroyM1TestWorld(UWorld* World)
{
	if (World)
	{
		World->DestroyWorld(false);
	}
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM1NativeCostEffectsValid,
	"MHGZ.M1.GameplayEffects.NativeCostEffectsAreValid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM1NativeCostEffectsValid::RunTest(const FString& Parameters)
{
	const FGameplayTag CostTag = UMHGZStaminaCostGameplayEffect::GetStaminaCostSetByCallerTag();
	TestTrue(TEXT("Data.Cost.Stamina is registered"), CostTag.IsValid());

	const UMHGZStaminaCostGameplayEffect* StaminaGE =
		GetDefault<UMHGZStaminaCostGameplayEffect>();
	TestEqual(TEXT("stamina GE is Instant"), StaminaGE->DurationPolicy,
		EGameplayEffectDurationType::Instant);
	TestEqual(TEXT("stamina GE has exactly one modifier"), StaminaGE->Modifiers.Num(), 1);
	if (StaminaGE->Modifiers.Num() == 1)
	{
		const FGameplayModifierInfo& Modifier = StaminaGE->Modifiers[0];
		TestEqual(TEXT("stamina modifier targets Stamina attribute"), Modifier.Attribute,
			UMHGZAttributeSet::GetStaminaAttribute());
		TestTrue(TEXT("stamina modifier is Additive"),
			Modifier.ModifierOp == EGameplayModOp::Additive);
		TestEqual(TEXT("stamina magnitude is SetByCaller"),
			Modifier.ModifierMagnitude.GetMagnitudeCalculationType(),
			EGameplayEffectMagnitudeCalculation::SetByCaller);
		TestEqual(TEXT("stamina SetByCaller tag matches"),
			Modifier.ModifierMagnitude.GetSetByCallerFloat().DataTag, CostTag);
	}

	const UMHGZCooldownGameplayEffect* CooldownGE =
		GetDefault<UMHGZCooldownGameplayEffect>();
	TestEqual(TEXT("cooldown GE is HasDuration"), CooldownGE->DurationPolicy,
		EGameplayEffectDurationType::HasDuration);
	float StaticDuration = 0.f;
	TestTrue(TEXT("cooldown GE exposes a static placeholder duration"),
		CooldownGE->DurationMagnitude.GetStaticMagnitudeIfPossible(1.f, StaticDuration));
	TestEqual(TEXT("cooldown placeholder duration is 1s"), StaticDuration, 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM1HostTokenAndRegistries,
	"MHGZ.M1.RuntimeHost.StaleTokenAndExactRegistries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM1HostTokenAndRegistries::RunTest(const FString& Parameters)
{
	UWorld* World = CreateM1TestWorld();
	if (!World)
	{
		return false;
	}

	ACharacter* Character = World->SpawnActor<ACharacter>(
		ACharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
	if (!Character)
	{
		DestroyM1TestWorld(World);
		return false;
	}

	UMHGZAbilitySystemComponent* ASC = NewObject<UMHGZAbilitySystemComponent>(Character);
	Character->AddInstanceComponent(ASC);
	ASC->RegisterComponent();

	UMHGZWeaponRuntimeHostComponent* Host =
		NewObject<UMHGZWeaponRuntimeHostComponent>(Character);
	Character->AddInstanceComponent(Host);
	Host->RegisterComponent();

	// The synthetic world has no floor and is never ticked, so a freshly
	// spawned Character defaults to Falling. Pin the movement mode explicitly;
	// RuntimeHost should still derive its initial posture from CharacterMovement.
	Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	Host->InitializePawnRuntime(Character, nullptr, ASC, nullptr);
	TestTrue(TEXT("host is initialized"), Host->IsRuntimeInitialized());
	TestEqual(TEXT("host registers itself on the ASC"), ASC->GetRuntimeHost(), Host);

	const FWeaponRuntimeToken CurrentToken = Host->GetCurrentToken();
	TestTrue(TEXT("current token is valid"), CurrentToken.IsValid());
	TestTrue(TEXT("current token is current"), Host->IsTokenCurrent(CurrentToken));
	const uint64 InitialGeneration = CurrentToken.Generation;

	// 幂等初始化：相同输入不重建，Generation 不变。
	Host->InitializePawnRuntime(Character, nullptr, ASC, nullptr);
	TestEqual(TEXT("idempotent init keeps generation"),
		Host->GetCurrentToken().Generation, InitialGeneration);

	// 姿态经 Ledger 初始化（Ground/Aerial、Sheathed/Unsheathed 互斥）。
	const FGameplayTag GroundedTag = FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Grounded"));
	const FGameplayTag AerialTag = FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aerial"));
	const FGameplayTag SheathedTag = FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Sheathed"));
	TestTrue(TEXT("pose tags are registered"),
		GroundedTag.IsValid() && AerialTag.IsValid() && SheathedTag.IsValid());
	TestTrue(TEXT("spawned character is on ground"),
		Character->GetCharacterMovement()->IsMovingOnGround());
	TestTrue(TEXT("host starts grounded"), Host->IsGrounded());
	TestEqual(TEXT("grounded ledger count is 1"), ASC->GetTagCount(GroundedTag), 1);
	TestEqual(TEXT("aerial ledger count is 0"), ASC->GetTagCount(AerialTag), 0);
	TestTrue(TEXT("host starts sheathed"), Host->IsSheathed());
	TestEqual(TEXT("sheathed ledger count is 1"), ASC->GetTagCount(SheathedTag), 1);

	TestTrue(TEXT("SetGrounded(false) succeeds"), Host->SetGrounded(false));
	TestEqual(TEXT("aerial ledger count is 1"), ASC->GetTagCount(AerialTag), 1);
	TestEqual(TEXT("grounded ledger count is 0"), ASC->GetTagCount(GroundedTag), 0);
	TestFalse(TEXT("unchanged SetGrounded is a no-op"), Host->SetGrounded(false));

	Host->HandleLanded();
	TestTrue(TEXT("host is grounded after landing"), Host->IsGrounded());
	TestEqual(TEXT("grounded ledger count is 1 after landing"),
		ASC->GetTagCount(GroundedTag), 1);
	TestEqual(TEXT("aerial ledger count is 0 after landing"),
		ASC->GetTagCount(AerialTag), 0);

	// 有效 ActionToken（真实 Handle + 独立实例）。
	const FGameplayAbilitySpecHandle AbilityHandle = ASC->GiveAbility(
		FGameplayAbilitySpec(UMHGZDodgeAbility::StaticClass(), 1, INDEX_NONE, ASC));
	TestTrue(TEXT("ability handle is valid"), AbilityHandle.IsValid());

	UMHGZDodgeAbility* DodgeInstance = NewObject<UMHGZDodgeAbility>(ASC);
	FWeaponActionToken ActionToken;
	ActionToken.RuntimeToken = CurrentToken;
	ActionToken.AbilityHandle = AbilityHandle;
	ActionToken.ActivationSequenceID = Host->AllocateActivationSequenceID();
	ActionToken.AbilityInstance = DodgeInstance;
	TestTrue(TEXT("action token is valid"), ActionToken.IsValid());

	// 陈旧 / 无效 Token 拒绝。
	FWeaponRuntimeToken StaleToken;
	StaleToken.Host = Host;
	StaleToken.Generation = InitialGeneration + 1;
	TestFalse(TEXT("stale token is not current"), Host->IsTokenCurrent(StaleToken));
	FWeaponActionToken StaleAction = ActionToken;
	StaleAction.RuntimeToken = StaleToken;
	TestFalse(TEXT("register rejects stale action"), Host->RegisterAction(StaleAction));
	FWeaponActionToken InvalidAction = ActionToken;
	InvalidAction.AbilityInstance = nullptr;
	TestFalse(TEXT("register rejects invalid action"), Host->RegisterAction(InvalidAction));

	// Action 注册 / 注销。
	TestTrue(TEXT("register action succeeds"), Host->RegisterAction(ActionToken));
	TestTrue(TEXT("register action is idempotent"), Host->RegisterAction(ActionToken));
	TestFalse(TEXT("stale action cannot own montage root motion"),
		Host->AcquireMontageRootMotion(StaleAction));
	TestTrue(TEXT("registered action acquires montage root motion"),
		Host->AcquireMontageRootMotion(ActionToken));
	TestTrue(TEXT("same action reacquires montage root motion idempotently"),
		Host->AcquireMontageRootMotion(ActionToken));
	TestTrue(TEXT("host reports exact montage root motion owner"),
		Host->IsMontageRootMotionOwnedBy(ActionToken));
	FWeaponActionToken CompetingAction = ActionToken;
	CompetingAction.ActivationSequenceID = Host->AllocateActivationSequenceID();
	TestTrue(TEXT("competing action can register"),
		Host->RegisterAction(CompetingAction));
	TestFalse(TEXT("competing action cannot steal montage root motion"),
		Host->AcquireMontageRootMotion(CompetingAction));
	TestFalse(TEXT("competing action cannot release montage root motion"),
		Host->ReleaseMontageRootMotion(CompetingAction));
	TestTrue(TEXT("competing action unregisters"),
		Host->UnregisterAction(CompetingAction));
	TestFalse(TEXT("unregister rejects stale action"), Host->UnregisterAction(StaleAction));
	TestTrue(TEXT("unregister action succeeds"), Host->UnregisterAction(ActionToken));
	TestFalse(TEXT("unregister action releases montage root motion ownership"),
		Host->IsMontageRootMotionOwned());
	TestFalse(TEXT("second unregister fails"), Host->UnregisterAction(ActionToken));

	// DispatchInputRelease：按激活输入身份（SourceControlTag + SequenceID）精确分发；
	// 基类默认 HandleInputReleased 为 No-Op，匹配后不得结束/注销 Action。
	// 未激活的 NewObject 实例其 ActivationContext.Input 为默认身份（无效 SourceControlTag、SequenceID 0）。
	TestTrue(TEXT("re-register action for release dispatch"), Host->RegisterAction(ActionToken));
	TestTrue(TEXT("re-registered action can reacquire montage root motion"),
		Host->AcquireMontageRootMotion(ActionToken));
	TestTrue(TEXT("exact owner releases montage root motion"),
		Host->ReleaseMontageRootMotion(ActionToken));
	TestFalse(TEXT("montage root motion release is idempotent"),
		Host->ReleaseMontageRootMotion(ActionToken));

	FWeaponInputSnapshot IdentityMismatchRelease;
	IdentityMismatchRelease.SourceControlTag =
		FGameplayTag::RequestGameplayTag(TEXT("Input.Weapon.Y"));
	IdentityMismatchRelease.SequenceID = 5;
	IdentityMismatchRelease.Phase = EWeaponInputPhase::Completed;
	Host->DispatchInputRelease(IdentityMismatchRelease);
	TestEqual(TEXT("identity-mismatched release leaves reason Normal"),
		DodgeInstance->GetActionEndReason(), EWeaponActionEndReason::Normal);

	FWeaponInputSnapshot IdentityMatchRelease;
	IdentityMatchRelease.SourceControlTag = FGameplayTag();
	IdentityMatchRelease.SequenceID = 0;
	IdentityMatchRelease.Phase = EWeaponInputPhase::Completed;
	Host->DispatchInputRelease(IdentityMatchRelease);
	TestEqual(TEXT("identity-matched release still leaves reason Normal (no-op default)"),
		DodgeInstance->GetActionEndReason(), EWeaponActionEndReason::Normal);
	TestTrue(TEXT("no-op default keeps the action registered"),
		Host->UnregisterAction(ActionToken));

	// 精确 Montage 注册表。
	TestTrue(TEXT("re-register action for montage registry"),
		Host->RegisterAction(ActionToken));
	USkeletalMeshComponent* Mesh = Character->GetMesh();
	TestNotNull(TEXT("character has a mesh"), Mesh);
	TestTrue(TEXT("register montage succeeds"),
		Host->RegisterMontage(ActionToken, Mesh, 7));
	TestFalse(TEXT("register montage rejects null mesh"),
		Host->RegisterMontage(ActionToken, nullptr, 7));
	TestFalse(TEXT("register montage rejects INDEX_NONE instance"),
		Host->RegisterMontage(ActionToken, Mesh, INDEX_NONE));
	TestFalse(TEXT("register montage rejects stale token"),
		Host->RegisterMontage(StaleAction, Mesh, 8));

	FWeaponActionToken Resolved;
	TestTrue(TEXT("resolve montage succeeds"), Host->ResolveMontage(Mesh, 7, Resolved));
	TestEqual(TEXT("resolved token matches the registered token"), Resolved, ActionToken);
	TestFalse(TEXT("resolve unknown instance fails"), Host->ResolveMontage(Mesh, 8, Resolved));
	TestEqual(TEXT("unregister montages removes one"), Host->UnregisterMontages(ActionToken), 1);
	TestFalse(TEXT("resolve after unregister fails"), Host->ResolveMontage(Mesh, 7, Resolved));
	TestEqual(TEXT("unregister montages is idempotent"),
		Host->UnregisterMontages(ActionToken), 0);
	TestTrue(TEXT("unregister action after montage registry test"),
		Host->UnregisterAction(ActionToken));

	// 资源预留透传：空 Specs 成功且预留无效；非空无 Provider 失败。
	TArray<FWeaponResourceCostSpec> EmptySpecs;
	TArray<FWeaponResourceCostSpec> NonEmptySpecs;
	FWeaponResourceCostSpec CostSpec;
	NonEmptySpecs.Add(CostSpec);
	TestTrue(TEXT("host accepts empty reserve specs"), Host->CanReserveCosts(EmptySpecs));
	TestFalse(TEXT("host rejects non-empty specs without provider"),
		Host->CanReserveCosts(NonEmptySpecs));
	FWeaponResourceCostReservation Reservation;
	TestTrue(TEXT("host empty reserve succeeds"),
		Host->TryReserveCosts(ActionToken, EmptySpecs, Reservation));
	TestFalse(TEXT("host empty reserve yields invalid reservation"), Reservation.IsValid());
	TestFalse(TEXT("host non-empty reserve fails without provider"),
		Host->TryReserveCosts(ActionToken, NonEmptySpecs, Reservation));

	// Shutdown：Generation +1、Token 失效、注册表与 Ledger 清空。
	Host->ShutdownRuntime(EWeaponRuntimeEndReason::RuntimeShutdown);
	TestFalse(TEXT("host is not initialized after shutdown"), Host->IsRuntimeInitialized());
	TestFalse(TEXT("old token is stale after shutdown"), Host->IsTokenCurrent(CurrentToken));
	TestFalse(TEXT("register rejects action after shutdown"), Host->RegisterAction(ActionToken));
	TestFalse(TEXT("resolve montage fails after shutdown"), Host->ResolveMontage(Mesh, 7, Resolved));
	TestEqual(TEXT("grounded ledger count is 0 after shutdown"),
		ASC->GetTagCount(GroundedTag), 0);
	TestEqual(TEXT("aerial ledger count is 0 after shutdown"),
		ASC->GetTagCount(AerialTag), 0);
	TestNull(TEXT("host detaches from the ASC after shutdown"), ASC->GetRuntimeHost());

	// 重建：Generation 单调递增，姿态重新初始化。
	Host->InitializePawnRuntime(Character, nullptr, ASC, nullptr);
	TestTrue(TEXT("host is re-initialized"), Host->IsRuntimeInitialized());
	TestTrue(TEXT("rebuild increments generation"),
		Host->GetCurrentToken().Generation > InitialGeneration);
	TestFalse(TEXT("pre-rebuild token is stale"), Host->IsTokenCurrent(CurrentToken));
	TestEqual(TEXT("host is re-grounded after rebuild"),
		ASC->GetTagCount(GroundedTag), 1);

	DestroyM1TestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM1DodgeMissingMontageCleanup,
	"MHGZ.M1.Dodge.MissingMontageHasNoSideEffects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM1DodgeMissingMontageCleanup::RunTest(const FString& Parameters)
{
	UWorld* World = CreateM1TestWorld();
	if (!World)
	{
		return false;
	}
	ACharacter* Character = World->SpawnActor<ACharacter>(
		ACharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
	if (!Character)
	{
		DestroyM1TestWorld(World);
		return false;
	}

	UMHGZAbilitySystemComponent* ASC = NewObject<UMHGZAbilitySystemComponent>(Character);
	Character->AddInstanceComponent(ASC);
	ASC->RegisterComponent();
	ASC->InitAbilityActorInfo(Character, Character);
	UMHGZWeaponRuntimeHostComponent* Host =
		NewObject<UMHGZWeaponRuntimeHostComponent>(Character);
	Character->AddInstanceComponent(Host);
	Host->RegisterComponent();
	Host->InitializePawnRuntime(Character, nullptr, ASC, nullptr);

	const FGameplayAbilitySpecHandle DodgeHandle = ASC->GiveAbility(
		FGameplayAbilitySpec(UMHGZDodgeAbility::StaticClass(), 1, INDEX_NONE, ASC));
	UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	const ECollisionResponse WeaponBefore =
		Capsule->GetCollisionResponseToChannel(ECC_GameTraceChannel1);
	const ECollisionResponse MonsterAttackBefore =
		Capsule->GetCollisionResponseToChannel(ECC_GameTraceChannel2);

	FWeaponInputSnapshot Snapshot;
	Snapshot.ResolvedInputTag = FGameplayTag::RequestGameplayTag(TEXT("Input.Dodge"));
	Snapshot.SourceControlTag = Snapshot.ResolvedInputTag;
	Snapshot.SequenceID = 77;
	Snapshot.Phase = EWeaponInputPhase::Started;
	Snapshot.ContextTags.AddTag(
		FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Sheathed")));
	ASC->HandleResolvedInputSnapshot(Snapshot);

	const FGameplayTag BlockMovement =
		FGameplayTag::RequestGameplayTag(TEXT("Combat.State.BlockMovement"));
	const FGameplayTag Invincible =
		FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Invincible"));
	TestEqual(TEXT("missing montage grants no BlockMovement"),
		ASC->GetTagCount(BlockMovement), 0);
	TestEqual(TEXT("missing montage opens no invincibility window"),
		ASC->GetTagCount(Invincible), 0);
	TestEqual(TEXT("missing montage leaves Weapon collision unchanged"),
		Capsule->GetCollisionResponseToChannel(ECC_GameTraceChannel1), WeaponBefore);
	TestEqual(TEXT("missing montage leaves MonsterAttack collision unchanged"),
		Capsule->GetCollisionResponseToChannel(ECC_GameTraceChannel2), MonsterAttackBefore);
	FWeaponAbilityActivationContext LeakedContext;
	TestFalse(TEXT("missing-montage activation consumes its context"),
		ASC->ConsumePendingActivationContext(DodgeHandle, LeakedContext));
	if (const FGameplayAbilitySpec* DodgeSpec = ASC->FindAbilitySpecFromHandle(DodgeHandle))
	{
		TestFalse(TEXT("missing-montage Dodge is no longer active"), DodgeSpec->IsActive());
	}

	Host->ShutdownRuntime(EWeaponRuntimeEndReason::RuntimeShutdown);
	DestroyM1TestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM1ComboTransactionHarness,
	"MHGZ.M1.Combo.TransactionReentryWindowsAndFailures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM1ComboTransactionHarness::RunTest(const FString& Parameters)
{
	UWorld* World = CreateM1TestWorld();
	if (!World)
	{
		return false;
	}

	ACharacter* Character = World->SpawnActor<ACharacter>(
		ACharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
	if (!Character)
	{
		DestroyM1TestWorld(World);
		return false;
	}

	UMHGZAbilitySystemComponent* ASC = NewObject<UMHGZAbilitySystemComponent>(Character);
	Character->AddInstanceComponent(ASC);
	ASC->RegisterComponent();
	ASC->InitAbilityActorInfo(Character, Character);

	UMHGZWeaponRuntimeHostComponent* Host =
		NewObject<UMHGZWeaponRuntimeHostComponent>(Character);
	Character->AddInstanceComponent(Host);
	Host->RegisterComponent();
	Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	Host->InitializePawnRuntime(Character, nullptr, ASC, nullptr);

	UMHGZM1ReservationProbeResource* Probe =
		NewObject<UMHGZM1ReservationProbeResource>(Character);
	Character->AddInstanceComponent(Probe);
	Probe->RegisterComponent();
	Host->SetResourceProvider(Probe);

	const FGameplayAbilitySpecHandle CoordinatorHandle = ASC->GiveAbility(
		FGameplayAbilitySpec(UGA_WeaponComboCoordinator::StaticClass(), 1, INDEX_NONE, ASC));
	const FGameplayAbilitySpecHandle ActionAHandle = ASC->GiveAbility(
		FGameplayAbilitySpec(UMHGZM1PlaceholderActionA::StaticClass(), 1, INDEX_NONE, ASC));
	ASC->GiveAbility(
		FGameplayAbilitySpec(UMHGZM1PlaceholderActionB::StaticClass(), 1, INDEX_NONE, ASC));
	const FGameplayAbilitySpecHandle CommitFailureHandle = ASC->GiveAbility(
		FGameplayAbilitySpec(UMHGZM1CommitFailureAbility::StaticClass(), 1, INDEX_NONE, ASC));

	TestTrue(TEXT("coordinator activates"), ASC->TryActivateAbility(CoordinatorHandle));
	UGA_WeaponComboCoordinator* Coordinator = ASC->GetActiveComboCoordinator();
	TestNotNull(TEXT("ASC exposes the active coordinator"), Coordinator);
	if (!Coordinator)
	{
		Host->ShutdownRuntime(EWeaponRuntimeEndReason::RuntimeShutdown);
		DestroyM1TestWorld(World);
		return false;
	}

	UMHGZWeaponComboData* ComboData = NewObject<UMHGZWeaponComboData>(Coordinator);
	const FGameplayTag YTag = FGameplayTag::RequestGameplayTag(TEXT("Input.Weapon.Y"));
	const FGameplayTag BTag = FGameplayTag::RequestGameplayTag(TEXT("Input.Weapon.B"));
	const FGameplayTag YBTag = FGameplayTag::RequestGameplayTag(TEXT("Input.Weapon.YB"));
	const FGameplayTag BlockMovementTag =
		FGameplayTag::RequestGameplayTag(TEXT("Combat.State.BlockMovement"));
	const FGameplayTag InvincibleTag =
		FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Invincible"));
	const FGameplayTag ComboWindowTag =
		FGameplayTag::RequestGameplayTag(TEXT("Combat.State.ComboWindowOpen"));

	auto AddTransition = [ComboData](
		FName ID, FName Source, const FGameplayTag& InputTag,
		TSubclassOf<UGameplayAbility> AbilityClass, FName Target)
	{
		FComboTransition& Transition = ComboData->Transitions.AddDefaulted_GetRef();
		Transition.TransitionID = ID;
		Transition.SourceState = Source;
		Transition.InputTag = InputTag;
		Transition.AbilityClass = AbilityClass;
		Transition.TargetState = Target;
		return &Transition;
	};

	AddTransition(TEXT("IdleToA"), TEXT("Idle"), YTag,
		UMHGZM1PlaceholderActionA::StaticClass(), TEXT("A"));
	FComboTransition* AToB = AddTransition(TEXT("AToB"), TEXT("A"), BTag,
		UMHGZM1PlaceholderActionB::StaticClass(), TEXT("B"));
	AToB->GrantedTags.AddTag(BlockMovementTag);
	FComboTransition* BToB2 = AddTransition(TEXT("BToB2"), TEXT("B"), YTag,
		UMHGZM1PlaceholderActionB::StaticClass(), TEXT("B2"));
	BToB2->GrantedTags.AddTag(BlockMovementTag);
	BToB2->GrantedTags.AddTag(InvincibleTag);
	FComboTransition* AutoGate = AddTransition(TEXT("B2AutoGate"), TEXT("B2"), FGameplayTag(),
		nullptr, TEXT("B2"));
	AutoGate->bAutoTransition = true;
	AutoGate->ExecutionPolicy = EComboExecutionPolicy::StateOnly;
	AutoGate->bRequiresComboWindow = true;
	AutoGate->RequiredTags.AddTag(InvincibleTag);
	AutoGate->GrantedTags.AddTag(BlockMovementTag);
	AutoGate->GrantedTags.AddTag(InvincibleTag);
	AddTransition(TEXT("TryActivateFailure"), TEXT("B2"), BTag,
		UMHGZM1PlaceholderActionA::StaticClass(), TEXT("Broken"));
	AddTransition(TEXT("CommitFailure"), TEXT("B2"), YBTag,
		UMHGZM1CommitFailureAbility::StaticClass(), TEXT("Broken"));
	Coordinator->InjectComboData(ComboData);

	auto SendStarted = [Coordinator](const FGameplayTag& InputTag, uint32 SequenceID)
	{
		FWeaponInputSnapshot Snapshot;
		Snapshot.ResolvedInputTag = InputTag;
		Snapshot.SourceControlTag = InputTag;
		Snapshot.SequenceID = SequenceID;
		Snapshot.Phase = EWeaponInputPhase::Started;
		Coordinator->HandleWeaponInput(Snapshot);
	};

	SendStarted(YTag, 1);
	TestEqual(TEXT("Idle -> A"), Coordinator->GetCurrentState(), FName(TEXT("A")));
	TestTrue(TEXT("A becomes active"), Coordinator->GetActiveTransition().IsSet());
	UMHGZGameplayAbility* FirstActionA = Cast<UMHGZGameplayAbility>(
		Coordinator->GetActiveTransition()->ActionToken.AbilityInstance.Get());
	TestNotNull(TEXT("A has a concrete PerExecution instance"), FirstActionA);
	if (FirstActionA)
	{
		FirstActionA->RequestEndAction(EWeaponActionEndReason::Normal);
	}
	TestEqual(TEXT("a non-attack action ending normally returns to Idle"),
		Coordinator->GetCurrentState(), FName(TEXT("Idle")));
	TestFalse(TEXT("normal action end clears ActiveTransition"),
		Coordinator->GetActiveTransition().IsSet());

	SendStarted(YTag, 2);
	TestEqual(TEXT("Idle can enter A again"),
		Coordinator->GetCurrentState(), FName(TEXT("A")));
	const FWeaponActionToken ActionAToken = Coordinator->GetActiveTransition()->ActionToken;
	UMHGZGameplayAbility* ActionA = Cast<UMHGZGameplayAbility>(ActionAToken.AbilityInstance.Get());
	TestTrue(TEXT("A re-entry uses another PerExecution instance"),
		ActionA && ActionA != FirstActionA);

	SendStarted(BTag, 3);
	TestEqual(TEXT("A -> B"), Coordinator->GetCurrentState(), FName(TEXT("B")));
	TestEqual(TEXT("A ends as Superseded"), ActionA ? ActionA->GetActionEndReason()
		: EWeaponActionEndReason::Normal, EWeaponActionEndReason::Superseded);
	const FWeaponActionToken FirstBToken = Coordinator->GetActiveTransition()->ActionToken;
	UMHGZGameplayAbility* FirstB =
		Cast<UMHGZGameplayAbility>(FirstBToken.AbilityInstance.Get());
	TestEqual(TEXT("B transition owns exactly one BlockMovement tag"),
		ASC->GetTagCount(BlockMovementTag), 1);

	USkeletalMeshComponent* Mesh = Character->GetMesh();
	TestTrue(TEXT("first B montage identity registers"),
		Host->RegisterMontage(FirstBToken, Mesh, 200));
	SendStarted(YTag, 4);
	TestEqual(TEXT("same-class B re-entry reaches B2"),
		Coordinator->GetCurrentState(), FName(TEXT("B2")));
	const FWeaponActionToken SecondBToken = Coordinator->GetActiveTransition()->ActionToken;
	UMHGZGameplayAbility* SecondB =
		Cast<UMHGZGameplayAbility>(SecondBToken.AbilityInstance.Get());
	TestTrue(TEXT("same-class re-entry uses a new ability instance"),
		FirstB && SecondB && FirstB != SecondB);
	TestEqual(TEXT("first B ends as Superseded"), FirstB ? FirstB->GetActionEndReason()
		: EWeaponActionEndReason::Normal, EWeaponActionEndReason::Superseded);

	FWeaponActionToken Resolved;
	TestFalse(TEXT("superseded montage identity is removed"),
		Host->ResolveMontage(Mesh, 200, Resolved));
	TestTrue(TEXT("second B montage identity registers"),
		Host->RegisterMontage(SecondBToken, Mesh, 201));
	TestTrue(TEXT("notify identity resolves only to second B"),
		Host->ResolveMontage(Mesh, 201, Resolved));
	TestEqual(TEXT("resolved notify belongs to second B"), Resolved, SecondBToken);

	Coordinator->OnActionFinished(FirstBToken, EWeaponActionEndReason::Normal);
	TestEqual(TEXT("late callback from superseded B cannot reset B2"),
		Coordinator->GetCurrentState(), FName(TEXT("B2")));

	TestTrue(TEXT("first overlapping combo window opens"),
		Coordinator->OpenComboWindow(SecondBToken, TEXT("Window.One")));
	TestTrue(TEXT("second overlapping combo window opens"),
		Coordinator->OpenComboWindow(SecondBToken, TEXT("Window.Two")));
	TestEqual(TEXT("overlapping windows produce ledger count two"),
		ASC->GetTagCount(ComboWindowTag), 2);
	Coordinator->CloseComboWindow(SecondBToken, TEXT("Window.One"));
	TestEqual(TEXT("closing one window leaves the other effective"),
		ASC->GetTagCount(ComboWindowTag), 1);
	Coordinator->CloseComboWindow(SecondBToken, TEXT("Window.Two"));
	TestEqual(TEXT("closing both windows clears the tag"),
		ASC->GetTagCount(ComboWindowTag), 0);
	TestFalse(TEXT("auto transition obeys its combo-window requirement"),
		Coordinator->OnAutoTransition(TEXT("B2AutoGate"), SecondBToken));
	TestTrue(TEXT("auto-gate window opens"),
		Coordinator->OpenComboWindow(SecondBToken, TEXT("Window.AutoGate")));
	TestTrue(TEXT("auto transition passes the shared requirements when open"),
		Coordinator->OnAutoTransition(TEXT("B2AutoGate"), SecondBToken));
	TestEqual(TEXT("state-only auto transition keeps B2"),
		Coordinator->GetCurrentState(), FName(TEXT("B2")));
	TestEqual(TEXT("state-only tag handoff keeps one BlockMovement owner"),
		ASC->GetTagCount(BlockMovementTag), 1);
	TestEqual(TEXT("state-only tag handoff keeps one Invincible owner"),
		ASC->GetTagCount(InvincibleTag), 1);
	Coordinator->CloseComboWindow(SecondBToken, TEXT("Window.AutoGate"));

	// B2 owns Invincible, and ActionA blocks on it: TryActivateAbility must fail
	// without changing the active FSM state or leaking its prepared context.
	SendStarted(BTag, 5);
	TestEqual(TEXT("TryActivate failure preserves B2"),
		Coordinator->GetCurrentState(), FName(TEXT("B2")));
	TestEqual(TEXT("TryActivate failure preserves active transition tags"),
		ASC->GetTagCount(InvincibleTag), 1);
	FWeaponAbilityActivationContext LeakedContext;
	TestFalse(TEXT("TryActivate failure discards prepared activation context"),
		ASC->ConsumePendingActivationContext(ActionAHandle, LeakedContext));

	// This action reserves one probe resource then deterministically fails Commit.
	SendStarted(YBTag, 6);
	TestEqual(TEXT("Commit failure preserves B2"),
		Coordinator->GetCurrentState(), FName(TEXT("B2")));
	TestEqual(TEXT("Commit failure releases its reservation exactly once"),
		Probe->GetReleaseCount(), 1);
	TestEqual(TEXT("Commit failure consumes no reservation"),
		Probe->GetConsumeCount(), 0);
	TestEqual(TEXT("Commit failure leaves no outstanding reservation"),
		Probe->GetOutstandingReservationCount(), 0);
	TestFalse(TEXT("Commit failure consumed its one-shot activation context"),
		ASC->ConsumePendingActivationContext(CommitFailureHandle, LeakedContext));
	TestEqual(TEXT("Commit failure grants no extra transition tag"),
		ASC->GetTagCount(BlockMovementTag), 1);

	FHitResult LandingHit;
	Coordinator->OnLanded(LandingHit);
	TestEqual(TEXT("landing resets the default policy to Idle"),
		Coordinator->GetCurrentState(), FName(TEXT("Idle")));
	TestFalse(TEXT("landing clears the active transition"),
		Coordinator->GetActiveTransition().IsSet());
	TestEqual(TEXT("landing ends the exact active action"),
		SecondB ? SecondB->GetActionEndReason() : EWeaponActionEndReason::Normal,
		EWeaponActionEndReason::Landed);
	TestEqual(TEXT("landing releases transition BlockMovement"),
		ASC->GetTagCount(BlockMovementTag), 0);
	TestEqual(TEXT("landing releases transition Invincible"),
		ASC->GetTagCount(InvincibleTag), 0);
	TestFalse(TEXT("landing unregisters second B montage identity"),
		Host->ResolveMontage(Mesh, 201, Resolved));

	ASC->CancelAllAbilities();
	Host->ShutdownRuntime(EWeaponRuntimeEndReason::RuntimeShutdown);
	DestroyM1TestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM1BaseAbilityPolicy,
	"MHGZ.M1.GameplayAbility.BaseInstancingAndNetPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM1BaseAbilityPolicy::RunTest(const FString& Parameters)
{
	const UMHGZDodgeAbility* Dodge = GetDefault<UMHGZDodgeAbility>();
	TestTrue(TEXT("base ability is InstancedPerExecution"),
		Dodge->GetInstancingPolicy() == EGameplayAbilityInstancingPolicy::InstancedPerExecution);
	TestTrue(TEXT("base ability is LocalOnly"),
		Dodge->GetNetExecutionPolicy() == EGameplayAbilityNetExecutionPolicy::LocalOnly);
	TestEqual(TEXT("dodge retains its InputTag"),
		Dodge->InputTag, FGameplayTag::RequestGameplayTag(TEXT("Input.Dodge")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM1ReservationDefaults,
	"MHGZ.M1.ResourceComponent.ReservationDefaultsRejectNonEmpty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM1ReservationDefaults::RunTest(const FString& Parameters)
{
	URes_InsectGlaive* Resource = NewObject<URes_InsectGlaive>();

	TArray<FWeaponResourceCostSpec> EmptySpecs;
	TArray<FWeaponResourceCostSpec> NonEmptySpecs;
	FWeaponResourceCostSpec CostSpec;
	NonEmptySpecs.Add(CostSpec);

	TestTrue(TEXT("empty specs are reservable"), Resource->CanReserveCosts(EmptySpecs));
	TestFalse(TEXT("non-empty specs are rejected by the default"),
		Resource->CanReserveCosts(NonEmptySpecs));

	FWeaponActionToken ActionToken;
	FWeaponResourceCostReservation Reservation;
	TestTrue(TEXT("empty reserve succeeds"),
		Resource->TryReserveCosts(ActionToken, EmptySpecs, Reservation));
	TestFalse(TEXT("empty reserve yields an invalid reservation"), Reservation.IsValid());
	TestFalse(TEXT("non-empty reserve fails"),
		Resource->TryReserveCosts(ActionToken, NonEmptySpecs, Reservation));

	// 默认 Release/Consume 无副作用，不应崩溃。
	Resource->ReleaseReservation(Reservation);
	Resource->ConsumeReservedCosts(Reservation);
	return true;
}

#endif
