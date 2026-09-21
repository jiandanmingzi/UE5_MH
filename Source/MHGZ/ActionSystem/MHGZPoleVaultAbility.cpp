// Copyright MHGZ Project. All Rights Reserved.

#include "ActionSystem/MHGZPoleVaultAbility.h"

#include "ActionSystem/AbilityTask_MHGZPlayMontageAndWait.h"
#include "ActionSystem/AbilityTask_MHGZWeaponMovement.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "Animation/AnimCompositeBase.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceBase.h"
#include "AttributeSystem/Res_InsectGlaive.h"
#include "Curves/CurveVector.h"
#include "Curves/RichCurve.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Generated/MHGZVaultCurveTables.h"
#include "InsectGlaive/InsectGlaiveCombatConfig.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"
#include "MHGZ.h"

namespace
{
const FGameplayTag& WhiteExtractTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("WeaponResource.IG.Extract.White"));
	return Tag;
}

float RequiredSegmentPlayRate(const UAnimSequenceBase& Sequence,
	const float DesiredDuration)
{
	if (!FMath::IsFinite(DesiredDuration) || DesiredDuration <= KINDA_SMALL_NUMBER
		|| !FMath::IsFinite(Sequence.RateScale)
		|| FMath::IsNearlyZero(Sequence.RateScale))
	{
		return 0.0f;
	}
	return Sequence.GetPlayLength() / (DesiredDuration * FMath::Abs(Sequence.RateScale));
}

void AddLinearKey(FRichCurve& Curve, const float Time, const float Value)
{
	const FKeyHandle Handle = Curve.AddKey(Time, Value);
	Curve.SetKeyInterpMode(Handle, RCIM_Linear);
}
}

namespace
{
/**
 * 后撑杆跳的两条实录轨迹。**手抄常量，值逐字未动。**
 *
 * 它们还没有迁到 `Generated/MHGZVaultCurveTables.h` —— 那里只有前/左/右六组。
 * 计划明确要求这一步不动后撑杆跳的手抄表，理由见构造里的注释。
 */
const FVaultTrajectoryKey GBackVaultTrajectory[] = {
		{ 0.000f, FVector(0.000f, 0.000f, 0.000f) },
		{ 0.050f, FVector(-0.004f, 0.009f, 0.000f) },
		{ 0.101f, FVector(0.018f, -0.003f, 0.000f) },
		{ 0.151f, FVector(0.052f, -0.013f, 0.000f) },
		{ 0.202f, FVector(0.159f, -0.019f, 0.043f) },
		{ 0.252f, FVector(0.249f, -0.021f, 0.131f) },
		{ 0.303f, FVector(0.266f, -0.023f, 0.141f) },
		{ 0.328f, FVector(0.272f, -0.023f, 0.177f) },
		{ 0.353f, FVector(0.301f, -0.019f, 0.309f) },
		{ 0.404f, FVector(0.356f, -0.007f, 0.527f) },
		{ 0.454f, FVector(0.413f, -0.002f, 0.703f) },
		{ 0.504f, FVector(0.472f, -0.001f, 0.839f) },
		{ 0.555f, FVector(0.530f, -0.001f, 0.934f) },
		{ 0.605f, FVector(0.589f, -0.001f, 0.987f) },
		{ 0.655f, FVector(0.646f, -0.001f, 1.000f) },
		{ 0.706f, FVector(0.700f, -0.001f, 0.972f) },
		{ 0.756f, FVector(0.751f, -0.001f, 0.902f) },
		{ 0.807f, FVector(0.803f, -0.001f, 0.792f) },
		{ 0.857f, FVector(0.854f, -0.001f, 0.640f) },
		{ 0.908f, FVector(0.906f, -0.001f, 0.447f) },
		{ 0.958f, FVector(0.957f, 0.000f, 0.214f) },
		{ 1.000f, FVector(1.000f, 0.000f, 0.000f) }
};

const FVaultTrajectoryKey GWhiteBackVaultTrajectory[] = {
		{ 0.000f, FVector(0.000f, 0.000f, 0.000f) },
		{ 0.047f, FVector(-0.003f, 0.007f, 0.001f) },
		{ 0.094f, FVector(0.015f, -0.004f, 0.001f) },
		{ 0.142f, FVector(0.058f, -0.017f, 0.001f) },
		{ 0.189f, FVector(0.136f, -0.026f, 0.001f) },
		{ 0.236f, FVector(0.235f, -0.031f, 0.097f) },
		{ 0.283f, FVector(0.350f, -0.035f, 0.149f) },
		{ 0.307f, FVector(0.394f, -0.037f, 0.145f) },
		{ 0.331f, FVector(0.422f, -0.038f, 0.260f) },
		{ 0.378f, FVector(0.464f, -0.040f, 0.467f) },
		{ 0.425f, FVector(0.507f, -0.041f, 0.640f) },
		{ 0.473f, FVector(0.549f, -0.041f, 0.779f) },
		{ 0.520f, FVector(0.590f, -0.037f, 0.885f) },
		{ 0.567f, FVector(0.632f, -0.030f, 0.957f) },
		{ 0.614f, FVector(0.672f, -0.023f, 0.995f) },
		{ 0.662f, FVector(0.712f, -0.017f, 1.000f) },
		{ 0.709f, FVector(0.752f, -0.011f, 0.970f) },
		{ 0.756f, FVector(0.791f, -0.006f, 0.908f) },
		{ 0.803f, FVector(0.829f, -0.003f, 0.812f) },
		{ 0.851f, FVector(0.868f, -0.001f, 0.682f) },
		{ 0.898f, FVector(0.909f, 0.000f, 0.518f) },
		{ 0.945f, FVector(0.951f, 0.000f, 0.321f) },
		{ 0.992f, FVector(0.993f, 0.000f, 0.090f) },
		{ 1.000f, FVector(1.000f, 0.000f, 0.000f) }
};

/**
 * `ClipDrift` 的键形状：**六组生成变体与后撑杆跳两条共用它**，值全为零。
 *
 * 零不是占位：A9 那次实测（2026-09-17）确认锁住根轨道之后实录路径已经把位移
 * 走满，回收量为 0。⚠ 那条结论**只在后撑杆跳上实测过**，新 clip 需要验证第 14 条
 * 的实测确认，否则会静默短跳（少一截位移，且没有任何报错）。
 */
constexpr float GDriftTimes[] = { 0.00f, 0.09f, 0.23f, 0.32f, 1.00f };

TArray<FVaultClipDriftKey> BuildZeroClipDrift()
{
	TArray<FVaultClipDriftKey> Table;
	Table.Reserve(UE_ARRAY_COUNT(GDriftTimes));
	for (const float Time : GDriftTimes)
	{
		FVaultClipDriftKey& Key = Table.AddDefaulted_GetRef();
		Key.CurveTime = Time;
		Key.ForwardFraction = 0.0f;
	}
	return Table;
}

/**
 * 四个方向各自的两条 clip。**曲线与弦向不在表里** —— 后撑杆跳那两条是手抄常量，
 * 前/左/右来自生成表，两条来源不同，所以由 `BuildDirectionProfiles` 分流。
 */
struct FVaultDirectionSeed
{
	EDirectionalInput Direction;
	const TCHAR* NormalJumpPath;
	const TCHAR* NormalJumpOverPath;
	const TCHAR* WhiteJumpPath;
	const TCHAR* WhiteJumpOverPath;
};

/**
 * 三向**共用弧段**（无白灯 `142` / 有白灯 `156`），差别全在起手段 —— 白灯的
 * 左/右**没有专用起手段**，复用 `144`/`145`。这不是偷懒，是实录如此。
 */
const FVaultDirectionSeed GDirectionSeeds[] = {
	{ EDirectionalInput::Back,
	  TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_Jump_Back.AS_Unsh_Jump_Back"), TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_Jump_Over_Back.AS_Unsh_Jump_Over_Back"), TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_W_Jump_Back.AS_Unsh_W_Jump_Back"), TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_W_Jump_Over_Back.AS_Unsh_W_Jump_Over_Back") },
	{ EDirectionalInput::Forward,
	  TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_Jump_Forward.AS_Unsh_Jump_Forward"), TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_Jump_Over.AS_Unsh_Jump_Over"), TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_W_Jump_Forward.AS_Unsh_W_Jump_Forward"), TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_W_Jump_Over.AS_Unsh_W_Jump_Over") },
	{ EDirectionalInput::Left,
	  TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_Jump_Left.AS_Unsh_Jump_Left"), TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_Jump_Over.AS_Unsh_Jump_Over"), TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_Jump_Left.AS_Unsh_Jump_Left"), TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_W_Jump_Over.AS_Unsh_W_Jump_Over") },
	{ EDirectionalInput::Right,
	  TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_Jump_Right.AS_Unsh_Jump_Right"), TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_Jump_Over.AS_Unsh_Jump_Over"), TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_Jump_Right.AS_Unsh_Jump_Right"), TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_W_Jump_Over.AS_Unsh_W_Jump_Over") },
};
} // namespace

