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
	/** Jump Input Action */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Input")
	UInputAction* JumpAction;

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

	/** UpdateMaxWalkSpeed——MoveSpeedMultiplier 变化时由 AttributeSet 回调 */
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

	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpStart();

	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpEnd();

private:
	/** 缓存的 CMC 基础行走速度 */
	float BaseMaxWalkSpeed = 500.f;
};
