// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZAttackAbility.h"
#include "MHGZAbilitySystemComponent.h"
#include "MHGZComboCoordinatorAbility.h"
#include "MHGZGameplayEffectContext.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"
#include "AbilitySystemGlobals.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Character.h"
#include "MHGZCharacter.h"
#include "Camera/CameraShakeBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

namespace
{
	/** Shared direct-Yaw correction used by exact in-action attack notifies. */
	bool ApplyDirectYawCorrection(ACharacter& Character, const FVector& WorldDirection,
		float MaxCorrectionAngle)
	{
		if (!FMath::IsFinite(MaxCorrectionAngle) || MaxCorrectionAngle <= 0.f)
		{
			return false;
		}

		const FVector InputDir(WorldDirection.X, WorldDirection.Y, 0.f);
		if (InputDir.IsNearlyZero())
		{
			return false;
		}

		const FVector CharacterForward = Character.GetActorForwardVector();
		const FVector CharacterForward2D(CharacterForward.X, CharacterForward.Y, 0.f);
		if (CharacterForward2D.IsNearlyZero())
		{
			return false;
		}

		const FVector NormalizedInput = InputDir.GetSafeNormal();
		const FVector NormalizedForward = CharacterForward2D.GetSafeNormal();
		const float Dot = FVector::DotProduct(NormalizedInput, NormalizedForward);
		const float AngleDegrees = FMath::RadiansToDegrees(
			FMath::Acos(FMath::Clamp(Dot, -1.f, 1.f)));
		if (AngleDegrees > MaxCorrectionAngle)
		{
			return false;
		}

		FRotator CorrectedRotation = Character.GetActorRotation();
		CorrectedRotation.Yaw = NormalizedInput.Rotation().Yaw;
		Character.SetActorRotation(CorrectedRotation);
		return true;
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
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerExecution;
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
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!IsActionActivationCommitted()) return;
	ACharacter* Character = Cast<ACharacter>(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr);
	UAnimInstance* AnimInstance = Character && Character->GetMesh()
		? Character->GetMesh()->GetAnimInstance() : nullptr;

	FGameplayTagContainer ActionTags;
	ActionTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Attacking")));
	ActionTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.BlockMovement")));
	AcquireActionTags(ActionTags, FName(TEXT("AttackState")));

	// 重置状态
	ActiveCollisionWindows.Empty();
	CurrentSegmentIndex = 0;
	bHasHitThisActivation = false;
	bHasActiveRootMotionTask = false;
	bIsEndingAbility = false;
	// 每次激活生成稳定攻击身份：一次激活内所有伤害/多跳/反馈共享同一 ID。
	ActivationAttackInstanceID = FGuid::NewGuid();
	// Action 已 Confirm；普通攻击在 Montage 播放前只读取一次冻结输入方向。
	ApplyDirectionCorrection();
	if (!PrepareAttackMontage())
	{
		RequestEndAction(EWeaponActionEndReason::Cancelled);
		return;
	}

	ActiveAttackMontage = AttackMontage;
	MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, FName(TEXT("AttackMontage")), AttackMontage, 1.0f);
	if (!MontageTask)
	{
		RequestEndAction(EWeaponActionEndReason::Cancelled);
		return;
	}

	MontageTask->OnCompleted.AddDynamic(this, &UMHGZAttackAbility::OnMontageCompleted);
	MontageTask->OnInterrupted.AddDynamic(this, &UMHGZAttackAbility::OnMontageInterrupted);
	MontageTask->OnCancelled.AddDynamic(this, &UMHGZAttackAbility::OnMontageInterrupted);
	MontageTask->ReadyForActivation();

	FAnimMontageInstance* MontageInstance = AnimInstance->GetActiveInstanceForMontage(AttackMontage);
	if (!MontageInstance
		|| !RegisterMontageInstance(Character->GetMesh(), MontageInstance->GetInstanceID()))
	{
		RequestEndAction(EWeaponActionEndReason::Interrupted);
	}
}

