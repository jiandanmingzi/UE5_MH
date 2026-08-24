// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "GameplayEffect.h"
#include "MHGZM3TestHarness.h"

namespace
{
const FGameplayTag& WhiteTag()
{
	static const FGameplayTag Tag = M3::Tag(TEXT("WeaponResource.IG.Extract.White"));
	return Tag;
}

const FGameplayTag& OrangeTag()
{
	static const FGameplayTag Tag = M3::Tag(TEXT("WeaponResource.IG.Extract.Orange"));
	return Tag;
}

const FGameplayTag& RedTag()
{
	static const FGameplayTag Tag = M3::Tag(TEXT("WeaponResource.IG.Extract.Red"));
	return Tag;
}

const FGameplayTag& TripleUpTag()
{
	static const FGameplayTag Tag = M3::Tag(TEXT("WeaponResource.IG.TripleUp"));
	return Tag;
}

const FGameplayTag& TestTripleCostTag()
{
	static const FGameplayTag Tag = M3::Tag(TEXT("Cost.IG.TripleUp"));
	return Tag;
}

/** 统计 ASC 上指定 GE 类的活跃实例数（按 Def 精确匹配）。 */
int32 M3CountActiveEffectsOfClass(
	UAbilitySystemComponent* ASC, TSubclassOf<UGameplayEffect> EffectClass)
{
	int32 Count = 0;
	if (!ASC || !EffectClass)
	{
		return 0;
	}
	const UGameplayEffect* Default = EffectClass->GetDefaultObject<UGameplayEffect>();
	for (const FActiveGameplayEffectHandle& Handle : ASC->GetActiveEffects(FGameplayEffectQuery()))
	{
		if (const FActiveGameplayEffect* Active = ASC->GetActiveGameplayEffect(Handle))
		{
			if (Active->Spec.Def == Default)
			{
				++Count;
			}
		}
	}
	return Count;
}

/** 指定 GE 类首个活跃实例的剩余时间；不存在返回 -1。 */
float M3RemainingTimeOfEffect(
	UAbilitySystemComponent* ASC, TSubclassOf<UGameplayEffect> EffectClass, float WorldTime)
{
	if (!ASC || !EffectClass)
	{
		return -1.f;
	}
	const UGameplayEffect* Default = EffectClass->GetDefaultObject<UGameplayEffect>();
	for (const FActiveGameplayEffectHandle& Handle : ASC->GetActiveEffects(FGameplayEffectQuery()))
	{
		if (const FActiveGameplayEffect* Active = ASC->GetActiveGameplayEffect(Handle))
		{
			if (Active->Spec.Def == Default)
			{
				return Active->GetTimeRemaining(WorldTime);
			}
		}
	}
	return -1.f;
}
}

// ── 1. 三种单灯可建立（含非法标签拒绝与同色刷新） ─────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM3SingleExtractsEstablish,
	"MHGZ.M3.Resource.SingleExtractsEstablish",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM3SingleExtractsEstablish::RunTest(const FString& Parameters)
{
	FMHGZM3Harness H;
	if (!H.Setup())
	{
		return false;
	}

	URes_InsectGlaive* Resource = H.Resource;
	const float WorldTime = H.World->GetTimeSeconds();

	// 非法/非叶子标签：拒绝且不产生任何灯。
	TestFalse(TEXT("invalid tag rejected"), Resource->ApplyExtract(FGameplayTag()));
	TestFalse(TEXT("non-leaf parent tag rejected"),
		Resource->ApplyExtract(M3::Tag(TEXT("WeaponResource.IG.Extract"))));
	TestFalse(TEXT("unrelated tag rejected"),
		Resource->ApplyExtract(M3::Tag(TEXT("Input.Weapon.Y"))));
	TestFalse(TEXT("no light after rejections"), Resource->HasExtract(WhiteTag()));
	TestFalse(TEXT("no triple after rejections"), Resource->IsTripleUpActive());

	// 白灯。
	TestTrue(TEXT("white extract applies"), Resource->ApplyExtract(WhiteTag()));
	TestTrue(TEXT("white light active"), Resource->HasExtract(WhiteTag()));
	TestTrue(TEXT("white GE tag on ASC"), H.ASC->HasMatchingGameplayTag(WhiteTag()));
	TestEqual(TEXT("exactly one white GE active"),
		M3CountActiveEffectsOfClass(H.ASC, UMHGZM3WhiteExtractGE::StaticClass()), 1);
	TestEqual(TEXT("white remaining time matches config"),
		M3RemainingTimeOfEffect(H.ASC, UMHGZM3WhiteExtractGE::StaticClass(), WorldTime),
		H.CombatConfig->WhiteExtractDuration);

	// 橙灯。
	TestTrue(TEXT("orange extract applies"), Resource->ApplyExtract(OrangeTag()));
	TestTrue(TEXT("white and orange lights active"),
		Resource->HasExtract(WhiteTag()) && Resource->HasExtract(OrangeTag()));
	TestTrue(TEXT("orange GE tag on ASC"), H.ASC->HasMatchingGameplayTag(OrangeTag()));
	TestEqual(TEXT("orange remaining time matches config"),
		M3RemainingTimeOfEffect(H.ASC, UMHGZM3OrangeExtractGE::StaticClass(), WorldTime),
		H.CombatConfig->OrangeExtractDuration);
	// 两盏灯尚未凑齐三色，不得误触发三灯；红灯建立见 Triple 测试。
	TestFalse(TEXT("two lights do not activate triple"), Resource->IsTripleUpActive());

	// 同色重 Apply：旧 Handle 移除，仍只有一盏且时间回到满值（刷新）。
	TestTrue(TEXT("re-applying white succeeds"), Resource->ApplyExtract(WhiteTag()));
	TestEqual(TEXT("re-apply keeps exactly one white GE"),
		M3CountActiveEffectsOfClass(H.ASC, UMHGZM3WhiteExtractGE::StaticClass()), 1);
	TestEqual(TEXT("re-applied white remaining time refreshes"),
		M3RemainingTimeOfEffect(H.ASC, UMHGZM3WhiteExtractGE::StaticClass(), WorldTime),
		H.CombatConfig->WhiteExtractDuration);
	TestTrue(TEXT("white still active after refresh"), Resource->HasExtract(WhiteTag()));

	H.Teardown();
	return true;
}

