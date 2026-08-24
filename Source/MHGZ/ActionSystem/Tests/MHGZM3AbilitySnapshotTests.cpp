// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include <limits>

#include "EngineUtils.h"
#include "InsectGlaive/Kinsect/IGMarkProjectile.h"
#include "MHGZM3TestHarness.h"
#include "MHGZM4TestTypes.h"

namespace
{
const FGameplayTag& SnapshotKinsectActiveTag()
{
	static const FGameplayTag Tag = M3::Tag(TEXT("WeaponResource.IG.Kinsect.Active"));
	return Tag;
}

int32 CountMarkProjectiles(UWorld* World)
{
	int32 Count = 0;
	for (TActorIterator<AIGMarkProjectile> It(World); It; ++It)
	{
		++Count;
	}
	return Count;
}

template <typename TAbility>
TAbility* GetActiveInstance(UMHGZAbilitySystemComponent& ASC,
	const FGameplayAbilitySpecHandle& Handle)
{
	FGameplayAbilitySpec* Spec = ASC.FindAbilitySpecFromHandle(Handle);
	if (!Spec)
	{
		return nullptr;
	}
	for (UGameplayAbility* Ability : Spec->GetAbilityInstances())
	{
		if (TAbility* Instance = Cast<TAbility>(Ability))
		{
			return Instance;
		}
	}
	return nullptr;
}
}