bool UMHGZAttackAbility::ValidateActionDependencies() const
{
	const ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	return Character && Character->GetMesh() && Character->GetMesh()->GetAnimInstance()
		&& AttackMontage;
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
	PendingDodgeSuperseder = FWeaponActionToken();
	CloseAllDodgeAcceptWindows();

	// 关碰撞（幂等安全——清除全部窗口与每目标 LockedTarget Timer）
	DisableCollision();

	// AbilityTask 会在 Ability 结束时负责停止 Montage 并解绑委托。
	MontageTask = nullptr;
	ActiveAttackMontage = nullptr;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

bool UMHGZAttackAbility::BeginDodgeAcceptWindow(FName NotifyEventID)
{
	if (!IsActive() || bIsEndingAbility || !IsActionActivationCommitted()
		|| NotifyEventID.IsNone())
	{
		return false;
	}
	if (DodgeAcceptWindowTokens.Contains(NotifyEventID))
	{
		return true;
	}

	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	const FWeaponActionToken& ActionToken = GetActionToken();
	if (!Host || !ActionToken.IsValid()
		|| !Host->IsTokenCurrent(ActionToken.RuntimeToken))
	{
		return false;
	}

	FGameplayTagContainer Tags;
	Tags.AddTag(FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.DodgeAcceptOpen")));
	FWeaponOwnedTagToken WindowToken = Host->AcquireTags(
		EWeaponTagOwnerKind::NotifyWindow,
		ActionToken.AbilityHandle,
		ActionToken.ActivationSequenceID,
		NotifyEventID,
		Tags);
	if (!WindowToken.IsValid())
	{
		return false;
	}

	DodgeAcceptWindowTokens.Add(NotifyEventID, WindowToken);
	return true;
}

void UMHGZAttackAbility::EndDodgeAcceptWindow(FName NotifyEventID)
{
	FWeaponOwnedTagToken* WindowToken = DodgeAcceptWindowTokens.Find(NotifyEventID);
	if (!WindowToken)
	{
		return;
	}
	if (UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost())
	{
		Host->ReleaseTags(*WindowToken);
	}
	DodgeAcceptWindowTokens.Remove(NotifyEventID);
}

bool UMHGZAttackAbility::HasOpenDodgeAcceptWindow(
	const FWeaponActionToken& ActionToken) const
{
	return IsActive() && !bIsEndingAbility && IsActionActivationCommitted()
		&& ActionToken == GetActionToken()
		&& !DodgeAcceptWindowTokens.IsEmpty();
}

bool UMHGZAttackAbility::PrepareDodgeSupersede(
	const FWeaponActionToken& DodgeActionToken)
{
	if (!DodgeActionToken.IsValid()
		|| !HasOpenDodgeAcceptWindow(GetActionToken()))
	{
		return false;
	}
	if (PendingDodgeSuperseder.IsValid())
	{
		return PendingDodgeSuperseder == DodgeActionToken;
	}
	PendingDodgeSuperseder = DodgeActionToken;
	return true;
}

bool UMHGZAttackAbility::CommitDodgeSupersede(
	const FWeaponActionToken& DodgeActionToken)
{
	if (PendingDodgeSuperseder != DodgeActionToken
		|| !IsActive() || bIsEndingAbility || !IsActionActivationCommitted())
	{
		return false;
	}
	PendingDodgeSuperseder = FWeaponActionToken();
	RequestEndAction(EWeaponActionEndReason::Superseded);
	return GetActionEndReason() == EWeaponActionEndReason::Superseded;
}

void UMHGZAttackAbility::CancelDodgeSupersede(
	const FWeaponActionToken& DodgeActionToken)
{
	if (PendingDodgeSuperseder != DodgeActionToken)
	{
		return;
	}
	PendingDodgeSuperseder = FWeaponActionToken();

	// A dependency/task failure occurs before Montage_Play and leaves the attack
	// untouched. If playback began but exact registration failed, the old montage
	// may already have been interrupted by UE; it cannot be resumed safely.
	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	UAnimInstance* AnimInstance = Character && Character->GetMesh()
		? Character->GetMesh()->GetAnimInstance()
		: nullptr;
	if (ActiveAttackMontage && AnimInstance
		&& !AnimInstance->Montage_IsPlaying(ActiveAttackMontage))
	{
		RequestEndAction(EWeaponActionEndReason::Interrupted);
	}
}

void UMHGZAttackAbility::CloseAllDodgeAcceptWindows()
{
	if (UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost())
	{
		for (TPair<FName, FWeaponOwnedTagToken>& Pair : DodgeAcceptWindowTokens)
		{
			Host->ReleaseTags(Pair.Value);
		}
	}
	DodgeAcceptWindowTokens.Reset();
}

void UMHGZAttackAbility::OnMontageCompleted()
{
	if (IsActive() && !bIsEndingAbility)
	{
		RequestEndAction(EWeaponActionEndReason::Normal);
	}
}

void UMHGZAttackAbility::OnMontageInterrupted()
{
	if (IsActive() && !bIsEndingAbility)
	{
		if (PendingDodgeSuperseder.IsValid())
		{
			return;
		}
		RequestEndAction(EWeaponActionEndReason::Interrupted);
	}
}

void UMHGZAttackAbility::ApplyDirectionCorrection()
{
	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!Character)
	{
		return;
	}

	// Direction is frozen during input resolution; activation never re-reads the stick.
	ApplyDirectYawCorrection(*Character,
		GetWeaponActivationContext().Input.WorldDirection, MaxCorrectionAngle);
}

bool UMHGZAttackAbility::ApplyInActionDirectionCorrection(
	const FWeaponActionToken& ActionToken, float MaxCorrectionAngleOverride)
{
	if (!IsActive() || bIsEndingAbility || !IsActionActivationCommitted()
		|| ActionToken != GetActionToken())
	{
		return false;
	}

	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	if (!Host || !Host->IsTokenCurrent(ActionToken.RuntimeToken))
	{
		return false;
	}

	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!Character)
	{
		return false;
	}

	const float CorrectionAngle = MaxCorrectionAngleOverride < 0.f
		? MaxCorrectionAngle
		: MaxCorrectionAngleOverride;
	return ApplyDirectYawCorrection(*Character,
		GetInActionCorrectionDirection(), CorrectionAngle);
}