// ── 2. Triple GE Apply 失败保留三单灯 ──────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM3TripleApplyFailureKeepsSingles,
	"MHGZ.M3.Resource.TripleApplyFailureKeepsSingles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM3TripleApplyFailureKeepsSingles::RunTest(const FString& Parameters)
{
	// 变体 A：三灯 GE 存在，但由不可满足的应用标签要求拒绝。
	{
		FMHGZM3Harness H;
		if (!H.Setup(/*bBlockTripleUpGE=*/true))
		{
			return false;
		}

		URes_InsectGlaive* Resource = H.Resource;
		TestTrue(TEXT("blocked variant: white applies"), Resource->ApplyExtract(WhiteTag()));
		TestTrue(TEXT("blocked variant: orange applies"), Resource->ApplyExtract(OrangeTag()));
		TestTrue(TEXT("blocked variant: red applies"), Resource->ApplyExtract(RedTag()));

		TestFalse(TEXT("blocked variant: triple never activates"),
			Resource->IsTripleUpActive());
		TestTrue(TEXT("blocked variant: all three singles survive"),
			Resource->HasExtract(WhiteTag())
			&& Resource->HasExtract(OrangeTag())
			&& Resource->HasExtract(RedTag()));
		TestTrue(TEXT("blocked variant: white tag still on ASC"),
			H.ASC->HasMatchingGameplayTag(WhiteTag()));
		TestTrue(TEXT("blocked variant: orange tag still on ASC"),
			H.ASC->HasMatchingGameplayTag(OrangeTag()));
		TestTrue(TEXT("blocked variant: red tag still on ASC"),
			H.ASC->HasMatchingGameplayTag(RedTag()));
		TestEqual(TEXT("blocked variant: no triple GE"),
			M3CountActiveEffectsOfClass(H.ASC, UMHGZM3BlockedTripleUpGE::StaticClass()), 0);
		TestFalse(TEXT("blocked variant: no triple tag on ASC"),
			H.ASC->HasMatchingGameplayTag(TripleUpTag()));
		H.Teardown();
	}

	// 变体 B：三灯 GE 完全未配置（null 类）——同样保留三单灯。
	{
		FMHGZM3Harness H;
		if (!H.Setup())
		{
			return false;
		}
		H.CombatConfig->TripleUpEffectClass = nullptr;
		URes_InsectGlaive* Resource = H.Resource;
		TestTrue(TEXT("null variant: white applies"), Resource->ApplyExtract(WhiteTag()));
		TestTrue(TEXT("null variant: orange applies"), Resource->ApplyExtract(OrangeTag()));
		TestTrue(TEXT("null variant: red applies"), Resource->ApplyExtract(RedTag()));
		TestFalse(TEXT("null variant: triple never activates"),
			Resource->IsTripleUpActive());
		TestTrue(TEXT("null variant: all three singles survive"),
			Resource->HasExtract(WhiteTag())
			&& Resource->HasExtract(OrangeTag())
			&& Resource->HasExtract(RedTag()));
		H.Teardown();
	}

	return true;
}

