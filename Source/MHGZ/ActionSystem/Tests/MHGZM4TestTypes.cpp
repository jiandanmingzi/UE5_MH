// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZM4TestTypes.h"

#include "InsectGlaive/Kinsect/Kinsect.h"

UMHGZM4TestAttackAbility::UMHGZM4TestAttackAbility()
{
	StaminaCostPolicy = EAbilityStaminaCostPolicy::None;
}

void UMHGZM4TestAttackAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	UMHGZGameplayAbility::ActivateAbility(
		Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!IsActionActivationCommitted())
	{
		return;
	}
	FGameplayTagContainer Tags;
	Tags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Attacking")));
	AcquireActionTags(Tags, FName(TEXT("M4TestAttackState")));
}

void UMHGZM4TestDrawAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	UMHGZGameplayAbility::ActivateAbility(
		Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (IsActionActivationCommitted() && !AcquireDrawMontageRootMotion())
	{
		RequestEndAction(EWeaponActionEndReason::Cancelled);
	}
}

bool UMHGZM4TestDrawAbility::CommitForTest()
{
	return CommitDraw(GetActionToken());
}

void UMHGZM4TestDrawAbility::InterruptForTest()
{
	RequestEndAction(EWeaponActionEndReason::Interrupted);
}

bool UMHGZM4TestDrawAndSendAbility::ValidateActionDependencies() const
{
	FKinsectFlightRequest Request;
	return BuildRequest(Request);
}

bool UMHGZM4TestDrawAndSendAbility::StartActionMontage(ACharacter& Character,
	UAnimMontage* Montage)
{
	(void)Character;
	(void)Montage;
	return true;
}

bool UMHGZM4TestDrawAndSendAbility::CommitDrawForTest()
{
	return CommitDraw(GetActionToken());
}

bool UMHGZM4TestDrawAndSendAbility::CommitSendForTest()
{
	return CommitSendKinsect(GetActionToken());
}

void UMHGZM4TestDrawAndSendAbility::FinishNormallyForTest()
{
	OnActionMontageCompleted();
}

void UMHGZM4TestDrawAndSendAbility::InterruptForTest()
{
	RequestEndAction(EWeaponActionEndReason::Interrupted);
}

bool UMHGZM4TestSendKinsectAbility::StartActionMontage(
	ACharacter& Character, UAnimMontage* Montage)
{
	(void)Character;
	(void)Montage;
	return true;
}

bool UMHGZM4TestSendKinsectAbility::CommitForTest()
{
	return CommitSendKinsect(GetActionToken());
}

void UMHGZM4TestSendKinsectAbility::FinishNormallyForTest()
{
	OnActionMontageCompleted();
}

void UMHGZM4TestSendKinsectAbility::InterruptForTest()
{
	OnActionMontageInterrupted();
}

bool UMHGZM4TestRecallKinsectAbility::StartActionMontage(
	ACharacter& Character, UAnimMontage* Montage)
{
	(void)Character;
	(void)Montage;
	return true;
}

bool UMHGZM4TestRecallKinsectAbility::CommitForTest()
{
	return CommitRecallKinsect(GetActionToken());
}

void UMHGZM4TestRecallKinsectAbility::FinishNormallyForTest()
{
	OnActionMontageCompleted();
}

void UMHGZM4TestRecallKinsectAbility::InterruptForTest()
{
	OnActionMontageInterrupted();
}

UMHGZM4TestDodgeAbility::UMHGZM4TestDodgeAbility()
{
	StaminaCostPolicy = EAbilityStaminaCostPolicy::None;
}

bool UMHGZM4TestDodgeAbility::StartDodgeMontage(
	ACharacter& Character, UAnimMontage* Montage, FName StartSection)
{
	(void)Character;
	(void)Montage;
	return StartSection == DodgeCoreSectionName;
}

bool UMHGZM4TestDodgeAbility::ConfigureDodgeCoreFallbackExit()
{
	++FallbackExitConfigurationCount;
	return true;
}

bool UMHGZM4TestDodgeAbility::JumpToDodgeSection(FName SectionName)
{
	LastJumpedSection = SectionName;
	return SectionName == IdleExitSectionName || SectionName == MoveExitSectionName;
}

void UMHGZM4TestDodgeAbility::BlendOutForTest()
{
	OnMontageBlendOut();
}

void UMHGZM4TestDodgeAbility::FinishNormallyForTest()
{
	OnMontageCompleted();
}

void UMHGZM4TestDodgeAbility::InterruptForTest()
{
	OnMontageInterrupted();
}

void UMHGZM4TestDodgeAbility::ExpireMontageEndFailsafeForTest()
{
	OnMontageEndFailsafeExpired();
}

void UMHGZM4TestDodgeAbility::EnterDodgeSectionForTest(FName SectionName)
{
	HandleDodgeSectionEntered(SectionName);
}

bool UMHGZM4FailingDodgeAbility::StartDodgeMontage(
	ACharacter& Character, UAnimMontage* Montage, FName StartSection)
{
	(void)Character;
	(void)Montage;
	(void)StartSection;
	return false;
}

bool UMHGZM4TestSheatheAbility::StartSheatheMontage(ACharacter& Character,
	UAnimMontage* Montage, FName StartSection)
{
	(void)Character;
	(void)Montage;
	(void)StartSection;
	return true;
}

bool UMHGZM4TestSheatheAbility::CommitForTest()
{
	return CommitSheathe(GetActionToken());
}

void UMHGZM4TestSheatheAbility::FinishNormallyForTest()
{
	OnMontageCompleted();
}

void UMHGZM4TestSheatheAbility::InterruptForTest()
{
	OnMontageInterrupted();
}
