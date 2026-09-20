// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "ActionSystem/AnimNotify_IG_AerialHandoff.h"
#include "ActionSystem/AnimNotify_IG_AdvancingChargeStarted.h"
#include "ActionSystem/MHGZInsectGlaiveAbility.h"
#include "Animation/AnimMontage.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayTagsManager.h"

// ─────────────────────────────────────────────────────────────────────────────
// 覆盖范围与**未**覆盖的部分
//
// 覆盖：
//  - 蒙太奇授权探测（MontageHasAerialHandoffNotify）—— 命令列写完之后，运行时是靠
//    它判断「这个招式的蒙太奇到底有没有最早可操作帧」
//  - MOVE_Flying 的引擎契约 —— 整套架构的前提是「Flying 不跑重力/地面/落地」，
//    以及 SetDefaultMovementMode 会按地面重查。引擎升级若改了这条，这些断言先炸
//  - tag 改名（ActionWindow → Actionable）
//
// 未覆盖：EnsureVaultFlightReleased 本身。它需要 UAbilityTask_MHGZWeaponMovement
// 的实例，而该任务的 Activate() 要求一个**真正激活过**的 GA
// （IsActionActivationCommitted() + ActionToken 匹配）。现有 M5 harness 建的是
// 从未激活的 NewObject<Ability>，所以拿不到。要覆盖它得先扩 harness 到能真激活，
// 那是独立一件事；这里不假装覆盖了它，只把它所依赖的引擎行为钉住。
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5AerialHandoffDetectionTest,
	"MHGZ.M5.Aerial.HandoffNotifyDetection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5AerialHandoffDetectionTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("a null montage carries no handoff notify"),
		UMHGZInsectGlaiveAbility::MontageHasAerialHandoffNotify(nullptr));

	UAnimMontage* Bare = NewObject<UAnimMontage>();
	if (!TestNotNull(TEXT("transient montage is created"), Bare))
	{
		return false;
	}
	TestFalse(TEXT("a montage with no notifies carries no handoff notify"),
		UMHGZInsectGlaiveAbility::MontageHasAerialHandoffNotify(Bare));

	// 点通知挂在 Notify 上（不是 NotifyStateClass）。这条断言存在的意义是：
	// 命令列写的是 Notify，而退役的 IG_AerialWindow 写的是 NotifyStateClass ——
	// 两者混淆过一次，探测会静默地永远为 false。
	{
		FAnimNotifyEvent& Event = Bare->Notifies.AddDefaulted_GetRef();
		Event.NotifyName = TEXT("IGAerialHandoff");
		Event.Notify = NewObject<UAnimNotify_IG_AerialHandoff>(Bare);
		Event.TrackIndex = 0;
		TestTrue(TEXT("a point notify on the Notify slot is detected"),
			UMHGZInsectGlaiveAbility::MontageHasAerialHandoffNotify(Bare));
	}

	// 轨道无关：命令列写在自己的轨道上，但探测不该依赖那个约定 ——
	// 手工在编辑器里拖到别的轨道上依然要认。
	{
		UAnimMontage* OffTrack = NewObject<UAnimMontage>();
		FAnimNotifyEvent& Event = OffTrack->Notifies.AddDefaulted_GetRef();
		Event.NotifyName = TEXT("IGAerialHandoff");
		Event.Notify = NewObject<UAnimNotify_IG_AerialHandoff>(OffTrack);
		Event.TrackIndex = 3;
		TestTrue(TEXT("the predicate is not track-specific"),
			UMHGZInsectGlaiveAbility::MontageHasAerialHandoffNotify(OffTrack));
	}

	// 别的点通知不算数 —— 判据是类，不是「有没有点通知」。
	// 用一个真实存在的具体通知类，不用 UAnimNotify 本体：后者是抽象类，
	// NewObject 会在 UObjectGlobals 里触发 ensure。
	{
		UAnimMontage* OtherNotify = NewObject<UAnimMontage>();
		FAnimNotifyEvent& Event = OtherNotify->Notifies.AddDefaulted_GetRef();
		Event.NotifyName = TEXT("SomethingElse");
		Event.Notify = NewObject<UAnimNotify_IG_AdvancingChargeStarted>(OtherNotify);
		Event.TrackIndex = 0;
		TestFalse(TEXT("an unrelated point notify does not count"),
			UMHGZInsectGlaiveAbility::MontageHasAerialHandoffNotify(OtherNotify));
	}

	return true;
}

