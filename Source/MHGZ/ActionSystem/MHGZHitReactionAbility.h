// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "MHGZHitReactionAbility.generated.h"

class UAbilityTask_PlayMontageAndWait;
class UAnimMontage;
class UAnimSequenceBase;

/** Direction of the attacking source in the defender's local XY plane. */
UENUM(BlueprintType)
enum class EMHGZLightHitDirection : uint8
{
	Forward,
	Back,
	Left,
	Right
};

/**
 * Event-driven, player-only light-hit reaction.
 *
 * This deliberately derives directly from UGameplayAbility: a HitStagger event
 * is not a weapon action and therefore must not consume an input snapshot,
 * reserve weapon resources, or require a live ActionToken.  It is guaranteed
 * as an infrastructure Core ability by UMHGZAbilitySystemComponent.
 */
UCLASS(BlueprintType, Blueprintable)
class MHGZ_API UMHGZHitReactionAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UMHGZHitReactionAbility();

	/** Source locations on the local +X/-X/+Y/-Y axes select Forward/Back/Right/Left. */
	static EMHGZLightHitDirection ResolveDirection(const FVector& SourceLocation,
		const FVector& TargetLocation, const FRotator& TargetRotation);

	/** Combat.Stagger.Light/Medium/Heavy map to 1/2/3; other tags map to zero. */
	static int32 GetStaggerPriority(const FGameplayTag& StaggerTag);

	/** Light is interrupted only by Medium/Heavy; Medium only by Heavy; Heavy only by Heavy. */
	static bool CanIncomingStaggerInterrupt(int32 IncomingPriority,
		int32 ActivePriority);

	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags,
		const FGameplayTagContainer* TargetTags,
		FGameplayTagContainer* OptionalRelevantTags) const override;

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility, bool bWasCancelled) override;

	/** 持刀向前轻受击的导入动画序列，播放路径不要求另建常驻蒙太奇。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HitReaction|Light")
	TSoftObjectPtr<UAnimSequenceBase> LightForwardSequence;

	/** 持刀向后轻受击的导入动画序列。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HitReaction|Light")
	TSoftObjectPtr<UAnimSequenceBase> LightBackSequence;

	/** 持刀向左轻受击的导入动画序列。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HitReaction|Light")
	TSoftObjectPtr<UAnimSequenceBase> LightLeftSequence;

	/** 持刀向右轻受击的导入动画序列。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HitReaction|Light")
	TSoftObjectPtr<UAnimSequenceBase> LightRightSequence;

	/** 轻受击动画播放时使用的Slot名。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HitReaction|Playback")
	FName MontageSlotName = FName(TEXT("DefaultSlot"));

	/** 该表现的混入时长，单位为秒。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HitReaction|Playback",
		meta = (ClampMin = "0.0"))
	float BlendInTime = 0.05f;

	/** 该表现的混出时长，单位为秒。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HitReaction|Playback",
		meta = (ClampMin = "0.0"))
	float BlendOutTime = 0.08f;

private:
	const TSoftObjectPtr<UAnimSequenceBase>& GetLightSequence(
		EMHGZLightHitDirection Direction) const;
	bool IsSuppressedByActiveReaction(int32 IncomingPriority) const;
	void CancelActivePlayerActions();
	void FinishReaction(bool bWasCancelled);

	UFUNCTION()
	void OnMontageCompleted();

	UFUNCTION()
	void OnMontageInterrupted();

	UFUNCTION()
	void OnMontageCancelled();

	/** 当前蒙太奇播放任务的运行时引用；随本次能力清理。 */
	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> MontageTask;

	/** 本次轻受击播放路径持有的动态蒙太奇引用。 */
	UPROPERTY()
	TObjectPtr<UAnimMontage> ActiveDynamicMontage;

	/** Priority owned by this live execution, used to arbitrate a later event. */
	int32 ActiveStaggerPriority = 0;

	bool bEndingReaction = false;
};
