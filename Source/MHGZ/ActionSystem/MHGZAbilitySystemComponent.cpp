// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZAbilitySystemComponent.h"
#include "MHGZGameplayAbility.h"
#include "MHGZAttackAbility.h"
#include "MHGZComboCoordinatorAbility.h"
#include "GameplayEffect.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "Engine/LocalPlayer.h"

UMHGZAbilitySystemComponent::UMHGZAbilitySystemComponent()
{
}

void UMHGZAbilitySystemComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UMHGZAbilitySystemComponent::InitializeAbilitySystem()
{
	if (!bAbilitySystemInitialized)
	{
		// (1) 设置初始 GameplayTag
		AddLooseGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Sheathed")));
		AddLooseGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Grounded")));

		// (2) 授予核心 Ability
		for (const TSubclassOf<UGameplayAbility>& AbilityClass : CoreAbilities)
		{
			if (!AbilityClass) continue;
			GiveAbility(FGameplayAbilitySpec(AbilityClass, 1, INDEX_NONE, this));
		}

		// (3) Apply 核心 GE
		for (const TSubclassOf<UGameplayEffect>& EffectClass : CoreAttributeEffects)
		{
			if (!EffectClass) continue;
			FGameplayEffectContextHandle Context = MakeEffectContext();
			ApplyGameplayEffectToSelf(EffectClass->GetDefaultObject<UGameplayEffect>(), 1.0f, Context);
		}

		bAbilitySystemInitialized = true;
	}

	// (4) 绑定 EnhancedInput → Tag 路由
	// ★ 必须在 InitAbilityActorInfo 之后调用——通过 Avatar 获取 Character→Controller→EnhancedInput
	if (bInputBound) return;  // 防止重复绑定（如复活后重新 PossessedBy）

	AActor* Avatar = GetAvatarActor();
	if (!Avatar) return;

	APawn* Pawn = Cast<APawn>(Avatar);
	if (!Pawn) return;

	APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
	if (!PC) return;

	if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PC->InputComponent))
	{
		for (const FAbilityInputBinding& Binding : InputBindings)
		{
			if (!Binding.InputAction) continue;

			ActionToTag.Add(Binding.InputAction, Binding.AbilityTag);

			EnhancedInput->BindAction(Binding.InputAction.Get(), ETriggerEvent::Started,
				this, &UMHGZAbilitySystemComponent::OnInputActionTriggered);

			EnhancedInput->BindAction(Binding.InputAction.Get(), ETriggerEvent::Completed,
				this, &UMHGZAbilitySystemComponent::OnInputActionCompleted);
		}

		bInputBound = true;
	}
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

UGA_WeaponComboCoordinator* UMHGZAbilitySystemComponent::GetActiveComboCoordinator() const
{
	return ActiveComboCoordinator.Get();
}

void UMHGZAbilitySystemComponent::BindInputAction(UInputAction* Action, FGameplayTag AbilityTag)
{
	// 查找并更新现有绑定，或添加新的
	for (FAbilityInputBinding& Binding : InputBindings)
	{
		if (Binding.AbilityTag == AbilityTag)
		{
			Binding.InputAction = Action;
			return;
		}
	}

	FAbilityInputBinding NewBinding;
	NewBinding.InputAction = Action;
	NewBinding.AbilityTag = AbilityTag;
	InputBindings.Add(NewBinding);
}

// ═══════════════════════════════════════════
// 输入回调
// ═══════════════════════════════════════════

void UMHGZAbilitySystemComponent::OnInputActionTriggered(const FInputActionInstance& Instance)
{
	const UInputAction* SourceAction = Instance.GetSourceAction();
	const FGameplayTag* Tag = ActionToTag.Find(SourceAction);
	if (!Tag) return;

	// 武器 Tag → 转发给协调器
	if (Tag->MatchesTag(FGameplayTag::RequestGameplayTag(TEXT("Input.Weapon"))))
	{
		if (ActiveComboCoordinator != nullptr)
		{
			ActiveComboCoordinator->HandleWeaponInput(*Tag);
		}
	}
	else
	{
		// 非武器 Tag → 标准 GAS 路径
		TryActivateAbilitiesByTag(FGameplayTagContainer(*Tag));
	}
}

void UMHGZAbilitySystemComponent::OnInputActionCompleted(const FInputActionInstance& Instance)
{
	// 检查是否处于蓄力态
	if (HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Charging"))))
	{
		FGameplayEventData EventData;
		EventData.Instigator = GetOwnerActor();
		EventData.Target = GetOwnerActor();
		EventData.EventTag = FGameplayTag::RequestGameplayTag(TEXT("Combat.Event.ChargeReleased"));

		HandleGameplayEvent(
			FGameplayTag::RequestGameplayTag(TEXT("Combat.Event.ChargeReleased")),
			&EventData);
	}
	// 若无 Charging Tag，静默跳过
}
