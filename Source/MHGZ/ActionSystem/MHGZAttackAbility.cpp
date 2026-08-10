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

namespace
{
	FName ResolveLegacyTraceEndSocket(const FAttackCollisionConfig& Collision)
	{
		return !Collision.TraceEndSocketName.IsNone()
			? Collision.TraceEndSocketName
			: Collision.AttachSocketName;
	}

	FVector EvaluateRotatingRegionPoint(
		const FVector& PreviousStart,
		const FVector& PreviousEnd,
		const FVector& CurrentStart,
		const FVector& CurrentEnd,
		float SpatialAlpha,
		float TimeAlpha)
	{
		const FVector PreviousSpan = PreviousEnd - PreviousStart;
		const FVector CurrentSpan = CurrentEnd - CurrentStart;
		const float PreviousLength = PreviousSpan.Size();
		const float CurrentLength = CurrentSpan.Size();

		if (PreviousLength <= KINDA_SMALL_NUMBER || CurrentLength <= KINDA_SMALL_NUMBER)
		{
			const FVector PreviousPoint = FMath::Lerp(PreviousStart, PreviousEnd, SpatialAlpha);
			const FVector CurrentPoint = FMath::Lerp(CurrentStart, CurrentEnd, SpatialAlpha);
			return FMath::Lerp(PreviousPoint, CurrentPoint, TimeAlpha);
		}

		const FVector PreviousDirection = PreviousSpan / PreviousLength;
		const FVector CurrentDirection = CurrentSpan / CurrentLength;
		const FQuat FrameRotation = FQuat::FindBetweenNormals(PreviousDirection, CurrentDirection);
		const FVector InterpolatedDirection = FQuat::Slerp(
			FQuat::Identity, FrameRotation, TimeAlpha).RotateVector(PreviousDirection).GetSafeNormal();
		const FVector InterpolatedCenter = FMath::Lerp(
			(PreviousStart + PreviousEnd) * 0.5f,
			(CurrentStart + CurrentEnd) * 0.5f,
			TimeAlpha);
		const float InterpolatedLength = FMath::Lerp(PreviousLength, CurrentLength, TimeAlpha);
		return InterpolatedCenter +
			InterpolatedDirection * ((SpatialAlpha - 0.5f) * InterpolatedLength);
	}
}

UMHGZAttackAbility::UMHGZAttackAbility()
{
	MaxCorrectionAngle = 30.0f;
}

