// Copyright MHGZ Project. All Rights Reserved.

#include "ActionSystem/MHGZAirDodgeAbility.h"

#include "ActionSystem/AbilityTask_MHGZPlayMontageAndWait.h"
#include "ActionSystem/AbilityTask_MHGZWeaponMovement.h"
#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "ActionSystem/MHGZComboCoordinatorAbility.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InsectGlaive/InsectGlaiveCombatConfig.h"
#include "Movement/MHGZInstrumentedCharacterMovementComponent.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"
#include "MHGZ.h"

namespace
{
void RecordAirDodgeMovementPhase(ACharacter* Character, const FName Phase)
{
	UMHGZInstrumentedCharacterMovementComponent* InstrumentedMovement = Character
		? Cast<UMHGZInstrumentedCharacterMovementComponent>(Character->GetCharacterMovement())
		: nullptr;
	if (InstrumentedMovement)
	{
		InstrumentedMovement->RecordAerialMovementEvent(Phase);
	}
}

const FGameplayTag& AerialTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Aerial"));
	return Tag;
}

const FGameplayTag& GroundedTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Grounded"));
	return Tag;
}

const FGameplayTag& AirDodgeFallingTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Aerial.Falling.IG_AirDodge"));
	return Tag;
}

const FGameplayTag& DeadTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Dead"));
	return Tag;
}

const FGameplayTag& HitstunTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Hitstun"));
	return Tag;
}

const FGameplayTag& KnockdownTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Knockdown"));
	return Tag;
}
}

