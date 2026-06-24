// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZAttackAbility.h"
#include "MHGZAbilitySystemComponent.h"
#include "MHGZComboCoordinatorAbility.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"
#include "AbilitySystemGlobals.h"
#include "MotionWarpingComponent.h"
#include "Components/SphereComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraShakeBase.h"

UMHGZAttackAbility::UMHGZAttackAbility()
{
	MaxCorrectionAngle = 30.0f;
}

void UMHGZAttackAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// 父类：扣耐力/资源 + 启动冷却
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC) return;

	// 添加攻击状态 Tag
	ASC->AddLooseGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Attacking")));

	// 重置状态
	HitTargets.Empty();
	CurrentSegmentIndex = 0;
	bHasHitThisActivation = false;
	bHasActiveRootMotionTask = false;

	// 方向修正
	ApplyDirectionCorrection();

	// 播放 Montage
	if (AttackMontage)
	{
		ACharacter* Character = Cast<ACharacter>(ActorInfo->AvatarActor.Get());
		if (Character)
		{
			UAnimInstance* AnimInstance = Character->GetMesh()->GetAnimInstance();
			if (AnimInstance)
			{
				ActiveAttackMontage = AttackMontage;
				AnimInstance->Montage_Play(AttackMontage);
			}
		}
	}
}

void UMHGZAttackAbility::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (ASC)
	{
		// 移除攻击状态 Tag
		ASC->RemoveLooseGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Attacking")));
	}

	// 关碰撞（幂等安全——清除 MultiHitTimer）
	DisableCollision();

	// 通知协调器
	NotifyCoordinatorAttackFinished();

	// 停止 Montage
	if (ActiveAttackMontage)
	{
		ACharacter* Character = Cast<ACharacter>(ActorInfo->AvatarActor.Get());
		if (Character)
		{
			UAnimInstance* AnimInstance = Character->GetMesh()->GetAnimInstance();
			if (AnimInstance)
			{
				AnimInstance->Montage_Stop(0.1f, ActiveAttackMontage);
			}
		}
		ActiveAttackMontage = nullptr;
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UMHGZAttackAbility::ApplyDirectionCorrection()
{
	if (MaxCorrectionAngle <= 0.f) return;

	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!Character) return;

	// 读摇杆方向
	FVector MovementInput = Character->GetLastMovementInputVector();
	if (MovementInput.SizeSquared() < 0.01f) return; // 无输入

	const FVector InputDir(MovementInput.X, MovementInput.Y, 0.f);
	const FVector CharForward = Character->GetActorForwardVector();
	const FVector CharForward2D = FVector(CharForward.X, CharForward.Y, 0.f).GetSafeNormal();

	if (CharForward2D.IsNearlyZero()) return;

	// 计算夹角
	const float Dot = FVector::DotProduct(InputDir.GetSafeNormal(), CharForward2D);
	const float AngleDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Dot, -1.f, 1.f)));

	if (AngleDeg <= MaxCorrectionAngle)
	{
		// 在容许范围内 → 设置 MotionWarping RotationTarget
		UMotionWarpingComponent* MotionWarping = GetMotionWarpingComponent();
		if (MotionWarping)
		{
			const FVector TargetLocation = Character->GetActorLocation() + InputDir.GetSafeNormal() * 500.f;
			MotionWarping->AddOrUpdateWarpTargetFromLocation(
				FName("AttackDirection"),
				TargetLocation);
		}
	}
}

UMotionWarpingComponent* UMHGZAttackAbility::GetMotionWarpingComponent() const
{
	if (ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		return Character->FindComponentByClass<UMotionWarpingComponent>();
	}
	return nullptr;
}