#if WITH_EDITOR
EDataValidationResult UMHGZAttackAbility::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	bool bInvalid = false;
	auto AddError = [&Context, &bInvalid](const FString& Message)
	{
		Context.AddError(FText::FromString(Message));
		bInvalid = true;
	};

	for (int32 SegmentIndex = 0; SegmentIndex < AttackSegments.Num(); ++SegmentIndex)
	{
		const FAttackSegmentConfig& Segment = AttackSegments[SegmentIndex];
		const float MotionValue = Segment.Damage.MotionValue.GetValueAtLevel(1.0f);
		if (!FMath::IsFinite(MotionValue) || MotionValue < 0.0f)
		{
			AddError(FString::Printf(
				TEXT("AttackSegments[%d] must explicitly set MotionValue to 0 or a positive value."),
				SegmentIndex));
		}

		if (Segment.MultiHitCount < 1)
		{
			AddError(FString::Printf(TEXT("AttackSegments[%d].MultiHitCount must be at least 1."), SegmentIndex));
		}

		if (Segment.MultiHitPolicy == EAttackMultiHitPolicy::LockedTargetTicks)
		{
			if (Segment.MultiHitCount < 2)
			{
				AddError(FString::Printf(TEXT("AttackSegments[%d] uses LockedTargetTicks but has fewer than 2 hits."), SegmentIndex));
			}
			if (!FMath::IsFinite(Segment.MultiHitInterval) || Segment.MultiHitInterval <= 0.0f)
			{
				AddError(FString::Printf(TEXT("AttackSegments[%d] LockedTargetTicks interval must be positive."), SegmentIndex));
			}
			if (!FMath::IsFinite(Segment.LockedTargetMaxDistance) || Segment.LockedTargetMaxDistance <= 0.0f)
			{
				AddError(FString::Printf(TEXT("AttackSegments[%d] LockedTargetTicks distance must be positive."), SegmentIndex));
			}
		}

		for (int32 RegionIndex = 0; RegionIndex < Segment.Collision.TraceRegions.Num(); ++RegionIndex)
		{
			const FWeaponTraceRegion& Region = Segment.Collision.TraceRegions[RegionIndex];
			if (Region.EndSocketName.IsNone() || Region.Radius <= 0.0f
				|| Region.MaxSampleSpacing <= 0.0f || Region.MaxSampleCount <= 0
				|| Region.MaxAngularStepDegrees <= 0.0f)
			{
				AddError(FString::Printf(
					TEXT("AttackSegments[%d].TraceRegions[%d] has invalid socket or sampling values."),
					SegmentIndex, RegionIndex));
			}
		}
	}

	return bInvalid ? EDataValidationResult::Invalid
		: (Result == EDataValidationResult::NotValidated ? EDataValidationResult::Valid : Result);
}
#endif

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
	ActiveCollisionWindows.Empty();
	CurrentSegmentIndex = 0;
	bHasHitThisActivation = false;
	bHasActiveRootMotionTask = false;
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
	const FAttackSegmentConfig& Seg = AttackSegments[SegmentIndex];

	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!Character) return;
	USkeletalMeshComponent* Mesh = FindTraceMeshComponent(Seg.Collision);
	if (!Mesh)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Attack] Missing trace mesh/socket set. Segment=%d ComponentTag='%s'"),
			SegmentIndex, *Seg.Collision.TraceMeshComponentTag.ToString());
		return;
	}

	if (FCollisionWindowRuntimeState* ExistingState = ActiveCollisionWindows.Find(SegmentIndex))
	{
		Character->GetWorldTimerManager().ClearTimer(ExistingState->MultiHitTimer);
		ActiveCollisionWindows.Remove(SegmentIndex);
	}

	FCollisionWindowRuntimeState WindowState;
	auto AddRuntimeRegion = [&](const FWeaponTraceRegion& Region)
	{
		if (Region.EndSocketName.IsNone() || !Mesh->DoesSocketExist(Region.EndSocketName))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Attack] Invalid trace region end socket. Segment=%d Socket='%s' Mesh='%s'"),
				SegmentIndex, *Region.EndSocketName.ToString(), *GetNameSafe(Mesh));
			return;
		}
		if (!Region.StartSocketName.IsNone() && !Mesh->DoesSocketExist(Region.StartSocketName))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Attack] Invalid trace region start socket. Segment=%d Socket='%s' Mesh='%s'"),
				SegmentIndex, *Region.StartSocketName.ToString(), *GetNameSafe(Mesh));
			return;
		}

		FTraceRegionRuntimeState RuntimeRegion;
		RuntimeRegion.StartSocketName = Region.StartSocketName;
		RuntimeRegion.EndSocketName = Region.EndSocketName;
		RuntimeRegion.Radius = FMath::Max(1.0f, Region.Radius);
		RuntimeRegion.MaxSampleSpacing = FMath::Max(1.0f, Region.MaxSampleSpacing);
		RuntimeRegion.MaxSampleCount = FMath::Clamp(Region.MaxSampleCount, 1, 32);
		RuntimeRegion.MaxAngularStepDegrees = FMath::Clamp(
			Region.MaxAngularStepDegrees, 1.0f, 90.0f);
		RuntimeRegion.PreviousEnd = Mesh->GetSocketLocation(RuntimeRegion.EndSocketName);
		RuntimeRegion.PreviousStart = RuntimeRegion.StartSocketName.IsNone()
			? RuntimeRegion.PreviousEnd
			: Mesh->GetSocketLocation(RuntimeRegion.StartSocketName);

		const float RegionLength = FVector::Distance(
			RuntimeRegion.PreviousStart, RuntimeRegion.PreviousEnd);
		const float SafeSpacing = FMath::Min(
			RuntimeRegion.MaxSampleSpacing, RuntimeRegion.Radius * 2.0f);
		const int32 DesiredSamples = RegionLength <= KINDA_SMALL_NUMBER
			? 1
			: FMath::CeilToInt(RegionLength / SafeSpacing) + 1;
		if (DesiredSamples > RuntimeRegion.MaxSampleCount)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Attack] Trace region sample cap may leave gaps. Segment=%d Start='%s' End='%s' Desired=%d Cap=%d"),
				SegmentIndex, *RuntimeRegion.StartSocketName.ToString(),
				*RuntimeRegion.EndSocketName.ToString(), DesiredSamples,
				RuntimeRegion.MaxSampleCount);
		}
		WindowState.Regions.Add(MoveTemp(RuntimeRegion));
	};

	if (!Seg.Collision.TraceRegions.IsEmpty())
	{
		for (const FWeaponTraceRegion& Region : Seg.Collision.TraceRegions)
		{
			AddRuntimeRegion(Region);
		}
	}
	else
	{
		FWeaponTraceRegion LegacyRegion;
		LegacyRegion.StartSocketName = Seg.Collision.TraceStartSocketName;
		LegacyRegion.EndSocketName = ResolveLegacyTraceEndSocket(Seg.Collision);
		LegacyRegion.Radius = FMath::Max(1.0f, Seg.Collision.ShapeExtent.X);
		LegacyRegion.MaxSampleSpacing = LegacyRegion.Radius * 2.0f;
		LegacyRegion.MaxSampleCount = FMath::Clamp(Seg.Collision.TraceSampleCount, 1, 8);
		AddRuntimeRegion(LegacyRegion);
		if (!WindowState.Regions.IsEmpty())
		{
			FTraceRegionRuntimeState& RuntimeRegion = WindowState.Regions.Last();
			RuntimeRegion.FixedSampleCount = FMath::Clamp(Seg.Collision.TraceSampleCount, 1, 8);
			RuntimeRegion.LegacyShape = Seg.Collision.Shape;
			RuntimeRegion.LegacyShapeExtent = Seg.Collision.ShapeExtent;
			RuntimeRegion.bUseLegacyShape = true;
		}
	}

	if (WindowState.Regions.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Attack] Segment %d has no valid trace regions"), SegmentIndex);
		return;
	}

	ActiveCollisionWindows.Add(SegmentIndex, MoveTemp(WindowState));
	PerformSweepCheck(SegmentIndex); // 首帧零距离 Sweep，防止窗口开启时已与部位相交。
}

