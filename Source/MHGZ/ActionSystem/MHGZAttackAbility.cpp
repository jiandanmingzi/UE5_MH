// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZAttackAbility.h"
#include "MHGZAbilitySystemComponent.h"
#include "MHGZComboCoordinatorAbility.h"
#include "MHGZCharacter.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"
#include "AbilitySystemGlobals.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "MotionWarpingComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraShakeBase.h"
#include "Components/SkeletalMeshComponent.h"

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

	// 添加攻击状态 Tag + 阻断 CMC 移动输入
	ASC->AddLooseGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Attacking")));
	ASC->AddLooseGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.BlockMovement")));

	// 重置状态
	HitTargets.Empty();
	CurrentSegmentIndex = 0;
	bHasHitThisActivation = false;
	bHasActiveRootMotionTask = false;
	bCollisionActive = false;
	bIsEndingAbility = false;

	// 方向修正
	ApplyDirectionCorrection();

	// 由 GAS AbilityTask 播放并等待 Montage，确保正常完成、取消和被打断都能 EndAbility。
	if (!AttackMontage)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, false, true);
		return;
	}

	ActiveAttackMontage = AttackMontage;
	MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, FName(TEXT("AttackMontage")), AttackMontage, 1.0f);
	if (!MontageTask)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, false, true);
		return;
	}

	MontageTask->OnCompleted.AddDynamic(this, &UMHGZAttackAbility::OnMontageCompleted);
	MontageTask->OnInterrupted.AddDynamic(this, &UMHGZAttackAbility::OnMontageInterrupted);
	MontageTask->OnCancelled.AddDynamic(this, &UMHGZAttackAbility::OnMontageInterrupted);
	MontageTask->ReadyForActivation();
}

void UMHGZAttackAbility::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	if (bIsEndingAbility)
	{
		return;
	}
	bIsEndingAbility = true;

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (ASC)
	{
		// 移除攻击状态 Tag + 解除 CMC 移动阻断
		ASC->RemoveLooseGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Attacking")));
		ASC->RemoveLooseGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.BlockMovement")));
	}

	// 关碰撞（幂等安全——清除 MultiHitTimer）
	DisableCollision();

	// 通知协调器
	NotifyCoordinatorAttackFinished();

	// AbilityTask 会在 Ability 结束时负责停止 Montage 并解绑委托。
	MontageTask = nullptr;
	ActiveAttackMontage = nullptr;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UMHGZAttackAbility::OnMontageCompleted()
{
	if (IsActive() && !bIsEndingAbility)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
	}
}

void UMHGZAttackAbility::OnMontageInterrupted()
{
	if (IsActive() && !bIsEndingAbility)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
	}
}

void UMHGZAttackAbility::ApplyDirectionCorrection()
{
	if (MaxCorrectionAngle <= 0.f) return;

	AMHGZCharacter* Character = Cast<AMHGZCharacter>(GetAvatarActorFromActorInfo());
	if (!Character) return;

	// 读摇杆方向——用 Character::LastMovementInputDir（不受 BlockMovement 影响，始终最新）
	const FVector MovementInput = Character->GetLastMovementInputDir();
	if (MovementInput.IsNearlyZero()) return; // 无输入

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

	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!Character) return;
	USkeletalMeshComponent* Mesh = FindTraceMeshComponent(Seg.Collision);
	if (!Mesh || Seg.Collision.AttachSocketName.IsNone() ||
		!Mesh->DoesSocketExist(Seg.Collision.AttachSocketName))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Attack] Missing trace mesh/socket. ComponentTag='%s' Socket='%s' Mesh='%s'"),
			*Seg.Collision.TraceMeshComponentTag.ToString(),
			*Seg.Collision.AttachSocketName.ToString(), *GetNameSafe(Mesh));
		return;
	}

	PreviousTraceEnd = Mesh->GetSocketLocation(Seg.Collision.AttachSocketName);
	PreviousTraceStart = (!Seg.Collision.TraceStartSocketName.IsNone() &&
		Mesh->DoesSocketExist(Seg.Collision.TraceStartSocketName))
		? Mesh->GetSocketLocation(Seg.Collision.TraceStartSocketName)
		: PreviousTraceEnd;

	MultiHitCurrentCount = 0;
	bCollisionActive = true;
	PerformSweepCheck(); // 首帧零距离 Sweep，防止窗口开启时已经与部位相交。
}

