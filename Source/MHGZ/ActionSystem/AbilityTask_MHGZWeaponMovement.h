// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/Tasks/AbilityTask.h"
#include "WeaponRuntime/MHGZWeaponRuntimeTypes.h"
#include "AbilityTask_MHGZWeaponMovement.generated.h"

class ACharacter;
class UCharacterMovementComponent;
class UPrimitiveComponent;
class UMHGZGameplayAbility;
class UMHGZWeaponRuntimeHostComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMHGZWeaponMovementResultDelegate,
	const FWeaponMovementResult&, Result);

/**
 * M5 唯一的动作位移执行任务。
 *
 * Request 必须持有已经 Commit 且仍注册在 RuntimeHost 中的 ActionToken。任务先取得
 * Host 的唯一 Movement ownership，再用 RootMotionSource 或 CMC 分段/下落积分执行位移；其结束、
 * Action 取消、换武器和 Runtime Shutdown 都经同一精确 Token 回收 RootMotion 与
 * MotionWarping Target。Character 的普通 locomotion 只在该 ownership 不存在时写旋转。
 */
UCLASS()
class MHGZ_API UAbilityTask_MHGZWeaponMovement : public UAbilityTask
{
	GENERATED_BODY()

public:
	UAbilityTask_MHGZWeaponMovement(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(BlueprintAssignable)
	FMHGZWeaponMovementResultDelegate OnFinished;

	/** 仅供已完成 Commit 的 UMHGZGameplayAbility 创建；无有效 ActionToken 时失败。 */
	static UAbilityTask_MHGZWeaponMovement* StartWeaponMovement(
		UMHGZGameplayAbility* OwningAbility, FName TaskInstanceName,
		const FWeaponMovementRequest& Request);

	virtual void Activate() override;
	virtual void TickTask(float DeltaTime) override;
	virtual void OnDestroy(bool AbilityIsEnding) override;

	bool DidStartMovement() const { return bStarted; }
	/** Retire the 186 hover source while the same action immediately installs its dash source. */
	bool EndHoverForImmediateMovementHandoff(const FWeaponActionToken& ActionToken);
	/** Same-action source replacement (hover/dash to189); never emits a fake grounded frame. */
	bool EndForImmediateMovementHandoff(const FWeaponActionToken& ActionToken);
	/** Enable real ProcessLanded on an active protected trajectory without resetting its clock. */
	bool EnableRecordedTrajectoryLanding(const FWeaponActionToken& ActionToken);

private:
	UFUNCTION()
	void HandleLanded(const FHitResult& Hit);

	UFUNCTION()
	void HandleCapsuleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	bool ValidateRequest() const;
	bool ApplyMovementSource();
	bool ApplyBoundedDirectionalSource();
	bool ApplyCurvedDirectionalSource();
	bool ApplyStationaryHoverSource();
	bool ApplyBallisticVaultSource();
	bool ApplyCurvedVaultSource();
	bool ApplyAdditiveInertiaSource();
	bool ApplyAuthoredAerialPhysics();
	bool ApplySegmentedDirectionalMovement();
	bool ApplyRecordedLocalTrajectoryMovement();
	void UpdateActionRotation(float DeltaTime);
	void FinishMovement(EWeaponMovementEndReason Reason, const FHitResult* BlockingHit = nullptr);
	void ReleaseMovementOwnership();
	void RemoveRootMotionSource();
	void RetireSegmentedMovement();
	/** 曲线模式把运动交给 CMC，租约同样必须在收尾时归还，否则下一相位接不上。 */
	void RetireRecordedTrajectory();
	bool OwnsMovement() const;

	/**
	 * 兜底：如果胶囊还停在 MOVE_Flying 里，把它交还给 CMC。
	 *
	 * 弧正常结束时，是 `UAnimNotify_IG_AerialHandoff` 在最早可操作帧把它切到
	 * MOVE_Falling —— 但那条路可能没走到：蒙太奇没挂通知、蒙太奇在释放点之前
	 * 被取消、或 Action 被抢先结束。MOVE_Flying 没有重力，胶囊会**悬停**，
	 * 而这是任务唯一能兜住的地方（`HandleXxxVaultFinished` 在 `!IsActive()` 时
	 * 早退，`EndAbility` 可能在 `OnDestroy` 之后）。
	 *
	 * 切换模式前先 FindFloor：站在可行走地面上回到 Walking，悬空直接进 Falling。
	 * 不经过 SetDefaultMovementMode 的临时 Walking，避免误报落地并恢复空中物理档。
	 *
	 * @param bOutWasFlying  调用前是否确实处于 MOVE_Flying（即释放是否由本次触发）
	 * @return 调用后胶囊是否站在可行走地面上
	 */
	bool EnsureVaultFlightReleased(bool& bOutWasFlying);
	void ApplyFinishVelocityPolicy();

	/** Keep request-owned runtime curves alive throughout the movement lease. */
	UPROPERTY(Transient)
	FWeaponMovementRequest MovementRequest;
	TWeakObjectPtr<UMHGZGameplayAbility> MovementAbility;
	TWeakObjectPtr<ACharacter> Character;
	TWeakObjectPtr<UCharacterMovementComponent> MovementComponent;
	TWeakObjectPtr<UMHGZWeaponRuntimeHostComponent> RuntimeHost;
	uint16 RootMotionSourceID = 0;
	uint64 SegmentedMovementLease = 0;
	uint64 RecordedTrajectoryLease = 0;
	bool bOwnsActionMovement = false;
	FVector StartLocation = FVector::ZeroVector;
	FVector LastLocation = FVector::ZeroVector;
	FVector ExpectedDestination = FVector::ZeroVector;
	/**
	 * CurvedVault's authored world-space tangent at the free-fall hand-off.
	 * Resolved once when the source is created so the hand-off never depends on
	 * the frame on which the task happens to observe the source's completion.
	 */
	FVector ResolvedHandoffVelocity = FVector::ZeroVector;
	bool bHasResolvedHandoffVelocity = false;
	/** World-space terminal tangent for the new arbitrary 3D directional curve mode. */
	FVector ResolvedDirectionalHandoffVelocity = FVector::ZeroVector;
	bool bHasResolvedDirectionalHandoffVelocity = false;
	float ResolvedDuration = 0.0f;
	float ElapsedMovementTime = 0.0f;
	bool bStarted = false;
	bool bFinished = false;
	bool bPreserveFlyingForImmediateHandoff = false;
	bool bBoundLandedDelegate = false;
	bool bBoundCapsuleHit = false;
	/** The 187 dash keeps its full duration after a wall hit, but must not relaunch into that wall at handoff. */
	bool bDirectionalHitWall = false;
	FHitResult DirectionalWallHit;
	FWeaponMovementResult Result;
};
