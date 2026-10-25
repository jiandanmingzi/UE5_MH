// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MHGZGameplayAbility.h"
#include "MHGZDodgeAbility.generated.h"

class UAbilityTask_PlayMontageAndWait;
class UAnimMontage;
class UCapsuleComponent;
class ACharacter;
class UAbilitySystemComponent;

UENUM(BlueprintType)
enum class EMHGZDodgeMovementPhase : uint8
{
	LockedRootMotion,
	SteeringRootMotion,
	MotionMatching
};

/** Snapshot-driven general dodge. It does not enter the weapon combo table. */
UCLASS(BlueprintType, Blueprintable)
class MHGZ_API UMHGZDodgeAbility : public UMHGZGameplayAbility
{
	GENERATED_BODY()

public:
	UMHGZDodgeAbility();

	/** 开启后，前翻滚要求实时Attacking；无方向输入也使用此项。攻击中仍须通过原攻击的DodgeAcceptWindow。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dodge|Activation",
		meta = (DisplayName = "前翻滚需要攻击状态"))
	bool bForwardDodgeRequiresAttacking = false;

	/** 开启后，左翻滚要求实时Attacking；关闭不解除收刀方向限制或攻击取消窗口要求。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dodge|Activation",
		meta = (DisplayName = "左翻滚需要攻击状态"))
	bool bLeftDodgeRequiresAttacking = true;

	/** 开启后，右翻滚要求实时Attacking；关闭不解除收刀方向限制或攻击取消窗口要求。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dodge|Activation",
		meta = (DisplayName = "右翻滚需要攻击状态"))
	bool bRightDodgeRequiresAttacking = true;

	/** 开启后，后翻滚要求实时Attacking；关闭不解除收刀方向限制或攻击取消窗口要求。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dodge|Activation",
		meta = (DisplayName = "后翻滚需要攻击状态"))
	bool bBackDodgeRequiresAttacking = true;

	/** 收刀状态使用的最终前向翻滚蒙太奇。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dodge|Sheathed")
	TSoftObjectPtr<UAnimMontage> SheathedDodgeMontage;

	/** 持刀状态使用的最终前向翻滚蒙太奇。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dodge|Unsheathed")
	TSoftObjectPtr<UAnimMontage> UnsheathedDodgeMontage;

	/** 持刀状态使用的最终左向翻滚蒙太奇。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dodge|Unsheathed")
	TSoftObjectPtr<UAnimMontage> UnsheathedLeftDodgeMontage;

	/** 持刀状态使用的最终右向翻滚蒙太奇。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dodge|Unsheathed")
	TSoftObjectPtr<UAnimMontage> UnsheathedRightDodgeMontage;

	/** 持刀状态使用的最终后向翻滚蒙太奇。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dodge|Unsheathed")
	TSoftObjectPtr<UAnimMontage> UnsheathedBackDodgeMontage;

	/** 翻滚核心动作Section名。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dodge|Sections")
	FName DodgeCoreSectionName = FName(TEXT("DodgeCore"));

	/** 翻滚结束后按无移动输入退出的Section名。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dodge|Sections")
	FName IdleExitSectionName = FName(TEXT("IdleExit"));

	/** 翻滚结束后按持续移动输入退出的Section名。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dodge|Sections")
	FName MoveExitSectionName = FName(TEXT("MoveExit"));

	/** 翻滚退出段接新动作的混合时间；左右后也用于接起步，前滚原移动交接不变。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dodge|Recovery", meta=(ClampMin="0.0", ForceUnits="s"))
	float DirectionalIdleExitBlendTime = 0.1f;

	/** 所有方向翻滚是否通过动作根运动通知窗口取得根运动所有权；成员名保留兼容旧资产，但当前开关作用于全部方向。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dodge|Motion Matching",
		meta = (DisplayName = "Dodge Uses Action Root Motion Phase"))
	bool bForwardDodgeUsesActionRootMotionPhase = false;

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

	bool BeginDodgeWindow(FName NotifyEventID);
	void EndDodgeWindow(FName NotifyEventID);

	/**
	 * Legacy exact-token helpers retained for tests and compatibility. Runtime
	 * Dodge exit selection uses this Ability's active-Montage SectionChanged
	 * callback instead of an AnimNotify registry lookup.
	 */
	bool DecideDodgeExit(const FWeaponActionToken& ActionToken);
	bool EnterMoveExit(const FWeaponActionToken& ActionToken);

