// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZComboCoordinatorAbility.h"
#include "MHGZWeaponComboData.h"
#include "MHGZAbilitySystemComponent.h"
#include "MHGZAttackAbility.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "AbilitySystemGlobals.h"

UGA_WeaponComboCoordinator::UGA_WeaponComboCoordinator()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalOnly;
	bIsContinuous = true; // 协调器是持续型 Ability——整个装备期间保持 Active
	CurrentState = FName(TEXT("Idle"));
}

void UGA_WeaponComboCoordinator::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	// 将自己注册到 ASC
	if (UMHGZAbilitySystemComponent* MHGZASC = Cast<UMHGZAbilitySystemComponent>(
		ActorInfo->AbilitySystemComponent.Get()))
	{
		// 使用 mutable 方式设置（friend class 已声明）
		MHGZASC->SetActiveComboCoordinator(this);
	}

	ResetComboTimeout();
}

void UGA_WeaponComboCoordinator::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	// 从 ASC 注销
	if (UMHGZAbilitySystemComponent* MHGZASC = Cast<UMHGZAbilitySystemComponent>(
		ActorInfo->AbilitySystemComponent.Get()))
	{
		MHGZASC->SetActiveComboCoordinator(nullptr);
	}

	AActor* Owner = GetOwningActorFromActorInfo();
	if (Owner)
	{
		Owner->GetWorldTimerManager().ClearTimer(ComboTimeoutTimer);
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_WeaponComboCoordinator::InjectComboData(UMHGZWeaponComboData* Data)
{
	ComboData = Data;
	BuildStateIndex();
	ResetComboTimeout();
}

void UGA_WeaponComboCoordinator::ResetComboTimeout()
{
	AActor* Owner = GetOwningActorFromActorInfo();
	if (!Owner) return;

	Owner->GetWorldTimerManager().ClearTimer(ComboTimeoutTimer);
	if (!ComboData || ComboData->GlobalComboTimeout <= 0.f) return;

	Owner->GetWorldTimerManager().SetTimer(
		ComboTimeoutTimer, [this]()
		{
			CurrentState = FName(TEXT("Idle"));
			PendingGrantedTags.Reset();
		},
		ComboData->GlobalComboTimeout, false);
}

void UGA_WeaponComboCoordinator::BuildStateIndex()
{
	StateIndex.Empty();
	if (!ComboData) return;

	for (int32 i = 0; i < ComboData->Transitions.Num(); ++i)
	{
		const FComboTransition& Transition = ComboData->Transitions[i];
		if (!Transition.bMatchAnyState)
		{
			StateIndex.FindOrAdd(Transition.SourceState).Add(i);
		}
	}
}

bool UGA_WeaponComboCoordinator::DoesTransitionMatchState(const FComboTransition& Transition) const
{
	if (Transition.bMatchAnyState)
	{
		if (Transition.BlockedSourceStates.Contains(CurrentState))
		{
			return false;
		}
		return true;
	}
	return Transition.SourceState == CurrentState;
}

const FComboTransition* UGA_WeaponComboCoordinator::FindBestMatch(
	const TArray<const FComboTransition*>& Candidates) const
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC) return nullptr;

	const FComboTransition* Best = nullptr;
	int32 BestPriority = INT32_MIN;

	for (const FComboTransition* Transition : Candidates)
	{
		// 检查 RequiredTags
		if (!Transition->RequiredTags.IsEmpty() && !ASC->HasAllMatchingGameplayTags(Transition->RequiredTags))
		{
			continue;
		}

		// 检查 BlockedTags
		if (ASC->HasAnyMatchingGameplayTags(Transition->BlockedTags))
		{
			continue;
		}

		// 检查耐力门槛
		if (Transition->StaminaRequired > 0.f)
		{
			const UMHGZAttributeSet* AttrSet = ASC->GetSet<UMHGZAttributeSet>();
			if (AttrSet && AttrSet->GetStamina() < Transition->StaminaRequired)
			{
				continue;
			}
		}

		if (Transition->Priority > BestPriority)
		{
			BestPriority = Transition->Priority;
			Best = Transition;
		}
	}

	return Best;
}