UMHGZPoleVaultAbility::UMHGZPoleVaultAbility()
{
	InputTag = FGameplayTag::RequestGameplayTag(TEXT("Input.Weapon.RTA"));
	// 四个方向都是「朝向锁死、只换行进方向」——任何朝向修正都会把整条路径转掉。
	MaxCorrectionAngle = 0.0f;

	// **故意不在这里灌 `VaultProfiles`。**
	//
	// 基类灌了会灌给每一个子类：后撑杆跳的 GA 上就会混进前/左/右六条，
	// `GetVaultDirection()` 要求所有条目同向 ⇒ 返回 None ⇒ 后撑杆跳**直接不出招**。
	// 所以曲线由**每个方向自己的构造**调用 `BuildDirectionProfiles(Direction)` 灌。
	VaultProfiles.Reset();
}

void UMHGZPoleVaultAbility::BuildDirectionProfiles(const EDirectionalInput Direction)
{
	const FVaultDirectionSeed* Seed = nullptr;
	for (const FVaultDirectionSeed& Candidate : GDirectionSeeds)
	{
		if (Candidate.Direction == Direction)
		{
			Seed = &Candidate;
			break;
		}
	}
	if (!Seed)
	{
		UE_LOG(LogMHGZ, Error, TEXT("[Vault] 没有方向 %d 的 clip 配置。"),
			static_cast<int32>(Direction));
		return;
	}

	VaultProfiles.Reset();

	auto Add = [this, Direction](const bool bWhite, const TCHAR* JumpPath,
		const TCHAR* JumpOverPath)
	{
		FVaultProfile& Profile = VaultProfiles.AddDefaulted_GetRef();
		Profile.Direction = Direction;
		Profile.bWhite = bWhite;
		// 起手段实测跑满 78 帧（全部观测），78 / 119.8 = 0.651 s；四条起手段共用同一个值。
		Profile.JumpDuration = 0.650f;
		Profile.JumpSequence = TSoftObjectPtr<UAnimSequenceBase>(FSoftObjectPath(JumpPath));
		Profile.JumpOverSequence = TSoftObjectPtr<UAnimSequenceBase>(FSoftObjectPath(JumpOverPath));
		Profile.ClipDrift = BuildZeroClipDrift();

		if (Direction == EDirectionalInput::Back)
		{
			// 后撑杆跳是**手抄常量**：它的实录不在采集目录里，生成器覆盖不到。
			// 计划要求这一步不动它，所以走这一支，值逐字未变。
			Profile.ChordYawDegrees = 180.0f;
			// 弧段**姿势**长度逐字照抄今天那两条资产的读数（= `GA_IG_HouChengGanTiao`
			// 上 `BackJumpOverDuration` / `WhiteBackJumpOverDuration` 的蓝图覆盖值）。
			// 白灯那个刻意比移动窗口 1.487 长 0.096 s —— 见 `JumpOverDuration` 的注释。
			Profile.JumpOverDuration = bWhite ? 1.583333f : 1.083333f;
			if (bWhite)
			{
				Profile.Trajectory.Append(GWhiteBackVaultTrajectory,
					UE_ARRAY_COUNT(GWhiteBackVaultTrajectory));
			}
			else
			{
				Profile.Trajectory.Append(GBackVaultTrajectory,
					UE_ARRAY_COUNT(GBackVaultTrajectory));
			}
			return;
		}

		FVaultCurveMeta Meta;
		if (!MHGZ::VaultCurves::FindMeta(Direction, bWhite, Meta)
			|| !MHGZ::VaultCurves::BuildTrajectory(Direction, bWhite, Profile.Trajectory))
		{
			// 生成表缺这一组。**不静默留一条空 profile** —— 那会让这个方向表现成
			// 「出招了但没动」，而真正的原因是表与代码不同步。
			UE_LOG(LogMHGZ, Error,
				TEXT("[Vault] 生成表里没有方向 %d / 灯态 %s 的曲线：该变体不可用。"),
				static_cast<int32>(Direction), bWhite ? TEXT("white") : TEXT("normal"));
			VaultProfiles.Pop();
			return;
		}
		Profile.ChordYawDegrees = Meta.ChordYawDegrees;
		// 前/左/右六组的姿势长度**恰好**等于移动窗口减去起手段（生成器就是这么
		// 定窗口的：`VaultDuration = 弧长 − 下坠段`，残差为 0），所以这里可以直接推。
		// 后撑杆跳白灯不满足这个巧合，它走上一条分支。
		Profile.JumpOverDuration = FMath::Max(Meta.VaultDuration - Profile.JumpDuration, 0.0f);
	};

	Add(false, Seed->NormalJumpPath, Seed->NormalJumpOverPath);
	Add(true, Seed->WhiteJumpPath, Seed->WhiteJumpOverPath);
}

const FVaultProfile* UMHGZPoleVaultAbility::ResolveVaultProfile() const
{
	// 方向来自**这个类的身份**，不是本次输入：「空摇杆 RT+A 走前推」是数据层的事
	// （DA_IG_Combo 加一条 Direction=None 的兜底边指向前推 GA），C++ 不需要特例。
	const EDirectionalInput Direction = GetVaultDirection();
	if (Direction == EDirectionalInput::None)
	{
		return nullptr;  // GetVaultDirection 已经记过原因
	}
	for (const FVaultProfile& Profile : VaultProfiles)
	{
		if (Profile.Direction == Direction && Profile.bWhite == bUseWhiteBackVault)
		{
			return &Profile;
		}
	}
	return nullptr;
}

EDirectionalInput UMHGZPoleVaultAbility::GetVaultDirection() const
{
	EDirectionalInput Result = EDirectionalInput::None;
	for (const FVaultProfile& Profile : VaultProfiles)
	{
		if (Profile.Direction == EDirectionalInput::None)
		{
			UE_LOG(LogMHGZ, Error,
				TEXT("[Vault] VaultProfiles 里有 Direction=None 的条目：方向没配，拒激活。"));
			return EDirectionalInput::None;
		}
		if (Result == EDirectionalInput::None)
		{
			Result = Profile.Direction;
		}
		else if (Result != Profile.Direction)
		{
			// 一个 GA 只能是一个方向。混了两个方向的曲线时，灯态只能二选一，
			// 运行时必然用错其中一条 —— 这是配置错误，没有可兜底的解释。
			UE_LOG(LogMHGZ, Error,
				TEXT("[Vault] VaultProfiles 混了多个方向（%d 与 %d）：每个方向应当一个 GA 蓝图。"),
				static_cast<int32>(Result), static_cast<int32>(Profile.Direction));
			return EDirectionalInput::None;
		}
	}
	return Result;
}

void UMHGZPoleVaultAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	bUseWhiteBackVault = CheckExtractRequirement(WhiteExtractTag());
	bBackVaultVisualRootMotionDisabled = false;
	bVisualFinished = false;
	bMovementFinished = false;
	bBeginFreeFallAfterEnd = false;
	bPlayLandedPresentation = false;
	bBackVaultInitialFlightOwned = false;
	PreBackVaultMovementMode = 0;
	PreBackVaultCustomMovementMode = 0;
	bIsEndingBackVault = false;
	BackVaultVisualTask = nullptr;
	JumpOverHandoffTask = nullptr;
	BackVaultMovementTask = nullptr;
	ActiveTrajectoryCurve = nullptr;

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!IsActionActivationCommitted() || !IsActive())
	{
		return;
	}
	if (!ScheduleJumpOverMovementHandoff())
	{
		RequestEndAction(EWeaponActionEndReason::Interrupted);
	}
}

