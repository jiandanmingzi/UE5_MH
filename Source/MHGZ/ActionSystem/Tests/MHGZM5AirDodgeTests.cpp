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
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InsectGlaive/InsectGlaiveCombatConfig.h"
#include "InputSystem/MHGZWeaponInputRouterComponent.h"
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

#endif
