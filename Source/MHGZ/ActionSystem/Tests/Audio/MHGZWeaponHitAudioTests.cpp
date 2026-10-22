// Copyright MHGZ Project. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "ActionSystem/MHGZDamageGameplayEffect.h"
#include "ActionSystem/MHGZGameplayEffectContext.h"
#include "ActionSystem/MHGZHitFeedbackRouterComponent.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "GameplayCueManager.h"
#include "GameplayCueSet.h"
#include "GameplayCues/MHGZWeaponHitSoundCue.h"
#include "Monster/MHGZTrainingDummy.h"

namespace
{
struct FWeaponHitAudioFixture
{
	UWorld* World = nullptr;
	AActor* Source = nullptr;
	AMHGZTrainingDummy* Target = nullptr;
	UBoxComponent* Part = nullptr;
	UAbilitySystemComponent* SourceASC = nullptr;
	UMHGZHitFeedbackRouterComponent* Router = nullptr;

	bool Setup()
	{
		auto& Globals = UAbilitySystemGlobals::Get();
		if (!Globals.IsAbilitySystemGlobalsInitialized()) { Globals.InitGlobalData(); }
		World = UWorld::CreateWorld(EWorldType::Game, false);
		if (!World) { return false; }
		Source = World->SpawnActor<AActor>();
		Target = World->SpawnActor<AMHGZTrainingDummy>();
		if (!Source || !Target) { return false; }
		Part = NewObject<UBoxComponent>(Target);
		Target->AddInstanceComponent(Part); Part->RegisterComponent();
		SourceASC = NewObject<UAbilitySystemComponent>(Source);
		Source->AddInstanceComponent(SourceASC); SourceASC->RegisterComponent();
		auto* Attributes = NewObject<UMHGZAttributeSet>(Source);
		SourceASC->AddAttributeSetSubobject(Attributes);
		SourceASC->InitAbilityActorInfo(Source, Source);
		Attributes->InitAttackPower(100.f);
		Target->GetAbilitySystemComponent()->InitAbilityActorInfo(Target, Target);
		Target->bInfiniteHealth = true;
		auto* TargetAttributes = Target->GetAttributeSet();
		TargetAttributes->InitMaxHealth(1000.f); TargetAttributes->InitHealth(1000.f);
		Router = Target->GetHitFeedbackRouter();
		return Router != nullptr;
	}

	FGameplayEffectContextHandle Contact(EMHGZDamageSourceType Type = EMHGZDamageSourceType::Weapon)
	{
		auto Handle = SourceASC->MakeEffectContext();
		auto* Context = FMHGZGameplayEffectContext::ExtractEffectContext(Handle);
		if (Context)
		{
			FHitResult Hit(Target, Part, FVector::ZeroVector, FVector::UpVector);
			Context->AddHitResult(Hit, true);
			Context->AddInstigator(Source, Source);
			Context->DamageSourceType = Type;
			Context->AttackInstanceID = FGuid::NewGuid();
			Context->bUseHitzoneDefense = false;
		}
		return Handle;
	}

