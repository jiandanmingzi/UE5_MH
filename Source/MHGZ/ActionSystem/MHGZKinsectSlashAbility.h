// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/DataValidation.h"
#include "InsectGlaive/IGActionConfigTypes.h"
#include "ActionSystem/MHGZInsectGlaiveAbility.h"
#include "WeaponRuntime/MHGZWeaponRuntimeTypes.h"
#include "MHGZKinsectSlashAbility.generated.h"

class UCurveFloat;
class UAbilityTask_MHGZKinsectSlashProbe;
class UAbilityTask_MHGZPlayMontageAndWait;
class UAbilityTask_MHGZWeaponMovement;
class UAbilitySystemComponent;
class UAnimMontage;
class UCharacterMovementComponent;
class USkeletalMeshComponent;
class UMHGZWeaponRuntimeHostComponent;

UENUM(BlueprintType)
enum class EKinsectSlashAimSource : uint8
{
	None,
	WeaponB,
	KinsectAim
};

UENUM(BlueprintType)
enum class EKinsectSlashMontageEvent : uint8
{
	AimLock,
	DashBegin,
	// Deprecated no-op values retained at their original numeric positions.
	LateHitOpen,
	LateHitClose,
	HoldMissPose,
	DanceBegin
};

UENUM(BlueprintType)
enum class EKinsectSlashPhase : uint8
{
	Inactive,
	Hover,
	Dash,
	MissFall,
	DancePending,
	DanceFromDash,
	DanceFromMiss,
	Ended
};

/** Both 189 hit entries share the captured trajectory; each remains editable. */
USTRUCT(BlueprintType)
struct FKinsectSlashDanceEntryTuning
{
	GENERATED_BODY()

