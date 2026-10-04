// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZM5KinsectSlashDamageTestTypes.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "ActionSystem/MHGZDamageGameplayEffect.h"
#include "ActionSystem/MHGZWeaponMovementCurves.h"
#include "ActionSystem/MHGZWeaponComboData.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "AttributeSystem/Res_InsectGlaive.h"
#include "Components/SkeletalMeshComponent.h"
#include "Curves/CurveFloat.h"
#include "Curves/RichCurve.h"
#include "Engine/World.h"
#include "Equipment/MHGZEquipmentComponent.h"
#include "Equipment/MHGZEquipmentDefinition.h"
#include "Equipment/MHGZEquipmentInstance.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "InsectGlaive/InsectGlaiveCombatConfig.h"
#include "InsectGlaive/Kinsect/InsectGlaiveKinsectData.h"
#include "InsectGlaive/Kinsect/Kinsect.h"
#include "MHGZPlayerState.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"
#include "WeaponRuntime/MHGZWeaponRuntimeDefinition.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

namespace
{
FGameplayTag DamageGateTag(const TCHAR* Name)
{
	return FGameplayTag::RequestGameplayTag(FName(Name));
}

UCurveFloat* MakeDamageGateCurve(UObject* Outer)
{
	UCurveFloat* Curve = NewObject<UCurveFloat>(Outer);
	if (!Curve)
	{
		return nullptr;
	}

	const FKeyHandle Start = Curve->FloatCurve.AddKey(0.0f, 0.0f);
	Curve->FloatCurve.GetKey(Start).InterpMode = RCIM_Linear;
	const FKeyHandle End = Curve->FloatCurve.AddKey(1.0f, 1.0f);
	Curve->FloatCurve.GetKey(End).InterpMode = RCIM_Linear;
	return Curve;
}
}

UMHGZM5KinsectSlashDamageRedExtractGE::UMHGZM5KinsectSlashDamageRedExtractGE()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(60.0f));
	UTargetTagsGameplayEffectComponent* TagsComponent =
		CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("TargetTags"));
	GEComponents.Add(TagsComponent);

	FInheritedTagContainer GrantedTags;
	GrantedTags.AddTag(DamageGateTag(TEXT("WeaponResource.IG.Extract.Red")));
	TagsComponent->SetAndApplyTargetTagChanges(GrantedTags);
}

bool UMHGZM5KinsectSlashDamageTestCombatConfig::ResolveAerialFallingPhysics(
	const bool bEnhancedVariant, float& OutGravityScale,
	float& OutBrakingDecelerationFalling) const
{
	(void)bEnhancedVariant;
	OutGravityScale = 2.0f;
	OutBrakingDecelerationFalling = 0.0f;
	return true;
}

UMHGZM5KinsectSlashDamageTestAbility::UMHGZM5KinsectSlashDamageTestAbility()
{
	StaminaCostPolicy = EAbilityStaminaCostPolicy::None;
}

void UMHGZM5KinsectSlashDamageTestAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// Per-execution instances are created from the production CDO when the weapon
	// grants the ability. Configure this instance before the inherited preflight.
	ConfigureForTest();
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}

void UMHGZM5KinsectSlashDamageTestAbility::ConfigureForTest()
{
	AttackSegments.Reset();
	FAttackSegmentConfig& Segment = AttackSegments.AddDefaulted_GetRef();
	Segment.Collision.HitzoneQueryTag = DamageGateTag(TEXT("Hitzone.Head"));
	Segment.Damage.DamageEffectClass = UMHGZDamageGameplayEffect::StaticClass();
	Segment.Damage.MotionValue = 1.0f;
	Segment.Damage.BaseStaggerValue = 10.0f;
	Segment.MultiHitCount = 1;
	Segment.MultiHitPolicy = EAttackMultiHitPolicy::ContactOnly;
	for (int32 SegmentIndex = 1; SegmentIndex < 3; ++SegmentIndex)
	{
		FAttackSegmentConfig& WeaponSegment = AttackSegments.AddDefaulted_GetRef();
		WeaponSegment.Damage.DamageEffectClass = UMHGZDamageGameplayEffect::StaticClass();
		WeaponSegment.Damage.MotionValue = 1.0f;
		WeaponSegment.MultiHitCount = 1;
		WeaponSegment.MultiHitPolicy = EAttackMultiHitPolicy::ContactOnly;
		FWeaponTraceRegion& Region = WeaponSegment.Collision.TraceRegions.AddDefaulted_GetRef();
		Region.StartSocketName = TEXT("Root");
		Region.EndSocketName = TEXT("IG_FrontTip");
	}

	AttackMontage = NewObject<UAnimMontage>(this);
	DefaultEntrySection = NAME_None;

	DanceFromDashTuning.ParameterMode = EBallisticParameterMode::ApexHeightAndDuration;
	DanceFromDashTuning.ApexHeight = 100.0f;
	DanceFromDashTuning.Duration = 0.5f;
	DanceFromDashTuning.HorizontalDistance = 100.0f;
	DanceFromDashTuning.ExplicitLaunchVelocity = FVector::ZeroVector;
	DanceFromMissTuning = DanceFromDashTuning;
}