void UMHGZPoleVaultAbility::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	if (bIsEndingBackVault)
	{
		return;
	}
	bIsEndingBackVault = true;
	// Combat.State.Aerial.Actionable 不在这里释放：基类 EndAbility 的
	// CloseAerialHandoff() 负责（本函数末尾会走 Super::EndAbility）。
	if (JumpOverHandoffTask)
	{
		JumpOverHandoffTask->EndTask();
		JumpOverHandoffTask = nullptr;
	}
	// The Jump segment owns Montage Root Motion, then CurvedVault owns the
	// JumpOver path.  If either phase is interrupted, stop this exact visual
	// first; otherwise an already-extracted source-sequence root track can keep
	// CMC in HasAnimRootMotion and swallow subsequent player input.
	ACharacter* VisualCharacter = ActorInfo && ActorInfo->AvatarActor.IsValid()
		? Cast<ACharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	UAnimInstance* AnimInstance = VisualCharacter && VisualCharacter->GetMesh()
		? VisualCharacter->GetMesh()->GetAnimInstance() : nullptr;
	// Balance the JumpOver Push unconditionally.  Whether bVisualFinished is set
	// by now is a race between the movement task and the montage's own length,
	// and the losing branch would leave IsRootMotionDisabled() standing on a
	// montage that keeps playing at weight 1 for another frame.  Popping a
	// matching push cannot double-pop: the flag is cleared as we go.
	if (bBackVaultVisualRootMotionDisabled && AnimInstance)
	{
		if (FAnimMontageInstance* MontageInstance =
			AnimInstance->GetActiveInstanceForMontage(AttackMontage))
		{
			MontageInstance->PopDisableRootMotion();
		}
		bBackVaultVisualRootMotionDisabled = false;
	}
	// The visual itself is only stopped when it did not finish on its own.
	//
	// **两条都要停**：起手段那条通常已经被交叉淡入停掉了（`Montage_Stop` 幂等，
	// 再停一次无害），但**弧段那条会一直保持着终末姿势** —— 它才是这时候还在
	// 出画面的那一条，漏掉它就会让上一个动作的姿势泄进下一个动作。
	if (!bVisualFinished && AnimInstance)
	{
		if (ArcMontage && ArcMontage != AttackMontage)
		{
			AnimInstance->Montage_Stop(0.05f, ArcMontage);
		}
		AnimInstance->Montage_Stop(0.05f, AttackMontage);
	}
	if (BackVaultArcVisualTask)
	{
		BackVaultArcVisualTask->EndTask();
		BackVaultArcVisualTask = nullptr;
	}
	if (BackVaultVisualTask)
	{
		BackVaultVisualTask->EndTask();
		BackVaultVisualTask = nullptr;
	}
	BackVaultMovementTask = nullptr;
	ActiveTrajectoryCurve = nullptr;
	ACharacter* EndingCharacter = ActorInfo && ActorInfo->AvatarActor.IsValid()
		? Cast<ACharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	const bool bStartFreeFall = bBeginFreeFallAfterEnd && !bWasCancelled
		&& EndingCharacter && EndingCharacter->GetCharacterMovement()
		&& EndingCharacter->GetCharacterMovement()->IsFalling();
	// 触地类收尾：弧线自己够到地面，本次 Action 直接认领落地姿势，不经过
	// AerialFalling。白灯就是这一类 —— 它的 HandoffProgress 恒为 1.0，从未有过
	// 自由落体窗口。与 bStartFreeFall 互斥。
	const bool bPlayLanding = bPlayLandedPresentation && !bWasCancelled;
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
	if (bPlayLanding)
	{
		if (UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost())
		{
			Host->PlayAerialLandingPresentation();
		}
	}
	// CurvedVault has already switched to Falling on its normal completion.  Any
	// other exit can happen before CurvedVault exists, or while its source is
	// being torn down; in both cases this GA must return the CMC to a valid
	// locomotion mode instead of leaving it in Flying.
	if (EndingCharacter && !bStartFreeFall)
	{
		RestoreBackVaultInitialFlight(*EndingCharacter);
	}
	else
	{
		bBackVaultInitialFlightOwned = false;
	}
	if (bStartFreeFall)
	{
		if (UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost())
		{
			Host->BeginAerialFalling(bUseWhiteBackVault,
				FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aerial.Falling.IG_BackVault")));
		}
	}
}

bool UMHGZPoleVaultAbility::ValidateActionDependencies() const
{
	const ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	const UInsectGlaiveCombatConfig* CombatConfig = nullptr;
	if (const UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost())
	{
		CombatConfig = Cast<UInsectGlaiveCombatConfig>(Host->GetCurrentContext().CombatConfig);
	}
	if (!Character || !Character->GetMesh() || !Character->GetMesh()->GetAnimInstance()
		|| !GetIGResourceComponent() || !CombatConfig)
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[BackVault] Dependency rejected: Character=%s Mesh=%s AnimInstance=%s Resource=%s CombatConfig=%s"),
			Character ? TEXT("true") : TEXT("false"),
			Character && Character->GetMesh() ? TEXT("true") : TEXT("false"),
			Character && Character->GetMesh() && Character->GetMesh()->GetAnimInstance() ? TEXT("true") : TEXT("false"),
			GetIGResourceComponent() ? TEXT("true") : TEXT("false"),
			CombatConfig ? TEXT("true") : TEXT("false"));
		return false;
	}

	const bool bWhite = CheckExtractRequirement(WhiteExtractTag());
	const EDirectionalInput Direction = GetVaultDirection();
	if (Direction == EDirectionalInput::None)
	{
		return false;  // GetVaultDirection 已经记过原因
	}
	const FVaultProfile* Profile = ResolveVaultProfile();
	if (!Profile)
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[Vault] Dependency rejected: 方向 %d / 灯态 %s 在 VaultProfiles 里没有条目。"),
			static_cast<int32>(Direction), bWhite ? TEXT("white") : TEXT("normal"));
		return false;
	}
	const TSoftObjectPtr<UAnimSequenceBase>& Jump = Profile->JumpSequence;
	const TSoftObjectPtr<UAnimSequenceBase>& JumpOver = Profile->JumpOverSequence;
	const TArray<FVaultTrajectoryKey>& Path = Profile->Trajectory;
	if (!Jump.LoadSynchronous() || !JumpOver.LoadSynchronous()
		|| Path.Num() < 2)
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[BackVault] Dependency rejected: White=%s Jump=%s JumpOver=%s PathKeys=%d"),
			bWhite ? TEXT("true") : TEXT("false"),
			*Jump.ToSoftObjectPath().ToString(), *JumpOver.ToSoftObjectPath().ToString(),
			Path.Num());
		return false;
	}

	// The JumpOver clip's root track must be locked to the ref pose and NOT
	// extracted.  Both other states are wrong, in different ways: with neither
	// flag set its forward lead lives on in the pose, survives until the fall
	// clip replaces it, and then snaps back in two frames (A9); with both set,
	// anim root motion pre-empts the CurvedVault source outright and the whole
	// analytic path dies.  Jump is the opposite case -- it owns montage root
	// motion and no source runs during it -- so it is deliberately not checked.
	if (const UAnimSequence* JumpOverSequence = Cast<UAnimSequence>(JumpOver.Get()))
	{
		const EVaultRootTrackPolicy Policy = ResolveRootTrackPolicy(
			JumpOverSequence->bEnableRootMotion, JumpOverSequence->bForceRootLock);
		if (Policy != EVaultRootTrackPolicy::LockedNotExtracted)
		{
			UE_LOG(LogMHGZ, Warning,
				TEXT("[BackVault] Dependency rejected: JumpOver %s policy=%d, need LockedNotExtracted (EnableRootMotion=%d ForceRootLock=%d)"),
				*JumpOver.ToSoftObjectPath().ToString(), static_cast<int32>(Policy),
				JumpOverSequence->bEnableRootMotion ? 1 : 0,
				JumpOverSequence->bForceRootLock ? 1 : 0);
			return false;
		}
	}

	const FVaultTuning* Tuning = CombatConfig->FindVaultTuning(Direction, bWhite);
	if (!Tuning)
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[Vault] Dependency rejected: CombatConfig 里没有方向 %d / 灯态 %s 的 VaultTuning。"),
			static_cast<int32>(Direction), bWhite ? TEXT("white") : TEXT("normal"));
		return false;
	}
	const float Duration = Tuning->VaultDuration;
	const float Distance = Tuning->Distance;
	const float Apex = Tuning->ApexHeight;
	const bool bValidMetrics = FMath::IsFinite(Duration) && Duration > 0.f
		&& FMath::IsFinite(Distance) && Distance > 0.f
		&& FMath::IsFinite(Apex) && Apex > 0.f;
	if (!bValidMetrics)
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[Vault] Dependency rejected: Direction=%d White=%s Duration=%.3f Distance=%.3f Apex=%.3f"),
			static_cast<int32>(Direction), bWhite ? TEXT("true") : TEXT("false"),
			Duration, Distance, Apex);
	}
	return bValidMetrics;
}

