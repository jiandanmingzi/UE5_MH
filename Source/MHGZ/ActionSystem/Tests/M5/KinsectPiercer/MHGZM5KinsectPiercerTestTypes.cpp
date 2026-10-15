// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZM5KinsectPiercerTestTypes.h"

#include "AbilitySystemComponent.h"
#include "ActionSystem/MHGZAttackAbility.h"
#include "Animation/AnimMontage.h"
#include "GameplayEffect.h"
#include "GameplayTagContainer.h"

TObjectPtr<UAnimMontage> UMHGZM5KinsectPiercerGateProbeAbility::TestMainMontage = nullptr;
TObjectPtr<UAnimMontage> UMHGZM5KinsectPiercerGateProbeAbility::TestLandingMontage = nullptr;
bool UMHGZM5KinsectPiercerGateProbeAbility::bTestConfigureAttackSegments = true;

bool UMHGZM5KinsectPiercerGateProbeAbility::bLastDependenciesAccepted = false;
bool UMHGZM5KinsectPiercerGateProbeAbility::bLastReachedPrepareAttackMontage = false;
bool UMHGZM5KinsectPiercerGateProbeAbility::bLastDependenciesCalled = false;
bool UMHGZM5KinsectPiercerGateProbeAbility::bLastProductionPrepareAccepted = false;
bool UMHGZM5KinsectPiercerGateProbeAbility::bLastFallingPermitHeldAfterPrepare = false;
bool UMHGZM5KinsectPiercerGateProbeAbility::bLastStartMovementRequestValid = false;
bool UMHGZM5KinsectPiercerGateProbeAbility::bLastDiveMovementRequestValid = false;
bool UMHGZM5KinsectPiercerGateProbeAbility::bLastLandingMovementRequestValid = false;

void UMHGZM5KinsectPiercerGateProbeAbility::ApplyTestConfiguration()
{
	// 只补测试关心的四样：两条 Montage、三段武器判定、RTY 输入标签。
	// 其余字段（Section 名、门禁标签、曲线来源）走生产默认值 —— 那正是要被验的部分。
	InputTag = FGameplayTag::RequestGameplayTag(TEXT("Input.Weapon.RTY"));
	AttackMontage = TestMainMontage;
	LandingMontage = TestLandingMontage;

	if (!bTestConfigureAttackSegments)
	{
		return;
	}
	AttackSegments.SetNum(3);
	for (int32 SegmentIndex = 0; SegmentIndex < 3; ++SegmentIndex)
	{
		FAttackSegmentConfig& Segment = AttackSegments[SegmentIndex];
		Segment.Damage.DamageEffectClass = UGameplayEffect::StaticClass();
		Segment.Damage.MotionValue = 10.0f;
		Segment.Damage.bRequiresHitToContinue = false;
		Segment.MultiHitCount = 1;
		Segment.MultiHitPolicy = EAttackMultiHitPolicy::ContactOnly;
		FWeaponTraceRegion& Region = Segment.Collision.TraceRegions.AddDefaulted_GetRef();
		Region.EndSocketName = TEXT("IG_FrontTip");
	}
}

void UMHGZM5KinsectPiercerGateProbeAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	ApplyTestConfiguration();
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}

bool UMHGZM5KinsectPiercerGateProbeAbility::ValidateActionDependencies() const
{
	bLastDependenciesCalled = true;
	bLastDependenciesAccepted = Super::ValidateActionDependencies();
	return bLastDependenciesAccepted;
}

bool UMHGZM5KinsectPiercerGateProbeAbility::PrepareAttackMontage()
{
	bLastReachedPrepareAttackMontage = true;

	// 先跑生产侧那一份：它复核 Section/落地 Montage，并在这一帧释放「空中可操作」许可。
	bLastProductionPrepareAccepted = Super::PrepareAttackMontage();
	bLastStartMovementRequestValid = FMHGZKM5KinsectPiercerTest::PhaseMovementRequest(
		*this, EKinsectPiercerPhase::Start200).HasValidRecordedTrajectoryParameters();
	bLastDiveMovementRequestValid = FMHGZKM5KinsectPiercerTest::PhaseMovementRequest(
		*this, EKinsectPiercerPhase::Dive201).HasValidRecordedTrajectoryParameters();
	bLastLandingMovementRequestValid = FMHGZKM5KinsectPiercerTest::PhaseMovementRequest(
		*this, EKinsectPiercerPhase::Landing203).HasValidRecordedTrajectoryParameters();

	// 观测点：这是「已 Commit、蒙太奇未起播」这一帧，也就是本招该已经把许可放掉的地方。
	const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	bLastFallingPermitHeldAfterPrepare = ASC && ASC->HasMatchingGameplayTag(
		FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aerial.Falling")));

	// 干净失败：闸门与准备都已经过了，探针只需要「到没到这一步、状态对不对」这两个事实。
	// 不建资产、不起播蒙太奇、不进入任何异步路径 —— 于是本体测试不依赖 M3 的产物。
	return false;
}