void UMHGZM5KinsectSlashDamageTestAbility::ResetTestConfiguration()
{
	AttackSegments.Reset();
	AttackMontage = nullptr;
	DefaultEntrySection = HoverSectionName;
	DanceFromDashTuning = FKinsectSlashDanceEntryTuning();
	DanceFromMissTuning = FKinsectSlashDanceEntryTuning();
}

bool UMHGZM5KinsectSlashDamageTestAbility::ValidateActionDependencies() const
{
	const FWeaponInputSnapshot& Input = GetWeaponActivationContext().Input;
	const bool bValidInput = Input.ResolvedInputTag.MatchesTagExact(
		DamageGateTag(TEXT("Input.Weapon.B")))
		|| Input.ResolvedInputTag.MatchesTagExact(DamageGateTag(TEXT("Input.Weapon.RTB")));
	const UMHGZAbilitySystemComponent* MHGZASC = Cast<UMHGZAbilitySystemComponent>(
		GetAbilitySystemComponentFromActorInfo());
	const UMHGZWeaponRuntimeHostComponent* RuntimeHost = MHGZASC
		? MHGZASC->GetRuntimeHost() : nullptr;
	const URes_InsectGlaive* IGResource = GetIGResourceComponent();
	const UInsectGlaiveCombatConfig* Config = IGResource
		? IGResource->GetCombatConfig() : nullptr;
	const ACharacter* Avatar = Cast<ACharacter>(GetAvatarActorFromActorInfo());

	const bool bFalling = MHGZASC && MHGZASC->HasMatchingGameplayTag(
		DamageGateTag(TEXT("Combat.State.Aerial.Falling")));
	const bool bUnsheathed = MHGZASC && MHGZASC->HasMatchingGameplayTag(
		DamageGateTag(TEXT("Combat.State.Unsheathed")));
	const bool bNoDisqualifyingState = MHGZASC
		&& !MHGZASC->HasMatchingGameplayTag(DamageGateTag(TEXT("Combat.State.Aerial.CantAttack")))
		&& !MHGZASC->HasMatchingGameplayTag(DamageGateTag(TEXT("Combat.State.Dead")))
		&& !MHGZASC->HasMatchingGameplayTag(DamageGateTag(TEXT("Combat.State.Hitstun")))
		&& !MHGZASC->HasMatchingGameplayTag(DamageGateTag(TEXT("Combat.State.Knockdown")));
	const bool bResourceReady = IGResource && RuntimeHost
		&& RuntimeHost->GetResourceProvider() == IGResource
		&& IGResource->GetKinsectActor() && IGResource->GetKinsectActor()->Mesh;
	const bool bConfigReady = Config && Config->KinsectSlashDistanceCurve
		&& Config->KinsectSlashMaxDistance > 0.0f
		&& Config->KinsectSlashDashDuration > 0.0f
		&& Config->KinsectSlashProbeRadius > 0.0f
		&& Config->KinsectSlashProbeMaxStep > 0.0f
		&& Config->KinsectSlashLateHitWindow >= 0.0f;
	const bool bAttackReady = AttackSegments.Num() == 3
		&& AttackSegments[0].Damage.DamageEffectClass
		&& AttackSegments[0].Damage.MotionValue.GetValueAtLevel(1.0f) > 0.0f
		&& AttackSegments[0].MultiHitCount == 1
		&& AttackSegments[0].MultiHitPolicy == EAttackMultiHitPolicy::ContactOnly
		&& AttackSegments[1].Damage.DamageEffectClass
		&& AttackSegments[1].Collision.TraceRegions.Num() == 1
		&& AttackSegments[2].Damage.DamageEffectClass
		&& AttackSegments[2].Collision.TraceRegions.Num() == 1
		&& AttackMontage;
	const bool bPreflight = bValidInput && RuntimeHost && !RuntimeHost->IsGrounded()
		&& bFalling && bUnsheathed && bNoDisqualifyingState
		&& bResourceReady && Config && Avatar && Avatar->GetMesh() && bConfigReady
		&& DanceFromDashTuning.IsValid() && DanceFromMissTuning.IsValid()
		&& bAttackReady;
	if (!bPreflight)
	{
		return false;
	}

	float TerminalSlope = 0.0f;
	return MHGZWeaponMovementCurves::ValidateNormalizedProgressCurve(
		Config->KinsectSlashDistanceCurve, TerminalSlope);
}