// MOVE_Flying 是这套架构的支点：弧之所以不可能被一次误判的触地打断，全靠
// PhysFlying 不跑重力、不 FindFloor、不 ProcessLanded。而 EnsureVaultFlightReleased
// 兜底时依赖 SetDefaultMovementMode 会重新按地面判定。两条都钉在这里。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5FlightModeContractTest,
	"MHGZ.M5.Aerial.FlightModeContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5FlightModeContractTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!World)
	{
		return false;
	}

	ACharacter* Character = World->SpawnActor<ACharacter>(
		ACharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
	if (!Character)
	{
		World->DestroyWorld(false);
		return false;
	}
	UCharacterMovementComponent* CMC = Character->GetCharacterMovement();
	if (!TestNotNull(TEXT("character has a movement component"), CMC))
	{
		World->DestroyWorld(false);
		return false;
	}

	// Flying 不是「站在地上」。AMHGZCharacter::OnMovementModeChanged 把它折算成
	// SetGrounded(false)，弧期间的角色姿态因此正确地为空中态。
	CMC->SetMovementMode(MOVE_Flying);
	TestFalse(TEXT("MOVE_Flying is not moving on ground"), CMC->IsMovingOnGround());
	TestFalse(TEXT("MOVE_Flying is not Falling either -- it is its own mode"),
		CMC->IsFalling());

	// 兜底路径的机制：从 Flying 出发，SetDefaultMovementMode 会重查地面。
	//
	// 断言**具体模式**，不只是「不是 Flying」—— EnsureVaultFlightReleased 的返回值
	// 决定 FinishMovement 把 `Completed` 翻成 `Landed` 与否，而那个返回值依赖
	// 「胶囊是不是真的站在可行走面上」。
	//
	// SetDefaultMovementMode 只在 `MOVE_Walking && GetMovementBase() == NULL` 时才
	// 回退到 Falling（CharacterMovementComponent.cpp:1308-1312）。这条断言把该行为
	// 钉在这里：测试世界没有地板，所以必须落到 Falling 而不是停在 Walking——
	// 否则一个悬空胶囊会被读成「站在地上」，悬空的弧尾就会错认触地。
	CMC->SetDefaultMovementMode();
	TestEqual(TEXT("releasing from Flying with no floor lands in MOVE_Falling, not Walking"),
		static_cast<uint8>(CMC->MovementMode), static_cast<uint8>(MOVE_Falling));
	TestFalse(TEXT("and it is therefore not moving on ground"), CMC->IsMovingOnGround());
	TestFalse(TEXT("with no floor under it"), CMC->CurrentFloor.IsWalkableFloor());

	World->DestroyWorld(false);
	return true;
}

// tag 改名是 ini 里的一行，改错了不会编译失败、只会让空中 GA 的判据永远为假。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM5AerialActionableTagTest,
	"MHGZ.M5.Aerial.ActionableTagReplacesActionWindow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM5AerialActionableTagTest::RunTest(const FString& Parameters)
{
	const FGameplayTag Actionable = FGameplayTag::RequestGameplayTag(
		FName(TEXT("Combat.State.Aerial.Actionable")), /*ErrorIfNotFound=*/false);
	TestTrue(TEXT("Combat.State.Aerial.Actionable is declared"),
		Actionable.IsValid());

	const FGameplayTag Retired = FGameplayTag::RequestGameplayTag(
		FName(TEXT("Combat.State.Aerial.ActionWindow")), /*ErrorIfNotFound=*/false);
	TestFalse(TEXT("the retired ActionWindow tag is gone"), Retired.IsValid());

	return true;
}

#endif