void UMHGZAttackAbility::EnableCollision(int32 SegmentIndex)
{
	if (!AttackSegments.IsValidIndex(SegmentIndex)) return;

	CurrentSegmentIndex = SegmentIndex;
	HitTargets.Empty();
	const FAttackSegmentConfig& Seg = AttackSegments[SegmentIndex];

	// 销毁旧碰撞体
	if (ActiveCollisionComponent)
	{
		ActiveCollisionComponent->DestroyComponent();
		ActiveCollisionComponent = nullptr;
	}

	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!Character) return;

	// 获取挂载 Socket 的骨骼位置
	const FVector SocketLocation = Character->GetMesh()->GetSocketLocation(Seg.Collision.AttachSocketName);

	// 创建碰撞体
	switch (Seg.Collision.Shape)
	{
	case EAttackCollisionShape::Sphere:
		{
			USphereComponent* Sphere = NewObject<USphereComponent>(Character);
			Sphere->SetSphereRadius(Seg.Collision.ShapeExtent.X);
			ActiveCollisionComponent = Sphere;
		}
		break;
	case EAttackCollisionShape::Capsule:
		{
			UCapsuleComponent* Capsule = NewObject<UCapsuleComponent>(Character);
			Capsule->SetCapsuleRadius(Seg.Collision.ShapeExtent.X);
			Capsule->SetCapsuleHalfHeight(Seg.Collision.ShapeExtent.Z);
			ActiveCollisionComponent = Capsule;
		}
		break;
	case EAttackCollisionShape::Box:
		{
			UBoxComponent* Box = NewObject<UBoxComponent>(Character);
			Box->SetBoxExtent(Seg.Collision.ShapeExtent);
			ActiveCollisionComponent = Box;
		}
		break;
	}

	if (ActiveCollisionComponent)
	{
		ActiveCollisionComponent->SetWorldLocation(SocketLocation);
		ActiveCollisionComponent->AttachToComponent(
			Character->GetMesh(),
			FAttachmentTransformRules::SnapToTargetIncludingScale,
			Seg.Collision.AttachSocketName);
		ActiveCollisionComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		ActiveCollisionComponent->SetCollisionObjectType(Seg.Collision.CollisionChannel);
		ActiveCollisionComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
		ActiveCollisionComponent->SetCollisionResponseToChannel(Seg.Collision.CollisionChannel, ECR_Overlap);
		ActiveCollisionComponent->RegisterComponent();

		// 绑定 Overlap 回调
		ActiveCollisionComponent->OnComponentBeginOverlap.AddDynamic(this, &UMHGZAttackAbility::OnAttackOverlap);

		// 首帧 Sweep
		PerformSweepCheck();

		// 启动多跳 Timer
		if (Seg.MultiHitCount > 1)
		{
			MultiHitCurrentCount = 1; // 首帧已算一跳
			Character->GetWorldTimerManager().SetTimer(
				MultiHitTimer,
				this, &UMHGZAttackAbility::OnMultiHitTick,
				Seg.MultiHitInterval, true);
		}
	}
}

void UMHGZAttackAbility::DisableCollision()
{
	// 清除多跳 Timer
	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (Character)
	{
		Character->GetWorldTimerManager().ClearTimer(MultiHitTimer);
	}

	// 空挥截断
	if (AttackSegments.IsValidIndex(CurrentSegmentIndex))
	{
		const FAttackSegmentConfig& Seg = AttackSegments[CurrentSegmentIndex];
		if (Seg.Damage.bRequiresHitToContinue && HitTargets.IsEmpty())
		{
			if (!ShouldContinueAfterHit())
			{
				EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, false, true);
				return;
			}
		}
	}

	// 销毁碰撞体
	if (ActiveCollisionComponent)
	{
		ActiveCollisionComponent->OnComponentBeginOverlap.RemoveAll(this);
		ActiveCollisionComponent->DestroyComponent();
		ActiveCollisionComponent = nullptr;
	}
}

void UMHGZAttackAbility::PerformSweepCheck()
{
	// 从武器上一帧位置扫到当前帧，取首个命中
	// 简化版：依赖 Overlap 事件
}