	/** 是否按录制校准后的准备与飞行物理参数生成舞踏轨迹。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement")
	bool bUseCapturedTrajectory = true;

	/** 舞踏起跳前准备阶段的时长，单位为秒。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement",
		meta = (ClampMin = "0.001", ForceUnits = "s"))
	float PrepDuration = 0.35f;

	/** 舞踏飞行阶段采用的时长，单位为秒。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement",
		meta = (ClampMin = "0.001", ForceUnits = "s", EditCondition = "bUseCapturedTrajectory"))
	float FlightDuration = 1.1833333f;

	/** 舞踏起跳时的竖直向上速度，单位为厘米每秒。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement",
		meta = (ClampMin = "0.001", EditCondition = "bUseCapturedTrajectory"))
	float LaunchUpSpeed = 1080.0f;

	/** 舞踏起跳时沿锁定朝向后方的水平速度，单位为厘米每秒。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement",
		meta = (ClampMin = "0.001", EditCondition = "bUseCapturedTrajectory"))
	float LaunchBackwardSpeed = 300.0f;

	/** 舞踏飞行的重力正幅值，单位为厘米每平方秒。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement",
		meta = (ClampMin = "0.001", EditCondition = "bUseCapturedTrajectory"))
	float GravityMagnitude = 1800.0f;

	/** 舞踏沿锁定朝向后方的水平加速度，单位为厘米每平方秒。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement",
		meta = (ClampMin = "0.001", EditCondition = "bUseCapturedTrajectory"))
	float BackwardAcceleration = 360.0f;

	/** 旧抛体参数的解释模式；仅对应回退路径读取，保留字段名用于加载旧序列化配置。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Legacy",
		meta = (DeprecatedProperty, DeprecationMessage = "Use the captured 189 trajectory.",
			EditCondition = "!bUseCapturedTrajectory"))
	EBallisticParameterMode ParameterMode = EBallisticParameterMode::ApexHeightAndDuration;

	/** 旧抛体回退配置的最高点相对高度，单位为厘米。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Legacy",
		meta = (ClampMin = "0.0", DeprecatedProperty,
			DeprecationMessage = "Use LaunchUpSpeed and GravityMagnitude.",
			EditCondition = "!bUseCapturedTrajectory"))
	float ApexHeight = 0.0f;

	/** 旧抛体回退配置的飞行时长，单位为秒。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Legacy",
		meta = (ClampMin = "0.0", DeprecatedProperty, DeprecationMessage = "Use FlightDuration.",
			EditCondition = "!bUseCapturedTrajectory"))
	float Duration = 0.0f;

	/** 旧抛体回退配置的水平移动距离，单位为厘米。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Legacy",
		meta = (ClampMin = "0.0", DeprecatedProperty,
			DeprecationMessage = "Use LaunchBackwardSpeed and BackwardAcceleration.",
			EditCondition = "!bUseCapturedTrajectory"))
	float HorizontalDistance = 0.0f;

	/** 旧抛体回退模式显式给定的发射速度，单位为厘米每秒。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Legacy",
		meta = (DeprecatedProperty, DeprecationMessage = "Use the captured 189 trajectory.",
			EditCondition = "!bUseCapturedTrajectory"))
	FVector ExplicitLaunchVelocity = FVector::ZeroVector;

	MHGZ_API bool IsValid() const;
	FWeaponMovementRequest MakeRequest(const FWeaponActionToken& ActionToken,
		const FVector& LaunchDirection, UObject* CurveOuter = nullptr) const;
};

/**
 * Production GA for both aerial B and RT+B Insect Glaive Kinsect Slash paths.
 * One committed Action owns 186 -> 187 -> 188 or 189, the real temporarily
 * attached Kinsect, three AttackCollision windows, movement tasks, aerial
 * budget, damage/extract effects, and all notify callbacks.
 */
UCLASS(BlueprintType, Blueprintable)
class MHGZ_API UMHGZKinsectSlashAbility : public UMHGZInsectGlaiveAbility
{
	GENERATED_BODY()

public:
	// 专属数值随本GA保存；Combat同名旧字段不再参与运行时。
	/** 操虫斩187段实录位移配置，包含停留、直冲和滑翔阶段；单B与RT+B共用。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "招式参数|操虫斩|位移")
	FSegmentedDirectionalMovementProfile KinsectSlashDashProfile;

	/** 是否使用当前实录操虫斩位移；仅关闭时才读取旧标量/曲线兼容参数。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "高级兼容|操虫斩")
	bool bUseCapturedKinsectSlashDash = true;

	/** 旧操虫斩标量距离（cm）；启用实录位移时无效。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "高级兼容|操虫斩", meta = (ClampMin = "0.01"))
	float KinsectSlashMaxDistance = 840.0f;

	/** 旧操虫斩标量位移时长（秒）；启用实录位移时无效。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "高级兼容|操虫斩", meta = (ClampMin = "0.01"))
	float KinsectSlashDashDuration = 0.242f;

	/** 操虫斩允许的最大上冲俯仰角（度）；更高准心角度夹紧到此值。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "招式参数|操虫斩|瞄准",
		meta = (ClampMin = "0.0", ClampMax = "89.0", ForceUnits = "deg"))
	float KinsectSlashMaxUpwardLaunchPitchDegrees = 53.74f;

	/** 旧操虫斩归一化距离曲线；启用实录位移时不读取。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "高级兼容|操虫斩")
	TObjectPtr<UCurveFloat> KinsectSlashDistanceCurve;

	/** 索引为舞踏层数（0/1/2/3）；仅武器段，出招时快照。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "资源|舞踏|增伤", meta = (ClampMin = "1.0"))
	TArray<float> KinsectSlashDanceDamageMultipliers = { 1.0f, 1.1f, 1.2f, 1.3f };


#if WITH_EDITOR
	EDataValidationResult ValidateOwnedActionConfig(FDataValidationContext& Context) const;
#endif

	UMHGZKinsectSlashAbility();
	virtual float GetStaminaRecoveryPerSecond() const override;

	virtual bool CanActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
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

	virtual void HandleLanded(const FHitResult& Hit) override;

	/** Called only by an exact Montage-instance notify resolver. */
	bool HandleMontageEvent(const FWeaponActionToken& ActionToken,
		int32 MontageInstanceID, EKinsectSlashMontageEvent Event);

