// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "ActionSystem/MHGZDodgeAbility.h"
#include "ActionSystem/MHGZIncomingHitResolverComponent.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/DataValidation.h"
#include "Monster/MHGZDummyConfig.h"
#include "Monster/MHGZTrainingDummy.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"
#include "WeaponRuntime/MHGZWeaponRuntimeTypes.h"

namespace
{
UWorld* CreateM2IncomingTestWorld()
{
	return UWorld::CreateWorld(EWorldType::Game, false);
}

void DestroyM2IncomingTestWorld(UWorld* World)
{
	if (World)
	{
		World->DestroyWorld(false);
	}
}

ACharacter* SpawnM2Character(UWorld* World)
{
	return World->SpawnActor<ACharacter>(
		ACharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
}

AMHGZTrainingDummy* SpawnM2Dummy(UWorld* World)
{
	return World->SpawnActor<AMHGZTrainingDummy>(
		AMHGZTrainingDummy::StaticClass(), FVector(0.f, 0.f, 200.f), FRotator::ZeroRotator);
}

UMHGZIncomingHitResolverComponent* AddM2Resolver(ACharacter* Character)
{
	UMHGZIncomingHitResolverComponent* Resolver =
		NewObject<UMHGZIncomingHitResolverComponent>(Character);
	Character->AddInstanceComponent(Resolver);
	Resolver->RegisterComponent();
	return Resolver;
}

UMHGZWeaponRuntimeHostComponent* AddM2Host(
	ACharacter* Character, UMHGZAbilitySystemComponent* ASC)
{
	UMHGZWeaponRuntimeHostComponent* Host =
		NewObject<UMHGZWeaponRuntimeHostComponent>(Character);
	Character->AddInstanceComponent(Host);
	Host->RegisterComponent();
	Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	Host->InitializePawnRuntime(Character, nullptr, ASC, nullptr);
	return Host;
}

FWeaponActionToken MakeM2ActionToken(
	UMHGZWeaponRuntimeHostComponent* Host,
	UMHGZAbilitySystemComponent* ASC,
	UGameplayAbility* AbilityInstance)
{
	FWeaponActionToken Token;
	Token.RuntimeToken = Host->GetCurrentToken();
	Token.AbilityHandle = ASC->GiveAbility(
		FGameplayAbilitySpec(UMHGZDodgeAbility::StaticClass(), 1, INDEX_NONE, ASC));
	Token.ActivationSequenceID = Host->AllocateActivationSequenceID();
	Token.AbilityInstance = AbilityInstance;
	return Token;
}

FIncomingHitContext MakeM2HitContext(
	AActor* Target, AActor* Source, bool bCounterable, float Damage,
	const FGuid& AttackInstanceID = FGuid::NewGuid())
{
	FIncomingHitContext Context;
	Context.AttackInstanceID = AttackInstanceID;
	Context.SourceActor = Source;
	Context.bCounterable = bCounterable;
	Context.Damage = Damage;
	UPrimitiveComponent* TargetComponent = nullptr;
	if (ACharacter* TargetCharacter = Cast<ACharacter>(Target))
	{
		TargetComponent = TargetCharacter->GetCapsuleComponent();
	}
	const FVector HitLocation = Target ? Target->GetActorLocation() : FVector::ZeroVector;
	Context.Hit = FHitResult(Target, TargetComponent, HitLocation, FVector::UpVector);
	Context.Hit.bBlockingHit = true;
	Context.Hit.bStartPenetrating = false;
	Context.Hit.Time = 0.f;
	Context.Hit.Location = HitLocation;
	Context.Hit.ImpactPoint = Context.Hit.Location;
	return Context;
}

void EnsureM2AbilitySystemGlobals()
{
	UAbilitySystemGlobals& Globals = UAbilitySystemGlobals::Get();
	if (!Globals.IsAbilitySystemGlobalsInitialized())
	{
		Globals.InitGlobalData();
	}
}
}

// 1. Pure resolver semantics: priority consume, duplicate, expiry, unregister, stale token.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM2ResolverPriorityConsumeDuplicateExpiry,
	"MHGZ.M2.Resolver.PriorityConsumeDuplicateExpiry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM2ResolverPriorityConsumeDuplicateExpiry::RunTest(const FString& Parameters)
{
	UWorld* World = CreateM2IncomingTestWorld();
	if (!World)
	{
		return false;
	}

	ACharacter* Character = SpawnM2Character(World);
	if (!Character)
	{
		DestroyM2IncomingTestWorld(World);
		return false;
	}

	// A real target ASC keeps the Pass path observable while token callbacks remain
	// the subject of this test.
	UMHGZAbilitySystemComponent* ASC = NewObject<UMHGZAbilitySystemComponent>(Character);
	Character->AddInstanceComponent(ASC);
	ASC->RegisterComponent();
	UMHGZAttributeSet* Attributes = NewObject<UMHGZAttributeSet>(Character);
	ASC->AddAttributeSetSubobject(Attributes);
	ASC->InitAbilityActorInfo(Character, Character);
	UMHGZWeaponRuntimeHostComponent* Host = AddM2Host(Character, ASC);
	UMHGZIncomingHitResolverComponent* Resolver = AddM2Resolver(Character);

	UMHGZDodgeAbility* AbilityInstance = NewObject<UMHGZDodgeAbility>(ASC);
	const FWeaponActionToken Token = MakeM2ActionToken(Host, ASC, AbilityInstance);
	TestTrue(TEXT("action token is valid"), Token.IsValid());

	int32 HighCalls = 0;
	int32 LowCalls = 0;
	int32 ExpiredCalls = 0;
	int32 UnregisteredCalls = 0;

	const int64 HighID = Resolver->RegisterInterceptorNative(Token, 100, 60.f,
		[&HighCalls](const FIncomingHitContext&) -> EIncomingHitInterceptResult
		{
			++HighCalls;
			return EIncomingHitInterceptResult::Consume;
		});
	const int64 LowID = Resolver->RegisterInterceptorNative(Token, 0, 60.f,
		[&LowCalls](const FIncomingHitContext&) -> EIncomingHitInterceptResult
		{
			++LowCalls;
			return EIncomingHitInterceptResult::Pass;
		});
	TestTrue(TEXT("high priority token registered"), HighID != 0);
	TestTrue(TEXT("low priority token registered"), LowID != 0);
	TestEqual(TEXT("two interceptors registered"), Resolver->GetActiveInterceptorCount(), 2);

	const FGuid FirstID = FGuid::NewGuid();
	FIncomingHitContext First = MakeM2HitContext(Character, Character, true, 10.f, FirstID);
	TestEqual(TEXT("counterable hit consumed by high priority"),
		Resolver->SubmitIncomingHit(First), EIncomingHitSubmitResult::Consumed);
	TestEqual(TEXT("high callback called once"), HighCalls, 1);
	TestEqual(TEXT("low callback not reached after consume"), LowCalls, 0);
	TestTrue(TEXT("consumed ID is recorded as processed"),
		Resolver->HasProcessedAttack(FirstID));

	TestEqual(TEXT("same ID resubmit returns Duplicate"),
		Resolver->SubmitIncomingHit(First), EIncomingHitSubmitResult::Duplicate);
	TestEqual(TEXT("duplicate does not re-invoke callbacks"), HighCalls, 1);
	TestEqual(TEXT("duplicate does not invoke low priority"), LowCalls, 0);

	ACharacter* OtherCharacter = SpawnM2Character(World);
	if (!TestNotNull(TEXT("secondary character spawned"), OtherCharacter))
	{
		DestroyM2IncomingTestWorld(World);
		return false;
	}
	FIncomingHitContext WrongOwnerHit = MakeM2HitContext(
		OtherCharacter, Character, false, 10.f);
	TestEqual(TEXT("resolver rejects a hit targeting another actor"),
		Resolver->SubmitIncomingHit(WrongOwnerHit), EIncomingHitSubmitResult::Rejected);
	TestFalse(TEXT("wrong-owner rejection does not enter dedupe cache"),
		Resolver->HasProcessedAttack(WrongOwnerHit.AttackInstanceID));

	UMHGZIncomingHitResolverComponent* ResolverWithoutHost = AddM2Resolver(OtherCharacter);
	TestEqual(TEXT("interceptor registration requires an owner RuntimeHost"),
		ResolverWithoutHost->RegisterInterceptorNative(Token, 10, 1.f,
			[](const FIncomingHitContext&) { return EIncomingHitInterceptResult::Pass; }),
		int64(0));

	Resolver->UnregisterAllInterceptors();
	TestEqual(TEXT("unregister all clears interceptors"),
		Resolver->GetActiveInterceptorCount(), 0);

	// TTL <= 0 expires at registration; pruned before callbacks run.
	const int64 ExpiredID = Resolver->RegisterInterceptorNative(Token, 50, 0.f,
		[&ExpiredCalls](const FIncomingHitContext&) -> EIncomingHitInterceptResult
		{
			++ExpiredCalls;
			return EIncomingHitInterceptResult::Consume;
		});
	TestTrue(TEXT("zero-TTL token accepted at registration"), ExpiredID != 0);
	FIncomingHitContext ExpiredHit = MakeM2HitContext(Character, Character, true, 10.f);
	TestEqual(TEXT("expired token pruned; hit passes to damage"),
		Resolver->SubmitIncomingHit(ExpiredHit), EIncomingHitSubmitResult::Applied);
	TestEqual(TEXT("expired callback never invoked"), ExpiredCalls, 0);
	TestTrue(TEXT("applied ID enters the dedupe record"),
		Resolver->HasProcessedAttack(ExpiredHit.AttackInstanceID));

	// Unregister is idempotent and the callback is never invoked afterwards.
	Resolver->UnregisterAllInterceptors();
	const int64 UnregID = Resolver->RegisterInterceptorNative(Token, 50, 60.f,
		[&UnregisteredCalls](const FIncomingHitContext&) -> EIncomingHitInterceptResult
		{
			++UnregisteredCalls;
			return EIncomingHitInterceptResult::Consume;
		});
	TestTrue(TEXT("unregister succeeds once"), Resolver->UnregisterInterceptor(UnregID));
	TestFalse(TEXT("second unregister is a no-op"), Resolver->UnregisterInterceptor(UnregID));
	FIncomingHitContext UnregHit = MakeM2HitContext(Character, Character, true, 10.f);
	TestEqual(TEXT("unregistered token no-op -> damage applies"),
		Resolver->SubmitIncomingHit(UnregHit), EIncomingHitSubmitResult::Applied);
	TestEqual(TEXT("unregistered callback never invoked"), UnregisteredCalls, 0);

	// A stale RuntimeToken (host generation advanced) is rejected at registration.
	Host->ShutdownRuntime(EWeaponRuntimeEndReason::RuntimeShutdown);
	const int64 StaleID = Resolver->RegisterInterceptorNative(Token, 50, 60.f,
		[](const FIncomingHitContext&) -> EIncomingHitInterceptResult
		{
			return EIncomingHitInterceptResult::Pass;
		});
	TestEqual(TEXT("stale runtime token rejected at registration"), StaleID, int64(0));
	TestEqual(TEXT("no interceptors remain"), Resolver->GetActiveInterceptorCount(), 0);

	DestroyM2IncomingTestWorld(World);
	return true;
}

// 2. Full GAS apply path: non-counterable cannot be consumed; same ID settles once.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM2ResolverGASApplyAndNonCounterable,
	"MHGZ.M2.Resolver.GASApplyAndNonCounterable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM2ResolverGASApplyAndNonCounterable::RunTest(const FString& Parameters)
{
	EnsureM2AbilitySystemGlobals();

	UWorld* World = CreateM2IncomingTestWorld();
	if (!World)
	{
		return false;
	}

	ACharacter* Character = SpawnM2Character(World);
	AMHGZTrainingDummy* Dummy = SpawnM2Dummy(World);
	if (!Character || !Dummy)
	{
		DestroyM2IncomingTestWorld(World);
		return false;
	}

	UMHGZAbilitySystemComponent* PlayerASC =
		NewObject<UMHGZAbilitySystemComponent>(Character);
	Character->AddInstanceComponent(PlayerASC);
	PlayerASC->RegisterComponent();
	UMHGZAttributeSet* PlayerAttrs = NewObject<UMHGZAttributeSet>(Character);
	PlayerASC->AddAttributeSetSubobject(PlayerAttrs);
	PlayerASC->InitAbilityActorInfo(Character, Character);

	Dummy->GetAbilitySystemComponent()->InitAbilityActorInfo(Dummy, Dummy);

	UMHGZIncomingHitResolverComponent* Resolver = AddM2Resolver(Character);
	UMHGZWeaponRuntimeHostComponent* Host = AddM2Host(Character, PlayerASC);
	UMHGZDodgeAbility* AbilityInstance = NewObject<UMHGZDodgeAbility>(PlayerASC);
	const FWeaponActionToken Token = MakeM2ActionToken(Host, PlayerASC, AbilityInstance);

	int32 Calls = 0;
	Resolver->RegisterInterceptorNative(Token, 50, 60.f,
		[&Calls](const FIncomingHitContext&) -> EIncomingHitInterceptResult
		{
			++Calls;
			return EIncomingHitInterceptResult::Consume;
		});

	// Non-counterable hit: interceptors must not run; damage applies and health drops.
	FIncomingHitContext NonCounter = MakeM2HitContext(Character, Dummy, false, 20.f);
	TestEqual(TEXT("non-counterable hit applies"),
		Resolver->SubmitIncomingHit(NonCounter), EIncomingHitSubmitResult::Applied);
	TestEqual(TEXT("consume callback never invoked for non-counterable"), Calls, 0);
	TestEqual(TEXT("health reduced by 20"), PlayerAttrs->GetHealth(), 80.f);
	TestTrue(TEXT("applied ID is recorded as processed"),
		Resolver->HasProcessedAttack(NonCounter.AttackInstanceID));

	TestEqual(TEXT("same ID after apply returns Duplicate"),
		Resolver->SubmitIncomingHit(NonCounter), EIncomingHitSubmitResult::Duplicate);
	TestEqual(TEXT("no double damage"), PlayerAttrs->GetHealth(), 80.f);

	// Counterable hit: consumed by the token; no damage is settled.
	FIncomingHitContext Counter = MakeM2HitContext(Character, Dummy, true, 20.f);
	TestEqual(TEXT("counterable hit consumed"),
		Resolver->SubmitIncomingHit(Counter), EIncomingHitSubmitResult::Consumed);
	TestEqual(TEXT("consume callback invoked once"), Calls, 1);
	TestEqual(TEXT("consumed hit deals no damage"), PlayerAttrs->GetHealth(), 80.f);

	TestEqual(TEXT("same ID after consume returns Duplicate"),
		Resolver->SubmitIncomingHit(Counter), EIncomingHitSubmitResult::Duplicate);
	TestEqual(TEXT("consumed duplicate deals no damage"), PlayerAttrs->GetHealth(), 80.f);

	DestroyM2IncomingTestWorld(World);
	return true;
}

// 3. Dummy config validation: three colors, no overlap, positive radius.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM2DummyConfigThreeColorValidation,
	"MHGZ.M2.DummyConfig.ThreeColorNonOverlapValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM2DummyConfigThreeColorValidation::RunTest(const FString& Parameters)
{
	auto MakeHitzone = [](FName Bone, const FString& TagName, const FString& ColorLeaf,
		const FVector& Location, float Radius) -> FDummyHitzoneConfig
	{
		FDummyHitzoneConfig HZ;
		HZ.BoneName = Bone;
		HZ.HitzoneTag = FGameplayTag::RequestGameplayTag(FName(*TagName));
		HZ.ExtractColorTag = FGameplayTag::RequestGameplayTag(
			FName(*FString::Printf(TEXT("WeaponResource.IG.Extract.%s"), *ColorLeaf)));
		HZ.RelativeLocation = Location;
		HZ.Radius = Radius;
		return HZ;
	};

	TArray<FDummyHitzoneConfig> Good;
	Good.Add(MakeHitzone(TEXT("Head"), TEXT("Hitzone.Head"), TEXT("Red"),
		FVector(0.f, 0.f, 0.f), 30.f));
	Good.Add(MakeHitzone(TEXT("Torso"), TEXT("Hitzone.Torso"), TEXT("White"),
		FVector(100.f, 0.f, 0.f), 30.f));
	Good.Add(MakeHitzone(TEXT("LeftLeg"), TEXT("Hitzone.LeftLeg"), TEXT("Orange"),
		FVector(0.f, 100.f, 0.f), 30.f));

	FString Error;
	TestTrue(TEXT("three non-overlapping colors validate"),
		UMHGZDummyConfig::ValidateHitzoneConfigs(Good, Error));
	TestTrue(TEXT("valid config leaves no error"), Error.IsEmpty());

	UMHGZDummyConfig* Config = NewObject<UMHGZDummyConfig>();
	Config->Hitzones = Good;
	FDataValidationContext EditorContext;
	TestEqual(TEXT("editor IsDataValid passes"),
		Config->IsDataValid(EditorContext), EDataValidationResult::Valid);

	TArray<FDummyHitzoneConfig> Overlap = Good;
	Overlap[2].RelativeLocation = FVector(50.f, 0.f, 0.f); // 50 < 30+30
	TestFalse(TEXT("overlapping spheres rejected"),
		UMHGZDummyConfig::ValidateHitzoneConfigs(Overlap, Error));
	TestTrue(TEXT("overlap error names the spheres"), Error.Contains(TEXT("overlap")));

	TArray<FDummyHitzoneConfig> MissingColor = Good;
	MissingColor[2].ExtractColorTag =
		FGameplayTag::RequestGameplayTag(TEXT("WeaponResource.IG.Extract.Red"));
	TestFalse(TEXT("duplicate color missing Orange rejected"),
		UMHGZDummyConfig::ValidateHitzoneConfigs(MissingColor, Error));

	TArray<FDummyHitzoneConfig> ZeroRadius = Good;
	ZeroRadius[0].Radius = 0.f;
	TestFalse(TEXT("zero radius rejected"),
		UMHGZDummyConfig::ValidateHitzoneConfigs(ZeroRadius, Error));

	TestFalse(TEXT("touching spheres are not overlapping"),
		UMHGZDummyConfig::AreHitzoneSpheresOverlapping(
			FVector::ZeroVector, 30.f, FVector(60.f, 0.f, 0.f), 30.f));
	TestTrue(TEXT("overlapping spheres detected"),
		UMHGZDummyConfig::AreHitzoneSpheresOverlapping(
			FVector::ZeroVector, 30.f, FVector(59.f, 0.f, 0.f), 30.f));
	return true;
}

// 4. Deterministic dummy entry point: same GUID settles at most once through the resolver.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM2DummyCounterTestAttack,
	"MHGZ.M2.Dummy.CounterTestAttackDeterministic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM2DummyCounterTestAttack::RunTest(const FString& Parameters)
{
	EnsureM2AbilitySystemGlobals();

	UWorld* World = CreateM2IncomingTestWorld();
	if (!World)
	{
		return false;
	}

	ACharacter* Character = SpawnM2Character(World);
	AMHGZTrainingDummy* Dummy = SpawnM2Dummy(World);
	if (!Character || !Dummy)
	{
		DestroyM2IncomingTestWorld(World);
		return false;
	}

	UMHGZAbilitySystemComponent* PlayerASC =
		NewObject<UMHGZAbilitySystemComponent>(Character);
	Character->AddInstanceComponent(PlayerASC);
	PlayerASC->RegisterComponent();
	UMHGZAttributeSet* PlayerAttrs = NewObject<UMHGZAttributeSet>(Character);
	PlayerASC->AddAttributeSetSubobject(PlayerAttrs);
	PlayerASC->InitAbilityActorInfo(Character, Character);

	Dummy->GetAbilitySystemComponent()->InitAbilityActorInfo(Dummy, Dummy);
	AddM2Resolver(Character);

	UMHGZDummyConfig* Config = NewObject<UMHGZDummyConfig>();
	Config->CounterTestAttack.Damage = 5.f;
	Config->CounterTestAttack.bCounterable = false;
	Dummy->DummyConfig = Config;

	const FGuid AttackID = FGuid::NewGuid();
	TestEqual(TEXT("first deterministic submission applies"),
		Dummy->SubmitCounterTestAttack(Character, AttackID), EIncomingHitSubmitResult::Applied);
	TestEqual(TEXT("health reduced by config damage"), PlayerAttrs->GetHealth(), 95.f);

	TestEqual(TEXT("same GUID resubmit returns Duplicate"),
		Dummy->SubmitCounterTestAttack(Character, AttackID), EIncomingHitSubmitResult::Duplicate);
	TestEqual(TEXT("no double damage from duplicate GUID"), PlayerAttrs->GetHealth(), 95.f);

	const FGuid SecondID = FGuid::NewGuid();
	TestEqual(TEXT("new GUID applies again"),
		Dummy->SubmitCounterTestAttack(Character, SecondID), EIncomingHitSubmitResult::Applied);
	TestEqual(TEXT("health reduced again"), PlayerAttrs->GetHealth(), 90.f);

	DestroyM2IncomingTestWorld(World);
	return true;
}

#endif
