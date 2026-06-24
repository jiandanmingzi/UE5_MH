// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZEquipmentComponent.h"
#include "MHGZEquipmentInstance.h"
#include "MHGZEquipmentDefinition.h"
#include "MHGZPlayerState.h"
#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "ActionSystem/MHGZWeaponComboData.h"
#include "ActionSystem/MHGZComboCoordinatorAbility.h"
#include "AttributeSystem/MHGZWeaponResourceComponent.h"
#include "AttributeSystem/Res_InsectGlaive.h"
#include "Data/MHGZDataManager.h"
#include "InsectGlaive/Kinsect/InsectGlaiveKinsectData.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"

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

	// 若槽位已有装备，先卸载
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
	UAbilitySystemComponent* ASC = GetPlayerASC();
	if (!ASC) return;

	FGameplayTagContainer EquipmentTags;
	EquipmentTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Effect.Source.Equipment")));

	// 一行清空全部装备 GE
	ASC->RemoveActiveEffectsWithAppliedTags(EquipmentTags);

	// 销毁旧 ResourceComponent
	AMHGZPlayerState* PS = Cast<AMHGZPlayerState>(GetOwner());
	if (PS)
	{
		TArray<UMHGZWeaponResourceComponent*> ExistingRCs;
		PS->GetComponents<UMHGZWeaponResourceComponent>(ExistingRCs);
		for (UMHGZWeaponResourceComponent* RC : ExistingRCs)
		{
			RC->ClearAllEntryModifiers();
			RC->DestroyComponent();
		}
	}

	// 遍历所有已装备物品，重新 Apply
	for (const auto& Pair : EquippedItems)
	{
		ApplyItemEffects(Pair.Value);
	}

	OnEquipmentChanged.Broadcast();
}

void UMHGZEquipmentComponent::ApplyItemEffects(UMHGZEquipmentInstance* Item)
{
	if (!Item || !Item->Definition) return;

	UAbilitySystemComponent* ASC = GetPlayerASC();
	if (!ASC) return;

	// Apply 词条 GE
	ApplyEntryGEs(ASC, Item->Definition->Entries);

	// 检查是否为武器定义 → 创建 ResourceComponent
	if (UMHGZWeaponDefinition* WeaponDef = Cast<UMHGZWeaponDefinition>(Item->Definition))
	{
		// 查 DT_WeaponResourceConfig 获取 ResourceComponent 类
		// 简化：硬编码虫棍
		if (WeaponDef->WeaponTypeTag.MatchesTag(
			FGameplayTag::RequestGameplayTag(TEXT("Weapon.InsectGlaive"))))
		{
			// 创建 URes_InsectGlaive 并注册
			AActor* Owner = GetOwner();
			if (Owner)
			{
				URes_InsectGlaive* IGComp = NewObject<URes_InsectGlaive>(Owner);
				IGComp->RegisterComponent();
			}
		}
	}

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
		// Demo 阶段简化：不实现完整词条系统
		// 完整实现见 attributes.md §DT_EntryCatalog + UExecCalc_EntryStat
	}
}
