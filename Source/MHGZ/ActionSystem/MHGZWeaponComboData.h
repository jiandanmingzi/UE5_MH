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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tags")
	ETransitionGrantTiming GrantTiming = ETransitionGrantTiming::OnActivation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tags")
	FGameplayTagContainer GrantedTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Execution")
	bool bAutoTransition = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match")
	int32 Priority = 0;

	/**
	 * 仅用于把旧资产的 bool 语义迁移到 GrantTiming；运行时不得读取。
	 * E0 重存目标资产后由 M2 删除。
	 */
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use GrantTiming"))
	bool bRequiresHitToGrantTags = false;
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

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
	virtual void PostLoad() override;
};