bool UMHGZPoleVaultAbility::PrepareAttackMontage()
{
	if (bUseWhiteBackVault && WhiteAttackMontage)
	{
		AttackMontage = WhiteAttackMontage;
		ArcMontage = WhiteArcMontage;
	}
	else if (!bUseWhiteBackVault && AttackMontage)
	{
		// Already assigned; nothing to build.
	}
	else if (!BuildBackVaultMontage())
	{
		return false;
	}

	if (!AttackMontage || !ArcMontage)
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[Vault] 蒙太奇缺一半：起手段=%s 弧段=%s。两条都要指派（或由兜底构建器产出）。"),
			AttackMontage ? TEXT("有") : TEXT("无"),
			ArcMontage ? TEXT("有") : TEXT("无"));
		return false;
	}

	// Whether this action has an earliest-actionable frame at all is a property of
	// the montage, so it is read once here rather than inferred at the handoff.
	//
	// **查弧段那一条** —— 拆成双蒙太奇之后点通知挂在弧段上，它的零点就是弧段自己的
	// 起点。起手段那一半**不该**有它（它只有 0.650 s 长、墙在 0.783 s，永远等不到，
	// 而「有没有」的判据却会为真）；清掉它的是烘焙命令列 `MHGZPoleVaultMontageSetup`。
	// 两条都查、任一条有即认，是为了让「有人把通知写错半边」当场看得见。
	//
	// 注意 BuildBackVaultMontage() 建出来的是 RF_Transient 的运行时蒙太奇，上面
	// 不可能挂点通知 —— 那条兜底路径下 authored 恒为 false，弧走完后由任务的
	// EnsureVaultFlightReleased() 接管（并会告警）。
	DetectAerialHandoffNotify(ArcMontage);
	if (!IsAerialHandoffAuthored() && MontageHasAerialHandoffNotify(AttackMontage))
	{
		UE_LOG(LogMHGZ, Error,
			TEXT("[Vault] 点通知挂错了半边 —— 它出现在起手段 %s 上，而弧段 %s 上没有。")
			TEXT("起手段只有 ~0.65 s 长而墙在 0.783 s，永远触发不了。")
			TEXT("先重跑 MHGZPoleVaultMontageSetup（它会清掉起手段的 Notifies）")
			TEXT("再重跑 MHGZAerialHandoffSetup。"),
			*GetNameSafe(AttackMontage), *GetNameSafe(ArcMontage));
	}
	return true;
}

bool UMHGZPoleVaultAbility::StartAttackMontage(ACharacter& Character,
	UAnimMontage* Montage, FName StartSection)
{
	// This must happen before the montage task is activated.  In Walking mode,
	// CMC constrains an animation Root Motion delta to the floor, so the Jump
	// sequence can pose a pole-vault without ever raising the capsule.
	if (!BeginBackVaultInitialFlight(Character))
	{
		return false;
	}
	// 起手段那一条**不绑 `OnCompleted`** —— 它的长度就是 `JumpDuration`，必然在
	// 动作中途"完成"。接了会把 `bVisualFinished` 提前置位，`TryFinishBackVault` 就会在
	// 弧段姿势还没播完时结束整个动作。完成信号只归弧段。
	if (!PlayVaultMontage(Character, Montage, StartSection,
		ResolveAttackMontageBlendInTime(GetWeaponActivationContext()),
		TEXT("BackVaultVisual"), /*bBindCompleted=*/false, BackVaultVisualTask))
	{
		RestoreBackVaultInitialFlight(Character);
		return false;
	}
	return true;
}

bool UMHGZPoleVaultAbility::PlayVaultMontage(ACharacter& Character,
	UAnimMontage* Montage, FName StartSection, float BlendInTime,
	const TCHAR* TaskName, bool bBindCompleted,
	TObjectPtr<UAbilityTask_MHGZPlayMontageAndWait>& OutTask)
{
	UAnimInstance* AnimInstance = Character.GetMesh()
		? Character.GetMesh()->GetAnimInstance() : nullptr;
	if (!AnimInstance || !Montage)
	{
		return false;
	}

	OutTask = UAbilityTask_MHGZPlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, TaskName, Montage, 1.0f, StartSection, BlendInTime, true, 1.0f);
	if (!OutTask)
	{
		return false;
	}
	if (bBindCompleted)
	{
		OutTask->OnCompleted.AddDynamic(this,
			&UMHGZPoleVaultAbility::HandleBackVaultVisualCompleted);
	}
	OutTask->OnInterrupted.AddDynamic(this,
		&UMHGZPoleVaultAbility::HandleBackVaultVisualInterrupted);
	OutTask->OnCancelled.AddDynamic(this,
		&UMHGZPoleVaultAbility::HandleBackVaultVisualInterrupted);
	OutTask->ReadyForActivation();

	FAnimMontageInstance* MontageInstance = AnimInstance->GetActiveInstanceForMontage(Montage);
	if (!MontageInstance
		|| !RegisterMontageInstance(Character.GetMesh(), MontageInstance->GetInstanceID()))
	{
		AnimInstance->Montage_Stop(0.0f, Montage);
		OutTask = nullptr;
		return false;
	}

	// `bEnableAutoBlendOut = false` 让这条蒙太奇在自身长度处**不终止、保持终末姿势**。
	// 对起手段来说这是交叉淡入的**前提**：播弧段那条时
	// `StopAllMontagesByGroupName` 要靠这份还活着的姿势做 0.10 s 淡出。对弧段来说
	// 则和从前一样 —— 物理曲线可能比视觉链晚几帧收尾，终末姿势要一直保持到
	// `EndAbility` 起下一个表现。两种情形都不要改成 true。
	//
	// Jump owns its imported Montage Root Motion.  The JumpOver boundary disables only
	// the remaining visual root track immediately before CurvedVault starts; do not use
	// Character-wide root-translation scaling here.
	MontageInstance->bEnableAutoBlendOut = false;
	return true;
}

bool UMHGZPoleVaultAbility::BeginBackVaultInitialFlight(ACharacter& Character)
{
	UCharacterMovementComponent* CMC = Character.GetCharacterMovement();
	if (!CMC)
	{
		return false;
	}

	if (bBackVaultInitialFlightOwned)
	{
		return CMC->MovementMode == MOVE_Flying;
	}

	PreBackVaultMovementMode = static_cast<uint8>(CMC->MovementMode);
	PreBackVaultCustomMovementMode = CMC->CustomMovementMode;
	CMC->SetMovementMode(MOVE_Flying);
	bBackVaultInitialFlightOwned = CMC->MovementMode == MOVE_Flying;
	return bBackVaultInitialFlightOwned;
}

void UMHGZPoleVaultAbility::RestoreBackVaultInitialFlight(ACharacter& Character)
{
	if (!bBackVaultInitialFlightOwned)
	{
		return;
	}
	bBackVaultInitialFlightOwned = false;

	UCharacterMovementComponent* CMC = Character.GetCharacterMovement();
	if (!CMC || CMC->MovementMode != MOVE_Flying)
	{
		return;
	}

	const EMovementMode PreviousMode = static_cast<EMovementMode>(PreBackVaultMovementMode);
	// MOVE_Flying 也在这一支：弧现在整段跑在 Flying 里，所以上一场动作异常结束时
	// 保存下来的「之前模式」完全可能本身就是 Flying。照原样恢复等于把这个错误状态
	// 一路重申下去，而 Flying 没有重力 —— 胶囊会悬停。
	if (PreviousMode == MOVE_Walking || PreviousMode == MOVE_NavWalking
		|| PreviousMode == MOVE_Flying)
	{
		// Re-checking the floor avoids incorrectly pinning an interrupted vault to
		// Walking when its capsule is already above the ground.
		CMC->SetDefaultMovementMode();
	}
	else
	{
		CMC->SetMovementMode(PreviousMode, PreBackVaultCustomMovementMode);
	}
}

