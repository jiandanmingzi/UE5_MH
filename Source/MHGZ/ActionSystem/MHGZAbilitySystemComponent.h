// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "GameplayTagContainer.h"
#include "WeaponRuntime/MHGZWeaponRuntimeTypes.h"
#include "MHGZAbilitySystemComponent.generated.h"

class UGameplayAbility;
class UGameplayEffect;
class UMHGZWeaponComboData;
class UMHGZWeaponRuntimeHostComponent;
class UGA_WeaponComboCoordinator;

/**
 * UMHGZAbilitySystemComponent — 扩展 ASC
 * 批量授予核心能力、武器 Ability 管理、协调器指针、M1 输入快照路由与
 * 一次性激活上下文（SpecHandle → FWeaponAbilityActivationContext）。
 */
UCLASS()
class UMHGZAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()

public:
	UMHGZAbilitySystemComponent();

	// ═══════════════════════════════════════════
	// 配置
	// ═══════════════════════════════════════════

	/** 核心能力列表（InitializeAbilitySystem 时授予） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability|Core")
	TArray<TSubclassOf<UGameplayAbility>> CoreAbilities;

	/** 核心 GE 列表（InitializeAbilitySystem 时 Apply） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability|Core")
	TArray<TSubclassOf<UGameplayEffect>> CoreAttributeEffects;

	// ═══════════════════════════════════════════
	// 初始化
	// ═══════════════════════════════════════════

	/** 幂等：仅授予 CoreAbilities 并 Apply CoreAttributeEffects。 */
	UFUNCTION(BlueprintCallable, Category = "MHGZ|ASC")
	void InitializeAbilitySystem();

	// ═══════════════════════════════════════════
	// 武器 Ability 管理
	// ═══════════════════════════════════════════

	/** 授予武器 Ability（先移除旧的全部武器 Ability） */
	void GrantWeaponAbilities(const TArray<TSubclassOf<UGameplayAbility>>& Abilities);

	/** 移除所有武器授予的 Ability */
	void RemoveWeaponAbilities();

	/** 查找装备阶段已经授予的武器 Ability Handle。 */
	FGameplayAbilitySpecHandle FindWeaponAbilityHandle(
		TSubclassOf<UGameplayAbility> AbilityClass);

	/** 按 InputTag 查找已授予 Ability 的 Handle。 */
	FGameplayAbilitySpecHandle FindAbilityHandleByInputTag(const FGameplayTag& InputTag) const;

	/** 获取当前激活的连招协调器 */
	UGA_WeaponComboCoordinator* GetActiveComboCoordinator() const;

	/** 设置当前激活的连招协调器 */
	void SetActiveComboCoordinator(UGA_WeaponComboCoordinator* InCoordinator) { ActiveComboCoordinator = InCoordinator; }

	// ═══════════════════════════════════════════
	// RuntimeHost
	// ═══════════════════════════════════════════

	void SetRuntimeHost(UMHGZWeaponRuntimeHostComponent* InHost);

	UMHGZWeaponRuntimeHostComponent* GetRuntimeHost() const;

	// ═══════════════════════════════════════════
	// 输入快照路由（M1）
	// ═══════════════════════════════════════════

	/**
	 * 解析后的输入快照：
	 * Input.Weapon 前缀 → 转发给 ActiveComboCoordinator；
	 * 其余 → 按 UMHGZGameplayAbility::InputTag 精确匹配，注册一次性激活上下文后 TryActivateAbility。
	 */
	void HandleResolvedInputSnapshot(const FWeaponInputSnapshot& Snapshot);

	/** 输入释放 → 委托给当前 RuntimeHost 的 Active Action 注册表。 */
	void HandleResolvedInputRelease(const FWeaponInputSnapshot& Snapshot);

	// ═══════════════════════════════════════════
	// 一次性激活上下文（SpecHandle 键控）
	// ═══════════════════════════════════════════

	/** 注册待消费的激活上下文；同 Handle 重复注册以最新为准。 */
	void PrepareWeaponAbilityActivation(
		const FGameplayAbilitySpecHandle& Handle,
		const FWeaponAbilityActivationContext& Context);

	/** 精确一次性消费：返回 true 时移除并写出；重复消费返回 false。 */
	bool ConsumePendingActivationContext(
		const FGameplayAbilitySpecHandle& Handle,
		FWeaponAbilityActivationContext& OutContext);

protected:
	virtual void BeginPlay() override;

private:
	/** 已授予的武器 Ability Handle */
	TArray<FGameplayAbilitySpecHandle> WeaponAbilityHandles;

	/** 连招协调器引用 */
	UPROPERTY()
	TObjectPtr<UGA_WeaponComboCoordinator> ActiveComboCoordinator;

	/** 当前武器运行时所有者（由 RuntimeHost 初始化时写入） */
	TWeakObjectPtr<UMHGZWeaponRuntimeHostComponent> RuntimeHost;

	/** 待消费的激活上下文；Register 覆盖、Consume 一次性移除。 */
	TMap<FGameplayAbilitySpecHandle, FWeaponAbilityActivationContext> PendingActivationContexts;

	/** 是否已经授予核心能力并应用核心 GE。 */
	bool bAbilitySystemInitialized = false;
};
