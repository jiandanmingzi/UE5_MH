// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MHGZInsectGlaiveAbility.h"
#include "MHGZAirDodgeAbility.generated.h"

class UAbilityTask_MHGZPlayMontageAndWait;
class UAbilityTask_MHGZWeaponMovement;
class UInsectGlaiveCombatConfig;

// （旧 EMHGZAerialDodgeInputPolicy 已退役 —— 最早可操作帧的锁是「空中可操作」
// tag 的缺席：`Combat.State.Aerial.Falling` 领于 handoff/离地/中止滞空、放于新动作
// 起播，本 GA 的 ActivationRequiredTags 查它的**在场**。行为表见
// `docs/using/m5-plan.md` §8.1。）

/**
 * 空中回避（MHR id 137）—— M5 阶段 D 第 1 招。
 *
 * 真值（唯一真值源 = MHR 实机录制）：纯抛体，g = −2378 cm/s²（R² 中位 0.9991）、水平
 * 900 cm/s 沿摇杆方向、垂直 +1182 cm/s 起，142 帧 / 1.185 s，净位移水平 1065 cm /
 * 垂直 −268 cm。**无白灯与有白灯逐值相同** ⇒ 没有白灯变体，`bEnhancedVariant` 恒 false。
 *
 * 三条结构性事实，写代码前必须知道：
 *
 * 1. **没有起手段。** 142 帧本身就是一条抛物线，初速从第 0 帧生效 —— 与撑杆跳
 *    「Jump 段 + JumpOver 段」本质不同，所以不拆两条蒙太奇、不做段速率压缩
 *    （剪辑 `rate_scale = 2.0`，`RequiredSegmentPlayRate` 回算正好 1.0）。
 * 2. **它是八向的。** 方向取输入快照里的世界系摇杆方向（**连续角，不量化到 8 扇区**：
 *    实测 47% 的段偏离最近的 45° 方位、最多 ±22.4°，而相机在这些时刻逐帧横摆恒为
 *    0.0°/帧，所以不是相机滞后），并在激活那一帧**瞬转**到该方向（实测段内 213/213 段
 *    朝向变化恰好 0.0°，段首相对前一帧的转向中位 82.3°、最大 178.7°）。瞬转由基类的
 *    `ApplyDirectionCorrection()` 完成，本 GA 把 `MaxCorrectionAngle` 放到 180 让它不被
 *    限幅。⚠ 真值表那列 `|方向−朝向| = 0.0°` 分不开「不转向」与「一按就瞬转」，别再用它。
 * 3. **「142 帧」不是源时长。** `ExplicitLaunchVelocity` 下源的时长由重力反算
 *    （2·vz/g ≈ 0.995 s），余下 ≈0.188 s 交给 CMC 自由落体 —— 两段相加才是 142 帧，
 *    这与真值「`137 → 157 → 落地` 是同一条抛物线、无速度重置」完全一致。**不要为凑时长加旋钮。**
 *
 * 入口门禁只有「`Combat.State.Aerial` 在、`Grounded` 不在」：真值里 `137` 的前驱有
 * 弧段（117 次）、舞踏（53 次）**以及 `143 起跳下坠`（25 次）** ⇒ 它必须能在
 * 「系统托管下落」期间触发，所以**不能**要求 `Combat.State.Aerial.Actionable`
 * （那个 tag 在下落态已被释放）。
 */
UCLASS(BlueprintType, Blueprintable)
class MHGZ_API UMHGZAirDodgeAbility : public UMHGZInsectGlaiveAbility
{
	GENERATED_BODY()

public:
	UMHGZAirDodgeAbility();

	/**
	 * 纯函数：按真值组装弹道请求。**不读摇杆、不读 CMC** —— 方向与配置都由调用方传入，
	 * 于是自动化测试可以直接断言请求形状而不需要活角色。
	 *
	 * `LaunchDirection` 是世界系水平方向；接近零向量时回退 `FallbackFacing`（摇杆回中）。
	 */
	static FWeaponMovementRequest BuildAirDodgeRequest(
		const FWeaponActionToken& OwnerAction,
		const FVector& LaunchDirection,
		const FVector& FallbackFacing,
		const UInsectGlaiveCombatConfig& Config);

	// （旧 AerialPreInputLifetime 已随空中预输入退役 —— 2026-09-22 用户拍板取消：
	// 锁定期里的按下直接作废，不入槽、放锁点不补发。）

	virtual bool CanActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags,
		const FGameplayTagContainer* TargetTags,
		FGameplayTagContainer* OptionalRelevantTags) const override;

	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

