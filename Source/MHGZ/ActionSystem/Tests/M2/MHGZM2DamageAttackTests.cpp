// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "ActionSystem/MHGZDamageGameplayEffect.h"
#include "ActionSystem/MHGZGameplayEffectContext.h"
#include "ActionSystem/MHGZHitFeedbackRouterComponent.h"
#include "ActionSystem/MHGZHitFeedbackTypes.h"
#include "ActionSystem/MHGZHitStopControllerComponent.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "Camera/CameraShakeBase.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"

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

UAbilitySystemComponent* CreateTestASC(AActor* Owner, UMHGZAttributeSet*& OutAttributeSet)
{
	UAbilitySystemComponent* ASC = NewObject<UAbilitySystemComponent>(Owner);
	Owner->AddInstanceComponent(ASC);
	ASC->RegisterComponent();

	OutAttributeSet = NewObject<UMHGZAttributeSet>(Owner);
	ASC->AddAttributeSetSubobject(OutAttributeSet);
	ASC->InitAbilityActorInfo(Owner, Owner);
	return ASC;
}
}

// ── 1. Context 深拷贝：反馈字段 + 真实 HitResult ──
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM2ContextDuplicateFeedbackFields,
	"MHGZ.M2.Damage.ContextDuplicateCarriesFeedbackFields",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM2ContextDuplicateFeedbackFields::RunTest(const FString& Parameters)
{
	UWorld* World = CreateM2TestWorld();
	if (!World)
	{
		return false;
	}

	AActor* OwnerActor = World->SpawnActor<AActor>();
	UAbilitySystemComponent* ASC = NewObject<UAbilitySystemComponent>(OwnerActor);
	OwnerActor->AddInstanceComponent(ASC);
	ASC->RegisterComponent();
	ASC->InitAbilityActorInfo(OwnerActor, OwnerActor);

	FGameplayEffectContextHandle Handle = ASC->MakeEffectContext();
	FMHGZGameplayEffectContext* Context =
		FMHGZGameplayEffectContext::ExtractEffectContext(Handle);
	if (!TestNotNull(TEXT("MakeEffectContext allocates FMHGZGameplayEffectContext"), Context))
	{
		DestroyM2TestWorld(World);
		return false;
	}

	Context->AttackInstanceID = FGuid::NewGuid();
	Context->SourceActionTag = FGameplayTag::RequestGameplayTag(TEXT("Input.Weapon.Y"));
	Context->HitzoneTag = FGameplayTag::RequestGameplayTag(TEXT("Hitzone.Head"));
	Context->bUseHitzoneDefense = false;
	Context->DamageSourceType = EMHGZDamageSourceType::Weapon;
	Context->HitCueTag = FGameplayTag::RequestGameplayTag(TEXT("GameplayCue.Hit.Slash"));
	Context->ElementCueTag = FGameplayTag::RequestGameplayTag(TEXT("GameplayCue.Hit.Fire"));
	Context->HitStaggerTag = FGameplayTag::RequestGameplayTag(TEXT("Combat.Stagger.Light"));
	Context->HitStopDuration = 0.12f;
	Context->CameraShakeClass = UCameraShakeBase::StaticClass();
	Context->CameraShakeScale = 0.5f;

	FHitResult Hit;
	Hit.TraceStart = FVector(1.f, 2.f, 3.f);
	Hit.ImpactPoint = FVector(10.f, 20.f, 30.f);
	Hit.ImpactNormal = FVector(0.f, 0.f, 1.f);
	Context->AddHitResult(Hit, true);

	const FGuid ExpectedID = Context->AttackInstanceID;
	FGameplayEffectContextHandle DuplicateHandle = Handle.Duplicate();
	const FMHGZGameplayEffectContext* Duplicate =
		FMHGZGameplayEffectContext::ExtractEffectContext(DuplicateHandle);
	if (!TestNotNull(TEXT("Duplicate preserves the project context type"), Duplicate))
	{
		DestroyM2TestWorld(World);
		return false;
	}

	TestEqual(TEXT("AttackInstanceID survives Duplicate"), Duplicate->AttackInstanceID, ExpectedID);
	TestEqual(TEXT("SourceActionTag survives Duplicate"), Duplicate->SourceActionTag, Context->SourceActionTag);
	TestEqual(TEXT("HitzoneTag survives Duplicate"), Duplicate->HitzoneTag, Context->HitzoneTag);
	TestFalse(TEXT("hitzone-defense switch survives Duplicate"), Duplicate->bUseHitzoneDefense);
	TestEqual(TEXT("HitCueTag survives Duplicate"), Duplicate->HitCueTag, Context->HitCueTag);
	TestEqual(TEXT("ElementCueTag survives Duplicate"), Duplicate->ElementCueTag, Context->ElementCueTag);
	TestEqual(TEXT("HitStaggerTag survives Duplicate"), Duplicate->HitStaggerTag, Context->HitStaggerTag);
	TestEqual(TEXT("HitStopDuration survives Duplicate"), Duplicate->HitStopDuration, 0.12f);
	TestEqual(TEXT("CameraShakeScale survives Duplicate"), Duplicate->CameraShakeScale, 0.5f);
	TestEqual(TEXT("CameraShakeClass survives Duplicate"),
		Duplicate->CameraShakeClass, Context->CameraShakeClass);
	TestNotNull(TEXT("HitResult survives Duplicate"), Duplicate->GetHitResult());
	if (Duplicate->GetHitResult())
	{
		TestEqual(TEXT("HitResult values survive Duplicate"),
			Duplicate->GetHitResult()->ImpactPoint, Hit.ImpactPoint);
	}

	Context->GetHitResult()->ImpactPoint = FVector::ZeroVector;
	TestEqual(TEXT("Duplicate owns a deep HitResult copy"),
		Duplicate->GetHitResult()->ImpactPoint, Hit.ImpactPoint);

	DestroyM2TestWorld(World);
	return true;
}

