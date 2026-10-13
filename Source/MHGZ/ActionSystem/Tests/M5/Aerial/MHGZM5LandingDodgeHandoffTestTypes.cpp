// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZM5LandingDodgeHandoffTestTypes.h"
#include "Animation/AnimMontage.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
class FMHGZM5LandingDodgeHandoffTestAccess
{
public:
	static bool Start(UMHGZKinsectPiercerAbility& Ability)
	{
		Ability.ResetActivationState();
		Ability.PiercerActionToken = Ability.GetActionToken();
		Ability.Phase = EKinsectPiercerPhase::Landing203;
		Ability.LandingMontage = LoadObject<UAnimMontage>(nullptr,
			TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_QiangHuaCaoChongChuanCi_Over.AM_IG_QiangHuaCaoChongChuanCi_Over"));
		return Ability.AcquireLandingRootMotion() && Ability.PlayLandingMontage();
	}
	static bool Start(UMHGZDescendingThrustAbility& Ability)
	{
		Ability.ResetActivationState();
		Ability.DescendingActionToken = Ability.GetActionToken();
		Ability.Phase = EDescendingThrustPhase::GroundOver;
		Ability.LandingMontage = LoadObject<UAnimMontage>(nullptr,
			TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_JiXiTuCi_Over.AM_IG_JiXiTuCi_Over"));
		return Ability.AcquireLandingRootMotion() && Ability.PlayLandingMontage();
	}
};
#endif

#define DEFINE_LANDING_HANDOFF_FIXTURE(AbilityClass) \
AbilityClass::AbilityClass() \
{ \
	ActivationRequiredTags.Reset(); \
	ActivationBlockedTags.Reset(); \
	StaminaCostPolicy = EAbilityStaminaCostPolicy::None; \
} \
bool AbilityClass::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, \
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags, \
	const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const \
{ \
	return UMHGZGameplayAbility::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags); \
}

DEFINE_LANDING_HANDOFF_FIXTURE(UMHGZM5PiercerLandingHandoffTestAbility)
DEFINE_LANDING_HANDOFF_FIXTURE(UMHGZM5ThrustLandingHandoffTestAbility)
#undef DEFINE_LANDING_HANDOFF_FIXTURE

#define DEFINE_LANDING_HANDOFF_ACTIVATION(AbilityClass) \
void AbilityClass::ActivateAbility(const FGameplayAbilitySpecHandle Handle, \
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, \
	const FGameplayEventData* TriggerEventData) \
{ \
	UMHGZGameplayAbility::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData); \
	if (!IsActionActivationCommitted()) { return; } \
	FGameplayTagContainer Tags; \
	Tags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Attacking"))); \
	Tags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.BlockMovement"))); \
	AcquireActionTags(Tags, TEXT("M5LandingHandoffFixture")); \
	if (!FMHGZM5LandingDodgeHandoffTestAccess::Start(*this)) \
	{ RequestEndAction(EWeaponActionEndReason::Interrupted); } \
}

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
DEFINE_LANDING_HANDOFF_ACTIVATION(UMHGZM5PiercerLandingHandoffTestAbility)
DEFINE_LANDING_HANDOFF_ACTIVATION(UMHGZM5ThrustLandingHandoffTestAbility)
#else
void UMHGZM5PiercerLandingHandoffTestAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	EndAbility(Handle, ActorInfo, ActivationInfo, false, true);
}
void UMHGZM5ThrustLandingHandoffTestAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	EndAbility(Handle, ActorInfo, ActivationInfo, false, true);
}
#endif
#undef DEFINE_LANDING_HANDOFF_ACTIVATION

void UMHGZM5FailedLandingDodgeTestAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	UClass* DodgeClass = LoadClass<UMHGZDodgeAbility>(nullptr,
		TEXT("/Game/Blueprints/Ability/Core/GA_Dodge.GA_Dodge_C"));
	const UMHGZDodgeAbility* Defaults = DodgeClass ? DodgeClass->GetDefaultObject<UMHGZDodgeAbility>() : nullptr;
	if (!Defaults) { EndAbility(Handle, ActorInfo, ActivationInfo, false, true); return; }
	UnsheathedDodgeMontage = Defaults->UnsheathedDodgeMontage;
	UnsheathedLeftDodgeMontage = Defaults->UnsheathedLeftDodgeMontage;
	UnsheathedRightDodgeMontage = Defaults->UnsheathedRightDodgeMontage;
	UnsheathedBackDodgeMontage = Defaults->UnsheathedBackDodgeMontage;
	bForwardDodgeUsesActionRootMotionPhase = Defaults->bForwardDodgeUsesActionRootMotionPhase;
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}

bool UMHGZM5FailedLandingDodgeTestAbility::StartDodgeMontage(
	ACharacter& Character, UAnimMontage* Montage, FName StartSection)
{
	Super::StartDodgeMontage(Character, Montage, StartSection);
	return false;
}
