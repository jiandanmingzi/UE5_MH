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
#include "Equipment/MHGZEquipmentComponent.h"
#include "Equipment/MHGZEquipmentDefinition.h"
#include "Equipment/MHGZEquipmentInstance.h"

AMHGZCharacter::AMHGZCharacter()
{
	// 碰撞胶囊体
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// CMC 配置 —— 碰撞壳子，位移由 AnimBP RootMotion 全权驱动
	UCharacterMovementComponent* CMC = GetCharacterMovement();
	CMC->bOrientRotationToMovement = false;    // 旋转由 DoMove 手动 RInterpTo
	CMC->bUseControllerDesiredRotation = false;
	CMC->RotationRate = FRotator::ZeroRotator;
	CMC->JumpZVelocity = 500.f;
	CMC->AirControl = 0.01f;
	CMC->BrakingDecelerationFalling = 80.f;
	CMC->GravityScale = 1.0f;
	// MaxWalkSpeed 设到足够大，防止 CMC 钳制 RootMotion 速度
	CMC->MaxWalkSpeed = 1200.f;

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

	AMHGZPlayerState* PS = GetPlayerState<AMHGZPlayerState>();
	if (!PS) return;

	UMHGZAbilitySystemComponent* ASC = PS->GetMHGZAbilitySystemComponent();
	if (ensure(ASC))
	{
		ASC->InitAbilityActorInfo(PS, this);
		ASC->InitializeAbilitySystem();
		EquipDefaultWeaponIfConfigured();
	}
}

void AMHGZCharacter::EquipDefaultWeaponIfConfigured()
{
	if (!DefaultWeaponDefinition) return;

	AMHGZPlayerState* PS = GetPlayerState<AMHGZPlayerState>();
	if (!PS || !PS->GetEquipmentComponent()) return;

	const FGameplayTag WeaponSlot =
		FGameplayTag::RequestGameplayTag(TEXT("Equipment.Slot.Weapon"));
	if (PS->GetEquipmentComponent()->GetEquippedItem(WeaponSlot)) return;

	UMHGZEquipmentInstance* Instance =
		UMHGZEquipmentInstance::CreateEquipmentInstance(PS, DefaultWeaponDefinition);
	if (Instance)
	{
		PS->GetEquipmentComponent()->EquipItem(WeaponSlot, Instance);
	}
}

void AMHGZCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);

	AMHGZPlayerState* PS = GetPlayerState<AMHGZPlayerState>();
	if (!PS) return;

	UMHGZAbilitySystemComponent* ASC = PS->GetMHGZAbilitySystemComponent();
	if (!ASC) return;

	ASC->RemoveLooseGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aerial")));
	ASC->RemoveLooseGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aerial.Falling")));
	ASC->RemoveLooseGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aerial.CantDodge")));
	ASC->RemoveLooseGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aerial.CantAttack")));
	ASC->AddLooseGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Grounded")));

	if (UGA_WeaponComboCoordinator* Coord = ASC->GetActiveComboCoordinator())
	{
		Coord->OnLanded();
	}
}

// ── Tick 兜底 ────────────────────────────────────────────────
// 每帧必跑——无论 IA 是否在本帧触发了 DoMove，DesiredSpeed 的衰减都能正常执行

void AMHGZCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 每帧更新拔刀态标记——AnimBP 读此值切换 Database_Armed / Database_Unarmed
	{
		AMHGZPlayerState* PS = GetPlayerState<AMHGZPlayerState>();
		if (PS)
		{
			UMHGZAbilitySystemComponent* ASC = PS->GetMHGZAbilitySystemComponent();
			bUnsheathed = ASC && ASC->HasMatchingGameplayTag(
				FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Unsheathed")));
		}
	}

	if (ShouldBlockMovement()) {
		TargetCruiseSpeed = 0.f;
		DesiredSpeed = 0.f;
		bForceMMIdle = true;
		return;
	}

	const uint64 CurrentFrame = GFrameCounter;

	// IA 漏帧兜底：本帧 DoMove 没跑 → DesiredSpeed 衰减
	if (CurrentFrame != LastTheoryUpdateFrame)
	{
		TargetCruiseSpeed = 0.f;
		DesiredSpeed = FMath::FInterpTo(DesiredSpeed, 0.f, DeltaTime, DesiredSpeedInterpSpeed);
	}

	// 旋转（每帧，在 Trajectory 生成之前）
	if (bHasInput)
	{
		const float TargetYaw = LastMovementInputDir.Rotation().Yaw;
		const float NewYaw = FMath::FInterpTo(
			GetActorRotation().Yaw, TargetYaw, DeltaTime, TurnRate);
		SetActorRotation(FRotator(0.0, NewYaw, 0.0));
	}

}

// ── 巡航速度计算 ──────────────────────────────────────────────

