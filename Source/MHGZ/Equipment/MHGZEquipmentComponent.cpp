// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZEquipmentComponent.h"
#include "MHGZEquipmentInstance.h"
#include "MHGZEquipmentDefinition.h"
#include "MHGZPlayerState.h"
#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "Inventory/MHGZItemTypes.h"
#include "AbilitySystemComponent.h"

UMHGZEquipmentComponent::UMHGZEquipmentComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UMHGZEquipmentComponent::BeginPlay()
{
	Super::BeginPlay();
}

UAbilitySystemComponent* UMHGZEquipmentComponent::GetPlayerASC() const
{
	const AActor* Owner = GetOwner();
	if (const AMHGZPlayerState* PS = Cast<AMHGZPlayerState>(Owner))
	{
		return PS->GetMHGZAbilitySystemComponent();
	}
	return nullptr;
}

void UMHGZEquipmentComponent::EquipItem(FGameplayTag SlotTag, UMHGZEquipmentInstance* Item)
{
	if (!Item) return;

	// 若槽位已有装备，先卸下旧装备
	if (const TObjectPtr<UMHGZEquipmentInstance>* OldItem = EquippedItems.Find(SlotTag))
	{
		if (*OldItem)
		{
			(*OldItem)->SetStatus(EEquipmentStatus::InStorage);
		}
	}

	Item->SetStatus(EEquipmentStatus::Equipped);
	EquippedItems.Add(SlotTag, Item);
	OnEquipmentChangedInternal();
}

void UMHGZEquipmentComponent::UnequipItem(FGameplayTag SlotTag)
{
	if (const TObjectPtr<UMHGZEquipmentInstance>* Item = EquippedItems.Find(SlotTag))
	{
		if (*Item)
		{
			(*Item)->SetStatus(EEquipmentStatus::InStorage);
		}
		EquippedItems.Remove(SlotTag);
		OnEquipmentChangedInternal();
	}
}

UMHGZEquipmentInstance* UMHGZEquipmentComponent::GetEquippedItem(FGameplayTag SlotTag) const
{
	if (const TObjectPtr<UMHGZEquipmentInstance>* Found = EquippedItems.Find(SlotTag))
	{
		return Found->Get();
	}
	return nullptr;
}

void UMHGZEquipmentComponent::SocketAccessory(UMHGZEquipmentInstance* HostItem,
	UMHGZEquipmentInstance* Accessory, FName SocketName)
{
	if (!HostItem || !Accessory) return;
	HostItem->SocketAccessory(Accessory, SocketName);
	OnEquipmentChangedInternal();
}

void UMHGZEquipmentComponent::RemoveAccessory(UMHGZEquipmentInstance* HostItem, FName SocketName)
{
	if (!HostItem) return;
	HostItem->RemoveAccessory(SocketName);
	OnEquipmentChangedInternal();
}