void UMHGZAttackAbility::DisableCollision(int32 SegmentIndex)
{
	if (SegmentIndex == INDEX_NONE)
	{
		TArray<int32> ActiveSegments;
		ActiveCollisionWindows.GetKeys(ActiveSegments);
		for (const int32 ActiveSegment : ActiveSegments)
		{
			DisableCollision(ActiveSegment);
		}
		return;
	}

	FCollisionWindowRuntimeState* WindowState = ActiveCollisionWindows.Find(SegmentIndex);
	if (!WindowState) return;

	if (ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		Character->GetWorldTimerManager().ClearTimer(WindowState->MultiHitTimer);
	}

	CurrentSegmentIndex = SegmentIndex;
	bool bEndForMiss = false;
	if (!bIsEndingAbility && AttackSegments.IsValidIndex(SegmentIndex))
	{
		const FAttackSegmentConfig& Seg = AttackSegments[SegmentIndex];
		if (Seg.Damage.bRequiresHitToContinue && WindowState->HitTargets.IsEmpty())
		{
			bEndForMiss = !ShouldContinueAfterHit();
		}
	}

	ActiveCollisionWindows.Remove(SegmentIndex);
	if (bEndForMiss && IsActive() && !bIsEndingAbility)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, false, true);
	}
}

void UMHGZAttackAbility::TickCollision(int32 SegmentIndex, float DeltaSeconds)
{
	(void)DeltaSeconds;
	if (ActiveCollisionWindows.Contains(SegmentIndex))
	{
		PerformSweepCheck(SegmentIndex);
	}
}

