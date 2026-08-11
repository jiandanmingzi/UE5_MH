// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Abilities/GameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "ActionSystem/MHGZComboCoordinatorAbility.h"
#include "ActionSystem/MHGZM1PlaceholderAbilities.h"
#include "ActionSystem/MHGZWeaponComboData.h"
#include "AttributeSystem/MHGZWeaponResourceComponent.h"
#include "AttributeSystem/Res_InsectGlaive.h"
#include "Engine/World.h"
#include "Equipment/MHGZEquipmentComponent.h"
#include "Equipment/MHGZEquipmentDefinition.h"
#include "Equipment/MHGZEquipmentInstance.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InsectGlaive/InsectGlaiveCombatConfig.h"
#include "MHGZPlayerState.h"
#include "WeaponRuntime/MHGZWeaponRuntimeDefinition.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"
#include "WeaponRuntime/MHGZWeaponRuntimeTypes.h"

namespace
{
UWorld* CreateM2TestWorld()
{
	return UWorld::CreateWorld(EWorldType::Game, false);
}

void DestroyM2TestWorld(UWorld* World)
{
	if (World)
	{
		World->DestroyWorld(false);
	}
}

/** 记录 OnWeaponRuntimeInvalidated 广播的探针（原生委托可用 AddRaw）。 */
struct FInvalidationProbe
{
	int32 Count = 0;
	FWeaponRuntimeToken LastToken;

	void OnInvalidated(const FWeaponRuntimeToken& Token)
	{
		++Count;
		LastToken = Token;
	}
};

/** 一条完整武器链：WeaponDefinition -> RuntimeDefinition -> CombatConfig -> ComboData。 */
struct FTestWeaponChain
{
	UMHGZWeaponDefinition* Definition = nullptr;
	UWeaponRuntimeDefinition* RuntimeDefinition = nullptr;
	UInsectGlaiveCombatConfig* CombatConfig = nullptr;
	UMHGZWeaponComboData* ComboData = nullptr;

	void Build(UObject* Outer, bool bWithAbility)
	{
		Definition = NewObject<UMHGZWeaponDefinition>(Outer);
		Definition->ItemID = FName(*FString::Printf(TEXT("TestWeapon_%p"), (void*)Definition));
		RuntimeDefinition = NewObject<UWeaponRuntimeDefinition>(Outer);
		RuntimeDefinition->ResourceComponentClass = URes_InsectGlaive::StaticClass();
		CombatConfig = NewObject<UInsectGlaiveCombatConfig>(Outer);
		ComboData = NewObject<UMHGZWeaponComboData>(Outer);
		CombatConfig->ComboData = ComboData;
		RuntimeDefinition->CombatConfig = CombatConfig;
		Definition->RuntimeDefinition = RuntimeDefinition;

		if (bWithAbility)
		{
			FComboTransition& Transition = ComboData->Transitions.AddDefaulted_GetRef();
			Transition.TransitionID = TEXT("IdleToA");
			Transition.SourceState = TEXT("Idle");
			Transition.TargetState = TEXT("A");
			Transition.AbilityClass = UMHGZM1PlaceholderActionA::StaticClass();
		}
	}

	void BuildWithNullCombatConfig(UObject* Outer)
	{
		Definition = NewObject<UMHGZWeaponDefinition>(Outer);
		Definition->ItemID = FName(*FString::Printf(TEXT("TestWeapon_%p"), (void*)Definition));
		RuntimeDefinition = NewObject<UWeaponRuntimeDefinition>(Outer);
		RuntimeDefinition->ResourceComponentClass = URes_InsectGlaive::StaticClass();
		Definition->RuntimeDefinition = RuntimeDefinition;
	}
};

struct FTestHarness
{
	UWorld* World = nullptr;
	ACharacter* Character = nullptr;
	AMHGZPlayerState* PlayerState = nullptr;
	UMHGZAbilitySystemComponent* ASC = nullptr;
	UMHGZEquipmentComponent* Equipment = nullptr;
	UMHGZWeaponRuntimeHostComponent* Host = nullptr;
	FGameplayTag WeaponSlot;

