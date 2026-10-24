// Copyright MHGZ Project. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "ActionSystem/MHGZDamageGameplayEffect.h"
#include "ActionSystem/MHGZGameplayEffectContext.h"
#include "ActionSystem/MHGZHitFeedbackRouterComponent.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"
#include "Monster/MHGZTrainingDummy.h"
#include "Monster/MHGZDummyConfig.h"
#include "Engine/World.h"
#include "Equipment/MHGZEquipmentComponent.h"
#include "Equipment/MHGZEquipmentDefinition.h"
#include "Equipment/MHGZEquipmentInstance.h"
#include "../M5/KinsectSlash/MHGZM5KinsectSlashDamageTestTypes.h"

namespace
{
struct FDamageFormulaFixture
{
	UWorld* World = nullptr;
	AActor* Source = nullptr;
	AActor* Target = nullptr;
	UAbilitySystemComponent* SourceASC = nullptr;
	UAbilitySystemComponent* TargetASC = nullptr;
	UMHGZAttributeSet* SourceAttributes = nullptr;
	UMHGZAttributeSet* TargetAttributes = nullptr;
	UMHGZMonsterHitzoneComponent* Hitzone = nullptr;
	UMHGZHitFeedbackRouterComponent* Router = nullptr;
	FMHGZWeaponDamageProfile Profile;

	bool Setup()
	{
		UAbilitySystemGlobals& Globals = UAbilitySystemGlobals::Get();
		if (!Globals.IsAbilitySystemGlobalsInitialized()) { Globals.InitGlobalData(); }
		World = UWorld::CreateWorld(EWorldType::Game, false);
		if (!World) { return false; }
		Source = World->SpawnActor<AActor>();
		Target = World->SpawnActor<AActor>();
		if (!Source || !Target) { return false; }
		auto AddASC = [](AActor* Owner, UMHGZAttributeSet*& Attributes)
		{
			auto* ASC = NewObject<UAbilitySystemComponent>(Owner);
			Owner->AddInstanceComponent(ASC); ASC->RegisterComponent();
			Attributes = NewObject<UMHGZAttributeSet>(Owner);
			ASC->AddAttributeSetSubobject(Attributes); ASC->InitAbilityActorInfo(Owner, Owner);
			return ASC;
		};
		SourceASC = AddASC(Source, SourceAttributes);
		TargetASC = AddASC(Target, TargetAttributes);
		SourceAttributes->InitAttackPower(200.0f);
		SourceAttributes->InitElementPower(20.0f);
		TargetAttributes->InitMaxHealth(10000.0f); TargetAttributes->InitHealth(10000.0f);
		Hitzone = NewObject<UMHGZMonsterHitzoneComponent>(Target);
		Target->AddInstanceComponent(Hitzone); Target->SetRootComponent(Hitzone);
		Hitzone->RegisterComponent(); Hitzone->DefenseMultiplier = 45.0f;
		Hitzone->ElementHitzoneMultipliers.Add(EMHGZElementType::Fire, 0.20f);
		Router = NewObject<UMHGZHitFeedbackRouterComponent>(Target);
		Target->AddInstanceComponent(Router); Router->RegisterComponent();
		Profile.ElementType = EMHGZElementType::Fire;
		Profile.PhysicalSharpnessMultiplier = 1.32f;
		Profile.ElementSharpnessMultiplier = 1.15f;
		return true;
	}

