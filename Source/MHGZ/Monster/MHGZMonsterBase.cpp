// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZMonsterBase.h"
#include "MHGZMonsterHitzoneComponent.h"
#include "MHGZDummyConfig.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Components/CapsuleComponent.h"

AMHGZMonsterBase::AMHGZMonsterBase()
{
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AttributeSet = CreateDefaultSubobject<UMHGZAttributeSet>(TEXT("AttributeSet"));

	// 只有 Hitzone 响应武器 Trace，避免角色胶囊先 Block 导致 Sweep 取不到具体部位。
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Ignore);
	GetMesh()->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Ignore);
}

UAbilitySystemComponent* AMHGZMonsterBase::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void AMHGZMonsterBase::BeginPlay()
{
	Super::BeginPlay();
	AbilitySystemComponent->InitAbilityActorInfo(this, this);
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

	// ApplyConfig 可重复调用；重建前先移除旧的动态/蓝图部位，避免重复伤害判定。
	TArray<UMHGZMonsterHitzoneComponent*> ExistingHitzones;
	GetComponents<UMHGZMonsterHitzoneComponent>(ExistingHitzones);
	for (UMHGZMonsterHitzoneComponent* Existing : ExistingHitzones)
	{
		if (Existing)
		{
			Existing->DestroyComponent();
		}
	}

	for (const FDummyHitzoneConfig& HZConfig : Config->Hitzones)
	{
		const FName ComponentName = MakeUniqueObjectName(
			this, UMHGZMonsterHitzoneComponent::StaticClass(),
			FName(*FString::Printf(TEXT("Hitzone_%s"), *HZConfig.BoneName.ToString())));
		UMHGZMonsterHitzoneComponent* Hitzone =
			NewObject<UMHGZMonsterHitzoneComponent>(this, ComponentName);
		Hitzone->BoneName = HZConfig.BoneName;
		Hitzone->HitzoneTag = HZConfig.HitzoneTag;
		Hitzone->DefenseMultiplier = HZConfig.DefenseMultiplier;
		Hitzone->StaggerRate = HZConfig.StaggerRate;

		// 挂载到对应骨骼
		Hitzone->AttachToComponent(
			GetMesh(),
			FAttachmentTransformRules::SnapToTargetIncludingScale,
			HZConfig.BoneName);
		AddInstanceComponent(Hitzone);
		Hitzone->RegisterComponent();
		Hitzone->SetSphereRadius(FMath::Max(1.f, HZConfig.HalfExtent.X));
	}
}