// ── 2. 完整伤害链：MotionValue=0 零扣血 + Meta 原子清空 + 单次反馈 ──
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM2DamagePipelineMotionValueZero,
	"MHGZ.M2.Damage.MotionValueZeroDealsNoDamageAndMetasClear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM2DamagePipelineMotionValueZero::RunTest(const FString& Parameters)
{
	// GAS 全局数据（AttributeSet 默认值/GameplayCue 管理）必须先初始化。
	UAbilitySystemGlobals& Globals = UAbilitySystemGlobals::Get();
	if (!Globals.IsAbilitySystemGlobalsInitialized())
	{
		Globals.InitGlobalData();
	}

	UWorld* World = CreateM2TestWorld();
	if (!World)
	{
		return false;
	}

	AActor* SourceActor = World->SpawnActor<AActor>();
	UMHGZAttributeSet* SourceAttributes = nullptr;
	UAbilitySystemComponent* SourceASC = CreateTestASC(SourceActor, SourceAttributes);
	SourceAttributes->InitAttackPower(100.f);
	SourceAttributes->InitCriticalRate(0.f);
	SourceAttributes->InitStaggerMultiplier(1.f);

	AActor* TargetActor = World->SpawnActor<AActor>();
	UMHGZAttributeSet* TargetAttributes = nullptr;
	UAbilitySystemComponent* TargetASC = CreateTestASC(TargetActor, TargetAttributes);
	TargetAttributes->InitMaxHealth(1000.f);
	TargetAttributes->InitHealth(1000.f);

	UMHGZMonsterHitzoneComponent* Hitzone =
		NewObject<UMHGZMonsterHitzoneComponent>(TargetActor);
	Hitzone->BoneName = TEXT("head");
	Hitzone->HitzoneTag = FGameplayTag::RequestGameplayTag(TEXT("Hitzone.Head"));
	Hitzone->DefenseMultiplier = 0.5f;
	Hitzone->StaggerRate = 1.0f;
	TargetActor->AddInstanceComponent(Hitzone);
	TargetActor->SetRootComponent(Hitzone);
	Hitzone->RegisterComponent();

	UMHGZHitFeedbackRouterComponent* Router =
		NewObject<UMHGZHitFeedbackRouterComponent>(TargetActor);
	TargetActor->AddInstanceComponent(Router);
	Router->RegisterComponent();

	auto ApplyHit = [&](float MotionValue, bool bUseHitzoneDefense = true) -> bool
	{
		FGameplayEffectContextHandle ContextHandle = SourceASC->MakeEffectContext();
		FMHGZGameplayEffectContext* Context =
			FMHGZGameplayEffectContext::ExtractEffectContext(ContextHandle);
		if (!Context)
		{
			return false;
		}

		FHitResult Hit(TargetActor, Hitzone, FVector(100.f, 0.f, 0.f), FVector::UpVector);
		Hit.bBlockingHit = true;
		Hit.Location = FVector(100.f, 0.f, 0.f);
		Hit.ImpactPoint = FVector(100.f, 0.f, 0.f);
		Hit.ImpactNormal = FVector(0.f, 0.f, 1.f);
		Context->AddHitResult(Hit, true);
		Context->AttackInstanceID = FGuid::NewGuid();
		Context->SourceActionTag = FGameplayTag::RequestGameplayTag(TEXT("Input.Weapon.Y"));
		Context->HitzoneTag = Hitzone->HitzoneTag;
		Context->bUseHitzoneDefense = bUseHitzoneDefense;
		Context->HitCueTag = FGameplayTag::RequestGameplayTag(TEXT("GameplayCue.Hit.Slash"));
		Context->HitStaggerTag = FGameplayTag::RequestGameplayTag(TEXT("Combat.Stagger.Light"));
		Context->DamageSourceType = EMHGZDamageSourceType::Weapon;
		Context->AddInstigator(SourceActor, SourceActor);

		FGameplayEffectSpecHandle Spec = SourceASC->MakeOutgoingSpec(
			UMHGZDamageGameplayEffect::StaticClass(), 1.f, ContextHandle);
		if (!Spec.IsValid())
		{
			return false;
		}
		Spec.Data->SetSetByCallerMagnitude(
			FGameplayTag::RequestGameplayTag(TEXT("Damage.MotionValue")), MotionValue);
		Spec.Data->SetSetByCallerMagnitude(
			FGameplayTag::RequestGameplayTag(TEXT("Damage.BaseStagger")), 10.f);
		SourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data, TargetASC);
		return true;
	};

	// 第一次：MotionValue=0 → 零扣血，但真实命中仍产生一次反馈。
	const int32 InitialCueCount = Router->GetExecutedCueCount();
	if (!TestTrue(TEXT("zero-MotionValue hit applies"), ApplyHit(0.f)))
	{
		DestroyM2TestWorld(World);
		return false;
	}
	TestEqual(TEXT("zero MotionValue keeps full health"), TargetAttributes->GetHealth(), 1000.f);
	TestEqual(TEXT("zero MotionValue produces one feedback pass"), Router->GetExecutedCueCount(), InitialCueCount + 2);
	TestEqual(TEXT("damage number carries zero damage"),
		Router->GetLastDamageNumberMagnitude(), 0.f);

	// Meta 原子清空：一次结算后四个 Meta 全部回到 0。
	TestEqual(TEXT("IncomingDamage cleared"), TargetAttributes->GetIncomingDamage(), 0.f);
	TestEqual(TEXT("IncomingStagger cleared"), TargetAttributes->GetIncomingStagger(), 0.f);
	TestEqual(TEXT("IncomingCriticalFlag cleared"), TargetAttributes->GetIncomingCriticalFlag(), 0.f);
	TestEqual(TEXT("IncomingHitSignal cleared"), TargetAttributes->GetIncomingHitSignal(), 0.f);

	// 第二次：正动作值且使用肉质 → 50 伤害（100 × 1 × 0.5）。
	if (!TestTrue(TEXT("positive-MotionValue hit applies"), ApplyHit(1.0f)))
	{
		DestroyM2TestWorld(World);
		return false;
	}
	TestEqual(TEXT("positive MotionValue uses hitzone defense"), TargetAttributes->GetHealth(), 950.f);
	TestEqual(TEXT("second hit produces exactly one more feedback pass"),
		Router->GetExecutedCueCount(), InitialCueCount + 4);
	TestEqual(TEXT("damage number carries actual damage"),
		Router->GetLastDamageNumberMagnitude(), 50.f);
	TestEqual(TEXT("IncomingDamage cleared after second hit"), TargetAttributes->GetIncomingDamage(), 0.f);
	TestEqual(TEXT("IncomingStagger cleared after second hit"), TargetAttributes->GetIncomingStagger(), 0.f);
	TestEqual(TEXT("IncomingCriticalFlag cleared after second hit"), TargetAttributes->GetIncomingCriticalFlag(), 0.f);
	TestEqual(TEXT("IncomingHitSignal cleared after second hit"), TargetAttributes->GetIncomingHitSignal(), 0.f);

	// 第三次：显式关闭肉质修正 → 同一部位按 1.0 结算 100 伤害。
	if (!TestTrue(TEXT("hitzone-defense-disabled hit applies"), ApplyHit(1.0f, false)))
	{
		DestroyM2TestWorld(World);
		return false;
	}
	TestEqual(TEXT("disabled hitzone defense bypasses multiplier"),
		TargetAttributes->GetHealth(), 850.f);
	TestEqual(TEXT("third hit produces exactly one more feedback pass"),
		Router->GetExecutedCueCount(), InitialCueCount + 6);
	TestEqual(TEXT("bypassed damage number carries 100"),
		Router->GetLastDamageNumberMagnitude(), 100.f);

	DestroyM2TestWorld(World);
	return true;
}