UMHGZAirDodgeAbility::UMHGZAirDodgeAbility()
{
	InputTag = FGameplayTag::RequestGameplayTag(TEXT("Input.AirDodge"));

	// 八向：瞬转可以到 180°，所以不给基类的 ApplyDirectionCorrection 留限幅。
	// 撑杆跳取 0 是因为它方向锁死；本招相反 —— 转向就是它的语义（真值：段首转向
	// 中位 82.3°、最大 178.7°，段内则恒为 0）。
	MaxCorrectionAngle = 180.0f;

	// 「本次滞空已用」的预算。acquire 在 ActivateAbility，释放只在落地（Host 的 Pose 槽，
	// HandleLanded 早就在释放它）—— 中途取消进别的空中招式**不退款**。
	ActivationBlockedTags.AddTag(
		FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aerial.CantDodge")));

	// 最早可操作帧 = 空中动作的 lead（起播→IG_AerialHandoff）里「空中可操作」缺席。
	// 正向查在场（引擎 DoesAbilitySatisfyTagRequirements 原生拦）：该 tag 领于
	// handoff/离地无 Action/中止滞空，放于新动作起播（DetectAerialHandoffNotify）。
	// 未来空中攻击行同款规矩走 DA_IG_Combo 行的 RequiredTags。
	ActivationRequiredTags.AddTag(
		FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aerial.Falling")));

	// 真值源没有耐力数据，刻意不抄地面翻滚那 25 点：抄了就等于凭空发明一个消耗。
}

FWeaponMovementRequest UMHGZAirDodgeAbility::BuildAirDodgeRequest(
	const FWeaponActionToken& OwnerAction,
	const FVector& LaunchDirection,
	const FVector& FallbackFacing,
	const UInsectGlaiveCombatConfig& Config)
{
	FVector Planar(LaunchDirection.X, LaunchDirection.Y, 0.0f);
	if (Planar.IsNearlyZero())
	{
		// 摇杆回中：真值里有 15 段起始转向 ≈0°，与「摇杆本就朝原方向」或「回中」都相容，
		// 取朝向是最保守的一条。
		Planar = FVector(FallbackFacing.X, FallbackFacing.Y, 0.0f);
	}
	Planar = Planar.GetSafeNormal();

	FWeaponMovementRequest Request;
	Request.OwnerAction = OwnerAction;
	Request.Mode = EWeaponMovementMode::BallisticVault;
	Request.BallisticMode = EBallisticParameterMode::ExplicitLaunchVelocity;
	// XOR 契约（HasValidBallisticParameters）：两组参数必须且只能启用一组，
	// 显式初速这一组要求 ApexHeight / Duration 都是 0。
	Request.ApexHeight = 0.0f;
	Request.Duration = 0.0f;
	// 水平分量同时是**方向来源**：ApplyBallisticVaultSource 用 LaunchVelocity.XY
	// 归一化覆盖 DirectionSnapshot，所以「方向 = 摇杆角」与「完全不继承进入速度」
	// 都是这条路由免费给的（真值：`147` 自己散在 3.1~12.1 m/s 而 `137` 恒 ~13）。
	Request.LaunchVelocity = Planar * Config.AirDodgeHorizontalSpeed
		+ FVector(0.0f, 0.0f, Config.AirDodgeVerticalSpeed);
	Request.DirectionSnapshot = Planar;
	// 显式初速模式下 MaxDistance 会被 |vxy|·ResolvedDuration 覆盖（任务 :402），
	// 这里写实测总长只为可读性 —— 它不参与求解。
	Request.MaxDistance = 1065.0f;
	// 瞬转已经在激活时做过（基类的 ApplyDirectionCorrection），整段不再转向 —— 与实测
	// 「段内 213/213 段朝向变化恰好 0.0°」一致。
	Request.RotationPolicy = EActionRotationPolicy::Locked;
	Request.CancelVelocityPolicy = EMovementCancelVelocityPolicy::PreserveVelocity;
	return Request;
}

bool UMHGZAirDodgeAbility::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
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
	if (!ASC)
	{
		return false;
	}

	// ⚠⚠ 这里**只能读 ASC 上的活 tag，绝不能读 `GetWeaponActivationContext().Input`。**
	//
	// `ActivationContext` 是在 `ActivateAbility` 里才从 `PendingActivationContexts` 消费进来的
	// （`MHGZGameplayAbility.cpp:187`），而本函数比它**早一步**被 GAS 调用 ⇒ 此刻读到的永远是
	// 默认构造的空上下文，`ContextTags` 为空 ⇒ 每个判据都恒为 false ⇒ **静默拒绝一切**，
	// 连一行日志都不打（2026-09-22 PIE 的「怎么都按不出来」就是这个）。
	// 快照相关的判据一律放 `ValidateActionDependencies`（它在 `ActivateAbility` 内部，
	// 上下文已就位）—— 地面闪避 `UMHGZDodgeAbility` 也是这个分工。
	//
	// 「只要求在空中」那条判据（以及**刻意不要求** `Combat.State.Aerial.Actionable` 的理由）
	// 见下面的 `ValidateActionDependencies`。
	//
	// 最早可操作帧的锁由引擎原生拦：`ActivationRequiredTags` 里的
	// `Combat.State.Aerial.Falling`（空中可操作）—— 空中动作的 lead 里它被
	// DetectAerialHandoffNotify 放掉，handoff / 中止滞空 / 离地再领回。
	// **没有预输入**（2026-09-22 取消）：锁定期的按下直接作废，到可操作帧再按才出。
	return !ASC->HasMatchingGameplayTag(DeadTag())
		&& !ASC->HasMatchingGameplayTag(HitstunTag())
		&& !ASC->HasMatchingGameplayTag(KnockdownTag());
}

bool UMHGZAirDodgeAbility::ValidateActionDependencies() const
{
	// 本函数在 `ActivateAbility` 内部（Resource Reserve / Commit 之前、零副作用）被调用，
	// 此时 `GetWeaponActivationContext()` 才真正持有本次输入快照。
	// 逐项体检再走 Super —— 四合一拒绝说不出是哪一项（PIE 第 3 轮靠它猜过一次）。
	{
		const ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
		const USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
		if (!Character || !Mesh || !Mesh->GetAnimInstance() || !AttackMontage || !GetRuntimeHost())
		{
			UE_LOG(LogMHGZ, Warning,
				TEXT("[AirDodge] 拒绝：角色=%d 网格=%d 网格资产=%s AnimInstance=%d 蒙太奇=%s Host=%d"),
				Character ? 1 : 0, Mesh ? 1 : 0,
				Mesh ? *GetNameSafe(Mesh->GetSkeletalMeshAsset()) : TEXT("<null>"),
				Mesh && Mesh->GetAnimInstance() ? 1 : 0,
				*GetNameSafe(AttackMontage), GetRuntimeHost() ? 1 : 0);
			return false;
		}
	}
	if (!Super::ValidateActionDependencies())
	{
		UE_LOG(LogMHGZ, Warning, TEXT("[AirDodge] 拒绝：入口 section 不成立。"));
		return false;
	}

	// 只要求「在空中」。
	//
	// **刻意不要求 `Combat.State.Aerial.Actionable`**：真值里 `137` 的前驱有弧段（117 次）、
	// 舞踏（53 次）与 `143 起跳下坠`（25 次）—— 最后这一类是在**系统托管下落**期间按下的，
	// 而那个 tag 在下落态已经被上一个动作释放掉了。要求它会把这一类整体挡掉。
	const FWeaponInputSnapshot& Input = GetWeaponActivationContext().Input;
	if (Input.ContextTags.HasTagExact(GroundedTag())
		|| !Input.ContextTags.HasTagExact(AerialTag()))
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[AirDodge] 拒绝：上下文不是空中态（Grounded=%d Aerial=%d）。"),
			Input.ContextTags.HasTagExact(GroundedTag()) ? 1 : 0,
			Input.ContextTags.HasTagExact(AerialTag()) ? 1 : 0);
		return false;
	}

	return true;
}