bool UMHGZM5KinsectSlashDamageTestAbility::PrepareAttackMontage()
{
	// A transient empty montage is enough for the common AttackAbility entry path;
	// this test deliberately does not start animation playback.
	return AttackMontage != nullptr;
}

bool UMHGZM5KinsectSlashDamageTestAbility::StartAttackMontage(
	ACharacter& Character, UAnimMontage* Montage, const FName StartSection)
{
	(void)Character;
	(void)StartSection;
	return Montage != nullptr;
}

bool FMHGZM5KinsectSlashDamageTestHarness::Build()
{
	UAbilitySystemGlobals& Globals = UAbilitySystemGlobals::Get();
	if (!Globals.IsAbilitySystemGlobalsInitialized())
	{
		Globals.InitGlobalData();
	}

	World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!World)
	{
		return false;
	}

	Character = World->SpawnActor<AMHGZM5KinsectSlashDamageTestCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	PlayerState = World->SpawnActor<AMHGZPlayerState>(
		AMHGZPlayerState::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
	if (!Character || !PlayerState)
	{
		return false;
	}

	Character->SetPlayerState(PlayerState);
	ASC = PlayerState->GetMHGZAbilitySystemComponent();
	SourceAttributes = PlayerState->GetAttributeSet();
	Equipment = PlayerState->GetEquipmentComponent();
	if (!ASC || !SourceAttributes || !Equipment)
	{
		return false;
	}
	ASC->InitAbilityActorInfo(PlayerState, Character);
	ASC->InitializeAbilitySystem();
	SourceAttributes->InitCriticalRate(0.0f);
	SourceAttributes->InitStaggerMultiplier(1.0f);

	Host = NewObject<UMHGZWeaponRuntimeHostComponent>(Character);
	if (!Host)
	{
		return false;
	}
	Character->AddInstanceComponent(Host);
	Host->RegisterComponent();

	UMHGZWeaponDefinition* WeaponDefinition = NewObject<UMHGZWeaponDefinition>(PlayerState);
	UWeaponRuntimeDefinition* RuntimeDefinition = NewObject<UWeaponRuntimeDefinition>(PlayerState);
	CombatConfig = NewObject<UMHGZM5KinsectSlashDamageTestCombatConfig>(PlayerState);
	UMHGZWeaponComboData* ComboData = NewObject<UMHGZWeaponComboData>(PlayerState);
	UInsectGlaiveKinsectData* KinsectData = NewObject<UInsectGlaiveKinsectData>(PlayerState);
	if (!WeaponDefinition || !RuntimeDefinition || !CombatConfig || !ComboData || !KinsectData)
	{
		return false;
	}

	WeaponDefinition->ItemID = TEXT("M5KinsectSlashDamageGateTransientWeapon");
	WeaponDefinition->RuntimeDefinition = RuntimeDefinition;
	RuntimeDefinition->ResourceComponentClass = URes_InsectGlaive::StaticClass();
	RuntimeDefinition->CombatConfig = CombatConfig;
	CombatConfig->ComboData = ComboData;
	CombatConfig->KinsectData = KinsectData;
	CombatConfig->MaxDanceStacks = 3;
	CombatConfig->KinsectAttachSocket = NAME_None;
	CombatConfig->KinsectMarkLaunchSocket = NAME_None;
	CombatConfig->RedEffectClass = UMHGZM5KinsectSlashDamageRedExtractGE::StaticClass();
	CombatConfig->KinsectSlashDistanceCurve = MakeDamageGateCurve(CombatConfig);
	if (!CombatConfig->KinsectSlashDistanceCurve)
	{
		return false;
	}

	FComboTransition& Transition = ComboData->Transitions.AddDefaulted_GetRef();
	Transition.TransitionID = TEXT("M5KinsectSlashDamageGate");
	Transition.SourceState = TEXT("Idle");
	Transition.TargetState = TEXT("KinsectSlashDamageGate");
	Transition.InputTag = DamageGateTag(TEXT("Input.Weapon.B"));
	Transition.AbilityClass = UMHGZM5KinsectSlashDamageTestAbility::StaticClass();

	Host->InitializePawnRuntime(Character, nullptr, ASC, Equipment);
	const FGameplayTag WeaponSlot = DamageGateTag(TEXT("Equipment.Slot.Weapon"));
	UMHGZEquipmentInstance* Instance = UMHGZEquipmentInstance::CreateEquipmentInstance(
		PlayerState, WeaponDefinition);
	if (!WeaponSlot.IsValid() || !Instance)
	{
		return false;
	}
	Equipment->EquipItem(WeaponSlot, Instance);
	// Equipment recomputes the attribute bases during EquipItem; seed test attack
	// power after that transaction so the real DamageExecCalc captures nonzero AP.
	ASC->SetNumericAttributeBase(UMHGZAttributeSet::GetAttackPowerAttribute(), 100.0f);

	Resource = Character->FindComponentByClass<URes_InsectGlaive>();
	AbilityHandle = ASC->FindWeaponAbilityHandle(
		UMHGZM5KinsectSlashDamageTestAbility::StaticClass());
	if (!Resource || Resource != Host->GetResourceProvider()
		|| !Resource->GetKinsectActor() || !AbilityHandle.IsValid()
		|| !Host->GetCurrentToken().IsValid())
	{
		return false;
	}

	Host->SetGrounded(false);
	Host->AcquireAerialFallingState();
	Host->SetSheathed(false);
	Character->GetCharacterMovement()->SetMovementMode(MOVE_Falling);

	TargetActor = World->SpawnActor<AActor>();
	if (!TargetActor)
	{
		return false;
	}
	TargetASC = NewObject<UAbilitySystemComponent>(TargetActor);
	TargetAttributes = NewObject<UMHGZAttributeSet>(TargetActor);
	Hitzone = NewObject<UMHGZMonsterHitzoneComponent>(TargetActor);
	if (!TargetASC || !TargetAttributes || !Hitzone)
	{
		return false;
	}
	TargetActor->AddInstanceComponent(TargetASC);
	TargetASC->RegisterComponent();
	TargetASC->AddAttributeSetSubobject(TargetAttributes);
	TargetASC->InitAbilityActorInfo(TargetActor, TargetActor);
	TargetAttributes->InitMaxHealth(1000.0f);
	TargetAttributes->InitHealth(1000.0f);
	TargetAttributes->InitDefense(0.0f);

	TargetActor->AddInstanceComponent(Hitzone);
	TargetActor->SetRootComponent(Hitzone);
	Hitzone->SetSphereRadius(30.0f);
	Hitzone->HitzoneTag = DamageGateTag(TEXT("Hitzone.Head"));
	Hitzone->ExtractColorTag = DamageGateTag(TEXT("WeaponResource.IG.Extract.Red"));
	Hitzone->DefenseMultiplier = 1.0f;
	Hitzone->StaggerRate = 1.0f;
	Hitzone->RegisterComponent();

	return Hitzone->HitzoneTag.IsValid() && Hitzone->ExtractColorTag.IsValid();
}