FVector UMHGZAttackAbility::GetInActionCorrectionDirection() const
{
	if (const AMHGZCharacter* Character = Cast<AMHGZCharacter>(GetAvatarActorFromActorInfo()))
	{
		return Character->GetLastMovementInputDir();
	}
	return FVector::ZeroVector;
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
		for (TPair<TWeakObjectPtr<AActor>, FHitTargetRuntimeState>& Pair : ExistingState->HitTargets)
		{
			Character->GetWorldTimerManager().ClearTimer(Pair.Value.TickTimer);
		}
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

	// 运行时只接受非空 TraceRegions；旧 Collision/Socket 字段只作序列化壳，不参与决策。
	if (Seg.Collision.TraceRegions.IsEmpty())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Attack] Segment %d has no TraceRegions configured; collision window will not open (legacy fields are no longer read)."),
			SegmentIndex);
		return;
	}

	for (const FWeaponTraceRegion& Region : Seg.Collision.TraceRegions)
	{
		AddRuntimeRegion(Region);
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
		for (TPair<TWeakObjectPtr<AActor>, FHitTargetRuntimeState>& Pair : WindowState->HitTargets)
		{
			Character->GetWorldTimerManager().ClearTimer(Pair.Value.TickTimer);
		}
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
		const int32 SampleCount =
			FMath::Clamp(DesiredSampleCount, 1, RegionState.MaxSampleCount);

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

		// 新 TraceRegions 全部按球形 Sweep 结算；旧 Shape/ShapeExtent 不参与决策。
		const FCollisionShape QueryShape = FCollisionShape::MakeSphere(RegionState.Radius);
		const FQuat QueryRotation = FQuat::Identity;

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

				// 同帧多 Region/时间子步只保留每 Actor 帧时间最早的命中。
				for (const FHitResult& Hit : Hits)
				{
					AActor* OtherActor = Hit.GetActor();
					const TWeakObjectPtr<AActor> ActorKey(OtherActor);
					if (!OtherActor || OtherActor == Character)
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
	if (!OtherActor || OtherActor == GetAvatarActorFromActorInfo())
	{
		return;
	}

	UMHGZMonsterHitzoneComponent* Hitzone = Cast<UMHGZMonsterHitzoneComponent>(Hit.GetComponent());
	if (!Hitzone) return;

	if (!AttackSegments.IsValidIndex(SegmentIndex)) return;
	const FAttackSegmentConfig& Seg = AttackSegments[SegmentIndex];
	{
		const FGameplayTag& QueryTag = Seg.Collision.HitzoneQueryTag;
		if (QueryTag.IsValid() && !Hitzone->HitzoneTag.MatchesTagExact(QueryTag))
		{
			return;
		}
	}

	UWorld* World = GetWorld();
	if (!World) return;

	// 每目标独立 Count + LastHitTime：ContactOnly 只靠真实后续 Sweep 接触重复。
	FHitTargetRuntimeState& TargetState = WindowState->HitTargets.FindOrAdd(ActorKey);
	if (TargetState.HitCount >= Seg.MultiHitCount)
	{
		return;
	}
	const float Now = World->GetTimeSeconds();
	if (TargetState.HitCount > 0 &&
		(Now - TargetState.LastHitTime) < Seg.MultiHitInterval)
	{
		return;
	}
	// LockedTargetTicks 的后续跳数由每目标 Timer 独占，避免真实 Sweep 重复触发双跳。
	if (Seg.MultiHitPolicy == EAttackMultiHitPolicy::LockedTargetTicks &&
		TargetState.TickTimer.IsValid())
	{
		return;
	}

	TargetState.TargetActor = OtherActor;
	TargetState.Hitzone = Hitzone;
	TargetState.LastHit = Hit;
	TargetState.HitCount++;
	TargetState.LastHitTime = Now;

	ApplyDamage(Hit, SegmentIndex);

	// GE 回调可能同步结束 Ability；重新按 Index 查找，避免使用失效引用。
	if (ActiveCollisionWindows.Contains(SegmentIndex))
	{
		// 只有显式 LockedTargetTicks 才启动缓存目标 Timer；ContactOnly 绝不启动。
		if (Seg.MultiHitPolicy == EAttackMultiHitPolicy::LockedTargetTicks)
		{
			StartLockedTargetTickTimer(SegmentIndex, ActorKey);
		}
	}
}

void UMHGZAttackAbility::StartLockedTargetTickTimer(
	int32 SegmentIndex, const TWeakObjectPtr<AActor>& TargetKey)
{
	if (!AttackSegments.IsValidIndex(SegmentIndex)) return;
	FCollisionWindowRuntimeState* WindowState = ActiveCollisionWindows.Find(SegmentIndex);
	if (!WindowState) return;
	const FAttackSegmentConfig& Seg = AttackSegments[SegmentIndex];
	FHitTargetRuntimeState* TargetState = WindowState->HitTargets.Find(TargetKey);
	if (!TargetState) return;

	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!Character) return;

	if (TargetState->HitCount >= Seg.MultiHitCount || TargetState->TickTimer.IsValid())
	{
		return;
	}

	FTimerDelegate TickDelegate;
	TickDelegate.BindUObject(
		this, &UMHGZAttackAbility::OnLockedTargetTick, SegmentIndex, TargetKey);
	Character->GetWorldTimerManager().SetTimer(
		TargetState->TickTimer, TickDelegate,
		FMath::Max(0.01f, Seg.MultiHitInterval), true);
}