// ── 3. HitStop 重叠 Token：任一请求到期/释放不能提前恢复 ──
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM2HitStopOverlappingTokens,
	"MHGZ.M2.HitStop.OverlappingTokensDoNotEarlyRestore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM2HitStopOverlappingTokens::RunTest(const FString& Parameters)
{
	UWorld* World = CreateM2TestWorld();
	if (!World)
	{
		return false;
	}

	AActor* Owner = World->SpawnActor<AActor>();
	Owner->CustomTimeDilation = 0.75f; // 非 1.0 的原值，验证“保存原值”语义

	UMHGZHitStopControllerComponent* Controller =
		NewObject<UMHGZHitStopControllerComponent>(Owner);
	Owner->AddInstanceComponent(Controller);
	Controller->RegisterComponent();

	TestFalse(TEXT("initially inactive"), Controller->IsHitStopActive());
	TestEqual(TEXT("no requests initially"), Controller->GetActiveRequestCount(), 0);

	TestEqual(TEXT("non-positive duration is rejected"),
		Controller->RequestHitStop(0.f), int64(0));

	const int64 TokenA = Controller->RequestHitStop(0.5f);
	const int64 TokenB = Controller->RequestHitStop(0.2f);
	TestTrue(TEXT("first request gets a unique token"), TokenA != 0);
	TestTrue(TEXT("second request gets a unique token"), TokenB != 0 && TokenB != TokenA);
	TestTrue(TEXT("hit stop active with two requests"), Controller->IsHitStopActive());
	TestEqual(TEXT("two active requests"), Controller->GetActiveRequestCount(), 2);
	TestEqual(TEXT("dilation applied"), Controller->GetCurrentTimeDilation(), 0.05f);

	// 释放较早的请求：另一个请求仍存活 → 不得恢复。
	Controller->ReleaseHitStop(TokenA);
	TestEqual(TEXT("one request remains"), Controller->GetActiveRequestCount(), 1);
	TestTrue(TEXT("still active while one request remains"), Controller->IsHitStopActive());
	TestEqual(TEXT("dilation not restored early"), Controller->GetCurrentTimeDilation(), 0.05f);

	// 释放最后一个请求 → 恢复首次请求前保存的原值。
	Controller->ReleaseHitStop(TokenB);
	TestEqual(TEXT("no requests remain"), Controller->GetActiveRequestCount(), 0);
	TestFalse(TEXT("inactive after last release"), Controller->IsHitStopActive());
	TestEqual(TEXT("original dilation restored"), Controller->GetCurrentTimeDilation(), 0.75f);

	// 未知 Token / 重复释放幂等；ClearAll 幂等。
	Controller->ReleaseHitStop(TokenA);
	Controller->ReleaseHitStop(12345);
	TestEqual(TEXT("unknown releases are no-ops"), Controller->GetActiveRequestCount(), 0);
	Controller->ClearAll();
	Controller->ClearAll();
	TestEqual(TEXT("ClearAll is idempotent"), Controller->GetActiveRequestCount(), 0);
	TestFalse(TEXT("ClearAll keeps inactive"), Controller->IsHitStopActive());

	// 活动状态下 ClearAll 立即恢复原值。
	(void)Controller->RequestHitStop(0.5f);
	TestEqual(TEXT("re-armed after clear"), Controller->GetActiveRequestCount(), 1);
	Controller->ClearAll();
	TestEqual(TEXT("ClearAll restores original dilation"), Controller->GetCurrentTimeDilation(), 0.75f);

	DestroyM2TestWorld(World);
	return true;
}

#endif
