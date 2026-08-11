// Copyright MHGZ Project. All Rights Reserved.

#include "AbilityTask_MHGZStaminaDrain.h"

#include "MHGZAbilityCostGameplayEffects.h"
#include "MHGZGameplayAbility.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "AbilitySystemComponent.h"

UAbilityTask_MHGZStaminaDrain::UAbilityTask_MHGZStaminaDrain(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UAbilityTask_MHGZStaminaDrain* UAbilityTask_MHGZStaminaDrain::StartStaminaDrain(
	UGameplayAbility* OwningAbility)
{
	UAbilityTask_MHGZStaminaDrain* Task =
		NewAbilityTask<UAbilityTask_MHGZStaminaDrain>(OwningAbility, TEXT("MHGZStaminaDrain"));
	Task->OwnerAbility = Cast<UMHGZGameplayAbility>(OwningAbility);
	Task->ReadyForActivation();
	return Task;
}

void UAbilityTask_MHGZStaminaDrain::Activate()
{
	Super::Activate();

	if (!OwnerAbility.IsValid())
	{
		EndTask();
		return;
	}

	if (UWorld* World = GetWorld())
	{
		LastTickTimeSeconds = World->GetTimeSeconds();
		World->GetTimerManager().SetTimer(
			DrainTimerHandle,
			this,
			&UAbilityTask_MHGZStaminaDrain::OnDrainTick,
			0.1f,
			true);
	}
	else
	{
		OwnerAbility->RequestEndAction(EWeaponActionEndReason::Cancelled);
	}
}

void UAbilityTask_MHGZStaminaDrain::OnDestroy(bool bInOwnerFinished)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DrainTimerHandle);
	}

	Super::OnDestroy(bInOwnerFinished);
}

void UAbilityTask_MHGZStaminaDrain::OnDrainTick()
{
	UMHGZGameplayAbility* MHGZAbility = OwnerAbility.Get();
	UWorld* World = GetWorld();
	if (!MHGZAbility || !World)
	{
		EndTask();
		return;
	}

	UAbilitySystemComponent* ASC = MHGZAbility->GetAbilitySystemComponentFromActorInfo();
	if (!ASC)
	{
		MHGZAbility->RequestEndAction(EWeaponActionEndReason::Cancelled);
		return;
	}

	const UMHGZAttributeSet* AttrSet = ASC->GetSet<UMHGZAttributeSet>();
	if (!AttrSet)
	{
		MHGZAbility->RequestEndAction(EWeaponActionEndReason::Cancelled);
		return;
	}

	// 按真实经过时间结算（Timer 仅为 0.1s 驱动）。
	const double NowSeconds = World->GetTimeSeconds();
	const float Elapsed = FMath::Clamp(
		static_cast<float>(NowSeconds - LastTickTimeSeconds),
		0.f,
		1.f);
	LastTickTimeSeconds = NowSeconds;

	const float Rate = MHGZAbility->StaminaCostRate.GetValueAtLevel(MHGZAbility->GetAbilityLevel());
	const float Payment = Rate * AttrSet->GetStaminaConsumptionRate() * Elapsed;
	if (Payment <= 0.f)
	{
		return; // 未配置成本或速率为 0：无需扣减。
	}

	// 下一笔付款不足 → 取消 Action（EndTask 由所属 GA 的 EndAbility 幂等执行）。
	if (AttrSet->GetStamina() < Payment)
	{
		MHGZAbility->RequestEndAction(EWeaponActionEndReason::Cancelled);
		return;
	}

	FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(
		UMHGZStaminaCostGameplayEffect::StaticClass(),
		MHGZAbility->GetAbilityLevel(),
		ASC->MakeEffectContext());
	if (Spec.IsValid())
	{
		Spec.Data->SetSetByCallerMagnitude(
			UMHGZStaminaCostGameplayEffect::GetStaminaCostSetByCallerTag(),
			-Payment);
		ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
	}
}