void UMHGZAttackAbility::OnLockedTargetTick(
	int32 SegmentIndex, TWeakObjectPtr<AActor> TargetKey)
{
	if (!AttackSegments.IsValidIndex(SegmentIndex)) return;
	FCollisionWindowRuntimeState* WindowState = ActiveCollisionWindows.Find(SegmentIndex);
	if (!WindowState) return;
	const FAttackSegmentConfig& Seg = AttackSegments[SegmentIndex];
	FHitTargetRuntimeState* TargetState = WindowState->HitTargets.Find(TargetKey);
	if (!TargetState) return;

	// 每跳重验：弱引用、Hitzone 组件仍有效、目标/部位存活、攻击者到部位距离。
	AActor* Target = TargetState->TargetActor.Get();
	UMHGZMonsterHitzoneComponent* Hitzone = TargetState->Hitzone.Get();
	AActor* Source = GetAvatarActorFromActorInfo();
	if (!Target || !Hitzone || !IsValid(Hitzone) || !Hitzone->IsRegistered() ||
		!IsValid(Target) || !IsTargetAlive(Target) || !Source)
	{
		StopLockedTarget(SegmentIndex, TargetKey);
		return;
	}

	const FVector HitzoneLocation = Hitzone->GetComponentLocation();
	if (FVector::Dist(Source->GetActorLocation(), HitzoneLocation) >
		Seg.LockedTargetMaxDistance)
	{
		// 离区：停止该目标（清 Timer 并移除状态；再次接触按新目标重新计数）。
		StopLockedTarget(SegmentIndex, TargetKey);
		return;
	}

	if (TargetState->HitCount >= Seg.MultiHitCount)
	{
		FinishLockedTarget(SegmentIndex, TargetKey);
		return;
	}

	// 用当前部位位置刷新真实 HitResult，反馈位置跟随部位。
	FHitResult TickHit = TargetState->LastHit;
	TickHit.Location = HitzoneLocation;
	TickHit.ImpactPoint = HitzoneLocation;
	TickHit.Component = Hitzone;
	TargetState->LastHit = TickHit;
	TargetState->HitCount++;

	UWorld* World = GetWorld();
	TargetState->LastHitTime = World ? World->GetTimeSeconds() : 0.f;

	ApplyDamage(TickHit, SegmentIndex);

	// Apply 后 Ability 可能已结束；重新获取状态。
	WindowState = ActiveCollisionWindows.Find(SegmentIndex);
	TargetState = WindowState ? WindowState->HitTargets.Find(TargetKey) : nullptr;
	if (TargetState && TargetState->HitCount >= Seg.MultiHitCount)
	{
		FinishLockedTarget(SegmentIndex, TargetKey);
	}
}

