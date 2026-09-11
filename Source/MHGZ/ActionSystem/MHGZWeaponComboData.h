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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	FName TransitionID;

	/** 源状态；bMatchAnyState=true 时忽略。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match")
	FName SourceState;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match")
	bool bMatchAnyState = false;

	/** 仅 bMatchAnyState=true 时生效。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match", meta = (EditCondition = "bMatchAnyState"))
	TArray<FName> BlockedSourceStates;

	/** 自动转移必须为空，输入转移必须有效。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match")
	FGameplayTag InputTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match")
	EDirectionalInput Direction = EDirectionalInput::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Execution")
	EComboExecutionPolicy ExecutionPolicy = EComboExecutionPolicy::ActivateAbility;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Execution")
	TSubclassOf<UGameplayAbility> AbilityClass;

	/**
	 * Per-transition Montage Blend In override in seconds. Negative inherits the
	 * target Montage asset's default; zero is an intentional hard cut.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Execution",
		meta = (ClampMin = "-1.0", ForceUnits = "s"))
	float MontageBlendInTime = -1.0f;

	/**
	 * Per-transition activation yaw-correction cap in degrees. Negative inherits
	 * the target attack GA's MaxCorrectionAngle; zero intentionally disables the
	 * activation-time correction for this edge.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Execution",
		meta = (ClampMin = "-1.0", ClampMax = "180.0", ForceUnits = "deg"))
	float MaxCorrectionAngle = -1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State")
	FName TargetState;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State")
	EComboStatePolicy StatePolicy = EComboStatePolicy::Replace;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State")
	EComboLandingPolicy LandingPolicy = EComboLandingPolicy::ResetToIdle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Requirements")
	FGameplayTagContainer RequiredTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Requirements")
	FGameplayTagContainer BlockedTags;

	/** 匹配门槛，不负责实际扣除。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Requirements", meta = (ClampMin = "0.0"))
	float StaminaRequired = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Requirements")
	bool bRequiresComboWindow = false;

	/**
	 * Requires the exact active attack ActionToken to own an open
	 * AnimNotifyState_DodgeAcceptWindow. This is intentionally stricter than
	 * testing the aggregate DodgeAcceptOpen gameplay tag.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Requirements")
	bool bRequiresDodgeAcceptWindow = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tags")
	ETransitionGrantTiming GrantTiming = ETransitionGrantTiming::OnActivation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tags")
	FGameplayTagContainer GrantedTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Execution")
	bool bAutoTransition = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match")
	int32 Priority = 0;

};

/** 武器连招图：平面边数组，运行时由协调器建立 SourceState 索引。 */
UCLASS(BlueprintType)
class UMHGZWeaponComboData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FGameplayTag WeaponTypeTag;

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
