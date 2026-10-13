// Copyright MHGZ Project. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "AbilitySystemGlobals.h"
#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "ActionSystem/MHGZComboCoordinatorAbility.h"
#include "ActionSystem/MHGZDamageGameplayEffect.h"
#include "ActionSystem/MHGZDescendingThrustAbility.h"
#include "ActionSystem/MHGZInsectGlaiveAbility.h"
#include "ActionSystem/MHGZGameplayEffectContext.h"
#include "ActionSystem/AnimNotify_DescendingThrustEvent.h"
#include "Animation/ActiveMontageInstanceScope.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "AnimationBlueprintLibrary.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "AttributeSystem/Res_InsectGlaive.h"
#include "Components/BoxComponent.h"
#include "Curves/CurveVector.h"
#include "Curves/RichCurve.h"
#include "Engine/World.h"
#include "EngineGlobals.h"
#include "Equipment/MHGZEquipmentComponent.h"
#include "Equipment/MHGZEquipmentDefinition.h"
#include "Equipment/MHGZEquipmentInstance.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputSystem/MHGZWeaponInputRouterComponent.h"
#include "WeaponRuntime/MHGZWeaponInputProfile.h"
#include "InsectGlaive/InsectGlaiveCombatConfig.h"
#include "InsectGlaive/Kinsect/InsectGlaiveKinsectData.h"
#include "InsectGlaive/Kinsect/Kinsect.h"
#include "MHGZPlayerState.h"
#include "ActionSystem/MHGZWeaponComboData.h"
#include "MHGZM2DescendingThrustLifecycleTestTypes.h"
#include "../../Shared/MHGZM5KinsectSlashTestTypes.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"
#include "Movement/MHGZInstrumentedCharacterMovementComponent.h"
#include "WeaponRuntime/MHGZWeaponRuntimeDefinition.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

namespace
{
FGameplayTag DescLifecycleTag(const TCHAR* Name)
{
	return FGameplayTag::RequestGameplayTag(FName(Name));
}

bool IsRegisteredForAction(UMHGZWeaponRuntimeHostComponent* Host,
	USkeletalMeshComponent* Mesh, const int32 MontageInstanceID,
	const FWeaponActionToken& ExpectedAction)
{
	FWeaponActionToken RegisteredAction;
	return Host && Host->ResolveMontage(Mesh, MontageInstanceID, RegisteredAction)
		&& RegisteredAction == ExpectedAction;
}

struct FScopedPreviousMoveSnapshotFrame
{
	const uint64 SavedFrame = GFrameCounter;
	uint64 CapturedFrame = 0;

	explicit FScopedPreviousMoveSnapshotFrame(AMHGZCharacter* Character)
	{
		// The production GA deliberately consumes only the exact N-1 input frame.
		// A commandlet automation test does not advance world ticks between setup
		// and TryActivateAbility, so capture one real Character tick at N and expose
		// activation at N+1 for the duration of that synchronous call.
		GFrameCounter = FMath::Max<uint64>(GFrameCounter, 2);
		if (Character)
		{
			Character->DoMove(0.6f, 0.8f);
			CapturedFrame = GFrameCounter;
			Character->Tick(0.0f);
		}
		++GFrameCounter;
	}

	~FScopedPreviousMoveSnapshotFrame()
	{
		GFrameCounter = SavedFrame;
	}
};

/** Advances the commandlet's synthetic frame clock alongside manually driven movement. */
struct FScopedLifecycleTestFrameSequence
{
	const uint64 SavedFrame = GFrameCounter;
	uint64 LastCapturedInputFrame = 0;

	FScopedLifecycleTestFrameSequence()
	{
		GFrameCounter = FMath::Max<uint64>(GFrameCounter, 2);
	}

	~FScopedLifecycleTestFrameSequence()
	{
		GFrameCounter = SavedFrame;
	}

	void CaptureInputForPreviousFrame(AMHGZCharacter* Character)
	{
		if (!Character)
		{
			return;
		}
		Character->DoMove(0.6f, 0.8f);
		LastCapturedInputFrame = GFrameCounter;
		Character->Tick(0.0f);
		++GFrameCounter;
	}
};

UCurveVector* MakeDescendingThrustLifecycleCurve()
{
	UCurveVector* Curve = NewObject<UCurveVector>(GetTransientPackage());
	if (!Curve)
	{
		return nullptr;
	}
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const FKeyHandle Start = Curve->FloatCurves[Axis].AddKey(0.0f, 0.0f);
		Curve->FloatCurves[Axis].GetKey(Start).InterpMode = RCIM_Linear;
		const float EndValue = Axis == 2 ? -900.0f : 0.0f;
		const FKeyHandle End = Curve->FloatCurves[Axis].AddKey(1.0f, EndValue);
		Curve->FloatCurves[Axis].GetKey(End).InterpMode = RCIM_Linear;
	}
	return Curve;
}

FRecordedLocalTrajectoryProfile MakeDescendingThrustLifecycleProfile()
{
	FRecordedLocalTrajectoryProfile Profile;
	Profile.Curve = MakeDescendingThrustLifecycleCurve();
	Profile.CoveredSeconds = 1.0f;
	Profile.TerminalVelocityLocal = FVector(0.0f, 0.0f, -900.0f);
	Profile.TerminalAccelerationLocal = FVector(0.0f, 0.0f, -1800.0f);
	return Profile;
}

UAnimMontage* MakeDescendingThrustTestMontage(UAnimSequence* Sequence,
	const TArray<TPair<FName, float>>& Sections,
	const TOptional<EDescendingThrustMontageEvent> Event)
{
	if (!Sequence || !Sequence->GetSkeleton() || Sequence->GetPlayLength() <= 0.02f)
	{
		return nullptr;
	}

	UAnimMontage* Montage = NewObject<UAnimMontage>(GetTransientPackage());
	if (!Montage)
	{
		return nullptr;
	}
	Montage->SetFlags(RF_Transient);
	Montage->SetSkeleton(Sequence->GetSkeleton());
	Montage->SlotAnimTracks.Reset();
	FSlotAnimationTrack& Slot = Montage->SlotAnimTracks.AddDefaulted_GetRef();
	Slot.SlotName = TEXT("DefaultSlot");
	FAnimSegment& Segment = Slot.AnimTrack.AnimSegments.AddDefaulted_GetRef();
	Segment.SetAnimReference(Sequence, true);
	Segment.StartPos = 0.0f;
	Segment.AnimStartTime = 0.0f;
	Segment.AnimEndTime = Sequence->GetPlayLength();
	Segment.AnimPlayRate = 1.0f;
	Segment.LoopingCount = 1;
	Montage->SetCompositeLength(Montage->CalculateSequenceLength());
	for (const TPair<FName, float>& SectionSpec : Sections)
	{
		FCompositeSection& Section = Montage->CompositeSections.AddDefaulted_GetRef();
		Section.SectionName = SectionSpec.Key;
		Section.Link(Montage, SectionSpec.Value, 0);
		Section.SetTime(SectionSpec.Value);
	}
	Montage->UpdateLinkableElements();
	Montage->RefreshCacheData();

	if (Event.IsSet())
	{
		const FName TrackName(TEXT("M2Lifecycle"));
		UAnimationBlueprintLibrary::AddAnimationNotifyTrack(
			Montage, TrackName, FLinearColor(0.2f, 0.8f, 1.0f));
		UAnimNotify_DescendingThrustEvent* Notify =
			Cast<UAnimNotify_DescendingThrustEvent>(
				UAnimationBlueprintLibrary::AddAnimationNotifyEvent(
					Montage, TrackName, FMath::Min(0.02f,
						FMath::Max(0.001f, Montage->GetPlayLength() * 0.1f)),
					UAnimNotify_DescendingThrustEvent::StaticClass()));
		if (!Notify)
		{
			return nullptr;
		}
		Notify->Event = Event.GetValue();
		Montage->RefreshCacheData();
	}
	return Montage;
}

UAnimMontage* MakeDescendingThrustLandingMontage()
{
	UAnimSequence* Source = LoadObject<UAnimSequence>(nullptr,
		TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_JiXiTuCi_Over.AS_Unsh_JiXiTuCi_Over"));
	if (!Source)
	{
		return nullptr;
	}
	UAnimSequence* RootMotionCopy = DuplicateObject<UAnimSequence>(Source,
		GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(),
			UAnimSequence::StaticClass(), TEXT("M2DescendingThrustOverSequence")));
	if (!RootMotionCopy)
	{
		return nullptr;
	}
	RootMotionCopy->ClearFlags(RF_Public | RF_Standalone);
	RootMotionCopy->SetFlags(RF_Transient);
	RootMotionCopy->bEnableRootMotion = true;
	RootMotionCopy->bForceRootLock = true;
	RootMotionCopy->RootMotionRootLock = ERootMotionRootLock::AnimFirstFrame;

	UAnimMontage* Montage = MakeDescendingThrustTestMontage(RootMotionCopy,
		{ TPair<FName, float>(TEXT("Over"), 0.0f) },
		EDescendingThrustMontageEvent::Correction3);
	if (Montage && Montage->CompositeSections.Num() == 1)
	{
		Montage->CompositeSections[0].NextSectionName = NAME_None;
		Montage->UpdateLinkableElements();
		Montage->RefreshCacheData();
	}
	return Montage;
}

FAnimNotifyEvent* FindDescendingThrustTestNotify(UAnimMontage* Montage,
	const EDescendingThrustMontageEvent Event)
{
	return Montage ? Montage->Notifies.FindByPredicate(
		[Event](const FAnimNotifyEvent& NotifyEvent)
		{
			const UAnimNotify_DescendingThrustEvent* Notify =
				Cast<UAnimNotify_DescendingThrustEvent>(NotifyEvent.Notify);
			return Notify && Notify->Event == Event;
		}) : nullptr;
}

bool DispatchDescendingThrustTestNotify(USkeletalMeshComponent* Mesh,
	UAnimMontage* Montage, FAnimNotifyEvent* NotifyEvent,
	const int32 MontageInstanceID)
{
	UAnimNotify_DescendingThrustEvent* Notify = NotifyEvent
		? Cast<UAnimNotify_DescendingThrustEvent>(NotifyEvent->Notify) : nullptr;
	if (!Mesh || !Montage || !Notify || NotifyEvent->NotifyStateClass
		|| MontageInstanceID == INDEX_NONE)
	{
		return false;
	}
	FAnimNotifyEventReference EventReference(NotifyEvent, Montage);
	EventReference.AddContextData<UE::Anim::FAnimNotifyMontageInstanceContext>(
		MontageInstanceID);
	Notify->Notify(Mesh, Montage, EventReference);
	return true;
}