void UMHGZAttackAbility::FinishLockedTarget(
	int32 SegmentIndex, const TWeakObjectPtr<AActor>& TargetKey)
{
	// 次数耗尽：只停 Timer，保留状态条目（HitCount 已满）用于后续 Sweep 去重，
	// 避免目标仍在接触时重新开链无限跳伤。
	FCollisionWindowRuntimeState* WindowState = ActiveCollisionWindows.Find(SegmentIndex);
	if (!WindowState) return;
	FHitTargetRuntimeState* TargetState = WindowState->HitTargets.Find(TargetKey);
	if (!TargetState) return;

	if (ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		Character->GetWorldTimerManager().ClearTimer(TargetState->TickTimer);
	}
	TargetState->TickTimer.Invalidate();
}

void UMHGZAttackAbility::StopLockedTarget(
	int32 SegmentIndex, const TWeakObjectPtr<AActor>& TargetKey)
{
	FCollisionWindowRuntimeState* WindowState = ActiveCollisionWindows.Find(SegmentIndex);
	if (!WindowState) return;
	FHitTargetRuntimeState* TargetState = WindowState->HitTargets.Find(TargetKey);
	if (!TargetState) return;

	if (ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		Character->GetWorldTimerManager().ClearTimer(TargetState->TickTimer);
	}
	WindowState->HitTargets.Remove(TargetKey);
}

bool UMHGZAttackAbility::IsTargetAlive(AActor* Target) const
{
	if (!IsValid(Target))
	{
		return false;
	}

	UAbilitySystemComponent* TargetASC =
		UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target);
	if (!TargetASC)
	{
		return true;
	}

	if (TargetASC->HasMatchingGameplayTag(
		FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Dead"))))
	{
		return false;
	}

	if (TargetASC->HasAttributeSetForAttribute(UMHGZAttributeSet::GetHealthAttribute()))
	{
		if (TargetASC->GetNumericAttribute(UMHGZAttributeSet::GetHealthAttribute()) <= 0.f)
		{
			return false;
		}
	}
	return true;
}

void UMHGZAttackAbility::ApplyDamage(const FHitResult& Hit, int32 SegmentIndex)
{
	if (!AttackSegments.IsValidIndex(SegmentIndex)) return;

	AActor* Target = Hit.GetActor();
	if (!Target) return;

	UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo();
	if (!SourceASC) return;

	// 构造并 Apply GE
	FGameplayEffectSpecHandle Spec = MakeDamageSpec(Hit, SegmentIndex);
	UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target);
	bool bApplied = false;
	if (Spec.IsValid() && TargetASC)
	{
		bApplied = SourceASC->ApplyGameplayEffectSpecToTarget(
			*Spec.Data, TargetASC).WasSuccessfullyApplied();
	}

	// 首次命中逻辑（成功 Apply 后）：通知协调器 + 施加 OnHitSelfEffect。
	const FAttackSegmentConfig& Seg = AttackSegments[SegmentIndex];
	if (bApplied && !bHasHitThisActivation)
	{
		bHasHitThisActivation = true;
		NotifyCoordinatorFirstHit();

		// Apply OnHitSelfEffect
		if (Seg.Damage.OnHitSelfEffect)
		{
			SourceASC->ApplyGameplayEffectToSelf(
				Seg.Damage.OnHitSelfEffect->GetDefaultObject<UGameplayEffect>(),
				1.0f, SourceASC->MakeEffectContext());
		}
	}
}