bool UMHGZPoleVaultAbility::BuildBackVaultMontage()
{
	// 这个兜底只认后撑杆跳的两条 clip（`BackJumpSequence` 那一组字段）。
	// 别的方向走到这里说明蒙太奇资产没指派 —— **直接失败**，而不是拿后撑杆跳的
	// 动画演一遍前撑杆跳：后者会「出招了但动作不对」，比不出招难查得多。
	const EDirectionalInput FallbackDirection = GetVaultDirection();
	if (FallbackDirection != EDirectionalInput::Back)
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[Vault] 方向 %d 没有蒙太奇资产，而回退构建器只支持 Back：拒激活。"),
			static_cast<int32>(FallbackDirection));
		return false;
	}
	const TSoftObjectPtr<UAnimSequenceBase>& JumpRef = bUseWhiteBackVault
		? WhiteBackJumpSequence : BackJumpSequence;
	const TSoftObjectPtr<UAnimSequenceBase>& JumpOverRef = bUseWhiteBackVault
		? WhiteBackJumpOverSequence : BackJumpOverSequence;
	UAnimSequenceBase* Jump = JumpRef.LoadSynchronous();
	UAnimSequenceBase* JumpOver = JumpOverRef.LoadSynchronous();
	if (!Jump || !JumpOver || !Jump->GetSkeleton()
		|| Jump->GetSkeleton() != JumpOver->GetSkeleton())
	{
		return false;
	}

	const float JumpDuration = bUseWhiteBackVault ? WhiteBackJumpDuration : BackJumpDuration;
	const float JumpOverDuration = bUseWhiteBackVault
		? WhiteBackJumpOverDuration : BackJumpOverDuration;
	if (RequiredSegmentPlayRate(*Jump, JumpDuration) <= 0.f
		|| RequiredSegmentPlayRate(*JumpOver, JumpOverDuration) <= 0.f)
	{
		return false;
	}

	// **建两条**单段蒙太奇，形状与烘焙命令列产出的资产一致 —— 兜底路径与资产路径
	// 必须是同一种形状，否则 `StartBackVaultMovement` 里那句「播弧段、让起手段交叉
	// 淡出」在兜底路径上会退化成两条各播各的。
	//
	// 段名沿用 `"Jump"` / `"JumpOver"`：拆分之后它们只是**可读性标签**，运行时不再
	// 靠段名找边界（交棒由 `JumpDuration` 触发）。
	auto BuildSingle = [this](UAnimSequenceBase& Sequence, const float DesiredDuration,
		const FName SectionName) -> UAnimMontage*
	{
		UAnimMontage* Montage = NewObject<UAnimMontage>(this, NAME_None, RF_Transient);
		if (!Montage)
		{
			return nullptr;
		}
		Montage->SetSkeleton(Sequence.GetSkeleton());
		Montage->BlendIn.SetBlendTime(3.0f / 60.0f);
		Montage->BlendOut.SetBlendTime(0.05f);

		FSlotAnimationTrack& SlotTrack = Montage->SlotAnimTracks.AddDefaulted_GetRef();
		SlotTrack.SlotName = BackVaultMontageSlot;

		FAnimSegment& Segment = SlotTrack.AnimTrack.AnimSegments.AddDefaulted_GetRef();
		Segment.SetAnimReference(&Sequence, true);
		Segment.StartPos = 0.0f;
		Segment.AnimStartTime = 0.0f;
		Segment.AnimEndTime = Sequence.GetPlayLength();
		Segment.AnimPlayRate = RequiredSegmentPlayRate(Sequence, DesiredDuration);
		Segment.LoopingCount = 1;

		FCompositeSection& Section = Montage->CompositeSections.AddDefaulted_GetRef();
		Section.SectionName = SectionName;
		Section.Link(Montage, 0.0f, 0);

		Montage->UpdateLinkableElements();
		Montage->RefreshCacheData();
		return Montage;
	};

	// 这里**不**再断言 `GetPlayLength()`：刚建出来、还没进过缓存路径的蒙太奇上它
	// 恒返回 0（烘焙命令列为此专门绕开过它），拿它当判据只会得到假失败。段速率
	// 已经被上面那道 `RequiredSegmentPlayRate > 0` 校验挡住了。
	UAnimMontage* Takeoff = BuildSingle(*Jump, JumpDuration, TEXT("Jump"));
	UAnimMontage* Arc = BuildSingle(*JumpOver, JumpOverDuration, TEXT("JumpOver"));
	if (!Takeoff || !Arc)
	{
		return false;
	}

	if (bUseWhiteBackVault)
	{
		WhiteAttackMontage = Takeoff;
		WhiteArcMontage = Arc;
	}
	AttackMontage = Takeoff;
	ArcMontage = Arc;
	return true;
}