void UMHGZAttackAbility::PerformSweepCheck(int32 SegmentIndex)
{
	if (!AttackSegments.IsValidIndex(SegmentIndex)) return;
	FCollisionWindowRuntimeState* WindowState = ActiveCollisionWindows.Find(SegmentIndex);
	if (!WindowState) return;

	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!Character || !GetWorld()) return;

	const FAttackCollisionConfig& Collision = AttackSegments[SegmentIndex].Collision;
	USkeletalMeshComponent* Mesh = FindTraceMeshComponent(Collision);
	if (!Mesh) return;

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MHGZWeaponSweep), false, Character);
	QueryParams.AddIgnoredActor(Character);

	struct FBestFrameHit
	{
		FHitResult Hit;
		float FrameTime = TNumericLimits<float>::Max();
	};
	TMap<TWeakObjectPtr<AActor>, FBestFrameHit> BestHits;

	for (int32 RegionIndex = 0; RegionIndex < WindowState->Regions.Num(); ++RegionIndex)
	{
		FTraceRegionRuntimeState& RegionState = WindowState->Regions[RegionIndex];
		if (!Mesh->DoesSocketExist(RegionState.EndSocketName) ||
			(!RegionState.StartSocketName.IsNone() &&
				!Mesh->DoesSocketExist(RegionState.StartSocketName)))
		{
			continue;
		}

		const FVector CurrentEnd = Mesh->GetSocketLocation(RegionState.EndSocketName);
		const FVector CurrentStart = RegionState.StartSocketName.IsNone()
			? CurrentEnd
			: Mesh->GetSocketLocation(RegionState.StartSocketName);
		const float RegionLength = FVector::Distance(CurrentStart, CurrentEnd);
		const float SafeSpacing = FMath::Min(
			FMath::Max(1.0f, RegionState.MaxSampleSpacing), RegionState.Radius * 2.0f);
		const int32 DesiredSampleCount = RegionLength <= KINDA_SMALL_NUMBER
			? 1
			: FMath::CeilToInt(RegionLength / SafeSpacing) + 1;
		const int32 SampleCount = RegionState.FixedSampleCount > 0
			? RegionState.FixedSampleCount
			: FMath::Clamp(DesiredSampleCount, 1, RegionState.MaxSampleCount);

		const FVector PreviousSpan = RegionState.PreviousEnd - RegionState.PreviousStart;
		const FVector CurrentSpan = CurrentEnd - CurrentStart;
		float AngularDeltaDegrees = 0.0f;
		if (!PreviousSpan.IsNearlyZero() && !CurrentSpan.IsNearlyZero())
		{
			const float DirectionDot = FVector::DotProduct(
				PreviousSpan.GetSafeNormal(), CurrentSpan.GetSafeNormal());
			AngularDeltaDegrees = FMath::RadiansToDegrees(
				FMath::Acos(FMath::Clamp(DirectionDot, -1.0f, 1.0f)));
		}
		const int32 TemporalStepCount = FMath::Clamp(
			FMath::CeilToInt(AngularDeltaDegrees /
				FMath::Max(1.0f, RegionState.MaxAngularStepDegrees)), 1, 8);

		FCollisionShape QueryShape = FCollisionShape::MakeSphere(RegionState.Radius);
		FQuat QueryRotation = FQuat::Identity;
		if (RegionState.bUseLegacyShape)
		{
			switch (RegionState.LegacyShape)
			{
			case EAttackCollisionShape::Capsule:
				QueryShape = FCollisionShape::MakeCapsule(
					FMath::Max(1.0f, RegionState.LegacyShapeExtent.X),
					FMath::Max(RegionState.LegacyShapeExtent.X, RegionState.LegacyShapeExtent.Z));
				QueryRotation = Mesh->GetComponentQuat();
				break;
			case EAttackCollisionShape::Box:
				QueryShape = FCollisionShape::MakeBox(
					RegionState.LegacyShapeExtent.GetAbs().ComponentMax(FVector(1.0f)));
				QueryRotation = Mesh->GetComponentQuat();
				break;
			case EAttackCollisionShape::Sphere:
			default:
				break;
			}
		}

		for (int32 TemporalStep = 0; TemporalStep < TemporalStepCount; ++TemporalStep)
		{
			const float TimeStart = static_cast<float>(TemporalStep) / TemporalStepCount;
			const float TimeEnd = static_cast<float>(TemporalStep + 1) / TemporalStepCount;
			for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
			{
				const float SpatialAlpha = SampleCount == 1
					? 1.0f
					: static_cast<float>(SampleIndex) / static_cast<float>(SampleCount - 1);
				const FVector SweepFrom = EvaluateRotatingRegionPoint(
					RegionState.PreviousStart, RegionState.PreviousEnd,
					CurrentStart, CurrentEnd, SpatialAlpha, TimeStart);
				const FVector SweepTo = EvaluateRotatingRegionPoint(
					RegionState.PreviousStart, RegionState.PreviousEnd,
					CurrentStart, CurrentEnd, SpatialAlpha, TimeEnd);

				TArray<FHitResult> Hits;
				GetWorld()->SweepMultiByChannel(
					Hits, SweepFrom, SweepTo, QueryRotation,
					Collision.CollisionChannel, QueryShape, QueryParams);

				for (const FHitResult& Hit : Hits)
				{
					AActor* OtherActor = Hit.GetActor();
					const TWeakObjectPtr<AActor> ActorKey(OtherActor);
					if (!OtherActor || OtherActor == Character ||
						WindowState->HitTargets.Contains(ActorKey))
					{
						continue;
					}
					UMHGZMonsterHitzoneComponent* Hitzone =
						Cast<UMHGZMonsterHitzoneComponent>(Hit.GetComponent());
					if (!Hitzone || (Collision.HitzoneQueryTag.IsValid() &&
						!Hitzone->HitzoneTag.MatchesTagExact(Collision.HitzoneQueryTag)))
					{
						continue;
					}

					const float FrameTime =
						(static_cast<float>(TemporalStep) + Hit.Time) / TemporalStepCount;
					FBestFrameHit* Existing = BestHits.Find(ActorKey);
					if (!Existing || FrameTime < Existing->FrameTime)
					{
						FBestFrameHit Candidate;
						Candidate.Hit = Hit;
						Candidate.FrameTime = FrameTime;
						BestHits.Add(ActorKey, MoveTemp(Candidate));
					}
				}

				if (Collision.bDrawDebug)
				{
					DrawDebugLine(GetWorld(), SweepFrom, SweepTo, FColor::Red,
						false, 0.15f, 0, 1.5f);
					DrawDebugSphere(GetWorld(), SweepTo, RegionState.Radius, 12,
						Hits.IsEmpty() ? FColor::Yellow : FColor::Green, false, 0.15f);
				}
			}
		}

		if (Collision.bDrawDebug)
		{
			DrawDebugLine(GetWorld(), CurrentStart, CurrentEnd, FColor::Cyan,
				false, 0.15f, 0, 2.0f);
		}
		RegionState.PreviousStart = CurrentStart;
		RegionState.PreviousEnd = CurrentEnd;
	}

	for (const TPair<TWeakObjectPtr<AActor>, FBestFrameHit>& Pair : BestHits)
	{
		ProcessSweepHit(Pair.Value.Hit, SegmentIndex);
		if (!ActiveCollisionWindows.Contains(SegmentIndex))
		{
			return;
		}
	}
}