void UMHGZEquipmentComponent::OnEquipmentChangedInternal()
{
	// 1) 解析新武器槽快照；仅在 EquipmentInstance / RuntimeDefinition 身份真实变化时
	//    递增 WeaponRevision 并广播 WeaponChanged。重复同武器、护甲/饰品/镶嵌均为 no-op。
	FEquippedWeaponSnapshot NewSnapshot;
	if (UMHGZEquipmentInstance* Weapon = GetEquippedItem(
		FGameplayTag::RequestGameplayTag(TEXT("Equipment.Slot.Weapon"))))
	{
		NewSnapshot.EquipmentInstance = Weapon;
		if (UMHGZWeaponDefinition* WeaponDef = Cast<UMHGZWeaponDefinition>(Weapon->Definition))
		{
			NewSnapshot.WeaponDefinition = WeaponDef;
			NewSnapshot.RuntimeDefinition = WeaponDef->RuntimeDefinition;
		}
	}

	const bool bWeaponIdentityChanged =
		CurrentWeaponSnapshot.EquipmentInstance.Get() != NewSnapshot.EquipmentInstance.Get()
		|| CurrentWeaponSnapshot.RuntimeDefinition.Get() != NewSnapshot.RuntimeDefinition.Get();

	if (bWeaponIdentityChanged)
	{
		NewSnapshot.WeaponRevision = CurrentWeaponSnapshot.WeaponRevision + 1;
		CurrentWeaponSnapshot = NewSnapshot;
		OnEquippedWeaponChanged.Broadcast(NewSnapshot);
	}
	else
	{
		// 同一身份：刷新定义引用但不递增修订，也不广播 WeaponChanged。
		CurrentWeaponSnapshot.WeaponDefinition = NewSnapshot.WeaponDefinition;
		CurrentWeaponSnapshot.RuntimeDefinition = NewSnapshot.RuntimeDefinition;
	}

	UMHGZAbilitySystemComponent* ASC = Cast<UMHGZAbilitySystemComponent>(GetPlayerASC());
	if (ASC)
	{
		// 2) 一行清空全部装备 GE
		FGameplayTagContainer EquipmentTags;
		EquipmentTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Effect.Source.Equipment")));
		ASC->RemoveActiveEffectsWithAppliedTags(EquipmentTags);

		// 3) 遍历所有已装备物品，重新 Apply 持久装备效果
		for (const auto& Pair : EquippedItems)
		{
			ApplyItemEffects(Pair.Value);
		}

		// 4) 重算基础三围
		RecalculateEquipmentBaseAttributes(ASC);
	}

	// 5) 属性/词条重算通知 + M1 遗留委托（不再承担武器运行时重建）
	OnEquipmentStatsChanged.Broadcast();
	OnEquipmentChanged.Broadcast();
}

void UMHGZEquipmentComponent::RecalculateEquipmentBaseAttributes(UAbilitySystemComponent* ASC)
{
	if (!ASC) return;

	float TotalAttackPower = 0.f;
	float TotalDefense = 0.f;
	float TotalCriticalRate = 0.f;
	for (const auto& Pair : EquippedItems)
	{
		const UMHGZEquipmentInstance* Instance = Pair.Value;
		if (!Instance || !Instance->Definition) continue;

		TotalAttackPower += Instance->Definition->AttackPower;
		TotalDefense += Instance->Definition->Defense;
		TotalCriticalRate += Instance->Definition->CriticalRate;
	}

	ASC->SetNumericAttributeBase(UMHGZAttributeSet::GetAttackPowerAttribute(), TotalAttackPower);
	ASC->SetNumericAttributeBase(UMHGZAttributeSet::GetDefenseAttribute(), TotalDefense);
	ASC->SetNumericAttributeBase(UMHGZAttributeSet::GetCriticalRateAttribute(), TotalCriticalRate);

	UE_LOG(LogTemp, Log, TEXT("[Equipment] Base stats Attack=%.1f Defense=%.1f Crit=%.1f"),
		TotalAttackPower, TotalDefense, TotalCriticalRate);
}

void UMHGZEquipmentComponent::ApplyItemEffects(UMHGZEquipmentInstance* Item)
{
	if (!Item || !Item->Definition) return;

	UMHGZAbilitySystemComponent* ASC = Cast<UMHGZAbilitySystemComponent>(GetPlayerASC());
	if (!ASC) return;

	// 仅持久装备效果：词条 GE（当前 Demo 阶段为占位实现）。
	ApplyEntryGEs(ASC, Item->Definition->Entries);

	// 遍历镶嵌的饰品
	for (const auto& SocketPair : Item->SocketedAccessories)
	{
		if (SocketPair.Value)
		{
			ApplyEntryGEs(ASC, SocketPair.Value->Definition->Entries);
		}
	}
}

void UMHGZEquipmentComponent::ApplyEntryGEs(UAbilitySystemComponent* ASC,
	const TArray<FEntryReference>& Entries)
{
	if (!ASC) return;

	for (const FEntryReference& EntryRef : Entries)
	{
		// Demo 阶段简化：不实现完整词条系统。
		// 完整实现见 attributes.md §DT_EntryCatalog + UExecCalc_EntryStat。
	}
}