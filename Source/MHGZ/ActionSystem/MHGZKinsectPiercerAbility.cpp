// Copyright MHGZ Project. All Rights Reserved.

#include "ActionSystem/MHGZKinsectPiercerAbility.h"
#include "ActionSystem/MHGZAerialActionCleanup.h"
#include "MHGZGameplayTags.h"
#include "MHGZMath.h"

#include "ActionSystem/AbilityTask_MHGZPlayMontageAndWait.h"
#include "ActionSystem/AbilityTask_MHGZWeaponMovement.h"
#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "ActionSystem/MHGZComboCoordinatorAbility.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "AttributeSystem/Res_InsectGlaive.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InsectGlaive/InsectGlaiveCombatConfig.h"
#include "MHGZ.h"
#include "MHGZCharacter.h"
#include "Movement/MHGZInstrumentedCharacterMovementComponent.h"
#include "TimerManager.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

namespace
{
const FGameplayTag& KinsectSlashDanceSourceTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Aerial.Dance.Source.KinsectSlash"));
	return Tag;
}

const FGameplayTag& WeaponRTYTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Input.Weapon.RTY"));
	return Tag;
}

/**
 * 入招与续行的共同判据。
 *
 * 来源查的是 **Resource 的 `DanceSource` 字段**，不是标签：字段是真值，标签只是它的
 * 对外镜像（`URes_InsectGlaive::RefreshDanceStateTags`）。两个都查会让人以为标签是
 * 第二真值源；只查标签则会在镜像还没广播出来的那一帧误拒。
 *
 * `Falling` 是**在场**判据而不是「曾经到过」：操虫斩舞踏的保护前段把它放掉，
 * 所以那一段天然拒绝本招，直到它自己的 Aerial Handoff 把标签领回来（§1/§9.2）。
 */
bool IsLiveAerialPiercerEntry(const UAbilitySystemComponent* ASC,
	const UMHGZWeaponRuntimeHostComponent* Host, const URes_InsectGlaive* Resource)
{
	return ASC && Host && Resource && !Host->IsGrounded()
		&& Resource->GetDanceSource() == EIGDanceSource::KinsectSlash
		&& ASC->HasMatchingGameplayTag(MHGZGameplayTags::AerialFalling())
		&& ASC->HasMatchingGameplayTag(MHGZGameplayTags::Unsheathed())
		&& !ASC->HasMatchingGameplayTag(MHGZGameplayTags::Grounded())
		&& !ASC->HasMatchingGameplayTag(MHGZGameplayTags::AerialCantAttack())
		&& !ASC->HasMatchingGameplayTag(MHGZGameplayTags::Dead())
		&& !ASC->HasMatchingGameplayTag(MHGZGameplayTags::Hitstun())
		&& !ASC->HasMatchingGameplayTag(MHGZGameplayTags::Knockdown());
}

/**
 * 从**参数里传进来的** ASC 解析虫棍资源。
 *
 * `CanActivateAbility` 是引擎在 `SetCurrentActorInfo` 之前就会调的纯查询，所以那条路上
 * 不能走 `GetIGResourceComponent()` —— 它经 `GetAbilitySystemComponentFromActorInfo()`
 * 读 `CurrentActorInfo`，在查询期还没设好。资源查找本身就是「Host 的 ResourceProvider」，
 * 这里直接问 Host，不经过任何依赖 `CurrentActorInfo` 的入口。
 */
const URes_InsectGlaive* ResolveResourceFromASC(const UAbilitySystemComponent* ASC)
{
	const UMHGZAbilitySystemComponent* MHGZASC =
		Cast<UMHGZAbilitySystemComponent>(ASC);
	const UMHGZWeaponRuntimeHostComponent* Host =
		MHGZASC ? MHGZASC->GetRuntimeHost() : nullptr;
	return Host ? Cast<URes_InsectGlaive>(Host->GetResourceProvider()) : nullptr;
}
}

UMHGZKinsectPiercerAbility::UMHGZKinsectPiercerAbility()
{
	// 200/201/203实录没有扣耐，且连空中段也以地面20点/秒回复。
	StaminaRecoveryOverride = 20.f;
	InputTag = WeaponRTYTag();
	DefaultEntrySection = StartSectionName;

	ActivationRequiredTags.AddTag(MHGZGameplayTags::AerialFalling());
	ActivationRequiredTags.AddTag(MHGZGameplayTags::Unsheathed());
	ActivationBlockedTags.AddTag(MHGZGameplayTags::Grounded());
	ActivationBlockedTags.AddTag(MHGZGameplayTags::AerialCantAttack());
	ActivationBlockedTags.AddTag(MHGZGameplayTags::Dead());
	ActivationBlockedTags.AddTag(MHGZGameplayTags::Hitstun());
	ActivationBlockedTags.AddTag(MHGZGameplayTags::Knockdown());
}

bool UMHGZKinsectPiercerAbility::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags,
		OptionalRelevantTags))
	{
		return false;
	}

	const UMHGZAbilitySystemComponent* ASC = ActorInfo
		? Cast<UMHGZAbilitySystemComponent>(ActorInfo->AbilitySystemComponent.Get())
		: nullptr;
	const UMHGZWeaponRuntimeHostComponent* Host = ASC ? ASC->GetRuntimeHost() : nullptr;
	return IsLiveAerialPiercerEntry(ASC, Host,
		ResolveResourceFromASC(ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr));
}

bool UMHGZKinsectPiercerAbility::ValidateActionDependencies() const
{
	if (!Super::ValidateActionDependencies())
	{
		return false;
	}

	const FWeaponInputSnapshot& Input = GetWeaponActivationContext().Input;
	if (!Input.ResolvedInputTag.MatchesTagExact(WeaponRTYTag()))
	{
		return false;
	}

	FName SelectedEntry;
	if (!SelectAttackMontageStartSection(SelectedEntry)
		|| SelectedEntry != StartSectionName
		|| !HasRequiredSections(AttackMontage)
		|| !HasRequiredLandingMontage())
	{
		return false;
	}

	const UMHGZAbilitySystemComponent* ASC = Cast<UMHGZAbilitySystemComponent>(
		GetAbilitySystemComponentFromActorInfo());
	const UMHGZWeaponRuntimeHostComponent* Host = ASC ? ASC->GetRuntimeHost() : nullptr;
	const URes_InsectGlaive* Resource = GetUsableResource();
	const ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!IsLiveAerialPiercerEntry(ASC, Host, Resource) || !Character
		|| !Character->GetMesh() || !Character->GetMesh()->GetAnimInstance()
		|| !HasUsableTrajectoryProfiles())
	{
		return false;
	}

	// 三段都是**武器**判定：各段必须有自己的伤害 GE、正动作值与真实轨迹区域。
	// 本招不复用操虫斩 [0] 的猎虫伤害/萃取逻辑，所以不检查附着 Socket。
	if (AttackSegments.Num() != 3)
	{
		return false;
	}
	for (int32 SegmentIndex = 0; SegmentIndex < 3; ++SegmentIndex)
	{
		const FAttackSegmentConfig& Segment = AttackSegments[SegmentIndex];
		if (!Segment.Damage.DamageEffectClass
			|| Segment.Damage.MotionValue.GetValueAtLevel(1.0f) <= 0.0f
			|| Segment.Damage.bRequiresHitToContinue
			|| Segment.MultiHitCount != 1
			|| Segment.MultiHitPolicy != EAttackMultiHitPolicy::ContactOnly
			|| Segment.Collision.TraceRegions.IsEmpty())
		{
			return false;
		}
	}
	return true;
}

const URes_InsectGlaive* UMHGZKinsectPiercerAbility::GetUsableResource() const
{
	return GetIGResourceComponent();
}

bool UMHGZKinsectPiercerAbility::HasUsableTrajectoryProfiles() const
{
	const URes_InsectGlaive* Resource = GetUsableResource();
	const UInsectGlaiveCombatConfig* Config = Resource ? Resource->GetCombatConfig() : nullptr;
	if (!Config
		|| !FMath::IsFinite(KinsectPiercerDiveWatchdogSeconds)
		|| KinsectPiercerDiveWatchdogSeconds <= 0.0f)
	{
		return false;
	}
	// 203 由 Montage Root Motion 驱动，旧 LandingProfile 不参与激活门禁。
	return KinsectPiercerStartProfile.IsValid()
		&& KinsectPiercerDiveProfile.IsValid();
}