void UGA_WeaponComboCoordinator::HandleWeaponInput(FGameplayTag InInputTag)
{
	if (!ComboData) return;

	TArray<const FComboTransition*> Candidates;

	// 从 StateIndex 获取候选节点
	if (const TArray<int32>* Indices = StateIndex.Find(CurrentState))
	{
		for (int32 idx : *Indices)
		{
			const FComboTransition& Transition = ComboData->Transitions[idx];
			if (Transition.InputTag == InInputTag)
			{
				Candidates.Add(&Transition);
			}
		}
	}

	// bMatchAnyState 节点
	for (const FComboTransition& Transition : ComboData->Transitions)
	{
		if (Transition.bMatchAnyState && Transition.InputTag == InInputTag
			&& DoesTransitionMatchState(Transition))
		{
			Candidates.Add(&Transition);
		}
	}

	// 四级排序取最佳匹配
	const FComboTransition* Best = FindBestMatch(Candidates);
	if (!Best) return;

	// 激活 Ability
	if (Best->AbilityClass)
	{
		UMHGZAbilitySystemComponent* ASC = Cast<UMHGZAbilitySystemComponent>(
			GetAbilitySystemComponentFromActorInfo());
		if (!ASC) return;

		// 激活装备时已授予的 Spec，避免每次按键重复 GiveAbility。
		const FGameplayAbilitySpecHandle AbilityHandle =
			ASC->FindWeaponAbilityHandle(Best->AbilityClass);
		if (!AbilityHandle.IsValid() || !ASC->TryActivateAbility(AbilityHandle))
		{
			return;
		}
		ActiveAttackHandle = AbilityHandle;

		// 更新状态
		if (Best->StatePolicy == EComboStatePolicy::Replace && !Best->TargetState.IsNone())
		{
			PreviousState = CurrentState;
			CurrentState = Best->TargetState;
		}

		// M0 兼容路径保持旧实现的首次命中授予语义，避免在 TagLedger 接入前制造无所有者 Loose Tag。
		// M1 的统一 ExecuteTransition 会按 GrantTiming 分流 OnActivation / OnFirstHit。
		if (!Best->GrantedTags.IsEmpty())
		{
			PendingGrantedTags = Best->GrantedTags;
		}

		ResetComboTimeout();
	}
}

void UGA_WeaponComboCoordinator::OnAttackFinished()
{
	ActiveAttackHandle = FGameplayAbilitySpecHandle();
	// 兜底——若当前 State 不在 StateIndex 中，回 Idle
	if (!StateIndex.Contains(CurrentState) && CurrentState != FName(TEXT("Idle")))
	{
		CurrentState = FName(TEXT("Idle"));
	}
}

void UGA_WeaponComboCoordinator::OnAttackHit()
{
	// 授予 PendingGrantedTags
	if (!PendingGrantedTags.IsEmpty())
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			for (const FGameplayTag& Tag : PendingGrantedTags)
			{
				ASC->AddLooseGameplayTag(Tag);
			}
		}
		PendingGrantedTags.Reset();
	}
}

void UGA_WeaponComboCoordinator::OnLanded()
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC) return;

	// 移除所有空中相关 Tag
	ASC->RemoveLooseGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aerial")));
	ASC->RemoveLooseGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aerial.Falling")));
	ASC->RemoveLooseGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aerial.CantDodge")));
	ASC->RemoveLooseGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aerial.CantAttack")));

	// 添加地面 Tag
	ASC->AddLooseGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Grounded")));

	// 重置协调器状态
	CurrentState = FName(TEXT("Idle"));
	PendingGrantedTags.Reset();
}
