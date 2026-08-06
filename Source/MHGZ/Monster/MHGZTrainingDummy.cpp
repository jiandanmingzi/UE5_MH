// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZTrainingDummy.h"
#include "MHGZDummyConfig.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "AbilitySystemComponent.h"

AMHGZTrainingDummy::AMHGZTrainingDummy()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AMHGZTrainingDummy::BeginPlay()
{
	Super::BeginPlay();

	if (UMHGZAttributeSet* Attributes = GetAttributeSet())
	{
		Attributes->InitMaxHealth(DummyMaxHealth);
		Attributes->InitHealth(DummyMaxHealth);
		GetAbilitySystemComponent()->GetGameplayAttributeValueChangeDelegate(
			UMHGZAttributeSet::GetHealthAttribute()).AddUObject(
				this, &AMHGZTrainingDummy::HandleHealthChanged);
	}

	if (DummyConfig)
	{
		ApplyConfig(DummyConfig);
	}

	OnHealthChanged.Broadcast(GetCurrentHealth(), GetMaxHealth());
}

void AMHGZTrainingDummy::ApplyConfig(UMHGZDummyConfig* Config)
{
	if (!Config) return;

	// 设置骨骼模型
	if (USkeletalMesh* SKMesh = Config->DisplayMesh.LoadSynchronous())
	{
		GetMesh()->SetSkeletalMesh(SKMesh);
	}

	// 生成部位碰撞体
	GenerateHitzonesFromConfig(Config);

	// 播放待机动画
	if (UAnimMontage* IdleMontage = Config->LoopingMontage.LoadSynchronous())
	{
		if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
		{
			AnimInstance->Montage_Play(IdleMontage);
		}
	}
}

float AMHGZTrainingDummy::GetCurrentHealth() const
{
	return GetAttributeSet() ? GetAttributeSet()->GetHealth() : 0.f;
}

float AMHGZTrainingDummy::GetMaxHealth() const
{
	return GetAttributeSet() ? GetAttributeSet()->GetMaxHealth() : 0.f;
}

void AMHGZTrainingDummy::HandleHealthChanged(const FOnAttributeChangeData& ChangeData)
{
	const float MaxHealth = GetMaxHealth();
	UE_LOG(LogTemp, Log, TEXT("[TrainingDummy] %s Health %.1f / %.1f"),
		*GetName(), ChangeData.NewValue, MaxHealth);
	OnHealthChanged.Broadcast(ChangeData.NewValue, MaxHealth);
}