bool UMHGZAirDodgeAbility::PrepareAttackMontage()
{
	ActiveAttackMontage = AttackMontage;
	// 点通知决定最早可操作帧（0.650 s）。没挂不是错误但会告警 ——
	// 静默无释放点比缺释放点更难查（与撑杆跳同一条教训）。
	DetectAerialHandoffNotify(AttackMontage);
	if (AttackMontage && !MontageHasAerialHandoffNotify(AttackMontage))
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[AirDodge] %s 上没有 UAnimNotify_IG_AerialHandoff —— 不会有最早可操作帧。"
				 "重跑 MHGZAerialHandoffSetup。"),
			*GetNameSafe(AttackMontage));
	}
	return AttackMontage != nullptr;
}

bool UMHGZAirDodgeAbility::StartAttackMontage(ACharacter& Character, UAnimMontage* Montage,
	FName StartSection)
{
	UAnimInstance* AnimInstance = Character.GetMesh()
		? Character.GetMesh()->GetAnimInstance() : nullptr;
	if (!AnimInstance || !Montage)
	{
		return false;
	}

	// 与基类同构（含逐转移的混入时长），只把三条委托绑到本 GA 的处理函数上。
	//
	// 收尾点仍是这条蒙太奇的长度（1.18333 s），但**触发方式与收尾方式照抄撑杆跳弧段**：
	// 关掉 `bEnableAutoBlendOut` 让它在自身长度处**保持终末姿势、不自动淡出终止**，
	// 由任务在位置到达长度时如实上报完成（`ReportCompletionAtMontageEnd`）。
	//
	// 保持默认 true 会坏在收尾那一帧：引擎在自身长度处先自动淡出、再把实例终止，
	// `OnCompleted` 到得比 `EndAbility` 早 —— 等 `EndAbility` 想 `Montage_Stop(0.05)` 时
	// 实例已经没了，于是那一帧**没有任何蒙太奇**：姿势掉回底层图（MM）再从半权重混入
	// 下落蒙太奇。录制实测 `MeshBoundsZ − LocationZ`：−17.4 → −30.4 → −33.5 → 回弹/过冲
	// （稳定值 −17.5），肉眼就是一下 ~15 cm 的下陷。
	//
	// 撑杆跳没有这一帧：弧段保持终末姿势，`EndAbility` 的 `Montage_Stop(0.05)` 与下坠蒙太奇
	// 起播在**同一帧**，权重 1.00→0.50 与 0.00→0.50 真交叉（录制 20260923-194246 第 2883 帧，
	// 姿势偏移全程 ±1 cm）。见 docs/using/空中下落实现缺口.md §6.5。
	const float BlendInTime = ResolveAttackMontageBlendInTime(GetWeaponActivationContext());
	AirDodgeVisualTask = UAbilityTask_MHGZPlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, FName(TEXT("AirDodgeVisual")), Montage, 1.0f, StartSection, BlendInTime);
	if (!AirDodgeVisualTask)
	{
		return false;
	}
	AirDodgeVisualTask->OnCompleted.AddDynamic(this,
		&UMHGZAirDodgeAbility::HandleAirDodgeVisualCompleted);
	AirDodgeVisualTask->OnInterrupted.AddDynamic(this,
		&UMHGZAirDodgeAbility::HandleAirDodgeVisualInterrupted);
	AirDodgeVisualTask->OnCancelled.AddDynamic(this,
		&UMHGZAirDodgeAbility::HandleAirDodgeVisualInterrupted);
	// 关掉自动淡出后，引擎不会在自身长度处终止实例，`OnCompleted` 也不会自己来 ——
	// 由任务在「位置到达长度」时报完成（判据见 HasMontageReachedEnd）。
	AirDodgeVisualTask->ReportCompletionAtMontageEnd();
	AirDodgeVisualTask->ReadyForActivation();

	FAnimMontageInstance* MontageInstance = AnimInstance->GetActiveInstanceForMontage(Montage);
	if (!MontageInstance
		|| !RegisterMontageInstance(Character.GetMesh(), MontageInstance->GetInstanceID()))
	{
		return false;
	}
	// 与撑杆跳 `PlayVaultMontage` 同一行、同一个理由（那里的注释：终末姿势要一直保持到
	// `EndAbility` 起下一个表现）。**不要改成 true，也不要跟地面翻滚对齐** ——
	// `UMHGZDodgeAbility` 把那一位设成 true 有它自己的理由（它没有「保持终末姿势等交棒」
	// 这一步，必须在末段自己结束并释放动作锁）。空中这一条正好相反：它的收尾是一次交棒。
	MontageInstance->bEnableAutoBlendOut = false;
	return true;
}

void UMHGZAirDodgeAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	bAirDodgeVisualFinished = false;
	bAirDodgeMovementFinished = false;
	bBeginFreeFallAfterEnd = false;
	bPlayLandedPresentation = false;
	bIsEndingAirDodge = false;
	AirDodgeMovementTask = nullptr;
	AirDodgeVisualTask = nullptr;

	// 基类的激活流程负责 ActionToken / RegisterAction / Commit，并在播放蒙太奇之前做一次
	// 「冻结输入 Yaw 瞬转」（ApplyDirectionCorrection，本 GA 的 MaxCorrectionAngle = 180）。
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!IsActionActivationCommitted() || !IsActive())
	{
		return;
	}

	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	if (!Host)
	{
		RequestEndAction(EWeaponActionEndReason::Interrupted);
		return;
	}

	// 【R3】预算激活即消费 —— commit（Super 内）已过，此刻起任何失败路径都不退款。
	// 语义：「本次滞空按过了」；CantDodge 仍只在落地释放。放在位移启动**之前**，
	// 位移失败不再留免费重按（PIE 第 2 轮 burst 自打断的根因）。
	Host->MarkAerialDodgeUsed();

	// 【R2】让位：其它活跃 IG 动作以 Superseded 结束（舞踏的视觉任务无中断回调，
	// 抢蒙太奇槽杀不死它，必须显式收）。必须在重力 profile **之前** —— 被让位者的
	// EndAbility 有自己的 CMC 收尾（撑杆跳 RestoreBackVaultInitialFlight →
	// SetDefaultMovementMode），先收它、再写重力、再建源，顺序才干净。
	SupersedeOtherLiveIGActions();

	// ⚠⚠ 顺序是正确性的一部分：**先施加下落重力 profile，再建弹道源。**
	//
	// 源的天际线与时长都由 `CMC->GetGravityZ()` 反算（AbilityTask_MHGZWeaponMovement.cpp:393），
	// 而 GravityScale 只在 Host 的 ApplyAerialFallingPhysics 里被写成 2.4246。撑杆跳弧段
	// 中途（Actionable 之后）也能触发本招，那一刻它还没走到自己的 BeginAerialFalling
	// ⇒ 不先设重力，反算会按 1.0g 得出 **2.4 倍**长的弧与 2.4 倍高的顶点，而且完全不报错。
	if (!Host->ApplyAerialFallingProfile(false))
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[AirDodge] 下落重力 profile 未生效（ResolveAerialFallingPhysics 拒绝或 CMC 缺失）"
				 "—— 拒绝激活，不能静默按 1.0g 起源。"));
		RequestEndAction(EWeaponActionEndReason::Interrupted);
		return;
	}

	if (!StartAirDodgeMovement())
	{
		RequestEndAction(EWeaponActionEndReason::Interrupted);
	}
}

