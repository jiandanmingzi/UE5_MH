// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "AbilityTask_MHGZPlayMontageAndWait.generated.h"

/**
 * GAS montage task with a per-play Blend In override. It deliberately keeps
 * UAbilityTask_PlayMontageAndWait's delegate and teardown contract while
 * routing playback through UMHGZAbilitySystemComponent.
 */
UCLASS()
class MHGZ_API UAbilityTask_MHGZPlayMontageAndWait : public UAbilityTask_PlayMontageAndWait
{
	GENERATED_BODY()

public:
	/** 开 tick（`bTickingTask = true`）—— TickTask 的门，理由见 .cpp 里的长注释。 */
	UAbilityTask_MHGZPlayMontageAndWait();

	static UAbilityTask_MHGZPlayMontageAndWait* CreatePlayMontageAndWaitProxy(
		UGameplayAbility* OwningAbility, FName TaskInstanceName,
		UAnimMontage* MontageToPlay, float Rate, FName StartSection,
		float BlendInTime, bool bStopWhenAbilityEnds = true,
		float AnimRootMotionTranslationScale = 1.0f,
		float StartTimeSeconds = 0.0f,
		bool bAllowInterruptAfterBlendOut = false);

	virtual void Activate() override;

	/**
	 * 蒙太奇**播到自身长度**时报完成。只给「关掉了 `bEnableAutoBlendOut` 的调用方」用。
	 *
	 * 关掉之后引擎永远不会在自身长度处终止实例 —— `FAnimMontageInstance::Advance` 的整段
	 * 自动淡出被 `bEnableAutoBlendOut` 跳过，实例停在末尾保持终末姿势，于是
	 * `OnMontageEnded`（以及本任务的 `OnCompleted`）**永远不会来**。
	 * `Montage_IsPlaying` 同理会变成 false（它的判据是实例的 `bPlaying`），所以判据用的是
	 * 「实例还在 **且** 位置已到长度」。
	 *
	 * 撑杆跳弧段就是这样一条蒙太奇（`PlayVaultMontage` 里那行 `bEnableAutoBlendOut = false`），
	 * 但它的收尾触发点是自己的移动源，用不到本入口；空中回避没有更早的移动收尾点，
	 * 它的收尾触发点**就是**这条蒙太奇的长度，所以由本任务如实上报。
	 */
	void ReportCompletionAtMontageEnd() { bCompleteAtMontageEnd = true; }

	/**
	 * 自动化专用：手动驱动一次 tick。
	 *
	 * 本工程的测试世界**不 tick**（`MHGZM5AirDodgeTests` 的 harness 只 `CreateWorld` + `SpawnActor`），
	 * 所以「播到长度 ⇒ 上报 ⇒ 走到能力里」这段链路只能这样驱动一次。它正是
	 * `MHGZ.M5.Aerial.AirDodgeVisualTaskTicksAndHoldsTerminalPose` 第 ④ 条要覆盖的链路。
	 */
	void DriveTickForTest(float DeltaTime) { TickTask(DeltaTime); }


	/**
	 * 判据本体（纯函数，便于离线回归）：位置是否已到自身长度。
	 * 引擎在末尾把 `Position` 夹到 `SectionEnd − KINDA_SMALL_NUMBER/2`，故留 1e-3 s 容差。
	 */
	static bool HasMontageReachedEnd(float Position, float PlayLength);

protected:
	virtual void TickTask(float DeltaTime) override;

private:
	bool bCompleteAtMontageEnd = false;
	bool bCompletionReported = false;

	/** Negative is never stored here: callers resolve asset fallback first. */
	float BlendInTime = 0.0f;
};
