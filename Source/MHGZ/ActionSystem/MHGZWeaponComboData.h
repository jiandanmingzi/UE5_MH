// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Misc/DataValidation.h"
#include "WeaponRuntime/MHGZWeaponRuntimeTypes.h"
#include "MHGZWeaponComboData.generated.h"

class UGameplayAbility;

UENUM(BlueprintType)
enum class EComboExecutionPolicy : uint8
{
	ActivateAbility,
	StateOnly
};

UENUM(BlueprintType)
enum class EComboStatePolicy : uint8
{
	Replace,
	Preserve
};

UENUM(BlueprintType)
enum class EComboLandingPolicy : uint8
{
	ResetToIdle,
	AbilityOwned
};

UENUM(BlueprintType)
enum class ETransitionGrantTiming : uint8
{
	OnActivation,
	OnFirstHit
};

/**
 * 连招状态机的一条有向边。
 *
 * 该结构是资产序列化的最终名称。旧 FComboNode 只通过 CoreRedirect 迁移，
 * 不再保留第二套运行时结构。
 */
USTRUCT(BlueprintType)
struct FComboTransition
{
	GENERATED_BODY()

	/** 资产内稳定且唯一的边身份。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity", meta = (ToolTip = "该连招边的稳定唯一标识。请勿在同一 Combo 表中重复；用于边匹配、动作上下文和运行时诊断。"))
	FName TransitionID;

	/** 源状态；bMatchAnyState=true 时忽略。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match", meta = (ToolTip = "这条边允许从哪个当前连招状态出发。若启用“匹配任意源状态”，此字段不参与匹配。"))
	FName SourceState;

	/** 启用后，这条边可从任意源状态匹配；仍会检查输入、标签、窗口等其他条件。可用“排除的源状态”列出例外。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match", meta = (ToolTip = "启用后，这条边可从任意源状态匹配；仍会检查输入、标签、窗口等其他条件。可用“排除的源状态”列出例外。"))
	bool bMatchAnyState = false;

	/** 仅 bMatchAnyState=true 时生效。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match", meta = (EditCondition = "bMatchAnyState", ToolTip = "启用“匹配任意源状态”时，列出不允许作为起点的状态；关闭任意源状态匹配时此列表不生效。"))
	TArray<FName> BlockedSourceStates;

	/** 自动转移必须为空，输入转移必须有效。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match", meta = (ToolTip = "触发这条输入边的逻辑 Gameplay Tag。普通输入边必须设置有效标签；自动转移边必须留空。"))
	FGameplayTag InputTag;

	/** 输入方向筛选。None 表示不限制方向，并不表示要求摇杆回中；其他值要求方向输入符合对应方向。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match", meta = (ToolTip = "输入方向筛选。None 表示不限制方向，并不表示要求摇杆回中；其他值要求方向输入符合对应方向。"))
	EDirectionalInput Direction = EDirectionalInput::None;

	/** 执行方式：ActivateAbility 会激活下方指定的能力；StateOnly 只执行状态转移，不激活能力，仅适用于自动状态边。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Execution", meta = (ToolTip = "执行方式：ActivateAbility 会激活下方指定的能力；StateOnly 只执行状态转移，不激活能力，仅适用于自动状态边。"))
	EComboExecutionPolicy ExecutionPolicy = EComboExecutionPolicy::ActivateAbility;

	/** 这条边要激活的 Gameplay Ability 类。ActivateAbility 必须指定有效的 MHGZ 能力类；StateOnly 必须留空。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Execution", meta = (ToolTip = "这条边要激活的 Gameplay Ability 类。ActivateAbility 必须指定有效的 MHGZ 能力类；StateOnly 必须留空。"))
	TSubclassOf<UGameplayAbility> AbilityClass;

	/** 该边覆盖目标 Montage 的混入时长（秒）。-1 表示沿用 Montage 资产默认值；0 表示硬切；正值指定本边的混入时长。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Execution",
		meta = (ClampMin = "-1.0", ForceUnits = "s", ToolTip = "该边覆盖目标 Montage 的混入时长（秒）。-1 表示沿用 Montage 资产默认值；0 表示硬切；正值指定本边的混入时长。"))
	float MontageBlendInTime = -1.0f;

	/** 激活时单次朝向修正的最大转角。-1 沿用目标攻击 GA 的 MaxCorrectionAngle；0 禁用本边修正。目标偏差超过此值但不超过 90 度余量时，仍按此最大转角修正；超过余量则不修正。有效范围为 0 到 180 度。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Execution",
		meta = (ClampMin = "-1.0", ClampMax = "180.0", ForceUnits = "deg", ToolTip = "激活时单次朝向修正的最大转角。-1 沿用目标攻击 GA 的 MaxCorrectionAngle；0 禁用本边修正。目标偏差超过此值但不超过 90 度余量时，仍按此最大转角修正；超过余量则不修正。有效范围为 0 到 180 度。"))
	float MaxCorrectionAngle = -1.0f;

	/** Replace 状态策略完成后切换到的目标连招状态。使用 Replace 时必须填写；Preserve 会保留当前状态并忽略此字段。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State", meta = (ToolTip = "Replace 状态策略完成后切换到的目标连招状态。使用 Replace 时必须填写；Preserve 会保留当前状态并忽略此字段。"))
	FName TargetState;

	/** Replace：将连招状态替换为目标状态；Preserve：保留当前状态。Preserve 边不能配置 GrantedTags。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State", meta = (ToolTip = "Replace：将连招状态替换为目标状态；Preserve：保留当前状态。Preserve 边不能配置 GrantedTags。"))
	EComboStatePolicy StatePolicy = EComboStatePolicy::Replace;

	/** 空中能力落地时的处理方式。ResetToIdle 由协调器重置到 Idle；AbilityOwned 将落地交给当前空中能力处理，仅用于明确配置的空中状态。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State", meta = (ToolTip = "空中能力落地时的处理方式。ResetToIdle 由协调器重置到 Idle；AbilityOwned 将落地交给当前空中能力处理，仅用于明确配置的空中状态。"))
	EComboLandingPolicy LandingPolicy = EComboLandingPolicy::ResetToIdle;

	/** 边可匹配所必须具备的 Gameplay Tags；容器中的所有标签都必须满足。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Requirements", meta = (ToolTip = "边可匹配所必须具备的 Gameplay Tags；容器中的所有标签都必须满足。"))
	FGameplayTagContainer RequiredTags;

	/** 边匹配时禁止存在的 Gameplay Tags；只要命中其中任意一个标签，这条边就不可用。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Requirements", meta = (ToolTip = "边匹配时禁止存在的 Gameplay Tags；只要命中其中任意一个标签，这条边就不可用。"))
	FGameplayTagContainer BlockedTags;

	/** 匹配门槛，不负责实际扣除。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Requirements", meta = (ClampMin = "0.0", ToolTip = "匹配这条边所需的最低耐力值。这里只做可用性检查，不会扣除耐力。"))
	float StaminaRequired = 0.f;

	/** 启用后，只有当前动作对应的 Combo Window 已开启时才能匹配；窗口关闭时输入可按系统预输入配置暂存。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Requirements", meta = (ToolTip = "启用后，只有当前动作对应的 Combo Window 已开启时才能匹配；窗口关闭时输入可按系统预输入配置暂存。"))
	bool bRequiresComboWindow = false;

	/** 启用后，要求当前活动攻击的精确 ActionToken 持有已开启的 DodgeAcceptWindow；仅有聚合的 DodgeAcceptOpen 标签不够。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Requirements", meta = (ToolTip = "启用后，要求当前活动攻击的精确 ActionToken 持有已开启的 DodgeAcceptWindow；仅有聚合的 DodgeAcceptOpen 标签不够。"))
	bool bRequiresDodgeAcceptWindow = false;

	/** 决定 GrantedTags 的授予时机：OnActivation 在能力激活时授予；OnFirstHit 在本动作首次有效命中时授予。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tags", meta = (ToolTip = "决定 GrantedTags 的授予时机：OnActivation 在能力激活时授予；OnFirstHit 在本动作首次有效命中时授予。"))
	ETransitionGrantTiming GrantTiming = ETransitionGrantTiming::OnActivation;

	/** 由这条边授予并在转移生命周期内管理的 Gameplay Tags。Preserve 状态策略不允许配置此字段。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tags", meta = (ToolTip = "由这条边授予并在转移生命周期内管理的 Gameplay Tags。Preserve 状态策略不允许配置此字段。"))
	FGameplayTagContainer GrantedTags;

	/** 标记为自动转移边。自动边不由 InputTag 触发，必须留空 InputTag，并遵守自动状态转移的配置约束。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Execution", meta = (ToolTip = "标记为自动转移边。自动边不由 InputTag 触发，必须留空 InputTag，并遵守自动状态转移的配置约束。"))
	bool bAutoTransition = false;

	/** 多条边同时满足匹配条件时的优先级；数值越大越优先。条件和优先级仍完全相同的边应避免并存。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match", meta = (ToolTip = "多条边同时满足匹配条件时的优先级；数值越大越优先。条件和优先级仍完全相同的边应避免并存。"))
	int32 Priority = 0;

};

/** 武器连招图：平面边数组，运行时由协调器建立 SourceState 索引。 */
UCLASS(BlueprintType)
class UMHGZWeaponComboData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 武器种类标签，用于匹配该武器的连招或资源配置。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FGameplayTag WeaponTypeTag;

	/** 本武器所有合法输入或自动连招转移边。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combo")
	TArray<FComboTransition> Transitions;

	/** 唯一安全兜底；不是动作正常结束手段。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combo", meta = (ClampMin = "0.0"))
	float GlobalComboTimeout = 10.0f;

	/**
	 * 输入在接受窗口开启前可保留的最长时间。0 关闭预输入；缓冲始终只有一条，后输入覆盖前输入。
	 * 物理和弦先由 Input Router 解析完成，计时从解析后的逻辑输入到达 Coordinator 时开始。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combo", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float PreInputLifetime = 0.5f;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