UAnimMontage* MakeDescendingThrustMainMontage()
{
	UAnimSequence* Sequence = LoadObject<UAnimSequence>(nullptr,
		TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_JiXiTuCi_StartAndFalling.AS_Unsh_JiXiTuCi_StartAndFalling"));
	if (!Sequence)
	{
		return nullptr;
	}
	const float LoopStart = FMath::Clamp(Sequence->GetPlayLength() * 0.15f,
		0.02f, Sequence->GetPlayLength() - 0.01f);
	UAnimMontage* Montage = MakeDescendingThrustTestMontage(Sequence,
		{ TPair<FName, float>(TEXT("Start"), 0.0f),
		  TPair<FName, float>(TEXT("FallingLoop"), LoopStart) },
		EDescendingThrustMontageEvent::EnterFallingLoop);
	if (Montage && Montage->CompositeSections.Num() == 2)
	{
		Montage->CompositeSections[0].NextSectionName = TEXT("FallingLoop");
		Montage->CompositeSections[1].NextSectionName = TEXT("FallingLoop");
		Montage->UpdateLinkableElements();
		Montage->RefreshCacheData();
	}
	return Montage;
}

void ConfigureDescendingThrustTestSegments(
	TArray<FAttackSegmentConfig>& OutSegments, const FName RootSocket)
{
	OutSegments.Reset();
	for (int32 Index = 0; Index < 4; ++Index)
	{
		FAttackSegmentConfig& Segment = OutSegments.AddDefaulted_GetRef();
		Segment.Collision.CollisionChannel = ECC_GameTraceChannel1;
		Segment.Collision.HitzoneQueryTag =
			DescLifecycleTag(TEXT("Hitzone.Head"));
		FWeaponTraceRegion& Region = Segment.Collision.TraceRegions.AddDefaulted_GetRef();
		Region.EndSocketName = RootSocket;
		Region.Radius = 24.0f;
		Segment.Damage.DamageEffectClass = UMHGZDamageGameplayEffect::StaticClass();
		Segment.Damage.MotionValue = 1.0f;
		Segment.Damage.BaseStaggerValue = 0.0f;
		Segment.Damage.bRequiresHitToContinue = false;
		Segment.Damage.bUseHitzoneDefense = false;
		Segment.MultiHitCount = 1;
		Segment.MultiHitPolicy = EAttackMultiHitPolicy::ContactOnly;
	}
}

struct FScopedDescendingThrustLifecycleTestConfiguration
{
	UAnimMontage* MainMontage = nullptr;
	UAnimMontage* LandingMontage = nullptr;
	TArray<FAttackSegmentConfig> Segments;

	bool Configure(const FName RootSocket)
	{
		MainMontage = MakeDescendingThrustMainMontage();
		LandingMontage = MakeDescendingThrustLandingMontage();
		if (!MainMontage || !LandingMontage)
		{
			return false;
		}
		ConfigureDescendingThrustTestSegments(Segments, RootSocket);
		UMHGZM2DescendingThrustLifecycleTestAbility::ConfigureForTest(
			MainMontage, LandingMontage, Segments);
		return true;
	}

	~FScopedDescendingThrustLifecycleTestConfiguration()
	{
		UMHGZM2DescendingThrustLifecycleTestAbility::ResetTestConfiguration();
	}
};

AActor* SpawnDescendingThrustFloor(UWorld& World)
{
	AActor* Floor = World.SpawnActor<AActor>();
	if (!Floor)
	{
		return nullptr;
	}
	UBoxComponent* Box = NewObject<UBoxComponent>(Floor);
	Floor->SetRootComponent(Box);
	Box->SetBoxExtent(FVector(2000.0f, 2000.0f, 50.0f));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Box->SetCollisionObjectType(ECC_WorldStatic);
	Box->SetCollisionResponseToAllChannels(ECR_Block);
	Box->RegisterComponent();
	Floor->SetActorLocation(FVector(0.0f, 0.0f, -50.0f));
	return Floor;
}

struct FDescendingThrustLifecycleHarness
{
	UWorld* World = nullptr;
	AMHGZM5KinsectSlashMovementTestCharacter* Character = nullptr;
	AMHGZPlayerState* PlayerState = nullptr;
	AMHGZM5KinsectSlashAimTestController* InputController = nullptr;
	UMHGZAbilitySystemComponent* ASC = nullptr;
	UMHGZWeaponRuntimeHostComponent* Host = nullptr;
	UMHGZEquipmentComponent* Equipment = nullptr;
	URes_InsectGlaive* Resource = nullptr;
	UInsectGlaiveCombatConfig* CombatConfig = nullptr;
	UMHGZInstrumentedCharacterMovementComponent* CMC = nullptr;
	AActor* Target = nullptr;
	UAbilitySystemComponent* TargetASC = nullptr;
	UMHGZAttributeSet* TargetAttributes = nullptr;
	UMHGZMonsterHitzoneComponent* TargetHitzone = nullptr;
	FGameplayAbilitySpecHandle AbilityHandle;
	UMHGZDescendingThrustAbility* Ability = nullptr;
	FWeaponActionToken PriorRotationOwnerToken;
	UMHGZM5KinsectSlashMovementTestAbility* PriorRotationOwnerInstance = nullptr;
	uint64 ActivationSnapshotFrame = 0;
	FString FailureStage;

	bool Build()
	{
		UAbilitySystemGlobals& Globals = UAbilitySystemGlobals::Get();
		if (!Globals.IsAbilitySystemGlobalsInitialized())
		{
			Globals.InitGlobalData();
		}
		World = UWorld::CreateWorld(EWorldType::Game, false);
		if (!World)
		{
			FailureStage = TEXT("world");
			return false;
		}
		if (!SpawnDescendingThrustFloor(*World))
		{
			FailureStage = TEXT("floor");
			return false;
		}
		Character = World->SpawnActor<AMHGZM5KinsectSlashMovementTestCharacter>(
			FVector(0.0f, 0.0f, 700.0f), FRotator::ZeroRotator);
		PlayerState = World->SpawnActor<AMHGZPlayerState>();
		InputController = World->SpawnActor<AMHGZM5KinsectSlashAimTestController>();
		if (!Character || !PlayerState || !InputController)
		{
			FailureStage = TEXT("character/player state/input controller");
			return false;
		}
		Character->SetPlayerState(PlayerState);
		InputController->SetPlayerState(PlayerState);
		InputController->SetControlRotation(FRotator(0.0f, 35.0f, 0.0f));
		InputController->Possess(Character);
		if (Character->GetController() != InputController
			|| Character->GetPlayerState() != PlayerState)
		{
			FailureStage = TEXT("controller possession preserves PlayerState");
			return false;
		}
		CMC = Cast<UMHGZInstrumentedCharacterMovementComponent>(
			Character->GetCharacterMovement());
		ASC = PlayerState->GetMHGZAbilitySystemComponent();
		Equipment = PlayerState->GetEquipmentComponent();
		if (!CMC || !ASC || !Equipment)
		{
			FailureStage = TEXT("instrumented CMC/ASC/equipment");
			return false;
		}

		USkeletalMeshComponent* Mesh = Character->GetMesh();
		USkeletalMesh* BodyMesh = LoadObject<USkeletalMesh>(nullptr,
			TEXT("/Game/Characters/Demo/Meshes/Body/SKM_Demo_Body.SKM_Demo_Body"));
		if (!Mesh || !BodyMesh)
		{
			FailureStage = TEXT("demo skeletal mesh");
			return false;
		}
		Mesh->SetSkeletalMesh(BodyMesh);
		Mesh->SetAnimInstanceClass(UAnimInstance::StaticClass());
		ASC->InitAbilityActorInfo(PlayerState, Character);
		ASC->InitializeAbilitySystem();
		ASC->RefreshAbilityActorInfo();
		if (!Mesh->GetAnimInstance())
		{
			FailureStage = TEXT("live AnimInstance");
			return false;
		}

		Host = NewObject<UMHGZWeaponRuntimeHostComponent>(Character);
		if (!Host)
		{
			FailureStage = TEXT("RuntimeHost");
			return false;
		}
		Character->AddInstanceComponent(Host);
		Host->RegisterComponent();
		Host->InitializePawnRuntime(Character, nullptr, ASC, Equipment);

		UMHGZWeaponDefinition* Definition = NewObject<UMHGZWeaponDefinition>(PlayerState);
		UWeaponRuntimeDefinition* RuntimeDefinition =
			NewObject<UWeaponRuntimeDefinition>(PlayerState);
		CombatConfig = NewObject<UInsectGlaiveCombatConfig>(PlayerState);
		UMHGZWeaponComboData* ComboData = NewObject<UMHGZWeaponComboData>(PlayerState);
		UInsectGlaiveKinsectData* KinsectData =
			NewObject<UInsectGlaiveKinsectData>(PlayerState);
		if (!Definition || !RuntimeDefinition || !CombatConfig || !ComboData
			|| !KinsectData)
		{
			FailureStage = TEXT("weapon definition/config");
			return false;
		}
		Definition->ItemID = TEXT("M2DescendingThrustLifecycleTransientWeapon");
		Definition->RuntimeDefinition = RuntimeDefinition;
		RuntimeDefinition->ResourceComponentClass = URes_InsectGlaive::StaticClass();
		RuntimeDefinition->CombatConfig = CombatConfig;
		CombatConfig->ComboData = ComboData;
		CombatConfig->KinsectData = KinsectData;
		CombatConfig->KinsectAttachSocket = NAME_None;
		CombatConfig->KinsectMarkLaunchSocket = NAME_None;
		CombatConfig->DescendingThrustAirProfile =
			MakeDescendingThrustLifecycleProfile();
		CombatConfig->DescendingThrustAirWatchdogSeconds = 8.0f;
		CombatConfig->DescendingThrustLandingBlendInTime = 0.05f;
		KinsectData->FlightSpeed = 2000.0f;
		KinsectData->ReturnSpeed = 2500.0f;
		KinsectData->MaxFlightRange = 3000.0f;
		KinsectData->StraightFlightDistance = 1500.0f;
		KinsectData->StaminaPool = 100.0f;
		KinsectData->StaminaRegenRate = 15.0f;
		KinsectData->HoverDrainRate = 3.0f;
		KinsectData->FlightDrainRate = 8.0f;
		KinsectData->KinsectAttackPower = 10.0f;
		const FGameplayTag WeaponSlot =
			DescLifecycleTag(TEXT("Equipment.Slot.Weapon"));
		Host->InitializePawnRuntime(Character, nullptr, ASC, Equipment);
		UMHGZEquipmentInstance* EquipmentInstance =
			UMHGZEquipmentInstance::CreateEquipmentInstance(PlayerState, Definition);
		if (!WeaponSlot.IsValid() || !EquipmentInstance)
		{
			FailureStage = TEXT("equipment instance");
			return false;
		}
		Equipment->EquipItem(WeaponSlot, EquipmentInstance);
		Resource = Character->FindComponentByClass<URes_InsectGlaive>();
		if (!Resource || Host->GetResourceProvider() != Resource
			|| !Resource->GetKinsectActor())
		{
			FailureStage = TEXT("equipped IG resource");
			return false;
		}

		Host->SetGrounded(false);
		Host->SetSheathed(false);
		Host->AcquireAerialFallingState();
		CMC->bRunPhysicsWithNoController = true;
		CMC->SetMovementMode(MOVE_Falling);
		CMC->Velocity = FVector(400.0f, 0.0f, 0.0f);
		CMC->GravityScale = 0.0f;
		Character->SetActorLocation(FVector(0.0f, 0.0f, 700.0f));
		return true;
	}

	bool PublishPriorAerialSource()
	{
		// This fixture publishes a Host-owned, supported source tag only; it does
		// not claim that the preceding Kinsect Slash GA lifecycle is under test.
		const FGameplayAbilitySpecHandle DummyHandle = ASC->GiveAbility(
			FGameplayAbilitySpec(UMHGZM5KinsectSlashMovementTestAbility::StaticClass(),
				1, INDEX_NONE, ASC));
		if (!DummyHandle.IsValid())
		{
			FailureStage = TEXT("grant source-owner fixture");
			return false;
		}
		FWeaponActionToken SourceAction;
		SourceAction.RuntimeToken = Host->GetCurrentToken();
		SourceAction.AbilityHandle = DummyHandle;
		SourceAction.ActivationSequenceID = Host->AllocateActivationSequenceID();
		SourceAction.AbilityInstance = NewObject<UMHGZM5KinsectSlashMovementTestAbility>(ASC);
		const FGameplayTag SourceTag =
			DescLifecycleTag(TEXT("Combat.State.Aerial.Source.IG_KinsectSlash.Hit"));
		if (!Host->RegisterAction(SourceAction)
			|| !Host->SetAerialMovementSource(SourceAction, SourceTag))
		{
			FailureStage = TEXT("publish supported prior aerial source");
			return false;
		}
		Host->UnregisterAction(SourceAction);
		return Host->GetAerialMovementSource() == SourceTag;
	}

	bool SpawnDamageTarget()
	{
		Target = World->SpawnActor<AActor>();
		if (!Target)
		{
			FailureStage = TEXT("damage target");
			return false;
		}
		TargetASC = NewObject<UMHGZM2RejectingGameplayEffectASC>(Target);
		TargetAttributes = NewObject<UMHGZAttributeSet>(Target);
		TargetHitzone = NewObject<UMHGZMonsterHitzoneComponent>(Target);
		if (!TargetASC || !TargetAttributes || !TargetHitzone)
		{
			FailureStage = TEXT("target ASC/attributes/hitzone");
			return false;
		}
		Target->AddInstanceComponent(TargetASC);
		TargetASC->RegisterComponent();
		TargetASC->AddAttributeSetSubobject(TargetAttributes);
		TargetASC->InitAbilityActorInfo(Target, Target);
		TargetAttributes->InitMaxHealth(10000.0f);
		TargetAttributes->InitHealth(10000.0f);
		TargetAttributes->InitDefense(0.0f);
		Target->AddInstanceComponent(TargetHitzone);
		Target->SetRootComponent(TargetHitzone);
		TargetHitzone->SetSphereRadius(80.0f);
		TargetHitzone->HitzoneTag = DescLifecycleTag(TEXT("Hitzone.Head"));
		TargetHitzone->ExtractColorTag = DescLifecycleTag(
			TEXT("WeaponResource.IG.Extract.Red"));
		TargetHitzone->DefenseMultiplier = 1.0f;
		TargetHitzone->StaggerRate = 1.0f;
		TargetHitzone->SetCollisionObjectType(ECC_GameTraceChannel3);
		TargetHitzone->SetCollisionResponseToAllChannels(ECR_Ignore);
		TargetHitzone->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block);
		TargetHitzone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		TargetHitzone->RegisterComponent();
		Target->SetActorLocation(Character->GetMesh()->GetSocketLocation(TEXT("Root")));
		ASC->SetNumericAttributeBase(UMHGZAttributeSet::GetAttackPowerAttribute(),
			100.0f);
		return TargetHitzone->HitzoneTag.IsValid();
	}

	bool SeedPriorRotationOverride()
	{
		const FGameplayAbilitySpecHandle PriorHandle = ASC->GiveAbility(
			FGameplayAbilitySpec(UMHGZM5KinsectSlashMovementTestAbility::StaticClass(),
				1, INDEX_NONE, ASC));
		PriorRotationOwnerInstance =
			NewObject<UMHGZM5KinsectSlashMovementTestAbility>(ASC);
		PriorRotationOwnerToken.RuntimeToken = Host->GetCurrentToken();
		PriorRotationOwnerToken.AbilityHandle = PriorHandle;
		PriorRotationOwnerToken.ActivationSequenceID =
			Host->AllocateActivationSequenceID();
		PriorRotationOwnerToken.AbilityInstance = PriorRotationOwnerInstance;
		if (!PriorHandle.IsValid() || !PriorRotationOwnerInstance
			|| !Host->RegisterAction(PriorRotationOwnerToken)
			|| !Host->AcquireActionRotationOverride(PriorRotationOwnerToken))
		{
			FailureStage = TEXT("seed a predecessor action with a lingering rotation override");
			return false;
		}
		return true;
	}

	void RetirePriorRotationOverride()
	{
		if (Host && PriorRotationOwnerToken.IsValid())
		{
			Host->UnregisterAction(PriorRotationOwnerToken);
		}
		PriorRotationOwnerToken = FWeaponActionToken();
		PriorRotationOwnerInstance = nullptr;
	}

	bool Activate(const float PhysicalPressDurationSeconds = 0.10f,
		const TOptional<float> ForcedNMinusOneYawDifference = TOptional<float>())
	{
		AbilityHandle = ASC->GiveAbility(FGameplayAbilitySpec(
			UMHGZM2DescendingThrustLifecycleTestAbility::StaticClass(),
			1, INDEX_NONE, ASC));
		if (!AbilityHandle.IsValid())
		{
			FailureStage = TEXT("grant production Descending Thrust GA");
			return false;
		}
		FWeaponAbilityActivationContext Context;
		Context.RuntimeToken = Host->GetCurrentToken();
		Context.ActivationSequenceID = Host->AllocateActivationSequenceID();
		Context.Input.ResolvedInputTag = DescLifecycleTag(TEXT("Input.Weapon.RT"));
		Context.Input.SourceControlTag = DescLifecycleTag(TEXT("Input.Modifier.RT"));
		Context.Input.Phase = EWeaponInputPhase::Started;
		Context.Input.Timestamp = 1.0 + PhysicalPressDurationSeconds;
		Context.Input.PhysicalPressStartedAtSeconds = 1.0;
		Context.Input.PhysicalPressDurationSeconds = PhysicalPressDurationSeconds;
		Context.Input.bHasPhysicalPressDuration = true;
		Context.Input.SequenceID = 12;
		ASC->PrepareWeaponAbilityActivation(AbilityHandle, Context);
		FScopedPreviousMoveSnapshotFrame SnapshotFrame(Character);
		if (SnapshotFrame.CapturedFrame == 0)
		{
			FailureStage = TEXT("capture exact N-1 input frame before activation");
			return false;
		}
		const uint64 ExpectedNMinusOneFrame = SnapshotFrame.CapturedFrame;
		ActivationSnapshotFrame = ExpectedNMinusOneFrame;
		if (ForcedNMinusOneYawDifference.IsSet())
		{
			FActionMoveInputFrameSnapshot InputSnapshot;
			if (!Character->GetActionMoveInputFrameSnapshot(
				ExpectedNMinusOneFrame, InputSnapshot)
				|| !InputSnapshot.bHasMoveInput
				|| InputSnapshot.WorldDirection.IsNearlyZero())
			{
				FailureStage = TEXT("read exact N-1 direction for forced angle boundary");
				return false;
			}
			Character->SetActorRotation(FRotator(0.0f,
				InputSnapshot.WorldDirection.Rotation().Yaw
					- ForcedNMinusOneYawDifference.GetValue(), 0.0f));
		}
		FGameplayTagContainer ActivationFailureTags;
		const FGameplayAbilitySpec* ActivationSpec =
			ASC->FindAbilitySpecFromHandle(AbilityHandle);
		const bool bCanActivate = ActivationSpec && ActivationSpec->Ability
			&& ActivationSpec->Ability->CanActivateAbility(AbilityHandle,
				ASC->AbilityActorInfo.Get(), nullptr, nullptr, &ActivationFailureTags);
		if (!bCanActivate)
		{
			FailureStage = FString::Printf(
				TEXT("GAS CanActivate denied: failures=%s falling=%d unsheathed=%d grounded=%d source=%s"),
				*ActivationFailureTags.ToString(),
				ASC->HasMatchingGameplayTag(DescLifecycleTag(
					TEXT("Combat.State.Aerial.Falling"))),
				ASC->HasMatchingGameplayTag(DescLifecycleTag(
					TEXT("Combat.State.Unsheathed"))),
				Host && Host->IsGrounded(),
				Host ? *Host->GetAerialMovementSource().ToString() : TEXT("none"));
			return false;
		}
		FString SynchronousEndState;
		const FDelegateHandle EndedDelegate = ASC->AbilityEndedCallbacks.AddLambda(
			[&SynchronousEndState, this](UGameplayAbility* EndedAbility)
			{
				const UMHGZDescendingThrustAbility* Desc =
					Cast<UMHGZDescendingThrustAbility>(EndedAbility);
				if (!Desc)
				{
					return;
				}
				FMHGZDescendingThrustTelemetrySnapshot Snapshot;
				Desc->GetRuntimeTelemetrySnapshot(Snapshot);
				SynchronousEndState = FString::Printf(
					TEXT("synchronous EndAbility phase=%d reason=%d committed=%d mainID=%d lease=%llu danceStacks=%d registeredAction=%d"),
					static_cast<int32>(Snapshot.Phase),
					static_cast<int32>(Desc->GetActionEndReason()),
					Desc->IsActionActivationCommitted(), Snapshot.MainMontageInstanceID,
					static_cast<unsigned long long>(Snapshot.MovementLease),
					Resource ? Resource->GetDanceStacks() : -1,
					Host && Host->HasRegisteredAction());
			});
		const bool bStarted = ASC->TryActivateAbility(AbilityHandle);
		ASC->AbilityEndedCallbacks.Remove(EndedDelegate);
		if (!bStarted)
		{
			FailureStage = FString::Printf(
				TEXT("TryActivateAbility rejected lifecycle test subclass (%s)"),
				SynchronousEndState.IsEmpty()
					? TEXT("no synchronous EndAbility") : *SynchronousEndState);
			return false;
		}
		if (FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(AbilityHandle))
		{
			for (UGameplayAbility* Instance : Spec->GetAbilityInstances())
			{
				if (UMHGZDescendingThrustAbility* Desc =
					Cast<UMHGZDescendingThrustAbility>(Instance))
				{
					Ability = Desc;
					break;
				}
			}
		}
		if (!Ability || !Ability->IsActive()
			|| !Ability->IsActionActivationCommitted())
		{
			FString RuntimeState = SynchronousEndState.IsEmpty()
				? TEXT("instance not found; no synchronous end callback")
				: SynchronousEndState;
			if (Ability)
			{
				FMHGZDescendingThrustTelemetrySnapshot Snapshot;
				Ability->GetRuntimeTelemetrySnapshot(Snapshot);
				RuntimeState = FString::Printf(
					TEXT("active=%d committed=%d phase=%d endReason=%d mainID=%d lease=%llu correction0Consumed=%d prior=%s"),
					Ability->IsActive(), Ability->IsActionActivationCommitted(),
					static_cast<int32>(Snapshot.Phase),
					static_cast<int32>(Ability->GetActionEndReason()),
					Snapshot.MainMontageInstanceID,
					static_cast<unsigned long long>(Snapshot.MovementLease),
					Snapshot.bCorrectionConsumed[0],
					*Snapshot.PriorAerialSource.ToString());
			}
			FailureStage = FString::Printf(
				TEXT("production lifecycle GA did not remain active: %s"),
				*RuntimeState);
			return false;
		}
		FMHGZDescendingThrustTelemetrySnapshot Snapshot;
		Ability->GetRuntimeTelemetrySnapshot(Snapshot);
		if (!Snapshot.bCorrectionSnapshotValid[0]
			|| Snapshot.CorrectionFrameNumbers[0] != ExpectedNMinusOneFrame
			|| !FMath::IsNearlyEqual(Snapshot.CorrectionRawMoveInputs[0].X,
				0.6f, 0.001f)
			|| !FMath::IsNearlyEqual(Snapshot.CorrectionRawMoveInputs[0].Y,
				0.8f, 0.001f))
		{
			FailureStage = FString::Printf(
				TEXT("production GA did not consume exact N-1 input (valid=%d frame=%llu expected=%llu raw=%.3f,%.3f)"),
				Snapshot.bCorrectionSnapshotValid[0],
				static_cast<unsigned long long>(Snapshot.CorrectionFrameNumbers[0]),
				static_cast<unsigned long long>(ExpectedNMinusOneFrame),
				Snapshot.CorrectionRawMoveInputs[0].X,
				Snapshot.CorrectionRawMoveInputs[0].Y);
			return false;
		}
		return true;
	}

	void Teardown()
	{
		if (ASC && AbilityHandle.IsValid())
		{
			ASC->CancelAbilityHandle(AbilityHandle);
		}
		if (Host)
		{
			Host->ShutdownRuntime(EWeaponRuntimeEndReason::RuntimeShutdown);
		}
		if (World)
		{
			World->DestroyWorld(false);
		}
		World = nullptr;
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM2DescendingThrustProductionLifecycleTest,
	"MHGZ.M2.DescendingThrust.ProductionLifecycleLandingDamageAndCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM2DescendingThrustProductionLifecycleTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FScopedLifecycleTestFrameSequence FrameSequence;
	FDescendingThrustLifecycleHarness H;
	if (!TestTrue(*FString::Printf(TEXT("production harness builds (%s)"),
		*H.FailureStage), H.Build()))
	{
		H.Teardown();
		return false;
	}
	FScopedDescendingThrustLifecycleTestConfiguration TestConfiguration;
	const FName RootSocket = H.Character->GetMesh()->DoesSocketExist(TEXT("Root"))
		? FName(TEXT("Root")) : FName(TEXT("root"));
	if (!TestTrue(TEXT("the demo skeleton exposes a root trace socket"),
		H.Character->GetMesh()->DoesSocketExist(RootSocket))
		|| !TestTrue(TEXT("transient lifecycle-only main/Over Montage fixtures build"),
			TestConfiguration.Configure(RootSocket)))
	{
		H.Teardown();
		return false;
	}
	if (!TestTrue(*FString::Printf(TEXT("a supported aerial source is published (%s)"),
		*H.FailureStage), H.PublishPriorAerialSource())
		|| !TestTrue(*FString::Printf(TEXT("real target setup succeeds (%s)"),
			*H.FailureStage), H.SpawnDamageTarget())
		|| !TestTrue(*FString::Printf(TEXT("a predecessor owns a lingering rotation override (%s)"),
			*H.FailureStage), H.SeedPriorRotationOverride()))
	{
		H.Teardown();
		return false;
	}
	H.CombatConfig->MaxDanceStacks = 3;
	H.CombatConfig->DanceDamageMultipliers = { 1.0f, 1.2f, 1.4f, 1.6f };
	const bool bDanceStackSeeded = H.Resource->AddDanceStack(
		EIGDanceSource::KinsectSlash);
	const float ExpectedFrozenMultiplier = H.Resource->GetDanceDamageMultiplier();
	if (!TestTrue(TEXT("one real Dance stack is present before Commit"), bDanceStackSeeded)
		|| !TestTrue(*FString::Printf(TEXT("real GAS activation succeeds (%s)"),
			*H.FailureStage), H.Activate()))
	{
		H.Teardown();
		return false;
	}
	TestTrue(TEXT("confirmed aerial attack spends Host CantAttack"),
		H.ASC->HasMatchingGameplayTag(DescLifecycleTag(
			TEXT("Combat.State.Aerial.CantAttack"))));
	TestTrue(TEXT("confirmed aerial attack also spends Host CantDodge"),
		H.ASC->HasMatchingGameplayTag(DescLifecycleTag(
			TEXT("Combat.State.Aerial.CantDodge"))));
	TestTrue(TEXT("the new movement owner supersedes the predecessor yaw override"),
		H.Host->IsActionRotationOverrideOwnedBy(H.Ability->GetActionToken()));
	TestFalse(TEXT("the predecessor no longer owns character rotation"),
		H.Host->IsActionRotationOverrideOwnedBy(H.PriorRotationOwnerToken));
	H.RetirePriorRotationOverride();

	FMHGZDescendingThrustTelemetrySnapshot Snapshot;
	H.Ability->GetRuntimeTelemetrySnapshot(Snapshot);
	TestTrue(TEXT("the exact production GA is active and committed"),
		Snapshot.bAbilityActive && H.Ability->IsActionActivationCommitted());
	TestTrue(TEXT("Correction1 consumes and applies the exact entry N-1 snapshot"),
		Snapshot.bCorrectionConsumed[0] && Snapshot.bCorrectionSnapshotValid[0]
			&& Snapshot.bCorrectionApplied[0]
			&& Snapshot.CorrectionFrameNumbers[0] == H.ActivationSnapshotFrame);
	TestEqual(TEXT("activation enters the air-start phase"), Snapshot.Phase,
		EDescendingThrustPhase::AirStart);
	TestTrue(TEXT("the production action owns its recorded movement lease"),
		Snapshot.MovementLease != 0 && Snapshot.bRecordedTrajectoryActive
			&& Snapshot.bActionMovementOwned);
	TestTrue(TEXT("the live execution instance has the configured main test Montage"),
		H.Ability->AttackMontage == TestConfiguration.MainMontage);
	TestTrue(TEXT("the live instance registers its main Montage by exact instance ID"),
		Snapshot.MainMontageInstanceID != INDEX_NONE
			&& IsRegisteredForAction(H.Host, H.Character->GetMesh(),
				Snapshot.MainMontageInstanceID, Snapshot.ActionToken));
	TestEqual(TEXT("the action publishes its own named aerial source"),
		H.Host->GetAerialMovementSource(),
		DescLifecycleTag(TEXT("Combat.State.Aerial.Source.IG_DescendingThrust")));
	TestTrue(TEXT("Commit clears Dance only after activation succeeds"),
		H.Resource->GetDanceStacks() == 0);
	TestTrue(TEXT("the committed Dance multiplier is frozen before clearing the stack"),
		FMath::IsNearlyEqual(Snapshot.FrozenDanceDamageMultiplier,
			ExpectedFrozenMultiplier, 0.001f));

	const int32 MainMontageID = Snapshot.MainMontageInstanceID;
	const uint64 InitialMovementLease = Snapshot.MovementLease;
	H.Ability->HandleMontageEvent(Snapshot.ActionToken, MainMontageID + 17,
		EDescendingThrustMontageEvent::EnterFallingLoop);
	H.Ability->GetRuntimeTelemetrySnapshot(Snapshot);
	TestEqual(TEXT("a stale/wrong main montage ID cannot enter FallingLoop"),
		Snapshot.Phase, EDescendingThrustPhase::AirStart);
	FAnimNotifyEvent* MainEntryNotify = FindDescendingThrustTestNotify(
		TestConfiguration.MainMontage, EDescendingThrustMontageEvent::EnterFallingLoop);
	if (!TestNotNull(TEXT("the main test Montage carries the entry Notify"),
		MainEntryNotify))
	{
		H.Teardown();
		return false;
	}
	// The production notify requires UE's MontageInstanceContext. First send a
	// stale ID through the actual UAnimNotify implementation, then the exact ID.
	TestTrue(TEXT("the production Notify is invoked with a stale Montage instance ID"),
		DispatchDescendingThrustTestNotify(H.Character->GetMesh(),
			TestConfiguration.MainMontage, MainEntryNotify, MainMontageID + 17));
	H.Ability->GetRuntimeTelemetrySnapshot(Snapshot);
	TestEqual(TEXT("a stale Notify context cannot enter FallingLoop"),
		Snapshot.Phase, EDescendingThrustPhase::AirStart);
	if (!TestTrue(TEXT("the real AnimNotify resolver dispatches the active main Montage"),
		DispatchDescendingThrustTestNotify(H.Character->GetMesh(),
			TestConfiguration.MainMontage, MainEntryNotify, MainMontageID)))
	{
		H.Teardown();
		return false;
	}
	H.Ability->GetRuntimeTelemetrySnapshot(Snapshot);
	TestEqual(TEXT("the resolved main Montage instance enters FallingLoop"), Snapshot.Phase,
		EDescendingThrustPhase::FallingLoop);
	TestEqual(TEXT("the FallingLoop event keeps the original movement lease"),
		Snapshot.MovementLease, InitialMovementLease);
	const uint64 MovementLease = Snapshot.MovementLease;
	DispatchDescendingThrustTestNotify(H.Character->GetMesh(),
		TestConfiguration.MainMontage, MainEntryNotify, MainMontageID);
	H.Ability->GetRuntimeTelemetrySnapshot(Snapshot);
	TestEqual(TEXT("replaying the entry notify does not re-enter or reset movement"),
		Snapshot.MovementLease, MovementLease);
	TestEqual(TEXT("replaying the entry notify leaves the action in FallingLoop"),
		Snapshot.Phase, EDescendingThrustPhase::FallingLoop);
	TestTrue(TEXT("replaying the loop-entry event cannot consume Correction1 again"),
		Snapshot.bCorrectionConsumed[0]
			&& Snapshot.CorrectionFrameNumbers[0] == H.ActivationSnapshotFrame);

	TArray<FGuid> AppliedDamageContextIDs;
	const FDelegateHandle AppliedDamageDelegate =
		H.TargetASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
			[&AppliedDamageContextIDs](UAbilitySystemComponent*,
				const FGameplayEffectSpec& AppliedSpec,
				FActiveGameplayEffectHandle)
			{
				const FMHGZGameplayEffectContext* Context =
					FMHGZGameplayEffectContext::ExtractEffectContext(
						AppliedSpec.GetEffectContext());
				AppliedDamageContextIDs.Add(Context
					? Context->AttackInstanceID : FGuid());
			});
	// Actual AttackCollision sweep path, real target ASC, and real production GE.
	for (int32 SegmentIndex = 0; SegmentIndex < 4; ++SegmentIndex)
	{
		H.Ability->EnableCollision(SegmentIndex);
		H.Ability->TickCollision(SegmentIndex, 1.0f / 60.0f);
		H.Ability->DisableCollision(SegmentIndex);
	}
	H.Ability->GetRuntimeTelemetrySnapshot(Snapshot);
	for (int32 SegmentIndex = 0; SegmentIndex < 4; ++SegmentIndex)
	{
		TestEqual(*FString::Printf(TEXT("segment %d successfully applies once despite a repeated sweep"),
			SegmentIndex), Snapshot.SuccessfulTargetCounts[SegmentIndex], 1);
		TestTrue(*FString::Printf(TEXT("segment %d owns a distinct per-activation GE identity"),
			SegmentIndex), Snapshot.SegmentDamageInstanceIDs[SegmentIndex].IsValid());
		for (int32 OtherSegment = 0; OtherSegment < SegmentIndex; ++OtherSegment)
		{
			TestTrue(*FString::Printf(TEXT("segment %d GE identity differs from segment %d"),
				SegmentIndex, OtherSegment),
			Snapshot.SegmentDamageInstanceIDs[SegmentIndex]
				!= Snapshot.SegmentDamageInstanceIDs[OtherSegment]);
		}
	}
	H.TargetASC->OnGameplayEffectAppliedDelegateToSelf.Remove(AppliedDamageDelegate);
	TestTrue(TEXT("the actual target health decreased through four production GEs"),
		H.TargetAttributes->GetHealth() < 10000.0f);
	TestEqual(TEXT("four applied production GEs expose four context identities"),
		AppliedDamageContextIDs.Num(), 4);
	for (int32 SegmentIndex = 0; SegmentIndex < 4
		&& SegmentIndex < AppliedDamageContextIDs.Num(); ++SegmentIndex)
	{
		TestEqual(*FString::Printf(TEXT("applied segment %d GE carries its exact production context GUID"),
			SegmentIndex), AppliedDamageContextIDs[SegmentIndex],
			Snapshot.SegmentDamageInstanceIDs[SegmentIndex]);
	}

	// Reopening the same segment in a distinct notify-like window must not re-hit
	// the same actor for this activation; segment-local ledger survives Disable.
	const float HealthAfterFirstWindows = H.TargetAttributes->GetHealth();
	H.Ability->EnableCollision(0);
	H.Ability->TickCollision(0, 1.0f / 60.0f);
	H.Ability->DisableCollision(0);
	H.Ability->GetRuntimeTelemetrySnapshot(Snapshot);
	TestEqual(TEXT("reopened segment 0 keeps one successful target for the activation"),
		Snapshot.SuccessfulTargetCounts[0], 1);
	TestTrue(TEXT("reopened segment 0 does not apply a second damage GE"),
		FMath::IsNearlyEqual(H.TargetAttributes->GetHealth(), HealthAfterFirstWindows));

	// Drive the real instrumented CMC in small physical steps. Only the enabled
	// LandingAirborne lease may report the actual blocking floor as a landing.
	for (int32 Frame = 0; Frame < 120 && H.Ability->IsActive(); ++Frame)
	{
		// Mirror the game-frame order: capture physical input at N, then let the
		// landing callback at N+1 consume precisely that N-1 snapshot.
		FrameSequence.CaptureInputForPreviousFrame(H.Character);
		H.CMC->PerformMovement(1.0f / 60.0f);
		H.Ability->GetRuntimeTelemetrySnapshot(Snapshot);
		if (Snapshot.Phase == EDescendingThrustPhase::GroundOver)
		{
			break;
		}
	}
	H.Ability->GetRuntimeTelemetrySnapshot(Snapshot);
	if (!TestTrue(*FString::Printf(
		TEXT("real CMC contact enters Over (phase=%d mode=%d z=%.1f; %s)"),
		static_cast<int32>(Snapshot.Phase), static_cast<int32>(H.CMC->MovementMode),
		H.Character->GetActorLocation().Z, *H.FailureStage),
		Snapshot.Phase == EDescendingThrustPhase::GroundOver))
	{
		H.Teardown();
		return false;
	}
	TestTrue(TEXT("landing goes through Host's normal grounded cleanup"), H.Host->IsGrounded());
	TestTrue(TEXT("Over owns the exact action's Montage Root Motion lease"),
		Snapshot.bLandingRootMotionOwned && Snapshot.bLandingRootMotionModeCaptured
			&& H.Host->IsMontageRootMotionOwnedBy(Snapshot.ActionToken));
	TestEqual(TEXT("landing leaves the CMC on the walkable floor"),
		H.CMC->MovementMode, MOVE_Walking);
	TestTrue(*FString::Printf(
		TEXT("Correction2 consumes and applies landing N-1 (consumed=%d valid=%d applied=%d frame=%llu activation=%llu current=%llu)"),
		Snapshot.bCorrectionConsumed[1] ? 1 : 0,
		Snapshot.bCorrectionSnapshotValid[1] ? 1 : 0,
		Snapshot.bCorrectionApplied[1] ? 1 : 0,
		Snapshot.CorrectionFrameNumbers[1], H.ActivationSnapshotFrame,
		GFrameCounter),
		Snapshot.bCorrectionConsumed[1] && Snapshot.bCorrectionSnapshotValid[1]
			&& Snapshot.bCorrectionApplied[1]
			&& Snapshot.CorrectionFrameNumbers[1]
				== FrameSequence.LastCapturedInputFrame);
	TestTrue(TEXT("Over receives a distinct registered Montage instance"),
		Snapshot.LandingMontageInstanceID != INDEX_NONE
			&& Snapshot.LandingMontageInstanceID != Snapshot.MainMontageInstanceID
			&& Snapshot.LandingMontagePath == TestConfiguration.LandingMontage->GetPathName()
			&& IsRegisteredForAction(H.Host, H.Character->GetMesh(),
				Snapshot.LandingMontageInstanceID, Snapshot.ActionToken));
	FAnimNotifyEvent* OverCorrectionNotify = FindDescendingThrustTestNotify(
		TestConfiguration.LandingMontage,
		EDescendingThrustMontageEvent::Correction3);
	if (!TestNotNull(TEXT("the Over test Montage carries Correction3"),
		OverCorrectionNotify))
	{
		H.Teardown();
		return false;
	}
	const float DirectionTargetYaw = Snapshot.CorrectionTargets[0].Rotation().Yaw;
	H.Character->SetActorRotation(FRotator(0.0f,
		DirectionTargetYaw - 149.9f, 0.0f));
	FrameSequence.CaptureInputForPreviousFrame(H.Character);
	if (!TestTrue(TEXT("the real AnimNotify resolver dispatches active Over"),
		DispatchDescendingThrustTestNotify(H.Character->GetMesh(),
			TestConfiguration.LandingMontage, OverCorrectionNotify,
			Snapshot.LandingMontageInstanceID)))
	{
		H.Teardown();
		return false;
	}
	H.Ability->GetRuntimeTelemetrySnapshot(Snapshot);
	TestTrue(*FString::Printf(
		TEXT("Correction3 accepts 149.9 degrees with N-1 (consumed=%d valid=%d applied=%d frame=%llu current=%llu angle=%.2f)"),
		Snapshot.bCorrectionConsumed[2] ? 1 : 0,
		Snapshot.bCorrectionSnapshotValid[2] ? 1 : 0,
		Snapshot.bCorrectionApplied[2] ? 1 : 0,
		Snapshot.CorrectionFrameNumbers[2], GFrameCounter,
		Snapshot.CorrectionAngles[2]),
		Snapshot.bCorrectionConsumed[2] && Snapshot.bCorrectionSnapshotValid[2]
			&& Snapshot.bCorrectionApplied[2]
			&& Snapshot.CorrectionFrameNumbers[2]
				== FrameSequence.LastCapturedInputFrame
			&& FMath::IsNearlyEqual(FMath::Abs(Snapshot.CorrectionAngles[2]),
				149.9f, 0.5f));
	const uint64 Correction3Frame = Snapshot.CorrectionFrameNumbers[2];
	DispatchDescendingThrustTestNotify(H.Character->GetMesh(),
		TestConfiguration.LandingMontage, OverCorrectionNotify,
		Snapshot.LandingMontageInstanceID);
	H.Ability->GetRuntimeTelemetrySnapshot(Snapshot);
	TestEqual(TEXT("replaying Correction3 cannot retry or replace its input frame"),
		Snapshot.CorrectionFrameNumbers[2], Correction3Frame);

	const int32 LandingMontageID = Snapshot.LandingMontageInstanceID;
	const FWeaponActionToken ActionToken = Snapshot.ActionToken;
	const ERootMotionMode::Type SavedRootMotionMode =
		H.Character->GetMesh()->GetAnimInstance()->RootMotionMode.GetValue();
	H.ASC->CancelAbilityHandle(H.AbilityHandle);
	TestFalse(TEXT("cancelling an active Over ends the production GA"),
		H.Ability->IsActive());
	H.Ability->GetRuntimeTelemetrySnapshot(Snapshot);
	TestEqual(TEXT("cancellation enters the terminal Ended phase"), Snapshot.Phase,
		EDescendingThrustPhase::Ended);
	TestFalse(TEXT("cancellation releases the recorded trajectory lease"),
		Snapshot.bRecordedTrajectoryActive);
	TestFalse(TEXT("cancellation releases action movement ownership"),
		H.Host->IsActionMovementOwnedBy(ActionToken));
	TestFalse(TEXT("cancellation releases Montage Root Motion ownership"),
		H.Host->IsMontageRootMotionOwnedBy(ActionToken));
	TestEqual(TEXT("cancellation restores the previous AnimInstance root mode"),
		H.Character->GetMesh()->GetAnimInstance()->RootMotionMode.GetValue(),
		SavedRootMotionMode);
	TestFalse(TEXT("cancellation unregisters the landing Montage instance"),
		IsRegisteredForAction(H.Host, H.Character->GetMesh(), LandingMontageID,
			ActionToken));
	DispatchDescendingThrustTestNotify(H.Character->GetMesh(),
		TestConfiguration.LandingMontage, OverCorrectionNotify, LandingMontageID);
	H.Ability->GetRuntimeTelemetrySnapshot(Snapshot);
	TestEqual(TEXT("a stale Over notify cannot revive a cancelled GA"),
		Snapshot.Phase, EDescendingThrustPhase::Ended);

	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM2DescendingThrustNaturalOverCompletionTest,
	"MHGZ.M2.DescendingThrust.NaturalOverCompletionEndsActionAndReleasesLeases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM2DescendingThrustNaturalOverCompletionTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FScopedLifecycleTestFrameSequence FrameSequence;
	FDescendingThrustLifecycleHarness H;
	if (!TestTrue(*FString::Printf(TEXT("natural-completion harness builds (%s)"),
		*H.FailureStage), H.Build()))
	{
		H.Teardown();
		return false;
	}
	FScopedDescendingThrustLifecycleTestConfiguration TestConfiguration;
	const FName RootSocket = H.Character->GetMesh()->DoesSocketExist(TEXT("Root"))
		? FName(TEXT("Root")) : FName(TEXT("root"));
	if (!TestTrue(TEXT("the demo skeleton exposes a root trace socket"),
		H.Character->GetMesh()->DoesSocketExist(RootSocket))
		|| !TestTrue(TEXT("natural-completion transient Montages build"),
			TestConfiguration.Configure(RootSocket))
		|| !TestTrue(*FString::Printf(TEXT("a supported prior aerial source is published (%s)"),
			*H.FailureStage), H.PublishPriorAerialSource())
		|| !TestTrue(*FString::Printf(TEXT("the production GA activates (%s)"),
			*H.FailureStage), H.Activate()))
	{
		H.Teardown();
		return false;
	}

	FMHGZDescendingThrustTelemetrySnapshot Snapshot;
	H.Ability->GetRuntimeTelemetrySnapshot(Snapshot);
	const FWeaponActionToken ActionToken = Snapshot.ActionToken;
	const int32 MainMontageID = Snapshot.MainMontageInstanceID;
	FAnimNotifyEvent* EntryNotify = FindDescendingThrustTestNotify(
		TestConfiguration.MainMontage,
		EDescendingThrustMontageEvent::EnterFallingLoop);
	if (!TestNotNull(TEXT("the Main Montage has its falling-loop entry Notify"), EntryNotify)
		|| !TestTrue(TEXT("the exact Main Notify resolves through production routing"),
			DispatchDescendingThrustTestNotify(H.Character->GetMesh(),
				TestConfiguration.MainMontage, EntryNotify, MainMontageID)))
	{
		H.Teardown();
		return false;
	}

	for (int32 Frame = 0; Frame < 120 && H.Ability->IsActive(); ++Frame)
	{
		FrameSequence.CaptureInputForPreviousFrame(H.Character);
		H.CMC->PerformMovement(1.0f / 60.0f);
		H.Ability->GetRuntimeTelemetrySnapshot(Snapshot);
		if (Snapshot.Phase == EDescendingThrustPhase::GroundOver)
		{
			break;
		}
	}
	H.Ability->GetRuntimeTelemetrySnapshot(Snapshot);
	if (!TestTrue(TEXT("real CMC floor contact enters GroundOver"),
		Snapshot.Phase == EDescendingThrustPhase::GroundOver))
	{
		H.Teardown();
		return false;
	}

	const int32 LandingMontageID = Snapshot.LandingMontageInstanceID;
	const ERootMotionMode::Type SavedRootMotionMode =
		H.Character->GetMesh()->GetAnimInstance()->RootMotionMode.GetValue();
	UAnimInstance* AnimInstance = H.Character->GetMesh()->GetAnimInstance();
	AnimInstance->Montage_SetPosition(TestConfiguration.LandingMontage,
		TestConfiguration.LandingMontage->GetPlayLength());
	UMHGZM2DescendingThrustLifecycleTestAbility* TestAbility =
		Cast<UMHGZM2DescendingThrustLifecycleTestAbility>(H.Ability);
	if (!TestNotNull(TEXT("the executing ability exposes only the automation task driver"),
		TestAbility)
		|| !TestTrue(TEXT("the exact Over Montage task reports natural end"),
			TestAbility->DriveMontageTaskToEndForTest(
				TEXT("DescendingThrustLandingMontage"))))
	{
		H.Teardown();
		return false;
	}

	H.Ability->GetRuntimeTelemetrySnapshot(Snapshot);
	TestFalse(TEXT("natural Over completion ends the production GA"),
		H.Ability->IsActive());
	TestEqual(TEXT("natural completion records the Normal action end reason"),
		H.Ability->GetActionEndReason(), EWeaponActionEndReason::Normal);
	TestEqual(TEXT("natural completion reaches terminal Ended phase"),
		Snapshot.Phase, EDescendingThrustPhase::Ended);
	TestFalse(TEXT("natural completion releases recorded trajectory movement"),
		Snapshot.bRecordedTrajectoryActive);
	TestFalse(TEXT("natural completion releases the action movement lease"),
		H.Host->IsActionMovementOwnedBy(ActionToken));
	TestFalse(TEXT("natural completion releases the Landing Root Motion lease"),
		H.Host->IsMontageRootMotionOwnedBy(ActionToken));
	TestFalse(TEXT("natural completion unregisters the exact Over Montage instance"),
		IsRegisteredForAction(H.Host, H.Character->GetMesh(), LandingMontageID,
			ActionToken));
	TestEqual(TEXT("natural completion restores the previous AnimInstance root mode"),
		AnimInstance->RootMotionMode.GetValue(), SavedRootMotionMode);

	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM2DescendingThrustCorrection3ToleranceBoundaryTest,
	"MHGZ.M2.DescendingThrust.Correction3ToleranceBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM2DescendingThrustCorrection3ToleranceBoundaryTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	auto RunCorrectionCase = [this](const float DifferenceDegrees,
		const bool bShouldApply)
	{
		FScopedLifecycleTestFrameSequence FrameSequence;
		FDescendingThrustLifecycleHarness H;
		if (!TestTrue(*FString::Printf(TEXT("Correction3 %.1f degree harness builds (%s)"),
			DifferenceDegrees, *H.FailureStage), H.Build()))
		{
			H.Teardown();
			return false;
		}
		FScopedDescendingThrustLifecycleTestConfiguration TestConfiguration;
		const FName RootSocket = H.Character->GetMesh()->DoesSocketExist(TEXT("Root"))
			? FName(TEXT("Root")) : FName(TEXT("root"));
		if (!TestTrue(TEXT("Correction3 boundary test Montages build"),
			TestConfiguration.Configure(RootSocket))
			|| !TestTrue(TEXT("the supported prior aerial source publishes"),
				H.PublishPriorAerialSource())
			|| !TestTrue(TEXT("the production GA activates"), H.Activate()))
		{
			H.Teardown();
			return false;
		}

		FMHGZDescendingThrustTelemetrySnapshot Snapshot;
		H.Ability->GetRuntimeTelemetrySnapshot(Snapshot);
		FAnimNotifyEvent* EntryNotify = FindDescendingThrustTestNotify(
			TestConfiguration.MainMontage,
			EDescendingThrustMontageEvent::EnterFallingLoop);
		if (!TestNotNull(TEXT("the Main Montage has its falling-loop entry Notify"),
			EntryNotify)
			|| !TestTrue(TEXT("the Main Notify resolves through production routing"),
				DispatchDescendingThrustTestNotify(H.Character->GetMesh(),
					TestConfiguration.MainMontage, EntryNotify,
					Snapshot.MainMontageInstanceID)))
		{
			H.Teardown();
			return false;
		}

		for (int32 Frame = 0; Frame < 120 && H.Ability->IsActive(); ++Frame)
		{
			FrameSequence.CaptureInputForPreviousFrame(H.Character);
			H.CMC->PerformMovement(1.0f / 60.0f);
			H.Ability->GetRuntimeTelemetrySnapshot(Snapshot);
			if (Snapshot.Phase == EDescendingThrustPhase::GroundOver)
			{
				break;
			}
		}
		H.Ability->GetRuntimeTelemetrySnapshot(Snapshot);
		if (!TestTrue(TEXT("real CMC contact enters Over before Correction3"),
			Snapshot.Phase == EDescendingThrustPhase::GroundOver))
		{
			H.Teardown();
			return false;
		}

		FAnimNotifyEvent* CorrectionNotify = FindDescendingThrustTestNotify(
			TestConfiguration.LandingMontage,
			EDescendingThrustMontageEvent::Correction3);
		const int32 LandingMontageID = Snapshot.LandingMontageInstanceID;
		const float TargetYaw = Snapshot.CorrectionTargets[0].Rotation().Yaw;
		H.Character->SetActorRotation(FRotator(0.0f,
			TargetYaw - DifferenceDegrees, 0.0f));
		FrameSequence.CaptureInputForPreviousFrame(H.Character);
		if (!TestNotNull(TEXT("the Over Montage has the Correction3 Notify"),
			CorrectionNotify)
			|| !TestTrue(TEXT("the exact Over Notify resolves through production routing"),
				DispatchDescendingThrustTestNotify(H.Character->GetMesh(),
					TestConfiguration.LandingMontage, CorrectionNotify,
					LandingMontageID)))
		{
			H.Teardown();
			return false;
		}

		H.Ability->GetRuntimeTelemetrySnapshot(Snapshot);
		TestTrue(*FString::Printf(TEXT("Correction3 %.1f degree N-1 snapshot is consumed"),
			DifferenceDegrees), Snapshot.bCorrectionConsumed[2]
				&& Snapshot.bCorrectionSnapshotValid[2]
				&& Snapshot.CorrectionFrameNumbers[2]
					== FrameSequence.LastCapturedInputFrame);
		TestEqual(*FString::Printf(TEXT("Correction3 %.1f degree boundary apply result"),
			DifferenceDegrees), Snapshot.bCorrectionApplied[2], bShouldApply);
		TestTrue(*FString::Printf(TEXT("Correction3 reports the %.1f degree frozen difference"),
			DifferenceDegrees), FMath::IsNearlyEqual(
				FMath::Abs(Snapshot.CorrectionAngles[2]), DifferenceDegrees, 0.5f));

		H.Teardown();
		return true;
	};

	const bool bAtInclusiveBoundary = RunCorrectionCase(150.0f, true);
	const bool bBeyondTolerance = RunCorrectionCase(150.1f, false);
	return bAtInclusiveBoundary && bBeyondTolerance;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM2DescendingThrustCorrection12MaxAngleTest,
	"MHGZ.M2.DescendingThrust.Correction12Accept180DegreeNMinusOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM2DescendingThrustCorrection12MaxAngleTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FScopedLifecycleTestFrameSequence FrameSequence;
	FDescendingThrustLifecycleHarness H;
	if (!TestTrue(*FString::Printf(TEXT("180 degree correction harness builds (%s)"),
		*H.FailureStage), H.Build()))
	{
		H.Teardown();
		return false;
	}
	FScopedDescendingThrustLifecycleTestConfiguration TestConfiguration;
	const FName RootSocket = H.Character->GetMesh()->DoesSocketExist(TEXT("Root"))
		? FName(TEXT("Root")) : FName(TEXT("root"));
	if (!TestTrue(TEXT("180 degree correction test Montages build"),
		TestConfiguration.Configure(RootSocket))
		|| !TestTrue(TEXT("the supported prior aerial source publishes"),
			H.PublishPriorAerialSource())
		|| !TestTrue(TEXT("the production GA activates with an exact opposite N-1 input"),
			H.Activate(0.10f, 180.0f)))
	{
		H.Teardown();
		return false;
	}

	FMHGZDescendingThrustTelemetrySnapshot Snapshot;
	H.Ability->GetRuntimeTelemetrySnapshot(Snapshot);
	TestTrue(TEXT("Correction1 accepts the exact 180 degree frozen input"),
		Snapshot.bCorrectionConsumed[0] && Snapshot.bCorrectionSnapshotValid[0]
			&& Snapshot.bCorrectionApplied[0]
			&& FMath::IsNearlyEqual(
				FMath::Abs(Snapshot.CorrectionAngles[0]), 180.0f, 0.5f));

	FAnimNotifyEvent* EntryNotify = FindDescendingThrustTestNotify(
		TestConfiguration.MainMontage,
		EDescendingThrustMontageEvent::EnterFallingLoop);
	if (!TestNotNull(TEXT("the Main Montage has its falling-loop entry Notify"),
		EntryNotify)
		|| !TestTrue(TEXT("the Main Notify resolves through production routing"),
			DispatchDescendingThrustTestNotify(H.Character->GetMesh(),
				TestConfiguration.MainMontage, EntryNotify,
				Snapshot.MainMontageInstanceID)))
	{
		H.Teardown();
		return false;
	}

	for (int32 Frame = 0; Frame < 120 && H.Ability->IsActive(); ++Frame)
	{
		FrameSequence.CaptureInputForPreviousFrame(H.Character);
		FActionMoveInputFrameSnapshot InputSnapshot;
		if (!H.Character->GetActionMoveInputFrameSnapshot(
			FrameSequence.LastCapturedInputFrame, InputSnapshot))
		{
			AddError(TEXT("the exact input sample for Correction2 could not be read"));
			break;
		}
		H.Character->SetActorRotation(FRotator(0.0f,
			InputSnapshot.WorldDirection.Rotation().Yaw - 180.0f, 0.0f));
		H.CMC->PerformMovement(1.0f / 60.0f);
		H.Ability->GetRuntimeTelemetrySnapshot(Snapshot);
		if (Snapshot.Phase == EDescendingThrustPhase::GroundOver)
		{
			break;
		}
	}
	H.Ability->GetRuntimeTelemetrySnapshot(Snapshot);
	if (!TestTrue(TEXT("real CMC contact enters Over for Correction2 boundary"),
		Snapshot.Phase == EDescendingThrustPhase::GroundOver))
	{
		H.Teardown();
		return false;
	}
	TestTrue(TEXT("Correction2 accepts the exact 180 degree landing N-1 input"),
		Snapshot.bCorrectionConsumed[1] && Snapshot.bCorrectionSnapshotValid[1]
			&& Snapshot.bCorrectionApplied[1]
			&& Snapshot.CorrectionFrameNumbers[1]
				== FrameSequence.LastCapturedInputFrame
			&& FMath::IsNearlyEqual(
				FMath::Abs(Snapshot.CorrectionAngles[1]), 180.0f, 0.5f));

	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM2DescendingThrustAirCancellationTest,
	"MHGZ.M2.DescendingThrust.AirDeathCancellationRestoresSourceAndReleasesLeases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM2DescendingThrustAirCancellationTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FDescendingThrustLifecycleHarness H;
	if (!TestTrue(*FString::Printf(TEXT("air-cancel harness builds (%s)"),
		*H.FailureStage), H.Build()))
	{
		H.Teardown();
		return false;
	}
	FScopedDescendingThrustLifecycleTestConfiguration TestConfiguration;
	const FName RootSocket = H.Character->GetMesh()->DoesSocketExist(TEXT("Root"))
		? FName(TEXT("Root")) : FName(TEXT("root"));
	if (!TestTrue(TEXT("the demo skeleton exposes a root trace socket"),
		H.Character->GetMesh()->DoesSocketExist(RootSocket))
		|| !TestTrue(TEXT("air-cancel transient lifecycle fixtures build"),
			TestConfiguration.Configure(RootSocket))
		|| !TestTrue(*FString::Printf(TEXT("a supported prior aerial source is published (%s)"),
			*H.FailureStage), H.PublishPriorAerialSource()))
	{
		H.Teardown();
		return false;
	}
	const FGameplayTag PriorSource = H.Host->GetAerialMovementSource();
	if (!TestTrue(*FString::Printf(TEXT("production GA activates before air cancellation (%s)"),
		*H.FailureStage), H.Activate()))
	{
		H.Teardown();
		return false;
	}

	FMHGZDescendingThrustTelemetrySnapshot Snapshot;
	H.Ability->GetRuntimeTelemetrySnapshot(Snapshot);
	TestEqual(TEXT("cancellation fixture begins in AirStart"), Snapshot.Phase,
		EDescendingThrustPhase::AirStart);
	const FWeaponActionToken ActionToken = Snapshot.ActionToken;
	const int32 MainMontageID = Snapshot.MainMontageInstanceID;
	H.ASC->AddLooseGameplayTag(DescLifecycleTag(TEXT("Combat.State.Dead")));
	H.Ability->GetRuntimeTelemetrySnapshot(Snapshot);
	TestFalse(TEXT("adding Death cancels the active production GA in air"),
		H.Ability->IsActive());
	TestEqual(TEXT("Death cancellation reaches the terminal phase"), Snapshot.Phase,
		EDescendingThrustPhase::Ended);
	TestEqual(TEXT("Death cancellation preserves the exact end reason"),
		H.Ability->GetActionEndReason(), EWeaponActionEndReason::Death);
	TestFalse(TEXT("Death cancellation releases recorded movement"),
		Snapshot.bRecordedTrajectoryActive);
	TestFalse(TEXT("Death cancellation releases the Host movement lease"),
		H.Host->IsActionMovementOwnedBy(ActionToken));
	TestFalse(TEXT("Death cancellation unregisters the Main Montage instance"),
		IsRegisteredForAction(H.Host, H.Character->GetMesh(), MainMontageID,
			ActionToken));
	TestEqual(TEXT("airborne Death cancellation restores the prior named aerial source"),
		H.Host->GetAerialMovementSource(), PriorSource);
	TestTrue(TEXT("airborne Death cancellation restores the Falling state"),
		H.ASC->HasMatchingGameplayTag(DescLifecycleTag(
			TEXT("Combat.State.Aerial.Falling"))));
	TestFalse(TEXT("Death cancellation leaves no registered outgoing action"),
		H.Host->HasRegisteredAction());
	TestEqual(TEXT("Death cancellation leaves CMC in normal Falling"),
		H.CMC->MovementMode, MOVE_Falling);
	H.Ability->HandleMontageEvent(ActionToken, MainMontageID,
		EDescendingThrustMontageEvent::EnterFallingLoop);
	H.Ability->GetRuntimeTelemetrySnapshot(Snapshot);
	TestEqual(TEXT("late Main Notify cannot revive a death-cancelled action"),
		Snapshot.Phase, EDescendingThrustPhase::Ended);

	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM2DescendingThrustRuntimeShutdownTest,
	"MHGZ.M2.DescendingThrust.RuntimeShutdownRetiresAbilityAndMovement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM2DescendingThrustRuntimeShutdownTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FDescendingThrustLifecycleHarness H;
	if (!TestTrue(*FString::Printf(TEXT("runtime-shutdown harness builds (%s)"),
		*H.FailureStage), H.Build()))
	{
		H.Teardown();
		return false;
	}
	FScopedDescendingThrustLifecycleTestConfiguration TestConfiguration;
	const FName RootSocket = H.Character->GetMesh()->DoesSocketExist(TEXT("Root"))
		? FName(TEXT("Root")) : FName(TEXT("root"));
	if (!TestTrue(TEXT("the demo skeleton exposes a root trace socket"),
		H.Character->GetMesh()->DoesSocketExist(RootSocket))
		|| !TestTrue(TEXT("runtime-shutdown transient lifecycle fixtures build"),
			TestConfiguration.Configure(RootSocket))
		|| !TestTrue(*FString::Printf(TEXT("a supported prior aerial source is published (%s)"),
			*H.FailureStage), H.PublishPriorAerialSource())
		|| !TestTrue(*FString::Printf(TEXT("production GA activates before runtime shutdown (%s)"),
			*H.FailureStage), H.Activate()))
	{
		H.Teardown();
		return false;
	}

	FMHGZDescendingThrustTelemetrySnapshot Snapshot;
	H.Ability->GetRuntimeTelemetrySnapshot(Snapshot);
	const FWeaponActionToken ActionToken = Snapshot.ActionToken;
	const int32 MainMontageID = Snapshot.MainMontageInstanceID;
	TestTrue(TEXT("runtime shutdown starts while the action owns recorded movement"),
		H.Ability->IsActive() && Snapshot.bRecordedTrajectoryActive
			&& H.Host->IsActionMovementOwnedBy(ActionToken));
	H.Host->ShutdownRuntime(EWeaponRuntimeEndReason::RuntimeShutdown);
	H.Ability->GetRuntimeTelemetrySnapshot(Snapshot);
	TestFalse(TEXT("runtime shutdown ends the production GA"), H.Ability->IsActive());
	TestEqual(TEXT("runtime shutdown leaves the GA terminal"), Snapshot.Phase,
		EDescendingThrustPhase::Ended);
	TestFalse(TEXT("runtime shutdown retires recorded trajectory"),
		Snapshot.bRecordedTrajectoryActive);
	TestFalse(TEXT("runtime shutdown releases action movement ownership"),
		H.Host->IsActionMovementOwnedBy(ActionToken));
	TestFalse(TEXT("runtime shutdown clears registered outgoing actions"),
		H.Host->HasRegisteredAction());
	TestFalse(TEXT("runtime shutdown clears Main Montage registration"),
		IsRegisteredForAction(H.Host, H.Character->GetMesh(), MainMontageID,
			ActionToken));
	TestTrue(TEXT("runtime shutdown leaves CMC out of action-owned Flying"),
		H.CMC->MovementMode != MOVE_Flying);

	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM2DescendingThrustWeaponChangeTest,
	"MHGZ.M2.DescendingThrust.WeaponChangeCancelsAndRebuildsRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM2DescendingThrustWeaponChangeTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FDescendingThrustLifecycleHarness H;
	if (!TestTrue(*FString::Printf(TEXT("weapon-change harness builds (%s)"),
		*H.FailureStage), H.Build()))
	{
		H.Teardown();
		return false;
	}
	FScopedDescendingThrustLifecycleTestConfiguration TestConfiguration;
	const FName RootSocket = H.Character->GetMesh()->DoesSocketExist(TEXT("Root"))
		? FName(TEXT("Root")) : FName(TEXT("root"));
	if (!TestTrue(TEXT("the demo skeleton exposes a root trace socket"),
		H.Character->GetMesh()->DoesSocketExist(RootSocket))
		|| !TestTrue(TEXT("weapon-change transient lifecycle fixtures build"),
			TestConfiguration.Configure(RootSocket))
		|| !TestTrue(*FString::Printf(TEXT("a supported prior aerial source is published (%s)"),
			*H.FailureStage), H.PublishPriorAerialSource())
		|| !TestTrue(*FString::Printf(TEXT("production GA activates before equipment replacement (%s)"),
			*H.FailureStage), H.Activate()))
	{
		H.Teardown();
		return false;
	}

	FMHGZDescendingThrustTelemetrySnapshot Snapshot;
	H.Ability->GetRuntimeTelemetrySnapshot(Snapshot);
	const FWeaponRuntimeToken OldRuntimeToken = H.Host->GetCurrentToken();
	const FWeaponActionToken OldActionToken = Snapshot.ActionToken;
	TWeakObjectPtr<URes_InsectGlaive> OldResource = H.Resource;
	TestTrue(TEXT("replacement begins while production GA owns movement and its source"),
		H.Ability->IsActive() && Snapshot.bRecordedTrajectoryActive
			&& H.Host->GetAerialMovementSource()
			== DescLifecycleTag(TEXT("Combat.State.Aerial.Source.IG_DescendingThrust")));

	UMHGZWeaponDefinition* ReplacementDefinition =
		NewObject<UMHGZWeaponDefinition>(H.PlayerState);
	UWeaponRuntimeDefinition* ReplacementRuntimeDefinition =
		NewObject<UWeaponRuntimeDefinition>(H.PlayerState);
	UMHGZEquipmentInstance* ReplacementInstance = nullptr;
	const FGameplayTag WeaponSlot =
		DescLifecycleTag(TEXT("Equipment.Slot.Weapon"));
	if (ReplacementDefinition && ReplacementRuntimeDefinition)
	{
		ReplacementDefinition->ItemID = TEXT("M2DescendingThrustReplacementWeapon");
		ReplacementDefinition->RuntimeDefinition = ReplacementRuntimeDefinition;
		ReplacementRuntimeDefinition->ResourceComponentClass =
			URes_InsectGlaive::StaticClass();
		ReplacementRuntimeDefinition->CombatConfig = H.CombatConfig;
		ReplacementInstance = UMHGZEquipmentInstance::CreateEquipmentInstance(
			H.PlayerState, ReplacementDefinition);
	}
	if (!TestTrue(TEXT("replacement weapon definition and instance are valid"),
		WeaponSlot.IsValid() && ReplacementInstance))
	{
		H.Teardown();
		return false;
	}

	H.Equipment->EquipItem(WeaponSlot, ReplacementInstance);
	H.Ability->GetRuntimeTelemetrySnapshot(Snapshot);
	TestFalse(TEXT("equipment identity change cancels the old production GA"),
		H.Ability->IsActive());
	TestEqual(TEXT("equipment identity change leaves the old GA terminal"),
		Snapshot.Phase, EDescendingThrustPhase::Ended);
	TestFalse(TEXT("equipment replacement retires the old runtime token"),
		H.Host->IsTokenCurrent(OldRuntimeToken));
	TestTrue(TEXT("equipment replacement builds a new current runtime"),
		H.Host->IsRuntimeInitialized()
			&& H.Host->GetCurrentToken().IsValid()
			&& H.Host->GetCurrentToken() != OldRuntimeToken);
	TestFalse(TEXT("equipment replacement retires the old movement lease"),
		Snapshot.bRecordedTrajectoryActive
			|| H.Host->IsActionMovementOwnedBy(OldActionToken));
	TestFalse(TEXT("equipment replacement removes the old named aerial source"),
		H.Host->GetAerialMovementSource().IsValid()
			|| H.ASC->HasMatchingGameplayTag(DescLifecycleTag(
				TEXT("Combat.State.Aerial.Source.IG_DescendingThrust"))));
	TestTrue(TEXT("replacement resource provider is live and not the retired component"),
		H.Host->GetResourceProvider()
			&& H.Host->GetResourceProvider() != OldResource.Get());
	TestTrue(TEXT("equipment replacement leaves CMC out of action-owned Flying"),
		H.CMC->MovementMode != MOVE_Flying);

	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM2DescendingThrustDamageFailureTest,
	"MHGZ.M2.DescendingThrust.DamageFailureDoesNotRecordSuccess",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM2DescendingThrustDamageFailureTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FDescendingThrustLifecycleHarness H;
	if (!TestTrue(*FString::Printf(TEXT("damage-failure harness builds (%s)"),
		*H.FailureStage), H.Build()))
	{
		H.Teardown();
		return false;
	}
	FScopedDescendingThrustLifecycleTestConfiguration TestConfiguration;
	const FName RootSocket = H.Character->GetMesh()->DoesSocketExist(TEXT("Root"))
		? FName(TEXT("Root")) : FName(TEXT("root"));
	if (!TestTrue(TEXT("the demo skeleton exposes a root trace socket"),
		H.Character->GetMesh()->DoesSocketExist(RootSocket))
		|| !TestTrue(TEXT("damage-failure transient lifecycle fixtures build"),
			TestConfiguration.Configure(RootSocket))
		|| !TestTrue(*FString::Printf(TEXT("real target setup succeeds (%s)"),
			*H.FailureStage), H.SpawnDamageTarget())
		|| !TestTrue(*FString::Printf(TEXT("a supported prior aerial source is published (%s)"),
			*H.FailureStage), H.PublishPriorAerialSource())
		|| !TestTrue(*FString::Printf(TEXT("production GA activates before damage failure (%s)"),
			*H.FailureStage), H.Activate()))
	{
		H.Teardown();
		return false;
	}

	const float HealthBeforeAttempt = H.TargetAttributes->GetHealth();
	int32 AppliedEffectCount = 0;
	const FDelegateHandle AppliedDamageDelegate =
		H.TargetASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
			[&AppliedEffectCount](UAbilitySystemComponent*,
				const FGameplayEffectSpec&, FActiveGameplayEffectHandle)
			{
				++AppliedEffectCount;
			});
	H.Ability->AttackSegments[0].Damage.DamageEffectClass = nullptr;
	H.Ability->EnableCollision(0);
	H.Ability->TickCollision(0, 1.0f / 60.0f);
	H.Ability->DisableCollision(0);

	// A valid spec must also fail cleanly when the target ASC rejects application.
	H.Ability->AttackSegments[0].Damage.DamageEffectClass =
		UMHGZDamageGameplayEffect::StaticClass();
	UMHGZM2RejectingGameplayEffectASC* RejectingASC =
		Cast<UMHGZM2RejectingGameplayEffectASC>(H.TargetASC);
	if (!TestNotNull(TEXT("damage-failure target uses the rejecting test ASC"), RejectingASC))
	{
		H.TargetASC->OnGameplayEffectAppliedDelegateToSelf.Remove(AppliedDamageDelegate);
		H.Teardown();
		return false;
	}
	RejectingASC->bRejectIncomingGameplayEffects = true;
	H.Ability->EnableCollision(0);
	H.Ability->TickCollision(0, 1.0f / 60.0f);
	H.Ability->DisableCollision(0);
	H.TargetASC->OnGameplayEffectAppliedDelegateToSelf.Remove(AppliedDamageDelegate);

	FMHGZDescendingThrustTelemetrySnapshot Snapshot;
	H.Ability->GetRuntimeTelemetrySnapshot(Snapshot);
	TestEqual(TEXT("a contact with no constructible damage GE records no successful target"),
		Snapshot.SuccessfulTargetCounts[0], 0);
	TestEqual(TEXT("failed damage does not advance last successful segment"),
		Snapshot.LastSuccessfulSegmentIndex, INDEX_NONE);
	TestTrue(TEXT("failed damage leaves target health unchanged"),
		FMath::IsNearlyEqual(H.TargetAttributes->GetHealth(), HealthBeforeAttempt));
	TestEqual(TEXT("failed damage emits no target-side GameplayEffect application"),
		AppliedEffectCount, 0);
	TestEqual(TEXT("the target ASC explicitly rejected a valid incoming GE"),
		RejectingASC->RejectedIncomingGameplayEffectCount, 1);

	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM2DescendingThrustSupersedeTest,
	"MHGZ.M2.DescendingThrust.SuccessorActionOwnsSourceAfterDodgeHandoff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM2DescendingThrustSupersedeTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FDescendingThrustLifecycleHarness H;
	if (!TestTrue(*FString::Printf(TEXT("supersede harness builds (%s)"),
		*H.FailureStage), H.Build()))
	{
		H.Teardown();
		return false;
	}
	FScopedDescendingThrustLifecycleTestConfiguration TestConfiguration;
	const FName RootSocket = H.Character->GetMesh()->DoesSocketExist(TEXT("Root"))
		? FName(TEXT("Root")) : FName(TEXT("root"));
	if (!TestTrue(TEXT("the demo skeleton exposes a root trace socket"),
		H.Character->GetMesh()->DoesSocketExist(RootSocket))
		|| !TestTrue(TEXT("supersede transient lifecycle fixtures build"),
			TestConfiguration.Configure(RootSocket))
		|| !TestTrue(*FString::Printf(TEXT("a supported prior aerial source is published (%s)"),
			*H.FailureStage), H.PublishPriorAerialSource())
		|| !TestTrue(*FString::Printf(TEXT("production GA activates before handoff (%s)"),
			*H.FailureStage), H.Activate()))
	{
		H.Teardown();
		return false;
	}

	FMHGZDescendingThrustTelemetrySnapshot Snapshot;
	H.Ability->GetRuntimeTelemetrySnapshot(Snapshot);
	const FWeaponActionToken OldActionToken = Snapshot.ActionToken;
	const int32 OldMontageID = Snapshot.MainMontageInstanceID;
	const FName DodgeWindowID(TEXT("M2DescendingThrustDodgeAccept"));
	TestTrue(TEXT("the active Descending Thrust opens its exact Dodge handoff window"),
		H.Ability->BeginDodgeAcceptWindow(DodgeWindowID));

	const FGameplayAbilitySpecHandle DodgeHandle = H.ASC->GiveAbility(
		FGameplayAbilitySpec(UMHGZM5KinsectSlashMovementTestAbility::StaticClass(),
			1, INDEX_NONE, H.ASC));
	UMHGZM5KinsectSlashMovementTestAbility* DodgeInstance =
		NewObject<UMHGZM5KinsectSlashMovementTestAbility>(H.ASC);
	FWeaponActionToken DodgeActionToken;
	DodgeActionToken.RuntimeToken = H.Host->GetCurrentToken();
	DodgeActionToken.AbilityHandle = DodgeHandle;
	DodgeActionToken.ActivationSequenceID = H.Host->AllocateActivationSequenceID();
	DodgeActionToken.AbilityInstance = DodgeInstance;
	const FGameplayTag DodgeSource =
		DescLifecycleTag(TEXT("Combat.State.Aerial.Source.IG_AirDodge"));
	if (!TestTrue(TEXT("successor action token is complete and registers with Host"),
		DodgeHandle.IsValid() && DodgeInstance && DodgeActionToken.IsValid()
			&& H.Host->RegisterAction(DodgeActionToken))
		|| !TestTrue(TEXT("successor publishes its source before ending the old action"),
			H.Host->SetAerialMovementSource(DodgeActionToken, DodgeSource)))
	{
		H.Teardown();
		return false;
	}

	TestTrue(TEXT("the active GA accepts the exact successor token"),
		H.Ability->PrepareDodgeSupersede(DodgeActionToken));
	TestTrue(TEXT("the exact Dodge successor commits takeover"),
		H.Ability->CommitDodgeSupersede(DodgeActionToken));
	H.Ability->GetRuntimeTelemetrySnapshot(Snapshot);
	TestFalse(TEXT("successful handoff ends Descending Thrust"), H.Ability->IsActive());
	TestEqual(TEXT("successful handoff classifies the outgoing GA as Superseded"),
		H.Ability->GetActionEndReason(), EWeaponActionEndReason::Superseded);
	TestEqual(TEXT("the outgoing lifecycle is terminal"), Snapshot.Phase,
		EDescendingThrustPhase::Ended);
	TestTrue(TEXT("the successor remains registered and owns the aerial source"),
		H.Host->HasRegisteredAction()
			&& H.Host->GetAerialMovementSource() == DodgeSource);
	TestFalse(TEXT("the outgoing movement lease is retired without taking the successor"),
		Snapshot.bRecordedTrajectoryActive
			|| H.Host->IsActionMovementOwnedBy(OldActionToken));
	TestFalse(TEXT("the outgoing Main Montage registration is retired"),
		IsRegisteredForAction(H.Host, H.Character->GetMesh(), OldMontageID,
			OldActionToken));
	TestTrue(TEXT("the successor source tag remains granted"),
		H.ASC->HasMatchingGameplayTag(DodgeSource));
	H.Host->UnregisterAction(DodgeActionToken);

	H.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM2DescendingThrustReleaseDurationBoundaryTest,
	"MHGZ.M2.DescendingThrust.ReleaseFallbackQuarterSecondBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM2DescendingThrustReleaseDurationBoundaryTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	auto RunDurationCase = [this](const float Duration, const bool bShouldActivate)
	{
		FDescendingThrustLifecycleHarness H;
		if (!TestTrue(*FString::Printf(TEXT("duration %.3f harness builds (%s)"),
			Duration, *H.FailureStage), H.Build()))
		{
			H.Teardown();
			return false;
		}
		FScopedDescendingThrustLifecycleTestConfiguration TestConfiguration;
		const FName RootSocket = H.Character->GetMesh()->DoesSocketExist(TEXT("Root"))
			? FName(TEXT("Root")) : FName(TEXT("root"));
		if (!TestTrue(TEXT("the demo skeleton exposes a root trace socket"),
			H.Character->GetMesh()->DoesSocketExist(RootSocket))
			|| !TestTrue(TEXT("duration boundary fixtures build"),
				TestConfiguration.Configure(RootSocket))
			|| !TestTrue(*FString::Printf(TEXT("supported aerial source publishes (%s)"),
				*H.FailureStage), H.PublishPriorAerialSource()))
		{
			H.Teardown();
			return false;
		}

		H.AbilityHandle = H.ASC->GiveAbility(FGameplayAbilitySpec(
			UMHGZM2DescendingThrustLifecycleTestAbility::StaticClass(),
			1, INDEX_NONE, H.ASC));
		UGA_WeaponComboCoordinator* Coordinator = H.ASC->GetActiveComboCoordinator();
		if (Coordinator && H.CombatConfig && H.CombatConfig->ComboData)
		{
			FComboTransition& RTTransition =
				H.CombatConfig->ComboData->Transitions.AddDefaulted_GetRef();
			RTTransition.TransitionID = TEXT("M2_ReleaseFallbackToDescendingThrust");
			RTTransition.SourceState = TEXT("Idle");
			RTTransition.InputTag = DescLifecycleTag(TEXT("Input.Weapon.RT"));
			RTTransition.AbilityClass =
				UMHGZM2DescendingThrustLifecycleTestAbility::StaticClass();
			RTTransition.TargetState = TEXT("DescendingThrustTest");
			Coordinator->InjectComboData(H.CombatConfig->ComboData);
		}
		UMHGZWeaponInputRouterComponent* Router =
			NewObject<UMHGZWeaponInputRouterComponent>(H.Character);
		UWeaponInputProfile* Profile = NewObject<UWeaponInputProfile>(H.Character);
		if (!TestTrue(TEXT("the production test ability is granted for routed activation"),
			H.AbilityHandle.IsValid())
			|| !TestNotNull(TEXT("the production combo coordinator is active"), Coordinator)
			|| !TestNotNull(TEXT("the input router is created"), Router)
			|| !TestNotNull(TEXT("the transient RT profile is created"), Profile))
		{
			H.Teardown();
			return false;
		}
		Profile->ChordGracePeriod = 0.05f;
		FWeaponChordDefinition& RTReleaseChord = Profile->Chords.AddDefaulted_GetRef();
		RTReleaseChord.OutputTag = DescLifecycleTag(TEXT("Input.Weapon.RT"));
		RTReleaseChord.TriggerControls.Add(
			DescLifecycleTag(TEXT("Input.Modifier.RT")));
		RTReleaseChord.DispatchPolicy =
			EWeaponChordDispatchPolicy::OnReleaseWithinMaxDurationIfUnconsumed;
		RTReleaseChord.MaxPressDurationSeconds = 0.25f;
		Router->AttachToPawn(H.Character);
		Router->SetInputProfile(Profile);
		const FGameplayTag PhysicalRT =
			DescLifecycleTag(TEXT("Input.Modifier.RT"));
		{
			FScopedPreviousMoveSnapshotFrame SnapshotFrame(H.Character);
			Router->HandlePhysicalStarted(PhysicalRT, 1.0);
			Router->HandlePhysicalCompleted(PhysicalRT, 1.0 + Duration);
		}
		FGameplayAbilitySpec* AbilitySpec =
			H.ASC->FindAbilitySpecFromHandle(H.AbilityHandle);
		if (AbilitySpec)
		{
			for (UGameplayAbility* Instance : AbilitySpec->GetAbilityInstances())
			{
				if (UMHGZDescendingThrustAbility* DescendingThrust =
					Cast<UMHGZDescendingThrustAbility>(Instance))
				{
					H.Ability = DescendingThrust;
					break;
				}
			}
		}
		const bool bActivated = H.Ability && H.Ability->IsActive()
			&& H.Ability->IsActionActivationCommitted();
		TestEqual(*FString::Printf(TEXT("physical RT duration %.3f activation expectation"),
			Duration), bActivated, bShouldActivate);
		if (bShouldActivate && H.Ability)
		{
			FMHGZDescendingThrustTelemetrySnapshot Snapshot;
			H.Ability->GetRuntimeTelemetrySnapshot(Snapshot);
			const TArray<FWeaponInputSnapshot>& RoutedInputs =
				Router->GetCapturedSnapshots();
			TestEqual(TEXT("one RT release-fallback snapshot reaches the ASC"),
				RoutedInputs.Num(), 1);
			if (RoutedInputs.Num() == 1)
			{
				TestEqual(TEXT("the router resolves physical RT to Input.Weapon.RT"),
					RoutedInputs[0].ResolvedInputTag,
					DescLifecycleTag(TEXT("Input.Weapon.RT")));
				TestEqual(TEXT("the router preserves the exact press duration"),
					RoutedInputs[0].PhysicalPressDurationSeconds, Duration);
			}
			TestTrue(TEXT("the inclusive 0.25s boundary remains a committed live action"),
				H.Ability && H.Ability->IsActive()
					&& H.Ability->IsActionActivationCommitted()
					&& Snapshot.bInputReleaseFallback
					&& Snapshot.InputTag == DescLifecycleTag(TEXT("Input.Weapon.RT"))
					&& FMath::IsNearlyEqual(
						Snapshot.PhysicalPressDurationSeconds, Duration, 0.001f));
		}
		else if (bShouldActivate)
		{
			AddError(TEXT("RT release fallback was expected to activate Descending Thrust, but no active GA instance was found."));
		}
		else
		{
			TestEqual(TEXT("overlong RT release emits no fallback snapshot"),
				Router->GetCapturedSnapshots().Num(), 0);
			TestFalse(TEXT("a hold beyond 0.25s cannot leave a live GA"),
				H.Ability && H.Ability->IsActive());
			TestFalse(TEXT("a rejected long hold never publishes the action source"),
				H.Host->GetAerialMovementSource()
					== DescLifecycleTag(
						TEXT("Combat.State.Aerial.Source.IG_DescendingThrust")));
			TestFalse(TEXT("a rejected long hold never registers an active action"),
				H.Host->HasRegisteredAction());
		}
		Router->ShutdownRouter();
		H.Teardown();
		return true;
	};

	const bool bBoundaryAccepted = RunDurationCase(0.25f, true);
	const bool bLongHoldRejected = RunDurationCase(0.251f, false);
	return bBoundaryAccepted && bLongHoldRejected;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMHGZM2DescendingThrustCantAttackGateTest,
	"MHGZ.M2.DescendingThrust.CantAttackProtectsAerialEntry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMHGZM2DescendingThrustCantAttackGateTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FDescendingThrustLifecycleHarness H;
	if (!TestTrue(*FString::Printf(TEXT("CantAttack harness builds (%s)"),
		*H.FailureStage), H.Build()))
	{
		H.Teardown();
		return false;
	}
	FScopedDescendingThrustLifecycleTestConfiguration TestConfiguration;
	const FName RootSocket = H.Character->GetMesh()->DoesSocketExist(TEXT("Root"))
		? FName(TEXT("Root")) : FName(TEXT("root"));
	if (!TestTrue(TEXT("the demo skeleton exposes a root trace socket"),
		H.Character->GetMesh()->DoesSocketExist(RootSocket))
		|| !TestTrue(TEXT("CantAttack transient lifecycle fixtures build"),
			TestConfiguration.Configure(RootSocket))
		|| !TestTrue(*FString::Printf(TEXT("supported aerial source publishes (%s)"),
			*H.FailureStage), H.PublishPriorAerialSource()))
	{
		H.Teardown();
		return false;
	}

	const FGameplayTag CantAttack =
		DescLifecycleTag(TEXT("Combat.State.Aerial.CantAttack"));
	H.ASC->AddLooseGameplayTag(CantAttack);
	TestFalse(TEXT("CantAttack denies activation even with a valid aerial source"),
		H.Activate());
	TestTrue(TEXT("the failure is the GAS activation tag gate"),
		H.FailureStage.Contains(TEXT("GAS CanActivate denied")));
	TestTrue(TEXT("rejection preserves the prior source and registers no new action"),
		H.Host->GetAerialMovementSource()
			== DescLifecycleTag(
				TEXT("Combat.State.Aerial.Source.IG_KinsectSlash.Hit"))
			&& !H.Host->HasRegisteredAction());
	H.ASC->RemoveLooseGameplayTag(CantAttack);

	H.Teardown();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
