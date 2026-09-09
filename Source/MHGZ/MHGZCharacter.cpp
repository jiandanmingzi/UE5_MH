// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZCharacter.h"
#include "MHGZ.h"
#include "Animation/MHGZMotionMatchingMath.h"
#include "MHGZPlayerState.h"
#include "Engine/LocalPlayer.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "MotionWarpingComponent.h"
#include "UI/MHGZAimComponent.h"
#include "InputSystem/MHGZEdgeVaultComponent.h"
#include "InputSystem/MHGZInputComponent.h"
#include "ActionSystem/MHGZIncomingHitResolverComponent.h"
#include "ActionSystem/MHGZHitFeedbackRouterComponent.h"
#include "ActionSystem/MHGZHitStopControllerComponent.h"
#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "ActionSystem/MHGZComboCoordinatorAbility.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"
#include "MHGZPlayerController.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "Equipment/MHGZEquipmentComponent.h"
#include "Equipment/MHGZEquipmentDefinition.h"
#include "Equipment/MHGZEquipmentInstance.h"
#include "ReferenceSkeleton.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
const FName DemoHeadAttachmentBone(TEXT("Head_00"));

bool GetReferenceBoneComponentTransform(const USkeletalMesh* SkeletalMesh, const FName BoneName,
	FTransform& OutTransform)
{
	if (!SkeletalMesh)
	{
		return false;
	}

	const FReferenceSkeleton& ReferenceSkeleton = SkeletalMesh->GetRefSkeleton();
	int32 BoneIndex = ReferenceSkeleton.FindBoneIndex(BoneName);
	if (BoneIndex == INDEX_NONE)
	{
		return false;
	}

	OutTransform = FTransform::Identity;
	for (; BoneIndex != INDEX_NONE; BoneIndex = ReferenceSkeleton.GetParentIndex(BoneIndex))
	{
		OutTransform = OutTransform * ReferenceSkeleton.GetRefBonePose()[BoneIndex];
	}
	return true;
}

void ConfigureHeadModule(USkeletalMeshComponent* MeshComponent, USkeletalMesh* SkeletalMesh)
{
	if (!MeshComponent || !SkeletalMesh)
	{
		return;
	}

	MeshComponent->SetSkeletalMesh(SkeletalMesh);

	// A Blueprint can retain old component-template values.  Keep this setup in
	// one function and run it again after components are initialized so an old
	// AnimBP, leader-pose binding, or relative transform cannot win over it.
	MeshComponent->SetAnimationMode(EAnimationMode::AnimationSingleNode);

	FTransform HeadBoneReferenceTransform;
	if (GetReferenceBoneComponentTransform(SkeletalMesh, DemoHeadAttachmentBone,
		HeadBoneReferenceTransform))
	{
		// The module is attached at the live Head_00 bone. Cancelling its own
		// reference Head_00 transform makes the two Head_00 transforms coincide.
		MeshComponent->SetRelativeTransform(HeadBoneReferenceTransform.Inverse());
	}
}

void RestoreHeadModuleAttachment(USkeletalMeshComponent* MeshComponent)
{
	if (!MeshComponent)
	{
		return;
	}

	MeshComponent->SetLeaderPoseComponent(nullptr);
	MeshComponent->SetAnimationMode(EAnimationMode::AnimationSingleNode);

	FTransform HeadBoneReferenceTransform;
	if (GetReferenceBoneComponentTransform(MeshComponent->GetSkeletalMeshAsset(),
		DemoHeadAttachmentBone, HeadBoneReferenceTransform))
	{
		MeshComponent->SetRelativeTransform(HeadBoneReferenceTransform.Inverse());
	}
}
}

