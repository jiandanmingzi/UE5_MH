// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZMonsterBase.h"
#include "MHGZMonsterHitzoneComponent.h"
#include "MHGZDummyConfig.h"
#include "AbilitySystemComponent.h"

AMHGZMonsterBase::AMHGZMonsterBase()
{
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
}

UAbilitySystemComponent* AMHGZMonsterBase::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void AMHGZMonsterBase::BeginPlay()
{
	Super::BeginPlay();
}

void AMHGZMonsterBase::ForceRestoreAllChannels()
{
	TArray<UMHGZMonsterHitzoneComponent*> Hitzones;
	GetComponents<UMHGZMonsterHitzoneComponent>(Hitzones);
	for (UMHGZMonsterHitzoneComponent* HZ : Hitzones)
	{
		HZ->ForceRestoreAllChannels();
	}
}

void AMHGZMonsterBase::GenerateHitzonesFromConfig(UMHGZDummyConfig* Config)
{
	if (!Config) return;

	for (const FDummyHitzoneConfig& HZConfig : Config->Hitzones)
	{
		UMHGZMonsterHitzoneComponent* Hitzone = NewObject<UMHGZMonsterHitzoneComponent>(this);
		Hitzone->BoneName = HZConfig.BoneName;
		Hitzone->HitzoneTag = HZConfig.HitzoneTag;
		Hitzone->DefenseMultiplier = HZConfig.DefenseMultiplier;
		Hitzone->StaggerRate = HZConfig.StaggerRate;
		Hitzone->SetSphereRadius(HZConfig.HalfExtent.X);

		// 挂载到对应骨骼
		Hitzone->AttachToComponent(
			GetMesh(),
			FAttachmentTransformRules::SnapToTargetIncludingScale,
			HZConfig.BoneName);
		Hitzone->RegisterComponent();
	}
}