	~FWeaponHitAudioFixture() { if (World) { World->DestroyWorld(false); } }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZWeaponHitAudioSelection,
	"MHGZ.Maintenance.Audio.Hit.ContactSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZWeaponHitAudioSelection::RunTest(const FString& Parameters)
{
	FWeaponHitAudioFixture F;
	if (!TestTrue(TEXT("fixture ready"), F.Setup())) { return false; }
	FVector Location(123.f);
	const auto Context = F.Contact();
	TestTrue(TEXT("true contact at world origin is valid"),
		UMHGZWeaponHitSoundCue::ResolveWeaponHitLocation(F.Target, Context, Location));
	TestTrue(TEXT("origin not replaced with actor pivot"), Location.IsZero());
	TestTrue(TEXT("repeated hit in same action is not deduplicated"),
		UMHGZWeaponHitSoundCue::ResolveWeaponHitLocation(F.Target, Context, Location));
	TestFalse(TEXT("wrong target is rejected"),
		UMHGZWeaponHitSoundCue::ResolveWeaponHitLocation(F.Source, Context, Location));
	TestFalse(TEXT("no context / air swing is rejected"),
		UMHGZWeaponHitSoundCue::ResolveWeaponHitLocation(F.Target, {}, Location));
	TestFalse(TEXT("base context is not a weapon contact"),
		UMHGZWeaponHitSoundCue::ResolveWeaponHitLocation(F.Target,
			FGameplayEffectContextHandle(new FGameplayEffectContext()), Location));
	TestFalse(TEXT("weapon context without contact is rejected"),
		UMHGZWeaponHitSoundCue::ResolveWeaponHitLocation(F.Target, F.SourceASC->MakeEffectContext(), Location));
	for (const auto Type : {EMHGZDamageSourceType::Kinsect, EMHGZDamageSourceType::Powder,
		EMHGZDamageSourceType::DummyAttack})
	{
		TestFalse(TEXT("non-weapon source is excluded"),
			UMHGZWeaponHitSoundCue::ResolveWeaponHitLocation(F.Target, F.Contact(Type), Location));
	}
	FGameplayCueParameters CueParams;
	CueParams.EffectContext = Context;
	TestFalse(TEXT("unconfigured sound is silent"),
		GetDefault<UMHGZWeaponHitSoundCue>()->OnExecute_Implementation(F.Target, CueParams));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZWeaponHitAudioRegistration,
	"MHGZ.Maintenance.Audio.Hit.ResidentRegistrationAndDispatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZWeaponHitAudioRegistration::RunTest(const FString& Parameters)
{
	FWeaponHitAudioFixture F;
	if (!TestTrue(TEXT("fixture ready"), F.Setup())) { return false; }
	const auto Tag = FGameplayTag::RequestGameplayTag(TEXT("GameplayCue.Hit.WeaponSound"));
	const FSoftObjectPath Expected(TEXT("/Game/GameplayCues/GCN_Hit_WeaponSound.GCN_Hit_WeaponSound_C"));
	UClass* CueClass = LoadClass<UMHGZWeaponHitSoundCue>(nullptr, *Expected.ToString());
	if (!TestNotNull(TEXT("resident native-parent Cue loads"), CueClass)) { return false; }
	auto* Manager = UAbilitySystemGlobals::Get().GetGameplayCueManager();
	Manager->InitializeRuntimeObjectLibrary();
	auto* Set = Manager->GetRuntimeCueSet();
	int32 Matches = 0;
	for (const auto& Data : Set->GameplayCueData)
	{
		if (Data.GameplayCueTag == Tag)
		{
			++Matches;
			TestEqual(TEXT("runtime registry references resident Cue"), Data.GameplayCueNotifyObj, Expected);
		}
	}
	TestEqual(TEXT("exactly one runtime handler"), Matches, 1);
	FGameplayCueParameters CueParams;
	CueParams.EffectContext = F.Contact();
	TestTrue(TEXT("runtime CueSet resolves and handles Executed event"),
		Set->HandleGameplayCue(F.Target, Tag, EGameplayCueEvent::Executed, CueParams));
	// -nosound tests registration/dispatch only, not speaker audibility.
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZWeaponHitAudioRoute,
	"MHGZ.Maintenance.Audio.Hit.ResolvedRouteOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZWeaponHitAudioRoute::RunTest(const FString& Parameters)
{
	FWeaponHitAudioFixture F;
	if (!TestTrue(TEXT("fixture ready"), F.Setup())) { return false; }
	FMHGZHitFeedbackResult Result;
	Result.ContextHandle = F.Contact();
	Result.Hit = *Result.ContextHandle.GetHitResult();
	Result.HitCueTag = FGameplayTag::RequestGameplayTag(TEXT("GameplayCue.Hit.Slash"));
	Result.ElementCueTag = FGameplayTag::RequestGameplayTag(TEXT("GameplayCue.Hit.Fire"));
	Result.bCritical = true;
	F.Router->RouteHitFeedback(Result);
	TestEqual(TEXT("one sound + physical + element + crit, no damage-number at zero damage"),
		F.Router->GetExecutedCueCount(), 4);
	F.Router->RouteHitFeedback(Result);
	TestEqual(TEXT("second contact is retained, not action-wide deduped"), F.Router->GetExecutedCueCount(), 8);
	Result = FMHGZHitFeedbackResult();
	Result.ContextHandle = F.Contact(EMHGZDamageSourceType::Kinsect);
	F.Router->RouteHitFeedback(Result);
	TestEqual(TEXT("kinsect never receives generic weapon sound"), F.Router->GetExecutedCueCount(), 8);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZWeaponHitAudioInfiniteHealth,
	"MHGZ.Maintenance.Audio.Hit.RealDamageInfiniteHealth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZWeaponHitAudioInfiniteHealth::RunTest(const FString& Parameters)
{
	FWeaponHitAudioFixture F;
	if (!TestTrue(TEXT("fixture ready"), F.Setup())) { return false; }
	const auto Contact = F.Contact();
	auto Apply = [&](float MotionValue)
	{
		auto Spec = F.SourceASC->MakeOutgoingSpec(UMHGZDamageGameplayEffect::StaticClass(), 1.f, Contact);
		if (!Spec.IsValid()) { return false; }
		Spec.Data->SetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag(TEXT("Damage.MotionValue")), MotionValue);
		Spec.Data->SetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag(TEXT("Damage.BaseStagger")), 0.f);
		F.SourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data, F.Target->GetAbilitySystemComponent());
		return true;
	};
	TestTrue(TEXT("real zero-damage GE applies"), Apply(0.f));
	TestEqual(TEXT("zero damage still routes one contact sound"), F.Router->GetExecutedCueCount(), 1);
	TestTrue(TEXT("real positive-damage GE applies"), Apply(10.f));
	TestEqual(TEXT("infinite dummy does not lose health"), F.Target->GetAttributeSet()->GetHealth(), 1000.f);
	TestEqual(TEXT("resolved damage remains ten"), F.Router->GetLastDamageNumberMagnitude(), 10.f);
	TestEqual(TEXT("one new contact sound and one number, without physical VFX tag"),
		F.Router->GetExecutedCueCount(), 3);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZKinsectHitAudioSelection,
	"MHGZ.Maintenance.Audio.KinsectHit.ContactSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZKinsectHitAudioSelection::RunTest(const FString& Parameters)
{
	FWeaponHitAudioFixture F;
	if (!TestTrue(TEXT("fixture ready"), F.Setup())) { return false; }
	FVector Location;
	const auto Contact = F.Contact(EMHGZDamageSourceType::Kinsect);
	TestTrue(TEXT("kinsect true impact at world origin is accepted"),
		UMHGZWeaponHitSoundCue::ResolveContactLocation(F.Target, Contact, EMHGZDamageSourceType::Kinsect, Location));
	TestTrue(TEXT("true impact is not replaced with target pivot"), Location.IsZero());
	TestTrue(TEXT("a second legitimate piercing contact is not suppressed"),
		UMHGZWeaponHitSoundCue::ResolveContactLocation(F.Target, Contact, EMHGZDamageSourceType::Kinsect, Location));
	TestFalse(TEXT("weapon cannot use kinsect sound"),
		UMHGZWeaponHitSoundCue::ResolveContactLocation(F.Target, F.Contact(), EMHGZDamageSourceType::Kinsect, Location));
	TestFalse(TEXT("kinsect cannot use weapon sound"),
		UMHGZWeaponHitSoundCue::ResolveWeaponHitLocation(F.Target, Contact, Location));
	TestFalse(TEXT("wrong target is rejected"),
		UMHGZWeaponHitSoundCue::ResolveContactLocation(F.Source, Contact, EMHGZDamageSourceType::Kinsect, Location));
	TestFalse(TEXT("air swing is silent"),
		UMHGZWeaponHitSoundCue::ResolveContactLocation(F.Target, {}, EMHGZDamageSourceType::Kinsect, Location));
	for (const auto Type : {EMHGZDamageSourceType::Powder, EMHGZDamageSourceType::DummyAttack})
	{
		TestFalse(TEXT("unsupported sources cannot opt in"),
			UMHGZWeaponHitSoundCue::ResolveContactLocation(F.Target, F.Contact(Type), Type, Location));
	}
	const auto Tag = FGameplayTag::RequestGameplayTag(TEXT("GameplayCue.Hit.Kinsect"));
	const FSoftObjectPath Expected(TEXT("/Game/GameplayCues/GCN_Hit_Kinsect.GCN_Hit_Kinsect_C"));
	if (!TestNotNull(TEXT("resident kinsect Cue loads"), LoadClass<UMHGZWeaponHitSoundCue>(nullptr, *Expected.ToString())))
	{
		return false;
	}
	auto* Manager = UAbilitySystemGlobals::Get().GetGameplayCueManager();
	Manager->InitializeRuntimeObjectLibrary();
	auto* Set = Manager->GetRuntimeCueSet();
	int32 Matches = 0;
	for (const auto& Data : Set->GameplayCueData)
	{
		if (Data.GameplayCueTag == Tag)
		{
			++Matches;
			TestEqual(TEXT("runtime registry selects the resident leaf"), Data.GameplayCueNotifyObj, Expected);
		}
	}
	TestEqual(TEXT("exactly one kinsect handler"), Matches, 1);
	FGameplayCueParameters CueParams;
	CueParams.EffectContext = Contact;
	TestTrue(TEXT("runtime registry handles Executed contact"),
		Set->HandleGameplayCue(F.Target, Tag, EGameplayCueEvent::Executed, CueParams));
	FMHGZHitFeedbackResult Result;
	Result.ContextHandle = Contact;
	Result.Hit = *Contact.GetHitResult();
	Result.HitCueTag = Tag;
	F.Router->RouteHitFeedback(Result);
	TestEqual(TEXT("existing physical hit route dispatches exactly once, no weapon layer"),
		F.Router->GetExecutedCueCount(), 1);
	F.Router->RouteHitFeedback(Result);
	TestEqual(TEXT("distinct repeated hit dispatch is retained"), F.Router->GetExecutedCueCount(), 2);
	return true;
}
#endif