	/** Exact overload used by UAnimNotify_IG_AerialHandoff for this GA. */
	bool NotifyAerialHandoff(const FWeaponActionToken& ActionToken,
		int32 MontageInstanceID);
	using UMHGZInsectGlaiveAbility::NotifyAerialHandoff;

	UFUNCTION(BlueprintPure, Category = "MHGZ|KinsectSlash")
	EKinsectSlashAimSource GetAimSource() const { return AimSource; }

	UFUNCTION(BlueprintPure, Category = "MHGZ|KinsectSlash")
	EKinsectSlashPhase GetSlashPhase() const { return Phase; }

	UFUNCTION(BlueprintPure, Category = "MHGZ|KinsectSlash")
	FVector GetLockedLaunchDirection() const { return LaunchDirection; }

	/** 操虫斩起手滞空阶段的Section名。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect Slash|Montage Sections")
	FName HoverSectionName = TEXT("Hover186");

	/** 操虫斩前冲阶段的Section名。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect Slash|Montage Sections")
	FName DashSectionName = TEXT("Dash187");

	/** 未命中后下坠表现的Section名。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect Slash|Montage Sections")
	FName MissFallSectionName = TEXT("Miss188");

	/** 操虫斩命中后播放的常驻舞踏蒙太奇。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect Slash|Montage")
	TObjectPtr<UAnimMontage> DanceMontage;

	/** 舞踏蒙太奇的入口段名，必须与常驻资产中的Section一致。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect Slash|Montage Sections")
	FName DanceSectionName = TEXT("HitDance189");

	/** 切入舞踏蒙太奇时使用的混入时长，单位为秒。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect Slash|Montage",
		meta = (ClampMin = "0.0", ForceUnits = "s"))
	float DanceMontageBlendInTime = 0.1f;

	/** 操虫斩视觉俯仰混入时长，单位为秒。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect Slash|Visual Pitch",
		meta = (ClampMin = "0.0", ForceUnits = "s"))
	float VisualPitchBlendInTime = 0.1f;

	/** 操虫斩视觉俯仰释放淡出时长，单位为秒。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect Slash|Visual Pitch",
		meta = (ClampMin = "0.0", ForceUnits = "s"))
	float VisualPitchBlendOutTime = 0.1f;

	/** 冲刺命中后直接进入舞踏时使用的准备及飞行轨迹参数。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect Slash|Movement")
	FKinsectSlashDanceEntryTuning DanceFromDashTuning;

	/** 未命中下坠阶段转入舞踏时使用的准备及飞行轨迹参数。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect Slash|Movement")
	FKinsectSlashDanceEntryTuning DanceFromMissTuning;

	/** 未命中后下坠阶段采用的重力正幅值，单位为厘米每平方秒。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect Slash|Movement",
		meta = (ClampMin = "0.001"))
	float MissGravityMagnitude = 3096.0f;

	/** 是否绘制操虫斩接触探测的调试图形；只影响诊断显示。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kinsect Slash|Debug",
		meta = (DeprecatedProperty, DeprecationMessage = "Use AttackSegments[].Collision.bDrawDebug."))
	bool bDrawProbeDebug = false;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

protected:
	virtual bool ValidateActionDependencies() const override;
	virtual FGameplayEffectSpecHandle MakeDamageSpec(
		const FHitResult& Hit, int32 SegmentIndex) override;
	virtual bool PrepareAttackMontage() override;
	virtual bool StartAttackMontage(ACharacter& Character, UAnimMontage* Montage,
		FName StartSection) override;
	virtual void ApplyActivationDirectionCorrection() override;
	virtual void HandleSuccessfulAttackDamage(const FHitResult& Hit,
		int32 SegmentIndex) override;
	virtual bool CanApplyCollisionHit(const FHitResult& Hit,
		int32 SegmentIndex) const override;
	virtual bool ShouldSweepCollision(int32 SegmentIndex) const override;
	virtual bool ShouldConsumeCollisionOnDamageFailure(int32 SegmentIndex) const override;
	virtual void OnAttackCollisionWindowClosed(int32 SegmentIndex) override;
	virtual void OnMontageCompleted() override;

private:
	#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
	friend class FMHGZM5KinsectSlashMontageCompletionRaceTest;
	friend class FMHGZM5KinsectSlashDamageResourceTest;
	friend class FMHGZM5KinsectSlashProbeSweepTest;
	friend class FMHGZM5KinsectSlashLateHitOpenBoundaryTest;
	friend class FMHGZM5KinsectSlashLifecycleCancellationTest;
	friend class FMHGZM5KinsectSlashDancePreparationGuardTest;
	friend class FMHGZM5KinsectSlashReentrantDanceCommitTest;
	friend class FMHGZKinsectSlashIndependentContactTest;
	friend class FMHGZIGDanceDamageTest;
	friend class FMHGZStaminaCostsTest;
	friend class FMHGZKinsectSlashPreparationTest;
	#endif

	bool IsLiveAerialSlashStateValid(const UAbilitySystemComponent* ASC,
		const UMHGZWeaponRuntimeHostComponent* Host) const;
	bool ShouldDeferMontageCompletionForPendingDance() const;
	bool IsCurrentAction(const FWeaponActionToken& ActionToken) const;
	bool IsCurrentMontage(const FWeaponActionToken& ActionToken,
		int32 MontageInstanceID) const;
	static bool IsLateHitOpenNotifyPhaseAllowed(EKinsectSlashPhase CurrentPhase,
		FName CurrentSection, FName MissSection);
	void BindLifecycleGuards();
	void UnbindLifecycleGuards();
	void HandleRuntimeInvalidated(const FWeaponRuntimeToken& InvalidatedToken);
	void HandleDisqualifyingStateTagChanged(FGameplayTag Tag, int32 NewCount);
	bool IsLiveContinuationStateValid() const;
	bool HasRequiredSections(const UAnimMontage* Montage) const;
	bool HasRequiredDanceMontage() const;
	virtual float GetInstantStaminaCost(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo) const override;
	USkeletalMeshComponent* FindKinsectSlashWeaponMesh(FName RequiredSocket) const;
	float GetKinsectPreparationTime() const;
	bool IsKinsectActionAttachmentCurrent() const;
	void ReleaseKinsectActionAttachment();
	void HandleSuccessfulKinsectContact(const FHitResult& Hit);
	bool LockAimAndDirection();
	void ReleaseActionVisualPitch(EActionVisualPitchReleaseMode ReleaseMode);
	bool StartDashMovement();
	bool StartHoverMovement();
	bool StartDancePreparationMovement();
	bool StartDanceMovement(bool bFromMiss);
	void HandleQueuedDanceTransition(FWeaponActionToken ActionToken,
		int32 MontageInstanceID);
	void HandleDancePreparationFinished(FWeaponActionToken ActionToken,
		int32 MontageInstanceID);
	void ClearDanceTimers();
	void HandleMontageCompleted();
	bool EnterMissFallPhase();
	void OnSlashMontageSectionChanged(UAnimMontage* Montage,
		FName SectionName, bool bLooped);
	void FinalizeHostLanding(bool bPlayLandingPresentation);
	void CompleteSlashAction();
	void CancelSlashAction(EWeaponActionEndReason EndReason,
		const TCHAR* Diagnostic);
	void ResetActivationState();
	FVector ResolveLaunchYawDirection() const;
	FVector ResolveDashTerminalVelocity() const;
	FWeaponMovementRequest MakeDashMovementRequest() const;

	UFUNCTION()
	void HandleSlashMontageCompleted();

	UFUNCTION()
	void HandleSlashMontageInterrupted();

	UFUNCTION()
	void HandleDanceMontageCompleted();

	UFUNCTION()
	void HandleDanceMontageInterrupted();

	UFUNCTION()
	void HandleMovementFinished(const FWeaponMovementResult& Result);

	UFUNCTION()
	void HandleProbeHitzoneContact(FWeaponActionToken ActionToken, FHitResult Hit);

	FTimerHandle QueuedDanceTransitionTimer;
	FTimerHandle DancePreparationTimer;
	FDelegateHandle RuntimeInvalidationDelegateHandle;
	TMap<FGameplayTag, FDelegateHandle> DisqualifyingStateTagDelegateHandles;

	/** 操虫斩主体蒙太奇的当前播放任务。 */
	UPROPERTY()
	TObjectPtr<UAbilityTask_MHGZPlayMontageAndWait> SlashMontageTask;