bool UMHGZAirDodgeAbility::StartAirDodgeMovement()
{
	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	const UInsectGlaiveCombatConfig* CombatConfig = Host
		? Cast<UInsectGlaiveCombatConfig>(Host->GetCurrentContext().CombatConfig) : nullptr;
	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!Host || !CombatConfig || !Character)
	{
		// 这三者任一为空都会让本招静默无位移 —— 名字写进日志，省掉下一次 PIE 的瞎猜。
		UE_LOG(LogMHGZ, Warning,
			TEXT("[AirDodge] 拒绝：Host=%d 战斗配置=%s 角色=%d。"),
			Host ? 1 : 0, *GetNameSafe(CombatConfig), Character ? 1 : 0);
		return false;
	}

	// 方向取**冻结的输入快照**里的世界系摇杆方向 —— 激活之后不再读摇杆。
	// 瞬转已经把角色朝向也对到它了，所以这里的回退值只是「摇杆回中」那一种情形。
	const FWeaponInputSnapshot& Input = GetWeaponActivationContext().Input;
	const FWeaponMovementRequest Request = BuildAirDodgeRequest(GetActionToken(),
		Input.WorldDirection, Character->GetActorForwardVector(), *CombatConfig);
	if (!Request.HasValidBallisticParameters())
	{
		UE_LOG(LogMHGZ, Warning, TEXT("[AirDodge] 弹道参数不合法（显式初速组）。"));
		return false;
	}

	UAbilityTask_MHGZWeaponMovement* Task =
		UAbilityTask_MHGZWeaponMovement::StartWeaponMovement(this, TEXT("AirDodgePath"), Request);
	if (!Task)
	{
		return false;
	}
	AirDodgeMovementTask = Task;
	Task->OnFinished.AddDynamic(this, &UMHGZAirDodgeAbility::HandleAirDodgeMovementFinished);
	Task->ReadyForActivation();
	if (!Task->DidStartMovement())
	{
		// AcquireActionMovement 互斥拒绝 / 源创建失败在任务侧是**静默**的（零日志）——
		// 这里补上唯一线索，省掉下一次「按出招没位移」的录像考古（PIE 第 2 轮的教训）。
		UE_LOG(LogMHGZ, Warning,
			TEXT("[AirDodge] 位移未启动（AcquireActionMovement 互斥或源创建失败）：")
			TEXT("ActionMovementOwned=%d MontageRootMotionOwned=%d"),
			Host->IsActionMovementOwned() ? 1 : 0,
			Host->IsMontageRootMotionOwned() ? 1 : 0);
		AirDodgeMovementTask = nullptr;
		return false;
	}

	// 把反算用的重力与它推出的弧参数打出来 —— 「重力定序错了」在这一行立刻可读，
	// 否则它表现为一条安静地长 2.4 倍的弧。
	if (const UCharacterMovementComponent* CMC = Character->GetCharacterMovement())
	{
		const float GravityZ = FMath::Abs(CMC->GetGravityZ());
		const float Upward = Request.LaunchVelocity.Z;
		UE_LOG(LogMHGZ, Log,
			TEXT("[AirDodge] launch=%s gravityZ=%.1f duration=%.4f height=%.2f distance=%.2f"),
			*Request.LaunchVelocity.ToCompactString(), GravityZ,
			GravityZ > KINDA_SMALL_NUMBER ? (2.0f * Upward) / GravityZ : 0.0f,
			GravityZ > KINDA_SMALL_NUMBER ? (Upward * Upward) / (2.0f * GravityZ) : 0.0f,
			Request.LaunchVelocity.Size2D()
				* (GravityZ > KINDA_SMALL_NUMBER ? (2.0f * Upward) / GravityZ : 0.0f));
	}
	return true;
}