// ── 7a. LTY 送虫：Aim 快照构造不变量 + ToPoint 分支 + Recall + Mark ────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM3SendKinsectSnapshotConstruction,
	"MHGZ.M3.Abilities.LTYSendAimSnapshotConstruction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM3SendKinsectSnapshotConstruction::RunTest(const FString& Parameters)
{
	FMHGZM3Harness H;
	if (!H.Setup())
	{
		return false;
	}
	TestTrue(TEXT("LTY harness enters unsheathed pose"), H.Host->SetSheathed(false));

	const FGameplayAbilitySpecHandle SendHandle =
		H.GiveAbility(UMHGZM4TestSendKinsectAbility::StaticClass());
	const FGameplayAbilitySpecHandle RecallHandle =
		H.GiveAbility(UMHGZM4TestRecallKinsectAbility::StaticClass());
	const FGameplayAbilitySpecHandle MarkHandle =
		H.GiveAbility(UMHGZMarkKinsectTargetAbility::StaticClass());
	TestTrue(TEXT("send ability granted"), SendHandle.IsValid());
	TestTrue(TEXT("recall ability granted"), RecallHandle.IsValid());
	TestTrue(TEXT("mark ability granted"), MarkHandle.IsValid());

	AKinsect* Kinsect = H.Kinsect;
	URes_InsectGlaive* Resource = H.Resource;
	const FGameplayTag Unsheathed = M3::Tag(TEXT("Combat.State.Unsheathed"));
	const FGameplayTag Sheathed = M3::Tag(TEXT("Combat.State.Sheathed"));

	// ── 非法 LTY 快照：拒绝且状态不变 ──
	FWeaponInputSnapshot BadAim = M3::MakePosedInput(/*bGrounded=*/true, /*bSheathed=*/false);
	BadAim.Aim.Context = EWeaponAimSnapshotContext::None;
	BadAim.Aim.Direction = FVector(1.f, 0.f, 0.f);
	H.TryActivateWithInput(SendHandle, BadAim);
	TestEqual(TEXT("non-kinsect aim context rejected"),
		Kinsect->GetState(), EKinsectState::Attached);

	FWeaponInputSnapshot NaNAim = M3::MakePosedInput(true, false);
	NaNAim.Aim.Context = EWeaponAimSnapshotContext::Kinsect;
	NaNAim.Aim.Direction = FVector(std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f);
	H.TryActivateWithInput(SendHandle, NaNAim);
	TestEqual(TEXT("NaN aim direction rejected"),
		Kinsect->GetState(), EKinsectState::Attached);

	FWeaponInputSnapshot ZeroAim = M3::MakePosedInput(true, false);
	ZeroAim.Aim.Context = EWeaponAimSnapshotContext::Kinsect;
	ZeroAim.Aim.Direction = FVector::ZeroVector;
	H.TryActivateWithInput(SendHandle, ZeroAim);
	TestEqual(TEXT("zero aim direction rejected"),
		Kinsect->GetState(), EKinsectState::Attached);

	FWeaponInputSnapshot SheathedAim = M3::MakePosedInput(true, true);
	SheathedAim.Aim.Context = EWeaponAimSnapshotContext::Kinsect;
	SheathedAim.Aim.Direction = FVector(1.f, 0.f, 0.f);
	H.TryActivateWithInput(SendHandle, SheathedAim);
	TestEqual(TEXT("sheathed pose rejected for LTY send"),
		Kinsect->GetState(), EKinsectState::Attached);
	TestEqual(TEXT("no kinsect active tag after rejections"),
		H.ASC->GetTagCount(SnapshotKinsectActiveTag()), 0);

	// ── 合法 LTY（吸附中）：AlongDirection + 归一化 Aim.Direction 快照 ──
	FWeaponInputSnapshot SendInput = M3::MakePosedInput(true, false);
	SendInput.Aim.Context = EWeaponAimSnapshotContext::Kinsect;
	SendInput.Aim.Direction = FVector(300.f, 0.f, 0.f); // 未归一化 → 构造时归一
	TestTrue(TEXT("valid LTY send activates"), H.TryActivateWithInput(SendHandle, SendInput));
	TestEqual(TEXT("kinsect remains attached before SendCommit"),
		Kinsect->GetState(), EKinsectState::Attached);
	UMHGZM4TestSendKinsectAbility* Send =
		GetActiveInstance<UMHGZM4TestSendKinsectAbility>(*H.ASC, SendHandle);
	TestNotNull(TEXT("active send instance exists"), Send);
	if (Send)
	{
		TestTrue(TEXT("exact SendCommit starts the frozen request"), Send->CommitForTest());
	}
	TestEqual(TEXT("kinsect flying after send"), Kinsect->GetState(), EKinsectState::Flying);
	TestTrue(TEXT("frozen runtime token preserved in flight request"),
		Kinsect->ActiveRequest.RuntimeToken == H.Host->GetCurrentToken());
	TestEqual(TEXT("attached send uses along-direction"),
		Kinsect->ActiveRequest.TrajectoryMode, EKinsectTrajectoryMode::AlongDirection);
	TestTrue(TEXT("aim direction frozen normalized"),
		Kinsect->ActiveRequest.DirectionSnapshot.Equals(FVector(1.f, 0.f, 0.f)));
	TestEqual(TEXT("max distance from kinsect data"),
		Kinsect->ActiveRequest.MaxDistance, H.KinsectData->MaxFlightRange);
	TestEqual(TEXT("flight speed from kinsect data"),
		Kinsect->ActiveRequest.FlightSpeed, H.KinsectData->FlightSpeed);
	TestEqual(TEXT("send motion value from config"),
		Kinsect->ActiveRequest.MotionValue, H.CombatConfig->SendKinsectMotionValue);
	TestEqual(TEXT("single-hit damage mode"),
		Kinsect->ActiveRequest.DamageMode, EKinsectDamageMode::SingleHit);
	TestEqual(TEXT("first-hit extract mode"),
		Kinsect->ActiveRequest.ExtractMode, EKinsectExtractMode::FirstHitOnly);
	TestEqual(TEXT("hover post-flight policy"),
		Kinsect->ActiveRequest.PostFlightPolicy, EKinsectPostFlightPolicy::Hover);
	TestEqual(TEXT("kinsect active tag acquired"),
		H.ASC->GetTagCount(SnapshotKinsectActiveTag()), 1);
	if (Send)
	{
		Send->FinishNormallyForTest();
	}

	// ── 非吸附 LTY：ToPoint + Aim.TargetPoint 快照 ──
	FWeaponInputSnapshot ToPointInput = M3::MakePosedInput(true, false);
	ToPointInput.Aim.Context = EWeaponAimSnapshotContext::Kinsect;
	ToPointInput.Aim.Direction = FVector(1.f, 0.f, 0.f);
	ToPointInput.Aim.TargetPoint = FVector(700.f, -300.f, 100.f);
	TestTrue(TEXT("aerial/loose send activates"),
		H.TryActivateWithInput(SendHandle, ToPointInput));
	Send = GetActiveInstance<UMHGZM4TestSendKinsectAbility>(*H.ASC, SendHandle);
	TestNotNull(TEXT("active loose send instance exists"), Send);
	if (Send)
	{
		TestTrue(TEXT("loose SendCommit starts the frozen request"), Send->CommitForTest());
	}
	TestEqual(TEXT("loose send uses to-point"),
		Kinsect->ActiveRequest.TrajectoryMode, EKinsectTrajectoryMode::ToPoint);
	TestTrue(TEXT("target point frozen"),
		Kinsect->ActiveRequest.TargetPointSnapshot.Equals(FVector(700.f, -300.f, 100.f)));
	TestEqual(TEXT("still flying"), Kinsect->GetState(), EKinsectState::Flying);
	if (Send)
	{
		Send->FinishNormallyForTest();
	}

	// ── Recall（LT+B）：Flying → Returning ──
	FWeaponInputSnapshot RecallInput = M3::MakePosedInput(true, false);
	TestTrue(TEXT("recall activates while deployed"),
		H.TryActivateWithInput(RecallHandle, RecallInput));
	TestEqual(TEXT("kinsect keeps flying before RecallCommit"),
		Kinsect->GetState(), EKinsectState::Flying);
	UMHGZM4TestRecallKinsectAbility* Recall =
		GetActiveInstance<UMHGZM4TestRecallKinsectAbility>(*H.ASC, RecallHandle);
	TestNotNull(TEXT("active recall instance exists"), Recall);
	if (Recall)
	{
		TestTrue(TEXT("exact RecallCommit starts return"), Recall->CommitForTest());
	}
	TestEqual(TEXT("kinsect returning after recall"),
		Kinsect->GetState(), EKinsectState::Returning);
	if (Recall)
	{
		Recall->FinishNormallyForTest();
	}

	// ── Mark（LT+RT）：合法 Aim 启动虫印弹，非法 Aim 拒绝 ──
	TestEqual(TEXT("no mark projectile before launch"),
		CountMarkProjectiles(H.World), 0);
	FWeaponInputSnapshot MarkInput = M3::MakePosedInput(true, false);
	MarkInput.Aim.Context = EWeaponAimSnapshotContext::Kinsect;
	MarkInput.Aim.Direction = FVector(1.f, 0.f, 0.f);
	MarkInput.Aim.Origin = FVector(50.f, 0.f, 0.f);
	TestTrue(TEXT("mark ability activates"), H.TryActivateWithInput(MarkHandle, MarkInput));
	TestEqual(TEXT("mark projectile spawned"), CountMarkProjectiles(H.World), 1);
	int32 DestroyedMarkProjectiles = 0;
	for (TActorIterator<AIGMarkProjectile> It(H.World); It; ++It)
	{
		if (It->IsActorBeingDestroyed())
		{
			++DestroyedMarkProjectiles;
		}
	}
	TestEqual(TEXT("launched mark projectile not destroyed"),
		DestroyedMarkProjectiles, 0);

	FWeaponInputSnapshot BadMark = M3::MakePosedInput(true, false);
	BadMark.Aim.Context = EWeaponAimSnapshotContext::Kinsect;
	BadMark.Aim.Direction = FVector(std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f);
	H.TryActivateWithInput(MarkHandle, BadMark);
	TestEqual(TEXT("NaN mark direction rejected"), CountMarkProjectiles(H.World), 1);

	// 姿态 Ledger 未被 LTY/Recall 污染（始终保持 Unsheathed）。
	TestFalse(TEXT("host still unsheathed"), H.Host->IsSheathed());
	TestEqual(TEXT("unsheathed tag count is 1"), H.ASC->GetTagCount(Unsheathed), 1);
	TestEqual(TEXT("sheathed tag count is 0"), H.ASC->GetTagCount(Sheathed), 0);

	H.Teardown();
	return true;
}