	/** 当前舞踏蒙太奇播放任务，供完成、取消和交接清理。 */
	UPROPERTY()
	TObjectPtr<UAbilityTask_MHGZPlayMontageAndWait> DanceMontageTask;

	/** 当前动作拥有的位移任务；任务结束或取消时释放。 */
	UPROPERTY()
	TObjectPtr<UAbilityTask_MHGZWeaponMovement> MovementTask;

	/** 操虫斩起手滞空阶段的位移任务。 */
	UPROPERTY()
	TObjectPtr<UAbilityTask_MHGZWeaponMovement> HoverMovementTask;

	/** 本次操虫斩接触检测任务引用，结束时随动作清理。 */
	UPROPERTY()
	TObjectPtr<UAbilityTask_MHGZKinsectSlashProbe> ProbeTask;

	/** 本次实际播放的操虫斩主体蒙太奇引用。 */
	UPROPERTY()
	TObjectPtr<UAnimMontage> ActiveSlashMontage;

	FWeaponActionToken SlashActionToken;
	FWeaponActionToken MontageActionToken;
	FWeaponActionToken MovementActionToken;
	int32 SlashMontageInstanceID = INDEX_NONE;
	int32 VisualPitchSourceMontageInstanceID = INDEX_NONE;

	EKinsectSlashAimSource AimSource = EKinsectSlashAimSource::None;
	EKinsectSlashPhase Phase = EKinsectSlashPhase::Inactive;
	FVector LaunchDirection = FVector::ForwardVector;
	FVector DashTerminalVelocity = FVector::ZeroVector;
	FHitResult SuccessfulHit;
	FGuid KinsectSlashPowderHitInstanceID;
	FName SuccessfulHitSourceSection = NAME_None;
	float SuccessfulHitSourcePosition = 0.0f;
	float FrozenDanceDamageMultiplier = 1.0f;
	int32 SuccessfulHitSourceMontageInstanceID = INDEX_NONE;
	// Owns the preparation lease too; physical socket readiness is queried separately.
	bool bKinsectActionAttached = false;
	bool bAimLocked = false;
	bool bDashMovementFinished = false;
	bool bDanceMovementFinished = false;
	bool bMontageFinished = false;
	bool bPendingDanceTransition = false;
	bool bDamageResolveInProgress = false;
	bool bCurrentDamageApplied = false;
	// Segment 0 owns cargo only; segment 1 alone authorizes the 189 handoff.
	bool bKinsectDamageApplied = false;
	bool bSuccessfulDamage = false;
	bool bExtractApplied = false;
	bool bDanceEntryCommitted = false;
	bool bDanceMontageActive = false;
	bool bMissPoseHeld = false;
	bool bEndingSlash = false;
	bool bEndingDashForDance = false;
	bool bMovementTaskIsDance = false;
	bool bLanded = false;
	bool PendingDanceFromMiss = false;
};
