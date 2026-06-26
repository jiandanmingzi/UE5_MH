// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZCharacter.h"
#include "MHGZ.h"
#include "MHGZPlayerState.h"
#include "Engine/LocalPlayer.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "MotionWarpingComponent.h"
#include "UI/MHGZAimComponent.h"
#include "InputSystem/MHGZEdgeVaultComponent.h"
#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "ActionSystem/MHGZComboCoordinatorAbility.h"
#include "AttributeSystem/MHGZAttributeSet.h"

AMHGZCharacter::AMHGZCharacter()
{
	// 碰撞胶囊体
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// CMC 配置
	UCharacterMovementComponent* CMC = GetCharacterMovement();
	CMC->bOrientRotationToMovement = true;
	CMC->RotationRate = FRotator(0.0f, 360.0f, 0.0f); // 360°/s 转向
	CMC->bUseControllerDesiredRotation = false;
	CMC->JumpZVelocity = 500.f;
	CMC->AirControl = 0.10f;        // 低值——惯性主导，接近怪猎手感

	// 初始用 WalkSpeed（非奔跑态）；Sprint 时 UpdateMaxWalkSpeed 切到 RunSpeed
	CMC->MaxWalkSpeed = WalkSpeed;
	CMC->MinAnalogWalkSpeed = 20.f;

	// 常速移动段加减速——过渡段（起步/停步）的位移由 RootMotion 蒙太奇驱动
	CMC->MaxAcceleration = 2048.f;
	CMC->BrakingDecelerationWalking = 2048.f;
	CMC->BrakingDecelerationFalling = 80.f; // 空中水平衰减
	CMC->GravityScale = 1.0f;              // 空中重力倍率

	// CameraBoom
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 400.0f;
	CameraBoom->bUsePawnControlRotation = true;

	// FollowCamera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	// MotionWarping
	MotionWarpingComponent = CreateDefaultSubobject<UMotionWarpingComponent>(TEXT("MotionWarpingComponent"));

	// AimComponent
	AimComponent = CreateDefaultSubobject<UMHGZAimComponent>(TEXT("AimComponent"));

	// EdgeVault
	EdgeVaultComponent = CreateDefaultSubobject<UMHGZEdgeVaultComponent>(TEXT("EdgeVaultComponent"));
}

UAbilitySystemComponent* AMHGZCharacter::GetAbilitySystemComponent() const
{
	if (AMHGZPlayerState* PS = GetPlayerState<AMHGZPlayerState>())
	{
		return PS->GetMHGZAbilitySystemComponent();
	}
	return nullptr;
}

void AMHGZCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// InitAbilityActorInfo（此时 PlayerState 已就绪）
	AMHGZPlayerState* PS = GetPlayerState<AMHGZPlayerState>();
	if (!PS) return;

	UMHGZAbilitySystemComponent* ASC = PS->GetMHGZAbilitySystemComponent();
	if (ensure(ASC))
	{
		ASC->InitAbilityActorInfo(PS, this);
		// Owner = PlayerState（拥有这些 Ability 的逻辑实体）
		// Avatar = Character（物理表现实体）
		ASC->InitializeAbilitySystem();
	}
}

void AMHGZCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);

	// 着陆重置——强制协调器回 Idle
	AMHGZPlayerState* PS = GetPlayerState<AMHGZPlayerState>();
	if (!PS) return;

	UMHGZAbilitySystemComponent* ASC = PS->GetMHGZAbilitySystemComponent();
	if (!ASC) return;

	// 移除空中 Tag
	ASC->RemoveLooseGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aerial")));
	ASC->RemoveLooseGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aerial.Falling")));
	ASC->RemoveLooseGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aerial.CantDodge")));
	ASC->RemoveLooseGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aerial.CantAttack")));
	// 添加地面 Tag
	ASC->AddLooseGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Grounded")));

	// 通知协调器
	if (UGA_WeaponComboCoordinator* Coord = ASC->GetActiveComboCoordinator())
	{
		Coord->OnLanded();
	}
}