bool UMHGZKinsectPiercerAbility::HasRequiredSections(const UAnimMontage* Montage) const
{
	if (!Montage || StartSectionName.IsNone() || DiveSectionName.IsNone()
		|| DiveLoopSectionName.IsNone())
	{
		return false;
	}

	const FName Sections[] = { StartSectionName, DiveSectionName, DiveLoopSectionName };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Sections); ++Index)
	{
		if (!Montage->IsValidSectionName(Sections[Index]))
		{
			return false;
		}
		for (int32 PreviousIndex = 0; PreviousIndex < Index; ++PreviousIndex)
		{
			if (Sections[Index] == Sections[PreviousIndex])
			{
				return false;
			}
		}
	}
	return DefaultEntrySection == StartSectionName;
}

bool UMHGZKinsectPiercerAbility::HasRequiredLandingMontage() const
{
	if (!LandingMontage || LandingSectionName.IsNone()
		|| !LandingMontage->IsValidSectionName(LandingSectionName))
	{
		return false;
	}
	// 落地 Montage 是**单个** Section、无自循环：它必须有自然的末尾，
	// 因为 203 的自然收尾要等它的真实末尾回调（§6.3.5）。
	return LandingMontage->CompositeSections.Num() == 1
		&& LandingMontage->CompositeSections[0].NextSectionName.IsNone()
		&& LandingMontage->HasRootMotion();
}

FVector UMHGZKinsectPiercerAbility::ResolveLockedYawDirection() const
{
	const FWeaponInputSnapshot& Input = GetWeaponActivationContext().Input;
	FVector Direction(Input.Aim.Direction.X, Input.Aim.Direction.Y, 0.0);
	Direction = Direction.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		// 快照没有可用准心射线时的兜底：输入瞬间的角色朝向。它仍然是**冻结**的，
		// 不会在动作中途跟随摇杆或镜头。
		Direction = FVector(Input.ActorForward.X, Input.ActorForward.Y, 0.0).GetSafeNormal();
	}
	return Direction;
}

bool UMHGZKinsectPiercerAbility::ApplyLockedYaw()
{
	const FVector Direction = ResolveLockedYawDirection();
	if (!MHGZMath::IsFiniteVector(Direction) || Direction.IsNearlyZero())
	{
		return false;
	}

	AMHGZCharacter* Character = Cast<AMHGZCharacter>(GetAvatarActorFromActorInfo());
	if (!Character)
	{
		return false;
	}

	// 只改 Yaw：镜头俯仰既不能驱动朝向，也不能进入曲线（§5.1）。
	const FRotator Current = Character->GetActorRotation();
	const float DeltaYaw = FMath::FindDeltaAngleDegrees(Current.Yaw, Direction.Rotation().Yaw);
	const float MaxCorrection = ResolveActivationMaxCorrectionAngle(GetWeaponActivationContext());
	if (!FMath::IsFinite(MaxCorrection) || FMath::Abs(DeltaYaw) > MaxCorrection)
	{
		// Combo 边给的是 180（§4.3），正常永远进不来。真进来了就是配置错误：
		// 与其让身体朝一边、轨迹朝另一边，不如拒绝这次激活。
		UE_LOG(LogMHGZ, Warning,
			TEXT("[KinsectPiercer] Locked yaw %.2f deg exceeds the resolved %.2f deg cap; refusing activation."),
			DeltaYaw, MaxCorrection);
		return false;
	}

	Character->SetActorRotation(FRotator(Current.Pitch,
		FMath::UnwindDegrees(Current.Yaw + DeltaYaw), Current.Roll));
	LockedYawDirection = Direction;
	return true;
}

void UMHGZKinsectPiercerAbility::ApplyActivationDirectionCorrection()
{
	// 本招的方向来自解析输入时冻结的**准心射线**，不是摇杆：第三人称视差会把两者
	// 分开好几度（§5.1）。真正的转向与锁定在 ActivateAbility 里一次做完，
	// 这里不先做一次基类的摇杆修正，否则会多出一次可见的朝向抖动。
}

bool UMHGZKinsectPiercerAbility::PrepareAttackMontage()
{
	if (!HasRequiredSections(AttackMontage) || !HasRequiredLandingMontage())
	{
		return false;
	}

	// §6.2 Start200 那行的「入招后释放 Falling 许可并持有攻击锁」。放在这里是因为它
	// 恰好是「已 Commit、蒙太奇还没起播」这一帧 —— 锁必须在表现开始**之前**就位。
	//
	// 这一次释放同时办三件事，少任何一件都会在真实资产上出问题：
	//
	//  1. 激活提交时已由 ConsumeAerialAttackAndDodgeBudgets() 同时消费 CantAttack 与
	//     CantDodge；因此 Falling 仍在场的 200/201/未命中下坠阶段都不能空回。
	//     这里释放 Falling 是另一项职责：关闭“Falling 在场即可输入空中攻击”的入口，
	//     防止本动作的 Montage lead/内部阶段再次被当作新空中攻击入口。Attacking、
	//     BlockMovement、CantAttack 本身并不拦空回；这条不变量由 Host 持有的 CantDodge
	//     预算保证。舞踏提交时 Host 会重置两种预算。
	//
	//  2. 停掉系统托管下落的**表现会话**。本招刻意不挂 `IG_AerialHandoff`（§6.4），
	//     没有别人会替我们停它。
	//
	//  3. 于是真实触地时 `Host::HandleLanded` 不会按「系统托管下落结束了」播系统落地表现 ——
	//     那一次会把水平速度重设成 337 cm/s 覆盖 203 的 authored 减速（§6.3.2 明令禁止），
	//     而且它 `Montage_Play` 的默认 stop-all 会顺手停掉本招自己的主蒙太奇。
	//
	// 中止滞空时**不需要**在这里回领：基类 `UMHGZInsectGlaiveAbility::EndAbility` 在
	// 「取消 + 未接地 + 没有别的已注册动作」时会重新领回这个许可。
	if (UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost())
	{
		Host->ReleaseAerialFallingState();
	}
	return true;
}

bool UMHGZKinsectPiercerAbility::StartAttackMontage(ACharacter& Character,
	UAnimMontage* Montage, const FName StartSection)
{
	USkeletalMeshComponent* Mesh = Character.GetMesh();
	UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (!AnimInstance || !Montage || !HasRequiredSections(Montage)
		|| StartSection != StartSectionName)
	{
		return false;
	}

	ActiveMainMontage = Montage;
	ActiveAttackMontage = Montage;
	MainMontageTask = UAbilityTask_MHGZPlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, TEXT("KinsectPiercerMainMontage"), Montage, 1.0f, StartSection,
		ResolveAttackMontageBlendInTime(GetWeaponActivationContext()), false);
	if (!MainMontageTask)
	{
		return false;
	}

	// 三条委托全绑本 GA 自己的处理函数。**不追加**基类默认终止绑定：
	// 201→203 的换段会同步停掉这条蒙太奇，基类那套会把它当成一次中断并结束整个 GA。
	MainMontageTask->OnCompleted.AddDynamic(this,
		&UMHGZKinsectPiercerAbility::HandleMainMontageCompleted);
	MainMontageTask->OnInterrupted.AddDynamic(this,
		&UMHGZKinsectPiercerAbility::HandleMainMontageInterrupted);
	MainMontageTask->OnCancelled.AddDynamic(this,
		&UMHGZKinsectPiercerAbility::HandleMainMontageInterrupted);
	MainMontageTask->ReportCompletionAtMontageEnd();
	MainMontageTask->ReadyForActivation();

	FAnimMontageInstance* MontageInstance = AnimInstance->GetActiveInstanceForMontage(Montage);
	if (!MontageInstance)
	{
		return false;
	}
	MainMontageInstanceID = MontageInstance->GetInstanceID();
	MontageInstance->bEnableAutoBlendOut = false;
	// 表现序列按现有 189/157 模式锁 root：位移由曲线唯一提供，不能再让根位移推一次胶囊。
	MontageInstance->PushDisableRootMotion();
	return RegisterMontageInstance(Mesh, MainMontageInstanceID);
}

void UMHGZKinsectPiercerAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	ResetActivationState();
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!IsActionActivationCommitted() || !IsActive())
	{
		return;
	}

	PiercerActionToken = GetActionToken();
	Phase = EKinsectPiercerPhase::Start200;
	BindLifecycleGuards();
	if (!IsActive())
	{
		return;
	}

	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	AMHGZCharacter* Character = Cast<AMHGZCharacter>(GetAvatarActorFromActorInfo());
	UCharacterMovementComponent* CMC = Character ? Character->GetCharacterMovement() : nullptr;
	URes_InsectGlaive* Resource = GetIGResourceComponent();
	if (!Host || !Character || !CMC || !Resource)
	{
		CancelPiercerAction(EWeaponActionEndReason::Interrupted,
			TEXT("The action requires a live character, CMC, Host and Insect Glaive resource."));
		return;
	}

	const FWeaponRuntimeToken RuntimeToken = PiercerActionToken.RuntimeToken;
	if (!ApplyLockedYaw())
	{
		CancelPiercerAction(EWeaponActionEndReason::Interrupted,
			TEXT("The frozen aim direction could not be locked as this action's yaw."));
		return;
	}

	// 空中攻击占用攻击与回避两种预算；确认成功后才取得，GA 结束不退款。
	// 本招自己的三次命中**不**重置预算、不增加舞踏，所以不能靠它再次开放 RTY。
	if (!ConsumeAerialAttackAndDodgeBudgets())
	{
		CancelPiercerAction(EWeaponActionEndReason::Interrupted,
			TEXT("Could not claim the Host-owned aerial attack and dodge budgets."));
		return;
	}

	if (UMHGZAbilitySystemComponent* ASC = Cast<UMHGZAbilitySystemComponent>(
		GetAbilitySystemComponentFromActorInfo()))
	{
		if (UGA_WeaponComboCoordinator* Coordinator = ASC->GetActiveComboCoordinator())
		{
			Coordinator->SupersedeOtherIGAerialActions(PiercerActionToken);
		}
	}
	if (!IsActive() || bEndingPiercer || !Host->IsTokenCurrent(RuntimeToken))
	{
		// 让位把本动作自己也收掉了（或运行期已失效）：`EndAbility` 正在收尾。
		return;
	}

	// Commit 时冻结倍率，整个 GA 的所有武器段共用；203 触地清层不影响本次快照。
	FrozenDanceDamageMultiplier = MHGZ::IGActionConfig::DanceDamageMultiplier(KinsectPiercerDanceDamageMultipliers, Resource->GetDanceStacks(), Resource->GetCombatConfig()->MaxDanceStacks);

	if (!StartPhaseMovement(EKinsectPiercerPhase::Start200))
	{
		CancelPiercerAction(EWeaponActionEndReason::Interrupted,
			TEXT("Could not start the recorded Start200 trajectory."));
		return;
	}
}

FWeaponMovementRequest UMHGZKinsectPiercerAbility::MakePhaseMovementRequest(
	const EKinsectPiercerPhase InPhase) const
{
	FWeaponMovementRequest Request;
	Request.OwnerAction = GetActionToken();
	Request.Mode = EWeaponMovementMode::RecordedLocalTrajectory;
	Request.DirectionSnapshot = LockedYawDirection;
	Request.RotationPolicy = EActionRotationPolicy::Locked;
	Request.CancelVelocityPolicy = EMovementCancelVelocityPolicy::PreserveVelocity;

	const URes_InsectGlaive* Resource = GetUsableResource();
	const UInsectGlaiveCombatConfig* Config = Resource ? Resource->GetCombatConfig() : nullptr;
	if (!Config)
	{
		return Request;
	}

	switch (InPhase)
	{
	case EKinsectPiercerPhase::Start200:
		Request.RecordedTrajectory = KinsectPiercerStartProfile;
		Request.TrajectoryGroundPolicy = ERecordedTrajectoryGroundPolicy::ProtectedFlight;
		Request.TrajectoryTailPolicy = ERecordedTrajectoryTailPolicy::HoldLastKey;
		break;

	case EKinsectPiercerPhase::Dive201:
		Request.RecordedTrajectory = KinsectPiercerDiveProfile;
		Request.TrajectoryGroundPolicy = ERecordedTrajectoryGroundPolicy::LandingAirborne;
		// 201 的覆盖是被录制时的触地截断的，不是动作长度：飞得更高就必须续行。
		Request.TrajectoryTailPolicy = ERecordedTrajectoryTailPolicy::IntegrateTerminalModel;
		break;

	default:
		break;
	}

	Request.Duration = Request.RecordedTrajectory.GetDuration();
	return Request;
}

bool UMHGZKinsectPiercerAbility::StartPhaseMovement(const EKinsectPiercerPhase InPhase)
{
	if (InPhase != EKinsectPiercerPhase::Start200 && InPhase != EKinsectPiercerPhase::Dive201)
	{
		return false;
	}
	const FWeaponMovementRequest Request = MakePhaseMovementRequest(InPhase);
	if (!Request.OwnerAction.IsValid() || !Request.HasValidRecordedTrajectoryParameters()
		|| Request.DirectionSnapshot.IsNearlyZero() || Request.Duration <= 0.0f)
	{
		return false;
	}

	const EKinsectPiercerPhase PreviousPhase = Phase;
	// 相位先落位再起任务：任务可能在同一调用栈里同步失败并回调，
	// 那时处理函数必须已经能看到正确的新相位。
	Phase = InPhase;
	MovementActionToken = GetActionToken();
	UAbilityTask_MHGZWeaponMovement* const StartedTask =
		UAbilityTask_MHGZWeaponMovement::StartWeaponMovement(this,
			TEXT("KinsectPiercerMovement"), Request);
	if (!StartedTask)
	{
		Phase = PreviousPhase;
		return false;
	}
	MovementTask = StartedTask;
	StartedTask->OnFinished.AddDynamic(this,
		&UMHGZKinsectPiercerAbility::HandleMovementFinished);
	StartedTask->ReadyForActivation();
	if (MovementTask != StartedTask || !StartedTask->DidStartMovement())
	{
		// 同步失败已经走到处理函数（它会把 MovementTask 清空并请求结束）。
		if (MovementTask)
		{
			MovementTask->EndTask();
			MovementTask = nullptr;
		}
		return false;
	}
	if (InPhase == EKinsectPiercerPhase::Start200)
	{
		UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
		if (!Host || !Host->SetAerialMovementSource(GetActionToken(),
			MHGZGameplayTags::IGKinsectPiercerSource()))
		{
			UE_LOG(LogMHGZ, Warning,
				TEXT("[KinsectPiercer] RuntimeHost could not publish the aerial source tag; aborting movement."));
			StartedTask->OnFinished.RemoveDynamic(this,
				&UMHGZKinsectPiercerAbility::HandleMovementFinished);
			if (MovementTask == StartedTask)
			{
				MovementTask = nullptr;
			}
			StartedTask->EndTask();
			Phase = PreviousPhase;
			return false;
		}
	}

	UE_LOG(LogMHGZ, Verbose,
		TEXT("[KinsectPiercer] phase=%d action=%u duration=%.4f direction=(%.3f, %.3f, %.3f)"),
		static_cast<int32>(InPhase), Request.OwnerAction.ActivationSequenceID,
		Request.Duration, Request.DirectionSnapshot.X, Request.DirectionSnapshot.Y,
		Request.DirectionSnapshot.Z);
	return true;
}

bool UMHGZKinsectPiercerAbility::AdvanceToDive()
{
	AMHGZCharacter* Character = Cast<AMHGZCharacter>(GetAvatarActorFromActorInfo());
	UAnimInstance* AnimInstance = Character && Character->GetMesh()
		? Character->GetMesh()->GetAnimInstance() : nullptr;
	if (!AnimInstance || !ActiveMainMontage
		|| !ActiveMainMontage->IsValidSectionName(DiveSectionName))
	{
		return false;
	}

	// 表现与位移在**同一个回调**里同步换段：中间没有物理帧，胶囊不会落回普通移动或重力。
	// 段内跳到 `Dive201` 不换实例、不换 ActionToken —— 命中额度与攻击 GUID 全程只有一份。
	AnimInstance->Montage_JumpToSection(DiveSectionName, ActiveMainMontage);
	if (!StartPhaseMovement(EKinsectPiercerPhase::Dive201))
	{
		return false;
	}
	StartDiveWatchdog();
	return true;
}

