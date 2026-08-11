// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZPlayerState.h"
#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "Equipment/MHGZEquipmentComponent.h"
#include "Inventory/MHGZBackpackComponent.h"
#include "Inventory/MHGZWarehouseComponent.h"

AMHGZPlayerState::AMHGZPlayerState()
{
	// ★ I-4 修复：PlayerState 不再为动态 Resource 永久 Tick（M2 起 Resource 挂在 Character/Pawn）
	PrimaryActorTick.bCanEverTick = false;

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