void UMHGZAttackAbility::OnAttackOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!OtherActor || OtherActor == GetAvatarActorFromActorInfo()) return;

	// 已在 HitTargets 中 → 跳过
	if (HitTargets.Contains(OtherActor)) return;

	// 查找 HitzoneComponent
	UMHGZMonsterHitzoneComponent* Hitzone = FindHitzoneComponent(OtherActor, NAME_None);
	if (!Hitzone) return;

	// 如果有 HitzoneQueryTag，检查匹配
	if (AttackSegments.IsValidIndex(CurrentSegmentIndex))
	{
		const FGameplayTag& QueryTag = AttackSegments[CurrentSegmentIndex].Collision.HitzoneQueryTag;
		if (QueryTag.IsValid() && Hitzone->HitzoneTag != QueryTag)
		{
			return;
		}
	}

	HitTargets.Add(OtherActor, Hitzone->BoneName);
	ApplyDamage(OtherActor, Hitzone->BoneName, CurrentSegmentIndex);
}

void UMHGZAttackAbility::OnMultiHitTick()
{
	if (!AttackSegments.IsValidIndex(CurrentSegmentIndex)) return;
	const FAttackSegmentConfig& Seg = AttackSegments[CurrentSegmentIndex];

	for (const auto& Pair : HitTargets)
	{
		ApplyDamage(Pair.Key.Get(), Pair.Value, CurrentSegmentIndex);
	}

	MultiHitCurrentCount++;
	if (MultiHitCurrentCount >= Seg.MultiHitCount)
	{
		ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
		if (Character)
		{
			Character->GetWorldTimerManager().ClearTimer(MultiHitTimer);
		}
	}
}

void UMHGZAttackAbility::ApplyDamage(AActor* Target, FName HitzoneBoneName, int32 SegmentIndex)
{
	if (!Target || !AttackSegments.IsValidIndex(SegmentIndex)) return;

	const FAttackSegmentConfig& Seg = AttackSegments[SegmentIndex];

	// 卡肉
	const float HitStop = Seg.Damage.HitStopBase.GetValueAtLevel(GetAbilityLevel());
	if (HitStop > 0.f)
	{
		// 简化：通过 CustomTimeDilation 实现
		if (ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
		{
			Character->CustomTimeDilation = 0.05f;
			FTimerHandle HitStopTimer;
			Character->GetWorldTimerManager().SetTimer(HitStopTimer, [Character]()
			{
				if (Character)
				{
					Character->CustomTimeDilation = 1.0f;
				}
			}, HitStop, false);
		}
	}

	// 构造并 Apply GE
	FGameplayEffectSpecHandle Spec = MakeDamageSpec(Target, HitzoneBoneName, SegmentIndex);
	if (Spec.IsValid())
	{
		UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo();
		UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target);
		if (SourceASC && TargetASC)
		{
			SourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data, TargetASC);
		}
	}

	// 震屏
	if (Seg.Damage.CameraShakeClass && Seg.Damage.CameraShakeScale > 0.f)
	{
		if (APlayerController* PC = Cast<APlayerController>(
			Cast<ACharacter>(GetAvatarActorFromActorInfo())->GetController()))
		{
			PC->ClientStartCameraShake(
				Seg.Damage.CameraShakeClass, Seg.Damage.CameraShakeScale);
		}
	}

	// 首次命中逻辑
	if (!bHasHitThisActivation)
	{
		bHasHitThisActivation = true;
		NotifyCoordinatorFirstHit();

		// Apply OnHitSelfEffect
		if (Seg.Damage.OnHitSelfEffect)
		{
			UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
			if (ASC)
			{
				ASC->ApplyGameplayEffectToSelf(
					Seg.Damage.OnHitSelfEffect->GetDefaultObject<UGameplayEffect>(),
					1.0f, ASC->MakeEffectContext());
			}
		}
	}
}