bool UMHGZPoleVaultAbility::ScheduleJumpOverMovementHandoff()
{
	if (!IsActive() || bIsEndingBackVault || JumpOverHandoffTask
		|| !IsActionActivationCommitted())
	{
		return false;
	}

	const FVaultProfile* Profile = ResolveVaultProfile();
	const float JumpDuration = Profile ? Profile->JumpDuration : 0.0f;
	if (!FMath::IsFinite(JumpDuration) || JumpDuration <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	// 交棒时刻 = `JumpDuration` 减去一个**提前量**，量纲是秒。
	//
	// 为什么需要它：这条 `WaitDelay` 在**能力 tick** 里到期，而胶囊的位移要在
	// **CMC 的 movement tick** 里累加 —— 后者在同一帧里更早。于是源第一次真正
	// 推动胶囊要等到下一帧，中间那一帧**没有任何东西在推**：起手段剪辑自己的根
	// 位移这时候已经走完（实测它在边界前 ~1 帧就停了），姿势还在动、胶囊不动。
	// 白灯左跳 15 次录制里源的首帧落在蒙太奇 0.675 / 0.700 / 0.725（±1 帧抖动），
	// 就是这一帧的量化误差。
	//
	// ⚠ **值必须实测标定，不要拍。** 提前量会把整条弧连同姿势一起前移，同时让
	// 起手段**少走**尾部那么多帧的根位移（那一带竖直速度有 ~11 cm/帧，不小）。
	// 默认 0 = 今天的既有行为，即先把结构改对、留好旋钮，再拿一次带下面那条
	// `[VaultHandoff]` 日志的 PIE 读数定它。
	const float HandoffLeadSeconds = VaultHandoffLeadSeconds;
	const float HandoffDelay = FMath::Max(JumpDuration - HandoffLeadSeconds, 0.0f);

	JumpOverHandoffTask = UAbilityTask_WaitDelay::WaitDelay(this, HandoffDelay);
	if (!JumpOverHandoffTask)
	{
		return false;
	}
	JumpOverHandoffTask->OnFinish.AddDynamic(this,
		&UMHGZPoleVaultAbility::HandleJumpOverMovementHandoff);
	JumpOverHandoffTask->ReadyForActivation();
	return true;
}

void UMHGZPoleVaultAbility::HandleJumpOverMovementHandoff()
{
	JumpOverHandoffTask = nullptr;
	if (!IsActive() || bIsEndingBackVault || !IsActionActivationCommitted())
	{
		return;
	}

	// 标定 `VaultHandoffLeadSeconds` 用的仪表（见那个属性的 docstring）。
	//
	// 要回答的问题只有一个：**这条日志的世界时间，与蒙太奇走到 `JumpDuration` 的
	// 世界时间，差了几帧。** 对着 `Character/Spatial.csv` 读 `LocationZ` 逐帧差，
	// 「零位移帧」出现在哪一帧就是它。读数落进 PIE 日志，不需要额外工具。
	{
		const FVaultProfile* LogProfile = ResolveVaultProfile();
		const ACharacter* LogCharacter = Cast<ACharacter>(GetAvatarActorFromActorInfo());
		const UAnimInstance* LogAnim = LogCharacter && LogCharacter->GetMesh()
			? LogCharacter->GetMesh()->GetAnimInstance() : nullptr;
		const FAnimMontageInstance* LogInstance = LogAnim && AttackMontage
			? LogAnim->GetActiveInstanceForMontage(AttackMontage) : nullptr;
		UE_LOG(LogMHGZ, Display,
			TEXT("[VaultHandoff] t=%.4f 方向=%d 灯=%s JumpDuration=%.4f 提前量=%.4f ")
			TEXT("起手段蒙太奇位置=%.4f 弧段已播=%.4f"),
			GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
			static_cast<int32>(GetVaultDirection()),
			bUseWhiteBackVault ? TEXT("white") : TEXT("normal"),
			LogProfile ? LogProfile->JumpDuration : -1.0f,
			VaultHandoffLeadSeconds,
			LogInstance ? LogInstance->GetPosition() : -1.0f,
			BackVaultArcVisualTask ? 1.0f : 0.0f);
	}

	if (!StartBackVaultMovement())
	{
		RequestEndAction(EWeaponActionEndReason::Interrupted);
	}
}

bool UMHGZPoleVaultAbility::StartBackVaultMovement()
{
	if (!IsActive() || bIsEndingBackVault || BackVaultMovementTask
		|| !IsActionActivationCommitted())
	{
		return false;
	}

	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	const UInsectGlaiveCombatConfig* CombatConfig = Host
		? Cast<UInsectGlaiveCombatConfig>(Host->GetCurrentContext().CombatConfig) : nullptr;
	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!Host || !CombatConfig || !Character)
	{
		return false;
	}

	UAnimInstance* AnimInstance = Character->GetMesh()
		? Character->GetMesh()->GetAnimInstance() : nullptr;
	FAnimMontageInstance* MontageInstance = AnimInstance && AttackMontage
		? AnimInstance->GetActiveInstanceForMontage(AttackMontage) : nullptr;
	if (!MontageInstance)
	{
		return false;
	}

	// Jump has completed.  Disable only the remaining JumpOver root track before
	// CurvedVault acquires the capsule so animation Root Motion cannot compete
	// with the procedural path or rotate the character during the handoff.
	//
	// **必须排在播弧段蒙太奇之前。** 起手段那条还要淡出 `VaultSeamBlendTime` 秒
	// （权重从 1 线性降到 0），而它的根位移此刻已经走完；不先关掉，那 0.1334 s 里
	// 它还会继续跟曲线源抢胶囊。先关则淡出期间它只贡献姿势。
	MontageInstance->PushDisableRootMotion();
	bBackVaultVisualRootMotionDisabled = true;

	// ── 接缝：在**同一帧**把弧段蒙太奇推上去 ────────────────────────────────
	//
	// 这一句是交叉淡入的唯一来源：`Montage_PlayInternal` 在 `bStopAllMontages`
	// 为真时调 `StopAllMontagesByGroupName(Group, BlendInSettings)`，**拿这里的
	// `BlendInTime` 把起手段按同样时长淡出**。起手段因为
	// `bEnableAutoBlendOut = false` 还活着、还保持着终末姿势，所以淡出的是真姿势
	// 而不是空位 —— 那个姿势正是接缝的另一侧。
	//
	// 为什么非拆不可：蒙太奇内部的段与段之间**没有混合**（`FAnimTrack::
	// GetAnimationPose` 用 `GetSegmentAtTime` 只取一段），接缝的逐骨落差实测是
	// 弧段自身相邻一帧的 1.4×~4.3×。
	//
	// ⚠⚠ **必须先摘掉起手段任务的中断回调，否则每一次撑杆跳都会在 0.650 s 结束。**
	// 同一次 `StopAllMontagesByGroupName` 传的是 `bInterrupt = true`
	// （`AnimInstance.cpp:3280`），而 `UAbilityTask_PlayMontageAndWait::
	// OnMontageBlendingOut` 在 `bInterrupted` 时直接 `OnInterrupted.Broadcast()`
	// （`AbilityTask_PlayMontageAndWait.cpp:42-44`）—— 那条回调会走到
	// `HandleBackVaultVisualInterrupted` → `RequestEndAction(Interrupted)`。
	//
	// 不能改成「起手段干脆不绑中断」：起手段阶段的**真**中断（取消输入、死亡、
	// 换武器）仍然必须结束动作，而动态委托不带 sender，一个处理函数分不出这两件事。
	// 在接缝这一帧摘掉，就同时保住了两条性质。
	//
	// 也**不**在这里 `EndTask()`：`bStopWhenAbilityEnds` 为真时任务销毁会调
	// `StopPlayingMontage()` 把起手段**无混合地**停掉，那正好毁掉这次交叉淡入。
	// 让它活着，由 `EndAbility` 照旧收尾。
	if (BackVaultVisualTask)
	{
		BackVaultVisualTask->OnInterrupted.RemoveDynamic(this,
			&UMHGZPoleVaultAbility::HandleBackVaultVisualInterrupted);
		BackVaultVisualTask->OnCancelled.RemoveDynamic(this,
			&UMHGZPoleVaultAbility::HandleBackVaultVisualInterrupted);
	}
	if (!PlayVaultMontage(*Character, ArcMontage, NAME_None, VaultSeamBlendTime,
		TEXT("BackVaultArcVisual"), /*bBindCompleted=*/true, BackVaultArcVisualTask))
	{
		return false;
	}

	// An ActionRootMotionPhase placed on the imported Jump sequence may still
	// own the Host at this exact boundary.  Release that exact owner before the
	// CurvedVault task takes ActionMovement; later notify end events are stale
	// and harmless.
	if (Host->IsMontageRootMotionOwnedBy(GetActionToken()))
	{
		Host->ReleaseMontageRootMotion(GetActionToken());
	}

	// 本次移动的全部来源都从 (方向, 灯态) 查出来，不再读平铺字段。
	const FVaultProfile* Profile = ResolveVaultProfile();
	const FVaultTuning* Tuning = CombatConfig
		? CombatConfig->FindVaultTuning(GetVaultDirection(), bUseWhiteBackVault) : nullptr;
	if (!Profile || !Tuning)
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[Vault] 拒绝移动：方向 %d / 灯态 %s 缺 Profile(%d) 或 Tuning(%d)。"),
			static_cast<int32>(GetVaultDirection()),
			bUseWhiteBackVault ? TEXT("white") : TEXT("normal"),
			Profile ? 1 : 0, Tuning ? 1 : 0);
		return false;
	}
	const float Duration = Tuning->VaultDuration;
	const float Distance = Tuning->Distance;
	const float ApexHeight = Tuning->ApexHeight;
	const TArray<FVaultTrajectoryKey>& Keys = Profile->Trajectory;
	const TArray<FVaultClipDriftKey>& DriftKeys = Profile->ClipDrift;
	const float JumpDuration = Profile->JumpDuration;
	const float JumpOverDuration = Duration - JumpDuration;
	// The recorded path is normalised over the whole airborne arc -- Jump,
	// JumpOver and the descent -- so every progress here is a fraction of that.
	// It used to be a fraction of Jump + JumpOver only, which put the Jump
	// boundary at 0.375 instead of 0.330 and handed the Jump ~30 cm of travel
	// that belongs to JumpOver.  The CurvedVault window keeps its own duration;
	// the two quantities are no longer derived from one number.
	const float ArcDuration = Tuning->ArcDuration;
	if (!FMath::IsFinite(ArcDuration) || ArcDuration <= KINDA_SMALL_NUMBER)
	{
		return false;
	}
	const float StartProgress = JumpDuration / ArcDuration;
	const float HandoffProgress = (JumpDuration + JumpOverDuration) / ArcDuration;
	const float CurveDuration = JumpOverDuration;
	// The recorded arc is normalised over the whole airborne chain, so whatever the
	// action-owned window does not cover is exactly the free fall that follows it.
	// White is 1.0 here and therefore has none; non-white leaves 0.2347 s, which is
	// MHR id 143's measured 0.234 s.
	//
	// 这个残余量**曾经**存进 BackVaultResidualFallSeconds 并喂给 exit 判据。现在判据
	// 只吃结束原因（见 ResolveVaultExit），字段已删 —— 一个写进去没人读的量正是本项目
	// 删掉 BackVaultFreeFallHandoffProgress 的同一类东西。
	if (!FMath::IsFinite(StartProgress) || StartProgress <= 0.0f
		|| !FMath::IsFinite(HandoffProgress) || StartProgress >= HandoffProgress
		// HandoffProgress == 1.0 is legal: the white variant has no separate
		// descent segment, so its whole arc is the CurvedVault window.
		|| HandoffProgress > 1.0f || !FMath::IsFinite(CurveDuration)
		|| CurveDuration <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	float CurveDistance = 0.0f;
	FVector HandoffVelocityLocal = FVector::ZeroVector;
	ActiveTrajectoryCurve = BuildTrajectoryCurve(Keys, DriftKeys, StartProgress,
		HandoffProgress, Distance, ApexHeight, CurveDuration, CurveDistance,
		HandoffVelocityLocal);
	if (!ActiveTrajectoryCurve || HandoffVelocityLocal.IsNearlyZero())
	{
		return false;
	}

	FWeaponMovementRequest Request;
	Request.OwnerAction = GetActionToken();
	Request.Mode = EWeaponMovementMode::CurvedVault;
	// 基准 = **冻结的输入朝向，绕 yaw 转该变体的整条链弦向**。
	//
	// 用冻结的输入朝向而不是此刻的角色朝向：测量基准就是起跳那一刻，而 Jump 段
	// 由**蒙太奇 root motion** 驱动 —— 拿实时朝向会被 clip 自带的 yaw 带跑。
	// 手性的推导与踩坑记录见 `ComputeDirectionSnapshot` 的定义。
	Request.DirectionSnapshot = ComputeDirectionSnapshot(
		GetWeaponActivationContext().Input.ActorForward, Profile->ChordYawDegrees);
	Request.MaxDistance = CurveDistance;
	Request.Duration = CurveDuration;
	Request.PathOffsetCurve = ActiveTrajectoryCurve;
	Request.CurvedVaultHandoffVelocityLocal = HandoffVelocityLocal;
	Request.RotationPolicy = EActionRotationPolicy::Locked;
	// Preserve the CurvedVault's tangent at the handoff.  Once the source ends,
	// CMC alone integrates gravity and collision for the free-fall phase.
	Request.CancelVelocityPolicy = EMovementCancelVelocityPolicy::PreserveVelocity;
	if (!Request.HasValidCurvedVaultParameters())
	{
		return false;
	}

	UAbilityTask_MHGZWeaponMovement* Task =
		UAbilityTask_MHGZWeaponMovement::StartWeaponMovement(this, TEXT("BackVaultPath"), Request);
	if (!Task)
	{
		return false;
	}
	BackVaultMovementTask = Task;
	Task->OnFinished.AddDynamic(this,
		&UMHGZPoleVaultAbility::HandleBackVaultMovementFinished);
	Task->ReadyForActivation();
	if (!Task->DidStartMovement())
	{
		BackVaultMovementTask = nullptr;
		return false;
	}
	return true;
}

namespace
{
/** Linear interpolation of the recorded local trajectory at one normalised time. */
bool SampleTrajectoryPosition(const TArray<FVaultTrajectoryKey>& Keys,
	const float Progress, FVector& OutPosition)
{
	for (int32 Index = 1; Index < Keys.Num(); ++Index)
	{
		const FVaultTrajectoryKey& Previous = Keys[Index - 1];
		const FVaultTrajectoryKey& Next = Keys[Index];
		if (Progress <= Next.Time)
		{
			const float Span = Next.Time - Previous.Time;
			const float Alpha = Span > KINDA_SMALL_NUMBER
				? (Progress - Previous.Time) / Span : 0.0f;
			OutPosition = FMath::Lerp(Previous.NormalizedPosition, Next.NormalizedPosition,
				FMath::Clamp(Alpha, 0.0f, 1.0f));
			return true;
		}
	}
	return false;
}
}

