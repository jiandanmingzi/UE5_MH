// Copyright MHGZ Project. All Rights Reserved.
//
// 强化操虫穿刺入招闸门的观测探针。
//
// 为什么需要它：闸门的两层 —— `CanActivateAbility` 与 `ValidateActionDependencies` ——
// 都只能通过真实的 `ASC->TryActivateAbility` 到达（`ValidateActionDependencies` 内部
// 要读 `CurrentActorInfo`，直接 `NewObject` 出来的能力实例没有它）。
//
// 配置**套在激活实例自己身上**（`ApplyTestConfiguration`），不写 CDO：
// 本 GA 的 `InstancingPolicy` 是 `InstancedPerExecution`，每次执行都是新实例，
// 而测试观测到的正是那个实例。这也正是生产里谱系 BP 提供字段的方式 ——
// 在实例上摆配置，而不是去改类默认值。
//
// 观测结果写在**静态**成员上：实例随激活生灭，测试要在它消失之后才读得到结果。
//
// `PrepareAttackMontage` 是闸门之后、任何资产被触碰之前的第一个钩子；
// 让它在记录之后干净地返回 false，就能在零资产、零蒙太奇的前提下把
// 「闸门放行 / 否决」变成一个布尔值。

#pragma once

#include "CoreMinimal.h"
#include "ActionSystem/MHGZKinsectPiercerAbility.h"
#include "MHGZM5KinsectPiercerTestTypes.generated.h"

/**
 * 友元测试桥。额度记录与若干配置复核是 protected/private 的 —— 它们是**生产**接口，
 * 只给引擎与派生类用；测试通过本类直接调，而不是把它们放宽成 public。
 *
 * 放在这个头里而不是某个 .cpp 里，是因为探针（需要它来做分项诊断）与测试本体都要用。
 */
class FMHGZKM5KinsectPiercerTest
{
public:
	static bool CanApply(UMHGZKinsectPiercerAbility& Ability, const FHitResult& Hit,
		const int32 SegmentIndex)
	{
		return Ability.CanApplyCollisionHit(Hit, SegmentIndex);
	}

	static void RecordDamage(UMHGZKinsectPiercerAbility& Ability, const FHitResult& Hit,
		const int32 SegmentIndex)
	{
		Ability.HandleSuccessfulAttackDamage(Hit, SegmentIndex);
	}

	static void ResetActivation(UMHGZKinsectPiercerAbility& Ability)
	{
		Ability.ResetActivationState();
	}

	static bool Validate(UMHGZKinsectPiercerAbility& Ability)
	{
		return Ability.ValidateActionDependencies();
	}

	static bool Profiles(const UMHGZKinsectPiercerAbility& Ability)
	{
		return Ability.HasUsableTrajectoryProfiles();
	}

	static bool Sections(const UMHGZKinsectPiercerAbility& Ability,
		const UAnimMontage* Montage)
	{
		return Ability.HasRequiredSections(Montage);
	}

	static bool LandingMontage(const UMHGZKinsectPiercerAbility& Ability)
	{
		return Ability.HasRequiredLandingMontage();
	}

	static FWeaponMovementRequest PhaseMovementRequest(
		const UMHGZKinsectPiercerAbility& Ability, const EKinsectPiercerPhase Phase)
	{
		return Ability.MakePhaseMovementRequest(Phase);
	}

	/**
	 * 自动化专用：把 Harness 已初始化的 ActorInfo 与 203 获取时保存的最小快照
	 * 注入一个持久 transient 实例；不模拟激活或 Montage 播放。
	 */
	static void InjectLandingRootMotionCapture(
		UMHGZKinsectPiercerAbility& Ability,
		const FGameplayAbilityActorInfo& ActorInfo,
		const FWeaponActionToken& ActionToken,
		UAnimInstance& AnimInstance,
		const uint8 SavedRootMotionMode)
	{
		Ability.SetCurrentActorInfo(ActionToken.AbilityHandle, &ActorInfo);
		Ability.PiercerActionToken = ActionToken;
		Ability.LandingRootMotionAnimInstance = &AnimInstance;
		Ability.SavedLandingRootMotionMode = SavedRootMotionMode;
		Ability.bLandingRootMotionModeCaptured = true;
	}

	/** 调用生产释放路径；断言必须通过 Host 的真实 owner API 观测结果。 */
	static void ReleaseLandingRootMotion(UMHGZKinsectPiercerAbility& Ability)
	{
		Ability.ReleaseLandingRootMotion();
	}

	/** `ActivationRequiredTags` / `ActivationBlockedTags` 是 `UGameplayAbility` 的
	 * protected 成员 —— 派生类的友元可以通过派生类型的对象读它。 */
	static bool RequiresTag(const UMHGZKinsectPiercerAbility& Ability,
		const FGameplayTag& Tag)
	{
		return Ability.ActivationRequiredTags.HasTag(Tag);
	}

	static bool BlocksTag(const UMHGZKinsectPiercerAbility& Ability,
		const FGameplayTag& Tag)
	{
		return Ability.ActivationBlockedTags.HasTag(Tag);
	}
};

UCLASS()
class UMHGZM5KinsectPiercerGateProbeAbility : public UMHGZKinsectPiercerAbility
{
	GENERATED_BODY()

public:
	/** 测试摆给**下一次激活**的配置；实例在 `ActivateAbility` 最前面把它套到自己身上。 */
	static TObjectPtr<UAnimMontage> TestMainMontage;
	static TObjectPtr<UAnimMontage> TestLandingMontage;
	/** 关掉之后留出「三段武器判定缺失」的负控。 */
	static bool bTestConfigureAttackSegments;

	/** 上一次激活里 `ValidateActionDependencies()` 的返回值。 */
	static bool bLastDependenciesAccepted;
	/** 上一次激活是否越过了闸门、走到 `PrepareAttackMontage`。 */
	static bool bLastReachedPrepareAttackMontage;
	/** 上一次激活有没有走到依赖预检 —— 区分「预检否决」与「根本没进 ActivateAbility」。 */
	static bool bLastDependenciesCalled;
	/** 生产侧 `PrepareAttackMontage`（Section/落地 Montage 复核 + 释放 Falling 许可）的返回值。 */
	static bool bLastProductionPrepareAccepted;
	/** 生产侧 `PrepareAttackMontage` 跑完之后，「空中可操作」许可还在不在。 */
	static bool bLastFallingPermitHeldAfterPrepare;
	/** Production request shapes observed with the real committed activation's actor info. */
	static bool bLastStartMovementRequestValid;
	static bool bLastDiveMovementRequestValid;
	static bool bLastLandingMovementRequestValid;

	static void ResetProbeForTest()
	{
		bLastDependenciesAccepted = false;
		bLastReachedPrepareAttackMontage = false;
		bLastDependenciesCalled = false;
		bLastProductionPrepareAccepted = false;
		bLastFallingPermitHeldAfterPrepare = false;
		bLastStartMovementRequestValid = false;
		bLastDiveMovementRequestValid = false;
		bLastLandingMovementRequestValid = false;
	}

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual bool ValidateActionDependencies() const override;

	virtual bool PrepareAttackMontage() override;

private:
	void ApplyTestConfiguration();
};