void UMHGZKinsectPiercerAbility::StartDiveWatchdog()
{
	const URes_InsectGlaive* Resource = GetUsableResource();
	const UInsectGlaiveCombatConfig* Config = Resource ? Resource->GetCombatConfig() : nullptr;
	UWorld* World = GetWorld();
	if (!Config || !World
		|| !FMath::IsFinite(KinsectPiercerDiveWatchdogSeconds)
		|| KinsectPiercerDiveWatchdogSeconds <= 0.0f)
	{
		return;
	}
	// 只处理「既没触地、也没结束」的异常会话。正常落地的终止条件是真实 ProcessLanded，
	// 这个计时器一秒都不会用上 —— 它不是 201→203 的出口。
	World->GetTimerManager().SetTimer(DiveWatchdogTimer, this,
		&UMHGZKinsectPiercerAbility::HandleDiveWatchdogExpired,
		KinsectPiercerDiveWatchdogSeconds, false);
}

void UMHGZKinsectPiercerAbility::ClearDiveWatchdog()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DiveWatchdogTimer);
	}
}

void UMHGZKinsectPiercerAbility::HandleDiveWatchdogExpired()
{
	if (!IsActive() || bEndingPiercer)
	{
		return;
	}
	CancelPiercerAction(EWeaponActionEndReason::Interrupted,
		TEXT("The 201 dive reached neither a confirmed landing nor a terminal phase before its watchdog expired."));
}

void UMHGZKinsectPiercerAbility::HandleMovementFinished(const FWeaponMovementResult& Result)
{
	const FWeaponActionToken CallbackToken = MovementActionToken;
	if (!IsCurrentAction(CallbackToken) || bEndingPiercer)
	{
		return;
	}

	MovementTask = nullptr;
	const EKinsectPiercerPhase FinishedPhase = Phase;
	switch (FinishedPhase)
	{
	case EKinsectPiercerPhase::Start200:
		if (Result.EndReason == EWeaponMovementEndReason::Completed)
		{
			if (!AdvanceToDive())
			{
				CancelPiercerAction(EWeaponActionEndReason::Interrupted,
					TEXT("The Start200 phase could not hand over to Dive201."));
			}
		}
		else
		{
			CancelPiercerAction(EWeaponActionEndReason::Interrupted,
				TEXT("The Start200 trajectory ended before its authored completion."));
		}
		break;

	case EKinsectPiercerPhase::Dive201:
		if (Result.EndReason == EWeaponMovementEndReason::Landed)
		{
			if (!BeginLandingSwitch(Result.BlockingHit))
			{
				CancelPiercerAction(EWeaponActionEndReason::Interrupted,
					TEXT("The confirmed landing could not switch the action to its Landing203 presentation."));
			}
		}
		else
		{
			// 201 永不按时长结束，所以这里只可能是取消、换武器、运行期失效或所有权被抢。
			CancelPiercerAction(EWeaponActionEndReason::Interrupted,
				TEXT("The Dive201 trajectory ended without a confirmed landing."));
		}
		break;

	default:
		break;
	}

	UE_LOG(LogMHGZ, Verbose,
		TEXT("[KinsectPiercer] movement end action=%u phase=%d reason=%d final=(%.1f, %.1f, %.1f)"),
		CallbackToken.ActivationSequenceID, static_cast<int32>(FinishedPhase),
		static_cast<int32>(Result.EndReason), Result.FinalVelocity.X,
		Result.FinalVelocity.Y, Result.FinalVelocity.Z);
}

void UMHGZKinsectPiercerAbility::HandleLanded(const FHitResult& Hit)
{
	// 生产链上 `AMHGZCharacter::Landed` 先广播 `LandedDelegate`（位移任务据此回报
	// `Landed`，走上面那条路完成了切换），之后才由 Coordinator 按 `AbilityOwned`
	// 把这一帧转给本 GA —— 转进来正是为了**不**让 Coordinator ResetCombo。
	//
	// 这里因此是第二入口而不是主入口：位移任务不在场时（例如已被取消）仍要保证
	// 「只开始一次 203」。锁存一立，重复入口全部落空。
	if (!IsCurrentAction(GetActionToken()) || bEndingPiercer)
	{
		return;
	}
	if (Phase != EKinsectPiercerPhase::Dive201 || bLandingLatched)
	{
		return;
	}
	if (!BeginLandingSwitch(Hit))
	{
		CancelPiercerAction(EWeaponActionEndReason::Interrupted,
			TEXT("The Coordinator-reported landing could not switch the action to Landing203."));
	}
}

void UMHGZKinsectPiercerAbility::FinalizeHostLanding()
{
	if (UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost())
	{
		// `ProcessLanded` 在 CMC 内部发出，MOVE_Flying 路径不保证 `ACharacter::Landed`
		// 一定先到，所以这里同步做资源清理：空中预算与舞踏来源不能活过触地。
		//
		// **不**调用 `PlayAerialLandingPresentation()`：那会起播系统落地蒙太奇并把水平
		// 速度重设成 337cm/s，与 203 自己的 Montage Root Motion 冲突。
		Host->HandleLanded();
	}
}

bool UMHGZKinsectPiercerAbility::AcquireLandingRootMotion()
{
	AMHGZCharacter* Character = Cast<AMHGZCharacter>(GetAvatarActorFromActorInfo());
	UCharacterMovementComponent* CMC = Character ? Character->GetCharacterMovement() : nullptr;
	UAnimInstance* Anim = Character && Character->GetMesh()
		? Character->GetMesh()->GetAnimInstance() : nullptr;
	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	if (bLandingRootMotionModeCaptured)
	{
		return Host && Host->IsMontageRootMotionOwnedBy(PiercerActionToken)
			&& LandingRootMotionAnimInstance.Get() == Anim;
	}
	if (!CMC || !CMC->IsMovingOnGround() || !Anim || !Host
		|| !IsCurrentAction(PiercerActionToken) || !Host->AcquireMontageRootMotion(PiercerActionToken))
	{
		return false;
	}
	// ABP uses FromEverything for locomotion. During this owner alone, extract
	// only the Montage so its blend-in cannot add MM locomotion root movement.
	LandingRootMotionAnimInstance = Anim;
	SavedLandingRootMotionMode = static_cast<uint8>(Anim->RootMotionMode.GetValue());
	bLandingRootMotionModeCaptured = true;
	Anim->SetRootMotionMode(ERootMotionMode::RootMotionFromMontagesOnly);
	CMC->StopMovementImmediately(); // drop the 201 tangent, not a timed input lock
	return true;
}

void UMHGZKinsectPiercerAbility::ReleaseLandingRootMotion()
{
	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	const bool bOwns = Host && Host->IsMontageRootMotionOwnedBy(PiercerActionToken);
	// The extraction-mode override is this ability's scoped state, not the
	// movement owner's state. A successor taking a movement lease must not make
	// us discard the only saved baseline. Restore only our temporary value;
	// an explicit change to a different mode by another system is left intact.
	if (bLandingRootMotionModeCaptured)
	{
		if (UAnimInstance* Anim = LandingRootMotionAnimInstance.Get())
		{
			if (Anim->RootMotionMode == ERootMotionMode::RootMotionFromMontagesOnly)
			{
				Anim->SetRootMotionMode(static_cast<ERootMotionMode::Type>(SavedLandingRootMotionMode));
			}
		}
	}
	if (bOwns)
	{
		// End only this owner's ground velocity; never erase a successor's motion.
		ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
		UCharacterMovementComponent* CMC = Character ? Character->GetCharacterMovement() : nullptr;
		if (CMC && CMC->IsMovingOnGround()
			&& GetActionEndReason() != EWeaponActionEndReason::Superseded)
		{
			CMC->StopMovementImmediately();
		}
		Host->ReleaseMontageRootMotion(PiercerActionToken);
	}
	LandingRootMotionAnimInstance.Reset();
	bLandingRootMotionModeCaptured = false;
}

