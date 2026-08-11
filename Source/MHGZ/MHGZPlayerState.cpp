// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZPlayerState.h"
#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "Equipment/MHGZEquipmentComponent.h"
#include "Inventory/MHGZBackpackComponent.h"
#include "Inventory/MHGZWarehouseComponent.h"

AMHGZPlayerState::AMHGZPlayerState()
{
	// ★ I-4 修复：PlayerState 必须启用 Tick（WeaponResourceComponent 依赖）
	PrimaryActorTick.bCanEverTick = true;

	AbilitySystemComponent = CreateDefaultSubobject<UMHGZAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AttributeSet = CreateDefaultSubobject<UMHGZAttributeSet>(TEXT("AttributeSet"));
	EquipmentComponent = CreateDefaultSubobject<UMHGZEquipmentComponent>(TEXT("EquipmentComponent"));
	BackpackComponent = CreateDefaultSubobject<UMHGZBackpackComponent>(TEXT("BackpackComponent"));
	WarehouseComponent = CreateDefaultSubobject<UMHGZWarehouseComponent>(TEXT("WarehouseComponent"));
	AbilitySystemComponent->AddAttributeSetSubobject(AttributeSet.Get());
}

UAbilitySystemComponent* AMHGZPlayerState::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void AMHGZPlayerState::BeginPlay()
{
	Super::BeginPlay();
}