AMHGZCharacter::AMHGZCharacter()
{
	// 碰撞胶囊体
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// CMC 配置 —— 碰撞壳子，位移由 AnimBP RootMotion 全权驱动
	UCharacterMovementComponent* CMC = GetCharacterMovement();
	CMC->bOrientRotationToMovement = false;    // 旋转由 Tick 按最大角速度手动更新
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

	// Body、Head 与 Hair 是独立 SkeletalMesh。它们的同名骨骼有不同的祖先链，
	// 因此不能使用 Copy Pose；模块挂到最终 Head_00 骨，并抵消自身参考 Head_00
	// 变换，使各模块的 Head_00 在参考姿势和运行时都严格重合。
	HeadMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("HeadMesh"));
	HeadMesh->SetupAttachment(GetMesh(), DemoHeadAttachmentBone);
	HeadMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HeadMesh->SetGenerateOverlapEvents(false);
	HeadMesh->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> DemoHeadMesh(
		TEXT("/Game/Characters/Demo/Meshes/Head/SKM_Demo_Head.SKM_Demo_Head"));
	if (DemoHeadMesh.Succeeded())
	{
		ConfigureHeadModule(HeadMesh, DemoHeadMesh.Object);
	}

	HairMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("HairMesh"));
	HairMesh->SetupAttachment(HeadMesh, DemoHeadAttachmentBone);
	HairMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HairMesh->SetGenerateOverlapEvents(false);
	HairMesh->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> DemoHairMesh(
		TEXT("/Game/Characters/Demo/Meshes/Hair/SKM_Demo_Hair.SKM_Demo_Hair"));
	if (DemoHairMesh.Succeeded())
	{
		ConfigureHeadModule(HairMesh, DemoHairMesh.Object);
	}

	// MotionWarping
	MotionWarpingComponent = CreateDefaultSubobject<UMotionWarpingComponent>(TEXT("MotionWarpingComponent"));

	// AimComponent
	AimComponent = CreateDefaultSubobject<UMHGZAimComponent>(TEXT("AimComponent"));

	// EdgeVault
	EdgeVaultComponent = CreateDefaultSubobject<UMHGZEdgeVaultComponent>(TEXT("EdgeVaultComponent"));

	// IncomingHitResolver
	IncomingHitResolver =
		CreateDefaultSubobject<UMHGZIncomingHitResolverComponent>(TEXT("IncomingHitResolver"));

	// M2 parallel-domain dependencies: these headers are provided by the feedback writer.
	HitFeedbackRouter =
		CreateDefaultSubobject<UMHGZHitFeedbackRouterComponent>(TEXT("HitFeedbackRouter"));
	HitStopController =
		CreateDefaultSubobject<UMHGZHitStopControllerComponent>(TEXT("HitStopController"));
}

UAbilitySystemComponent* AMHGZCharacter::GetAbilitySystemComponent() const
{
	if (AMHGZPlayerState* PS = GetPlayerState<AMHGZPlayerState>())
	{
		return PS->GetMHGZAbilitySystemComponent();
	}
	return nullptr;
}

void AMHGZCharacter::PostLoad()
{
	Super::PostLoad();

	// Blueprint component templates are deserialized after the native
	// constructor. Restore the module setup here as well, so asset previews use
	// the corrected transform instead of an old serialized value.
	RestoreHeadModuleAttachment(HeadMesh);
	RestoreHeadModuleAttachment(HairMesh);
}

void AMHGZCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Last runtime guard in case an inherited Blueprint construction script or
	// an old component template changes a module after its native construction.
	RestoreHeadModuleAttachment(HeadMesh);
	RestoreHeadModuleAttachment(HairMesh);
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
		if (UMHGZWeaponRuntimeHostComponent* RuntimeHost = GetWeaponRuntimeHost())
		{
			RuntimeHost->InitializePawnRuntime(this, Cast<APlayerController>(NewController), ASC,
				PS->GetEquipmentComponent());
		}
		EquipDefaultWeaponIfConfigured();

		// ASC Init 完成后显式绑定 Aim 生命周期（BeginPlay 的尝试可能早于 Possession）
		if (AimComponent)
		{
			AimComponent->BindToAbilitySystem(ASC);
		}
	}
}