	FGameplayEffectSpecHandle MakeSpec(float Motion = 20.0f, float ElementMotion = 2.0f,
		EMHGZDamageSourceType SourceType = EMHGZDamageSourceType::Weapon,
		EMHGZPhysicalDamageType PhysicalType = EMHGZPhysicalDamageType::Sever,
		bool bUseHitzone = true)
	{
		auto ContextHandle = SourceASC->MakeEffectContext();
		auto* Context = FMHGZGameplayEffectContext::ExtractEffectContext(ContextHandle);
		if (!Context) { return {}; }
		FHitResult Hit(Target, Hitzone->GetQueryCollisionComponent(), FVector::ZeroVector, FVector::UpVector);
		Hit.bBlockingHit = true;
		Context->AddHitResult(Hit, true); Context->AddInstigator(Source, Source);
		Context->AttackInstanceID = FGuid::NewGuid(); Context->DamageSourceType = SourceType;
		Context->PhysicalDamageType = PhysicalType; Context->WeaponDamageProfile = Profile;
		Context->bUseHitzoneDefense = bUseHitzone;
		auto Spec = SourceASC->MakeOutgoingSpec(UMHGZDamageGameplayEffect::StaticClass(), 1.0f, ContextHandle);
		if (Spec.IsValid())
		{
			Spec.Data->SetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag(TEXT("Damage.MotionValue")), Motion);
			Spec.Data->SetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag(TEXT("Damage.ElementMotionValue")), ElementMotion);
			Spec.Data->SetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag(TEXT("Damage.BaseStagger")), 0.0f);
		}
		return Spec;
	}
	float Apply(const FGameplayEffectSpecHandle& Spec)
	{
		if (!Spec.IsValid()) { return -1.0f; }
		const float Before = TargetAttributes->GetHealth();
		SourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data, TargetASC);
		return Before - TargetAttributes->GetHealth();
	}
	~FDamageFormulaFixture() { if (World) { World->DestroyWorld(false); } }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZDamageDualChannel,
	"MHGZ.M2.Damage.Formula.PhysicalAndElementIndependent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZDamageDualChannel::RunTest(const FString& Parameters)
{
	FDamageFormulaFixture F;
	if (!TestTrue(TEXT("fixture builds"), F.Setup())) { return false; }
	// 200*(20/100)*1.32*(45/100)=23.76 ->24; 20*2*1.15*.2=9.2 ->9.
	TestEqual(TEXT("real GE settles independently rounded physical + element"), F.Apply(F.MakeSpec()), 33.0f);
	TestEqual(TEXT("changing physical MV does not scale elemental channel"), F.Apply(F.MakeSpec(40.0f)), 57.0f);
	TestEqual(TEXT("element motion zero suppresses elemental damage"), F.Apply(F.MakeSpec(20.0f, 0.0f)), 24.0f);
	F.Profile.ElementType = EMHGZElementType::Water;
	TestEqual(TEXT("unconfigured elemental hitzone does not fall back to physical"), F.Apply(F.MakeSpec()), 24.0f);
	F.Profile.ElementType = EMHGZElementType::None;
	TestEqual(TEXT("None cannot accidentally use an elemental hitzone entry"), F.Apply(F.MakeSpec()), 24.0f);
	TestEqual(TEXT("IncomingDamage clears after combined hit"), F.TargetAttributes->GetIncomingDamage(), 0.0f);
	TestEqual(TEXT("IncomingHitSignal clears after combined hit"), F.TargetAttributes->GetIncomingHitSignal(), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZDamageAffinity,
	"MHGZ.M2.Damage.Formula.AffinityAndDanceDoNotLeakToElement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZDamageAffinity::RunTest(const FString& Parameters)
{
	FDamageFormulaFixture F;
	if (!TestTrue(TEXT("fixture builds"), F.Setup())) { return false; }
	F.SourceAttributes->InitCriticalRate(100.0f);
	TestEqual(TEXT("normal critical raises physical only"), F.Apply(F.MakeSpec()), 39.0f);
	F.Profile.CriticalElementMultiplier = 1.35f;
	TestEqual(TEXT("explicit critical element adds its independent multiplier"), F.Apply(F.MakeSpec()), 42.0f);
	F.SourceASC->SetNumericAttributeBase(UMHGZAttributeSet::GetCriticalRateAttribute(), -100.0f);
	TestEqual(TEXT("negative affinity applies 0.75 physical but no negative elemental crit"), F.Apply(F.MakeSpec()), 27.0f);
	F.SourceASC->SetNumericAttributeBase(UMHGZAttributeSet::GetCriticalRateAttribute(), 0.0f);
	auto Spec = F.MakeSpec();
	Spec.Data->SetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag(TEXT("Damage.DanceMultiplier")), 2.0f);
	TestEqual(TEXT("existing dance scales physical but not attribute"), F.Apply(Spec), 57.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZDamageTypedHitzone,
	"MHGZ.M2.Damage.Formula.TypedHitzonesZeroAndRounding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZDamageTypedHitzone::RunTest(const FString& Parameters)
{
	FDamageFormulaFixture F;
	if (!TestTrue(TEXT("fixture builds"), F.Setup())) { return false; }
	F.Profile.PhysicalSharpnessMultiplier = F.Profile.ElementSharpnessMultiplier = 1.0f;
	F.SourceAttributes->InitAttackPower(10.0f); F.SourceAttributes->InitElementPower(10.0f);
	F.Hitzone->DefenseMultiplier = 100.0f;
	F.Hitzone->ElementHitzoneMultipliers[EMHGZElementType::Fire] = 0.15f;
	TestEqual(TEXT("round 1.5 physical + round 1.5 element =4, not round sum=3"), F.Apply(F.MakeSpec(15.0f, 1.0f)), 4.0f);
	F.Hitzone->PhysicalHitzoneMultipliers.Add(EMHGZPhysicalDamageType::Blunt, 0.0f);
	TestEqual(TEXT("explicit zero typed physical remains zero; element still applies"),
		F.Apply(F.MakeSpec(15.0f, 1.0f, EMHGZDamageSourceType::Weapon, EMHGZPhysicalDamageType::Blunt)), 2.0f);
	F.Hitzone->ElementHitzoneMultipliers[EMHGZElementType::Fire] = 0.0f;
	TestEqual(TEXT("zero both hitzones must not manufacture minimum damage"),
		F.Apply(F.MakeSpec(15.0f, 1.0f, EMHGZDamageSourceType::Weapon, EMHGZPhysicalDamageType::Blunt)), 0.0f);
	TestEqual(TEXT("bypass physical does not bypass elemental immunity"),
		F.Apply(F.MakeSpec(15.0f, 1.0f, EMHGZDamageSourceType::Weapon, EMHGZPhysicalDamageType::Blunt, false)), 2.0f);
	F.Hitzone->ElementHitzoneMultipliers[EMHGZElementType::Fire] = 1.0f;
	TestEqual(TEXT("zero motion still means explicitly non-damaging contact"), F.Apply(F.MakeSpec(0.0f)), 0.0f);
	TestEqual(TEXT("negative motion still means non-damaging contact"), F.Apply(F.MakeSpec(-1.0f)), 0.0f);
	F.Profile.PhysicalSharpnessMultiplier = -1.0f;
	TestEqual(TEXT("invalid weapon profile fails closed rather than fabricating damage"), F.Apply(F.MakeSpec()), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZDamageOtherSources,
	"MHGZ.M2.Damage.Formula.KinsectPowderAndDummyRemainCompatible",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZDamageOtherSources::RunTest(const FString& Parameters)
{
	FDamageFormulaFixture F;
	if (!TestTrue(TEXT("fixture builds"), F.Setup())) { return false; }
	F.Profile.PhysicalSharpnessMultiplier = 9.0f;
	F.Profile.ElementSharpnessMultiplier = 9.0f;
	F.Hitzone->DefenseMultiplier = 50.0f;
	F.Hitzone->PhysicalHitzoneMultipliers.Add(EMHGZPhysicalDamageType::Sever, 0.0f);
	auto KinsectSpec = F.MakeSpec(20.0f, 9.0f, EMHGZDamageSourceType::Kinsect);
	KinsectSpec.Data->SetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag(TEXT("Damage.AttackPower")), 47.0f);
	TestEqual(TEXT("kinsect uses AP×MV/100×physical hitzone/100 and floor; no inherited element"), F.Apply(KinsectSpec), 4.0f);
	auto PowderSpec = F.MakeSpec(100.0f, 9.0f, EMHGZDamageSourceType::Powder,
		EMHGZPhysicalDamageType::Sever, false);
	PowderSpec.Data->SetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag(TEXT("Damage.AttackPower")), 17.0f);
	PowderSpec.Data->SetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag(TEXT("Damage.CritOverride")), 0.0f);
	TestEqual(TEXT("fixed powder receives neither gear scaling nor elemental damage"), F.Apply(PowderSpec), 17.0f);
	TestEqual(TEXT("dummy does not receive weapon sharpness/typed hitzone/attribute"),
		F.Apply(F.MakeSpec(0.20f, 9.0f, EMHGZDamageSourceType::DummyAttack)), 20.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZDamageSpecSnapshot,
	"MHGZ.M2.Damage.Formula.SpecCapturesAttributesAndProductionProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZDamageSpecSnapshot::RunTest(const FString& Parameters)
{
	FDamageFormulaFixture F;
	if (!TestTrue(TEXT("fixture builds"), F.Setup())) { return false; }
	auto Spec = F.MakeSpec();
	F.SourceASC->SetNumericAttributeBase(UMHGZAttributeSet::GetAttackPowerAttribute(), 1000.0f);
	F.SourceASC->SetNumericAttributeBase(UMHGZAttributeSet::GetElementPowerAttribute(), 1000.0f);
	TestEqual(TEXT("both source stats frozen at Spec creation, not reread on apply"), F.Apply(Spec), 33.0f);

	FMHGZM5KinsectSlashDamageTestHarness H;
	if (!TestTrue(TEXT("equipped production attack harness builds"), H.Build())) { H.Teardown(); return false; }
	const FGameplayTag WeaponSlot = FGameplayTag::RequestGameplayTag(TEXT("Equipment.Slot.Weapon"));
	auto* Item = H.Equipment->GetEquippedItem(WeaponSlot);
	auto* Weapon = Item ? Cast<UMHGZWeaponDefinition>(Item->Definition) : nullptr;
	if (!TestNotNull(TEXT("equipped weapon definition is available"), Weapon)) { H.Teardown(); return false; }
	Weapon->ElementPower = 20.0f;
	H.Equipment->EquipItem(WeaponSlot, Item);
	TestEqual(TEXT("actual equipped weapon writes base element into source ASC"), H.SourceAttributes->GetElementPower(), 20.0f);
	if (!TestTrue(TEXT("production attack activates"), H.ActivateAbility())) { H.Teardown(); return false; }
	H.CombatConfig->DamageProfile = F.Profile;
	H.Ability->AttackSegments[1].Damage.PhysicalDamageType = EMHGZPhysicalDamageType::Blunt;
	H.Ability->AttackSegments[1].Damage.ElementMotionValue = 2.0f;
	H.Ability->AttackSegments[1].Damage.PhysicalDamageMultiplier = 0.5f;
	H.Ability->AttackSegments[1].Damage.ElementDamageMultiplier = 3.0f;
	FHitResult Hit(H.TargetActor, H.Hitzone, FVector::ZeroVector, FVector::UpVector);
	auto ProductionSpec = static_cast<UMHGZAttackAbility*>(H.Ability)->MakeDamageSpec(Hit, 1);
	if (TestTrue(TEXT("production MakeDamageSpec succeeds"), ProductionSpec.IsValid()))
	{
		const auto* Context = FMHGZGameplayEffectContext::ExtractEffectContext(ProductionSpec.Data->GetContext());
		if (TestNotNull(TEXT("production context exists"), Context))
		{
			TestEqual(TEXT("segment physical type forwarded"), Context->PhysicalDamageType, EMHGZPhysicalDamageType::Blunt);
			TestEqual(TEXT("equipped element forwarded"), Context->WeaponDamageProfile.ElementType, EMHGZElementType::Fire);
			TestEqual(TEXT("equipped sharpness snapshotted"), Context->WeaponDamageProfile.PhysicalSharpnessMultiplier, 1.32f);
		}
		H.CombatConfig->DamageProfile.PhysicalSharpnessMultiplier = 9.0f;
		TestEqual(TEXT("element motion multiplier forwarded"), ProductionSpec.Data->GetSetByCallerMagnitude(
			FGameplayTag::RequestGameplayTag(TEXT("Damage.ElementMotionValue")), false), 2.0f);
		TestEqual(TEXT("physical correction forwarded"), ProductionSpec.Data->GetSetByCallerMagnitude(
			FGameplayTag::RequestGameplayTag(TEXT("Damage.PhysicalMultiplier")), false), 0.5f);
		TestEqual(TEXT("element correction forwarded"), ProductionSpec.Data->GetSetByCallerMagnitude(
			FGameplayTag::RequestGameplayTag(TEXT("Damage.ElementMultiplier")), false), 3.0f);
		if (Context)
		{
			TestEqual(TEXT("profile mutation cannot change a prepared damage Spec"), Context->WeaponDamageProfile.PhysicalSharpnessMultiplier, 1.32f);
		}
	}
	Weapon->ElementPower = 0.0f;
	H.Equipment->EquipItem(WeaponSlot, Item);
	TestEqual(TEXT("switching base stats back to no element cannot retain old attribute value"), H.SourceAttributes->GetElementPower(), 0.0f);
	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZDamageGeneratedHitzone,
	"MHGZ.M2.Damage.Formula.ConfigGeneratesBoxFleshAndSingleTotalFeedback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZDamageGeneratedHitzone::RunTest(const FString& Parameters)
{
	FDamageFormulaFixture F;
	if (!TestTrue(TEXT("fixture builds"), F.Setup())) { return false; }
	auto* ResidentCombat = LoadObject<UInsectGlaiveCombatConfig>(nullptr,
		TEXT("/Game/Weapons/InsectGlaive/Data/DA_IG_Combat.DA_IG_Combat"));
	if (TestNotNull(TEXT("existing resident combat config still loads"), ResidentCombat))
	{
		TestTrue(TEXT("resident profile is compatible and valid"), ResidentCombat->DamageProfile.IsValid());
	}
	auto* Config = NewObject<UMHGZDummyConfig>();
	FDummyHitzoneConfig Part;
	Part.Shape = EMHGZHitzoneShape::Box;
	Part.HitzoneTag = FGameplayTag::RequestGameplayTag(TEXT("Hitzone.Head"));
	Part.ExtractColorTag = FGameplayTag::RequestGameplayTag(TEXT("WeaponResource.IG.Extract.Red"));
	Part.DefenseMultiplier = 1.0f;
	Part.PhysicalHitzoneMultipliers.Add(EMHGZPhysicalDamageType::Blunt, 80.0f);
	Part.ElementHitzoneMultipliers.Add(EMHGZElementType::Fire, 0.20f);
	Config->Hitzones.Add(Part);
	auto* Dummy = F.World->SpawnActor<AMHGZTrainingDummy>();
	if (!TestNotNull(TEXT("dummy spawns"), Dummy)) { return false; }
	Dummy->bInfiniteHealth = false;
	Dummy->GenerateHitzonesFromConfig(Config);
	TArray<UMHGZMonsterHitzoneComponent*> Parts;
	Dummy->GetComponents(Parts);
	if (!TestEqual(TEXT("one configured part produces one metadata component"), Parts.Num(), 1)) { return false; }
	F.Target = Dummy; F.Hitzone = Parts[0];
	F.TargetASC = Dummy->GetAbilitySystemComponent();
	F.TargetASC->InitAbilityActorInfo(Dummy, Dummy);
	F.TargetAttributes = Dummy->GetAttributeSet();
	F.TargetAttributes->InitMaxHealth(1000.0f); F.TargetAttributes->InitHealth(1000.0f);
	F.Router = Dummy->GetHitFeedbackRouter();
	TestTrue(TEXT("real hit primitive is a Box child rather than disabled sphere metadata"),
		F.Hitzone->GetQueryCollisionComponent() != F.Hitzone);
	TestTrue(TEXT("Box primitive resolves the configured metadata"),
		UMHGZMonsterHitzoneComponent::Resolve(F.Hitzone->GetQueryCollisionComponent()) == F.Hitzone);
	const auto Spec = F.MakeSpec(20.0f, 2.0f, EMHGZDamageSourceType::Weapon, EMHGZPhysicalDamageType::Blunt);
	// 200*(20/100)*1.32*(80/100)=42.24 ->42; 20*2*1.15*.2=9.2 ->9.
	TestEqual(TEXT("generated typed flesh tables drive actual GE through Box hit"), F.Apply(Spec), 51.0f);
	TestEqual(TEXT("combined hit routes one final total to number feedback"), F.Router->GetLastDamageNumberMagnitude(), 51.0f);
	TestEqual(TEXT("combined channels produce one contact sound and one damage-number cue"), F.Router->GetExecutedCueCount(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZKinsectDamageIntegerBoundary,
	"MHGZ.Maintenance.Damage.KinsectPowder.IntegerPercentBoundaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZKinsectDamageIntegerBoundary::RunTest(const FString&)
{
	FDamageFormulaFixture F;
	if (!TestTrue(TEXT("fixture builds"), F.Setup())) return false;
	for (const auto SourceType : {EMHGZDamageSourceType::Kinsect, EMHGZDamageSourceType::Powder})
	for (const int32 Power : {25, 50, 100, 200, 1000})
	for (const int32 Motion : {5, 10, 15, 20, 25, 30, 40})
	for (const int32 Hitzone : {20, 25, 50, 75, 100})
	{
		F.Hitzone->DefenseMultiplier = float(Hitzone);
		F.TargetAttributes->InitHealth(10000.f);
		auto Spec = F.MakeSpec(float(Motion), 9.f, SourceType);
		Spec.Data->SetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag(TEXT("Damage.AttackPower")), float(Power));
		// Independent exact integer arithmetic, never recalculate via float ratio.
		const int32 Expected = FMath::Max(1, SourceType == EMHGZDamageSourceType::Kinsect
			? Power * Motion * Hitzone / 10000 : Power * Motion / 100);
		TestEqual(*FString::Printf(TEXT("actual GE type=%d AP=%d MV=%d HZ=%d"),
			int32(SourceType), Power, Motion, Hitzone), F.Apply(Spec), float(Expected));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMHGZKinsectDamageFractionFloor,
	"MHGZ.Maintenance.Damage.KinsectPowder.FractionsRemainFloored",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMHGZKinsectDamageFractionFloor::RunTest(const FString&)
{
	FDamageFormulaFixture F;
	if (!TestTrue(TEXT("fixture builds"), F.Setup())) return false;
	F.Hitzone->DefenseMultiplier = 100.f;
	F.Profile.PhysicalSharpnessMultiplier = F.Profile.ElementSharpnessMultiplier = 9.f;
	F.SourceASC->SetNumericAttributeBase(UMHGZAttributeSet::GetCriticalRateAttribute(), 100.f);
	auto Apply = [&](float Power, float Motion, EMHGZDamageSourceType Type)
	{
		F.TargetAttributes->InitHealth(10000.f);
		auto Spec = F.MakeSpec(Motion, 9.f, Type);
		Spec.Data->SetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag(TEXT("Damage.AttackPower")), Power);
		Spec.Data->SetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag(TEXT("Damage.DanceMultiplier")), 2.f);
		return F.Apply(Spec);
	};
	for (const auto Type : {EMHGZDamageSourceType::Kinsect, EMHGZDamageSourceType::Powder})
	{
		TestEqual(TEXT("AP100 MV10 is 10, independent of gear/crit/dance"), Apply(100, 10, Type), 10.f);
		TestEqual(TEXT("AP100 MV20 is 20"), Apply(100, 20, Type), 20.f);
		TestEqual(TEXT("true 9.99 remains floored"), Apply(100, 9.99f, Type), 9.f);
		TestEqual(TEXT("one float step below 10 is not promoted"), Apply(100, 9.999999046325684f, Type), 9.f);
		TestEqual(TEXT("one float step above 10 still floors to 10"), Apply(100, 10.000000953674316f, Type), 10.f);
		TestEqual(TEXT("non-integral power still floors, not rounds"), Apply(99.9f, 10, Type), 9.f);
		TestEqual(TEXT("existing positive sub-one minimum preserved"), Apply(1, 10, Type), 1.f);
		TestEqual(TEXT("zero power still yields zero"), Apply(0, 10, Type), 0.f);
		TestEqual(TEXT("zero motion still yields zero"), Apply(100, 0, Type), 0.f);
		TestEqual(TEXT("negative motion still yields zero"), Apply(100, -10, Type), 0.f);
	}
	F.Hitzone->PhysicalHitzoneMultipliers.Add(EMHGZPhysicalDamageType::Sever, 50.f);
	TestEqual(TEXT("kinsect still uses typed physical percent"), Apply(100, 10, EMHGZDamageSourceType::Kinsect), 5.f);
	TestEqual(TEXT("powder still ignores physical flesh"), Apply(100, 10, EMHGZDamageSourceType::Powder), 10.f);
	return true;
}
#endif
