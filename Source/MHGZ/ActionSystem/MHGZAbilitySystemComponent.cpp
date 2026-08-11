// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZAbilitySystemComponent.h"

#include "MHGZComboCoordinatorAbility.h"
#include "MHGZGameplayAbility.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"
#include "GameplayEffect.h"

UMHGZAbilitySystemComponent::UMHGZAbilitySystemComponent()
{
}

void UMHGZAbilitySystemComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UMHGZAbilitySystemComponent::InitializeAbilitySystem()
{
	if (bAbilitySystemInitialized)
	{
		return;
	}

	// 仅授予核心能力并 Apply 核心 GE；幂等。
	for (const TSubclassOf<UGameplayAbility>& AbilityClass : CoreAbilities)
	{
		if (!AbilityClass) continue;
		GiveAbility(FGameplayAbilitySpec(AbilityClass, 1, INDEX_NONE, this));
	}

	for (const TSubclassOf<UGameplayEffect>& EffectClass : CoreAttributeEffects)
	{
		if (!EffectClass) continue;
		FGameplayEffectContextHandle Context = MakeEffectContext();
		ApplyGameplayEffectToSelf(EffectClass->GetDefaultObject<UGameplayEffect>(), 1.0f, Context);
	}

	bAbilitySystemInitialized = true;
}

void UMHGZAbilitySystemComponent::GrantWeaponAbilities(const TArray<TSubclassOf<UGameplayAbility>>& Abilities)
{
	RemoveWeaponAbilities();

	for (const TSubclassOf<UGameplayAbility>& AbilityClass : Abilities)
	{
		if (!AbilityClass) continue;
		FGameplayAbilitySpecHandle Handle = GiveAbility(
			FGameplayAbilitySpec(AbilityClass, 1, INDEX_NONE, this));
		WeaponAbilityHandles.Add(Handle);
	}
}

void UMHGZAbilitySystemComponent::RemoveWeaponAbilities()
{
	if (ActiveComboCoordinator)
	{
		ActiveComboCoordinator->K2_CancelAbility();
		ActiveComboCoordinator = nullptr;
	}

	for (const FGameplayAbilitySpecHandle& Handle : WeaponAbilityHandles)
	{
		ClearAbility(Handle);
	}
	WeaponAbilityHandles.Empty();
}

FGameplayAbilitySpecHandle UMHGZAbilitySystemComponent::FindWeaponAbilityHandle(
	TSubclassOf<UGameplayAbility> AbilityClass)
{
	if (!AbilityClass) return FGameplayAbilitySpecHandle();
	if (FGameplayAbilitySpec* Spec = FindAbilitySpecFromClass(AbilityClass))
	{
		return Spec->Handle;
	}
	return FGameplayAbilitySpecHandle();
}

FGameplayAbilitySpecHandle UMHGZAbilitySystemComponent::FindAbilityHandleByInputTag(
	const FGameplayTag& InputTag) const
{
	if (!InputTag.IsValid())
	{
		return FGameplayAbilitySpecHandle();
	}

	for (const FGameplayAbilitySpec& Spec : GetActivatableAbilities())
	{
		if (!Spec.Ability)
		{
			continue;
		}
		const UMHGZGameplayAbility* Ability = Cast<UMHGZGameplayAbility>(Spec.Ability);
		if (Ability && Ability->InputTag == InputTag)
		{
			return Spec.Handle;
		}
	}
	return FGameplayAbilitySpecHandle();
}

UGA_WeaponComboCoordinator* UMHGZAbilitySystemComponent::GetActiveComboCoordinator() const
{
	return ActiveComboCoordinator.Get();
}

void UMHGZAbilitySystemComponent::SetRuntimeHost(UMHGZWeaponRuntimeHostComponent* InHost)
{
	RuntimeHost = InHost;
}

UMHGZWeaponRuntimeHostComponent* UMHGZAbilitySystemComponent::GetRuntimeHost() const
{
	return RuntimeHost.Get();
}

void UMHGZAbilitySystemComponent::HandleResolvedInputSnapshot(const FWeaponInputSnapshot& Snapshot)
{
	if (!Snapshot.ResolvedInputTag.IsValid())
	{
		return;
	}

	// 武器输入 → 协调器（协调器负责匹配连招表并激活攻击 GA）。
	if (Snapshot.ResolvedInputTag.MatchesTag(
		FGameplayTag::RequestGameplayTag(TEXT("Input.Weapon"))))
	{
		if (UGA_WeaponComboCoordinator* Coordinator = GetActiveComboCoordinator())
		{
			Coordinator->HandleWeaponInput(Snapshot);
		}
		return;
	}

	// 一般输入 → 按 InputTag 精确匹配。
	const FGameplayAbilitySpecHandle Handle = FindAbilityHandleByInputTag(Snapshot.ResolvedInputTag);
	if (!Handle.IsValid())
	{
		return;
	}

	FWeaponAbilityActivationContext Context;
	if (UMHGZWeaponRuntimeHostComponent* Host = RuntimeHost.Get())
	{
		Context.RuntimeToken = Host->GetCurrentToken();
		Context.ActivationSequenceID = Host->AllocateActivationSequenceID();
	}
	Context.TransitionID = NAME_None;
	Context.SourceState = NAME_None;
	Context.TargetState = NAME_None;
	Context.Input = Snapshot;

	// 注册一次性上下文后同步激活；失败则丢弃，避免陈旧上下文被后续激活消费。
	PrepareWeaponAbilityActivation(Handle, Context);
	if (!TryActivateAbility(Handle))
	{
		ConsumePendingActivationContext(Handle, Context);
	}
}

void UMHGZAbilitySystemComponent::HandleResolvedInputRelease(const FWeaponInputSnapshot& Snapshot)
{
	if (UMHGZWeaponRuntimeHostComponent* Host = RuntimeHost.Get())
	{
		Host->DispatchInputRelease(Snapshot);
	}
}

void UMHGZAbilitySystemComponent::PrepareWeaponAbilityActivation(
	const FGameplayAbilitySpecHandle& Handle,
	const FWeaponAbilityActivationContext& Context)
{
	PendingActivationContexts.Add(Handle, Context);
}

bool UMHGZAbilitySystemComponent::ConsumePendingActivationContext(
	const FGameplayAbilitySpecHandle& Handle,
	FWeaponAbilityActivationContext& OutContext)
{
	return PendingActivationContexts.RemoveAndCopyValue(Handle, OutContext);
}