void AMHGZCharacter::UnPossessed()
{
	// Aim 解绑先于武器运行时关闭，避免 Tick 期间访问失效 ASC
	if (AimComponent)
	{
		AimComponent->UnbindFromAbilitySystem();
	}
	ClearSprintHeld();

	if (UMHGZWeaponRuntimeHostComponent* RuntimeHost = GetWeaponRuntimeHost())
	{
		RuntimeHost->ShutdownRuntime(EWeaponRuntimeEndReason::AvatarChanged);
	}
	Super::UnPossessed();
}

void AMHGZCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearSprintHeld();
	Super::EndPlay(EndPlayReason);
}

void AMHGZCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// 显式覆写旧 Blueprint 序列化的 Leader Pose、AnimBP 与 Relative Transform，
	// 避免它们把模块拉回曾经的错误骨架空间。
	RestoreHeadModuleAttachment(HeadMesh);
	RestoreHeadModuleAttachment(HairMesh);
}

UMHGZWeaponRuntimeHostComponent* AMHGZCharacter::GetWeaponRuntimeHost() const
{
	return FindComponentByClass<UMHGZWeaponRuntimeHostComponent>();
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
	if (UMHGZWeaponRuntimeHostComponent* RuntimeHost = GetWeaponRuntimeHost())
	{
		RuntimeHost->HandleLanded();
	}

	AMHGZPlayerState* PS = GetPlayerState<AMHGZPlayerState>();
	if (!PS) return;

	UMHGZAbilitySystemComponent* ASC = PS->GetMHGZAbilitySystemComponent();
	if (!ASC) return;

	if (UGA_WeaponComboCoordinator* Coord = ASC->GetActiveComboCoordinator())
	{
		Coord->OnLanded(Hit);
	}
}

void AMHGZCharacter::OnMovementModeChanged(
	EMovementMode PrevMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PrevMovementMode, PreviousCustomMode);
	if (UMHGZWeaponRuntimeHostComponent* RuntimeHost = GetWeaponRuntimeHost())
	{
		RuntimeHost->SetGrounded(GetCharacterMovement()
			&& !GetCharacterMovement()->IsFalling());
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

	UMHGZWeaponRuntimeHostComponent* RuntimeHost = GetWeaponRuntimeHost();
	const bool bBlockMovement = ShouldBlockMovement();
	bForceMMIdle = bBlockMovement
		|| (RuntimeHost && RuntimeHost->IsMontageRootMotionOwned());
	if (bBlockMovement)
	{
		// 保留 RawMoveInput/LastMovementInputDir 给输入快照和动作入口方向，
		// 但绝不能把锁定期间的摇杆泄露给 AnimBP 的 locomotion 分支。
		InputMagnitude = 0.f;
		bHasInput = false;
		TargetCruiseSpeed = 0.f;
		DesiredSpeed = 0.f;
		LocomotionDownshiftProbe.Reset();
		RefreshDownshiftConfirmDebugState();
		return;
	}

	const uint64 CurrentFrame = GFrameCounter;

	// IA 漏帧兜底：本帧 DoMove 没跑 → 清空 locomotion 意图并让 DesiredSpeed 衰减。
	if (CurrentFrame != LastTheoryUpdateFrame)
	{
		InputMagnitude = 0.f;
		bHasInput = false;
		TargetCruiseSpeed = 0.f;
		DesiredSpeed = FMath::FInterpTo(DesiredSpeed, 0.f, DeltaTime, DesiredSpeedInterpSpeed);
		LocomotionDownshiftProbe.Reset();
		RefreshDownshiftConfirmDebugState();
	}

	// 旋转（每帧，在 Trajectory 生成之前）
	if (bHasInput)
	{
		const float TargetYaw = LastMovementInputDir.Rotation().Yaw;
		const float CurrentYaw = GetActorRotation().Yaw;
		const float DeltaYaw = FMath::FindDeltaAngleDegrees(CurrentYaw, TargetYaw);
		const float MaxYawStep = FMath::Max(0.f, TurnRate) * DeltaTime;
		const float NewYaw = FMath::UnwindDegrees(
			CurrentYaw + FMath::Clamp(DeltaYaw, -MaxYawStep, MaxYawStep));
		SetActorRotation(FRotator(0.0, NewYaw, 0.0));
	}

}