// ── 7b. RT 收刀送虫：ActorForward 快照 + 拔刀切换 ───────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM3DrawSendSnapshotConstruction,
	"MHGZ.M3.Abilities.RTDrawSendActorForwardSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM3DrawSendSnapshotConstruction::RunTest(const FString& Parameters)
{
	FMHGZM3Harness H;
	if (!H.Setup())
	{
		return false;
	}

	const FGameplayAbilitySpecHandle DrawHandle =
		H.GiveAbility(UMHGZM4TestDrawAndSendAbility::StaticClass());
	TestTrue(TEXT("draw-send ability granted"), DrawHandle.IsValid());

	AKinsect* Kinsect = H.Kinsect;
	TestTrue(TEXT("host starts sheathed"), H.Host->IsSheathed());
	const FGameplayTag Sheathed = M3::Tag(TEXT("Combat.State.Sheathed"));
	const FGameplayTag Unsheathed = M3::Tag(TEXT("Combat.State.Unsheathed"));

	// ── 非法 ActorForward 快照：拒绝且收刀状态不变 ──
	FWeaponInputSnapshot NaNActorForward = M3::MakePosedInput(true, true);
	NaNActorForward.ActorForward =
		FVector(std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f);
	H.TryActivateWithInput(DrawHandle, NaNActorForward);
	TestEqual(TEXT("NaN actor forward rejected"),
		Kinsect->GetState(), EKinsectState::Attached);
	TestTrue(TEXT("host stays sheathed after NaN forward"), H.Host->IsSheathed());

	FWeaponInputSnapshot ZeroActorForward = M3::MakePosedInput(true, true);
	ZeroActorForward.ActorForward = FVector::ZeroVector;
	H.TryActivateWithInput(DrawHandle, ZeroActorForward);
	TestEqual(TEXT("zero actor forward rejected"),
		Kinsect->GetState(), EKinsectState::Attached);
	TestTrue(TEXT("host stays sheathed after zero forward"), H.Host->IsSheathed());
	TestEqual(TEXT("no kinsect active tag after rejections"),
		H.ASC->GetTagCount(SnapshotKinsectActiveTag()), 0);

	// ── 合法 RT：ActorForward 归一化快照 + StraightFlightDistance + 拔刀 ──
	FWeaponInputSnapshot DrawInput = M3::MakePosedInput(true, true);
	DrawInput.ActorForward = FVector(0.f, 500.f, 0.f); // 未归一化 → 构造时归一
	TestTrue(TEXT("valid RT draw-send activates"),
		H.TryActivateWithInput(DrawHandle, DrawInput));
	const FGameplayTag Attacking = M3::Tag(TEXT("Combat.State.Attacking"));
	const FGameplayTag BlockMovement = M3::Tag(TEXT("Combat.State.BlockMovement"));
	TestEqual(TEXT("draw-send acquires no attacking tag"),
		H.ASC->GetTagCount(Attacking), 0);
	TestEqual(TEXT("draw-send acquires no movement lock"),
		H.ASC->GetTagCount(BlockMovement), 0);
	TestFalse(TEXT("draw-send never owns montage root motion"),
		H.Host->IsMontageRootMotionOwned());
	TestEqual(TEXT("kinsect stays attached before DrawCommit"),
		Kinsect->GetState(), EKinsectState::Attached);
	TestTrue(TEXT("host stays sheathed before DrawCommit"), H.Host->IsSheathed());
	UMHGZM4TestDrawAndSendAbility* Draw =
		GetActiveInstance<UMHGZM4TestDrawAndSendAbility>(*H.ASC, DrawHandle);
	TestNotNull(TEXT("active draw-send instance exists"), Draw);
	if (Draw)
	{
		FWeaponActionToken WrongAction = Draw->GetActionToken();
		++WrongAction.ActivationSequenceID;
		TestFalse(TEXT("wrong action token cannot commit draw-send"),
			Draw->CommitDraw(WrongAction));
		TestFalse(TEXT("SendCommit before DrawCommit is rejected"),
			Draw->CommitSendForTest());
		TestTrue(TEXT("exact DrawCommit applies the draw pose"),
			Draw->CommitDrawForTest());
	}
	TestEqual(TEXT("kinsect remains attached after DrawCommit"),
		Kinsect->GetState(), EKinsectState::Attached);
	if (Draw)
	{
		TestTrue(TEXT("exact SendCommit deploys after draw"),
			Draw->CommitSendForTest());
	}
	TestEqual(TEXT("kinsect flying after draw-send"),
		Kinsect->GetState(), EKinsectState::Flying);
	TestEqual(TEXT("draw-send uses along-direction"),
		Kinsect->ActiveRequest.TrajectoryMode, EKinsectTrajectoryMode::AlongDirection);
	TestTrue(TEXT("actor forward frozen normalized"),
		Kinsect->ActiveRequest.DirectionSnapshot.Equals(FVector(0.f, 1.f, 0.f)));
	TestEqual(TEXT("draw-send max distance is straight-flight distance"),
		Kinsect->ActiveRequest.MaxDistance, H.KinsectData->StraightFlightDistance);
	TestEqual(TEXT("draw-send motion value from config"),
		Kinsect->ActiveRequest.MotionValue, H.CombatConfig->DrawSendKinsectMotionValue);
	TestEqual(TEXT("draw-send acquires kinsect active tag"),
		H.ASC->GetTagCount(SnapshotKinsectActiveTag()), 1);

	// 拔刀：Sheathed → Unsheathed Ledger 翻转。
	TestFalse(TEXT("host unsheathed after draw-send"), H.Host->IsSheathed());
	TestEqual(TEXT("sheathed tag removed"), H.ASC->GetTagCount(Sheathed), 0);
	TestEqual(TEXT("unsheathed tag acquired"), H.ASC->GetTagCount(Unsheathed), 1);
	if (Draw)
	{
		Draw->InterruptForTest();
	}
	TestFalse(TEXT("post-commit interruption preserves unsheathed pose"),
		H.Host->IsSheathed());

	H.Teardown();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
