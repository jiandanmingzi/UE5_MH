// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZEquipmentComponent.h"
#include "MHGZEquipmentInstance.h"
#include "MHGZEquipmentDefinition.h"
#include "MHGZPlayerState.h"
#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "ActionSystem/MHGZWeaponComboData.h"
#include "ActionSystem/MHGZComboCoordinatorAbility.h"
#include "AttributeSystem/MHGZWeaponResourceComponent.h"
#include "Data/MHGZDataManager.h"
#include "Inventory/MHGZItemTypes.h"
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

	UMHGZAbilitySystemComponent* ASC = Cast<UMHGZAbilitySystemComponent>(GetPlayerASC());
	if (!ASC) return;

	// Apply 词条 GE
	ApplyEntryGEs(ASC, Item->Definition->Entries);

	// 检查是否为武器定义
	if (UMHGZWeaponDefinition* WeaponDef = Cast<UMHGZWeaponDefinition>(Item->Definition))
	{
		AActor* Owner = GetOwner();
		UMHGZDataManager* DataMgr = UMHGZDataManager::Get(this);

		// ── ① 创建 ResourceComponent（查 DT_WeaponResourceConfig）──
		if (DataMgr)
		{
			if (UDataTable* ResConfigDT = DataMgr->GetWeaponResourceConfig())
			{
				TArray<FWeaponResourceConfigRow*> ResRows;
				ResConfigDT->GetAllRows<FWeaponResourceConfigRow>(TEXT("MHGZ"), ResRows);
				for (const FWeaponResourceConfigRow* ResRow : ResRows)
				{
					if (ResRow && ResRow->WeaponTypeTag == WeaponDef->WeaponTypeTag
						&& ResRow->ResourceComponentClass)
					{
						if (Owner)
						{
							UMHGZWeaponResourceComponent* RC = NewObject<UMHGZWeaponResourceComponent>(
								Owner, ResRow->ResourceComponentClass);
							RC->RegisterComponent();
						}
						break;
					}
				}
			}
		}

		// ── ② 查连招表映射 → ③ 激活协调器 + 注入 → ④ 授予武器技能 ──
		if (DataMgr)
		{
			if (UDataTable* ComboConfigDT = DataMgr->GetWeaponComboConfig())
			{
				TArray<FWeaponComboConfigRow*> ComboRows;
				ComboConfigDT->GetAllRows<FWeaponComboConfigRow>(TEXT("MHGZ"), ComboRows);
				for (const FWeaponComboConfigRow* ComboRow : ComboRows)
				{
					if (!ComboRow || ComboRow->WeaponTypeTag != WeaponDef->WeaponTypeTag
						|| ComboRow->ComboDataAsset.IsNull())
					{
						continue;
					}

					UMHGZWeaponComboData* ComboData = ComboRow->ComboDataAsset.LoadSynchronous();
					if (!ComboData) break;

					// ③ 激活连招协调器 + 注入连招表
					UGA_WeaponComboCoordinator* Coord = NewObject<UGA_WeaponComboCoordinator>(Owner);
					FGameplayAbilitySpec CoordSpec(Coord, 1, INDEX_NONE, ASC);
					ASC->GiveAbilityAndActivateOnce(CoordSpec);
					Coord->InjectComboData(ComboData);

					// ④ 授予所有武器 Ability（从 ComboTable 收集）
					TArray<TSubclassOf<UGameplayAbility>> WeaponAbilities;
					for (const FComboNode& Node : ComboData->ComboTable)
					{
						if (Node.AbilityClass)
						{
							WeaponAbilities.AddUnique(Node.AbilityClass);
						}
					}
					ASC->GrantWeaponAbilities(WeaponAbilities);

					break;  // 一个武器只匹配一行
				}
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
