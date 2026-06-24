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
 * AMHGZCharacter — MHGZ 玩家角色
 * - 实现 IAbilitySystemInterface
 * - 集成 MotionWarping
 * - 集成 UMHGZAimComponent（瞄准）
 * - 移动由 CMC + AddMovementInput 负责（非 GAS）
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

public:
	AMHGZCharacter();

	// ── IAbilitySystemInterface ──
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	// ── GAS 初始化 ──
	virtual void PossessedBy(AController* NewController) override;

	// ── 着陆重置 ──
	virtual void Landed(const FHitResult& Hit) override;

	// ── 移动过渡检测 ──
	virtual void Tick(float DeltaTime) override;

	/** 刷新 CMC 速度——MoveSpeedMultiplier 变化时由 AttributeSet 回调，或 Sprint 状态变化时调用 */
	void UpdateMaxWalkSpeed();

	// ── 组件访问 ──
	FORCEINLINE USpringArmComponent* GetCameraBoom() const { return CameraBoom; }
	FORCEINLINE UCameraComponent* GetFollowCamera() const { return FollowCamera; }
	FORCEINLINE UMotionWarpingComponent* GetMotionWarpingComponent() const { return MotionWarpingComponent; }
	FORCEINLINE UMHGZAimComponent* GetAimComponent() const { return AimComponent; }

protected:
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);

public:
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoMove(float Right, float Forward);

	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoLook(float Yaw, float Pitch);

	/** 检查当前是否应屏蔽移动输入（攻击/硬直/击倒/死亡中） */
	bool ShouldBlockMovement() const;

private:
	/** 行走速度（cm/s）——对应混合空间 Walk 节点 */
	UPROPERTY(EditDefaultsOnly, Category="Movement|Speed")
	float WalkSpeed = 150.f;

	/** 奔跑速度（cm/s）——对应混合空间 Run 节点 */
	UPROPERTY(EditDefaultsOnly, Category="Movement|Speed")
	float RunSpeed = 500.f;

	/** 上次 Tick 的移动速度——用于检测起步/停步过渡 */
	float LastTickSpeed = 0.f;

	/** 起步/停步速度阈值——低于此值视为静止 */
	static constexpr float MovementTransitionThreshold = 10.f;

	/** 在 ASC 上管理移动过渡 Tag（StartingMovement / StoppingMovement），供 AnimBP 触发过渡蒙太奇 */
	void UpdateMovementTransitionTags(float CurrentSpeed);
};
