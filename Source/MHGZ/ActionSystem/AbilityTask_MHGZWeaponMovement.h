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
 * Host 的唯一 Movement ownership，再用 CMC RootMotionSource 执行位移；其结束、
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

private:
	UFUNCTION()
	void HandleLanded(const FHitResult& Hit);

	UFUNCTION()
	void HandleCapsuleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	bool ValidateRequest() const;
	bool ApplyMovementSource();
	bool ApplyBoundedDirectionalSource();
	bool ApplyBallisticVaultSource();
	bool ApplyCurvedVaultSource();
	bool ApplyAdditiveInertiaSource();
	void UpdateActionRotation(float DeltaTime);
	void FinishMovement(EWeaponMovementEndReason Reason, const FHitResult* BlockingHit = nullptr);
	void ReleaseMovementOwnership();
	void RemoveRootMotionSource();
	void ApplyFinishVelocityPolicy();

	FWeaponMovementRequest MovementRequest;
	TWeakObjectPtr<UMHGZGameplayAbility> MovementAbility;
	TWeakObjectPtr<ACharacter> Character;
	TWeakObjectPtr<UCharacterMovementComponent> MovementComponent;
	TWeakObjectPtr<UMHGZWeaponRuntimeHostComponent> RuntimeHost;
	uint16 RootMotionSourceID = 0;
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
	float ResolvedDuration = 0.0f;
	bool bStarted = false;
	bool bFinished = false;
	bool bBoundLandedDelegate = false;
	bool bBoundCapsuleHit = false;
	FWeaponMovementResult Result;
};