bool UMHGZKinsectPiercerAbility::PlayLandingMontage()
{
	AMHGZCharacter* Character = Cast<AMHGZCharacter>(GetAvatarActorFromActorInfo());
	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (!AnimInstance || !HasRequiredLandingMontage())
	{
		return false;
	}

	const URes_InsectGlaive* Resource = GetUsableResource();
	const UInsectGlaiveCombatConfig* Config = Resource ? Resource->GetCombatConfig() : nullptr;
	const float BlendInTime = Config && FMath::IsFinite(KinsectPiercerLandingBlendInTime)
		? FMath::Max(0.0f, KinsectPiercerLandingBlendInTime) : 0.1f;

	LandingMontageTask = UAbilityTask_MHGZPlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, TEXT("KinsectPiercerLandingMontage"), LandingMontage, 1.0f, LandingSectionName,
		BlendInTime, false);
	if (!LandingMontageTask)
	{
		return false;
	}
	LandingMontageTask->OnCompleted.AddDynamic(this,
		&UMHGZKinsectPiercerAbility::HandleLandingMontageCompleted);
	LandingMontageTask->OnInterrupted.AddDynamic(this,
		&UMHGZKinsectPiercerAbility::HandleLandingMontageInterrupted);
	LandingMontageTask->OnCancelled.AddDynamic(this,
		&UMHGZKinsectPiercerAbility::HandleLandingMontageInterrupted);
	LandingMontageTask->ReportCompletionAtMontageEnd();
	TSet<int32> ExistingInstances;
	for (const FAnimMontageInstance* Instance : AnimInstance->MontageInstances)
	{
		if (Instance) { ExistingInstances.Add(Instance->GetInstanceID()); }
	}
	LandingMontageTask->ReadyForActivation();
	// Montage_Play broadcasts OnMontageStarted before the task returns or ASC
	// records the montage. A listener can cancel us there. Recover only our newly
	// created instance (including stopped/blending ones), never an old instance
	// or an instance registered to a reentrant successor.
	FAnimMontageInstance* MontageInstance = nullptr;
	for (FAnimMontageInstance* Instance : AnimInstance->MontageInstances)
	{
		if (!Instance || Instance->Montage != LandingMontage
			|| ExistingInstances.Contains(Instance->GetInstanceID())) { continue; }
		FWeaponActionToken RegisteredAction;
		UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
		if (Host && Host->ResolveMontage(Mesh, Instance->GetInstanceID(), RegisteredAction)
			&& RegisteredAction != PiercerActionToken) { continue; }
		MontageInstance = Instance;
		break;
	}
	if (!IsActive() || bEndingPiercer || !LandingMontageTask)
	{
		if (MontageInstance)
		{
			MontageInstance->PushDisableRootMotion();
			MontageInstance->Stop(FAlphaBlend(0.0f));
		}
		return false;
	}
	if (!MontageInstance)
	{
		return false;
	}
	LandingMontageInstanceID = MontageInstance->GetInstanceID();
	MontageInstance->bEnableAutoBlendOut = false;
	ActiveAttackMontage = LandingMontage;
	if (!RegisterMontageInstance(Mesh, LandingMontageInstanceID))
	{
		return false;
	}
	bLandingMontageActive = true;
	return true;
}

bool UMHGZKinsectPiercerAbility::BeginLandingSwitch(const FHitResult& Hit)
{
	if (Phase != EKinsectPiercerPhase::Dive201 || bLandingLatched || bSwitchingToLanding)
	{
		return false;
	}

	// 1) 切换事务与落地锁存**先立起来**：停止蒙太奇/结束任务都可能同步回调，
	//    那些回调不能再被当成「意外中断」。
	bSwitchingToLanding = true;
	bLandingLatched = true;

	// 2) 立即关闭本招的全部碰撞窗口并停掉 201 的看门狗。
	//    正常回调已归还曲线租约；HandleLanded 兜底仍可能持有任务。
	DisableCollision();
	ClearDiveWatchdog();
	if (MovementTask)
	{
		MovementTask->OnFinished.RemoveAll(this);
		MovementTask->EndTask();
		MovementTask = nullptr;
	}
	// ProcessLanded broadcasts ACharacter::Landed before post-landed physics,
	// and our recorded dive enters it from Flying. Use this confirmed contact
	// to enter Walking before acquiring ground Montage Root Motion, not a timer.
	AMHGZCharacter* LandingCharacter = Cast<AMHGZCharacter>(GetAvatarActorFromActorInfo());
	UCharacterMovementComponent* LandingCMC = LandingCharacter
		? LandingCharacter->GetCharacterMovement() : nullptr;
	if (!LandingCMC || !Hit.IsValidBlockingHit()
		|| !LandingCMC->IsValidLandingSpot(LandingCharacter->GetActorLocation(), Hit))
	{
		return false;
	}
	LandingCMC->SetMovementMode(MOVE_Walking);

	// 3) Host 的既有落地清理：释放空中预算、清 DanceSource/层数。
	FinalizeHostLanding();

	// 4) 先解绑旧任务的委托、注销旧实例，**再**停主 Montage。
	//    停止操作会同步回调，顺序反过来就会让旧中断先结束本 GA（§6.4）。
	if (MainMontageTask)
	{
		MainMontageTask->OnCompleted.RemoveAll(this);
		MainMontageTask->OnInterrupted.RemoveAll(this);
		MainMontageTask->OnCancelled.RemoveAll(this);
		MainMontageTask->EndTask();
		MainMontageTask = nullptr;
	}

	AMHGZCharacter* Character = Cast<AMHGZCharacter>(GetAvatarActorFromActorInfo());
	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (AnimInstance && ActiveMainMontage && MainMontageInstanceID != INDEX_NONE)
	{
		// 注销在前：旧实例混出期间的通知不允许再解析到本动作，更不允许回调新实例。
		if (UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost())
		{
			Host->UnregisterMontages(PiercerActionToken);
		}
		const FAnimMontageInstance* ActiveInstance =
			AnimInstance->GetActiveInstanceForMontage(ActiveMainMontage);
		if (ActiveInstance && ActiveInstance->GetInstanceID() == MainMontageInstanceID)
		{
			const URes_InsectGlaive* Resource = GetUsableResource();
			const UInsectGlaiveCombatConfig* Config = Resource
				? Resource->GetCombatConfig() : nullptr;
			const float BlendOutTime = Config
				&& FMath::IsFinite(KinsectPiercerLandingBlendInTime)
				? FMath::Max(0.0f, KinsectPiercerLandingBlendInTime) : 0.1f;
			AnimInstance->Montage_Stop(BlendOutTime, ActiveMainMontage);
		}
	}
	MainMontageInstanceID = INDEX_NONE;
	ActiveMainMontage = nullptr;

	// 5) 先取得地面根位移所有权，再播 `_Over`、注册新实例。任一步失败都按异常中断
	//    清理整个本招 —— 不允许留下「只有移动、没有合法表现」的 Action。
	Phase = EKinsectPiercerPhase::Landing203;
	if (!AcquireLandingRootMotion() || !PlayLandingMontage())
	{
		return false;
	}

	bSwitchingToLanding = false;
	UE_LOG(LogMHGZ, Display,
		TEXT("[KinsectPiercer] landed action=%u point=(%.1f, %.1f, %.1f) landing_instance=%d"),
		PiercerActionToken.ActivationSequenceID, Hit.ImpactPoint.X, Hit.ImpactPoint.Y,
		Hit.ImpactPoint.Z, LandingMontageInstanceID);
	return true;
}

void UMHGZKinsectPiercerAbility::HandleMainMontageCompleted()
{
	if (Phase != EKinsectPiercerPhase::Start200 && Phase != EKinsectPiercerPhase::Dive201) { return; }
	MainMontageTask = nullptr;
	// 主 Montage 的最后一个 Section 是**自循环**（`NextSectionName == DiveLoop201`），
	// 正常路径下这里永远不会被走到。走到了就说明自循环没生效 ——
	// M3 生成后必须回读 `CompositeSections[2].NextSectionName`，E1 要目视确认。
	//
	// 这里**不**结束 Action：下潜段仍由曲线租约与真实触地驱动。表现冻在终末姿势
	// 也比整个 GA 被收掉安全，而且这条警告会指出是生成环节的问题。
	UE_LOG(LogMHGZ, Warning,
		TEXT("[KinsectPiercer] primary montage reported completion at phase=%d: the DiveLoop201 section self-loop did not take effect. The flight continues under its curve lease."),
		static_cast<int32>(Phase));
}

