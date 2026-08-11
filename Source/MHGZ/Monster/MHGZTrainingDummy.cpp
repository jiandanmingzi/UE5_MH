// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZTrainingDummy.h"
#include "MHGZ.h"
#include "MHGZDummyConfig.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Components/CapsuleComponent.h"

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

	FString ValidationError;
	if (!UMHGZDummyConfig::ValidateHitzoneConfigs(Config->Hitzones, ValidationError))
	{
		UE_LOG(LogMHGZ, Error,
			TEXT("[TrainingDummy] %s config validation failed: %s"),
			*GetName(), *ValidationError);
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

EIncomingHitSubmitResult AMHGZTrainingDummy::SubmitCounterTestAttack(
	ACharacter* TargetCharacter, FGuid AttackInstanceID)
{
	if (!TargetCharacter)
	{
		return EIncomingHitSubmitResult::Rejected;
	}

	UMHGZIncomingHitResolverComponent* Resolver =
		TargetCharacter->FindComponentByClass<UMHGZIncomingHitResolverComponent>();
	if (!Resolver)
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[TrainingDummy] %s cannot submit: target %s has no IncomingHitResolver"),
			*GetName(), *TargetCharacter->GetName());
		return EIncomingHitSubmitResult::Rejected;
	}

	const FDummyCounterAttackConfig& Attack =
		DummyConfig ? DummyConfig->CounterTestAttack : FDummyCounterAttackConfig();

	FIncomingHitContext Context;
	Context.AttackInstanceID = AttackInstanceID;
	Context.SourceActor = this;
	Context.Damage = Attack.Damage;
	Context.bCounterable = Attack.bCounterable;
	Context.SourceAttackTag = Attack.SourceActionTag;
	Context.StaggerTag = Attack.StaggerTag;

	UCapsuleComponent* Capsule = TargetCharacter->GetCapsuleComponent();
	if (!Capsule)
	{
		return EIncomingHitSubmitResult::Rejected;
	}

	// Fixed, reproducible hit against the target capsule/impact.
	const FVector CapsuleCenter = Capsule->GetComponentLocation();
	const FVector Offset =
		Attack.HitOffset.IsNearlyZero() ? FVector::UpVector * 10.f : Attack.HitOffset;
	const FVector ImpactPoint = CapsuleCenter + Offset;
	Context.Hit = FHitResult(TargetCharacter, Capsule, ImpactPoint, FVector::UpVector);
	Context.Hit.bBlockingHit = true;
	Context.Hit.bStartPenetrating = false;
	Context.Hit.Time = 0.f;
	Context.Hit.Distance = Offset.Size();
	Context.Hit.Location = ImpactPoint;
	Context.Hit.ImpactPoint = ImpactPoint;
	Context.Hit.ImpactNormal = FVector::UpVector;
	Context.Hit.Normal = FVector::UpVector;

	return Resolver->SubmitIncomingHit(Context);
}

void AMHGZTrainingDummy::HandleHealthChanged(const FOnAttributeChangeData& ChangeData)
{
	const float MaxHealth = GetMaxHealth();
	UE_LOG(LogTemp, Log, TEXT("[TrainingDummy] %s Health %.1f / %.1f"),
		*GetName(), ChangeData.NewValue, MaxHealth);
	OnHealthChanged.Broadcast(ChangeData.NewValue, MaxHealth);
}