// ── 3. Triple 成功后单灯被清、后续 Apply 被吞、Triple 不刷新 ────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM3TripleUpActivatesAndSwallows,
	"MHGZ.M3.Resource.TripleUpActivatesSwallowsAndDoesNotRefresh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM3TripleUpActivatesAndSwallows::RunTest(const FString& Parameters)
{
	FMHGZM3Harness H;
	if (!H.Setup())
	{
		return false;
	}

	URes_InsectGlaive* Resource = H.Resource;
	const float WorldTime = H.World->GetTimeSeconds();

	TestTrue(TEXT("white applies"), Resource->ApplyExtract(WhiteTag()));
	TestTrue(TEXT("orange applies"), Resource->ApplyExtract(OrangeTag()));
	TestFalse(TEXT("two lights are not triple yet"), Resource->IsTripleUpActive());
	TestTrue(TEXT("red applies and completes triple"), Resource->ApplyExtract(RedTag()));

	TestTrue(TEXT("triple is active"), Resource->IsTripleUpActive());
	TestTrue(TEXT("triple tag on ASC"), H.ASC->HasMatchingGameplayTag(TripleUpTag()));
	TestEqual(TEXT("exactly one triple GE active"),
		M3CountActiveEffectsOfClass(H.ASC, UMHGZM3TripleUpGE::StaticClass()), 1);
	TestFalse(TEXT("white light cleared by triple"), Resource->HasExtract(WhiteTag()));
	TestFalse(TEXT("orange light cleared by triple"), Resource->HasExtract(OrangeTag()));
	TestFalse(TEXT("red light cleared by triple"), Resource->HasExtract(RedTag()));
	TestEqual(TEXT("no white GE remains"),
		M3CountActiveEffectsOfClass(H.ASC, UMHGZM3WhiteExtractGE::StaticClass()), 0);
	TestEqual(TEXT("no orange GE remains"),
		M3CountActiveEffectsOfClass(H.ASC, UMHGZM3OrangeExtractGE::StaticClass()), 0);
	TestEqual(TEXT("no red GE remains"),
		M3CountActiveEffectsOfClass(H.ASC, UMHGZM3RedExtractGE::StaticClass()), 0);
	TestFalse(TEXT("no single extract tags remain on ASC"),
		H.ASC->HasMatchingGameplayTag(WhiteTag())
		|| H.ASC->HasMatchingGameplayTag(OrangeTag())
		|| H.ASC->HasMatchingGameplayTag(RedTag()));

	// 三灯期间 Apply 被吞：不生成单灯、不刷新三灯剩余时间。
	const float TripleRemainingBefore =
		M3RemainingTimeOfEffect(H.ASC, UMHGZM3TripleUpGE::StaticClass(), WorldTime);
	TestTrue(TEXT("triple remaining time is positive"), TripleRemainingBefore > 0.f);
	TestTrue(TEXT("swallowed white apply returns true"), Resource->ApplyExtract(WhiteTag()));
	TestTrue(TEXT("swallowed orange apply returns true"), Resource->ApplyExtract(OrangeTag()));
	TestTrue(TEXT("triple still active after swallows"), Resource->IsTripleUpActive());
	TestEqual(TEXT("still exactly one triple GE"),
		M3CountActiveEffectsOfClass(H.ASC, UMHGZM3TripleUpGE::StaticClass()), 1);
	TestEqual(TEXT("no white GE spawned by swallow"),
		M3CountActiveEffectsOfClass(H.ASC, UMHGZM3WhiteExtractGE::StaticClass()), 0);
	TestFalse(TEXT("no white light after swallow"), Resource->HasExtract(WhiteTag()));
	TestEqual(TEXT("triple remaining time not refreshed by swallow"),
		M3RemainingTimeOfEffect(H.ASC, UMHGZM3TripleUpGE::StaticClass(), WorldTime),
		TripleRemainingBefore);

	// 三灯期间部分消费拒绝。
	TestFalse(TEXT("single extract consume refused during triple"),
		Resource->ConsumeExtract(WhiteTag()));
	TestTrue(TEXT("triple survives refused single consume"), Resource->IsTripleUpActive());

	H.Teardown();
	return true;
}

