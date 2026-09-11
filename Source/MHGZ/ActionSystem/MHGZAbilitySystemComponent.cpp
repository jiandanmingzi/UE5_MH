// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZAbilitySystemComponent.h"

#include "MHGZComboCoordinatorAbility.h"
#include "MHGZGameplayAbility.h"
#include "MHGZHitReactionAbility.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"
#include "GameplayEffect.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"

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

	// HitReaction is event infrastructure rather than player input.  Ensure it
	// exists even on old BP_PlayerState assets that serialized CoreAbilities
	// before this core ability was introduced.  A future BP child in the array
	// supersedes the native default instead of producing two event listeners.
	const bool bHasConfiguredHitReaction = CoreAbilities.ContainsByPredicate(
		[](const TSubclassOf<UGameplayAbility>& AbilityClass)
		{
			return AbilityClass && AbilityClass->IsChildOf(
				UMHGZHitReactionAbility::StaticClass());
		});
	if (!bHasConfiguredHitReaction)
	{
		GiveAbility(FGameplayAbilitySpec(UMHGZHitReactionAbility::StaticClass(),
			1, INDEX_NONE, this));
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

	// 旧武器的一次性激活上下文全部作废，防止换武器后被新激活误消费。
	PendingActivationContexts.Empty();
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

	if (!TryActivateDirectInput(Snapshot))
	{
		if (UGA_WeaponComboCoordinator* Coordinator = GetActiveComboCoordinator())
		{
			Coordinator->TryBufferDirectInput(Snapshot);
		}
	}
}

bool UMHGZAbilitySystemComponent::TryActivateDirectInput(
	const FWeaponInputSnapshot& Snapshot)
{
	if (!Snapshot.ResolvedInputTag.IsValid() || Snapshot.ResolvedInputTag.MatchesTag(
		FGameplayTag::RequestGameplayTag(TEXT("Input.Weapon"))))
	{
		return false;
	}

	// 一般输入 → 按 InputTag 精确匹配。
	const FGameplayAbilitySpecHandle Handle = FindAbilityHandleByInputTag(Snapshot.ResolvedInputTag);
	if (!Handle.IsValid())
	{
		return false;
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
		return false;
	}
	return true;
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

float UMHGZAbilitySystemComponent::PlayMontageWithBlendIn(
	UGameplayAbility* AnimatingAbility,
	FGameplayAbilityActivationInfo ActivationInfo, UAnimMontage* Montage,
	float PlayRate, FName StartSectionName, float StartTimeSeconds,
	float BlendInTime)
{
	(void)ActivationInfo;
	UAnimInstance* AnimInstance = AbilityActorInfo.IsValid()
		? AbilityActorInfo->GetAnimInstance()
		: nullptr;
	if (!AnimInstance || !Montage)
	{
		return -1.0f;
	}

	// Start from the Montage's authored alpha curve/options, overriding only the
	// duration for this instance. Montage_PlayWithBlendIn preserves the asset's
	// BlendMode and BlendProfile.
	FAlphaBlendArgs BlendIn = Montage->BlendIn;
	BlendIn.BlendTime = FMath::Max(0.0f, BlendInTime);
	const float Duration = AnimInstance->Montage_PlayWithBlendIn(Montage, BlendIn,
		PlayRate, EMontagePlayReturnType::MontageLength, StartTimeSeconds, true);
	if (Duration <= 0.0f)
	{
		return Duration;
	}

	LocalAnimMontageInfo.AnimMontage = Montage;
	LocalAnimMontageInfo.AnimatingAbility = AnimatingAbility;
	LocalAnimMontageInfo.PlayInstanceId = LocalAnimMontageInfo.PlayInstanceId < UINT8_MAX
		? LocalAnimMontageInfo.PlayInstanceId + 1
		: 0;
	if (AnimatingAbility)
	{
		AnimatingAbility->SetCurrentMontage(Montage);
	}
	if (!StartSectionName.IsNone())
	{
		AnimInstance->Montage_JumpToSection(StartSectionName, Montage);
	}
	return Duration;
}