// ── 巡航速度计算 ──────────────────────────────────────────────

float AMHGZCharacter::CalcCruiseSpeed(float StickMagnitude) const
{
	// 普通移动只有 Walk / Run / Sprint / Unsheathed 四条正式 Root Motion
	// Loop。TargetCruiseSpeed 必须只输出这些动画真实拥有的速度档位，不能再
	// 输出 PSS 中不存在的中间速度，否则 Pose Search 会在 Loop 之间摇摆。
	MHGZMotionMatching::FMHGZMotionMatchingCruiseSpeedSettings Settings;
	Settings.RawMoveDeadzone = MoveDeadzone;
	Settings.SheathedWalkInputThreshold = SheathedWalkInputThreshold;
	Settings.SheathedRunInputThreshold = SheathedRunInputThreshold;
	Settings.SprintInputThreshold = SprintInputThreshold;
	Settings.WalkCruiseSpeed = WalkCruise_Sheathed;
	Settings.RunCruiseSpeed = RunCruise_Sheathed;
	Settings.SprintCruiseSpeed = SprintCruise;
	Settings.UnsheathedCruiseSpeed = RunCruise_Unsheathed;
	return MHGZMotionMatching::QuantizeCruiseSpeed(Settings, StickMagnitude,
		IsUnsheathedForLocomotion(), bSprintHeld);
}

bool AMHGZCharacter::IsUnsheathedForLocomotion() const
{
	AMHGZPlayerState* PS = GetPlayerState<AMHGZPlayerState>();
	if (!PS)
	{
		return false;
	}

	UMHGZAbilitySystemComponent* ASC = PS->GetMHGZAbilitySystemComponent();
	return ASC && ASC->HasMatchingGameplayTag(
		FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Unsheathed")));
}

void AMHGZCharacter::RefreshDownshiftConfirmDebugState()
{
	bDownshiftConfirmActive = LocomotionDownshiftProbe.bActive;
	DownshiftConfirmRemaining = bDownshiftConfirmActive
		? FMath::Max(0.0f, DownshiftConfirmDuration - LocomotionDownshiftProbe.ElapsedSeconds)
		: 0.0f;
}

float AMHGZCharacter::GetQuantizedCruiseSpeedForRawMoveInput(const float StickMagnitude) const
{
	return CalcCruiseSpeed(StickMagnitude);
}

// ── 输入 ────────────────────────────────────────────────────

void AMHGZCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	if (AMHGZPlayerController* MHGZPC = Cast<AMHGZPlayerController>(GetController()))
	{
		if (UMHGZInputComponent* InputOwner = MHGZPC->GetMHGZInputComponent())
		{
			InputOwner->InitializeInput(MHGZPC, MHGZPC->GetWeaponInputRouter());
		}
	}
}

void AMHGZCharacter::BindCharacterInput(
	UEnhancedInputComponent* EnhancedInputComponent, TArray<uint32>& OutBindingHandles)
{
	if (!EnhancedInputComponent) return;
	auto Remember = [&OutBindingHandles](FEnhancedInputActionEventBinding& Binding)
	{
		OutBindingHandles.Add(Binding.GetHandle());
	};
	if (MoveAction)
	{
		Remember(EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AMHGZCharacter::Move));
		Remember(EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Completed, this, &AMHGZCharacter::Move));
	}
	if (LookAction)
	{
		Remember(EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AMHGZCharacter::Look));
	}
	if (MouseLookAction)
	{
		Remember(EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &AMHGZCharacter::Look));
	}
	if (SprintAction)
	{
		Remember(EnhancedInputComponent->BindAction(SprintAction, ETriggerEvent::Started, this, &AMHGZCharacter::SprintPressed));
		Remember(EnhancedInputComponent->BindAction(SprintAction, ETriggerEvent::Completed, this, &AMHGZCharacter::SprintReleased));
		Remember(EnhancedInputComponent->BindAction(SprintAction, ETriggerEvent::Canceled, this, &AMHGZCharacter::SprintReleased));
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
	// RB（0.1s 判定）：仅收刀态记录按下；持刀态按下不启动
	if (!IsSheathedForSprint())
	{
		return;
	}

	bSprintPressed = true;
	// While locomotion is already active, RB is a direct gait request. Waiting
	// for the legacy hold timer here made Run -> Sprint appear to accelerate
	// gradually even though the player had already committed to movement.
	if (bHasInput)
	{
		bSprintHeld = true;
		return;
	}

	GetWorldTimerManager().SetTimer(
		SprintHoldTimer, this, &AMHGZCharacter::OnSprintHoldTimerExpired,
		FMath::Max(0.f, SprintHoldThreshold), false);
}

