// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MHGZGameplayAbility.h"
#include "MHGZComboCoordinatorAbility.generated.h"

class UMHGZWeaponComboData;
struct FComboNode;

/**
 * UGA_WeaponComboCoordinator — 连招协调器
 * Infinite 持续型 Ability，整个装备期间保持 Active
 *
 * 核心职责：
 * - HandleWeaponInput：接收 ASC 转发来的输入 Tag → 四级排序匹配 → Activate 匹配的 GA
 * - OnAttackFinished / OnAttackHit：接收攻击 GA 回调 → 状态更新 / Tag 授予
 * - 帧级输入批处理 + 预输入缓冲
 */
UCLASS(BlueprintType, Blueprintable)
class UGA_WeaponComboCoordinator : public UMHGZGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_WeaponComboCoordinator();

	// ── 配置 ──

	/** 当前状态名 */
	UPROPERTY(BlueprintReadOnly, Category = "MHGZ|Combo")
	FName CurrentState;

	/** 上一个状态名 */
	UPROPERTY(BlueprintReadOnly, Category = "MHGZ|Combo")
	FName PreviousState;

	// ═══════════════════════════════════════════
	// 连招数据注入
	// ═══════════════════════════════════════════

	/** 注入连招数据（异步加载完成后调用） */
	void InjectComboData(UMHGZWeaponComboData* Data);

	// ═══════════════════════════════════════════
	// 输入处理
	// ═══════════════════════════════════════════

	/**
	 * 处理武器输入（由 ASC 转发）
	 * 四级排序：Priority → RequiredTags 满足数 → BlockedTags 无命中 → Chord > 单键
	 */
	void HandleWeaponInput(FGameplayTag InputTag);

	// ═══════════════════════════════════════════
	// GA 回调
	// ═══════════════════════════════════════════

	/** 当前攻击 GA 结束回调 */
	void OnAttackFinished();

	/** 当前攻击 GA 首次命中回调（触发 PendingGrantedTags） */
	void OnAttackHit();

	/** 着陆回调 */
	void OnLanded();

	// ═══════════════════════════════════════════
	// 查询
	// ═══════════════════════════════════════════

	UFUNCTION(BlueprintCallable, Category = "MHGZ|Combo")
	FName GetCurrentState() const { return CurrentState; }

	UFUNCTION(BlueprintCallable, Category = "MHGZ|Combo")
	FName GetPreviousState() const { return PreviousState; }

	// ── Ability 覆写 ──
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
	/** 匹配并激活符合条件的 GA */
	bool TryMatchAndActivate(const FComboNode& Node);

	/** 构建 StateIndex（按 StateName 建索引） */
	void BuildStateIndex();

	/** 检查节点是否匹配当前状态 */
	bool DoesNodeMatchState(const FComboNode& Node) const;

	/** 按四级排序获取最佳匹配节点 */
	const FComboNode* FindBestMatch(const TArray<const FComboNode*>& Candidates) const;

private:
	UPROPERTY()
	TObjectPtr<UMHGZWeaponComboData> ComboData;

	/** StateName → ComboTable 索引列表 */
	TMap<FName, TArray<int32>> StateIndex;

	/** 待授予的 Tag（首次命中时 Apply） */
	FGameplayTagContainer PendingGrantedTags;

	/** 当前激活的 GA */
	FGameplayAbilitySpecHandle ActiveAttackHandle;

	/** 全局超时 Timer */
	FTimerHandle ComboTimeoutTimer;
};
