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
	CMC->RotationRate = FRotator(0.0f, 540.0f, 0.0f); // 540°/s 转向
	CMC->bUseControllerDesiredRotation = false;
	CMC->JumpZVelocity = 500.f;
	CMC->AirControl = 0.15f;        // 低值——惯性主导，接近怪猎手感
	CMC->MaxWalkSpeed = 500.f;
	CMC->MinAnalogWalkSpeed = 20.f;
	CMC->BrakingDecelerationWalking = 2000.f;
	CMC->BrakingDecelerationFalling = 80.f; // 空中水平衰减
	CMC->GravityScale = 1.8f;              // 空中重力倍率

	// 缓存基础行走速度
	BaseMaxWalkSpeed = CMC->MaxWalkSpeed;

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
	GetCharacterMovement()->MaxWalkSpeed = BaseMaxWalkSpeed * Multiplier;
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

void AMHGZCharacter::DoMove(float Right, float Forward)
{
	if (Controller)
	{
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		AddMovementInput(ForwardDirection, Forward);
		AddMovementInput(RightDirection, Right);
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

void AMHGZCharacter::DoJumpStart()
{
	Jump();
}

void AMHGZCharacter::DoJumpEnd()
{
	StopJumping();
}