bool FMHGZM5KinsectSlashDamageTestHarness::ActivateAbility(const FGameplayTag ResolvedInputTag)
{
	if (!ASC || !Host || !AbilityHandle.IsValid())
	{
		return false;
	}

	FWeaponAbilityActivationContext Context;
	Context.RuntimeToken = Host->GetCurrentToken();
	Context.ActivationSequenceID = Host->AllocateActivationSequenceID();
	Context.Input.ResolvedInputTag = ResolvedInputTag.IsValid()
		? ResolvedInputTag : DamageGateTag(TEXT("Input.Weapon.B"));
	Context.Input.ActorForward = Character ? Character->GetActorForwardVector()
		: FVector::ForwardVector;
	Context.Input.WorldDirection = Context.Input.ActorForward;
	ASC->PrepareWeaponAbilityActivation(AbilityHandle, Context);

	const bool bActivated = ASC->TryActivateAbility(AbilityHandle);
	if (!bActivated)
	{
		FWeaponAbilityActivationContext DiscardedContext;
		ASC->ConsumePendingActivationContext(AbilityHandle, DiscardedContext);
	}
	if (!bActivated)
	{
		return false;
	}

	FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(AbilityHandle);
	if (!Spec)
	{
		return false;
	}
	for (UGameplayAbility* Instance : Spec->GetAbilityInstances())
	{
		if (UMHGZM5KinsectSlashDamageTestAbility* SlashAbility =
			Cast<UMHGZM5KinsectSlashDamageTestAbility>(Instance))
		{
			Ability = SlashAbility;
			break;
		}
	}
	return Ability && Ability->IsActive() && Ability->IsActionActivationCommitted();
}

void FMHGZM5KinsectSlashDamageTestHarness::Teardown()
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