FGameplayEffectSpecHandle UMHGZAttackAbility::MakeDamageSpec(
	const FHitResult& Hit, int32 SegmentIndex)
{
	if (!AttackSegments.IsValidIndex(SegmentIndex))
	{
		return FGameplayEffectSpecHandle();
	}

	const FAttackSegmentConfig& Seg = AttackSegments[SegmentIndex];
	if (!Seg.Damage.DamageEffectClass) return FGameplayEffectSpecHandle();

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC) return FGameplayEffectSpecHandle();

	// 自定义 EffectContext 写满真实 HitResult/攻击身份/Cue/反馈参数。
	FGameplayEffectContextHandle ContextHandle = ASC->MakeEffectContext();
	FMHGZGameplayEffectContext* Context =
		FMHGZGameplayEffectContext::ExtractEffectContext(ContextHandle);
	if (!Context)
	{
		return FGameplayEffectSpecHandle();
	}
	else
	{
		Context->AddHitResult(Hit, true);
		Context->AttackInstanceID = ActivationAttackInstanceID;
		Context->SourceActionTag = InputTag; // 输入标签即动作身份
		Context->DamageSourceType = EMHGZDamageSourceType::Weapon;
		Context->bUseHitzoneDefense = Seg.Damage.bUseHitzoneDefense;
		Context->HitStaggerTag = Seg.Damage.HitStaggerTag;
		Context->HitCueTag = Seg.Damage.HitCueTag;
		Context->ElementCueTag = Seg.Damage.ElementalCueTag;
		Context->HitStopDuration = Seg.Damage.HitStopBase.GetValueAtLevel(GetAbilityLevel());
		Context->CameraShakeClass = Seg.Damage.CameraShakeClass;
		Context->CameraShakeScale = Seg.Damage.CameraShakeScale;
		if (const UMHGZMonsterHitzoneComponent* Hitzone =
			Cast<UMHGZMonsterHitzoneComponent>(Hit.GetComponent()))
		{
			Context->HitzoneTag = Hitzone->HitzoneTag;
		}
		if (AActor* Source = GetAvatarActorFromActorInfo())
		{
			Context->AddInstigator(Source, Source);
		}
	}

	FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(
		Seg.Damage.DamageEffectClass, 1.0f, ContextHandle);

	if (!Spec.IsValid()) return Spec;

	// 动作值
	Spec.Data->SetSetByCallerMagnitude(
		FGameplayTag::RequestGameplayTag(TEXT("Damage.MotionValue")),
		Seg.Damage.MotionValue.GetValueAtLevel(GetAbilityLevel()));

	// 基础破坏值
	Spec.Data->SetSetByCallerMagnitude(
		FGameplayTag::RequestGameplayTag(TEXT("Damage.BaseStagger")),
		Seg.Damage.BaseStaggerValue.GetValueAtLevel(GetAbilityLevel()));

	// DynamicAssetTag 只作调试镜像；结算与反馈以 Context 为真相源。
	const UMHGZMonsterHitzoneComponent* Hitzone =
		Cast<UMHGZMonsterHitzoneComponent>(Hit.GetComponent());
	if (Seg.Damage.bUseHitzoneDefense && Hitzone && Hitzone->HitzoneTag.IsValid())
	{
		Spec.Data->AddDynamicAssetTag(Hitzone->HitzoneTag);
	}

	if (Seg.Damage.HitStaggerTag.IsValid())
	{
		Spec.Data->AddDynamicAssetTag(Seg.Damage.HitStaggerTag);
	}

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

	return Spec;
}

FGameplayEffectSpecHandle UMHGZAttackAbility::MakeDamageSpec(
	AActor* Target, FName HitzoneBoneName, int32 SegmentIndex)
{
	(void)Target;
	(void)HitzoneBoneName;
	// 旧签名序列化兼容壳：运行时攻击链路只走 FHitResult 版本。
	UE_LOG(LogTemp, Warning,
		TEXT("[Attack] Legacy MakeDamageSpec(Actor, Bone) is not supported at runtime; returning an invalid spec. Segment=%d"),
		SegmentIndex);
	return FGameplayEffectSpecHandle();
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

void UMHGZAttackAbility::NotifyCoordinatorFirstHit()
{
	const FWeaponActionToken CallbackActionToken = GetActionToken();
	if (!CallbackActionToken.IsValid()) return;
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC) return;

	UMHGZAbilitySystemComponent* MHGZASC = Cast<UMHGZAbilitySystemComponent>(ASC);
	if (MHGZASC)
	{
		if (UGA_WeaponComboCoordinator* Coord = MHGZASC->GetActiveComboCoordinator())
		{
			Coord->OnAttackHit(CallbackActionToken);
		}
	}
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

	// 运行时只接受非空 TraceRegions；无配置返回 false，由调用方干净失败。
	if (Collision.TraceRegions.IsEmpty())
	{
		return false;
	}

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