float AMHGZCharacter::CalcCruiseSpeed(float StickMagnitude) const
{
	if (StickMagnitude < MoveDeadzone) return 0.f;

	// 拔刀态：走跑合一，单速
	AMHGZPlayerState* PS = GetPlayerState<AMHGZPlayerState>();
	if (PS)
	{
		UMHGZAbilitySystemComponent* ASC = PS->GetMHGZAbilitySystemComponent();
		if (ASC && ASC->HasMatchingGameplayTag(
			FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Unsheathed"))))
		{
			return RunCruise_Unsheathed;
		}
	}

	// 收刀态：摇杆幅度映射三档速度
	// 0.1 ≤ 摇杆 < 0.5 → 线性映射 0 → WalkCruise_Sheathed
	// 0.5 ≤ 摇杆 ≤ 1.0 → 线性映射 WalkCruise_Sheathed → RunCruise_Sheathed
	if (StickMagnitude < 0.5f)
	{
		const float T = (StickMagnitude - MoveDeadzone) / (0.5f - MoveDeadzone);
		return FMath::Lerp(0.f, WalkCruise_Sheathed, FMath::Clamp(T, 0.f, 1.f));
	}
	else if (StickMagnitude <= 0.9f)
	{
		const float T = (StickMagnitude - 0.5f) / 0.4f;
		return FMath::Lerp(WalkCruise_Sheathed, RunCruise_Sheathed, FMath::Clamp(T, 0.f, 1.f));
	}
	else
	{
		// 摇杆 > 0.9 且冲刺键按住 → SprintCruise
		return bSprintHeld ? SprintCruise : RunCruise_Sheathed;
	}
}

// ── 输入 ────────────────────────────────────────────────────

void AMHGZCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// Moving
		if (MoveAction)
		{
			EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AMHGZCharacter::Move);
			EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Completed, this, &AMHGZCharacter::Move);
		}

		// Looking
		if (LookAction)
		{
			EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AMHGZCharacter::Look);
		}
		if (MouseLookAction)
		{
			EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &AMHGZCharacter::Look);
		}

		// Sprint (LS/L3 hold)
		if (SprintAction)
		{
			EnhancedInputComponent->BindAction(SprintAction, ETriggerEvent::Started, this, &AMHGZCharacter::SprintPressed);
			EnhancedInputComponent->BindAction(SprintAction, ETriggerEvent::Completed, this, &AMHGZCharacter::SprintReleased);
			EnhancedInputComponent->BindAction(SprintAction, ETriggerEvent::Canceled, this, &AMHGZCharacter::SprintReleased);
		}

		if (AimAction)
		{
			EnhancedInputComponent->BindAction(AimAction, ETriggerEvent::Started,
				this, &AMHGZCharacter::AimPressed);
			EnhancedInputComponent->BindAction(AimAction, ETriggerEvent::Completed,
				this, &AMHGZCharacter::AimReleased);
			EnhancedInputComponent->BindAction(AimAction, ETriggerEvent::Canceled,
				this, &AMHGZCharacter::AimReleased);
		}
	}
	else
	{
		UE_LOG(LogMHGZ, Error, TEXT("Failed to find Enhanced Input component!"));
	}

	// PossessedBy 可能早于 PlayerController::InputComponent 创建。此处输入组件已确定就绪，
	// 再执行一次幂等初始化，保证 ASC 的 IA -> GameplayTag 路由实际完成绑定。
	if (UMHGZAbilitySystemComponent* ASC =
		Cast<UMHGZAbilitySystemComponent>(GetAbilitySystemComponent()))
	{
		ASC->InitializeAbilitySystem();
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
}

void AMHGZCharacter::SprintReleased(const FInputActionValue& Value)
{
	bSprintHeld = false;
}

void AMHGZCharacter::AimPressed()
{
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		ASC->AddLooseGameplayTag(
			FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aiming")));
	}
}

void AMHGZCharacter::AimReleased()
{
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		ASC->RemoveLooseGameplayTag(
			FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aiming")));
	}
}

FVector AMHGZCharacter::GetLastMovementInputDir() const
{
	return LastMovementInputDir;
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
	if (!Controller) return;

	// 1. 计算世界方向 + 存储 LastMovementInputDir
	const FRotator YawRotation(0, Controller->GetControlRotation().Yaw, 0);
	const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

	const FVector InputVector = ForwardDirection * Forward + RightDirection * Right;
	if (!InputVector.IsNearlyZero())
	{
		LastMovementInputDir = InputVector.GetSafeNormal();
	}

	// 2. 更新 InputMagnitude 和 bHasInput（始终更新，供 AnimBP 读）
	const float Mag = FMath::Sqrt(Right * Right + Forward * Forward);
	bHasInput = Mag >= MoveDeadzone;
	InputMagnitude = bHasInput ? Mag : 0.f;

	// 3. BlockMovement → 全部归零，强制 MM 切 Idle
	if (ShouldBlockMovement())
	{
		TargetCruiseSpeed = 0.f;
		DesiredSpeed = 0.f;
		bForceMMIdle = true;
		return;
	}
	bForceMMIdle = false;

	// 4. 计算目标巡航速度 + 平滑期望速度
	const float DeltaTime = GetWorld()->GetDeltaSeconds();
	TargetCruiseSpeed = CalcCruiseSpeed(InputMagnitude);
	DesiredSpeed = FMath::FInterpTo(DesiredSpeed, TargetCruiseSpeed, DeltaTime, DesiredSpeedInterpSpeed);

	// 5. 记录帧号——Tick 中不再重复计算速度
	LastTheoryUpdateFrame = GFrameCounter;
}

void AMHGZCharacter::DoLook(float Yaw, float Pitch)
{
	if (Controller)
	{
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}