void AMHGZCharacter::UpdateMaxWalkSpeed(float StickMagnitude)
{
	AMHGZPlayerState* PS = GetPlayerState<AMHGZPlayerState>();
	const float Multiplier = PS
		? PS->GetMHGZAbilitySystemComponent()->GetNumericAttribute(
			UMHGZAttributeSet::GetMoveSpeedMultiplierAttribute())
		: 1.f;

	float Ceiling;
	if (bSprintHeld)
	{
		// Sprint 按住 → 固定 SprintSpeed，无视摇杆幅度
		Ceiling = SprintSpeed;
	}
	else
	{
		// 摇杆幅度从 [Deadzone, 1.0] 线性映射到 [WalkSpeed, RunSpeed]
		const float T = (StickMagnitude - MoveDeadzone) / (1.f - MoveDeadzone);
		Ceiling = FMath::Lerp(WalkSpeed, RunSpeed, FMath::Clamp(T, 0.f, 1.f));
	}

	GetCharacterMovement()->MaxWalkSpeed = Ceiling * Multiplier;
}

void AMHGZCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AMHGZCharacter::Move);

		// Looking
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AMHGZCharacter::Look);
		EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &AMHGZCharacter::Look);

		// Sprint (LS/L3 hold)
		EnhancedInputComponent->BindAction(SprintAction, ETriggerEvent::Started, this, &AMHGZCharacter::SprintPressed);
		EnhancedInputComponent->BindAction(SprintAction, ETriggerEvent::Completed, this, &AMHGZCharacter::SprintReleased);
	}
	else
	{
		UE_LOG(LogMHGZ, Error, TEXT("Failed to find Enhanced Input component!"));
	}
}

void AMHGZCharacter::Move(const FInputActionValue& Value)
{
	const FVector2D MovementVector = Value.Get<FVector2D>();
	DoMove(MovementVector.X, MovementVector.Y);
}

void AMHGZCharacter::Look(const FInputActionValue& Value)
{
	const FVector2D LookAxisVector = Value.Get<FVector2D>();
	DoLook(LookAxisVector.X, LookAxisVector.Y);
}