void UMHGZKinsectPiercerAbility::HandleMainMontageInterrupted()
{
	if (Phase != EKinsectPiercerPhase::Start200 && Phase != EKinsectPiercerPhase::Dive201) { return; }
	MainMontageTask = nullptr;
	if (bEndingPiercer || bSwitchingToLanding)
	{
		// 201→203 的切换会主动停掉主 Montage 并先解绑本委托；那不是中断。
		return;
	}
	if (IsActive())
	{
		CancelPiercerAction(EWeaponActionEndReason::Interrupted,
			TEXT("The primary montage was interrupted before the landing switch."));
	}
}

void UMHGZKinsectPiercerAbility::HandleLandingMontageCompleted()
{
	if (Phase != EKinsectPiercerPhase::Landing203 || bEndingPiercer) { return; }
	LandingMontageTask = nullptr;
	bLandingMontageFinished = true;
	TryCompleteLandingPhase();
}

void UMHGZKinsectPiercerAbility::HandleLandingMontageInterrupted()
{
	if (HasPreparedDodgeSupersede())
	{
		// Montage_Play 尚未返回；旧 Action 必须保留到翻滚提交/回滚。
		// 也不能先清 LandingMontageTask，否则提交时无法解绑延后的回调。
		return;
	}
	LandingMontageTask = nullptr;
	if (bEndingPiercer || !bLandingMontageActive)
	{
		return;
	}
	if (IsActive())
	{
		CancelPiercerAction(EWeaponActionEndReason::Interrupted,
			TEXT("The Landing203 montage was interrupted before normal completion."));
	}
}

void UMHGZKinsectPiercerAbility::TryCompleteLandingPhase()
{
	if (Phase != EKinsectPiercerPhase::Landing203 || bEndingPiercer)
	{
		return;
	}
	// 203 的表现与运动同源，真实 Montage 末尾是唯一自然完成条件。
	if (!bLandingMontageFinished)
	{
		return;
	}
	CompletePiercerAction();
}

void UMHGZKinsectPiercerAbility::OnMontageCompleted()
{
	// 基类只会在它自己的 `StartAttackMontage` 里把这个虚函数接上；本招 override 了
	// 那一步并自建任务，所以这里永远不该被走到。留一个显式空实现，防止将来有人
	// 把基类的默认绑定接回来之后，它静默地结束本 GA。
}

void UMHGZKinsectPiercerAbility::CompletePiercerAction()
{
	if (!IsActive() || bEndingPiercer)
	{
		return;
	}
	bEndingPiercer = true;
	ClearDiveWatchdog();
	DisableCollision();
	// 地面自然结束：只交回普通移动，不起下落表现、不设 ForceIdle 死区（§6.2/§9.1）。
	RequestEndAction(EWeaponActionEndReason::Normal);
}

void UMHGZKinsectPiercerAbility::CancelPiercerAction(
	const EWeaponActionEndReason EndReason, const TCHAR* Diagnostic)
{
	if (!IsActive() || bEndingPiercer)
	{
		return;
	}
	UE_LOG(LogMHGZ, Warning,
		TEXT("[KinsectPiercer] action=%u phase=%d reason=%d: %s"),
		GetActionToken().ActivationSequenceID, static_cast<int32>(Phase),
		static_cast<int32>(EndReason), Diagnostic);
	bEndingPiercer = true;
	ClearDiveWatchdog();
	RequestEndAction(EndReason);
}

bool UMHGZKinsectPiercerAbility::IsCurrentAction(const FWeaponActionToken& ActionToken) const
{
	const UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	return !bEndingPiercer && IsActive() && IsActionActivationCommitted()
		&& PiercerActionToken.IsValid() && ActionToken == PiercerActionToken
		&& ActionToken == GetActionToken() && Host
		&& Host->IsTokenCurrent(ActionToken.RuntimeToken);
}

bool UMHGZKinsectPiercerAbility::HasDamagedTarget(const int32 SegmentIndex,
	const AActor* Target) const
{
	if (!Target || !DamagedTargetsPerSegment.IsValidIndex(SegmentIndex))
	{
		return false;
	}
	return DamagedTargetsPerSegment[SegmentIndex].Contains(TWeakObjectPtr<const AActor>(Target));
}

void UMHGZKinsectPiercerAbility::RecordDamagedTarget(const int32 SegmentIndex,
	const AActor* Target)
{
	if (!Target || !DamagedTargetsPerSegment.IsValidIndex(SegmentIndex))
	{
		return;
	}
	DamagedTargetsPerSegment[SegmentIndex].Add(TWeakObjectPtr<const AActor>(Target));
}

bool UMHGZKinsectPiercerAbility::CanApplyCollisionHit(const FHitResult& Hit,
	const int32 SegmentIndex) const
{
	// 攻击基类的命中状态按**碰撞窗口**保存，窗口关了再开就可能重新计数。
	// 本招的额度是**激活级**的：跨部位、重复通知、201 的动画循环和两条 Montage
	// 都不能让同一个目标在同一段里被结算第二次（§7）。
	return !HasDamagedTarget(SegmentIndex, Hit.GetActor());
}

void UMHGZKinsectPiercerAbility::HandleSuccessfulAttackDamage(const FHitResult& Hit,
	const int32 SegmentIndex)
{
	// 只在 GE **真的成功**时记录；失败不点亮额度，也不触发任何资源效果。
	RecordDamagedTarget(SegmentIndex, Hit.GetActor());
	LastSuccessfulConfigIndex = SegmentIndex;
}

void UMHGZKinsectPiercerAbility::GetRuntimeTelemetrySnapshot(
	FMHGZKinsectPiercerTelemetrySnapshot& OutSnapshot) const
{
	OutSnapshot = FMHGZKinsectPiercerTelemetrySnapshot();
	OutSnapshot.ActivationSequenceID = PiercerActionToken.ActivationSequenceID;
	OutSnapshot.Phase = static_cast<int32>(Phase);
	OutSnapshot.LockedYawDirection = LockedYawDirection;
	OutSnapshot.MainMontageInstanceID = MainMontageInstanceID;
	OutSnapshot.LandingMontageInstanceID = LandingMontageInstanceID;
	OutSnapshot.bLandingRootMotionModeCaptured = bLandingRootMotionModeCaptured;
	OutSnapshot.SegmentConfigCount = AttackSegments.Num();
	OutSnapshot.LastSuccessfulConfigIndex = LastSuccessfulConfigIndex;
	for (int32 SegmentIndex = 0;
		SegmentIndex < UE_ARRAY_COUNT(OutSnapshot.SuccessfulTargetCounts); ++SegmentIndex)
	{
		OutSnapshot.SuccessfulTargetCounts[SegmentIndex] = DamagedTargetsPerSegment.IsValidIndex(SegmentIndex)
			? DamagedTargetsPerSegment[SegmentIndex].Num() : 0;
	}

	if (!PiercerActionToken.IsValid())
	{
		return;
	}

	const UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	OutSnapshot.bAbilityActive = IsActive() && Host
		&& Host->IsTokenCurrent(PiercerActionToken.RuntimeToken);
	OutSnapshot.bActionMovementOwned = Host
		&& Host->IsActionMovementOwnedBy(PiercerActionToken);
	OutSnapshot.bLandingRootMotionOwned = Host
		&& Host->IsMontageRootMotionOwnedBy(PiercerActionToken);

	const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	OutSnapshot.bAerialAttackBudgetSpent = Host && Host->IsAerialAttackBudgetSpent();
	OutSnapshot.bKinsectSlashSourceTagPresent = ASC
		&& ASC->HasMatchingGameplayTag(KinsectSlashDanceSourceTag());
	if (const URes_InsectGlaive* Resource = GetUsableResource())
	{
		OutSnapshot.DanceSource = static_cast<int32>(Resource->GetDanceSource());
	}

	const AMHGZCharacter* Character = Cast<AMHGZCharacter>(GetAvatarActorFromActorInfo());
	const UCharacterMovementComponent* CharacterMovement = Character
		? Character->GetCharacterMovement() : nullptr;
	if (const UMHGZInstrumentedCharacterMovementComponent* InstrumentedCMC =
		Cast<UMHGZInstrumentedCharacterMovementComponent>(CharacterMovement))
	{
		FMHGZRecordedTrajectoryTelemetry MovementTelemetry;
		InstrumentedCMC->GetRecordedTrajectoryTelemetry(PiercerActionToken, MovementTelemetry);
		if (Phase == EKinsectPiercerPhase::Start200 || Phase == EKinsectPiercerPhase::Dive201)
		{
			OutSnapshot.MovementLease = MovementTelemetry.Lease;
			OutSnapshot.CurveElapsedSeconds = MovementTelemetry.ElapsedSeconds;
			OutSnapshot.CurveCoveredSeconds = MovementTelemetry.CoveredSeconds;
			OutSnapshot.bRecordedTrajectoryActive = MovementTelemetry.bActive;
		}
	}

	const UAnimInstance* AnimInstance = Character && Character->GetMesh()
		? Character->GetMesh()->GetAnimInstance() : nullptr;
	if (AnimInstance)
	{
		OutSnapshot.AnimRootMotionMode = static_cast<int32>(AnimInstance->RootMotionMode.GetValue());
		if (Phase == EKinsectPiercerPhase::Landing203 && LandingMontage)
		{
			OutSnapshot.LandingMontageTimeSeconds = AnimInstance->Montage_GetPosition(LandingMontage);
		}
	}
}