bool UMHGZPoleVaultAbility::ComputeTrajectoryTangent(
	const TArray<FVaultTrajectoryKey>& Keys, const float StartProgress,
	const float HandoffProgress, const float TotalDistance, const float ApexHeight,
	const float CurveDuration, FVector& OutVelocityLocal)
{
	OutVelocityLocal = FVector::ZeroVector;
	const float ProgressSpan = HandoffProgress - StartProgress;
	if (Keys.Num() < 2 || TotalDistance <= 0.f || ApexHeight <= 0.f
		|| !FMath::IsFinite(CurveDuration) || CurveDuration <= KINDA_SMALL_NUMBER
		|| !FMath::IsFinite(ProgressSpan) || ProgressSpan <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	FVector HandoffPosition;
	if (!SampleTrajectoryPosition(Keys, HandoffProgress, HandoffPosition))
	{
		return false;
	}

	// Taking a backward difference over the recorded prefix keeps the sample
	// inside the action-owned segment and yields the same vector the curve is
	// about to hand to the CMC, so the hand-off no longer has to be read back
	// from a live root-motion source after the fact.
	float LastKeyBeforeHandoff = StartProgress;
	for (const FVaultTrajectoryKey& Key : Keys)
	{
		if (Key.Time <= StartProgress)
		{
			continue;
		}
		if (Key.Time >= HandoffProgress)
		{
			break;
		}
		LastKeyBeforeHandoff = Key.Time;
	}
	// A prefix with no interior key has no measured shape to differentiate, so
	// fall back to the average over the whole span.
	const float AvailableSpan = HandoffProgress - LastKeyBeforeHandoff;
	const float DeltaProgress = AvailableSpan > KINDA_SMALL_NUMBER
		? FMath::Min(ProgressSpan * 0.02f, AvailableSpan * 0.5f) : ProgressSpan;
	FVector BeforeHandoffPosition;
	if (DeltaProgress <= KINDA_SMALL_NUMBER
		|| !SampleTrajectoryPosition(Keys, HandoffProgress - DeltaProgress,
			BeforeHandoffPosition))
	{
		return false;
	}

	const FVector PositionRatePerProgress =
		(HandoffPosition - BeforeHandoffPosition) / DeltaProgress;
	// The curve's normalised time spans ProgressSpan of recorded progress, and the
	// source advances it once over CurveDuration.
	OutVelocityLocal = FVector(
		PositionRatePerProgress.X * TotalDistance,
		PositionRatePerProgress.Y * TotalDistance,
		PositionRatePerProgress.Z * ApexHeight) * (ProgressSpan / CurveDuration);
	return !OutVelocityLocal.ContainsNaN() && !OutVelocityLocal.IsNearlyZero();
}

EVaultRootTrackPolicy
UMHGZPoleVaultAbility::ResolveRootTrackPolicy(
	const bool bEnableRootMotion, const bool bForceRootLock)
{
	if (bForceRootLock)
	{
		return bEnableRootMotion
			? EVaultRootTrackPolicy::Conflicting
			: EVaultRootTrackPolicy::LockedNotExtracted;
	}
	return bEnableRootMotion
		? EVaultRootTrackPolicy::Extracted
		: EVaultRootTrackPolicy::Unaccounted;
}

FVector UMHGZPoleVaultAbility::ComputeDirectionSnapshot(
	const FVector& FrozenActorForward, const float ChordYawDegrees)
{
	// ── ⚠ 弦向必须**减**：两个约定的 yaw 正方向相反 ───────────────────────
	//
	// `ChordYawDegrees` 存在 **MHR / 文档的约定**里，那个约定里正角指向猎人的**左**：
	//   · `(f × t)_y = f_z·t_x − f_x·t_z > 0` ⇔ 行进在朝向的左侧（**用录制现算的**：
	//     向左 +0.998、向右 −0.995，与招式命名吻合）；
	//   · 本项目归一化曲线用的侧向轴 `(p_x, p_z) = (t_z, −t_x)` 正是那个左手侧 ——
	//     所以 CSV 里 `y_lateral > 0` 是**向左**偏。
	// 而 UE 的 `FRotator(0, Yaw, 0).Vector()` 在 `Yaw > 0` 时指向 `+Y`，是角色的**右**。
	// 同一个数值，两侧相反 ⇒ 必须取负。
	//
	// ── **这个符号写错过一次，代价是左右撑杆跳互换**（用户 PIE 实测）────────
	//
	// ⚠ **当时错在哪，必须记准 —— 不是「两个坐标系手性不同」。**
	//
	//   根骨局部帧与文档列**其实是同一个约定**。标定锚点**不依赖招式命名**：后撑杆跳的
	//   Jump 段由该 clip 的蒙太奇根运动驱动，而 PIE 已确认它把猎人送**后**，同时
	//   `AS_Unsh_Jump_Back` 的根骨局部净位移是 **(−13.2, −156.1) cm**
	//   ⇒ **局部 +Y = 角色的前**。于是 `AS_Unsh_Jump_Left` 的 (+171.6, +16.3) 就是
	//   「沿角色的**左**走 172.4 cm」（MHR 实测 173 cm 吻合）⇒ **局部 +X = 角色的左**。
	//   局部 `atan2(Δx, Δy)` 因此**朝左为正**，与文档列逐向同号同量级
	//   （141 +6.44/+6.5、144 +84.59/+84.8、145 −78.93/−79.3）。
	//   **同号是真的一致，不是「两侧相反的迹象」。**
	//
	//   错误发生在**最后一步**：把「朝左为正」的文档角**直接当成 UE 的 yaw** 用，
	//   而 UE 的 yaw 是「朝右为正」。取负的原因**只有一个，就是这一句** —— 把它说成
	//   「手性不同」，会让人顺着那个错理由把负号再翻回去（审计逮到过这段措辞）。
	//
	// 之所以难发现：后撑杆跳的弦向恰好是 180（取负等于不取负），向前只有 0.56°，
	// **写反了只会在左右两个方向上暴露**。所以这段逻辑被抽成独立纯函数，由
	// `MHGZM5MovementTests.cpp` 按「正弦向 = 角色的左侧」直接断言。
	//
	// 弦向本身取**实测值而不是 ±90 这类整数**：整数角会让曲线末帧的侧向分量从
	// 0 变成约 30 cm，违反消费方「起止偏移为零」的约束。
	const float FacingYaw = FrozenActorForward.Rotation().Yaw;
	return FRotator(0.0f, FacingYaw - ChordYawDegrees, 0.0f).Vector();
}

bool UMHGZPoleVaultAbility::SampleClipDriftForwardFraction(
	const TArray<FVaultClipDriftKey>& DriftKeys, const float CurveTime,
	float& OutFraction)
{
	OutFraction = 0.0f;
	if (DriftKeys.Num() == 0)
	{
		// No drift to recover is a legitimate configuration.
		return true;
	}
	if (DriftKeys.Num() < 2 || !FMath::IsFinite(CurveTime))
	{
		return false;
	}
	// A first key with a head start, or a table that does not reach the end of
	// the window, both mean the capsule would take a discontinuity at a boundary.
	if (!FMath::IsNearlyZero(DriftKeys[0].CurveTime)
		|| !FMath::IsNearlyEqual(DriftKeys.Last().CurveTime, 1.0f))
	{
		return false;
	}
	// Validate the WHOLE table before sampling anything.  Interpolating in the
	// same pass would only inspect keys up to the sample point, so a malformed
	// tail would slip through on every frame that samples before reaching it.
	for (int32 Index = 0; Index < DriftKeys.Num(); ++Index)
	{
		const FVaultClipDriftKey& Key = DriftKeys[Index];
		if (!FMath::IsFinite(Key.CurveTime) || !FMath::IsFinite(Key.ForwardFraction))
		{
			return false;
		}
		if (Index == 0)
		{
			continue;
		}
		const FVaultClipDriftKey& Previous = DriftKeys[Index - 1];
		if (Key.CurveTime <= Previous.CurveTime
			|| Key.ForwardFraction < Previous.ForwardFraction)
		{
			return false;
		}
		if (Key.CurveTime >= 1.0f
			&& !FMath::IsNearlyEqual(Key.ForwardFraction, Previous.ForwardFraction))
		{
			// A non-zero terminal slope would make the capsule's real exit
			// velocity differ from the analytic tangent the free fall launches
			// with -- the exact class of bug ComputeTrajectoryTangent exists to
			// prevent.
			return false;
		}
	}

	for (int32 Index = 1; Index < DriftKeys.Num(); ++Index)
	{
		const FVaultClipDriftKey& Previous = DriftKeys[Index - 1];
		const FVaultClipDriftKey& Next = DriftKeys[Index];
		if (CurveTime <= Next.CurveTime)
		{
			const float Span = Next.CurveTime - Previous.CurveTime;
			const float Alpha = Span > KINDA_SMALL_NUMBER
				? (CurveTime - Previous.CurveTime) / Span : 0.0f;
			OutFraction = FMath::Lerp(Previous.ForwardFraction, Next.ForwardFraction,
				FMath::Clamp(Alpha, 0.0f, 1.0f));
			return true;
		}
	}
	return false;
}

UCurveVector* UMHGZPoleVaultAbility::BuildTrajectoryCurve(
	const TArray<FVaultTrajectoryKey>& Keys,
	const TArray<FVaultClipDriftKey>& DriftKeys, const float StartProgress,
	const float FreeFallHandoffProgress, const float TotalDistance,
	const float ApexHeight, const float CurveDuration, float& OutHandoffDistance,
	FVector& OutHandoffVelocityLocal) const
{
	OutHandoffDistance = 0.0f;
	OutHandoffVelocityLocal = FVector::ZeroVector;
	if (Keys.Num() < 2 || TotalDistance <= 0.f || ApexHeight <= 0.f
		|| StartProgress < 0.f || StartProgress >= FreeFallHandoffProgress
		|| FreeFallHandoffProgress <= 0.f || FreeFallHandoffProgress > 1.f
		|| !FMath::IsFinite(CurveDuration) || CurveDuration <= KINDA_SMALL_NUMBER)
	{
		return nullptr;
	}

	UCurveVector* Curve = NewObject<UCurveVector>(const_cast<UMHGZPoleVaultAbility*>(this));
	if (!Curve)
	{
		return nullptr;
	}
	FVector StartPosition;
	FVector HandoffPosition;
	if (!SampleTrajectoryPosition(Keys, StartProgress, StartPosition)
		|| !SampleTrajectoryPosition(Keys, FreeFallHandoffProgress, HandoffPosition))
	{
		return nullptr;
	}
	const FVector CurveDisplacement = HandoffPosition - StartPosition;
	OutHandoffDistance = CurveDisplacement.X * TotalDistance;
	if (OutHandoffDistance <= KINDA_SMALL_NUMBER)
	{
		return nullptr;
	}

	// The JumpOver clip's root track carries the rest of the forward travel in
	// its pose.  With the sequence locked to the ref pose that lead no longer
	// exists, so the capsule has to take the same distance here or the move
	// lands short by exactly that amount.
	float RecoveredFraction = 0.0f;
	if (!SampleClipDriftForwardFraction(DriftKeys, 1.0f, RecoveredFraction))
	{
		return nullptr;
	}
	OutHandoffDistance += RecoveredFraction * TotalDistance;
	if (OutHandoffDistance <= KINDA_SMALL_NUMBER)
	{
		return nullptr;
	}

	if (!ComputeTrajectoryTangent(Keys, StartProgress, FreeFallHandoffProgress,
		TotalDistance, ApexHeight, CurveDuration, OutHandoffVelocityLocal))
	{
		return nullptr;
	}
	float PreviousTime = -1.0f;
	auto AddTrajectoryKey = [&Curve, &PreviousTime, TotalDistance, ApexHeight,
		&OutHandoffDistance, &StartPosition, &DriftKeys](const float NormalizedTime,
		const FVector& Position)
	{
		if (!FMath::IsFinite(NormalizedTime) || NormalizedTime < 0.f || NormalizedTime > 1.f
			|| NormalizedTime <= PreviousTime || Position.ContainsNaN())
		{
			return false;
		}
		float DriftFraction = 0.0f;
		if (!SampleClipDriftForwardFraction(DriftKeys, NormalizedTime, DriftFraction))
		{
			return false;
		}
		// MoveToForce contributes NormalizedTime * endpoint X.  The curve supplies
		// the exact sampled lateral, vertical and non-linear X remainder, plus the
		// forward distance recovered from the clip's now-locked root track.
		const FVector RelativePosition = Position - StartPosition;
		const float OffsetX = RelativePosition.X * TotalDistance
			+ DriftFraction * TotalDistance
			- NormalizedTime * OutHandoffDistance;
		AddLinearKey(Curve->FloatCurves[0], NormalizedTime, OffsetX);
		AddLinearKey(Curve->FloatCurves[1], NormalizedTime, RelativePosition.Y * TotalDistance);
		AddLinearKey(Curve->FloatCurves[2], NormalizedTime, RelativePosition.Z * ApexHeight);
		PreviousTime = NormalizedTime;
		return true;
	};

	if (!AddTrajectoryKey(0.0f, StartPosition))
	{
		return nullptr;
	}
	for (const FVaultTrajectoryKey& Key : Keys)
	{
		if (Key.Time <= StartProgress)
		{
			continue;
		}
		if (Key.Time >= FreeFallHandoffProgress)
		{
			break;
		}
		const float CurveTime = (Key.Time - StartProgress)
			/ (FreeFallHandoffProgress - StartProgress);
		if (!AddTrajectoryKey(CurveTime, Key.NormalizedPosition))
		{
			return nullptr;
		}
	}
	if (!AddTrajectoryKey(1.0f, HandoffPosition))
	{
		return nullptr;
	}
	return FMath::IsNearlyZero(Keys[0].Time)
		&& FMath::IsNearlyEqual(PreviousTime, 1.0f)
		? Curve : nullptr;
}

void UMHGZPoleVaultAbility::HandleBackVaultVisualCompleted()
{
	// ⚠ 这条**只绑在弧段任务上**（起手段不绑 `OnCompleted`，见 `StartAttackMontage`），
	// 所以要清的是弧段那个指针。清错一个等于留下一个已经被销毁的任务指针。
	BackVaultArcVisualTask = nullptr;
	if (!IsActive() || bIsEndingBackVault)
	{
		return;
	}
	bVisualFinished = true;
	TryFinishBackVault();
}

void UMHGZPoleVaultAbility::HandleBackVaultVisualInterrupted()
{
	// 两条蒙太奇共用这一个中断处理，所以两个指针都要清 —— 只清一个会让另一个
	// 悬着，而它在 `EndAbility` 里还要被 `EndTask`。
	BackVaultVisualTask = nullptr;
	BackVaultArcVisualTask = nullptr;
	if (IsActive() && !bIsEndingBackVault)
	{
		RequestEndAction(EWeaponActionEndReason::Interrupted);
	}
}

void UMHGZPoleVaultAbility::HandleBackVaultMovementFinished(
	const FWeaponMovementResult& MovementResult)
{
	BackVaultMovementTask = nullptr;
	ActiveTrajectoryCurve = nullptr;
	if (!IsActive() || bIsEndingBackVault)
	{
		return;
	}
	// 只吃结束原因 —— 见 UMHGZInsectGlaiveAbility::ResolveVaultExit 的注释。
	//
	// 这里原来传的是 `bHasResidualAir && IsFalling()`，两个 conjunct 都是在回避
	// 同一个错误：旧 FinishMovement 会在广播前无条件切 MOVE_Falling，于是每一次
	// CurvedVault 结束时 IsFalling() 都为真，白灯（残余恰好为 0）也会被误判成
	// 有空中段。改由任务如实上报 Landed / Completed 之后，这两个代理指标连同
	// 承载它们的 BackVaultResidualFallSeconds / AerialFallMinResidualSeconds 一起删除。
	const EVaultExit Exit = ResolveVaultExit(MovementResult.EndReason);
	if (Exit == EVaultExit::None)
	{
		RequestEndAction(EWeaponActionEndReason::Interrupted);
		return;
	}
	bMovementFinished = true;
	// 白灯的弧长恰好等于它的 CurvedVault 窗口（HandoffProgress == 1.0），所以它在
	// 源完成那一帧就够到地面、从不产生自由落体段。任务把这种收尾如实报成 Landed，
	// 于是落地姿势归本次 Action —— 而不是靠 CMC 恰好在 Falling 里待一帧。
	bBeginFreeFallAfterEnd = Exit == EVaultExit::FreeFall;
	bPlayLandedPresentation = Exit == EVaultExit::LandedPresentation;
	// CurvedVault 已经交棒（或已触地）。在这个确定性的交棒点上结束，而不是等视觉
	// 蒙太奇自然播完 —— 视觉实例刻意保持终末姿势，直到 EndAbility 起下一个表现。
	RequestEndAction(EWeaponActionEndReason::Normal);
}

void UMHGZPoleVaultAbility::TryFinishBackVault()
{
	if (bVisualFinished && bMovementFinished && IsActive() && !bIsEndingBackVault)
	{
		RequestEndAction(EWeaponActionEndReason::Normal);
	}
}