void AMHGZCharacter::SprintPressed(const FInputActionValue& Value)
{
	// 拔刀态不可奔跑（决策 #61）
	AMHGZPlayerState* PS = GetPlayerState<AMHGZPlayerState>();
	if (PS)
	{
		UMHGZAbilitySystemComponent* ASC = PS->GetMHGZAbilitySystemComponent();
		if (ASC && ASC->HasMatchingGameplayTag(
			FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Unsheathed"))))
		{
			return;
		}
	}

	bSprintHeld = true;
	UpdateMaxWalkSpeed();
}

void AMHGZCharacter::SprintReleased(const FInputActionValue& Value)
{
	bSprintHeld = false;
	UpdateMaxWalkSpeed();
}

FVector AMHGZCharacter::GetLastMovementInputDir() const
{
	return LastMovementInputDir;
}

ETransitionState AMHGZCharacter::GetTransitionState() const
{
	return TransitionState;
}

void AMHGZCharacter::AddBlockMovementTag() const
{
	AMHGZPlayerState* PS = GetPlayerState<AMHGZPlayerState>();
	if (!PS) return;

	UMHGZAbilitySystemComponent* ASC = PS->GetMHGZAbilitySystemComponent();
	if (!ASC) return;

	ASC->AddLooseGameplayTag(
		FGameplayTag::RequestGameplayTag(TEXT("Combat.State.BlockMovement")));
}

void AMHGZCharacter::RemoveBlockMovementTag() const
{
	AMHGZPlayerState* PS = GetPlayerState<AMHGZPlayerState>();
	if (!PS) return;

	UMHGZAbilitySystemComponent* ASC = PS->GetMHGZAbilitySystemComponent();
	if (!ASC) return;

	ASC->RemoveLooseGameplayTag(
		FGameplayTag::RequestGameplayTag(TEXT("Combat.State.BlockMovement")));
}

void AMHGZCharacter::OnTransitionMontageEnded()
{
	TransitionState = ETransitionState::None;
	RemoveBlockMovementTag();
	// 此时 AnimBP 回到状态机 Idle/Moving，CMC 接管
}

bool AMHGZCharacter::ShouldBlockMovement() const
{
	AMHGZPlayerState* PS = GetPlayerState<AMHGZPlayerState>();
	if (!PS) return false;

	UMHGZAbilitySystemComponent* ASC = PS->GetMHGZAbilitySystemComponent();
	if (!ASC) return false;

	static const FGameplayTag BlockMovementTag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.BlockMovement"));
	return ASC->HasMatchingGameplayTag(BlockMovementTag);
}

void AMHGZCharacter::DoMove(float Right, float Forward)
{
	if (!Controller)
	{
		return;
	}

	const FRotator Rotation = Controller->GetControlRotation();
	const FRotator YawRotation(0, Rotation.Yaw, 0);
	const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

	// ★ 始终记录摇杆世界方向——供 MotionWarping/AnimBP 读取（不受 BlockMovement 影响）
	const FVector InputVector = ForwardDirection * Forward + RightDirection * Right;
	if (!InputVector.IsNearlyZero())
	{
		LastMovementInputDir = InputVector.GetSafeNormal();
	}

	// ── 死区判断 ──
	const float InputMagnitude = FMath::Sqrt(Right * Right + Forward * Forward);
	const bool bHasInput = InputMagnitude >= MoveDeadzone;

	// ═══════════════════════════════════════════════════════════════
	// ★ 过渡状态中断处理 —— 在 BlockMovement 检查之前
	//   起步中松手 → 立即切停步；停步中推摇杆 → 立即切起步
	// ═══════════════════════════════════════════════════════════════

	// 起步中松手 → 中断起步，切到停步
	if (TransitionState == ETransitionState::Starting && !bHasInput)
	{
		TransitionState = ETransitionState::Stopping;
		bHasMovementInput = false;
		GetCharacterMovement()->Velocity = FVector::ZeroVector;
		// BlockMovement 保持（起步时已设），AnimBP 下一帧读到 Stopping → 切蒙太奇
		return;
	}

	// 停步中推摇杆 → 中断停步，切回起步（速度可能不同于上次）
	if (TransitionState == ETransitionState::Stopping && bHasInput)
	{
		TransitionState = ETransitionState::Starting;
		bHasMovementInput = true;
		UpdateMaxWalkSpeed(InputMagnitude);
		GetCharacterMovement()->Velocity = FVector::ZeroVector;
		// BlockMovement 保持，AnimBP 下一帧读到 Starting → 切蒙太奇
		return;
	}

	// 攻击/翻滚等外部 GA 设的 BlockMovement，且没有我们自己的过渡蒙太奇在播
	if (ShouldBlockMovement() && TransitionState == ETransitionState::None)
	{
		return;
	}

	// ═══════════════════════════════════════════════════════════════
	// 正常边沿检测（TransitionState == None 时）
	// ═══════════════════════════════════════════════════════════════

	// 起步：摇杆从无到有
	if (bHasInput && !bHasMovementInput)
	{
		TransitionState = ETransitionState::Starting;
		bHasMovementInput = true;
		UpdateMaxWalkSpeed(InputMagnitude);
		AddBlockMovementTag();      // AnimBP 读到 Starting + BlockMovement → 播起步蒙太奇
		return;
	}

	// 停步：摇杆从有到无 → 快照当前 Speed 供 AnimBP 选 Walk_Stop / Run_Stop
	if (!bHasInput && bHasMovementInput)
	{
		TransitionState = ETransitionState::Stopping;
		bHasMovementInput = false;
		SnapSpeedAtRelease = GetVelocity().Size2D();  // ★ 快照松手瞬间速度
		GetCharacterMovement()->Velocity = FVector::ZeroVector;
		AddBlockMovementTag();      // AnimBP 读到 Stopping + BlockMovement → 播停步蒙太奇
		return;
	}

	// 无输入且不在移动
	if (!bHasInput)
	{
		return;
	}

	// ── 常速移动段：CMC 驱动 ──
	const float InvMag = 1.f / InputMagnitude;
	const float NormRight = Right * InvMag;
	const float NormForward = Forward * InvMag;

	UpdateMaxWalkSpeed(InputMagnitude);

	AddMovementInput(ForwardDirection, NormForward);
	AddMovementInput(RightDirection, NormRight);
}

void AMHGZCharacter::DoLook(float Yaw, float Pitch)
{
	if (Controller)
	{
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}
