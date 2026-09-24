// Copyright MHGZ Project. All Rights Reserved.
//
// 空中回避（M5 阶段 D 第 1 招）的四类断言：
//   1. 输入路由四态 —— A 单独在空/地各自出什么、RT 先按还出不出撑杆跳、
//      「先按 A 再补 RT」为什么不再出撑杆跳（那是刻意的代价，钉住它）。
//   2. 弹道请求形状 —— 真值代入后的初速/方向/参数组。
//   3. CantDodge 预算 —— acquire 幂等、落地才释放（能力结束不退款这条靠
//      「Token 由 Host 持有」保证，测试只能钉住可观测的那一半）。
//   4. 「空中可操作」(Combat.State.Aerial.Falling) 的领放生命周期 —— 最早可操作帧
//      的锁 = 它的**缺席**（空中动作 lead 里被 Detect 放掉）。（空回没有预输入：
//      锁定期按下直接作废，2026-09-22 用户拍板。）

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "ActionSystem/MHGZAirDodgeAbility.h"
#include "ActionSystem/AbilityTask_MHGZPlayMontageAndWait.h"
#include "ActionSystem/MHGZComboCoordinatorAbility.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/MHGZMotionMatchingAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/GameInstance.h"
#include "EngineGlobals.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "GameplayTasksComponent.h"
#include "HAL/IConsoleManager.h"
#include "InsectGlaive/InsectGlaiveCombatConfig.h"
#include "InputSystem/MHGZWeaponInputRouterComponent.h"
#include "MHGZCharacter.h"
#include "MHGZPlayerState.h"
#include "MHGZM3TestTypes.h"
#include "MHGZM5TestTypes.h"
#include "Movement/MHGZInstrumentedCharacterMovementComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "WeaponRuntime/MHGZWeaponInputProfile.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

namespace
{
FGameplayTag AirDodgeTag(const TCHAR* Name)
{
	return FGameplayTag::RequestGameplayTag(Name);
}

/**
 * 与 DA_IG_InputProfile 现状同构的一条 Chord：单 trigger、无修饰键、按上下文分流。
 * `bRequireExactModifiers = false` 是为了保住「按住 LT/RT 时 A 仍然出闪避」——
 * 直出路径从不看修饰键，Chord 中介不该把它变成一条新行为。
 */
FWeaponChordDefinition& AddAirDodgeChord(UWeaponInputProfile* Profile,
	const TCHAR* Output, const TCHAR* Context, bool bModifiersMustPrecedeTriggers = false)
{
	FWeaponChordDefinition& Chord = Profile->Chords.AddDefaulted_GetRef();
	Chord.OutputTag = AirDodgeTag(Output);
	Chord.TriggerControls.Add(AirDodgeTag(TEXT("Input.Dodge")));
	Chord.RequiredContextTags.AddTag(AirDodgeTag(Context));
	Chord.ReleaseControlTag = AirDodgeTag(TEXT("Input.Dodge"));
	Chord.bRequireExactModifiers = false;
	Chord.bModifiersMustPrecedeTriggers = bModifiersMustPrecedeTriggers;
	return Chord;
}

FWeaponChordDefinition& AddRtaChord(UWeaponInputProfile* Profile,
	bool bModifiersMustPrecedeTriggers)
{
	FWeaponChordDefinition& Chord = Profile->Chords.AddDefaulted_GetRef();
	Chord.OutputTag = AirDodgeTag(TEXT("Input.Weapon.RTA"));
	Chord.TriggerControls.Add(AirDodgeTag(TEXT("Input.Dodge")));
	Chord.RequiredHeldModifiers.Add(AirDodgeTag(TEXT("Input.Modifier.RT")));
	// 撑杆跳是从**地面**起跳的，所以这条 Chord 带 Grounded 上下文门。
	Chord.RequiredContextTags.AddTag(AirDodgeTag(TEXT("Combat.State.Grounded")));
	Chord.bModifiersMustPrecedeTriggers = bModifiersMustPrecedeTriggers;
	return Chord;
}

struct FAirDodgeHarness
{
	UWorld* World = nullptr;
	AMHGZM3TestCharacter* Character = nullptr;
	AMHGZPlayerState* PlayerState = nullptr;
	UMHGZAbilitySystemComponent* ASC = nullptr;
	UMHGZWeaponRuntimeHostComponent* Host = nullptr;
	UMHGZWeaponInputRouterComponent* Router = nullptr;
	UGA_WeaponComboCoordinator* Coordinator = nullptr;

	bool Build()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false);
		if (!World)
		{
			return false;
		}
		Character = World->SpawnActor<AMHGZM3TestCharacter>();
		PlayerState = World->SpawnActor<AMHGZPlayerState>();
		if (!Character || !PlayerState)
		{
			return false;
		}
		Character->SetPlayerState(PlayerState);
		Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		ASC = PlayerState->GetMHGZAbilitySystemComponent();
		ASC->InitAbilityActorInfo(PlayerState, Character);

		Host = NewObject<UMHGZWeaponRuntimeHostComponent>(Character);
		Character->AddInstanceComponent(Host);
		Host->RegisterComponent();
		Host->InitializePawnRuntime(Character, nullptr, ASC, PlayerState->GetEquipmentComponent());

		Router = NewObject<UMHGZWeaponInputRouterComponent>();
		Router->AttachToPawn(Character);
		return true;
	}

	/** 起一个活的协调器（闸门/预输入/让位的家）。独立于 Build()，老用例不受影响。 */
	bool StartCoordinator()
	{
		const FGameplayAbilitySpecHandle Handle = ASC->GiveAbility(FGameplayAbilitySpec(
			UGA_WeaponComboCoordinator::StaticClass(), 1, INDEX_NONE, ASC));
		if (!Handle.IsValid() || !ASC->TryActivateAbility(Handle))
		{
			return false;
		}
		Coordinator = ASC->GetActiveComboCoordinator();
		return Coordinator != nullptr;
	}

	void Teardown()
	{
		if (World)
		{
			World->DestroyWorld(false);
		}
	}
};

/**
 * P0-2 integration setup: use the project's actual BP character and AnimBP, not the
 * lightweight M3 test pawn. The world is fully begun and advanced through UWorld::Tick;
 * no montage position, CMC step, or ability completion callback is driven by the test.
 */
struct FProductionAirDodgeHarness
{
	UWorld* World = nullptr;
	FWorldContext* WorldContext = nullptr;
	UGameInstance* GameInstance = nullptr;
	AMHGZCharacter* Character = nullptr;
	AMHGZPlayerState* PlayerState = nullptr;
	UMHGZAbilitySystemComponent* ASC = nullptr;
	UMHGZWeaponRuntimeHostComponent* Host = nullptr;
	UInsectGlaiveCombatConfig* CombatConfig = nullptr;
	UAnimMontage* DodgeMontage = nullptr;
	UAnimMontage* FallMontage = nullptr;
	UMHGZAirDodgeAbility* Action = nullptr;
	FGameplayAbilitySpecHandle AbilityHandle;