FGameplayEffectSpecHandle UMHGZAttackAbility::MakeDamageSpec(
	AActor* Target, FName HitzoneBoneName, int32 SegmentIndex)
{
	if (!AttackSegments.IsValidIndex(SegmentIndex))
	{
		return FGameplayEffectSpecHandle();
	}

	const FAttackSegmentConfig& Seg = AttackSegments[SegmentIndex];
	if (!Seg.Damage.DamageEffectClass) return FGameplayEffectSpecHandle();

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC) return FGameplayEffectSpecHandle();

	FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(
		Seg.Damage.DamageEffectClass, 1.0f, ASC->MakeEffectContext());

	if (!Spec.IsValid()) return Spec;

	// 动作值
	Spec.Data->SetSetByCallerMagnitude(
		FGameplayTag::RequestGameplayTag(TEXT("Damage.MotionValue")),
		Seg.Damage.MotionValue.GetValueAtLevel(GetAbilityLevel()));

	// 基础破坏值
	Spec.Data->SetSetByCallerMagnitude(
		FGameplayTag::RequestGameplayTag(TEXT("Damage.BaseStagger")),
		Seg.Damage.BaseStaggerValue.GetValueAtLevel(GetAbilityLevel()));

	// 部位 HitzoneTag
	UMHGZMonsterHitzoneComponent* Hitzone = FindHitzoneComponent(Target, HitzoneBoneName);
	if (Hitzone && Hitzone->HitzoneTag.IsValid())
	{
		Spec.Data->AddDynamicAssetTag(Hitzone->HitzoneTag);
	}

	// 硬直等级
	if (Seg.Damage.HitStaggerTag.IsValid())
	{
		Spec.Data->AddDynamicAssetTag(Seg.Damage.HitStaggerTag);
	}

	// GameplayCue —— 命中反馈
	if (Seg.Damage.HitCueTag.IsValid())
	{
		Spec.Data->AddDynamicAssetTag(Seg.Damage.HitCueTag);
	}
	if (Seg.Damage.ElementalCueTag.IsValid())
	{
		Spec.Data->AddDynamicAssetTag(Seg.Damage.ElementalCueTag);
	}
	// 伤害数字 GC——始终追加
	Spec.Data->AddDynamicAssetTag(
		FGameplayTag::RequestGameplayTag(TEXT("GameplayCue.Hit.DamageNumber")));

	// GameplayEffectContext —— HitResult
	if (Hitzone)
	{
		FGameplayAbilityTargetDataHandle TargetData;
		FGameplayAbilityTargetData_SingleTargetHit* HitData = new FGameplayAbilityTargetData_SingleTargetHit();
		HitData->HitResult.Location = Hitzone->GetComponentLocation();
		TargetData.Add(HitData);
		Spec.Data->GetContext().AddHitResult(HitData->HitResult);
	}

	return Spec;
}

bool UMHGZAttackAbility::ShouldContinueAfterHit_Implementation() const
{
	if (!AttackSegments.IsValidIndex(CurrentSegmentIndex)) return true;

	const FAttackSegmentConfig& Seg = AttackSegments[CurrentSegmentIndex];
	if (Seg.Damage.bRequiresHitToContinue && HitTargets.IsEmpty())
	{
		return false;
	}
	return true;
}

void UMHGZAttackAbility::NotifyCoordinatorAttackFinished()
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC) return;

	UMHGZAbilitySystemComponent* MHGZASC = Cast<UMHGZAbilitySystemComponent>(ASC);
	if (MHGZASC)
	{
		if (UGA_WeaponComboCoordinator* Coord = MHGZASC->GetActiveComboCoordinator())
		{
			Coord->OnAttackFinished();
		}
	}
}

void UMHGZAttackAbility::NotifyCoordinatorFirstHit()
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC) return;

	UMHGZAbilitySystemComponent* MHGZASC = Cast<UMHGZAbilitySystemComponent>(ASC);
	if (MHGZASC)
	{
		if (UGA_WeaponComboCoordinator* Coord = MHGZASC->GetActiveComboCoordinator())
		{
			Coord->OnAttackHit();
		}
	}
}

UMHGZMonsterHitzoneComponent* UMHGZAttackAbility::FindHitzoneComponent(
	AActor* Target, FName BoneName) const
{
	if (!Target) return nullptr;

	TArray<UMHGZMonsterHitzoneComponent*> Hitzones;
	Target->GetComponents<UMHGZMonsterHitzoneComponent>(Hitzones);

	for (UMHGZMonsterHitzoneComponent* HZ : Hitzones)
	{
		if (BoneName == NAME_None || HZ->BoneName == BoneName)
		{
			return HZ;
		}
	}

	return nullptr;
}