void UMHGZAttackAbility::ProcessSweepHit(const FHitResult& Hit, int32 SegmentIndex)
{
	FCollisionWindowRuntimeState* WindowState = ActiveCollisionWindows.Find(SegmentIndex);
	if (!WindowState) return;

	AActor* OtherActor = Hit.GetActor();
	const TWeakObjectPtr<AActor> ActorKey(OtherActor);
	if (!OtherActor || OtherActor == GetAvatarActorFromActorInfo() ||
		WindowState->HitTargets.Contains(ActorKey))
	{
		return;
	}

	UMHGZMonsterHitzoneComponent* Hitzone = Cast<UMHGZMonsterHitzoneComponent>(Hit.GetComponent());
	if (!Hitzone) return;

	if (AttackSegments.IsValidIndex(SegmentIndex))
	{
		const FGameplayTag& QueryTag = AttackSegments[SegmentIndex].Collision.HitzoneQueryTag;
		if (QueryTag.IsValid() && !Hitzone->HitzoneTag.MatchesTagExact(QueryTag))
		{
			return;
		}
	}

	WindowState->HitTargets.Add(ActorKey, Hitzone->BoneName);
	ApplyDamage(OtherActor, Hitzone->BoneName, SegmentIndex);

	// GE/GameplayCue 回调可能同步结束 Ability；重新按 Index 查找，避免使用失效引用。
	if (ActiveCollisionWindows.Contains(SegmentIndex))
	{
		StartMultiHitTimerIfNeeded(SegmentIndex);
	}
}

void UMHGZAttackAbility::StartMultiHitTimerIfNeeded(int32 SegmentIndex)
{
	if (!AttackSegments.IsValidIndex(SegmentIndex)) return;
	FCollisionWindowRuntimeState* WindowState = ActiveCollisionWindows.Find(SegmentIndex);
	if (!WindowState) return;
	const FAttackSegmentConfig& Seg = AttackSegments[SegmentIndex];
	if (Seg.MultiHitCount <= 1 || WindowState->MultiHitCurrentCount > 0) return;

	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!Character) return;

	WindowState->MultiHitCurrentCount = 1; // 首次接触已立即结算第一跳。
	FTimerDelegate MultiHitDelegate;
	MultiHitDelegate.BindUObject(this, &UMHGZAttackAbility::OnMultiHitTick, SegmentIndex);
	Character->GetWorldTimerManager().SetTimer(
		WindowState->MultiHitTimer, MultiHitDelegate,
		FMath::Max(0.01f, Seg.MultiHitInterval), true);
}