FGameplayEffectSpecHandle UMHGZKinsectPiercerAbility::MakeDamageSpec(
	const FHitResult& Hit, const int32 SegmentIndex)
{
	FGameplayEffectSpecHandle Spec = Super::MakeDamageSpec(Hit, SegmentIndex);
	if (!Spec.IsValid() || !AttackSegments.IsValidIndex(SegmentIndex)
		|| FrozenDanceDamageMultiplier <= 0.0f)
	{
		return Spec;
	}

	// 全部伤害段使用同一出招快照，包含触地后的203；不再按空中/地面相位分流。
	Spec.Data->SetSetByCallerMagnitude(
		FGameplayTag::RequestGameplayTag(TEXT("Damage.MotionValue")),
		AttackSegments[SegmentIndex].Damage.MotionValue.GetValueAtLevel(GetAbilityLevel())
			* FrozenDanceDamageMultiplier);
	return Spec;
}

void UMHGZKinsectPiercerAbility::BindLifecycleGuards()
{
	UMHGZAbilitySystemComponent* ASC = Cast<UMHGZAbilitySystemComponent>(
		GetAbilitySystemComponentFromActorInfo());
	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	if (!ASC || !Host)
	{
		return;
	}

	if (!RuntimeInvalidationDelegateHandle.IsValid())
	{
		RuntimeInvalidationDelegateHandle = Host->OnWeaponRuntimeInvalidated.AddUObject(
			this, &UMHGZKinsectPiercerAbility::HandleRuntimeInvalidated);
	}

	const FGameplayTag DisqualifyingTags[] = { MHGZGameplayTags::Dead(), MHGZGameplayTags::Hitstun(), MHGZGameplayTags::Knockdown() };
	for (const FGameplayTag& Tag : DisqualifyingTags)
	{
		if (!DisqualifyingStateTagDelegateHandles.Contains(Tag))
		{
			const FDelegateHandle Handle = ASC->RegisterGameplayTagEvent(
				Tag, EGameplayTagEventType::NewOrRemoved).AddUObject(
				this, &UMHGZKinsectPiercerAbility::HandleDisqualifyingStateTagChanged);
			DisqualifyingStateTagDelegateHandles.Add(Tag, Handle);
		}
	}

	if (!IsLiveContinuationStateValid())
	{
		CancelPiercerAction(EWeaponActionEndReason::Interrupted,
			TEXT("A disqualifying state was already present when the action began."));
	}
}

void UMHGZKinsectPiercerAbility::UnbindLifecycleGuards()
{
	MHGZAerialActionCleanup::UnbindRuntimeInvalidation(GetRuntimeHost(),
		RuntimeInvalidationDelegateHandle);
	MHGZAerialActionCleanup::UnbindStateTagDelegates(GetAbilitySystemComponentFromActorInfo(),
		DisqualifyingStateTagDelegateHandles);
}

bool UMHGZKinsectPiercerAbility::IsLiveContinuationStateValid() const
{
	const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	const UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	// 续行判据里**没有** Aerial.Falling：本招全程持有它，但取消/受击时不该靠它判断，
	// 而 203 已经在接地之后 —— 那时 Falling 本来就该被放掉。
	return ASC && Host && ASC->HasMatchingGameplayTag(MHGZGameplayTags::Unsheathed())
		&& !ASC->HasMatchingGameplayTag(MHGZGameplayTags::Dead())
		&& !ASC->HasMatchingGameplayTag(MHGZGameplayTags::Hitstun())
		&& !ASC->HasMatchingGameplayTag(MHGZGameplayTags::Knockdown());
}

void UMHGZKinsectPiercerAbility::HandleRuntimeInvalidated(
	const FWeaponRuntimeToken& InvalidatedToken)
{
	if (IsActive() && PiercerActionToken.IsValid()
		&& PiercerActionToken.RuntimeToken == InvalidatedToken)
	{
		CancelPiercerAction(EWeaponActionEndReason::Interrupted,
			TEXT("The owning weapon runtime token was invalidated."));
	}
}

void UMHGZKinsectPiercerAbility::HandleDisqualifyingStateTagChanged(
	const FGameplayTag Tag, const int32 NewCount)
{
	if (NewCount > 0 && IsActive()
		&& (Tag == MHGZGameplayTags::Dead() || Tag == MHGZGameplayTags::Hitstun() || Tag == MHGZGameplayTags::Knockdown()))
	{
		CancelPiercerAction(Tag == MHGZGameplayTags::Dead() ? EWeaponActionEndReason::Death
			: EWeaponActionEndReason::Interrupted,
			TEXT("A disqualifying combat-state tag was added during the action."));
	}
}

void UMHGZKinsectPiercerAbility::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const bool bReplicateEndAbility,
	const bool bWasCancelled)
{
	bEndingPiercer = true;
	DisableCollision();
	ClearDiveWatchdog();
	Phase = EKinsectPiercerPhase::Ended;
	UnbindLifecycleGuards();

	MHGZAerialActionCleanup::EndTaskAndReset(MovementTask);
	if (MainMontageTask)
	{
		MainMontageTask->OnCompleted.RemoveAll(this);
		MainMontageTask->OnInterrupted.RemoveAll(this);
		MainMontageTask->OnCancelled.RemoveAll(this);
		MainMontageTask->EndTask();
		MainMontageTask = nullptr;
	}
	if (LandingMontageTask)
	{
		LandingMontageTask->OnCompleted.RemoveAll(this);
		LandingMontageTask->OnInterrupted.RemoveAll(this);
		LandingMontageTask->OnCancelled.RemoveAll(this);
		LandingMontageTask->EndTask();
		LandingMontageTask = nullptr;
	}

	AMHGZCharacter* Character = Cast<AMHGZCharacter>(GetAvatarActorFromActorInfo());
	UAnimInstance* AnimInstance = Character && Character->GetMesh()
		? Character->GetMesh()->GetAnimInstance() : nullptr;
	if (AnimInstance)
	{
		// 只停本动作自己的实例：绝不动共享资产或别的动作正在用的实例。
		if (ActiveMainMontage && MainMontageInstanceID != INDEX_NONE)
		{
			FAnimMontageInstance* ActiveInstance =
				AnimInstance->GetMontageInstanceForID(MainMontageInstanceID);
			if (ActiveInstance && ActiveInstance->Montage == ActiveMainMontage)
			{
				ActiveInstance->Stop(FAlphaBlend(0.05f));
			}
		}
		if (LandingMontage && LandingMontageInstanceID != INDEX_NONE)
		{
			FAnimMontageInstance* ActiveInstance =
				AnimInstance->GetMontageInstanceForID(LandingMontageInstanceID);
			if (ActiveInstance && ActiveInstance->Montage == LandingMontage)
			{
				// Stop keeps the outgoing instance alive for its blend. Suppress its
				// remaining root before restoring locomotion extraction ownership.
				ActiveInstance->PushDisableRootMotion();
				ActiveInstance->Stop(FAlphaBlend(0.05f));
			}
		}
	}

	ReleaseLandingRootMotion();
	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	const FWeaponRuntimeToken RuntimeToken = PiercerActionToken.RuntimeToken;
	UCharacterMovementComponent* CMC = Character ? Character->GetCharacterMovement() : nullptr;
	const bool bNeedsMovementModeRestore = CMC && CMC->MovementMode == MOVE_Flying;
	ActiveMainMontage = nullptr;
	ActiveAttackMontage = nullptr;
	MainMontageInstanceID = INDEX_NONE;
	LandingMontageInstanceID = INDEX_NONE;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility,
		bWasCancelled);

	// 取消/受击/换武器可能把胶囊留在 MOVE_Flying（曲线模式的保护飞行段没有重力）。
	// 交回普通物理前先问地面，不拿模式当代理指标。被后继动作接管时不插手 ——
	// 那时新动作自己的物理才是权威（§9.1）。
	if (bNeedsMovementModeRestore && Host && Host->IsTokenCurrent(RuntimeToken)
		&& !Host->IsGrounded() && !Host->HasRegisteredAction() && CMC
		&& CMC->MovementMode == MOVE_Flying)
	{
		MHGZAerialActionCleanup::RestoreMovementModeFromFloor(*Character, *CMC);
	}
}