void UMHGZAttackAbility::DisableCollision()
{
	bCollisionActive = false;

	// 清除多跳 Timer
	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (Character)
	{
		Character->GetWorldTimerManager().ClearTimer(MultiHitTimer);
	}

	// 空挥截断
	if (!bIsEndingAbility && AttackSegments.IsValidIndex(CurrentSegmentIndex))
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
}

void UMHGZAttackAbility::TickCollision(float DeltaSeconds)
{
	if (bCollisionActive)
	{
		PerformSweepCheck();
	}
}

void UMHGZAttackAbility::PerformSweepCheck()
{
	if (!bCollisionActive || !AttackSegments.IsValidIndex(CurrentSegmentIndex)) return;

	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!Character || !GetWorld()) return;

	const FAttackCollisionConfig& Collision = AttackSegments[CurrentSegmentIndex].Collision;
	USkeletalMeshComponent* Mesh = FindTraceMeshComponent(Collision);
	if (!Mesh || !Mesh->DoesSocketExist(Collision.AttachSocketName)) return;
	const FVector CurrentEnd = Mesh->GetSocketLocation(Collision.AttachSocketName);
	const FVector CurrentStart = (!Collision.TraceStartSocketName.IsNone() &&
		Mesh->DoesSocketExist(Collision.TraceStartSocketName))
		? Mesh->GetSocketLocation(Collision.TraceStartSocketName)
		: CurrentEnd;

	FCollisionShape QueryShape = FCollisionShape::MakeSphere(FMath::Max(1.f, Collision.ShapeExtent.X));
	switch (Collision.Shape)
	{
	case EAttackCollisionShape::Capsule:
		QueryShape = FCollisionShape::MakeCapsule(
			FMath::Max(1.f, Collision.ShapeExtent.X),
			FMath::Max(Collision.ShapeExtent.X, Collision.ShapeExtent.Z));
		break;
	case EAttackCollisionShape::Box:
		QueryShape = FCollisionShape::MakeBox(Collision.ShapeExtent.GetAbs().ComponentMax(FVector(1.f)));
		break;
	case EAttackCollisionShape::Sphere:
	default:
		break;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MHGZWeaponSweep), false, Character);
	QueryParams.AddIgnoredActor(Character);

	const bool bHasSpan = !Collision.TraceStartSocketName.IsNone() &&
		!CurrentStart.Equals(CurrentEnd, 0.1f);
	const int32 SampleCount = bHasSpan ? FMath::Clamp(Collision.TraceSampleCount, 1, 8) : 1;
	for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
	{
		const float Alpha = SampleCount == 1 ? 1.f :
			static_cast<float>(SampleIndex) / static_cast<float>(SampleCount - 1);
		const FVector SweepFrom = FMath::Lerp(PreviousTraceStart, PreviousTraceEnd, Alpha);
		const FVector SweepTo = FMath::Lerp(CurrentStart, CurrentEnd, Alpha);

		TArray<FHitResult> Hits;
		GetWorld()->SweepMultiByChannel(
			Hits, SweepFrom, SweepTo, Character->GetActorQuat(),
			Collision.CollisionChannel, QueryShape, QueryParams);

		for (const FHitResult& Hit : Hits)
		{
			ProcessSweepHit(Hit);
		}

		if (Collision.bDrawDebug)
		{
			DrawDebugLine(GetWorld(), SweepFrom, SweepTo, FColor::Red, false, 0.15f, 0, 1.5f);
			DrawDebugSphere(GetWorld(), SweepTo, Collision.ShapeExtent.X, 12,
				Hits.IsEmpty() ? FColor::Yellow : FColor::Green, false, 0.15f);
		}
	}

	PreviousTraceStart = CurrentStart;
	PreviousTraceEnd = CurrentEnd;
}

