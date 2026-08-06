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
class UMHGZWeaponDefinition;
struct FInputActionValue;

/**
 * AMHGZCharacter — MHGZ 玩家角色
 * - 实现 IAbilitySystemInterface
 * - 集成 MotionWarping
 * - 集成 UMHGZAimComponent（瞄准）
 * - 位移由 AnimBP RootMotion（Motion Matching）全权驱动；CMC 为碰撞壳
 * - ShouldBlockMovement（BlockMovement Tag）仅外部 GA 使用——攻击/受击/翻滚冻结移动
 * - C++ 暴露 DesiredSpeed / TargetCruiseSpeed 供 AnimBP 计算 Trajectory
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

	/** LT/L2 瞄准输入；Started/Completed 维护 Combat.State.Aiming。 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Input")
	UInputAction* AimAction;

	/** Demo 固定武器；ASC 初始化完成后自动装备。留空则由背包/装备 UI 决定。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Equipment|Demo")
	TObjectPtr<UMHGZWeaponDefinition> DefaultWeaponDefinition;

public:
	AMHGZCharacter();

	// ── IAbilitySystemInterface ──
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	// ── 每帧兜底：IA 漏帧时 DesiredSpeed 仍正常衰减 ──
	virtual void Tick(float DeltaTime) override;

	// ── GAS 初始化 ──
	virtual void PossessedBy(AController* NewController) override;

	// ── 着陆重置 ──
	virtual void Landed(const FHitResult& Hit) override;

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
	void AimPressed();
	void AimReleased();
	void EquipDefaultWeaponIfConfigured();

public:
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoMove(float Right, float Forward);

	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoLook(float Yaw, float Pitch);

	/** 检查是否应屏蔽移动输入——单一 Tag 控制：Combat.State.BlockMovement */
	bool ShouldBlockMovement() const;

	/** 当前帧摇杆输入幅度（0.0~1.0），低于死区时为 0——AnimBP 每帧读取 */
	UPROPERTY(BlueprintReadOnly, Category="Input")
	float InputMagnitude = 0.f;

	/** 当前帧是否有有效输入（幅度 >= MoveDeadzone）——AnimBP 每帧读取 */
	UPROPERTY(BlueprintReadOnly, Category="Input")
	bool bHasInput = false;

	/** 摇杆输入的世界方向（每帧更新，不受 BlockMovement 影响）——供 MotionWarping / AttackAbility / AnimBP 读取 */
	UFUNCTION(BlueprintCallable, Category="Input")
	FVector GetLastMovementInputDir() const;

	// ── Motion Matching 期望速度 ────────────────────────────────

	/** 当前期望速度（平滑插值后的值，cm/s）——AnimBP 拿此值喂 Trajectory */
	UPROPERTY(BlueprintReadOnly, Category="Movement|MM")
	float DesiredSpeed = 0.f;

	/** 摇杆瞬时目标巡航速度（cm/s，无平滑）——AnimBP 可读此值做启停方向判断 */
	UPROPERTY(BlueprintReadOnly, Category="Movement|MM")
	float TargetCruiseSpeed = 0.f;

	/** 强制 MM 输出 Idle Pose——BlockMovement 时切断 MM，防止 RM 和蒙太奇 RM 叠加 */
	UPROPERTY(BlueprintReadOnly, Category="Movement|MM")
	bool bForceMMIdle = false;

	/** 是否拔刀态——AnimBP 读此值切换 Database_Unarmed / Database_Armed */
	UPROPERTY(BlueprintReadOnly, Category="Movement|MM")
	bool bUnsheathed = false;

private:
	// ── 速度计算 ────────────────────────────────────────────────

	float CalcCruiseSpeed(float StickMagnitude) const;

	// ── 收刀态巡航速度常量 ─────────────────────────────────────

	UPROPERTY(EditDefaultsOnly, Category="Movement|MM|Sheathed")
	float WalkCruise_Sheathed = 150.f;

	UPROPERTY(EditDefaultsOnly, Category="Movement|MM|Sheathed")
	float RunCruise_Sheathed = 500.f;

	UPROPERTY(EditDefaultsOnly, Category="Movement|MM|Sheathed")
	float SprintCruise = 650.f;

	// ── 拔刀态巡航速度（单速，走跑合一）────────────────────────

	UPROPERTY(EditDefaultsOnly, Category="Movement|MM|Unsheathed")
	float RunCruise_Unsheathed = 450.f;

	// ── 其他参数 ────────────────────────────────────────────────

	/** 摇杆死区 */
	UPROPERTY(EditDefaultsOnly, Category="Movement|Deadzone")
	float MoveDeadzone = 0.1f;

	/** 期望速度平滑速率——DesiredSpeed 追踪 TargetCruiseSpeed 的 InterpSpeed（>1 时越大越灵敏） */
	UPROPERTY(EditDefaultsOnly, Category="Movement|MM")
	float DesiredSpeedInterpSpeed = 20.f;

	/** 角色旋转速度（度/秒）——DoMove 每帧 RInterpTo 的目标朝向 */
	UPROPERTY(EditDefaultsOnly, Category="Movement|Rotation")
	float TurnRate = 360.f;

	/** Sprint 是否按住中 */
	bool bSprintHeld = false;

	/** 摇杆输入世界方向——每帧 DoMove 更新，不受 BlockMovement 影响 */
	FVector LastMovementInputDir = FVector::ZeroVector;

	/** 本帧已处理理论速度的帧号——防止 Tick 和 DoMove 同帧重复处理 */
	uint64 LastTheoryUpdateFrame = 0;
};