void UMHGZAttackAbility::OnMultiHitTick(int32 SegmentIndex)
{
	if (!AttackSegments.IsValidIndex(SegmentIndex)) return;
	FCollisionWindowRuntimeState* WindowState = ActiveCollisionWindows.Find(SegmentIndex);
	if (!WindowState) return;
	const FAttackSegmentConfig& Seg = AttackSegments[SegmentIndex];

	TArray<TPair<TWeakObjectPtr<AActor>, FName>> HitTargetSnapshot;
	HitTargetSnapshot.Reserve(WindowState->HitTargets.Num());
	for (const TPair<TWeakObjectPtr<AActor>, FName>& Pair : WindowState->HitTargets)
	{
		HitTargetSnapshot.Add(Pair);
	}

	for (const TPair<TWeakObjectPtr<AActor>, FName>& Pair : HitTargetSnapshot)
	{
		if (AActor* Target = Pair.Key.Get())
		{
			ApplyDamage(Target, Pair.Value, SegmentIndex);
			if (!ActiveCollisionWindows.Contains(SegmentIndex))
			{
				return;
			}
		}
	}

	WindowState = ActiveCollisionWindows.Find(SegmentIndex);
	if (!WindowState) return;
	WindowState->MultiHitCurrentCount++;
	if (WindowState->MultiHitCurrentCount >= Seg.MultiHitCount)
	{
		ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
		if (Character)
		{
			Character->GetWorldTimerManager().ClearTimer(WindowState->MultiHitTimer);
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
	const FCollisionWindowRuntimeState* WindowState =
		ActiveCollisionWindows.Find(CurrentSegmentIndex);
	if (Seg.Damage.bRequiresHitToContinue &&
		(!WindowState || WindowState->HitTargets.IsEmpty()))
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
	if (!Character) return nullptr;

	TArray<USkeletalMeshComponent*> MeshComponents;
	Character->GetComponents<USkeletalMeshComponent>(MeshComponents);

	// 显式标记的武器组件优先，避免角色主 Mesh 上的同名 Socket 被误选。
	if (!Collision.TraceMeshComponentTag.IsNone())
	{
		for (USkeletalMeshComponent* Mesh : MeshComponents)
		{
			if (Mesh && Mesh->ComponentHasTag(Collision.TraceMeshComponentTag) &&
				HasRequiredTraceSockets(Mesh, Collision))
			{
				return Mesh;
			}
		}
	}

	// 兼容旧配置：优先尝试角色主骨骼网格。
	if (USkeletalMeshComponent* CharacterMesh = Character->GetMesh())
	{
		if (HasRequiredTraceSockets(CharacterMesh, Collision))
		{
			return CharacterMesh;
		}
	}

	// 最后回退到任意拥有目标 Socket 的骨骼网格组件。
	for (USkeletalMeshComponent* Mesh : MeshComponents)
	{
		if (Mesh && HasRequiredTraceSockets(Mesh, Collision))
		{
			return Mesh;
		}
	}

	return nullptr;
}

bool UMHGZAttackAbility::HasRequiredTraceSockets(
	const USkeletalMeshComponent* Mesh,
	const FAttackCollisionConfig& Collision) const
{
	if (!Mesh) return false;

	if (!Collision.TraceRegions.IsEmpty())
	{
		bool bHasAtLeastOneRegion = false;
		for (const FWeaponTraceRegion& Region : Collision.TraceRegions)
		{
			if (Region.EndSocketName.IsNone())
			{
				continue;
			}
			bHasAtLeastOneRegion = true;
			if (!Mesh->DoesSocketExist(Region.EndSocketName) ||
				(!Region.StartSocketName.IsNone() &&
					!Mesh->DoesSocketExist(Region.StartSocketName)))
			{
				return false;
			}
		}
		return bHasAtLeastOneRegion;
	}

	const FName EndSocketName = ResolveLegacyTraceEndSocket(Collision);
	return !EndSocketName.IsNone() && Mesh->DoesSocketExist(EndSocketName) &&
		(Collision.TraceStartSocketName.IsNone() ||
			Mesh->DoesSocketExist(Collision.TraceStartSocketName));
}