void UMHGZAttackAbility::ProcessSweepHit(const FHitResult& Hit)
{
	AActor* OtherActor = Hit.GetActor();
	if (!OtherActor || OtherActor == GetAvatarActorFromActorInfo() || HitTargets.Contains(OtherActor))
	{
		return;
	}

	UMHGZMonsterHitzoneComponent* Hitzone = Cast<UMHGZMonsterHitzoneComponent>(Hit.GetComponent());
	if (!Hitzone) return;

	// 如果有 HitzoneQueryTag，检查匹配
	if (AttackSegments.IsValidIndex(CurrentSegmentIndex))
	{
		const FGameplayTag& QueryTag = AttackSegments[CurrentSegmentIndex].Collision.HitzoneQueryTag;
		if (QueryTag.IsValid() && !Hitzone->HitzoneTag.MatchesTagExact(QueryTag))
		{
			return;
		}
	}

	HitTargets.Add(OtherActor, Hitzone->BoneName);
	ApplyDamage(OtherActor, Hitzone->BoneName, CurrentSegmentIndex);
	StartMultiHitTimerIfNeeded();
}

void UMHGZAttackAbility::StartMultiHitTimerIfNeeded()
{
	if (!AttackSegments.IsValidIndex(CurrentSegmentIndex)) return;
	const FAttackSegmentConfig& Seg = AttackSegments[CurrentSegmentIndex];
	if (Seg.MultiHitCount <= 1 || MultiHitCurrentCount > 0) return;

	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!Character) return;

	MultiHitCurrentCount = 1; // 首次接触已立即结算第一跳。
	Character->GetWorldTimerManager().SetTimer(
		MultiHitTimer, this, &UMHGZAttackAbility::OnMultiHitTick,
		FMath::Max(0.01f, Seg.MultiHitInterval), true);
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
			TWeakObjectPtr<ACharacter> WeakCharacter(Character);
			Character->GetWorldTimerManager().SetTimer(HitStopTimer, [WeakCharacter]()
			{
				if (ACharacter* ValidCharacter = WeakCharacter.Get())
				{
					ValidCharacter->CustomTimeDilation = 1.0f;
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
	if (Seg.Damage.bUseHitzoneDefense && Hitzone && Hitzone->HitzoneTag.IsValid())
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

USkeletalMeshComponent* UMHGZAttackAbility::FindTraceMeshComponent(
	const FAttackCollisionConfig& Collision) const
{
	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!Character || Collision.AttachSocketName.IsNone()) return nullptr;

	TArray<USkeletalMeshComponent*> MeshComponents;
	Character->GetComponents<USkeletalMeshComponent>(MeshComponents);

	// 显式标记的武器组件优先，避免角色主 Mesh 上的同名 Socket 被误选。
	if (!Collision.TraceMeshComponentTag.IsNone())
	{
		for (USkeletalMeshComponent* Mesh : MeshComponents)
		{
			if (Mesh && Mesh->ComponentHasTag(Collision.TraceMeshComponentTag) &&
				Mesh->DoesSocketExist(Collision.AttachSocketName))
			{
				return Mesh;
			}
		}
	}

	// 兼容旧配置：优先尝试角色主骨骼网格。
	if (USkeletalMeshComponent* CharacterMesh = Character->GetMesh())
	{
		if (CharacterMesh->DoesSocketExist(Collision.AttachSocketName))
		{
			return CharacterMesh;
		}
	}

	// 最后回退到任意拥有目标 Socket 的骨骼网格组件。
	for (USkeletalMeshComponent* Mesh : MeshComponents)
	{
		if (Mesh && Mesh->DoesSocketExist(Collision.AttachSocketName))
		{
			return Mesh;
		}
	}

	return nullptr;
}