void UMHGZAirDodgeAbility::HandleAirDodgeMovementFinished(
	const FWeaponMovementResult& MovementResult)
{
	RecordAirDodgeMovementPhase(Cast<ACharacter>(GetAvatarActorFromActorInfo()),
		TEXT("AirDodge.MovementFinished.Pre"));
	AirDodgeMovementTask = nullptr;
	if (!IsActive() || bIsEndingAirDodge)
	{
		return;
	}

	// 只吃结束原因 —— 见 ResolveVaultExit 的注释：`Landed` 意味着胶囊真的够到地面。
	const EVaultExit Exit = ResolveVaultExit(MovementResult.EndReason);
	if (Exit == EVaultExit::None)
	{
		RequestEndAction(EWeaponActionEndReason::Interrupted);
		return;
	}

	bAirDodgeMovementFinished = true;
	bBeginFreeFallAfterEnd = Exit == EVaultExit::FreeFall;
	bPlayLandedPresentation = Exit == EVaultExit::LandedPresentation;

	// 【R4】源以 FreeFall 结束后，位移任务已在 FinishMovement→OnDestroy 解绑
	// LandedDelegate —— 蒙太奇尾段那 0.188 s 里触地就没人认领了（录 id=96：102.179
	// 落地却无落地表现）。这里由本 GA 接棒绑一次：正常跑满走 BeginAerialFalling，
	// 尾段内触地则当帧认领落地表现。
	if (bBeginFreeFallAfterEnd)
	{
		if (ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
		{
			Character->LandedDelegate.AddDynamic(this,
				&UMHGZAirDodgeAbility::HandleAirDodgeTailLanded);
			bBoundTailLanded = true;
		}
	}
	TryFinishAirDodge();
	RecordAirDodgeMovementPhase(Cast<ACharacter>(GetAvatarActorFromActorInfo()),
		TEXT("AirDodge.MovementFinished.Post"));
}

void UMHGZAirDodgeAbility::HandleAirDodgeTailLanded(const FHitResult& Hit)
{
	(void)Hit;
	if (!IsActive() || bIsEndingAirDodge || bPlayLandedPresentation)
	{
		return;
	}
	// 两旗标互斥（与撑杆跳 EndAbility:400-403 同口径）：认领落地就不能再交自由落体。
	bBeginFreeFallAfterEnd = false;
	bPlayLandedPresentation = true;
	TryFinishAirDodge();
}

void UMHGZAirDodgeAbility::SupersedeOtherLiveIGActions()
{
	UMHGZAbilitySystemComponent* ASC = Cast<UMHGZAbilitySystemComponent>(
		GetAbilitySystemComponentFromActorInfo());
	UGA_WeaponComboCoordinator* Coordinator = ASC ? ASC->GetActiveComboCoordinator() : nullptr;
	if (!Coordinator)
	{
		return;
	}
	const int32 SupersededCount = Coordinator->SupersedeOtherIGAerialActions(GetActionToken());
	if (SupersededCount > 0)
	{
		UE_LOG(LogMHGZ, Log, TEXT("[AirDodge] supersede %d"), SupersededCount);
	}
}

void UMHGZAirDodgeAbility::HandleAirDodgeVisualCompleted()
{
	RecordAirDodgeMovementPhase(Cast<ACharacter>(GetAvatarActorFromActorInfo()),
		TEXT("AirDodge.VisualCompleted.Pre"));
	// 完成之后任务不再需要收尾，指针先清掉（与撑杆跳同构）—— 否则 EndAbility 会对一个
	// 已经结束的任务再调一次 EndTask。
	AirDodgeVisualTask = nullptr;
	if (!IsActive() || bIsEndingAirDodge)
	{
		return;
	}
	bAirDodgeVisualFinished = true;
	TryFinishAirDodge();
	RecordAirDodgeMovementPhase(Cast<ACharacter>(GetAvatarActorFromActorInfo()),
		TEXT("AirDodge.VisualCompleted.Post"));
}

void UMHGZAirDodgeAbility::HandleAirDodgeVisualCompletedForTest()
{
	// A real OnCompleted arrives while the montage instance is still alive and holding its terminal
	// pose (`bEnableAutoBlendOut = false`), and `EndAbility` is what blends it out. The deterministic
	// CMC test world has no anim graph to reach the length, so retire the instance here instead and
	// then run the exact production completion handler so Host can start the 157 fall montage.
	if (AirDodgeVisualTask)
	{
		AirDodgeVisualTask->EndTask();
		AirDodgeVisualTask = nullptr;
	}
	if (ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		if (UAnimInstance* AnimInstance = Character->GetMesh()
			? Character->GetMesh()->GetAnimInstance() : nullptr)
		{
			if (ActiveAttackMontage)
			{
				AnimInstance->Montage_Stop(0.0f, ActiveAttackMontage);
			}
		}
	}
	HandleAirDodgeVisualCompleted();
}

UAbilityTask_MHGZPlayMontageAndWait*
UMHGZAirDodgeAbility::GetAirDodgeVisualTaskForTest() const
{
	return AirDodgeVisualTask.Get();
}

void UMHGZAirDodgeAbility::HandleAirDodgeVisualInterrupted()
{
	AirDodgeVisualTask = nullptr;
	if (IsActive() && !bIsEndingAirDodge)
	{
		RequestEndAction(EWeaponActionEndReason::Interrupted);
	}
}

void UMHGZAirDodgeAbility::TryFinishAirDodge()
{
	if (!IsActive() || bIsEndingAirDodge || !bAirDodgeMovementFinished)
	{
		return;
	}
	// 触地就得立刻收（落地表现要接上）；还在空中则等蒙太奇把尾段播完 ——
	// 运动源在 ≈0.995 s 就结束了，而 142 帧里最后那 0.188 s 的姿势属于这一招。
	if (bPlayLandedPresentation || bAirDodgeVisualFinished)
	{
		RequestEndAction(EWeaponActionEndReason::Normal);
	}
}

void UMHGZAirDodgeAbility::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	if (bIsEndingAirDodge)
	{
		return;
	}
	ACharacter* EndingCharacter = ActorInfo && ActorInfo->AvatarActor.IsValid()
		? Cast<ACharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	RecordAirDodgeMovementPhase(EndingCharacter, TEXT("AirDodge.EndAbility.Pre"));
	bIsEndingAirDodge = true;

	// 两个交接判据必须在 Super 之前算（Super 会清本 GA 的状态）。
	// 与撑杆跳同构：`IsFalling()` 此刻是可信的 —— 最早的释放点（0.650 s 的点通知）
	// 已经把 CMC 从 Flying 切回 Falling。
	const bool bStartFreeFall = bBeginFreeFallAfterEnd && !bWasCancelled
		&& EndingCharacter && EndingCharacter->GetCharacterMovement()
		&& EndingCharacter->GetCharacterMovement()->IsFalling();
	const bool bPlayLanding = bPlayLandedPresentation && !bWasCancelled;

	AirDodgeMovementTask = nullptr;

	// 尾段落地委托只在源的 FreeFall 分支绑过；收尾一律解绑（幂等）。
	if (bBoundTailLanded)
	{
		if (EndingCharacter)
		{
			EndingCharacter->LandedDelegate.RemoveDynamic(this,
				&UMHGZAirDodgeAbility::HandleAirDodgeTailLanded);
		}
		bBoundTailLanded = false;
	}

	// 显式 EndTask **不会**停蒙太奇（引擎：OnDestroy(false) 不走 StopPlayingMontage；
	// 且先 EndTask 再 Super 连 TaskOwnerEnded 都轮不到它）—— 未播完就收尾时必须显式停，
	// 否则闪断实例会活到下一次 PlayMontage（PIE 第 2 轮 burst 的观感来源）。
	// 与撑杆跳 EndAbility 的同一行同构。
	//
	// ⚠ 这里**不再**用 `bAirDodgeVisualFinished` 当守卫。撑杆跳那条 `if (!bVisualFinished)`
	// 守卫在本招是错的：本招的「视觉完成」= 位置到达蒙太奇自身长度，而 `bEnableAutoBlendOut
	// = false` 让实例在那之后**还活着**并保持终末姿势 —— 恰好是这一行要淡出的对象。
	// 漏掉它，实例会以权重 1.0 一直留在槽里与下落蒙太奇抢姿势。停一条没在播的蒙太奇是空操作。
	if (EndingCharacter && EndingCharacter->GetMesh() && ActiveAttackMontage)
	{
		if (UAnimInstance* AnimInstance = EndingCharacter->GetMesh()->GetAnimInstance())
		{
			AnimInstance->Montage_Stop(0.05f, ActiveAttackMontage);
		}
	}

	if (AirDodgeVisualTask)
	{
		AirDodgeVisualTask->EndTask();
		AirDodgeVisualTask = nullptr;
	}

	// Combat.State.Aerial.Actionable 由基类 EndAbility 的 CloseAerialHandoff() 释放。
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
	RecordAirDodgeMovementPhase(EndingCharacter, TEXT("AirDodge.EndAbility.PostSuper"));

	if (bStartFreeFall)
	{
		if (UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost())
		{
			// style tag 就是 DefaultGameplayTags.ini:64 那个此前零引用的 IG_AirDodge。
			// 真值：`137 → 157 → 落地` 是同一条抛物线、无速度重置，所以这里不需要任何
			// 速度补偿 —— 源结束时装上的切线速度就是这条抛物线的下一段。
			//
			// 下坠 clip 显式取 **157**（GetAerialDodgeFallMontage = WhiteAerialFallMontage
			// = AS_Unsh_Fall_W_Jump）：真值里回避后任何灯态的下坠都是 id 157。物理仍走
			// 非白灯档（false ⇒ 2.4246）—— clip 与重力刻意解耦。
			UAnimMontage* DodgeFallMontage = nullptr;
			if (const UWeaponCombatConfigBase* CombatConfig =
				Host->GetCurrentContext().CombatConfig.Get())
			{
				DodgeFallMontage = CombatConfig->GetAerialDodgeFallMontage();
			}
			Host->BeginAerialFalling(false, AirDodgeFallingTag(), DodgeFallMontage);
			RecordAirDodgeMovementPhase(EndingCharacter, TEXT("AirDodge.EndAbility.PostBeginFall"));
		}
	}
	if (bPlayLanding)
	{
		if (UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost())
		{
			Host->PlayAerialLandingPresentation();
			RecordAirDodgeMovementPhase(EndingCharacter, TEXT("AirDodge.EndAbility.PostLanding"));
		}
	}
	RecordAirDodgeMovementPhase(EndingCharacter, TEXT("AirDodge.EndAbility.Finished"));
}
