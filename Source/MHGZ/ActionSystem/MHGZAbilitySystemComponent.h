// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "GameplayTagContainer.h"
#include "MHGZAbilitySystemComponent.generated.h"

class UInputAction;
class UGameplayAbility;
class UGameplayEffect;
class UMHGZWeaponComboData;
class UGA_WeaponComboCoordinator;

/**
 * FAbilityInputBinding — 输入-技能绑定
 * 策划在蓝图中配置
 */
USTRUCT(BlueprintType)
struct FAbilityInputBinding
{
	GENERATED_BODY()

	/** EnhancedInput 的 InputAction 资产 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	TObjectPtr<UInputAction> InputAction;

	/** 触发时激活的 Ability Tag，也作为蓄力 GA 的 Completed 事件标识 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	FGameplayTag AbilityTag;

	/** 触发后是否消耗此次输入 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	bool bConsumeInput = true;
};

/**
 * UMHGZAbilitySystemComponent — 扩展 ASC
 * 增加输入绑定、批量授予能力、统一派发路由
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

	/** 输入绑定列表（策划在蓝图中配置） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TArray<FAbilityInputBinding> InputBindings;

	/** 核心能力列表（BeginPlay 时自动授予） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability|Core")
	TArray<TSubclassOf<UGameplayAbility>> CoreAbilities;

	/** 核心 GE 列表（BeginPlay 时自动 Apply） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability|Core")
	TArray<TSubclassOf<UGameplayEffect>> CoreAttributeEffects;

	// ═══════════════════════════════════════════
	// 初始化
	// ═══════════════════════════════════════════

	/**
	 * 初始化 Ability 系统
	 * 依次执行：设置初始 Tag → 授予 CoreAbilities → Apply CoreAttributeEffects → 绑定输入
	 */
	UFUNCTION(BlueprintCallable, Category = "MHGZ|ASC")
	void InitializeAbilitySystem();

	// ═══════════════════════════════════════════
	// 武器 Ability 管理
	// ═══════════════════════════════════════════

	/** 授予武器 Ability */
	void GrantWeaponAbilities(const TArray<TSubclassOf<UGameplayAbility>>& Abilities);

	/** 移除所有武器授予的 Ability */
	void RemoveWeaponAbilities();

	/** 查找装备阶段已经授予的武器 Ability Handle。 */
	FGameplayAbilitySpecHandle FindWeaponAbilityHandle(
		TSubclassOf<UGameplayAbility> AbilityClass);

	/** 获取当前激活的连招协调器 */
	UGA_WeaponComboCoordinator* GetActiveComboCoordinator() const;

	/** 设置当前激活的连招协调器 */
	void SetActiveComboCoordinator(UGA_WeaponComboCoordinator* InCoordinator) { ActiveComboCoordinator = InCoordinator; }

	// ═══════════════════════════════════════════
	// 输入绑定
	// ═══════════════════════════════════════════

	/** 运行时动态绑定/替换单个 IA→Tag 映射 */
	UFUNCTION(BlueprintCallable, Category = "MHGZ|Input")
	void BindInputAction(UInputAction* Action, FGameplayTag AbilityTag);

protected:
	virtual void BeginPlay() override;

	// ═══════════════════════════════════════════
	// 输入回调
	// ═══════════════════════════════════════════

	/** Triggered 事件——通过 Instance 获取来源 Action，查 ActionToTag 路由 */
	UFUNCTION()
	void OnInputActionTriggered(const FInputActionInstance& Instance);

	/** Completed 事件——蓄力释放处理 */
	UFUNCTION()
	void OnInputActionCompleted(const FInputActionInstance& Instance);

	/** Action→Tag 映射（由 BindAction 构建） */
	UPROPERTY()
	TMap<TObjectPtr<UInputAction>, FGameplayTag> ActionToTag;

	/** 是否已绑定 EnhancedInput（避免 PossessedBy 重新调用时重复绑定） */
	bool bInputBound = false;

	/** 是否已经授予初始能力并应用初始 GE。 */
	bool bAbilitySystemInitialized = false;

private:
	/** 已授予的武器 Ability Handle */
	TArray<FGameplayAbilitySpecHandle> WeaponAbilityHandles;

	/** 连招协调器引用 */
	UPROPERTY()
	TObjectPtr<UGA_WeaponComboCoordinator> ActiveComboCoordinator;

	friend class UMHGZEquipmentComponent;
};
