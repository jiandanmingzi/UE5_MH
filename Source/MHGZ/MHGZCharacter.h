// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "Logging/LogMacros.h"
#include "MHGZCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UInputAction;
class UMotionWarpingComponent;
class UMHGZAimComponent;
class UMHGZEdgeVaultComponent;
class UMHGZAbilitySystemComponent;
struct FInputActionValue;

/**
 * ETransitionState — 起步/停步过渡蒙太奇的播放状态
 * AnimBP 每帧读取此枚举值驱动蒙太奇切换
 */
UENUM(BlueprintType)
enum class ETransitionState : uint8
{
	None		UMETA(DisplayName = "无过渡"),
	Starting	UMETA(DisplayName = "起步中"),
	Stopping	UMETA(DisplayName = "停步中"),
};

/**
 * AMHGZCharacter — MHGZ 玩家角色
 * - 实现 IAbilitySystemInterface
 * - 集成 MotionWarping
 * - 集成 UMHGZAimComponent（瞄准）
 * - 移动由 CMC + AddMovementInput 负责（非 GAS）
 * - ShouldBlockMovement 检查 Combat.State.BlockMovement ——单 Tag 控制所有移动屏蔽
 */
UCLASS(abstract)
class AMHGZCharacter : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

	/** Camera boom */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
	USpringArmComponent* CameraBoom;

	/** Follow camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
	UCameraComponent* FollowCamera;

	/** MotionWarping 组件 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
	UMotionWarpingComponent* MotionWarpingComponent;

	/** 瞄准检测组件 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
	UMHGZAimComponent* AimComponent;

	/** 边缘跳越组件 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
	UMHGZEdgeVaultComponent* EdgeVaultComponent;

protected:
	/** Move Input Action */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Input")
	UInputAction* MoveAction;

	/** Look Input Action */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Input")
	UInputAction* LookAction;

	/** Mouse Look Input Action */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Input")
	UInputAction* MouseLookAction;

	/** Sprint Input Action (LS/L3) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Input")
	UInputAction* SprintAction;

public:
	AMHGZCharacter();

	// ── IAbilitySystemInterface ──
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	// ── GAS 初始化 ──
	virtual void PossessedBy(AController* NewController) override;

	// ── 着陆重置 ──
	virtual void Landed(const FHitResult& Hit) override;

	/** 刷新 CMC 速度——MoveSpeedMultiplier 变化或 Sprint 状态变化时由外部回调，DoMove 每帧调用 */
	void UpdateMaxWalkSpeed(float StickMagnitude = 1.0f);

	// ── 组件访问 ──
	FORCEINLINE USpringArmComponent* GetCameraBoom() const { return CameraBoom; }
	FORCEINLINE UCameraComponent* GetFollowCamera() const { return FollowCamera; }
	FORCEINLINE UMotionWarpingComponent* GetMotionWarpingComponent() const { return MotionWarpingComponent; }
	FORCEINLINE UMHGZAimComponent* GetAimComponent() const { return AimComponent; }

protected:
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void SprintPressed(const FInputActionValue& Value);
	void SprintReleased(const FInputActionValue& Value);

public:
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoMove(float Right, float Forward);

	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoLook(float Yaw, float Pitch);

	/** 检查是否应屏蔽移动输入——单一 Tag 控制：Combat.State.BlockMovement */
	bool ShouldBlockMovement() const;

	/** 摇杆输入的世界方向（每帧更新，不受 BlockMovement 影响）——供 MotionWarping / AttackAbility / AnimBP 读取 */
	UFUNCTION(BlueprintCallable, Category="Input")
	FVector GetLastMovementInputDir() const;

	/** ★ 当前过渡蒙太奇播放状态——AnimBP 每帧读取以决定蒙太奇切换 */
	UFUNCTION(BlueprintCallable, Category="Movement|Transition")
	ETransitionState GetTransitionState() const;

	/** ★ 过渡蒙太奇正常播放完毕——由 AnimNotify 在起步/停步蒙太奇末帧调用 */
	UFUNCTION(BlueprintCallable, Category="Movement|Transition")
	void OnTransitionMontageEnded();

	/** 移除 BlockMovement Tag——AnimNotify 回调 */
	UFUNCTION(BlueprintCallable, Category="Movement|Transition")
	void RemoveBlockMovementTag() const;

private:
	/** 行走速度（cm/s）——对应混合空间 Walk 节点，摇杆刚过死区时的最低速度 */
	UPROPERTY(EditDefaultsOnly, Category="Movement|Speed")
	float WalkSpeed = 150.f;

	/** 奔跑速度（cm/s）——对应混合空间 Run 节点，摇杆推满时的最高速度 */
	UPROPERTY(EditDefaultsOnly, Category="Movement|Speed")
	float RunSpeed = 500.f;

	/** 冲刺速度（cm/s）——按住 Sprint 时无视摇杆幅度的固定速度，混合空间用 Run 动画加速播放 */
	UPROPERTY(EditDefaultsOnly, Category="Movement|Speed")
	float SprintSpeed = 650.f;

	/** 摇杆死区——幅度低于此值不触发移动，防止轻微触碰触发起步动画 */
	UPROPERTY(EditDefaultsOnly, Category="Movement|Deadzone")
	float MoveDeadzone = 0.2f;

	/** Sprint 是否按住中 */
	bool bSprintHeld = false;

	/** 上一帧是否处于常速移动中——用于检测起步/停步边沿，避免起步时 CMC 先滑一帧 */
	bool bHasMovementInput = false;

	/** 摇杆输入世界方向——每帧 DoMove 更新，不受 BlockMovement 影响，供 MotionWarping/AnimBP 读 */
	FVector LastMovementInputDir = FVector::ZeroVector;

	/** ★ 当前过渡蒙太奇播放状态——AnimBP 每帧读取 */
	ETransitionState TransitionState = ETransitionState::None;

	/** ★ 松手瞬间的 Speed 快照——区分 Walk_Stop / Run_Stop */
	float SnapSpeedAtRelease = 0.f;

	/** 添加 BlockMovement Tag 以触发起步/停步蒙太奇 */
	void AddBlockMovementTag() const;
};
