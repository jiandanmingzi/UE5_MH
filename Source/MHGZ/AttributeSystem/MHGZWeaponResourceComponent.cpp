// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZWeaponResourceComponent.h"
#include "AbilitySystemComponent.h"
#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "MHGZPlayerState.h"
#include "Kismet/GameplayStatics.h"

UMHGZWeaponResourceComponent::UMHGZWeaponResourceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

UAbilitySystemComponent* UMHGZWeaponResourceComponent::GetPlayerASC() const
{
	const AActor* Owner = GetOwner();
	if (const AMHGZPlayerState* PS = Cast<AMHGZPlayerState>(Owner))
	{
		return PS->GetMHGZAbilitySystemComponent();
	}
	return nullptr;
}

void UMHGZWeaponResourceComponent::ApplyEntryModifier(FGameplayTag AttributeTag, float Value, TEnumAsByte<EGameplayModOp::Type> Op)
{
	// 前缀校验——仅处理 WeaponResource.* 标签
	if (!AttributeTag.MatchesTag(FGameplayTag::RequestGameplayTag(TEXT("WeaponResource"))))
	{
		UE_LOG(LogTemp, Warning, TEXT("[WeaponResource] ApplyEntryModifier: Tag %s 不在 WeaponResource 命名空间下，跳过"),
			*AttributeTag.ToString());
		return;
	}

	FActiveModifier& Mod = ActiveModifiers.FindOrAdd(AttributeTag);
	Mod.AttributeTag = AttributeTag;
	Mod.Value = Value;
	Mod.Op = Op;
}

void UMHGZWeaponResourceComponent::ClearAllEntryModifiers()
{
	ActiveModifiers.Empty();
}

float UMHGZWeaponResourceComponent::GetModifiedParam(FName ParamName) const
{
	const float* Base = BaseParams.Find(ParamName);
	float Result = Base ? *Base : 1.0f;

	// 遍历所有修饰器，累积倍率
	for (const auto& Pair : ActiveModifiers)
	{
		const FActiveModifier& Mod = Pair.Value;
		if (Mod.Op == EGameplayModOp::Multiplicitive)
		{
			Result *= Mod.Value;
		}
		else if (Mod.Op == EGameplayModOp::Additive)
		{
			Result += Mod.Value;
		}
	}

	return Result;
}

void UMHGZWeaponResourceComponent::PlayResourceSound(USoundBase* Sound)
{
	if (Sound)
	{
		UGameplayStatics::PlaySound2D(this, Sound);
	}
}