protected:
	virtual bool ValidateActionDependencies() const override;

	/** 只多做一件事：找出蒙太奇上的点通知（它决定最早可操作帧）。 */
	virtual bool PrepareAttackMontage() override;

	/**
	 * 与基类同构，只换绑定的处理函数：本条蒙太奇属于本 GA，完成帧就是本招的收尾点
	 * （见 `TryFinishAirDodge`），不能被攻击能力那套完成处理接管。
	 *
	 * 收尾点仍是蒙太奇自身长度（1.18333 s；运动源更早结束 ≈0.995 s，但尾段 0.188 s 的
	 * 动画还要播完），**收尾方式照抄撑杆跳弧段**：`bEnableAutoBlendOut = false` 让终末姿势
	 * 保持住，由任务在位置到达长度时报完成 —— 于是 `EndAbility` 那一帧实例还活着，
	 * `Montage_Stop(0.05f)` 才是一次真淡出，能与下落蒙太奇交叉。详见 .cpp 里的长注释。
	 */
	virtual bool StartAttackMontage(ACharacter& Character, UAnimMontage* Montage,
		FName StartSection) override;

	/** 位移源结束：只按结束原因分类，**不**在这里收尾（蒙太奇还有尾段）。 */
	UFUNCTION()
	void HandleAirDodgeMovementFinished(const FWeaponMovementResult& MovementResult);

	UFUNCTION()
	void HandleAirDodgeVisualCompleted();

	UFUNCTION()
	void HandleAirDodgeVisualInterrupted();

	/**
	 * R4 修复：源到期（FreeFall 分类）后的蒙太奇尾段里触地 —— 位移任务已在 FinishMovement
	 * 解绑 LandedDelegate，这里由本 GA 接棒，把落地表现认领下来（两旗标互斥翻转）。
	 */
	UFUNCTION()
	void HandleAirDodgeTailLanded(const FHitResult& Hit);

public:
	/** 自动化专用：摆收尾旗标 / 触发尾段落地分支（生产路径由位移回调驱动）。 */
	void SetAirDodgeExitFlagsForTest(bool bInMovementFinished, bool bInBeginFreeFall)
	{
		bAirDodgeMovementFinished = bInMovementFinished;
		bBeginFreeFallAfterEnd = bInBeginFreeFall;
	}
	bool GetBeginFreeFallAfterEndForTest() const { return bBeginFreeFallAfterEnd; }
	bool GetPlayLandedPresentationForTest() const { return bPlayLandedPresentation; }
	void HandleAirDodgeTailLandedForTest() { HandleAirDodgeTailLanded(FHitResult()); }
	void HandleAirDodgeVisualCompletedForTest();

	/**
	 * 自动化专用：取视觉任务裸指针，用来钉住两条曾经各自失效过的不变量 ——
	 * ① 任务确实登记了 tick（`IsTickingTask()` + ticking 列表，少了它 `ReportCompletionAtMontageEnd`
	 * 永远不上报 ⇒ 终末姿势保持到触地、下坠段姿势冻结）；② 蒙太奇实例保持终末姿势
	 * （`bEnableAutoBlendOut == false`，从 `AnimInstance->GetActiveInstanceForMontage` 读）。
	 *
	 * 返回非 const：第 ①/④ 条钉子里要用 `DriveTickForTest` 手动驱动一次 tick（测试世界不 tick）。
	 * 除那次驱动外只做只读断言。
	 */
	UAbilityTask_MHGZPlayMontageAndWait* GetAirDodgeVisualTaskForTest() const;

	/** 自动化专用：视觉完成旗标（第 ④ 条钉子要断言 `OnCompleted` 真的走到了这里）。 */
	bool GetAirDodgeVisualFinishedForTest() const { return bAirDodgeVisualFinished; }

protected:

private:
	/** 起弹道源。调用前重力 profile 必须已经就位（见 ActivateAbility 的顺序注释）。 */
	bool StartAirDodgeMovement();

	/** 让位：把其它活跃 IG 动作以 Superseded 结束（同步摘源、释放位移所有权）。 */
	void SupersedeOtherLiveIGActions();

	/** 两个条件都满足才收尾：运动结束，且（视觉播完 或 已经触地认领落地姿势）。 */
	void TryFinishAirDodge();

	UPROPERTY(Transient)
	TObjectPtr<UAbilityTask_MHGZWeaponMovement> AirDodgeMovementTask;

	UPROPERTY(Transient)
	TObjectPtr<UAbilityTask_MHGZPlayMontageAndWait> AirDodgeVisualTask;

	bool bAirDodgeVisualFinished = false;
	bool bAirDodgeMovementFinished = false;
	bool bBeginFreeFallAfterEnd = false;
	bool bPlayLandedPresentation = false;
	bool bIsEndingAirDodge = false;
	bool bBoundTailLanded = false;
};