	EMHGZDodgeMovementPhase GetMovementPhase() const { return MovementPhase; }
	FName GetChosenExitSection() const { return ChosenExitSection; }
	bool DoesActiveDodgeAllowMoveExit() const { return bActiveDodgeAllowsMoveExit; }

	/** Exact committed exit recoveries may yield to a successor; Core never qualifies. */
	bool IsActionRecoveryOpen() const;
	static UMHGZDodgeAbility* FindOpenActionRecovery(UAbilitySystemComponent* ASC,
		const UMHGZGameplayAbility* Exclude = nullptr);
	/** Movement-only recovery remains directional: both forward exits retain their authored route. */
	bool IsDirectionalRecoveryOpen() const;
	static UMHGZDodgeAbility* FindOpenDirectionalRecovery(UAbilitySystemComponent* ASC,
		const UMHGZGameplayAbility* Exclude = nullptr);
	bool TryCompleteDirectionalRecoveryForMovement();
	void CompleteDirectionalRecoveryForAction();

protected:
	struct FDodgeSelection
	{
		UAnimMontage* Montage = nullptr;
		bool bAllowMoveExit = false;
	};

	virtual bool ValidateActionDependencies() const override;
	virtual bool ValidateDodgeMontageDependencies() const;
	virtual bool StartDodgeMontage(ACharacter& Character, UAnimMontage* Montage,
		FName StartSection);
	/**
	 * Arms the deterministic DodgeCore -> IdleExit fallback after playback begins.
	 * This Ability's per-Montage SectionChanged callback redirects a forward roll
	 * with live input to MoveExit; directional rolls deliberately use the fallback.
	 */
	virtual bool ConfigureDodgeCoreFallbackExit();
	virtual bool HasLiveMovementInput() const;
	virtual bool JumpToDodgeSection(FName SectionName);
	virtual FDodgeSelection SelectDodgeSelection() const;
	bool DoesDodgeDirectionRequireAttacking(EDirectionalInput Direction) const;
	/** Preserve usable directional rolls; front-hemisphere inputs may use forward when a side roll is gated. */
	EDirectionalInput ResolveDodgeDirection(const FWeaponInputSnapshot& Input,
		const UAbilitySystemComponent* ASC, const UMHGZWeaponRuntimeHostComponent* Host) const;
	static bool IsDodgeDirectionAllowedForPose(bool bSheathed,
		EDirectionalInput Direction);
	void OnDodgeMontageSectionChanged(UAnimMontage* Montage, FName SectionName,
		bool bLooped);
	void HandleDodgeSectionEntered(FName SectionName);

	UFUNCTION()
	void OnMontageCompleted();

	UFUNCTION()
	void OnMontageBlendOut();

	UFUNCTION()
	void OnMontageInterrupted();

	/** Normal-blend fallback for assets/tasks that do not subsequently emit OnCompleted. */
	void OnMontageEndFailsafeExpired();

private:
	/** 当前蒙太奇播放任务的运行时引用；随本次能力清理。 */
	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> MontageTask;

	/** 本次实际播放的翻滚蒙太奇引用。 */
	UPROPERTY()
	TObjectPtr<UAnimMontage> ActiveDodgeMontage;

	TMap<FName, FWeaponOwnedTagToken> DodgeWindowTokens;
	TMap<TEnumAsByte<ECollisionChannel>, ECollisionResponse> CachedCollisionResponses;
	TWeakObjectPtr<UCapsuleComponent> DodgeCapsule;
	FWeaponOwnedTagToken DodgingToken;
	FWeaponOwnedTagToken BlockMovementToken;
	EMHGZDodgeMovementPhase MovementPhase = EMHGZDodgeMovementPhase::LockedRootMotion;
	FName ChosenExitSection = NAME_None;
	bool bActiveDodgeAllowsMoveExit = false;
	bool bOwnsMontageRootMotion = false;
	bool bPreparedAttackSupersede = false;
	bool bEndingDodge = false;
	bool bActionRecoveryOpen = false;
	FTimerHandle MontageEndFailsafeTimer;

	bool IsAttacking() const;
	void OpenActionRecovery();
	void ArmMontageEndFailsafe();
	void ClearMontageEndFailsafe();
	void ReleaseMontageRootMotionOwnership();
	void CancelPreparedAttackSupersede();
	void CloseAllDodgeWindows();
	void RestoreDodgeCollisionResponses();
};
