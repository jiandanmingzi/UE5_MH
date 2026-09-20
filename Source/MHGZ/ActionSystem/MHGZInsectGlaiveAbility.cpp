// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZInsectGlaiveAbility.h"
#include "ActionSystem/AnimNotify_IG_AerialHandoff.h"
#include "Animation/AnimMontage.h"
#include "AttributeSystem/Res_InsectGlaive.h"
#include "MHGZAbilitySystemComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameplayEffect.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"
#include "MHGZ.h"

UMHGZInsectGlaiveAbility::UMHGZInsectGlaiveAbility()
{
}

URes_InsectGlaive* UMHGZInsectGlaiveAbility::GetIGResourceComponent() const
{
	const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC) return nullptr;

	// M2：资源查找统一走 RuntimeHost 的 ResourceProvider，不再扫描 PlayerState 组件。
	const UMHGZAbilitySystemComponent* MHGZASC = Cast<UMHGZAbilitySystemComponent>(ASC);
	if (!MHGZASC) return nullptr;

	const UMHGZWeaponRuntimeHostComponent* Host = MHGZASC->GetRuntimeHost();
	if (!Host) return nullptr;

	return Cast<URes_InsectGlaive>(Host->GetResourceProvider());
}

bool UMHGZInsectGlaiveAbility::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	return true;
}

void UMHGZInsectGlaiveAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// 父类（UMHGZAttackAbility）：扣耐力、方向修正、播 Montage
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!IsActionActivationCommitted()) return;

	// 每次激活都重置最早可操作帧的锁存。DetectAerialHandoffNotify 随后会按
	// 本次蒙太奇重新推导 authored，但锁存必须在这里先清干净 —— 否则上一次
	// 激活残留的 true 会让 exit 判据以为本次已经越过释放点。
	bAerialHandoffReached = false;
	bAerialHandoffAuthored = false;

	// 三灯攻击音效——每个攻击 GA 激活时播放（无论是否命中）
	if (TripleUpSwingSound)
	{
		const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
		if (ASC && ASC->HasMatchingGameplayTag(
			FGameplayTag::RequestGameplayTag(TEXT("Combat.Branch.TripleUp"))))
		{
			UGameplayStatics::PlaySound2D(this, TripleUpSwingSound);
		}
	}
}

bool UMHGZInsectGlaiveAbility::CheckExtractRequirement(FGameplayTag ExtractColor) const
{
	if (URes_InsectGlaive* RC = GetIGResourceComponent())
	{
		return RC->HasExtract(ExtractColor);
	}
	return false;
}

bool UMHGZInsectGlaiveAbility::ConsumeExtractAndApplyBurst(
	FGameplayTag ExtractType, TSubclassOf<UGameplayEffect> BurstGE)
{
	URes_InsectGlaive* RC = GetIGResourceComponent();
	if (!RC || !RC->HasExtract(ExtractType)) return false;

	// 消耗灯
	RC->ConsumeExtract(ExtractType);

	// Apply 爆发 Buff
	if (BurstGE)
	{
		UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
		if (ASC)
		{
			ASC->ApplyGameplayEffectToSelf(
				BurstGE->GetDefaultObject<UGameplayEffect>(),
				1.0f, ASC->MakeEffectContext());
		}
	}
	return true;
}

UMHGZInsectGlaiveAbility::EVaultExit UMHGZInsectGlaiveAbility::ResolveVaultExit(
	const EWeaponMovementEndReason Reason)
{
	switch (Reason)
	{
	case EWeaponMovementEndReason::Landed:
		// 胶囊触地了。落地姿势归本次 Action。
		return EVaultExit::LandedPresentation;
	case EWeaponMovementEndReason::Completed:
	case EWeaponMovementEndReason::BlockingHit:
		// 弧在空中跑完、且全程没有触地 —— 本体交给 CMC 继续积分。
		//
		// BlockingHit 归到这一支：胶囊撞到东西时任务不再中断移动
		// （见 UAbilityTask_MHGZWeaponMovement::HandleCapsuleHit），所以这一支
		// 目前不可达，保留是为了不让「被打断」重新变成「身体无主」——
		// 那正是舞踏擦到木桩后带着 1309 cm/s 飞到 971 cm 时的状态。
		return EVaultExit::FreeFall;
	default:
		// Interrupted / Cancelled / Failed / Death / WeaponChanged /
		// RuntimeShutdown / HitHitzone：本次 Action 正在被拆除，交棒无意义。
		return EVaultExit::None;
	}
}