void UMHGZKinsectPiercerAbility::ResetActivationState()
{
	ClearDiveWatchdog();
	Phase = EKinsectPiercerPhase::Inactive;
	PiercerActionToken = FWeaponActionToken();
	MovementActionToken = FWeaponActionToken();
	MainMontageTask = nullptr;
	LandingMontageTask = nullptr;
	MovementTask = nullptr;
	ActiveMainMontage = nullptr;
	MainMontageInstanceID = INDEX_NONE;
	LandingMontageInstanceID = INDEX_NONE;
	LockedYawDirection = FVector::ForwardVector;
	LastSuccessfulConfigIndex = INDEX_NONE;
	DamagedTargetsPerSegment.Reset();
	DamagedTargetsPerSegment.SetNum(3);
	FrozenDanceDamageMultiplier = 1.0f;
	bLandingLatched = false;
	bSwitchingToLanding = false;
	bLandingMontageActive = false;
	bLandingMontageFinished = false;
	LandingRootMotionAnimInstance.Reset();
	bLandingRootMotionModeCaptured = false;
	bEndingPiercer = false;
}

#if WITH_EDITOR
EDataValidationResult UMHGZKinsectPiercerAbility::IsDataValid(
	FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (ValidateOwnedActionConfig(Context) == EDataValidationResult::Invalid) return EDataValidationResult::Invalid;
	bool bInvalid = false;
	auto AddError = [&Context, &bInvalid](const FString& Message)
	{
		Context.AddError(FText::FromString(Message));
		bInvalid = true;
	};

	if (!AttackMontage)
	{
		AddError(TEXT("Kinsect Piercer requires the resident AM_IG_QiangHuaCaoChongChuanCi main montage; there is no runtime fallback that builds one from sequences."));
	}
	else if (!HasRequiredSections(AttackMontage))
	{
		AddError(TEXT("AttackMontage must contain the Start200, Dive201 and DiveLoop201 sections, with DiveLoop201 self-looped by the generator."));
	}
	if (!LandingMontage)
	{
		AddError(TEXT("Kinsect Piercer requires the resident AM_IG_QiangHuaCaoChongChuanCi_Over landing montage."));
	}
	else if (!HasRequiredLandingMontage())
	{
		AddError(TEXT("LandingMontage must contain exactly one Landing203 section, no self-loop, and enabled animation Root Motion."));
	}
	if (!InputTag.MatchesTagExact(FGameplayTag::RequestGameplayTag(TEXT("Input.Weapon.RTY"))))
	{
		AddError(TEXT("Kinsect Piercer's InputTag must be Input.Weapon.RTY."));
	}

	if (AttackSegments.Num() != 3)
	{
		AddError(TEXT("Kinsect Piercer requires exactly three AttackSegments: three independent weapon contacts with no hit-required cancellation."));
	}
	else
	{
		for (int32 SegmentIndex = 0; SegmentIndex < 3; ++SegmentIndex)
		{
			const FAttackSegmentConfig& Segment = AttackSegments[SegmentIndex];
			if (!Segment.Damage.DamageEffectClass
				|| Segment.Damage.MotionValue.GetValueAtLevel(1.0f) <= 0.0f
				|| Segment.Damage.bRequiresHitToContinue
				|| Segment.MultiHitCount != 1
				|| Segment.MultiHitPolicy != EAttackMultiHitPolicy::ContactOnly)
			{
				AddError(FString::Printf(TEXT("AttackSegments[%d] requires a damage GE, a positive MotionValue, no hit-required cancellation, and exactly one ContactOnly hit."), SegmentIndex));
			}
			if (Segment.Collision.TraceRegions.IsEmpty())
			{
				AddError(FString::Printf(TEXT("AttackSegments[%d] requires TraceRegions for its AttackCollision window."), SegmentIndex));
			}
		}
	}

	return bInvalid ? EDataValidationResult::Invalid
		: (Result == EDataValidationResult::NotValidated
			? EDataValidationResult::Valid : Result);
}
#endif

#if WITH_EDITOR
#undef LOCTEXT_NAMESPACE
#define LOCTEXT_NAMESPACE "MHGZKinsectPiercerConfig"
EDataValidationResult UMHGZKinsectPiercerAbility::ValidateOwnedActionConfig(FDataValidationContext& Context) const
{
	const EDataValidationResult Result = EDataValidationResult::Valid;
	bool bInvalid = false;
	auto AddError = [&Context, &bInvalid](const FText& Error)
	{
		Context.AddError(Error);
		bInvalid = true;
	};

	auto CheckPositive = [&AddError](const TCHAR* FieldName, float Value)
	{
		if (!FMath::IsFinite(Value) || Value <= 0.f)
		{
			AddError(FText::Format(
				LOCTEXT("NotPositive", "{0} must be > 0 (current {1})."),
				FText::FromString(FieldName), FText::AsNumber(Value)));
		}
	};

	auto CheckNonNegative = [&AddError](const TCHAR* FieldName, float Value)
	{
		if (!FMath::IsFinite(Value) || Value < 0.f)
		{
			AddError(FText::Format(
				LOCTEXT("Negative", "{0} must be >= 0 (current {1})."),
				FText::FromString(FieldName), FText::AsNumber(Value)));
		}
	};

	CheckPositive(TEXT("KinsectPiercerDiveWatchdogSeconds"), KinsectPiercerDiveWatchdogSeconds);
	CheckNonNegative(TEXT("KinsectPiercerLandingBlendInTime"), KinsectPiercerLandingBlendInTime);
	auto CheckTrajectoryProfile = [&AddError](const TCHAR* FieldName,
		const FRecordedLocalTrajectoryProfile& Profile)
	{
		if (!Profile.Curve)
		{
			return;
		}
		if (!Profile.IsValid())
		{
			AddError(FText::Format(
				LOCTEXT("InvalidPiercerTrajectory",
					"{0} is assigned but not self-consistent: it needs strictly increasing finite keys starting at the origin, a positive CoveredSeconds no later than its last key, and finite terminal velocity/acceleration."),
				FText::FromString(FieldName)));
		}
	};
	CheckTrajectoryProfile(TEXT("KinsectPiercerStartProfile"), KinsectPiercerStartProfile);
	CheckTrajectoryProfile(TEXT("KinsectPiercerDiveProfile"), KinsectPiercerDiveProfile);
	CheckTrajectoryProfile(TEXT("KinsectPiercerLandingProfile"), KinsectPiercerLandingProfile);

    if (KinsectPiercerDanceDamageMultipliers.IsEmpty() || !FMath::IsNearlyEqual(KinsectPiercerDanceDamageMultipliers[0], 1.0f)) AddError(FText::FromString(TEXT("舞踏倍率第0层必须为1。")));
    for (float Value : KinsectPiercerDanceDamageMultipliers) if (!FMath::IsFinite(Value) || Value < 1.0f) AddError(FText::FromString(TEXT("舞踏倍率必须有限且不小于1。")));

	return bInvalid ? EDataValidationResult::Invalid : (Result == EDataValidationResult::NotValidated ? EDataValidationResult::Valid : Result);
}
#undef LOCTEXT_NAMESPACE
#endif