void AMHGZCharacter::SprintReleased(const FInputActionValue& Value)
{
	ClearSprintHeld();
}

void AMHGZCharacter::ClearSprintHeld()
{
	GetWorldTimerManager().ClearTimer(SprintHoldTimer);
	bSprintPressed = false;
	bSprintHeld = false;
}

void AMHGZCharacter::OnSprintHoldTimerExpired()
{
	SprintHoldTimer.Invalidate();

	// 到期仍按住且仍收刀 → 冲刺成立
	if (bSprintPressed && IsSheathedForSprint())
	{
		bSprintHeld = true;
	}
}

bool AMHGZCharacter::IsSheathedForSprint() const
{
	// 优先 RuntimeHost 权威状态；无 Host（未挂载/测试环境）时回退 ASC Sheathed tag
	if (UMHGZWeaponRuntimeHostComponent* RuntimeHost = GetWeaponRuntimeHost())
	{
		return RuntimeHost->IsSheathed();
	}

	AMHGZPlayerState* PS = GetPlayerState<AMHGZPlayerState>();
	if (!PS) return false;

	UMHGZAbilitySystemComponent* ASC = PS->GetMHGZAbilitySystemComponent();
	return ASC && ASC->HasMatchingGameplayTag(
		FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Sheathed")));
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
	RawMoveInput = FVector2D(Right, Forward);
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

	// 2. 计算本帧原始摇杆幅度。RawMoveInput/LastMovementInputDir 始终保留，
	//    但公开给 AnimBP 的 locomotion 意图会在 BlockMovement 下被屏蔽。
	const float Mag = FMath::Sqrt(Right * Right + Forward * Forward);

	// 3. 移动锁和 Montage 根位移所有权分离：SteeringRootMotion
	//    允许更新速度/朝向，但 MM 仍输出零 RM，避免双重位移。
	UMHGZWeaponRuntimeHostComponent* RuntimeHost = GetWeaponRuntimeHost();
	const bool bBlockMovement = ShouldBlockMovement();
	bForceMMIdle = bBlockMovement
		|| (RuntimeHost && RuntimeHost->IsMontageRootMotionOwned());
	if (bBlockMovement)
	{
		InputMagnitude = 0.f;
		bHasInput = false;
		TargetCruiseSpeed = 0.f;
		DesiredSpeed = 0.f;
		LocomotionDownshiftProbe.Reset();
		RefreshDownshiftConfirmDebugState();
		return;
	}

	// 4. 计算目标巡航速度 + 平滑期望速度
	const float DeltaTime = GetWorld()->GetDeltaSeconds();
	const bool bIsUnsheathed = IsUnsheathedForLocomotion();
	const float RequestedCruiseSpeed = CalcCruiseSpeed(Mag);
	bHasInput = RequestedCruiseSpeed > KINDA_SMALL_NUMBER;
	InputMagnitude = bHasInput ? Mag : 0.f;
	TargetCruiseSpeed = MHGZMotionMatching::ResolveCruiseSpeedWithDownshiftProbe(
		LocomotionDownshiftProbe, RequestedCruiseSpeed, Mag, DeltaTime,
		DownshiftConfirmDuration, !bIsUnsheathed);
	RefreshDownshiftConfirmDebugState();
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