// ── 4. 原子消费后 Triple 无效；资源预留（Cost.IG.TripleUp）语义 ─────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM3TripleAtomicConsume,
	"MHGZ.M3.Resource.TripleAtomicConsumeAndReservations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM3TripleAtomicConsume::RunTest(const FString& Parameters)
{
	FMHGZM3Harness H;
	if (!H.Setup())
	{
		return false;
	}

	URes_InsectGlaive* Resource = H.Resource;
	TestTrue(TEXT("white applies"), Resource->ApplyExtract(WhiteTag()));
	TestTrue(TEXT("orange applies"), Resource->ApplyExtract(OrangeTag()));
	TestTrue(TEXT("red applies"), Resource->ApplyExtract(RedTag()));
	TestTrue(TEXT("triple active"), Resource->IsTripleUpActive());

	// ── 预留语义 ──
	FWeaponResourceCostSpec TripleSpec;
	TripleSpec.CostType = TestTripleCostTag();
	TripleSpec.Amount.Value = 1.f;
	TArray<FWeaponResourceCostSpec> TripleSpecs = { TripleSpec };

	TestTrue(TEXT("empty specs always reservable"), Resource->CanReserveCosts({}));
	TestTrue(TEXT("triple cost reservable while active"),
		Resource->CanReserveCosts(TripleSpecs));

	FWeaponResourceCostSpec WrongAmount = TripleSpec;
	WrongAmount.Amount.Value = 0.5f;
	TestFalse(TEXT("wrong triple amount rejected"),
		Resource->CanReserveCosts({ WrongAmount }));
	FWeaponResourceCostSpec WrongTag;
	WrongTag.CostType = M3::Tag(TEXT("Input.Weapon.Y"));
	WrongTag.Amount.Value = 1.f;
	TestFalse(TEXT("non-triple cost rejected while active"),
		Resource->CanReserveCosts({ WrongTag }));

	FWeaponActionToken Token = H.MakeActionToken(UMHGZSendKinsectAbility::StaticClass());
	TestTrue(TEXT("action token valid"), Token.IsValid());
	FWeaponResourceCostReservation Reservation;
	TestTrue(TEXT("triple reserve succeeds"),
		Resource->TryReserveCosts(Token, TripleSpecs, Reservation));
	TestTrue(TEXT("reservation is valid"), Reservation.IsValid());
	TestFalse(TEXT("invalid action token rejected"),
		Resource->TryReserveCosts(FWeaponActionToken(), TripleSpecs, Reservation));
	TestFalse(TEXT("wrong amount reservation rejected"),
		Resource->TryReserveCosts(Token, { WrongAmount }, Reservation));

	Resource->ReleaseReservation(Reservation);
	TestTrue(TEXT("released reservation keeps triple active"),
		Resource->IsTripleUpActive());

	FWeaponResourceCostReservation ConsumedReservation;
	TestTrue(TEXT("second reserve succeeds"),
		Resource->TryReserveCosts(Token, TripleSpecs, ConsumedReservation));
	Resource->ConsumeReservedCosts(ConsumedReservation);
	TestFalse(TEXT("consumed reservation clears triple"), Resource->IsTripleUpActive());
	TestFalse(TEXT("triple tag gone after reserved consume"),
		H.ASC->HasMatchingGameplayTag(TripleUpTag()));
	TestFalse(TEXT("triple cost no longer reservable"),
		Resource->CanReserveCosts(TripleSpecs));

	// ── 原子消费 ──
	TestTrue(TEXT("white applies again"), Resource->ApplyExtract(WhiteTag()));
	TestTrue(TEXT("orange applies again"), Resource->ApplyExtract(OrangeTag()));
	TestTrue(TEXT("red applies again"), Resource->ApplyExtract(RedTag()));
	TestTrue(TEXT("triple rebuilt"), Resource->IsTripleUpActive());
	TestTrue(TEXT("atomic consume succeeds"), Resource->TryConsumeTripleUpAtomic());
	TestFalse(TEXT("triple invalid after atomic consume"),
		Resource->IsTripleUpActive());
	TestFalse(TEXT("triple tag gone after atomic consume"),
		H.ASC->HasMatchingGameplayTag(TripleUpTag()));
	TestFalse(TEXT("no single lights resurrected by consume"),
		Resource->HasExtract(WhiteTag())
		|| Resource->HasExtract(OrangeTag())
		|| Resource->HasExtract(RedTag()));
	TestEqual(TEXT("no extract GEs remain after atomic consume"),
		M3CountActiveEffectsOfClass(H.ASC, UMHGZM3WhiteExtractGE::StaticClass())
		+ M3CountActiveEffectsOfClass(H.ASC, UMHGZM3OrangeExtractGE::StaticClass())
		+ M3CountActiveEffectsOfClass(H.ASC, UMHGZM3RedExtractGE::StaticClass())
		+ M3CountActiveEffectsOfClass(H.ASC, UMHGZM3TripleUpGE::StaticClass()), 0);
	TestFalse(TEXT("second atomic consume fails"), Resource->TryConsumeTripleUpAtomic());

	H.Teardown();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
