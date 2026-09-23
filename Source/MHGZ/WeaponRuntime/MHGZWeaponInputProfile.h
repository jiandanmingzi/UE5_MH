// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "WeaponRuntime/MHGZWeaponRuntimeTypes.h"
#include "MHGZWeaponInputProfile.generated.h"

class UInputAction;

/** 决定一个 Chord 在物理按压期间还是在未消费的松开时派发。 */
UENUM(BlueprintType)
enum class EWeaponChordDispatchPolicy : uint8
{
	OnPress UMETA(DisplayName = "On Press"),
	OnReleaseIfUnconsumed UMETA(DisplayName = "On Release If Unconsumed")
};

/**
 * 组合键声明：全部 TriggerControls 必须在 ChordGracePeriod 内 Started，
 * RequiredHeldModifiers 可提前长按或在 Trigger 候选等待期内最后补齐。
 */
USTRUCT(BlueprintType)
struct FWeaponChordDefinition
{
	GENERATED_BODY()

	/** 组合解析成功后的唯一 ResolvedInputTag */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chord")
	FGameplayTag OutputTag;

	/** 需要在 GracePeriod 内组成的普通触发键（PhysicalInputTag）；至少 1 个 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chord")
	TArray<FGameplayTag> TriggerControls;

	/** LT/RT 等可提前长按的修饰键（PhysicalInputTag） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chord")
	TArray<FGameplayTag> RequiredHeldModifiers;

	/** 解析瞬间必须全部存在的姿态/瞄准上下文；用于收刀 RT 等状态限定输入。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chord|Context")
	FGameplayTagContainer RequiredContextTags;

	/** 解析瞬间任一存在即拒绝该 Chord 的上下文。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chord|Context")
	FGameplayTagContainer BlockedContextTags;

	/** 默认 true：额外 LT/RT 会阻止该 Chord */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chord")
	bool bRequireExactModifiers = true;

	/**
	 * 默认 false：修饰键可以在触发键的等待窗口内**最后补齐** —— 这是「RT 后按也能凑成
	 * RT+A」的实现方式，也是 `Input.Weapon.RTA` 一直以来的行为。
	 *
	 * true：修饰键必须**先于**触发键按下；触发键先到该 Chord 就永久不成立 —— 既不形成
	 * （`IsChordComplete`），也不参与「更好的 Chord 可能凑成」的推迟判定
	 * （`IsPossiblyCompletable`）。
	 *
	 * 它解决的是「一个物理键既要做单独动作、又是组合键成员」的延迟问题：A 单独是
	 * 闪避/空中回避，RT+A 是撑杆跳。开着这条，A 单独按下时不必再为等 RT 拖满一个
	 * `ChordGracePeriod`；代价是「先按 A 再补 RT」不再凑成组合键 —— 要出组合键必须
	 * **先按住 RT**。默认 false 保证既有 Chord 的行为逐字不变。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chord")
	bool bModifiersMustPrecedeTriggers = false;

	/** 候选排序权重（数值越大越优先） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chord")
	int32 Priority = 0;

	/** 形成组合后是否消费 TriggerControls */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chord")
	bool bConsumeTriggerControls = true;

	/** OnPress 立即派发；OnReleaseIfUnconsumed 只在本次物理按压未被 OnPress Chord 消费时于松开派发。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chord")
	EWeaponChordDispatchPolicy DispatchPolicy = EWeaponChordDispatchPolicy::OnPress;

	/** 需要释放身份的动作显式指定的释放键；必须是本 Chord 的 Trigger 或 Modifier */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chord")
	FGameplayTag ReleaseControlTag;

	/**
	 * 除 ReleaseControlTag 外，同一 Chord 还应派发 Completed 的物理键。
	 * 适用于“任一组合键松开即释放蓄力”这类动作；每个 Tag 同样必须属于
	 * TriggerControls 或 RequiredHeldModifiers。空数组保持既有单一 ReleaseControlTag 行为。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chord")
	TArray<FGameplayTag> AdditionalReleaseControlTags;

	/** 组合解析时调用 AimComponent 捕获瞄准快照的上下文；None 表示不捕获 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chord")
	EWeaponAimSnapshotContext AimSnapshotContext = EWeaponAimSnapshotContext::None;
};

/**
 * UWeaponInputProfile —— 单种武器的通用输入声明。
 * 只描述键与通用上下文；武器动作选择仍在 ComboData，不在这里写动作类。
 */
UCLASS(BlueprintType)
class MHGZ_API UWeaponInputProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** RawAction（Enhanced Input 输入动作）-> PhysicalInputTag */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TMap<TObjectPtr<UInputAction>, FGameplayTag> RawActionToPhysicalInputTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input|Chords")
	TArray<FWeaponChordDefinition> Chords;

	/** 组合键宽限期（秒）；必须 > 0 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input|Chords", meta = (ClampMin = "0.01", UIMin = "0.01"))
	float ChordGracePeriod = 0.25f;

	/** 判定“前”的世界方向输入阈值；[0, 1] */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input|Direction", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DirectionInputThreshold = 0.5f;

	/** 前方判定锥半角（度）；[0, 180] */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input|Direction", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float ForwardConeHalfAngle = 45.0f;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
