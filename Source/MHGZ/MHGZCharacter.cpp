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

	// 持续移动阶段的加减速——响应灵敏即可，过渡阶段的位移由 RootMotion 蒙太奇驱动
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

void AMHGZCharacter::UpdateMaxWalkSpeed()
{
	AMHGZPlayerState* PS = GetPlayerState<AMHGZPlayerState>();
	if (!PS) return;

	UMHGZAbilitySystemComponent* ASC = PS->GetMHGZAbilitySystemComponent();
	if (!ASC) return;

	const float Multiplier = ASC->GetNumericAttribute(
		UMHGZAttributeSet::GetMoveSpeedMultiplierAttribute());

	// Sprint 状态决定基准速度：奔跑 500 / 行走 150
	const bool bSprinting = ASC->HasMatchingGameplayTag(
		FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Sprinting")));
	const float BaseSpeed = bSprinting ? RunSpeed : WalkSpeed;

	const float TargetSpeed = BaseSpeed * Multiplier;
	GetCharacterMovement()->MaxWalkSpeed = TargetSpeed;
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

bool AMHGZCharacter::ShouldBlockMovement() const
{
	AMHGZPlayerState* PS = GetPlayerState<AMHGZPlayerState>();
	if (!PS) return false;

	UMHGZAbilitySystemComponent* ASC = PS->GetMHGZAbilitySystemComponent();
	if (!ASC) return false;

	// RootMotion 驱动位移的场景 → 屏蔽 CMC AddMovementInput，避免双重力叠加
	//   - 攻击 Montage（Attacking）
	//   - 起步 Montage（Movement.Starting）——RootMotion 从 0 加速到目标速度
	//   - 停步 Montage（Movement.Stopping）——RootMotion 从目标速度减速到 0
	//   - 受击硬直/击倒/死亡
	static const FGameplayTagContainer BlockMovementTags = FGameplayTagContainer::CreateFromArray({
		FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Attacking")),
		FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Hitstun")),
		FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Knockdown")),
		FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Dead")),
		FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Movement.Starting")),
		FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Movement.Stopping")),
	});

	return ASC->HasAnyMatchingGameplayTag(BlockMovementTags);
}

void AMHGZCharacter::DoMove(float Right, float Forward)
{
	if (ShouldBlockMovement())
	{
		return;
	}

	if (Controller)
	{
		const bool bHasInput = !FMath::IsNearlyZero(Right) || !FMath::IsNearlyZero(Forward);

		if (bHasInput)
		{
			const FRotator Rotation = Controller->GetControlRotation();
			const FRotator YawRotation(0, Rotation.Yaw, 0);

			const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
			const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

			AddMovementInput(ForwardDirection, Forward);
			AddMovementInput(RightDirection, Right);
		}
		else
		{
			// 输入归零 + 速度仍高于阈值 → 进入停步过渡
			AMHGZPlayerState* PS = GetPlayerState<AMHGZPlayerState>();
			if (PS)
			{
				UMHGZAbilitySystemComponent* ASC = PS->GetMHGZAbilitySystemComponent();
				static const FGameplayTag StoppingTag = FGameplayTag::RequestGameplayTag(
					TEXT("Combat.State.Movement.Stopping"));
				if (ASC && GetVelocity().Size2D() > MovementTransitionThreshold
					&& !ASC->HasMatchingGameplayTag(StoppingTag))
				{
					ASC->AddLooseGameplayTag(StoppingTag);
				}
			}
		}
	}
}

void AMHGZCharacter::DoLook(float Yaw, float Pitch)
{
	if (Controller)
	{
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void AMHGZCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	const float CurrentSpeed = GetVelocity().Size2D();
	UpdateMovementTransitionTags(CurrentSpeed);
	LastTickSpeed = CurrentSpeed;
}

void AMHGZCharacter::UpdateMovementTransitionTags(const float CurrentSpeed)
{
	AMHGZPlayerState* PS = GetPlayerState<AMHGZPlayerState>();
	if (!PS) return;

	UMHGZAbilitySystemComponent* ASC = PS->GetMHGZAbilitySystemComponent();
	if (!ASC) return;

	const float TargetSpeed = GetCharacterMovement()->MaxWalkSpeed;

	// ── 起步检测：从静止 → 移动中 ──
	const bool bWasStopped = LastTickSpeed <= MovementTransitionThreshold;
	const bool bIsMoving = CurrentSpeed > MovementTransitionThreshold;

	if (bWasStopped && bIsMoving)
	{
		// 触发起步动画
		ASC->AddLooseGameplayTag(
			FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Movement.Starting")));
	}
	else if (!bIsMoving)
	{
		// 已经完全停止——清除所有移动过渡 Tag
		ASC->RemoveLooseGameplayTag(
			FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Movement.Starting")));
		ASC->RemoveLooseGameplayTag(
			FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Movement.Stopping")));
	}

	// ── 起步完成检测：速度接近目标 → 进入持续移动态 ──
	if (ASC->HasMatchingGameplayTag(
			FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Movement.Starting")))
		&& CurrentSpeed >= TargetSpeed * 0.90f)
	{
		ASC->RemoveLooseGameplayTag(
			FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Movement.Starting")));
	}

	// ── 停步检测：从移动中 → 输入归零开始减速 ──
	//   （由 DoMove 触发——当输入为 0 且速度 > 阈值时设 Stopping Tag）
	const bool bIsDeceleratingToStop = ASC->HasMatchingGameplayTag(
			FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Movement.Stopping")));
	const bool bHasStopped = CurrentSpeed <= MovementTransitionThreshold;

	if (bIsDeceleratingToStop && bHasStopped)
	{
		ASC->RemoveLooseGameplayTag(
			FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Movement.Stopping")));
	}
}