namespace
{
const FGameplayTag& AerialActionableTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Aerial.Actionable"));
	return Tag;
}
}

bool UMHGZInsectGlaiveAbility::NotifyAerialHandoff()
{
	// 动作已经拆到一半：什么都不做。胶囊不会被丢在 MOVE_Flying ——
	// UAbilityTask_MHGZWeaponMovement::EnsureVaultFlightReleased() 从
	// FinishMovement 与 OnDestroy 两条路都兜住了。
	if (!IsActive() || !IsActionActivationCommitted())
	{
		return false;
	}
	if (bAerialHandoffReached)
	{
		// 点通知跨过两次（重入蒙太奇、循环段）不是错误，但只算一次。
		return true;
	}
	// 先锁存再动模式：exit 判据读的是这个标志，它必须与模式切换无关地成立。
	bAerialHandoffReached = true;

	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	const FWeaponActionToken& ActionToken = GetActionToken();
	if (Host && ActionToken.IsValid() && Host->IsTokenCurrent(ActionToken.RuntimeToken))
	{
		FGameplayTagContainer Tags;
		Tags.AddTag(AerialActionableTag());
		AerialHandoffTag = Host->AcquireTags(EWeaponTagOwnerKind::NotifyWindow,
			ActionToken.AbilityHandle, ActionToken.ActivationSequenceID,
			FName(TEXT("AerialHandoff")), Tags);
	}

	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	UCharacterMovementComponent* CMC = Character ? Character->GetCharacterMovement() : nullptr;
	if (!CMC || CMC->MovementMode != MOVE_Flying)
	{
		return true;
	}

	// 显式 MOVE_Falling，不用 SetDefaultMovementMode()：释放按构造发生在半空
	// （舞踏 0.316 / 1.6167，后撑杆跳 0.783 / 1.7333），而 SetDefaultMovementMode
	// 会去查 CurrentFloor —— 那个 floor 已被 SetMovementMode(MOVE_Flying) 清掉了。
	// 这里要表达的正是「CMC 接管」这个事实本身。
	CMC->SetMovementMode(MOVE_Falling);
	return true;
}

void UMHGZInsectGlaiveAbility::CloseAerialHandoff()
{
	if (!AerialHandoffTag.IsValid())
	{
		return;
	}
	if (UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost())
	{
		Host->ReleaseTags(AerialHandoffTag);
	}
	AerialHandoffTag = FWeaponOwnedTagToken();
}

void UMHGZInsectGlaiveAbility::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const bool bReplicateEndAbility,
	const bool bWasCancelled)
{
	// 在 Super::EndAbility 之前释放：父类会拆 Ledger 与 Action 注册，
	// 那之后 Host 上按本次 Action 记账的 tag 就找不到主人了。
	CloseAerialHandoff();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

bool UMHGZInsectGlaiveAbility::MontageHasAerialHandoffNotify(const UAnimMontage* Montage)
{
	if (!Montage)
	{
		return false;
	}
	for (const FAnimNotifyEvent& Notify : Montage->Notifies)
	{
		// 点通知挂在 Notify（不是 NotifyStateClass）上；轨道无关 ——
		// 命令列把它写在专用轨道上，但查询不该依赖这个约定。
		if (Notify.Notify && Notify.Notify->IsA(UAnimNotify_IG_AerialHandoff::StaticClass()))
		{
			return true;
		}
	}
	return false;
}

void UMHGZInsectGlaiveAbility::DetectAerialHandoffNotify(const UAnimMontage* Montage)
{
	bAerialHandoffAuthored = false;
	bAerialHandoffReached = false;
	if (!Montage)
	{
		return;
	}
	if (MontageHasAerialHandoffNotify(Montage))
	{
		bAerialHandoffAuthored = true;
		return;
	}
	UE_LOG(LogMHGZ, Warning,
		TEXT("[AerialHandoff] %s carries no IG_AerialHandoff notify: nothing hands the capsule back to the CMC mid-arc, so it stays MOVE_Flying until the arc completes and any ground contact during the arc is invisible. Legitimate for a montage that never leaves the ground; a defect for a vault."),
		*Montage->GetPathName());
}