	bool Build()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false);
		if (!World)
		{
			return false;
		}
		World->AddToRoot();
		World->SetShouldTick(false); // manually ticked by this deterministic integration test
		if (GEngine)
		{
			WorldContext = &GEngine->CreateNewWorldContext(EWorldType::Game);
			GameInstance = NewObject<UGameInstance>(GEngine);
			WorldContext->OwningGameInstance = GameInstance;
			WorldContext->SetCurrentWorld(World);
		}
		if (!WorldContext || !GameInstance)
		{
			return false;
		}
		World->SetGameInstance(GameInstance);
		GameInstance->Init();

		UClass* CharacterClass = LoadClass<AMHGZCharacter>(nullptr,
			TEXT("/Game/Blueprints/Characters/Demo/BP_IG_Character.BP_IG_Character_C"));
		CombatConfig = LoadObject<UInsectGlaiveCombatConfig>(nullptr,
			TEXT("/Game/Weapons/InsectGlaive/Data/DA_IG_Combat.DA_IG_Combat"));
		DodgeMontage = LoadObject<UAnimMontage>(nullptr,
			TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_AirDodge.AM_IG_AirDodge"));
		if (!CharacterClass || !CombatConfig || !DodgeMontage)
		{
			return false;
		}
		FallMontage = CombatConfig->GetAerialDodgeFallMontage();
		if (!FallMontage)
		{
			return false;
		}

		Character = World->SpawnActor<AMHGZCharacter>(CharacterClass,
			FVector(0.0f, 0.0f, 10000.0f), FRotator::ZeroRotator);
		PlayerState = World->SpawnActor<AMHGZPlayerState>();
		if (!Character || !PlayerState)
		{
			return false;
		}
		Character->SetPlayerState(PlayerState);

		// Initialize real components and dispatch BeginPlay before manually mirroring the
		// possession-time ASC/Host wiring. The test has no controller, but all runtime
		// objects, generated AnimBP, and task tick registration are production instances.
		const FURL URL;
		World->SetGameMode(URL);
		World->InitializeActorsForPlay(URL);
		World->BeginPlay();

		ASC = PlayerState->GetMHGZAbilitySystemComponent();
		Host = Character->GetWeaponRuntimeHost();
		USkeletalMeshComponent* Mesh = Character->GetMesh();
		if (!ASC || !Host || !Mesh
			|| !Cast<UMHGZMotionMatchingAnimInstance>(Mesh->GetAnimInstance()))
		{
			return false;
		}

		ASC->InitAbilityActorInfo(PlayerState, Character);
		ASC->InitializeAbilitySystem();
		Host->InitializePawnRuntime(Character, nullptr, ASC, PlayerState->GetEquipmentComponent());
		Host->SetCombatConfigForTest(CombatConfig);
		Host->SetGrounded(false);
		Character->SetActorLocation(FVector(0.0f, 0.0f, 10000.0f), false, nullptr,
			ETeleportType::TeleportPhysics);
		Character->GetCharacterMovement()->Velocity = FVector::ZeroVector;
		Character->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
		Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Mesh->bEnableUpdateRateOptimizations = false;
		Mesh->SetComponentTickEnabled(true);
		return true;
	}

	bool Activate()
	{
		UClass* AbilityClass = LoadClass<UMHGZAirDodgeAbility>(nullptr,
			TEXT("/Game/Weapons/InsectGlaive/Abilities/GA_IG_AirDodge.GA_IG_AirDodge_C"));
		if (!AbilityClass || !ASC || !Host)
		{
			return false;
		}
		AbilityHandle = ASC->GiveAbility(FGameplayAbilitySpec(AbilityClass, 1, INDEX_NONE, ASC));
		if (!AbilityHandle.IsValid())
		{
			return false;
		}

		FWeaponAbilityActivationContext Context;
		Context.RuntimeToken = Host->GetCurrentToken();
		Context.ActivationSequenceID = Host->AllocateActivationSequenceID();
		Context.Input.ContextTags.AddTag(AirDodgeTag(TEXT("Combat.State.Aerial")));
		Context.Input.WorldDirection = FVector::ForwardVector;
		Context.Input.ActorForward = Character->GetActorForwardVector();
		ASC->PrepareWeaponAbilityActivation(AbilityHandle, Context);
		if (!ASC->TryActivateAbility(AbilityHandle))
		{
			return false;
		}

		if (FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(AbilityHandle))
		{
			for (UGameplayAbility* Instance : Spec->GetAbilityInstances())
			{
				if (UMHGZAirDodgeAbility* AirDodge = Cast<UMHGZAirDodgeAbility>(Instance))
				{
					Action = AirDodge;
					break;
				}
			}
		}
		return Action && Action->IsActive();
	}

	void Teardown()
	{
		if (World)
		{
			if (World->HasBegunPlay())
			{
				World->BeginTearingDown();
				World->EndPlay(EEndPlayReason::Quit);
			}
			World->RemoveFromRoot();
			if (GameInstance)
			{
				GameInstance->Shutdown();
			}
			if (GEngine)
			{
				GEngine->DestroyWorldContext(World);
			}
			World->DestroyWorld(false);
			World = nullptr;
			WorldContext = nullptr;
			GameInstance = nullptr;
		}
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5AirDodgeChordRoutingTest,
	"MHGZ.M5.Input.AirDodgeChordsRouteByContext",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5AirDodgeChordRoutingTest::RunTest(const FString& Parameters)
{
	FAirDodgeHarness H;
	if (!TestTrue(TEXT("harness built"), H.Build()))
	{
		H.Teardown();
		return false;
	}

	UWeaponInputProfile* Profile = NewObject<UWeaponInputProfile>();
	Profile->ChordGracePeriod = 0.05f;
	AddAirDodgeChord(Profile, TEXT("Input.Dodge"), TEXT("Combat.State.Grounded"));
	AddAirDodgeChord(Profile, TEXT("Input.AirDodge"), TEXT("Combat.State.Aerial"));
	AddRtaChord(Profile, /*bModifiersMustPrecedeTriggers=*/true);
	H.Router->SetInputProfile(Profile);

	const FGameplayTag A = AirDodgeTag(TEXT("Input.Dodge"));
	const FGameplayTag RT = AirDodgeTag(TEXT("Input.Modifier.RT"));

	// ── 1. 地面：A 单独按下**立即**出 Input.Dodge ────────────────────────────
	// 这一条就是 bModifiersMustPrecedeTriggers 存在的全部理由：没有它，RTA 会算作
	// 「可能凑成」，A 必须等满一个 ChordGracePeriod。
	H.Host->SetGrounded(true);
	H.Router->HandlePhysicalStarted(A, 0.0);
	TestEqual(TEXT("ground A emits on press, without waiting out the grace window"),
		H.Router->GetCapturedSnapshots().Num(), 1);
	if (H.Router->GetCapturedSnapshots().Num() == 1)
	{
		TestEqual(TEXT("ground A resolves to Input.Dodge"),
			H.Router->GetCapturedSnapshots().Last().ResolvedInputTag, A);
	}
	H.Router->HandlePhysicalCompleted(A, 0.02);

	// ── 2. 空中：A 单独按下立即出 Input.AirDodge ─────────────────────────────
	H.Host->SetGrounded(false);
	const int32 BeforeAir = H.Router->GetCapturedSnapshots().Num();
	H.Router->HandlePhysicalStarted(A, 1.0);
	TestEqual(TEXT("air A emits on press"), H.Router->GetCapturedSnapshots().Num(), BeforeAir + 1);
	if (H.Router->GetCapturedSnapshots().Num() > BeforeAir)
	{
		TestEqual(TEXT("air A resolves to Input.AirDodge"),
			H.Router->GetCapturedSnapshots().Last().ResolvedInputTag,
			AirDodgeTag(TEXT("Input.AirDodge")));
	}
	H.Router->HandlePhysicalCompleted(A, 1.02);

	// ── 3. 地面：先按住 RT 再按 A ⇒ 仍然是撑杆跳，且仍然按下即发 ─────────────
	H.Host->SetGrounded(true);
	const int32 BeforeVault = H.Router->GetCapturedSnapshots().Num();
	H.Router->HandlePhysicalStarted(RT, 2.0);
	H.Router->HandlePhysicalStarted(A, 2.01);
	TestEqual(TEXT("RT-then-A emits one snapshot on press"),
		H.Router->GetCapturedSnapshots().Num(), BeforeVault + 1);
	if (H.Router->GetCapturedSnapshots().Num() > BeforeVault)
	{
		TestEqual(TEXT("RT-then-A still resolves to the vault"),
			H.Router->GetCapturedSnapshots().Last().ResolvedInputTag,
			AirDodgeTag(TEXT("Input.Weapon.RTA")));
	}
	H.Router->HandlePhysicalCompleted(A, 2.05);
	H.Router->HandlePhysicalCompleted(RT, 2.06);

	// ── 4. 地面：先按 A 再补 RT ⇒ **不再**出撑杆跳（刻意的代价）─────────────
	//
	// 用户 2026-09-22 拍板：摘掉「修饰键可最后补齐」以换取 A 单独按下零延迟。
	// 这一条把它钉住 —— 若哪天它悄悄变回「也能凑成」，说明开关被去掉了。
	const int32 BeforeLateRt = H.Router->GetCapturedSnapshots().Num();
	H.Router->HandlePhysicalStarted(A, 3.0);
	TestEqual(TEXT("A alone emits the dodge immediately"), H.Router->GetCapturedSnapshots().Num(),
		BeforeLateRt + 1);
	H.Router->HandlePhysicalStarted(RT, 3.02);
	TestEqual(TEXT("a modifier arriving after the trigger does NOT retro-form the vault"),
		H.Router->GetCapturedSnapshots().Num(), BeforeLateRt + 1);
	H.Router->HandlePhysicalCompleted(A, 3.05);
	H.Router->HandlePhysicalCompleted(RT, 3.06);

	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5AirDodgePreContextGateTest,
	"MHGZ.M5.Aerial.AirDodgeCanActivateIgnoresActivationContext",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 2026-09-22 PIE 的「怎么都按不出来」：`CanActivateAbility` 里读了
// `GetWeaponActivationContext().Input.ContextTags`，而那个成员要到 `ActivateAbility`
// 才被消费进来 ⇒ 早一步读到的永远是空上下文 ⇒ 恒 false ⇒ 能力静默不激活（连日志都没有）。
//
// 这条断言把分工钉死：**激活闸门只许看 ASC 上的活 tag，快照判据归
// `ValidateActionDependencies`**。把上下文判据搬回 `CanActivateAbility` 会让它变红。
bool FMHGZM5AirDodgePreContextGateTest::RunTest(const FString& Parameters)
{
	FAirDodgeHarness H;
	if (!TestTrue(TEXT("harness built"), H.Build()))
	{
		H.Teardown();
		return false;
	}

	const FGameplayAbilitySpecHandle Handle = H.ASC->GiveAbility(
		FGameplayAbilitySpec(UMHGZAirDodgeAbility::StaticClass(), 1, INDEX_NONE, H.ASC));
	TestTrue(TEXT("ability granted"), Handle.IsValid());

	// 空回现在正向查「空中可操作」（ActivationRequiredTags）—— 先把该 tag 摆上
	// （离地无 Action 领取），本条的分工断言才只关于「上下文读不读」。
	H.Host->SetGrounded(false);

	FGameplayAbilityActorInfo ActorInfo;
	ActorInfo.InitFromActor(H.Character, H.Character, H.ASC);
	const UMHGZAirDodgeAbility* Ability =
		UMHGZAirDodgeAbility::StaticClass()->GetDefaultObject<UMHGZAirDodgeAbility>();

	// ① 没有待消费上下文（= 真实的激活前一步）也必须放行。
	TestTrue(TEXT("activation gate passes with no activation context consumed yet"),
		Ability->CanActivateAbility(Handle, &ActorInfo, nullptr, nullptr, nullptr));

	// ② 闸门仍然做它该做的事：Dead / Hitstun / Knockdown 与自锁 tag 照旧拦。
	const FGameplayTag CantDodge = AirDodgeTag(TEXT("Combat.State.Aerial.CantDodge"));
	TestTrue(TEXT("recording a dodge acquires the self-lock"),
		H.Host->MarkAerialDodgeUsed() && H.ASC->HasMatchingGameplayTag(CantDodge));
	TestFalse(TEXT("ActivationBlockedTags still refuses the second dodge"),
		Ability->CanActivateAbility(Handle, &ActorInfo, nullptr, nullptr, nullptr));

	H.Host->HandleLanded();
	// 落地清掉了可操作 tag —— 重新领上，让 Dead 的断言只关于 Dead。
	H.Host->SetGrounded(false);
	H.ASC->SetLooseGameplayTagCount(
		AirDodgeTag(TEXT("Combat.State.Dead")), 1);
	TestFalse(TEXT("Dead still refuses"),
		Ability->CanActivateAbility(Handle, &ActorInfo, nullptr, nullptr, nullptr));
	H.ASC->SetLooseGameplayTagCount(AirDodgeTag(TEXT("Combat.State.Dead")), 0);

	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5AirDodgeRequestShapeTest,
	"MHGZ.M5.Aerial.AirDodgeRequestShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5AirDodgeRequestShapeTest::RunTest(const FString& Parameters)
{
	UInsectGlaiveCombatConfig* Config = NewObject<UInsectGlaiveCombatConfig>();
	const FVector Stick(1.0f, 0.0f, 0.0f);
	const FVector Facing(0.0f, 1.0f, 0.0f);

	const FWeaponMovementRequest Request = UMHGZAirDodgeAbility::BuildAirDodgeRequest(
		FWeaponActionToken(), Stick, Facing, *Config);

	TestEqual(TEXT("uses the ballistic mode"), Request.Mode, EWeaponMovementMode::BallisticVault);
	TestEqual(TEXT("uses explicit launch velocity"),
		Request.BallisticMode, EBallisticParameterMode::ExplicitLaunchVelocity);
	// XOR 契约：显式初速这一组要求顶点与时长都为 0，否则整条请求会被直接拒绝。
	TestEqual(TEXT("apex height stays zero"), Request.ApexHeight, 0.0f);
	TestEqual(TEXT("duration stays zero"), Request.Duration, 0.0f);
	TestTrue(TEXT("request passes HasValidBallisticParameters"),
		Request.HasValidBallisticParameters());
	TestTrue(TEXT("horizontal speed matches the recorded 900 cm/s"),
		FMath::IsNearlyEqual(Request.LaunchVelocity.Size2D(), 900.0f, 0.01f));
	TestTrue(TEXT("vertical speed matches the recorded +1182 cm/s"),
		FMath::IsNearlyEqual(Request.LaunchVelocity.Z, 1182.0f, 0.01f));
	TestTrue(TEXT("direction comes from the stick, not from facing"),
		FVector::DotProduct(Request.LaunchVelocity.GetSafeNormal2D(), Stick) > 0.999f);
	TestEqual(TEXT("rotation stays locked after the one-frame snap"),
		Request.RotationPolicy, EActionRotationPolicy::Locked);
	TestEqual(TEXT("the tangent survives the source end"),
		Request.CancelVelocityPolicy, EMovementCancelVelocityPolicy::PreserveVelocity);

	// 摇杆回中 ⇒ 回退到朝向（真值里有 15 段起始转向 ≈0°，与「回中」相容）。
	const FWeaponMovementRequest Fallback = UMHGZAirDodgeAbility::BuildAirDodgeRequest(
		FWeaponActionToken(), FVector::ZeroVector, Facing, *Config);
	TestTrue(TEXT("centred stick falls back to facing"),
		FVector::DotProduct(Fallback.LaunchVelocity.GetSafeNormal2D(), Facing) > 0.999f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5AirDodgeCantBudgetTest,
	"MHGZ.M5.Aerial.AirDodgeCantBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5AirDodgeCantBudgetTest::RunTest(const FString& Parameters)
{
	FAirDodgeHarness H;
	if (!TestTrue(TEXT("harness built"), H.Build()))
	{
		H.Teardown();
		return false;
	}

	const FGameplayTag CantDodge = AirDodgeTag(TEXT("Combat.State.Aerial.CantDodge"));
	H.Host->SetGrounded(false);
	TestFalse(TEXT("no budget is spent before the dodge"),
		H.ASC->HasMatchingGameplayTag(CantDodge));

	TestTrue(TEXT("recording a dodge succeeds"), H.Host->MarkAerialDodgeUsed());
	TestTrue(TEXT("the budget tag is present while airborne"),
		H.ASC->HasMatchingGameplayTag(CantDodge));
	TestTrue(TEXT("recording is idempotent"), H.Host->MarkAerialDodgeUsed());

	// 唯一释放点是落地清理。刻意**没有**「能力结束时释放」这条路径：语义是
	// 「本次滞空已用」，取消进别的空中招式不得退款。
	H.Host->HandleLanded();
	TestFalse(TEXT("touchdown releases the budget"),
		H.ASC->HasMatchingGameplayTag(CantDodge));

	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5AirDodgeFallingTagTest,
	"MHGZ.M5.Aerial.AirDodgeFallingTagDeclared",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5AirDodgeFallingTagTest::RunTest(const FString& Parameters)
{
	const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Aerial.Falling.IG_AirDodge"), /*ErrorIfNotFound=*/false);
	TestTrue(TEXT("Combat.State.Aerial.Falling.IG_AirDodge is declared"), Tag.IsValid());
	return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// PIE 第 2 轮三缺陷的回归面：闸门 / 预输入 / 让位 / 尾段落地 / 预算时序
// （根因与行为表见 docs/using/m5-plan.md §8.1）
// ═══════════════════════════════════════════════════════════════════════════

namespace
{
UMHGZM5TestIGAction* FindTestIGAction(UMHGZAbilitySystemComponent* ASC,
	const FGameplayAbilitySpecHandle& Handle)
{
	FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(Handle);
	if (!Spec)
	{
		return nullptr;
	}
	for (UGameplayAbility* Instance : Spec->GetAbilityInstances())
	{
		if (UMHGZM5TestIGAction* Action = Cast<UMHGZM5TestIGAction>(Instance))
		{
			return Action;
		}
	}
	return nullptr;
}

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5AirDodgeFallingGateLifecycleTest,
	"MHGZ.M5.Aerial.AirDodgeFallingGateLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 「空中可操作」(Combat.State.Aerial.Falling) 的领放生命周期 —— 最早可操作帧的锁
// 就是它的**缺席**：离地无 Action 领 / 有 Action 跳过（起手段自己要锁）→ Detect
// 放（lead 开始）→ handoff 领 → 中止滞空领。缺席时空回被引擎原生拦
// （ActivationRequiredTags，DoesAbilitySatisfyTagRequirements 直接可观测）。
bool FMHGZM5AirDodgeFallingGateLifecycleTest::RunTest(const FString& Parameters)
{
	FAirDodgeHarness H;
	if (!TestTrue(TEXT("harness built"), H.Build()))
	{
		H.Teardown();
		return false;
	}

	const FGameplayTag Falling = AirDodgeTag(TEXT("Combat.State.Aerial.Falling"));
	UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr,
		TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_AirDodge.AM_IG_AirDodge"));
	if (!TestNotNull(TEXT("AM_IG_AirDodge loads"), Montage))
	{
		H.Teardown();
		return false;
	}
	const UMHGZAirDodgeAbility* Dodge =
		UMHGZAirDodgeAbility::StaticClass()->GetDefaultObject<UMHGZAirDodgeAbility>();

	// ① 离地且无已注册 Action ⇒ 领（走落台阶 / 起跳腾空，含上升段）
	H.Host->SetGrounded(false);
	TestTrue(TEXT("leaving the ground with no action grants the operable state"),
		H.ASC->HasMatchingGameplayTag(Falling));
	TestTrue(TEXT("the engine tag gate passes while it is up"),
		Dodge->DoesAbilitySatisfyTagRequirements(*H.ASC));

	// 落地清理放（HandleLanded 是唯一放落地侧的点）
	H.Host->HandleLanded();
	TestFalse(TEXT("touchdown releases the operable state"),
		H.ASC->HasMatchingGameplayTag(Falling));

	// ② 有已注册 Action 时离地**不**领：起手段自己就是 lead 锁定期
	const FGameplayAbilitySpecHandle Handle = H.ASC->GiveAbility(FGameplayAbilitySpec(
		UMHGZM5TestIGAction::StaticClass(), 1, INDEX_NONE, H.ASC));
	TestTrue(TEXT("test action granted"), H.ASC->TryActivateAbility(Handle));
	UMHGZM5TestIGAction* Action = FindTestIGAction(H.ASC, Handle);
	if (!TestNotNull(TEXT("test action instance"), Action))
	{
		H.Teardown();
		return false;
	}
	H.Host->SetGrounded(false);
	TestFalse(TEXT("take-off under a registered action keeps its lead locked"),
		H.ASC->HasMatchingGameplayTag(Falling));

	// ③ handoff 领
	TestTrue(TEXT("handoff reaches"), Action->NotifyAerialHandoff());
	TestTrue(TEXT("handoff grants the operable state"),
		H.ASC->HasMatchingGameplayTag(Falling));

	// 无通知的蒙太奇不放不领（地面招式行为不变）
	UAnimMontage* Bare = NewObject<UAnimMontage>(GetTransientPackage());
	Action->RunDetectForTest(Bare);
	TestTrue(TEXT("a montage without the notify releases nothing"),
		H.ASC->HasMatchingGameplayTag(Falling));

	// ④ 新动作起播（Detect 发现点通知）⇒ 放 —— lead 锁定期由此开始
	Action->RunDetectForTest(Montage);
	TestFalse(TEXT("detecting the notify-bearing montage releases the state (lead locked)"),
		H.ASC->HasMatchingGameplayTag(Falling));
	TestFalse(TEXT("the engine tag gate rejects while it is absent"),
		Dodge->DoesAbilitySatisfyTagRequirements(*H.ASC));
	{
		FGameplayAbilityActorInfo ActorInfo;
		ActorInfo.InitFromActor(H.Character, H.Character, H.ASC);
		TestFalse(TEXT("the air dodge is blocked natively inside the lead window"),
			Dodge->CanActivateAbility(FGameplayAbilitySpecHandle(), &ActorInfo, nullptr, nullptr, nullptr));
	}

	// ⑤ 中止滞空 ⇒ 领（输入即刻恢复）：第二个 lead 先摆好再打断
	TestTrue(TEXT("handoff reaches again"), Action->NotifyAerialHandoff());
	Action->RunDetectForTest(Montage);
	TestFalse(TEXT("the second lead releases the state again"),
		H.ASC->HasMatchingGameplayTag(Falling));
	Action->RequestEndAction(EWeaponActionEndReason::Interrupted);
	TestTrue(TEXT("an airborne abort re-grants the operable state"),
		H.ASC->HasMatchingGameplayTag(Falling));

	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5AirDodgeSupersedeTest,
	"MHGZ.M5.Aerial.AirDodgeSupersedeYieldsOtherIGActions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 让位（R2）：其它已 Commit 的 IG 动作以 Superseded 结束（首因生效、bWasCancelled），
// Superseder 自身与非 IG 能力不被波及。
bool FMHGZM5AirDodgeSupersedeTest::RunTest(const FString& Parameters)
{
	FAirDodgeHarness H;
	if (!TestTrue(TEXT("harness built"), H.Build()) || !TestTrue(TEXT("coordinator live"), H.StartCoordinator()))
	{
		H.Teardown();
		return false;
	}

	const FGameplayAbilitySpecHandle HandleA = H.ASC->GiveAbility(FGameplayAbilitySpec(
		UMHGZM5TestIGAction::StaticClass(), 1, INDEX_NONE, H.ASC));
	const FGameplayAbilitySpecHandle HandleB = H.ASC->GiveAbility(FGameplayAbilitySpec(
		UMHGZM5TestIGAction::StaticClass(), 1, INDEX_NONE, H.ASC));
	H.ASC->TryActivateAbility(HandleA);
	H.ASC->TryActivateAbility(HandleB);
	UMHGZM5TestIGAction* ActionA = FindTestIGAction(H.ASC, HandleA);
	UMHGZM5TestIGAction* ActionB = FindTestIGAction(H.ASC, HandleB);
	if (!TestNotNull(TEXT("action A instance"), ActionA) || !TestNotNull(TEXT("action B instance"), ActionB))
	{
		H.Teardown();
		return false;
	}

	FWeaponActionToken SupersederToken;
	SupersederToken.AbilityInstance = ActionB;
	const int32 Superseded = H.Coordinator->SupersedeOtherIGAerialActions(SupersederToken);
	TestEqual(TEXT("exactly the other action is superseded"), Superseded, 1);
	TestEqual(TEXT("A ended exactly once"), ActionA->EndCount, 1);
	TestTrue(TEXT("A ended cancelled (Superseded maps to bWasCancelled)"), ActionA->LastEndWasCancelled);
	TestEqual(TEXT("A recorded the Superseded reason"), ActionA->GetActionEndReason(),
		EWeaponActionEndReason::Superseded);
	TestEqual(TEXT("the superseder itself is untouched"), ActionB->EndCount, 0);
	TestTrue(TEXT("the coordinator is not IG and stays live"), H.Coordinator->IsActive());

	// 首因生效：重复请求不再结束一次
	ActionA->RequestEndAction(EWeaponActionEndReason::Normal);
	TestEqual(TEXT("the first end reason wins; repeats are ignored"), ActionA->EndCount, 1);

	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5AirDodgeTailLandedTest,
	"MHGZ.M5.Aerial.AirDodgeTailLandedClaimsLanding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
/**
 * 装出「能播蒙太奇」的最小现场并**真实激活**一次空中回避（授予生产蓝图类
 * `GA_IG_AirDodge_C` —— 资产里自带蒙太奇指派；**不要**走「CDO 运行期改写」那条路，
 * 实测改写不会传到激活出的实例上）。
 *
 * Host 上有合法战斗配置（SetCombatConfigForTest）⇒ 整条流水线（含位移源）活起来；
 * 没有 ⇒ 激活会在 commit 之后死于重力 profile（R3 测试要的失败路径）。
 * 收尾链（RequestEndAction → EndAbility）需要 CurrentActorInfo —— 所以任何要走
 * 那条链的测试都必须用真实激活，不能 NewObject 一个惰性对象（踩过 ensure）。
 */
UMHGZAirDodgeAbility* ActivateRealAirDodge(FAirDodgeHarness& H,
	TObjectPtr<UAnimMontage>& OutMontage)
{
	OutMontage = LoadObject<UAnimMontage>(nullptr,
		TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_AirDodge.AM_IG_AirDodge"));
	UClass* AbilityClass = LoadClass<UMHGZAirDodgeAbility>(nullptr,
		TEXT("/Game/Weapons/InsectGlaive/Abilities/GA_IG_AirDodge.GA_IG_AirDodge_C"));
	USkeletalMeshComponent* Mesh = H.Character ? H.Character->GetMesh() : nullptr;
	if (!OutMontage || !AbilityClass || !Mesh)
	{
		return nullptr;
	}
	Mesh->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Characters/Demo/Meshes/Body/SKM_Demo_Body.SKM_Demo_Body")));
	Mesh->SetAnimInstanceClass(UAnimInstance::StaticClass());

	// 空回的 ActivationRequiredTags 查「空中可操作」——真实激活前先离地入态
	// （无已注册 Action ⇒ Host 领裸 Falling）。激活后 Detect 会把它放掉（自己的 lead）。
	H.Host->SetGrounded(false);

	const FGameplayAbilitySpecHandle Handle = H.ASC->GiveAbility(FGameplayAbilitySpec(
		AbilityClass, 1, INDEX_NONE, H.ASC));
	FWeaponAbilityActivationContext Context;
	Context.RuntimeToken = H.Host->GetCurrentToken();
	Context.ActivationSequenceID = H.Host->AllocateActivationSequenceID();
	Context.Input.ContextTags.AddTag(AirDodgeTag(TEXT("Combat.State.Aerial")));
	H.ASC->PrepareWeaponAbilityActivation(Handle, Context);
	H.ASC->TryActivateAbility(Handle);

	if (FGameplayAbilitySpec* Spec = H.ASC->FindAbilitySpecFromHandle(Handle))
	{
		for (UGameplayAbility* Instance : Spec->GetAbilityInstances())
		{
			if (UMHGZAirDodgeAbility* Action = Cast<UMHGZAirDodgeAbility>(Instance))
			{
				return Action;
			}
		}
	}
	return nullptr;
}
}

// R4：源以 FreeFall 结束后、蒙太奇尾段内触地 ⇒ 两旗标互斥翻转，当帧认领落地表现。
// 走**真实激活**的活实例 —— 收尾链需要 CurrentActorInfo。
bool FMHGZM5AirDodgeTailLandedTest::RunTest(const FString& Parameters)
{
	UInsectGlaiveCombatConfig* Config = NewObject<UInsectGlaiveCombatConfig>(GetTransientPackage());
	FAirDodgeHarness H;
	if (!TestNotNull(TEXT("combat config"), Config) || !TestTrue(TEXT("harness built"), H.Build()))
	{
		H.Teardown();
		return false;
	}
	// 生产路径由 ApplyWeaponSnapshot 经武器快照装配；测试直接注入。
	H.Host->SetCombatConfigForTest(Config);

	TObjectPtr<UAnimMontage> SavedMontage;
	UMHGZAirDodgeAbility* Action = ActivateRealAirDodge(H, SavedMontage);
	if (!TestNotNull(TEXT("live air dodge instance"), Action))
	{
		H.Teardown();
		return false;
	}
	TestTrue(TEXT("the instance is live with its movement pipeline"), Action->IsActive());

	// 源以 FreeFall 到期 + 蒙太奇尾段未完 的窗口里触地
	Action->SetAirDodgeExitFlagsForTest(/*bInMovementFinished=*/true, /*bInBeginFreeFall=*/true);
	Action->HandleAirDodgeTailLandedForTest();
	TestFalse(TEXT("free-fall claim is given up on touchdown"),
		Action->GetBeginFreeFallAfterEndForTest());
	TestTrue(TEXT("the landing presentation is claimed"), Action->GetPlayLandedPresentationForTest());
	TestEqual(TEXT("the tail landing ends the action normally"), Action->GetActionEndReason(),
		EWeaponActionEndReason::Normal);

	// 幂等：二次触地不得翻回去（此时能力已结束，处理器应当直接返回）
	Action->HandleAirDodgeTailLandedForTest();
	TestTrue(TEXT("the claim is idempotent"), Action->GetPlayLandedPresentationForTest());

	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5AirDodgeBudgetAtCommitTest,
	"MHGZ.M5.Aerial.AirDodgeBudgetSpentEvenIfPipelineFails",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// R3 时序（⑤）+ 失败路径停蒙太奇（⑧）：真实激活走「commit 成功、随后流水线失败」
// （CombatConfig 为空 ⇒ ApplyAerialFallingProfile 必败）—— 预算必须已经记上
// （Mark 在位移启动之前），且闪断蒙太奇必须被停掉。
// 把 Mark 挪回 StartAirDodgeMovement 之后 ⇒ 这条会红。
bool FMHGZM5AirDodgeBudgetAtCommitTest::RunTest(const FString& Parameters)
{
	FAirDodgeHarness H;
	if (!TestTrue(TEXT("harness built"), H.Build()))
	{
		H.Teardown();
		return false;
	}

	TObjectPtr<UAnimMontage> SavedMontage;
	ActivateRealAirDodge(H, SavedMontage);

	const FGameplayTag CantDodge = AirDodgeTag(TEXT("Combat.State.Aerial.CantDodge"));
	TestTrue(TEXT("budget is spent at commit, before the movement pipeline"),
		H.ASC->HasMatchingGameplayTag(CantDodge));

	USkeletalMeshComponent* Mesh = H.Character ? H.Character->GetMesh() : nullptr;
	UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (AnimInstance && SavedMontage)
	{
		TestFalse(TEXT("the failure path stops the montage instead of leaving a ghost instance"),
			AnimInstance->Montage_IsActive(SavedMontage));
	}

	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5AirDodgeLandingResetTest,
	"MHGZ.M5.Aerial.LandingHorizontalReset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 落地水平重设（下落物理.md §六 / 操虫斩曲线.md §六）：认领落地表现那一刻
// （= 真值 148 的首帧）水平速度**重设**到 337±6、方向保持、与入速无关；
// 近零速取朝向。走落台阶（不认领落地表现）不经过这里。
bool FMHGZM5AirDodgeLandingResetTest::RunTest(const FString& Parameters)
{
	FAirDodgeHarness H;
	if (!TestTrue(TEXT("harness built"), H.Build()))
	{
		H.Teardown();
		return false;
	}
	UInsectGlaiveCombatConfig* Config = NewObject<UInsectGlaiveCombatConfig>(GetTransientPackage());
	if (!TestNotNull(TEXT("combat config"), Config))
	{
		H.Teardown();
		return false;
	}
	H.Host->SetCombatConfigForTest(Config);
	UCharacterMovementComponent* CMC = H.Character->GetCharacterMovement();
	const float Tolerance = 1.0f;

	// ① 高入速 900 沿 +X 落地 ⇒ 337、方向保持（真值 137→148：898 → 338）
	CMC->Velocity = FVector(900.0, 0.0, -500.0);
	H.Host->PlayAerialLandingPresentation();
	TestTrue(TEXT("entry 900 resets to the landing speed"),
		FMath::Abs(FVector2D(CMC->Velocity.X, CMC->Velocity.Y).Size() - 337.0f) <= Tolerance);
	TestTrue(TEXT("direction is preserved (forward)"),
		CMC->Velocity.X > 330.0f && FMath::Abs(CMC->Velocity.Y) <= Tolerance);

	// ② 低入速 300 斜向 ⇒ 仍是 337、方向保持（真值 156→148：306 → 338）
	CMC->Velocity = FVector(-212.0, -212.0, -300.0);
	H.Host->PlayAerialLandingPresentation();
	TestTrue(TEXT("entry 300 also resets to the landing speed"),
		FMath::Abs(FVector2D(CMC->Velocity.X, CMC->Velocity.Y).Size() - 337.0f) <= Tolerance);
	TestTrue(TEXT("diagonal direction is preserved"),
		CMC->Velocity.X < -200.0f && CMC->Velocity.Y < -200.0f);

	// ③ 近零速 ⇒ 取朝向（+Y）
	H.Character->SetActorRotation(FRotator(0.0, 90.0, 0.0));
	CMC->Velocity = FVector(0.0, 0.0, -100.0);
	H.Host->PlayAerialLandingPresentation();
	TestTrue(TEXT("near-zero entry takes the facing direction"),
		FMath::Abs(CMC->Velocity.Y - 337.0f) <= Tolerance
		&& FMath::Abs(CMC->Velocity.X) <= Tolerance);

	// 竖直分量不动
	TestTrue(TEXT("vertical component is untouched"), CMC->Velocity.Z == -100.0f);

	// 落地回调发生在 PerformMovement 内；同帧剩余 Walking 时间会将 337 刹到更低。
	// 单次 frame-end 重设把 Rise 148 的首帧采样对齐，但不影响后续常规输入/制动。
	UMHGZInstrumentedCharacterMovementComponent* InstrumentedCMC =
		Cast<UMHGZInstrumentedCharacterMovementComponent>(CMC);
	if (!TestNotNull(TEXT("character uses the instrumented production CMC"), InstrumentedCMC))
	{
		H.Teardown();
		return false;
	}
	CMC->Velocity = FVector(0.0, 0.0, -100.0);
	InstrumentedCMC->QueuePostMovementLandingSpeedReset(337.0f, FVector::ForwardVector);
	InstrumentedCMC->PerformMovement(0.025f);
	TestTrue(TEXT("post-PerformMovement landing sample retains 337 for the first frame"),
		FMath::Abs(FVector2D(CMC->Velocity.X, CMC->Velocity.Y).Size() - 337.0f) <= Tolerance);

	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5AirDodgeUnobstructedHandoffTest,
	"MHGZ.M5.Aerial.AirDodgeToFallHandoffKeepsPlanarVelocityWithoutCollision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 生产链路回归：真实 GA_IG_AirDodge_C、真实空回蒙太奇、JumpForce Root Motion Source、
// Ability EndAbility → Host.BeginAerialFalling → 真实 157 表现，以及同一生产 CMC。
// 高空空场排除环境碰撞；只在这种试次要求 137→157 的 900 cm/s 水平速度不跳变。
bool FMHGZM5AirDodgeUnobstructedHandoffTest::RunTest(const FString& Parameters)
{
	FAirDodgeHarness H;
	if (!TestTrue(TEXT("harness built"), H.Build()))
	{
		H.Teardown();
		return false;
	}

	UInsectGlaiveCombatConfig* Config = LoadObject<UInsectGlaiveCombatConfig>(
		nullptr, TEXT("/Game/Weapons/InsectGlaive/Data/DA_IG_Combat.DA_IG_Combat"));
	if (!TestNotNull(TEXT("production combat config loads"), Config))
	{
		H.Teardown();
		return false;
	}
	H.Host->SetCombatConfigForTest(Config);
	H.Character->SetActorLocation(FVector(0.0, 0.0, 10000.0));
	H.Character->GetCharacterMovement()->SetMovementMode(MOVE_Falling);

	TObjectPtr<UAnimMontage> SavedMontage;
	UMHGZAirDodgeAbility* Action = ActivateRealAirDodge(H, SavedMontage);
	if (!TestNotNull(TEXT("production AirDodge ability is active"), Action)
		|| !TestTrue(TEXT("the real AirDodge montage instance started"),
			H.Character->GetMesh()->GetAnimInstance()
			&& H.Character->GetMesh()->GetAnimInstance()->Montage_IsActive(SavedMontage)))
	{
		H.Teardown();
		return false;
	}

	UMHGZInstrumentedCharacterMovementComponent* CMC =
		Cast<UMHGZInstrumentedCharacterMovementComponent>(H.Character->GetCharacterMovement());
	if (!TestNotNull(TEXT("production instrumented CMC"), CMC))
	{
		H.Teardown();
		return false;
	}

	// Advance the actual JumpForce for longer than its 0.9949 s duration in a collision-free
	// transient world. The ability task's completion delegate is intentionally driven below at
	// the same seam, after verifying the CMC has naturally dropped the source.
	constexpr float Dt = 0.025f;
	for (int32 Frame = 0; Frame < 48; ++Frame)
	{
		if (Frame == 26)
		{
			TestTrue(TEXT("the real ability accepts its mid-air handoff notify"),
				Action->NotifyAerialHandoff());
		}
		CMC->PerformMovement(Dt);
	}
	AddInfo(FString::Printf(TEXT("Pre-handoff: Velocity=%s Mode=%d ActiveRMS=%d Location=%s"),
		*CMC->Velocity.ToCompactString(), static_cast<int32>(CMC->MovementMode),
		CMC->GetCurrentActiveRootMotionSourceCount(), *H.Character->GetActorLocation().ToCompactString()));
	TestEqual(TEXT("JumpForce has handed off before the visual tail ends"),
		CMC->GetCurrentActiveRootMotionSourceCount(), 0);
	TestTrue(TEXT("the real ballistic source hands off near 900 cm/s"),
		FMath::Abs(FVector2D(CMC->Velocity.X, CMC->Velocity.Y).Size() - 900.0f) <= 5.0f);

	// Stand in for the montage's actual completion delegate after the mounted 137 tail.
	// All state changes below are production EndAbility/Host code, not mocked velocity writes.
	Action->SetAirDodgeExitFlagsForTest(/*bInMovementFinished=*/true,
		/*bInBeginFreeFall=*/true);
	Action->HandleAirDodgeVisualCompletedForTest();
	AddInfo(FString::Printf(TEXT("Post-handoff: Velocity=%s Mode=%d HostFalling=%d FallTag=%d"),
		*CMC->Velocity.ToCompactString(), static_cast<int32>(CMC->MovementMode),
		H.Host->IsAerialFalling() ? 1 : 0,
		H.ASC->HasMatchingGameplayTag(AirDodgeTag(TEXT("Combat.State.Aerial.Falling"))) ? 1 : 0));
	TestFalse(TEXT("the real GA ended at the visual completion handoff"), Action->IsActive());
	TestTrue(TEXT("the Host claimed system-owned falling presentation"), H.Host->IsAerialFalling());
	TestEqual(TEXT("handoff leaves the production CMC falling"),
		CMC->MovementMode, MOVE_Falling);

	for (int32 Frame = 0; Frame < 4; ++Frame)
	{
		CMC->PerformMovement(Dt);
		TestTrue(FString::Printf(TEXT("unobstructed free-fall frame %d preserves 900 cm/s"), Frame),
			FMath::Abs(FVector2D(CMC->Velocity.X, CMC->Velocity.Y).Size() - 900.0f) <= 5.0f);
	}

	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5AirDodgeFallClipTest,
	"MHGZ.M5.Aerial.DodgeFallClipIs157",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 空回后坠的 clip = 真值 id 157（AS_Unsh_Fall_W_Jump = WhiteAerialFallMontage），
// 与灯态无关；物理档仍由 bEnhancedVariant 独立决定（空回恒 false ⇒ 2.4246）。
// 落地 clip = 148（AM_IG_AerialLanding，0.5833 s = 1.1667 @rate2.0）。
bool FMHGZM5AirDodgeFallClipTest::RunTest(const FString& Parameters)
{
	const UInsectGlaiveCombatConfig* Config = LoadObject<UInsectGlaiveCombatConfig>(
		nullptr, TEXT("/Game/Weapons/InsectGlaive/Data/DA_IG_Combat.DA_IG_Combat"));
	if (!TestNotNull(TEXT("DA_IG_Combat loads"), Config))
	{
		return false;
	}

	const UAnimMontage* DodgeFall = Config->GetAerialDodgeFallMontage();
	const UAnimMontage* WhiteFall = Config->GetAerialFallingMontage(true);
	const UAnimMontage* PlainFall = Config->GetAerialFallingMontage(false);
	TestNotNull(TEXT("dodge fall montage assigned"), DodgeFall);
	TestNotNull(TEXT("white fall montage assigned"), WhiteFall);
	TestNotNull(TEXT("plain fall montage assigned"), PlainFall);
	if (DodgeFall && WhiteFall && PlainFall)
	{
		TestEqual(TEXT("dodge fall reuses the 157 clip (WhiteAerialFallMontage)"),
			DodgeFall, WhiteFall);
		TestTrue(TEXT("157 clip is AM_IG_AerialFall_W"),
			DodgeFall->GetName() == TEXT("AM_IG_AerialFall_W"));
		TestTrue(TEXT("143 clip stays AM_IG_AerialFall for the plain vault fall"),
			PlainFall->GetName() == TEXT("AM_IG_AerialFall"));
	}

	const UAnimMontage* Landing = Config->GetAerialLandingMontage();
	TestNotNull(TEXT("landing montage assigned"), Landing);
	if (Landing)
	{
		TestTrue(TEXT("landing montage is AM_IG_AerialLanding"),
			Landing->GetName() == TEXT("AM_IG_AerialLanding"));
		TestTrue(TEXT("landing length matches 148 (0.5833 s)"),
			FMath::Abs(Landing->GetPlayLength() - 0.5833f) <= 0.005f);
	}
	TestTrue(TEXT("landing reset speed is the truth value 337"),
		FMath::Abs(Config->GetAerialLandingHorizontalSpeed() - 337.0f) <= 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5AirDodgeAnimRootMotionShieldTest,
	"MHGZ.M5.Aerial.AnimRootMotionShield",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 空中动画根盾（PIE 第 5 轮缝上「速度骤变」的架构性修复，附录 A.25）：非地面或托管
// 表现窗口内，动画根位移被换成 Velocity*Δt、旋转清零 —— ConstrainAnimRootMotionVelocity
// 的「XY:=动画根速度、Z 保留」变成恒等替换，恒定根轨道不再抹水平。地面 locomotion
// 的动画根（Walk/Dash 一族 bEnableRootMotion=true）是合法驱动，原样放行。
bool FMHGZM5AirDodgeAnimRootMotionShieldTest::RunTest(const FString& Parameters)
{
	FAirDodgeHarness H;
	if (!TestTrue(TEXT("harness built"), H.Build()))
	{
		H.Teardown();
		return false;
	}
	UInsectGlaiveCombatConfig* Config = LoadObject<UInsectGlaiveCombatConfig>(
		nullptr, TEXT("/Game/Weapons/InsectGlaive/Data/DA_IG_Combat.DA_IG_Combat"));
	if (!TestNotNull(TEXT("DA_IG_Combat loads"), Config))
	{
		H.Teardown();
		return false;
	}
	H.Host->SetCombatConfigForTest(Config);
	UMHGZWeaponRuntimeHostComponent* Host = H.Host;
	UCharacterMovementComponent* CMC = H.Character->GetCharacterMovement();
	if (!TestNotNull(TEXT("host and CMC"), Host) || !TestNotNull(TEXT("CMC"), CMC))
	{
		H.Teardown();
		return false;
	}

	// 盾在初始化期绑上 CMC 的后转换口（生产绑定路径本身要有回归）。
	TestTrue(TEXT("shield bound to the CMC post-convert hook"),
		CMC->ProcessRootMotionPostConvertToWorld.IsBoundToObject(Host));

	// 恒定根的典型输入：零位移 + 无关键旋转（表现 clip 的根轨道形态）。
	const FTransform ConstantRoot(FQuat(FVector::UpVector, 0.3f), FVector::ZeroVector);
	const float Dt = 0.025f;

	// ① 空中：位移换成 V*Δt、旋转清零 ⇒ translation/dt 恰好回到 V，水平不丢。
	CMC->SetMovementMode(MOVE_Falling);
	CMC->Velocity = FVector(900.0, -38.0, -1500.0);
	Host->SetGrounded(false);
	const FTransform Shielded = Host->ShieldAerialAnimRootMotionForTest(ConstantRoot, Dt);
	TestTrue(TEXT("airborne rotation is neutralized"), Shielded.GetRotation().IsIdentity());
	TestTrue(TEXT("airborne translation re-derives the current velocity"),
		Shielded.GetTranslation().Equals(CMC->Velocity * Dt, 0.01f));
	TestTrue(TEXT("horizontal speed survives the constrain replacement"),
		FMath::Abs((Shielded.GetTranslation() / Dt).Size2D() - 900.0f) < 1.0f);

	// ② 地面（无表现会话）：原样放行。
	CMC->SetMovementMode(MOVE_Walking);
	Host->SetGrounded(true);
	const FTransform Passed = Host->ShieldAerialAnimRootMotionForTest(ConstantRoot, Dt);
	TestTrue(TEXT("grounded locomotion root motion passes through"),
		Passed.GetTranslation().IsNearlyZero() && !Passed.GetRotation().IsIdentity());

	// ③ 落地表现窗口（人在地面）：仍进盾 —— 337 重设不许被 148 的根抹掉。
	// harness 的 CharacterMesh0 无 SkeletalMesh ⇒ 没有 AnimInstance，认领可能过不了
	// 表现守卫 —— 认领成功才断言窗口；337 重设（守卫之前）无论认领与否都断言。
	Host->PlayAerialLandingPresentation();
	TestTrue(TEXT("the 337 reset runs before the presentation guard"),
		FMath::Abs(FVector2D(CMC->Velocity.X, CMC->Velocity.Y).Size() - 337.0f) <= 1.0f);
	if (Host->IsAerialLanding())
	{
		const FTransform LandingShielded =
			Host->ShieldAerialAnimRootMotionForTest(ConstantRoot, Dt);
		TestTrue(TEXT("landing presentation window stays shielded even on the ground"),
			LandingShielded.GetRotation().IsIdentity()
			&& LandingShielded.GetTranslation().Equals(CMC->Velocity * Dt, 0.01f));
	}
	else
	{
		AddInfo(TEXT("landing window assert skipped: no AnimInstance in this harness"));
	}

	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5AirDodgePresentationRootMotionTest,
	"MHGZ.M5.Aerial.PresentationClipsHaveNoRootMotion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 137→下坠缝「速度骤变」的根因钉（PIE 第 5 轮，命令列 MHGZStripPresentationRootMotion
// 的回归锁）：表现 clip 的根运动抽取必须关死。恒定 (0,0,-111) 根一旦被
// Accumulate(identity) 置起 bHasRootMotion，ConstrainAnimRootMotionVelocity
// （Falling：XY:=动画根速度、Z 保留）就把水平 900 抹成 ~0。
// bForceRootLock 保根骨钉首帧的观感；copied_from_montage 标记 + 蒙太奇废弃字段
// 关断 PostLoad 强制回灌（A9 式静默还原的两条路）。把任一字段改回去 ⇒ 这条会红。
bool FMHGZM5AirDodgePresentationRootMotionTest::RunTest(const FString& Parameters)
{
	const UInsectGlaiveCombatConfig* Config = LoadObject<UInsectGlaiveCombatConfig>(
		nullptr, TEXT("/Game/Weapons/InsectGlaive/Data/DA_IG_Combat.DA_IG_Combat"));
	if (!TestNotNull(TEXT("DA_IG_Combat loads"), Config))
	{
		return false;
	}

	const UAnimMontage* DodgeFall = Config->GetAerialDodgeFallMontage();
	const UAnimMontage* WhiteFall = Config->GetAerialFallingMontage(true);
	const UAnimMontage* PlainFall = Config->GetAerialFallingMontage(false);
	const UAnimMontage* Landing = Config->GetAerialLandingMontage();
	const UAnimMontage* Dodge = LoadObject<UAnimMontage>(nullptr,
		TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_AirDodge.AM_IG_AirDodge"));

	const UAnimMontage* Montages[] = { DodgeFall, WhiteFall, PlainFall, Landing, Dodge };
	for (const UAnimMontage* Montage : Montages)
	{
		if (!TestNotNull(TEXT("presentation montage assigned"), Montage))
		{
			continue;
		}
		TestFalse(TEXT("presentation montage extracts no root motion"),
			Montage->HasRootMotion());
		TestFalse(TEXT("montage deprecated translation flag stays off (PostLoad re-enable vector)"),
			Montage->bEnableRootMotionTranslation);
		TestFalse(TEXT("montage deprecated rotation flag stays off (PostLoad re-enable vector)"),
			Montage->bEnableRootMotionRotation);

		for (const FSlotAnimationTrack& Track : Montage->SlotAnimTracks)
		{
			for (const FAnimSegment& Segment : Track.AnimTrack.AnimSegments)
			{
				const UAnimSequence* Seq = Cast<UAnimSequence>(Segment.GetAnimReference().Get());
				if (!TestNotNull(TEXT("montage segment source is a sequence"), Seq))
				{
					continue;
				}
				TestFalse(TEXT("source clip has root motion extraction disabled"), Seq->bEnableRootMotion);
				TestTrue(TEXT("source clip keeps the root locked to the first frame"), Seq->bForceRootLock);
				TestTrue(TEXT("montage re-enable channel is sealed"), Seq->bRootMotionSettingsCopiedFromMontage);
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5AirDodgeHandoffWindowTest,
	"MHGZ.M5.Aerial.MontageEndCompletionWindow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 收尾判据（缺口文档 §6.5）：空回蒙太奇关掉 `bEnableAutoBlendOut` 之后不会自己终止，也不会再
// 广播 `OnCompleted` —— 实例停在自身长度保持终末姿势，等 `EndAbility` 用 `Montage_Stop(0.05)`
// 把它淡出成撑杆跳那种真交叉帧。于是「播完了」这件事必须由任务按**位置**如实上报：
//
//   * 判据是「位置已到自身长度」，**不是**剩余 ≤ 一帧的提前量 —— 提前量随帧率漂，事实不漂；
//   * 容差要大于引擎的末尾夹取 `SectionEnd − KINDA_SMALL_NUMBER/2`（长度 − 5e-5），
//     又要远小于一帧，所以 1e-3 s：宽 20 倍于夹取量、窄 25 倍于一帧（40 Hz）。
//
// 纯函数，不需要世界，故可离线钉住。
bool FMHGZM5AirDodgeHandoffWindowTest::RunTest(const FString& Parameters)
{
	using Task = UAbilityTask_MHGZPlayMontageAndWait;
	constexpr float DodgeLength = 1.18333f;  // AM_IG_AirDodge 实测长度

	// 引擎末尾把那帧的位置夹在 `长度 − 5e-5`：必须判成「已到」。
	TestTrue(TEXT("engine's clamped terminal position counts as reached"),
		Task::HasMontageReachedEnd(DodgeLength - 5e-5f, DodgeLength));
	TestTrue(TEXT("position exactly at the length counts as reached"),
		Task::HasMontageReachedEnd(DodgeLength, DodgeLength));

	// 还没播完就不许报 —— 尤其是提前一帧那一档（40 Hz 的 1/25 帧只差 1 ms，仍在容差内，
	// 这是刻意的：早 1 ms 报等于早一帧交棒，而撑杆跳配方对早一帧是稳的）。
	TestFalse(TEXT("one frame before the end does not fire (40Hz)"),
		Task::HasMontageReachedEnd(DodgeLength - 0.0251f, DodgeLength));
	TestFalse(TEXT("mid-montage does not fire"),
		Task::HasMontageReachedEnd(DodgeLength * 0.5f, DodgeLength));
	TestFalse(TEXT("zero position does not fire"),
		Task::HasMontageReachedEnd(0.0f, DodgeLength));

	// 与具体资产解耦：判据只吃「位置 / 长度」两个数。
	TestTrue(TEXT("works for a shorter montage too"),
		Task::HasMontageReachedEnd(0.6499f, 0.6500f));
	TestFalse(TEXT("and does not fire for it before its end"),
		Task::HasMontageReachedEnd(0.6000f, 0.6500f));

	// 退化输入：空蒙太奇不得被判成「永远没播完」而把动作卡住。
	TestTrue(TEXT("a zero-length montage counts as reached"),
		Task::HasMontageReachedEnd(0.0f, 0.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5LandingRealTouchdownTest,
	"MHGZ.M5.Aerial.LandingRealTouchdownTwoEntrySpeeds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// P1-1 的**真实触地**验收（此前只有「手设速度 + 直接调表现入口」的函数内形态）：
// 让胶囊在真的地板几何上落到 MOVE_Walking，走完
// CMC::ProcessLanded → AMHGZCharacter::Landed → Host::HandleLanded → PlayAerialLandingVisual
// 这条生产链，并按三个**数据独立**的采样点断言。
//
// **采样相位（§D，2026-09-24）**：真值 148 首帧 = 落地动作第一帧，对应项目里**落地帧帧末**的值
// （`CMC.LandingSpeedReset.Applied` / `CMC.PerformMovement.Post`）。本测试用 CMC 的帧边界样本把它做成判据：
// 落地帧 Pre = 入场速度、落地帧 Post = 337、下一帧 Pre = 337。
// ⚠ 这里**不**背书 CSV 口径：`Spatial.csv` 由 `NativeUpdateAnimation` 写出，而它在项目中由 `PerformMovement`
// 内部在 `TickCharacterPose` 处驱动（`CharacterMovementComponent.cpp:2824`）⇒ 它记的是**本帧位移积分前**的
// 速度（不是「本帧 PerformMovement 之前」；两者多数帧相同，但有帧内写速的例外），且落地帧那行的**下一帧**
// 可能被到期 RMS 源的 FinishVelocity 冲掉（真实游戏里 2/14，见方案文档 P1-4）——判「重设有没有发生」要读
// `MovementPhases.csv` 的 `CMC.LandingSpeedReset.Queued/Applied`。
//
// ⚠ 曾经写成「三个采样点各断言」，但其中「下一帧值」与「同帧后值」读的是同一个 Velocity、中间没有任何
// 写入（harness 世界不 tick）⇒ 那只是同一次写入的两次读法。现在三点分别来自帧边界样本 Pre / Post /
// 计数器，互不重复。
bool FMHGZM5LandingRealTouchdownTest::RunTest(const FString& Parameters)
{
	UInsectGlaiveCombatConfig* Config = LoadObject<UInsectGlaiveCombatConfig>(
		nullptr, TEXT("/Game/Weapons/InsectGlaive/Data/DA_IG_Combat.DA_IG_Combat"));
	if (!TestNotNull(TEXT("production combat config loads"), Config))
	{
		return false;
	}
	UAnimMontage* FallMontage = Config->GetAerialDodgeFallMontage();
	if (!TestNotNull(TEXT("the fall montage resolves (157)"), FallMontage))
	{
		return false;
	}
	// 期望值取自配置，而不是把 337 再抄一遍（否则实现里的兜底常量会让断言变成常量对常量）。
	const float Expected = Config->GetAerialLandingHorizontalSpeed();
	TestTrue(TEXT("the production config authors the landing speed as 337"),
		FMath::Abs(Expected - 337.0f) <= 0.5f);

	// 帧边界样本只在遥测开着时记录。开的是 CMC 自己的采样缓冲，与 CSV 写手无关
	// （测试世界的 AnimInstance 是基础 UAnimInstance，不会起那个写手），所以不会落盘。
	IConsoleVariable* TelemetryCVar = IConsoleManager::Get().FindConsoleVariable(
		TEXT("mhgz.Telemetry.Enable"));
	if (!TestNotNull(TEXT("the telemetry switch exists"), TelemetryCVar))
	{
		return false;
	}
	struct FScopedTelemetry
	{
		IConsoleVariable* CVar = nullptr;
		~FScopedTelemetry() { if (CVar) { CVar->Set(TEXT("0"), ECVF_SetByCode); } }
	} TelemetryGuard{ TelemetryCVar };
	TelemetryCVar->Set(TEXT("1"), ECVF_SetByCode);

	const float EntrySpeeds[] = { 900.0f, 300.0f };
	const FVector Directions[] = { FVector(1.0f, 0.0f, 0.0f), FVector(-0.7071f, -0.7071f, 0.0f) };
	constexpr float Tolerance = 6.0f;   // 真值允差 337±6
	constexpr float Step = 0.02f;

	// 造一具「有地板 / 无地板」的台架。无地板那份是**负控**：没有它，「这条绿来自那块地板」
	// 就只是注释里的话，而不是测试自己证明的事实。
	auto BuildLandingSetup = [&](bool bWithFloor, FAirDodgeHarness& OutHarness,
		UMHGZInstrumentedCharacterMovementComponent*& OutCMC, TObjectPtr<AActor>& OutFloor) -> bool
	{
		if (!OutHarness.Build())
		{
			return false;
		}
		OutHarness.Host->SetCombatConfigForTest(Config);
		USkeletalMeshComponent* Mesh = OutHarness.Character->GetMesh();
		OutCMC = Cast<UMHGZInstrumentedCharacterMovementComponent>(
			OutHarness.Character->GetCharacterMovement());
		if (!OutCMC || !Mesh)
		{
			return false;
		}
		AActor* Floor = OutHarness.World->SpawnActor<AActor>(AActor::StaticClass(),
			FVector(0.0f, 0.0f, -60.0f), FRotator::ZeroRotator);
		UBoxComponent* FloorBox = NewObject<UBoxComponent>(Floor);
		Floor->SetRootComponent(FloorBox);
		FloorBox->SetBoxExtent(FVector(4000.0f, 4000.0f, 50.0f));
		FloorBox->SetCollisionEnabled(bWithFloor
			? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
		FloorBox->SetCollisionObjectType(ECC_WorldStatic);
		FloorBox->SetCollisionResponseToAllChannels(ECR_Block);
		FloorBox->RegisterComponent();
		Floor->SetActorLocation(FVector(0.0f, 0.0f, -60.0f));   // 顶面在 Z = −10
		OutFloor = Floor;

		Mesh->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr,
			TEXT("/Game/Characters/Demo/Meshes/Body/SKM_Demo_Body.SKM_Demo_Body")));
		Mesh->SetAnimInstanceClass(UAnimInstance::StaticClass());
		OutHarness.Character->SetActorLocation(FVector(0.0f, 0.0f,
			OutHarness.Character->GetDefaultHalfHeight() + 6.0f));
		OutCMC->SetMovementMode(MOVE_Falling);
		OutHarness.Host->SetGrounded(false);
		return OutHarness.Host->BeginAerialFalling(false,
			AirDodgeTag(TEXT("Combat.State.Aerial.Falling.IG_AirDodge")), FallMontage);
	};

	// ── 负控：没有地板就永远不落地 ───────────────────────────────────────────
	{
		FAirDodgeHarness Bare;
		UMHGZInstrumentedCharacterMovementComponent* BareCMC = nullptr;
		TObjectPtr<AActor> BareFloor;
		if (TestTrue(TEXT("negative-control harness builds"),
			BuildLandingSetup(false, Bare, BareCMC, BareFloor)))
		{
			BareCMC->Velocity = FVector(900.0f, 0.0f, -400.0f);
			int32 BareSteps = 0;
			for (; BareSteps < 60; ++BareSteps)
			{
				BareCMC->PerformMovement(Step);
				if (BareCMC->MovementMode == MOVE_Walking || BareCMC->MovementMode == MOVE_NavWalking)
				{
					break;
				}
			}
			TestTrue(TEXT("without the floor the capsule never lands (so the green below comes from it)"),
				BareSteps == 60);
			TestEqual(TEXT("and no landing reset was applied"),
				BareCMC->GetLandingSpeedResetApplyCountForTest(), 0);
			Bare.Teardown();
		}
	}

	for (int32 Index = 0; Index < 2; ++Index)
	{
		FAirDodgeHarness H;
		UMHGZInstrumentedCharacterMovementComponent* CMC = nullptr;
		TObjectPtr<AActor> Floor;
		const FString Label = FString::Printf(TEXT("[entry %.0f] "), EntrySpeeds[Index]);
		if (!TestTrue(*(Label + TEXT("harness builds with the floor")),
			BuildLandingSetup(true, H, CMC, Floor)))
		{
			H.Teardown();
			continue;
		}

		const FVector Entry = Directions[Index] * EntrySpeeds[Index] + FVector(0.0f, 0.0f, -400.0f);
		const float IncomingPlanar = Entry.Size2D();
		const FVector EntryDirection = Entry.GetSafeNormal2D();
		CMC->Velocity = Entry;

		const uint64 SerialBeforeLanding = CMC->GetLatestAerialMovementSampleSerial();
		int32 LandingStep = INDEX_NONE;
		for (int32 Iteration = 0; Iteration < 60; ++Iteration)
		{
			CMC->PerformMovement(Step);
			if (CMC->MovementMode == MOVE_Walking || CMC->MovementMode == MOVE_NavWalking)
			{
				LandingStep = Iteration;
				break;
			}
		}
		if (!TestTrue(*(Label + TEXT("the capsule really landed")), LandingStep != INDEX_NONE))
		{
			H.Teardown();
			continue;
		}
		// 地板身份 + 几何：落在**那块**合成地板上，且真的是下落撞上去的（不是初始就在地面判定内）。
		TestTrue(*(Label + TEXT("it landed on the synthetic floor actor")),
			CMC->CurrentFloor.HitResult.GetActor() == Floor.Get());
		TestTrue(*(Label + TEXT("it fell before landing (not spawned inside the floor)")), LandingStep >= 1);
		TestTrue(*(Label + TEXT("the impact point sits on the floor top")),
			FMath::Abs(CMC->CurrentFloor.HitResult.ImpactPoint.Z + 10.0f) <= 2.0f);

		// ① 回调载荷：回调那次写、队列、帧末应用都是同一个载荷 —— 值 = 配置值、方向 = 入场方向。
		//    方向这条对斜向那档有信息量：走「近零速取角色朝向」的回退会写成 +X，dot 只有 0.707 ⇒ 红。
		TestTrue(*(Label + TEXT("the landing payload is the authored speed")),
			FMath::Abs(CMC->GetLastAppliedLandingSpeedForTest() - Expected) <= 1.0f);
		TestTrue(*(Label + TEXT("the payload kept the incoming direction (not the facing fallback)")),
			FVector::DotProduct(CMC->GetLastAppliedLandingDirectionForTest(), EntryDirection) > 0.99f);
		TestEqual(*(Label + TEXT("the reset is applied exactly once for this landing")),
			CMC->GetLandingSpeedResetApplyCountForTest(), 1);

		// ② 帧边界样本：再走一帧，然后读「落地帧 Pre / 落地帧 Post / 下一帧 Pre」三个点。
		CMC->PerformMovement(Step);
		TArray<FMHGZAerialMovementPhaseSample> Samples;
		CMC->GetAerialMovementSamplesSince(SerialBeforeLanding, Samples);
		TArray<float> PreSpeeds;
		TArray<float> PostSpeeds;
		for (const FMHGZAerialMovementPhaseSample& Sample : Samples)
		{
			const float Planar = FVector2D(Sample.Velocity.X, Sample.Velocity.Y).Size();
			if (Sample.Phase == FName(TEXT("CMC.PerformMovement.Pre"))) { PreSpeeds.Add(Planar); }
			else if (Sample.Phase == FName(TEXT("CMC.PerformMovement.Post"))) { PostSpeeds.Add(Planar); }
		}
		// 采样点按**落地帧**取（胶囊要先掉几帧才触地，所以 [0] 不是落地帧）。
		if (TestTrue(*(Label + FString::Printf(
				TEXT("frame-boundary samples cover the landing frame (landing step %d)"), LandingStep)),
			PreSpeeds.IsValidIndex(LandingStep + 1) && PostSpeeds.IsValidIndex(LandingStep)))
		{
			TestTrue(*(Label + FString::Printf(
				TEXT("landing frame ENTERS at the incoming speed (%.1f vs %.1f) - this is the Spatial.csv sample"),
				PreSpeeds[LandingStep], IncomingPlanar)),
				FMath::Abs(PreSpeeds[LandingStep] - IncomingPlanar) <= Tolerance);
			TestTrue(*(Label + FString::Printf(
				TEXT("landing frame LEAVES at 337+-6 (got %.1f) - the reset beats same-frame walking braking"),
				PostSpeeds[LandingStep])),
				FMath::Abs(PostSpeeds[LandingStep] - Expected) <= Tolerance);
			TestTrue(*(Label + FString::Printf(
				TEXT("the next frame ENTERS at 337+-6 (got %.1f) - the defined sampling phase"),
				PreSpeeds[LandingStep + 1])),
				FMath::Abs(PreSpeeds[LandingStep + 1] - Expected) <= Tolerance);
		}

		// ③ 不每帧锁速：多跑的那一帧之后重设次数仍为 1，且速度已被 Walking 制动显著削掉。
		//    上界必须留余量 —— 曾经的 `AfterWalking < 337 + 1e-4` 恰好被「每帧写 337」满足。
		TestEqual(*(Label + TEXT("no further reset was applied on the following frame")),
			CMC->GetLandingSpeedResetApplyCountForTest(), 1);
		const float AfterWalking = FVector2D(CMC->Velocity.X, CMC->Velocity.Y).Size();
		TestTrue(*(Label + FString::Printf(
			TEXT("walking braking takes over (%.1f, must decay below 337-20)"), AfterWalking)),
			AfterWalking < Expected - 20.0f);
		// 这一条世界没有 Controller ⇒ 落地后的走路更新会把速度直接清零（不是游戏里的摩擦衰减曲线），
		// 所以「衰减到多少」在这里没有判据价值 —— 真正抓「每帧锁速」的是上面那个 count：
		// 锁速实现（不清 pending 或每帧重新 queue）会让它在下一帧变成 2。
		// 游戏里那条衰减曲线由 PIE 遥测覆盖（`337 → 250 量级` 逐帧下降，且不触发 AerialMovementDiscontinuity）。
		TestTrue(*(Label + FString::Printf(
			TEXT("the reset never exceeds the authored speed afterwards (%.1f)"), AfterWalking)),
			AfterWalking <= Expected + KINDA_SMALL_NUMBER);

		H.Teardown();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5AirDodgeVisualTeardownPinsTest,
	"MHGZ.M5.Aerial.AirDodgeVisualTaskTicksAndHoldsTerminalPose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 收尾设计的四条不变量，各自**曾经独立失效过**，所以分开钉（2026-09-23）：
//
//  ① 视觉任务必须处于「会 tick」的状态，且必须是**登记之前**就置真的。引擎只在激活那一刻
//     登记 tick（`GameplayTasksComponent.cpp:86`，`bTickingTask` 默认 false），之后再置 true
//     是静默无效 ⇒ `TickTask` 成死代码 ⇒ `ReportCompletionAtMontageEnd()` 永不上报 ⇒
//     `bAirDodgeVisualFinished` 恒假 ⇒ 收尾一直等到触地 ⇒ 下坠段姿势冻结在终末姿势
//     （录 `20260923-225916`：9 帧 `RootBoneRelZ` 恒 −101.00、胶囊却下落 361 cm）。
//     ⚠ 只看 `IsTickingTask()` 挡不住「挪到 ReadyForActivation 之后」这种改法（那个位是粘性的、
//     且移除时也不清），所以这里直接查**任务在不在 ticking 列表里**（登记与否的真凭据）。
//  ② 蒙太奇实例必须保持终末姿势（`bEnableAutoBlendOut == false`）—— 否则收尾那一帧实例已被
//     引擎终止，`EndAbility` 的 `Montage_Stop(0.05f)` 成空操作，缝上会掉一帧「无蒙太奇」。
//     实例位是从**资产**拷来的（`AnimMontage.cpp:1506`），所以同时断言资产仍是 true ——
//     两半合起来才证明「是这行 C++ 关的」，只有实例那一半的话，资产改一下就能替掉代码而不红。
//  ③ 判据的**前提**是资产事实：`HasMontageReachedEnd` 拿 `GetPlayLength()` 当终点，而引擎在末尾
//     夹的是**当前段的末端**。单段蒙太奇时两者相等；重切成多段／非连续段后位置就永远够不到长度
//     ⇒ 判据永假 ⇒ 动作卡住（比原来的视觉缺陷更严重）。钉段数，让「前提失效」红出来。
//     （不钉「末段末端 == 长度」：单段蒙太奇下 `GetSectionStartAndEndTime` 内部就是取
//      `GetPlayLength()`，那是恒真断言、只会造成覆盖率的错觉。）
//  ④ 机制端到端：把位置推到长度、手动驱动一次 `TickTask`，断言 `OnCompleted` 真的走到
//     `bAirDodgeVisualFinished`。harness 的世界不 tick（整个文件没有 World->Tick），
//     所以这里手动驱动 —— 这段链路正是「死了两轮」的那一段，此前零覆盖。
bool FMHGZM5AirDodgeVisualTeardownPinsTest::RunTest(const FString& Parameters)
{
	FAirDodgeHarness H;
	if (!TestTrue(TEXT("harness built"), H.Build()))
	{
		H.Teardown();
		return false;
	}
	UInsectGlaiveCombatConfig* Config = LoadObject<UInsectGlaiveCombatConfig>(
		nullptr, TEXT("/Game/Weapons/InsectGlaive/Data/DA_IG_Combat.DA_IG_Combat"));
	if (!TestNotNull(TEXT("production combat config loads"), Config))
	{
		H.Teardown();
		return false;
	}
	// 有战斗配置才会走到 StartAttackMontage 的最后两行（没有会在重力 profile 处提前收尾）。
	H.Host->SetCombatConfigForTest(Config);

	TObjectPtr<UAnimMontage> SavedMontage;
	UMHGZAirDodgeAbility* Action = ActivateRealAirDodge(H, SavedMontage);
	if (!TestNotNull(TEXT("production AirDodge ability is active (also fails if SKM_Demo_Body is missing)"), Action))
	{
		H.Teardown();
		return false;
	}

	// ① 任务会 tick，且**已经登记进 ticking 列表**（只看旗标挡不住「置得太晚」那种改法）。
	UAbilityTask_MHGZPlayMontageAndWait* Task = Action->GetAirDodgeVisualTaskForTest();
	if (TestNotNull(TEXT("the visual task is live right after activation"), Task))
	{
		TestTrue(TEXT("the visual task must have bTickingTask set (TickTask is dead code otherwise)"),
			Task->IsTickingTask());
		const UGameplayTasksComponent* Tasks = Task->GetGameplayTasksComponent();
		bool bRegistered = false;
		if (Tasks)
		{
			for (auto It = Tasks->GetTickingTaskIterator(); It; ++It)
			{
				if (*It == Task)
				{
					bRegistered = true;
					break;
				}
			}
		}
		TestTrue(TEXT("the task must be registered in the ticking list (set bTickingTask BEFORE ReadyForActivation)"),
			bRegistered);
	}

	// ② 实例保持终末姿势，且这一位是**本行 C++** 关的（资产仍为 true ⇒ 不是资产替的）。
	UAnimInstance* Anim = H.Character && H.Character->GetMesh()
		? H.Character->GetMesh()->GetAnimInstance() : nullptr;
	FAnimMontageInstance* Instance = Anim ? Anim->GetActiveInstanceForMontage(SavedMontage) : nullptr;
	if (TestNotNull(TEXT("the dodge montage instance is live"), Instance))
	{
		TestFalse(TEXT("the dodge montage must keep its terminal pose (bEnableAutoBlendOut == false)"),
			Instance->bEnableAutoBlendOut);
		if (SavedMontage)
		{
			TestTrue(TEXT("the asset still ships with auto-blend-out ON (so the flip is the C++ line's)"),
				SavedMontage->bEnableAutoBlendOut);
		}
	}

	// ③ 判据的前提：单段蒙太奇（末段末端即整个长度）。
	if (SavedMontage)
	{
		TestEqual(TEXT("the air dodge montage is a single section (the detector's premise)"),
			SavedMontage->CompositeSections.Num(), 1);
	}

	// ④ 机制：播到长度 ⇒ 手动 tick 一次 ⇒ OnCompleted 走到能力里。
	if (Task && Anim && SavedMontage)
	{
		Anim->Montage_SetPosition(SavedMontage, SavedMontage->GetPlayLength());
		Task->DriveTickForTest(0.033f);
		TestTrue(TEXT("reaching the montage length reports completion through to the ability"),
			Action->GetAirDodgeVisualFinishedForTest());
	}
	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5AirDodgeProductionMontageHandoffTest,
	"MHGZ.M5.Aerial.ProductionAirDodgeMontageToFallHandoff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// P0-2: let the real Blueprint character, AnimBP, GA task, CMC and both production
// montages progress from ordinary UWorld ticks. This is deliberately not a test of
// NotifyAerialHandoff()/completion helpers: the authored notify and montage-end task
// must drive those state changes themselves.
bool FMHGZM5AirDodgeProductionMontageHandoffTest::RunTest(const FString& Parameters)
{
	IConsoleVariable* TelemetryCVar = IConsoleManager::Get().FindConsoleVariable(
		TEXT("mhgz.Telemetry.Enable"));
	if (!TestNotNull(TEXT("the CMC telemetry switch exists"), TelemetryCVar))
	{
		return false;
	}
	struct FScopedTelemetry
	{
		IConsoleVariable* CVar = nullptr;
		int32 PreviousValue = 0;
		~FScopedTelemetry()
		{
			if (CVar)
			{
				CVar->Set(PreviousValue, ECVF_SetByCode);
			}
		}
	} TelemetryGuard{ TelemetryCVar, TelemetryCVar->GetInt() };
	TelemetryCVar->Set(1, ECVF_SetByCode);

	FProductionAirDodgeHarness H;
	if (!TestTrue(TEXT("production BP_IG_Character, ASC, Host, config and AnimBP initialize"), H.Build()))
	{
		H.Teardown();
		return false;
	}
	if (!TestTrue(TEXT("the real GA_IG_AirDodge_C activates"), H.Activate()))
	{
		H.Teardown();
		return false;
	}

	USkeletalMeshComponent* Mesh = H.Character->GetMesh();
	UMHGZMotionMatchingAnimInstance* Anim = Mesh
		? Cast<UMHGZMotionMatchingAnimInstance>(Mesh->GetAnimInstance()) : nullptr;
	UMHGZInstrumentedCharacterMovementComponent* CMC = Cast<UMHGZInstrumentedCharacterMovementComponent>(
		H.Character->GetCharacterMovement());
	if (!TestNotNull(TEXT("the production Motion Matching AnimInstance remains installed"), Anim)
		|| !TestNotNull(TEXT("BP character uses the instrumented production CMC"), CMC)
		|| !TestTrue(TEXT("the actual air-dodge montage is active"),
			Anim && Anim->Montage_IsActive(H.DodgeMontage)))
	{
		H.Teardown();
		return false;
	}

	// Inspect only the real task's registration; never call DriveTickForTest.
	UAbilityTask_MHGZPlayMontageAndWait* Task = H.Action->GetAirDodgeVisualTaskForTest();
	bool bTaskRegisteredForTick = false;
	if (Task)
	{
		if (const UGameplayTasksComponent* Tasks = Task->GetGameplayTasksComponent())
		{
			for (auto It = Tasks->GetTickingTaskIterator(); It; ++It)
			{
				if (*It == Task)
				{
					bTaskRegisteredForTick = true;
					break;
				}
			}
		}
	}
	TestTrue(TEXT("the real montage task is registered for ticking before the world advances"),
		Task && Task->IsTickingTask() && bTaskRegisteredForTick);

	constexpr float Dt = 1.0f / 60.0f;
	constexpr int32 MaxFrames = 180;
	constexpr float WeightEpsilon = 0.01f;
	struct FScopedFrameCounter
	{
		uint64 InitialValue = GFrameCounter;
		~FScopedFrameCounter() { GFrameCounter = InitialValue; }
	} FrameCounterGuard;
	bool bSawDodgePose = false;
	bool bSawSource = false;
	bool bSawSourceExpire = false;
	bool bSawActionableNotify = false;
	bool bSawAbilityEnd = false;
	bool bSawSystemFall = false;
	bool bSawFallMontagePose = false;
	bool bSawFallMontageAdvance = false;
	bool bSawMontageWeightOverlap = false;
	bool bSawZeroWeightGapBeforeFall = false;
	bool bSawBlockingImpact = false;
	float MaxDodgePosition = 0.0f;
	float MaxFallPosition = 0.0f;
	float WorstTailSpeedError = 0.0f;
	float WorstTailStepDisplacementError = 0.0f;
	int32 TailMovementSamples = 0;
	int32 FallPoseFrames = 0;
	FVector FinalLocation = H.Character->GetActorLocation();
	uint64 LastSampleSerial = CMC->GetLatestAerialMovementSampleSerial();

	for (int32 Frame = 0; Frame < MaxFrames; ++Frame)
	{
		H.World->Tick(ELevelTick::LEVELTICK_All, Dt);
		// Manual test-world ticks don't advance the engine's global frame number. Without
		// this, SkeletalMeshComponent::PoseTickedThisFrame stays true after tick one and
		// the production AnimBP never gets another pose tick.
		++GFrameCounter;

		// GetInstanceForMontage retains a montage while it is blending out; the Active
		// accessor drops the outgoing instance precisely when this overlap is meaningful.
		const FAnimMontageInstance* DodgeInstance = Anim->GetInstanceForMontage(H.DodgeMontage);
		const FAnimMontageInstance* FallInstance = Anim->GetInstanceForMontage(H.FallMontage);
		const float DodgeWeight = DodgeInstance ? DodgeInstance->GetWeight() : 0.0f;
		const float FallWeight = FallInstance ? FallInstance->GetWeight() : 0.0f;
		const float DodgePosition = Anim->Montage_GetPosition(H.DodgeMontage);
		const float FallPosition = Anim->Montage_GetPosition(H.FallMontage);
		MaxDodgePosition = FMath::Max(MaxDodgePosition, DodgePosition);
		MaxFallPosition = FMath::Max(MaxFallPosition, FallPosition);

		bSawDodgePose |= DodgeWeight > WeightEpsilon;
		bSawFallMontagePose |= FallWeight > WeightEpsilon;
		bSawFallMontageAdvance |= FallPosition >= 2.0f * Dt;
		bSawMontageWeightOverlap |= DodgeWeight > WeightEpsilon && FallWeight > WeightEpsilon;
		if (bSawDodgePose && !H.Host->IsAerialFalling()
			&& DodgeWeight + FallWeight <= WeightEpsilon)
		{
			bSawZeroWeightGapBeforeFall = true;
		}
		bSawActionableNotify |= H.ASC->HasMatchingGameplayTag(
			AirDodgeTag(TEXT("Combat.State.Aerial.Actionable")));
		bSawAbilityEnd |= !H.Action->IsActive();
		bSawSystemFall |= H.Host->IsAerialFalling();

		const int32 ActiveSourceCount = CMC->GetCurrentActiveRootMotionSourceCount();
		bSawSource |= ActiveSourceCount > 0;
		if (bSawSource && ActiveSourceCount == 0)
		{
			bSawSourceExpire = true;
		}

		TArray<FMHGZAerialMovementPhaseSample> Samples;
		CMC->GetAerialMovementSamplesSince(LastSampleSerial, Samples);
		for (const FMHGZAerialMovementPhaseSample& Sample : Samples)
		{
			LastSampleSerial = FMath::Max(LastSampleSerial, Sample.Serial);
			bSawBlockingImpact |= Sample.bHasMovementImpact && Sample.bImpactBlockingHit;
		}

		if (bSawSourceExpire && CMC->MovementMode == MOVE_Falling && !bSawBlockingImpact)
		{
			const float PlanarSpeed = FVector2D(CMC->Velocity.X, CMC->Velocity.Y).Size();
			WorstTailSpeedError = FMath::Max(WorstTailSpeedError,
				FMath::Abs(PlanarSpeed - 900.0f));
			++TailMovementSamples;

			const FMHGZAerialMovementPhaseSample* PreSample = nullptr;
			const FMHGZAerialMovementPhaseSample* PostSample = nullptr;
			for (const FMHGZAerialMovementPhaseSample& Sample : Samples)
			{
				if (Sample.Phase == FName(TEXT("CMC.PerformMovement.Pre")))
				{
					PreSample = &Sample;
				}
				else if (Sample.Phase == FName(TEXT("CMC.PerformMovement.Post")))
				{
					PostSample = &Sample;
				}
			}
			if (PreSample && PostSample)
			{
				const float MeasuredStep = FVector2D(
					PostSample->Location.X - PreSample->Location.X,
					PostSample->Location.Y - PreSample->Location.Y).Size();
				const float ExpectedStep = 900.0f * PostSample->DeltaSeconds;
				WorstTailStepDisplacementError = FMath::Max(WorstTailStepDisplacementError,
					FMath::Abs(MeasuredStep - ExpectedStep));
			}
		}

		if (FallWeight > WeightEpsilon)
		{
			++FallPoseFrames;
		}
		FinalLocation = H.Character->GetActorLocation();
		if (bSawFallMontageAdvance && FallPoseFrames >= 4 && bSawAbilityEnd)
		{
			break;
		}
	}

	AddInfo(FString::Printf(
		TEXT("Natural handoff: Dodge %.3f/%.3f s, Fall %.3f/%.3f s, RMS expired=%d, tail samples=%d, worst XY speed error=%.2f, worst step displacement error=%.2f cm, final capsule=%s"),
		MaxDodgePosition, H.DodgeMontage->GetPlayLength(), MaxFallPosition,
		H.FallMontage->GetPlayLength(), bSawSourceExpire ? 1 : 0, TailMovementSamples,
		WorstTailSpeedError, WorstTailStepDisplacementError, *FinalLocation.ToCompactString()));

	TestTrue(TEXT("the production AnimBP evaluated the dodge montage through its authored end"),
		MaxDodgePosition >= H.DodgeMontage->GetPlayLength() - 2.0f * Dt);
	TestTrue(TEXT("the authored aerial handoff notify naturally made the action actionable"),
		bSawActionableNotify);
	TestTrue(TEXT("the CMC ballistic root-motion source was present and expired naturally"),
		bSawSource && bSawSourceExpire);
	TestTrue(TEXT("the real GA ended from its montage task rather than a test callback"),
		bSawAbilityEnd);
	TestTrue(TEXT("the Host naturally started the configured aerial-fall presentation"),
		bSawSystemFall && H.Host->IsAerialFalling());
	TestTrue(TEXT("the configured 157 fall montage evaluated and advanced at least two frames"),
		bSawFallMontagePose && bSawFallMontageAdvance && FallPoseFrames >= 4);
	TestFalse(TEXT("dodge-to-fall never exposed a zero-weight montage gap"),
		bSawZeroWeightGapBeforeFall);
	TestTrue(TEXT("dodge and fall montage weights overlapped during the handoff"),
		bSawMontageWeightOverlap);
	TestFalse(TEXT("the high-altitude test world produced no blocking impacts"), bSawBlockingImpact);
	TestEqual(TEXT("the capsule stayed in CMC falling through the montage handoff"),
		CMC->MovementMode, MOVE_Falling);
	TestTrue(TEXT("the unblocked post-RMS tail kept Rise's 900 cm/s horizontal tangent"),
		TailMovementSamples >= 4 && WorstTailSpeedError <= 5.0f);
	TestTrue(TEXT("post-RMS capsule displacement follows 900 cm/s at measured frame delta"),
		TailMovementSamples >= 4 && WorstTailStepDisplacementError <= 2.0f);

	H.Teardown();
	return true;
}

#endif