	bool Setup()
	{
		World = CreateM2TestWorld();
		if (!World)
		{
			return false;
		}

		Character = World->SpawnActor<ACharacter>(
			ACharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
		if (!Character)
		{
			return false;
		}

		PlayerState = World->SpawnActor<AMHGZPlayerState>(
			AMHGZPlayerState::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
		if (!PlayerState)
		{
			return false;
		}

		ASC = PlayerState->GetMHGZAbilitySystemComponent();
		if (!ASC)
		{
			return false;
		}
		ASC->InitAbilityActorInfo(PlayerState, Character);

		Equipment = PlayerState->GetEquipmentComponent();
		if (!Equipment)
		{
			return false;
		}

		Host = NewObject<UMHGZWeaponRuntimeHostComponent>(Character);
		Character->AddInstanceComponent(Host);
		Host->RegisterComponent();
		Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);

		WeaponSlot = FGameplayTag::RequestGameplayTag(TEXT("Equipment.Slot.Weapon"));
		return WeaponSlot.IsValid();
	}

	void Teardown()
	{
		if (Host)
		{
			Host->ShutdownRuntime(EWeaponRuntimeEndReason::RuntimeShutdown);
		}
		DestroyM2TestWorld(World);
		World = nullptr;
	}

	UMHGZEquipmentInstance* EquipChain(FTestWeaponChain& Chain, const FGameplayTag& Slot)
	{
		UMHGZEquipmentInstance* Instance =
			UMHGZEquipmentInstance::CreateEquipmentInstance(PlayerState, Chain.Definition);
		Equipment->EquipItem(Slot, Instance);
		return Instance;
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM2SameSnapshotNoOp,
	"MHGZ.M2.Equipment.SameWeaponArmorSocketAreNoOp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM2SameSnapshotNoOp::RunTest(const FString& Parameters)
{
	FTestHarness Harness;
	if (!Harness.Setup())
	{
		return false;
	}

	// M1 语义保留：无装备 InitializePawnRuntime 仍产生有效 Host Token。
	Harness.Host->InitializePawnRuntime(
		Harness.Character, nullptr, Harness.ASC, Harness.Equipment);
	TestTrue(TEXT("empty-equipment init still yields a valid token"),
		Harness.Host->GetCurrentToken().IsValid());
	const uint64 InitialGeneration = Harness.Host->GetCurrentToken().Generation;
	const FWeaponRuntimeToken InitialToken = Harness.Host->GetCurrentToken();

	const FEquippedWeaponSnapshot EmptySnapshot = Harness.Equipment->GetEquippedWeaponSnapshot();
	TestEqual(TEXT("no weapon yet: revision is 0"), EmptySnapshot.WeaponRevision, int64(0));
	TestFalse(TEXT("no weapon yet: instance is empty"), EmptySnapshot.EquipmentInstance.IsValid());

	// 事件计数探针：原生委托支持 AddLambda。
	int32 WeaponChangedCount = 0;
	int32 StatsChangedCount = 0;
	Harness.Equipment->OnEquippedWeaponChanged.AddLambda(
		[&WeaponChangedCount](const FEquippedWeaponSnapshot&) { ++WeaponChangedCount; });
	Harness.Equipment->OnEquipmentStatsChanged.AddLambda(
		[&StatsChangedCount]() { ++StatsChangedCount; });

	FInvalidationProbe Probe;
	Harness.Host->OnWeaponRuntimeInvalidated.AddRaw(
		&Probe, &FInvalidationProbe::OnInvalidated);

	// 武器 A：实例 + 完整运行时链（含一个 AbilityClass 的 ComboData）。
	FTestWeaponChain ChainA;
	ChainA.Build(Harness.PlayerState, /*bWithAbility=*/true);
	UMHGZEquipmentInstance* InstanceA = Harness.EquipChain(ChainA, Harness.WeaponSlot);
	TestNotNull(TEXT("weapon A instance equipped"), InstanceA);
	TestEqual(TEXT("weapon A changed the snapshot"), WeaponChangedCount, 1);
	TestEqual(TEXT("weapon A fired stats recalc"), StatsChangedCount, 1);

	const FEquippedWeaponSnapshot SnapshotA = Harness.Equipment->GetEquippedWeaponSnapshot();
	TestEqual(TEXT("weapon A snapshot revision is 1"), SnapshotA.WeaponRevision, int64(1));
	TestEqual(TEXT("snapshot instance is A"), SnapshotA.EquipmentInstance.Get(), InstanceA);
	TestEqual(TEXT("snapshot weapon definition is A"),
		SnapshotA.WeaponDefinition.Get(), ChainA.Definition);
	TestEqual(TEXT("snapshot runtime definition is A"),
		SnapshotA.RuntimeDefinition.Get(), ChainA.RuntimeDefinition);
	TestEqual(TEXT("host context weapon definition is A"),
		Harness.Host->GetCurrentContext().WeaponDefinition.Get(), ChainA.Definition);

	URes_InsectGlaive* ResourceA = Harness.Character->FindComponentByClass<URes_InsectGlaive>();
	TestNotNull(TEXT("host created the IG resource"), ResourceA);
	UGA_WeaponComboCoordinator* CoordinatorA = Harness.ASC->GetActiveComboCoordinator();
	TestNotNull(TEXT("host activated the combo coordinator"), CoordinatorA);
	TestTrue(TEXT("weapon A ability was granted"),
		Harness.ASC->FindWeaponAbilityHandle(UMHGZM1PlaceholderActionA::StaticClass()).IsValid());
	const uint64 GenerationAfterA = Harness.Host->GetCurrentToken().Generation;
	const FWeaponRuntimeToken TokenAfterA = Harness.Host->GetCurrentToken();
	TestEqual(TEXT("first equip invalidated the empty runtime"), Probe.Count, 1);
	TestTrue(TEXT("first equip invalidated the initial token"),
		Probe.LastToken == InitialToken);

	// 重复装备同一武器实例：完全 no-op。
	Harness.Equipment->EquipItem(Harness.WeaponSlot, InstanceA);
	TestEqual(TEXT("same instance keeps revision 1"),
		Harness.Equipment->GetEquippedWeaponSnapshot().WeaponRevision, int64(1));
	TestEqual(TEXT("same instance fires no weapon event"), WeaponChangedCount, 1);
	TestEqual(TEXT("same instance still fires stats recalc"), StatsChangedCount, 2);
	TestEqual(TEXT("same instance keeps generation"),
		Harness.Host->GetCurrentToken().Generation, GenerationAfterA);
	TestEqual(TEXT("same instance keeps the resource"),
		Harness.Character->FindComponentByClass<URes_InsectGlaive>(), ResourceA);
	TestEqual(TEXT("same instance keeps the coordinator"),
		Harness.ASC->GetActiveComboCoordinator(), CoordinatorA);
	TestEqual(TEXT("same instance invalidates nothing"), Probe.Count, 1);

	// 护甲（非武器槽）：不触发武器事件。
	UMHGZEquipmentDefinition* ArmorDef = NewObject<UMHGZEquipmentDefinition>(Harness.PlayerState);
	ArmorDef->ItemID = TEXT("TestArmor");
	UMHGZEquipmentInstance* ArmorInstance =
		UMHGZEquipmentInstance::CreateEquipmentInstance(Harness.PlayerState, ArmorDef);
	Harness.Equipment->EquipItem(
		FGameplayTag::RequestGameplayTag(TEXT("Equipment.Slot.Armor")), ArmorInstance);
	TestEqual(TEXT("armor keeps revision 1"),
		Harness.Equipment->GetEquippedWeaponSnapshot().WeaponRevision, int64(1));
	TestEqual(TEXT("armor fires no weapon event"), WeaponChangedCount, 1);
	TestEqual(TEXT("armor fires stats recalc"), StatsChangedCount, 3);
	TestEqual(TEXT("armor keeps generation"),
		Harness.Host->GetCurrentToken().Generation, GenerationAfterA);
	TestEqual(TEXT("armor keeps the resource"),
		Harness.Character->FindComponentByClass<URes_InsectGlaive>(), ResourceA);
	TestEqual(TEXT("armor invalidates nothing"), Probe.Count, 1);

	// 镶嵌饰品（挂在武器实例上）：不触发武器事件。
	ChainA.Definition->Sockets.Add({ TEXT("Slot_01"), 1 });
	UMHGZEquipmentDefinition* AccDef = NewObject<UMHGZEquipmentDefinition>(Harness.PlayerState);
	AccDef->ItemID = TEXT("TestAccessory");
	AccDef->RarityLevel = 1;
	UMHGZEquipmentInstance* AccInstance =
		UMHGZEquipmentInstance::CreateEquipmentInstance(Harness.PlayerState, AccDef);
	Harness.Equipment->SocketAccessory(InstanceA, AccInstance, TEXT("Slot_01"));
	TestEqual(TEXT("socket keeps revision 1"),
		Harness.Equipment->GetEquippedWeaponSnapshot().WeaponRevision, int64(1));
	TestEqual(TEXT("socket fires no weapon event"), WeaponChangedCount, 1);
	TestEqual(TEXT("socket fires stats recalc"), StatsChangedCount, 4);
	TestEqual(TEXT("socket keeps generation"),
		Harness.Host->GetCurrentToken().Generation, GenerationAfterA);
	TestEqual(TEXT("socket keeps the resource"),
		Harness.Character->FindComponentByClass<URes_InsectGlaive>(), ResourceA);
	TestEqual(TEXT("socket keeps the coordinator"),
		Harness.ASC->GetActiveComboCoordinator(), CoordinatorA);
	TestEqual(TEXT("socket invalidates nothing"), Probe.Count, 1);

	// 拆除饰品：同样 no-op。
	Harness.Equipment->RemoveAccessory(InstanceA, TEXT("Slot_01"));
	TestEqual(TEXT("remove socket keeps revision 1"),
		Harness.Equipment->GetEquippedWeaponSnapshot().WeaponRevision, int64(1));
	TestEqual(TEXT("remove socket fires no weapon event"), WeaponChangedCount, 1);
	TestEqual(TEXT("remove socket keeps generation"),
		Harness.Host->GetCurrentToken().Generation, GenerationAfterA);
	TestEqual(TEXT("remove socket keeps the resource"),
		Harness.Character->FindComponentByClass<URes_InsectGlaive>(), ResourceA);

	// 同武器但换新实例（同 RuntimeDefinition）：实例身份变化 → 允许重建（身份判定以实例为准）。
	UMHGZEquipmentInstance* InstanceA2 =
		UMHGZEquipmentInstance::CreateEquipmentInstance(Harness.PlayerState, ChainA.Definition);
	Harness.Equipment->EquipItem(Harness.WeaponSlot, InstanceA2);
	TestEqual(TEXT("new instance of same weapon bumps revision"),
		Harness.Equipment->GetEquippedWeaponSnapshot().WeaponRevision, int64(2));
	TestEqual(TEXT("new instance fires the weapon event"), WeaponChangedCount, 2);
	TestTrue(TEXT("new instance increments host generation"),
		Harness.Host->GetCurrentToken().Generation > GenerationAfterA);
	TestFalse(TEXT("old token is stale after instance swap"),
		Harness.Host->IsTokenCurrent(TokenAfterA));
	TestEqual(TEXT("instance swap invalidated the old runtime"), Probe.Count, 2);

	Harness.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM2WeaponSwapCleanup,
	"MHGZ.M2.RuntimeHost.WeaponSwapRebuildsAndCleans",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM2WeaponSwapCleanup::RunTest(const FString& Parameters)
{
	FTestHarness Harness;
	if (!Harness.Setup())
	{
		return false;
	}
	Harness.Host->InitializePawnRuntime(
		Harness.Character, nullptr, Harness.ASC, Harness.Equipment);

	FInvalidationProbe Probe;
	Harness.Host->OnWeaponRuntimeInvalidated.AddRaw(
		&Probe, &FInvalidationProbe::OnInvalidated);

	// 武器 A：带一个 Ability 的完整运行时。
	FTestWeaponChain ChainA;
	ChainA.Build(Harness.PlayerState, /*bWithAbility=*/true);
	Harness.EquipChain(ChainA, Harness.WeaponSlot);
	const FWeaponRuntimeToken TokenA = Harness.Host->GetCurrentToken();
	const uint64 GenerationA = TokenA.Generation;
	URes_InsectGlaive* ResourceA = Harness.Character->FindComponentByClass<URes_InsectGlaive>();
	UGA_WeaponComboCoordinator* CoordinatorA = Harness.ASC->GetActiveComboCoordinator();
	TestNotNull(TEXT("resource A exists"), ResourceA);
	TestNotNull(TEXT("coordinator A exists"), CoordinatorA);
	TestEqual(TEXT("empty -> A invalidates the initial runtime"), Probe.Count, 1);
	const FGameplayTag KinsectActiveTag = FGameplayTag::RequestGameplayTag(
		TEXT("WeaponResource.IG.Kinsect.Active"));
	Harness.ASC->SetLooseGameplayTagCount(KinsectActiveTag, 2);
	TestTrue(TEXT("test precondition: old kinsect active tag exists"),
		Harness.ASC->HasMatchingGameplayTag(KinsectActiveTag));

	// 武器 B：另一个 RuntimeDefinition，ComboData 无任何 Ability。
	FTestWeaponChain ChainB;
	ChainB.Build(Harness.PlayerState, /*bWithAbility=*/false);
	Harness.EquipChain(ChainB, Harness.WeaponSlot);

	TestEqual(TEXT("swap bumps revision to 2"),
		Harness.Equipment->GetEquippedWeaponSnapshot().WeaponRevision, int64(2));
	TestTrue(TEXT("swap increments generation"),
		Harness.Host->GetCurrentToken().Generation > GenerationA);
	TestFalse(TEXT("old token is stale after swap"), Harness.Host->IsTokenCurrent(TokenA));
	TestEqual(TEXT("swap invalidates exactly the old token"), Probe.Count, 2);
	TestTrue(TEXT("invalidated token is token A"), Probe.LastToken == TokenA);
	TestFalse(TEXT("weapon swap clears every old kinsect active loose-tag count"),
		Harness.ASC->HasMatchingGameplayTag(KinsectActiveTag));
	TestEqual(TEXT("host context now points to weapon B"),
		Harness.Host->GetCurrentContext().WeaponDefinition.Get(), ChainB.Definition);

	// 旧 Resource 已销毁，新 Resource 已创建（同一类，不同实例）。
	TestFalse(TEXT("resource A was destroyed"), IsValid(ResourceA));
	URes_InsectGlaive* ResourceB = Harness.Character->FindComponentByClass<URes_InsectGlaive>();
	TestNotNull(TEXT("resource B exists"), ResourceB);
	TestTrue(TEXT("resource B is a fresh instance"), ResourceB != ResourceA);
	TestTrue(TEXT("resource B exposes the current runtime context"),
		ResourceB->GetRuntimeContext().RuntimeToken == Harness.Host->GetCurrentToken());
	const FWeaponRuntimeToken TokenB = Harness.Host->GetCurrentToken();

	// 旧协调器已取消，新协调器已激活。
	UGA_WeaponComboCoordinator* CoordinatorB = Harness.ASC->GetActiveComboCoordinator();
	TestNotNull(TEXT("coordinator B exists"), CoordinatorB);
	TestTrue(TEXT("coordinator B is a fresh instance"), CoordinatorB != CoordinatorA);
	TestFalse(TEXT("old coordinator is no longer active"), CoordinatorA->IsActive());

	// B 的 ComboData 无 Ability → 旧武器 Ability 已移除。
	TestFalse(TEXT("weapon A ability was removed after swap"),
		Harness.ASC->FindWeaponAbilityHandle(UMHGZM1PlaceholderActionA::StaticClass()).IsValid());

	// 卸下武器：真实身份变化（B → 空），完整清理且 Host 保持初始化。
	Harness.Equipment->UnequipItem(Harness.WeaponSlot);
	TestEqual(TEXT("unequip bumps revision to 3"),
		Harness.Equipment->GetEquippedWeaponSnapshot().WeaponRevision, int64(3));
	TestFalse(TEXT("unequip leaves the snapshot empty"),
		Harness.Equipment->GetEquippedWeaponSnapshot().EquipmentInstance.IsValid());
	TestEqual(TEXT("unequip invalidates token B"), Probe.Count, 3);
	TestTrue(TEXT("invalidated token is token B"), Probe.LastToken == TokenB);
	TestNull(TEXT("resource destroyed on unequip"),
		Harness.Character->FindComponentByClass<URes_InsectGlaive>());
	TestNull(TEXT("coordinator cleared on unequip"),
		Harness.ASC->GetActiveComboCoordinator());
	TestNull(TEXT("context weapon definition cleared on unequip"),
		Harness.Host->GetCurrentContext().WeaponDefinition.Get());
	TestTrue(TEXT("host remains initialized without a weapon"),
		Harness.Host->IsRuntimeInitialized());
	TestTrue(TEXT("empty-runtime token stays valid and current"),
		Harness.Host->IsTokenCurrent(Harness.Host->GetCurrentToken()));

	// UnPossessed / EndPlay 双重调用幂等。
	Harness.Host->ShutdownRuntime(EWeaponRuntimeEndReason::RuntimeShutdown);
	Harness.Host->ShutdownRuntime(EWeaponRuntimeEndReason::EndPlay);
	TestFalse(TEXT("host is shut down"), Harness.Host->IsRuntimeInitialized());
	TestFalse(TEXT("token is stale after terminal shutdown"),
		Harness.Host->IsTokenCurrent(Harness.Host->GetCurrentToken()));

	Harness.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM2EmptyRuntimeSafeEnd,
	"MHGZ.M2.RuntimeHost.EmptyRuntimeDefinitionSafeEnd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM2EmptyRuntimeSafeEnd::RunTest(const FString& Parameters)
{
	FTestHarness Harness;
	if (!Harness.Setup())
	{
		return false;
	}
	Harness.Host->InitializePawnRuntime(
		Harness.Character, nullptr, Harness.ASC, Harness.Equipment);

	// 1) RuntimeDefinition 为空的武器定义：安全结束。
	UMHGZWeaponDefinition* BareWeaponDef = NewObject<UMHGZWeaponDefinition>(Harness.PlayerState);
	BareWeaponDef->ItemID = TEXT("BareWeapon");
	UMHGZEquipmentInstance* BareInstance =
		UMHGZEquipmentInstance::CreateEquipmentInstance(Harness.PlayerState, BareWeaponDef);
	Harness.Equipment->EquipItem(Harness.WeaponSlot, BareInstance);
	TestNull(TEXT("null RuntimeDefinition creates no resource"),
		Harness.Character->FindComponentByClass<URes_InsectGlaive>());
	TestNull(TEXT("null RuntimeDefinition activates no coordinator"),
		Harness.ASC->GetActiveComboCoordinator());
	TestTrue(TEXT("null RuntimeDefinition keeps host initialized"),
		Harness.Host->IsRuntimeInitialized());

	// 2) CombatConfig 为空：安全结束。
	FTestWeaponChain NullCombatChain;
	NullCombatChain.BuildWithNullCombatConfig(Harness.PlayerState);
	Harness.EquipChain(NullCombatChain, Harness.WeaponSlot);
	TestNull(TEXT("null CombatConfig creates no resource"),
		Harness.Character->FindComponentByClass<URes_InsectGlaive>());
	TestNull(TEXT("null CombatConfig activates no coordinator"),
		Harness.ASC->GetActiveComboCoordinator());
	TestTrue(TEXT("null CombatConfig keeps host initialized"),
		Harness.Host->IsRuntimeInitialized());

	// 3) CombatConfig 存在但 ComboData 为空：安全结束。
	FTestWeaponChain NullComboChain;
	NullComboChain.Build(Harness.PlayerState, /*bWithAbility=*/false);
	NullComboChain.CombatConfig->ComboData = nullptr;
	Harness.EquipChain(NullComboChain, Harness.WeaponSlot);
	TestNull(TEXT("null ComboData creates no resource"),
		Harness.Character->FindComponentByClass<URes_InsectGlaive>());
	TestNull(TEXT("null ComboData activates no coordinator"),
		Harness.ASC->GetActiveComboCoordinator());
	TestTrue(TEXT("null ComboData keeps host initialized"),
		Harness.Host->IsRuntimeInitialized());

	Harness.Teardown();
	return true;
}

#endif
