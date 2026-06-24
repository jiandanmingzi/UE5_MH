// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "MHGZWeaponComboData.generated.h"

class UGameplayAbility;

/**
 * 方向输入类型
 */
UENUM(BlueprintType)
enum class EDirectionalInput : uint8
{
	None    UMETA(DisplayName = "无方向"),
	Forward UMETA(DisplayName = "前"),
	Back    UMETA(DisplayName = "后"),
	Left    UMETA(DisplayName = "左"),
	Right   UMETA(DisplayName = "右")
};

/**
 * FComboNode — 连招表中的单个状态转换规则
 * 定义"从哪个状态 + 什么输入 → 激活哪个 GA → 转为什么状态"
 */
USTRUCT(BlueprintType)
struct FComboNode
{
	GENERATED_BODY()

	/** 源招式状态名（如 "Idle", "Slash_101"）。bMatchAnyState=true 时忽略 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName StateName;

	/** 是否匹配任意源状态 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bMatchAnyState = false;

	/** ★ 排除的源状态名（仅 bMatchAnyState=true 生效） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bMatchAnyState"))
	TArray<FName> BlockedStateNames;

	/** 输入标签（Input.Weapon.Y / Input.Weapon.B 等） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag InputTag;

	/** 激活的 GA 蓝图类 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSubclassOf<UGameplayAbility> AbilityClass;

	/** 需要满足的 Tag（全部存在才匹配） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTagContainer RequiredTags;

	/** 阻塞 Tag（任一存在则不匹配） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTagContainer BlockedTags;

	/** 转移后状态名 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName NextState;

	/** ★ 匹配优先级（数值越大越优先） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Priority = 0;

	/** 激活后授予的 Tag（首次命中时由协调器 Apply） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTagContainer GrantedTags;

	/** 耐力门槛（匹配前检查，不扣耐） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float StaminaRequired = 0.f;

	/** 方向输入 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EDirectionalInput DirectionalInput = EDirectionalInput::None;

	/** 是否需要命中才授予 Tag */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bRequiresHitToGrantTags = false;

	/** 是否需要翻滚窗口打开 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bRequiresWindowOpen = false;

	/** 是否自动转移到下一节点（ε 转移） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bAutoTransition = false;
};

/**
 * UMHGZWeaponComboData — 武器连招表 DataAsset
 * 平面数组 + StateIndex 索引——策划在一处可视化完整连招树
 */
UCLASS(BlueprintType)
class UMHGZWeaponComboData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 武器种类 Tag */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FGameplayTag WeaponTypeTag;

	/** ★ 连招节点数组 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FComboNode> ComboTable;

	/** 全局连招超时（秒）——唯一安全兜底 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float GlobalComboTimeout = 10.0f;

	// ── FPrimaryDataAsset 覆写 ──
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
};
