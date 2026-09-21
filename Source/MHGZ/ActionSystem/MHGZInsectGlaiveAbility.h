// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MHGZAttackAbility.h"
#include "WeaponRuntime/MHGZWeaponRuntimeTypes.h"
#include "MHGZInsectGlaiveAbility.generated.h"

class URes_InsectGlaive;
class USoundBase;
class UAnimMontage;

/**
 * UMHGZInsectGlaiveAbility — 虫棍 GA 基类
 * 继承 UMHGZAttackAbility，增加萃取检查、三灯音效注入、消耗灯
 */
UCLASS(BlueprintType, Blueprintable, Abstract)
class UMHGZInsectGlaiveAbility : public UMHGZAttackAbility
{
	GENERATED_BODY()

public:
	UMHGZInsectGlaiveAbility();

	/**
	 * BallisticVault / CurvedVault 跑完之后，本次 Action 该交接什么。互斥且穷尽。
	 *
	 * 两个使用者 —— 舞踏（UMHGZAdvancingCounterAbility）与后撑杆跳
	 * （UMHGZPoleVaultAbility）—— 的弧线形状不同，但「够到地面就认领落地、
	 * 还在空中就交给 CMC 自由落体」这条判据是同一个，所以放在基类。
	 */
	enum class EVaultExit : uint8
	{
		/** 取消/被挡/失败：什么都不交接。 */
		None,
		/** 弧线在空中跑完，本体交给 CMC 继续积分。 */
		FreeFall,
		/** 弧线抵达地面，本次 Action 认领落地姿势。 */
		LandedPresentation,
	};

	/**
	 * 由移动结束原因判定交接去向。**只吃结束原因**。
	 *
	 * 唯一要问的问题：「弧走完时，胶囊在半空吗？」。UE 对这个问题的回答是
	 * 直接的 —— `LandedDelegate` 有没有响过：
	 *   - 响了       ⇒ 胶囊在地上 ⇒ LandedPresentation
	 *   - 跑完却没响 ⇒ 胶囊在半空 ⇒ FreeFall
	 *
	 * 这里曾经还有第二个 `bCmcIsFalling` 参数，后撑杆跳还额外 `&&` 一个
	 * `BackVaultResidualFallSeconds > 0`。两者都是在拿「CMC 恰好处于什么模式」
	 * 这个**代理指标**去猜上面那个问题，而代理指标一直是错的：`ProcessLanded`
	 * 先广播 LandedDelegate、之后才 `SetPostLandedPhysics`，回调那一刻 CMC 仍在
	 * MOVE_Falling；旧 `FinishMovement` 又会在广播前强行切 MOVE_Falling，于是
	 * 每一次 CurvedVault 结束时 `IsFalling()` 都为真。
	 *
	 * 现在改由 `UAbilityTask_MHGZWeaponMovement` 在广播前把「弧尾时还在
	 * MOVE_Flying」如实翻译成 `Completed`（悬空）或 `Landed`（落在实地），
	 * 所以只剩原因这一个真值来源，两个 vault 共用一条规则。
	 */
	static EVaultExit ResolveVaultExit(EWeaponMovementEndReason Reason);

	/**
	 * 空中最早可操作帧 —— 由 UAnimNotify_IG_AerialHandoff（**点**通知）驱动。
	 *
	 * 这一帧之前，弧独占胶囊：MovementTask 把 CMC 置于 MOVE_Flying，
	 * PhysFlying 不跑重力、不 FindFloor、不 ProcessLanded，所以弧不可能被一次
	 * 误判的触地打断。实测过这条路径的代价：舞踏起飞前已是 MOVE_Falling，
	 * CMC 立刻找到脚下地面，动作 3 帧后被 Landed 收尾，源算出的力
	 * （force-Z = 1373.75，完全正确）根本没进 Velocity。
	 *
	 * 这一帧之后，CMC 接管：重力与地面判定回来，空中招式与落地动画可触发。
	 * override 模式下的根运动源仍每帧覆盖速度，所以**轨迹不变**，
	 * 变的只是「落地现在看得见了」。
	 *
	 * 动画继续播完 —— 这里只切物理模式与记账，不碰表现态（下落/落地蒙太奇
	 * 仍由 Host 在动作结束时交接）。
	 */
	virtual bool NotifyAerialHandoff();

	/** 本动作是否**曾经**越过最早可操作帧。没挂通知时恒为 false。 */
	bool IsAerialHandoffReached() const { return bAerialHandoffReached; }

	/** 蒙太奇上是否挂了点通知。没挂不是错误，但会告警 —— 见 DetectAerialHandoffNotify。 */
	bool IsAerialHandoffAuthored() const { return bAerialHandoffAuthored; }

	/**
	 * 纯谓词：这个蒙太奇挂了点通知吗。
	 *
	 * 公开且静态，为的是不建 GA 就能测 —— 它是「命令列写完之后运行时到底认不认」
	 * 的唯一判据，而它曾经出过一个静默失效的坑：点通知写在 `Notify` 上，
	 * 退役的 IG_AerialWindow 写在 `NotifyStateClass` 上，查错槽位就永远为 false。
	 */
	static bool MontageHasAerialHandoffNotify(const UAnimMontage* Montage);

	// ── 覆写 ──
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

	// ── 配置 ──

	/** 三灯攻击音效——每个攻击 GA 激活时播放 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability|Audio")
	TObjectPtr<USoundBase> TripleUpSwingSound;

	/** 检查是否有指定灯 */
	UFUNCTION(BlueprintCallable, Category = "MHGZ|IG")
	bool CheckExtractRequirement(FGameplayTag ExtractColor) const;

	/** 消耗灯并 Apply 爆发 Buff */
	UFUNCTION(BlueprintCallable, Category = "MHGZ|IG")
	bool ConsumeExtractAndApplyBurst(FGameplayTag ExtractType, TSubclassOf<UGameplayEffect> BurstGE);

protected:
	/** 获取虫棍资源组件 */
	URes_InsectGlaive* GetIGResourceComponent() const;

	/**
	 * 在蒙太奇上找 UAnimNotify_IG_AerialHandoff，写进 bAerialHandoffAuthored，
	 * 并清掉上一次激活留下的锁存。由派生类在起播蒙太奇前调用一次。
	 * 找不到就告警 —— 静默无释放点比缺释放点更难查。
	 */
	void DetectAerialHandoffNotify(const UAnimMontage* Montage);

	/** 释放 Combat.State.Aerial.Actionable。由基类 EndAbility 调用。幂等。 */
	void CloseAerialHandoff();

private:
	/** 最早可操作帧那一帧拿到的 tag，随本次激活存续。 */
	FWeaponOwnedTagToken AerialHandoffTag;
	/** 锁存：越过最早可操作帧后只增不减，供 exit 判据与调试读。 */
	bool bAerialHandoffReached = false;
	/** 蒙太奇上是否挂了点通知。只作诊断，不参与行为。 */
	bool bAerialHandoffAuthored = false;
};
