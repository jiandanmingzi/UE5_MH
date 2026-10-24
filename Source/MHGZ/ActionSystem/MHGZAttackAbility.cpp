// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZAttackAbility.h"
#include "MHGZAbilitySystemComponent.h"
#include "MHGZComboCoordinatorAbility.h"
#include "MHGZGameplayEffectContext.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"
#include "AbilitySystemGlobals.h"
#include "AbilityTask_MHGZActionDirectionCorrection.h"
#include "AbilityTask_MHGZPlayMontageAndWait.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Character.h"
#include "MHGZCharacter.h"
#include "Camera/CameraShakeBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"
#include "WeaponRuntime/MHGZWeaponCombatConfig.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#endif

FCollisionShape FWeaponTraceRegion::MakeQueryShape() const
{
	const float SafeRadius = FMath::IsFinite(Radius) ? FMath::Max(1.0f, Radius) : 14.0f;
	return FMath::IsFinite(HorizontalHalfWidth) && HorizontalHalfWidth > SafeRadius
		? FCollisionShape::MakeCapsule(SafeRadius, HorizontalHalfWidth)
		: FCollisionShape::MakeSphere(SafeRadius);
}

USoundBase* UMHGZAttackAbility::GetLegacySwingSound(int32 SegmentIndex) const
{
	return AttackSegments.IsValidIndex(SegmentIndex)
		? AttackSegments[SegmentIndex].Damage.SwingSound.Get() : nullptr;
}

namespace
{
	constexpr float MaxCorrectionAngleOvershootToleranceDegrees = 90.0f;

	/** Resolve the existing angle-cap/overshoot rule without mutating the Character. */
	bool ResolveDirectYawCorrectionDelta(const ACharacter& Character,
		const FVector& WorldDirection, const float MaxCorrectionAngle,
		float& OutDeltaYaw)
	{
		OutDeltaYaw = 0.0f;
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

		const float CurrentYaw = Character.GetActorRotation().Yaw;
		const float TargetYaw = InputDir.Rotation().Yaw;
		const float DeltaYaw = FMath::FindDeltaAngleDegrees(CurrentYaw, TargetYaw);
		const float AbsoluteDeltaYaw = FMath::Abs(DeltaYaw);
		if (AbsoluteDeltaYaw > MaxCorrectionAngle
			+ MaxCorrectionAngleOvershootToleranceDegrees)
		{
			return false;
		}

		// Small overshoots are accepted, but only the configured angle cap is applied.
		OutDeltaYaw = FMath::Clamp(DeltaYaw,
			-MaxCorrectionAngle, MaxCorrectionAngle);
		return !FMath::IsNearlyZero(OutDeltaYaw);
	}

	bool ApplyDirectYawCorrection(ACharacter& Character, const FVector& WorldDirection,
		const float MaxCorrectionAngle)
	{
		float AppliedDeltaYaw = 0.0f;
		if (!ResolveDirectYawCorrectionDelta(Character, WorldDirection,
			MaxCorrectionAngle, AppliedDeltaYaw))
		{
			return false;
		}

		const float CurrentYaw = Character.GetActorRotation().Yaw;
		FRotator CorrectedRotation = Character.GetActorRotation();
		CorrectedRotation.Yaw = FRotator::NormalizeAxis(CurrentYaw + AppliedDeltaYaw);
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
		for (const FScalableFloat* Multiplier : { &Segment.Damage.ElementMotionValue,
			&Segment.Damage.PhysicalDamageMultiplier, &Segment.Damage.ElementDamageMultiplier })
		{
			const float Value = Multiplier->GetValueAtLevel(1.0f);
			if (!FMath::IsFinite(Value) || Value < 0.0f)
			{
				AddError(FString::Printf(TEXT("AttackSegments[%d] elemental motion / damage multipliers must be finite and >= 0."), SegmentIndex));
			}
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
				|| !FMath::IsFinite(Region.HorizontalHalfWidth) || Region.HorizontalHalfWidth < 0.0f
				|| Region.MaxSampleSpacing <= 0.0f || Region.MaxSampleCount <= 0
				|| Region.MaxAngularStepDegrees <= 0.0f)
			{
				AddError(FString::Printf(
					TEXT("AttackSegments[%d].TraceRegions[%d] has invalid socket or sampling values."),
					SegmentIndex, RegionIndex));
			}
		}
	}

	if (!AttackMontage)
	{
		if (!DefaultEntrySection.IsNone() || !EntrySectionByTransitionID.IsEmpty()
			|| !EntrySectionBySourceState.IsEmpty())
		{
			AddError(TEXT("AttackMontage is required when configuring attack Entry Sections."));
		}
	}
	else
	{
		auto ValidateSection = [this, &AddError](FName SectionName,
			const FString& PropertyPath, bool bAllowNone)
		{
			if (SectionName.IsNone())
			{
				if (!bAllowNone)
				{
					AddError(FString::Printf(TEXT("%s must not be None."), *PropertyPath));
				}
				return;
			}
			if (!AttackMontage->IsValidSectionName(SectionName))
			{
				AddError(FString::Printf(TEXT("%s references missing AttackMontage section '%s'."),
					*PropertyPath, *SectionName.ToString()));
			}
		};

		ValidateSection(DefaultEntrySection, TEXT("DefaultEntrySection"), true);
		for (const TPair<FName, FName>& Pair : EntrySectionBySourceState)
		{
			if (Pair.Key.IsNone())
			{
				AddError(TEXT("EntrySectionBySourceState must not contain a None source-state key."));
			}
			ValidateSection(Pair.Value,
				FString::Printf(TEXT("EntrySectionBySourceState[%s]"), *Pair.Key.ToString()), false);
		}

		TArray<FAssetData> CombatConfigAssets;
		FARFilter CombatConfigFilter;
		CombatConfigFilter.ClassPaths.Add(UWeaponCombatConfigBase::StaticClass()->GetClassPathName());
		CombatConfigFilter.bRecursiveClasses = true;
		FAssetRegistryModule& AssetRegistryModule =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		AssetRegistryModule.Get().GetAssets(CombatConfigFilter, CombatConfigAssets);

		for (const TPair<FName, FName>& Pair : EntrySectionByTransitionID)
		{
			if (Pair.Key.IsNone())
			{
				AddError(TEXT("EntrySectionByTransitionID must not contain a None TransitionID key."));
			}
			ValidateSection(Pair.Value,
				FString::Printf(TEXT("EntrySectionByTransitionID[%s]"), *Pair.Key.ToString()), false);

			bool bReferencesThisAbility = false;
			for (const FAssetData& AssetData : CombatConfigAssets)
			{
				const UWeaponCombatConfigBase* CombatConfig =
					Cast<UWeaponCombatConfigBase>(AssetData.GetAsset());
				const UMHGZWeaponComboData* ComboData = CombatConfig
					? CombatConfig->ComboData
					: nullptr;
				if (!ComboData)
				{
					continue;
				}
				for (const FComboTransition& Transition : ComboData->Transitions)
				{
					if (Transition.TransitionID == Pair.Key
						&& Transition.AbilityClass == GetClass())
					{
						bReferencesThisAbility = true;
						break;
					}
				}
				if (bReferencesThisAbility)
				{
					break;
				}
			}

			if (!bReferencesThisAbility)
			{
				AddError(FString::Printf(
					TEXT("EntrySectionByTransitionID[%s] must reference a Combo transition whose AbilityClass is this Attack Ability."),
					*Pair.Key.ToString()));
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

	FGameplayTagContainer ActionTags;
	ActionTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Attacking")));
	if (bBlockCharacterMovementDuringAction)
	{
		ActionTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Combat.State.BlockMovement")));
	}
	AcquireActionTags(ActionTags, FName(TEXT("AttackState")));

	// 重置状态
	ActiveCollisionWindows.Empty();
	CollisionNotifyRoutes.Reset();
	CurrentSegmentIndex = 0;
	bHasHitThisActivation = false;
	bHasActiveRootMotionTask = false;
	bIsEndingAbility = false;
	bMontageSuspendedForFollowup = false;
	ActiveDirectionCorrectionTask = nullptr;
	// 每次激活生成稳定攻击身份：一次激活内所有伤害/多跳/反馈共享同一 ID。
	ActivationAttackInstanceID = FGuid::NewGuid();
	// Action 已 Confirm；普通攻击在 Montage 播放前只读取一次冻结输入方向。
	ApplyActivationDirectionCorrection();
	if (!PrepareAttackMontage())
	{
		RequestEndAction(EWeaponActionEndReason::Cancelled);
		return;
	}

	FName StartSection;
	if (!SelectAttackMontageStartSection(StartSection))
	{
		RequestEndAction(EWeaponActionEndReason::Cancelled);
		return;
	}

	ActiveAttackMontage = AttackMontage;
	if (!Character || !StartAttackMontage(*Character, AttackMontage, StartSection))
	{
		RequestEndAction(EWeaponActionEndReason::Interrupted);
	}
}

bool UMHGZAttackAbility::ValidateActionDependencies() const
{
	const ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!Character || !Character->GetMesh() || !Character->GetMesh()->GetAnimInstance()
		|| !AttackMontage)
	{
		return false;
	}

	// 此预检发生在 Resource Reserve / GAS Commit / Coordinator Confirm 之前：
	// 无效入口绝不能把来源动作以 Superseded 结束。
	FName StartSection;
	return SelectAttackMontageStartSection(StartSection);
}

bool UMHGZAttackAbility::SelectAttackMontageStartSection(FName& OutStartSection) const
{
	return SelectAttackMontageStartSection(GetWeaponActivationContext(), OutStartSection);
}

bool UMHGZAttackAbility::SelectAttackMontageStartSection(
	const FWeaponAbilityActivationContext& Context, FName& OutStartSection) const
{
	OutStartSection = NAME_None;
	if (!AttackMontage)
	{
		return false;
	}

	bool bHasExplicitMapping = false;
	if (!Context.TransitionID.IsNone())
	{
		if (const FName* Section = EntrySectionByTransitionID.Find(Context.TransitionID))
		{
			OutStartSection = *Section;
			bHasExplicitMapping = true;
		}
	}
	if (!bHasExplicitMapping && !Context.SourceState.IsNone())
	{
		if (const FName* Section = EntrySectionBySourceState.Find(Context.SourceState))
		{
			OutStartSection = *Section;
			bHasExplicitMapping = true;
		}
	}
	if (!bHasExplicitMapping && !DefaultEntrySection.IsNone())
	{
		OutStartSection = DefaultEntrySection;
		bHasExplicitMapping = true;
	}

	// 没有配置入口是合法回退：由 AbilityTask 从 Montage 开头开始。
	if (!bHasExplicitMapping)
	{
		return true;
	}
	return !OutStartSection.IsNone()
		&& AttackMontage->IsValidSectionName(OutStartSection);
}

float UMHGZAttackAbility::ResolveAttackMontageBlendInTime(
	const FWeaponAbilityActivationContext& Context) const
{
	if (!AttackMontage)
	{
		return 0.0f;
	}
	return Context.MontageBlendInTime >= 0.0f
		? Context.MontageBlendInTime
		: AttackMontage->BlendIn.GetBlendTime();
}

float UMHGZAttackAbility::ResolveActivationMaxCorrectionAngle(
	const FWeaponAbilityActivationContext& Context) const
{
	return Context.MaxCorrectionAngle >= 0.0f
		? Context.MaxCorrectionAngle
		: MaxCorrectionAngle;
}

bool UMHGZAttackAbility::StartAttackMontage(ACharacter& Character,
	UAnimMontage* Montage, FName StartSection)
{
	UAnimInstance* AnimInstance = Character.GetMesh()
		? Character.GetMesh()->GetAnimInstance()
		: nullptr;
	if (!AnimInstance || !Montage)
	{
		return false;
	}

	const float BlendInTime = ResolveAttackMontageBlendInTime(
		GetWeaponActivationContext());
	MontageTask = UAbilityTask_MHGZPlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, FName(TEXT("AttackMontage")), Montage, 1.0f, StartSection,
		BlendInTime);
	if (!MontageTask)
	{
		return false;
	}

	MontageTask->OnCompleted.AddDynamic(this, &UMHGZAttackAbility::OnMontageCompleted);
	MontageTask->OnInterrupted.AddDynamic(this, &UMHGZAttackAbility::OnMontageInterrupted);
	MontageTask->OnCancelled.AddDynamic(this, &UMHGZAttackAbility::OnMontageInterrupted);
	MontageTask->ReadyForActivation();

	FAnimMontageInstance* MontageInstance = AnimInstance->GetActiveInstanceForMontage(Montage);
	return MontageInstance
		&& RegisterMontageInstance(Character.GetMesh(), MontageInstance->GetInstanceID());
}

bool UMHGZAttackAbility::SuspendAttackMontageForFollowup()
{
	if (!IsActive() || bIsEndingAbility || !IsActionActivationCommitted())
	{
		return false;
	}

	bMontageSuspendedForFollowup = true;
	DisableCollision();

	// 出招蒙太奇只是被「挂起」，不是被抹掉 —— 下面 EndTask 会用蒙太奇自己的
	// BlendOut 让它淡出一段。而在这段淡出窗口里它**仍然是 RootMotionMontageInstance**：
	// Montage_PlayInternal 只在「新蒙太奇自己带根运动」时才去 Stop 上一个根运动实例
	// （AnimInstance.cpp:2412-2426），而舞踏的 AM_IG_WuTa 是纯表现蒙太奇、没有根运动，
	// 那条分支根本不进。于是 AM_IG_TuJinHuiXuan 在整个淡出期间一直提取根运动，把
	// CMC->RootMotionParams.bHasRootMotion 点亮；ApplyRootMotionToVelocity 随即走
	// CharacterMovementComponent.cpp:4410 的动画分支并提前 return，把后续动作新加的
	// RootMotionSource 整段盖掉。实测后果：舞踏起跳后冻结整整一个混入时长
	// （0.15 s，22/22 次），源算出的力全程正确却进不了 Velocity，顶点不再是 564。
	//
	// 显式打断它的提取即可：实例一旦 IsRootMotionDisabled()，AnimInstance.cpp:1952 的
	// bExtractRootMotion 就为假。只停提取、不影响混合，所以下面的淡出观感不变。
	// 不需要配对 Pop —— 本项目没有任何恢复被挂起的出招蒙太奇的路径
	// （bMontageSuspendedForFollowup 只被设置与清除，全项目无 Resume/Restore），
	// 该实例只会淡出至销毁，每次重播拿到的是新实例。
	if (ActiveAttackMontage)
	{
		if (UAnimInstance* AnimInstance = CurrentActorInfo
			? CurrentActorInfo->GetAnimInstance() : nullptr)
		{
			FAnimMontageInstance* Outgoing =
				AnimInstance->GetActiveInstanceForMontage(ActiveAttackMontage);

			if (Outgoing)
			{
				Outgoing->PushDisableRootMotion();
			}
		}
	}

	// EndTask stops this exact ability montage and unbinds its delegates.  The
	// callback can be synchronous, so its handler explicitly recognises the
	// follow-up state instead of ending the still-live Action.
	if (MontageTask)
	{
		MontageTask->EndTask();
		MontageTask = nullptr;
	}
	else if (ActiveAttackMontage)
	{
		if (UAnimInstance* AnimInstance = CurrentActorInfo
			? CurrentActorInfo->GetAnimInstance() : nullptr)
		{
			AnimInstance->Montage_Stop(0.0f, ActiveAttackMontage);
		}
	}
	ActiveAttackMontage = nullptr;

	// A stopped RootMotion Phase may send its NotifyEnd immediately or on the
	// next animation update. Releasing the exact owner here makes the MovementTask
	// handoff deterministic; the later NotifyEnd is a harmless stale no-op.
	if (UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost())
	{
		if (Host->IsMontageRootMotionOwnedBy(GetActionToken()))
		{
			Host->ReleaseMontageRootMotion(GetActionToken());
		}
	}
	return true;
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
	bMontageSuspendedForFollowup = false;
	PendingDodgeSuperseder = FWeaponActionToken();
	CloseAllDodgeAcceptWindows();

	// 关碰撞（幂等安全——清除全部窗口与每目标 LockedTarget Timer）
	DisableCollision();
	if (UAbilityTask_MHGZActionDirectionCorrection* DirectionTask =
		ActiveDirectionCorrectionTask.Get())
	{
		DirectionTask->EndTask();
	}
	ActiveDirectionCorrectionTask = nullptr;
	CollisionNotifyRoutes.Reset();

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
	// Freeze recovery sampling before the coordinator can consume a pre-input.
	// Keep a value copy: listeners may end this action or register its successor.
	const FWeaponActionToken OpenedAction = ActionToken;
	Host->OnWeaponDodgeAcceptWindowOpened.Broadcast(OpenedAction);
	if (!IsActive() || bIsEndingAbility || GetActionToken() != OpenedAction
		|| !Host->IsActionRegistered(OpenedAction)) return true;
	if (UMHGZAbilitySystemComponent* ASC = Cast<UMHGZAbilitySystemComponent>(
		GetAbilitySystemComponentFromActorInfo()))
	{
		if (UGA_WeaponComboCoordinator* Coordinator = ASC->GetActiveComboCoordinator())
		{
			Coordinator->OnDodgeAcceptWindowOpened(OpenedAction);
		}
	}
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

bool UMHGZAttackAbility::HasPreparedDodgeSupersede() const
{
	return PendingDodgeSuperseder.IsValid();
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
	if (IsActive() && !bIsEndingAbility && !bMontageSuspendedForFollowup)
	{
		RequestEndAction(EWeaponActionEndReason::Normal);
	}
}

void UMHGZAttackAbility::OnMontageInterrupted()
{
	if (IsActive() && !bIsEndingAbility && !bMontageSuspendedForFollowup)
	{
		if (HasPreparedDodgeSupersede())
		{
			return;
		}
		RequestEndAction(EWeaponActionEndReason::Interrupted);
	}
}

void UMHGZAttackAbility::ApplyDirectionCorrection()
{
	// Direction is frozen during input resolution; activation never re-reads the stick.
	const FWeaponAbilityActivationContext& Context = GetWeaponActivationContext();
	// WorldDirection is backed by the character's last non-zero movement direction,
	// so it can remain valid after the stick returns to neutral. Only correct when
	// this input snapshot classified an active directional movement input.
	if (Context.Input.Direction == EDirectionalInput::None)
	{
		return;
	}

	ApplyActivationDirectionCorrectionFromWorldDirection(
		Context.Input.WorldDirection, ResolveActivationMaxCorrectionAngle(Context));
}

bool UMHGZAttackAbility::ApplyActivationDirectionCorrectionFromWorldDirection(
	const FVector& WorldDirection, const float CorrectionAngleDegrees)
{
	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	return Character && ApplyDirectYawCorrection(*Character, WorldDirection,
		CorrectionAngleDegrees);
}

void UMHGZAttackAbility::ApplyActivationDirectionCorrection()
{
	ApplyDirectionCorrection();
}

bool UMHGZAttackAbility::ApplyInActionDirectionCorrection(
	const FWeaponActionToken& ActionToken,
	const float MaxCorrectionAngleOverride,
	const float CorrectionDuration,
	const float MaxYawSpeedDegreesPerSecond,
	const EActionDirectionCorrectionEasing Easing)
{
	return ApplyInActionDirectionCorrectionWithDirection(ActionToken,
		GetInActionCorrectionDirection(), MaxCorrectionAngleOverride,
		CorrectionDuration, MaxYawSpeedDegreesPerSecond, Easing);
}

bool UMHGZAttackAbility::ApplyInActionDirectionCorrectionFromSnapshot(
	const FWeaponActionToken& ActionToken,
	const FActionMoveInputFrameSnapshot& InputSnapshot,
	const float MaxCorrectionAngleOverride,
	const float CorrectionDuration,
	const float MaxYawSpeedDegreesPerSecond,
	const EActionDirectionCorrectionEasing Easing)
{
	const AMHGZCharacter* Character =
		Cast<AMHGZCharacter>(GetAvatarActorFromActorInfo());
	const uint64 CurrentFrame = GFrameCounter;
	if (!Character || InputSnapshot.Pawn.Get() != Character
		|| InputSnapshot.RuntimeToken != ActionToken.RuntimeToken
		|| CurrentFrame == 0 || InputSnapshot.FrameNumber != CurrentFrame - 1
		|| !InputSnapshot.bHasMoveInput
		|| InputSnapshot.WorldDirection.ContainsNaN()
		|| InputSnapshot.WorldDirection.IsNearlyZero())
	{
		return false;
	}

	return ApplyInActionDirectionCorrectionWithDirection(ActionToken,
		InputSnapshot.WorldDirection, MaxCorrectionAngleOverride,
		CorrectionDuration, MaxYawSpeedDegreesPerSecond, Easing);
}

bool UMHGZAttackAbility::ApplyInActionDirectionCorrectionWithDirection(
	const FWeaponActionToken& ActionToken,
	const FVector& WorldDirection,
	const float MaxCorrectionAngleOverride,
	const float CorrectionDuration,
	const float MaxYawSpeedDegreesPerSecond,
	const EActionDirectionCorrectionEasing Easing)
{
	if (!IsActive() || bIsEndingAbility || !IsActionActivationCommitted()
		|| ActionToken != GetActionToken()
		|| !FMath::IsFinite(MaxCorrectionAngleOverride)
		|| !FMath::IsFinite(WorldDirection.X)
		|| !FMath::IsFinite(WorldDirection.Y)
		|| !FMath::IsFinite(WorldDirection.Z)
		|| !FMath::IsFinite(CorrectionDuration) || CorrectionDuration < 0.0f
		|| !FMath::IsFinite(MaxYawSpeedDegreesPerSecond)
		|| MaxYawSpeedDegreesPerSecond < 0.0f)
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
	float DeltaYaw = 0.0f;
	if (!ResolveDirectYawCorrectionDelta(*Character,
		WorldDirection, CorrectionAngle, DeltaYaw))
	{
		return false;
	}

	return StartInActionDirectionCorrection(ActionToken, *Character, DeltaYaw,
		CorrectionDuration, MaxYawSpeedDegreesPerSecond, Easing);
}

bool UMHGZAttackAbility::ApplyInActionDirectionCorrectionToYaw(
	const FWeaponActionToken& ActionToken,
	const float TargetYaw,
	const float CorrectionDuration,
	const float MaxYawSpeedDegreesPerSecond,
	const EActionDirectionCorrectionEasing Easing)
{
	if (!FMath::IsFinite(TargetYaw))
	{
		return false;
	}

	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!Character)
	{
		return false;
	}

	const float DeltaYaw = FMath::FindDeltaAngleDegrees(
		Character->GetActorRotation().Yaw, TargetYaw);
	return StartInActionDirectionCorrection(ActionToken, *Character, DeltaYaw,
		CorrectionDuration, MaxYawSpeedDegreesPerSecond, Easing);
}

bool UMHGZAttackAbility::StartInActionDirectionCorrection(
	const FWeaponActionToken& ActionToken,
	ACharacter& Character,
	const float DeltaYaw,
	const float CorrectionDuration,
	const float MaxYawSpeedDegreesPerSecond,
	const EActionDirectionCorrectionEasing Easing)
{
	if (!IsActive() || bIsEndingAbility || !IsActionActivationCommitted()
		|| ActionToken != GetActionToken()
		|| !FMath::IsFinite(DeltaYaw)
		|| !FMath::IsFinite(CorrectionDuration) || CorrectionDuration < 0.0f
		|| !FMath::IsFinite(MaxYawSpeedDegreesPerSecond)
		|| MaxYawSpeedDegreesPerSecond < 0.0f)
	{
		return false;
	}

	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	if (!Host || !Host->IsTokenCurrent(ActionToken.RuntimeToken))
	{
		return false;
	}

	// A later notify always supersedes an unfinished correction from this action,
	// including when the new request is the legacy instantaneous form.
	if (UAbilityTask_MHGZActionDirectionCorrection* ExistingTask =
		ActiveDirectionCorrectionTask.Get())
	{
		ExistingTask->EndTask();
	}
	ActiveDirectionCorrectionTask = nullptr;
	if (FMath::IsNearlyZero(DeltaYaw, 0.01f))
	{
		return true;
	}

	// Duration zero is the backward-compatible one-frame correction path.
	if (CorrectionDuration <= 0.0f)
	{
		FRotator CorrectedRotation = Character.GetActorRotation();
		CorrectedRotation.Yaw = FRotator::NormalizeAxis(
			CorrectedRotation.Yaw + DeltaYaw);
		Character.SetActorRotation(CorrectedRotation);
		return true;
	}

	UAbilityTask_MHGZActionDirectionCorrection* DirectionTask =
		UAbilityTask_MHGZActionDirectionCorrection::StartCorrection(
			this, ActionToken, &Character, Character.GetActorRotation().Yaw,
			DeltaYaw, CorrectionDuration, MaxYawSpeedDegreesPerSecond, Easing);
	if (!DirectionTask || !DirectionTask->DidAcquireRotationOverride())
	{
		return false;
	}

	ActiveDirectionCorrectionTask = DirectionTask;
	return true;
}

FVector UMHGZAttackAbility::GetInActionCorrectionDirection() const
{
	if (const AMHGZCharacter* Character = Cast<AMHGZCharacter>(GetAvatarActorFromActorInfo()))
	{
		return Character->GetCurrentMoveInputWorldDirection();
	}
	return FVector::ZeroVector;
}

void UMHGZAttackAbility::EnableCollision(int32 SegmentIndex)
{
	EnableCollisionInternal(SegmentIndex, nullptr);
}

void UMHGZAttackAbility::EnableCollisionInternal(int32 SegmentIndex,
	const FCollisionNotifyRoute* NotifyRoute)
{
	if (!AttackSegments.IsValidIndex(SegmentIndex)) return;
	const FWeaponActionToken SourceAction = GetActionToken();

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
		const uint64 ReplacedOccurrence = ExistingState->Occurrence;
		ActiveCollisionWindows.Remove(SegmentIndex);
		OnAttackCollisionWindowRetired(SegmentIndex, ReplacedOccurrence);
		if (GetActionToken() != SourceAction || bIsEndingAbility
			|| ActiveCollisionWindows.Contains(SegmentIndex)) return;
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
		RuntimeRegion.QueryShape = Region.MakeQueryShape();
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

	// 运行时只接受非空 TraceRegions；为空时拒绝本次碰撞窗口。
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

	if (++NextCollisionOccurrence == 0) ++NextCollisionOccurrence;
	const uint64 Occurrence = WindowState.Occurrence = NextCollisionOccurrence;
	ActiveCollisionWindows.Add(SegmentIndex, MoveTemp(WindowState));
	if (NotifyRoute)
	{
		FCollisionNotifyRoute Route = *NotifyRoute;
		Route.Occurrence = Occurrence;
		CollisionNotifyRoutes.Add(MoveTemp(Route));
	}
	OnAttackCollisionWindowOpened(SegmentIndex, Occurrence);
	// A first-hit or extension callback can synchronously end/re-enter the action.
	if (GetActionToken() != SourceAction || bIsEndingAbility
		|| GetCollisionWindowOccurrence(SegmentIndex) != Occurrence) return;
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
	const uint64 ClosedOccurrence = WindowState->Occurrence;
	const FWeaponActionToken ClosingAction = GetActionToken();

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

	if (GetActionToken() != ClosingAction || GetCollisionWindowOccurrence(SegmentIndex) != ClosedOccurrence) return;
	ActiveCollisionWindows.Remove(SegmentIndex);
	OnAttackCollisionWindowRetired(SegmentIndex, ClosedOccurrence);
	if (GetActionToken() != ClosingAction || ActiveCollisionWindows.Contains(SegmentIndex)) return;
	OnAttackCollisionWindowClosed(SegmentIndex);
	if (bEndForMiss && GetActionToken() == ClosingAction && IsActive() && !bIsEndingAbility)
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
	if (!ShouldSweepCollision(SegmentIndex))
	{
		// Keep authored window lifetime/closure and fresh poses without issuing
		// a spatial query; later readiness must not sweep the entire absent interval.
		for (FTraceRegionRuntimeState& Region : WindowState->Regions)
		{
			if (Mesh->DoesSocketExist(Region.EndSocketName)
				&& (Region.StartSocketName.IsNone() || Mesh->DoesSocketExist(Region.StartSocketName)))
			{
				Region.PreviousEnd = Mesh->GetSocketLocation(Region.EndSocketName);
				Region.PreviousStart = Region.StartSocketName.IsNone()
					? Region.PreviousEnd : Mesh->GetSocketLocation(Region.StartSocketName);
			}
		}
		return;
	}

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

		// 默认仍为球形。可选横置胶囊只扩大水平截面积，不改变原竖向/前后厚度。
		const FCollisionShape& QueryShape = RegionState.QueryShape;
		const FQuat QueryRotation = QueryShape.IsCapsule()
			? FQuat::FindBetweenNormals(FVector::UpVector, Character->GetActorRightVector().GetSafeNormal2D())
			: FQuat::Identity;

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
						UMHGZMonsterHitzoneComponent::Resolve(Hit.GetComponent());
					if (!Hitzone || (Collision.HitzoneQueryTag.IsValid() &&
						!Hitzone->HitzoneTag.MatchesTagExact(Collision.HitzoneQueryTag)))
					{
						continue;
					}
					// Reject invalid contacts BEFORE per-actor earliest-hit selection.
					// A rear Kinsect overlap must not hide a later front hit on another
					// part of the same monster. ProcessSweepHit rechecks at submission.
					if (!CanApplyCollisionHit(Hit, SegmentIndex))
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
					const FColor DebugColor = Hits.IsEmpty() ? FColor::Yellow : FColor::Green;
					if (QueryShape.IsCapsule())
					{
						DrawDebugCapsule(GetWorld(), SweepTo, QueryShape.GetCapsuleHalfHeight(),
							RegionState.Radius, QueryRotation, DebugColor, false, 0.15f);
					}
					else
					{
						DrawDebugSphere(GetWorld(), SweepTo, RegionState.Radius, 12,
							DebugColor, false, 0.15f);
					}
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

	UMHGZMonsterHitzoneComponent* Hitzone = UMHGZMonsterHitzoneComponent::Resolve(Hit.GetComponent());
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
	if (!CanApplyCollisionHit(Hit, SegmentIndex))
	{
		return;
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
	const int32 PreviousHitCount = TargetState.HitCount;
	const float PreviousHitTime = TargetState.LastHitTime;
	TargetState.HitCount++;
	TargetState.LastHitTime = Now;

	const bool bDamageApplied = ApplyDamage(Hit, SegmentIndex);
	if (!bDamageApplied && !ShouldConsumeCollisionOnDamageFailure(SegmentIndex))
	{
		// GE failure is retryable only for explicitly opted-in segments. Re-resolve
		// after Apply because GameplayEffect callbacks may end the ability/window.
		if (FCollisionWindowRuntimeState* CurrentWindow =
			ActiveCollisionWindows.Find(SegmentIndex))
		{
			if (FHitTargetRuntimeState* CurrentTarget =
				CurrentWindow->HitTargets.Find(ActorKey))
			{
				if (CurrentTarget->HitCount == PreviousHitCount + 1
					&& CurrentTarget->LastHitTime == Now)
				{
					CurrentTarget->HitCount = PreviousHitCount;
					CurrentTarget->LastHitTime = PreviousHitTime;
				}
			}
		}
		return;
	}

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

bool UMHGZAttackAbility::ApplyDamage(const FHitResult& Hit, int32 SegmentIndex)
{
	if (!AttackSegments.IsValidIndex(SegmentIndex)) return false;

	AActor* Target = Hit.GetActor();
	if (!Target) return false;

	UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo();
	if (!SourceASC) return false;

	// 构造并 Apply GE
	FGameplayEffectSpecHandle Spec = MakeDamageSpec(Hit, SegmentIndex);
	UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target);
	bool bApplied = false;
	if (Spec.IsValid() && TargetASC)
	{
		bApplied = SourceASC->ApplyGameplayEffectSpecToTarget(
			*Spec.Data, TargetASC).WasSuccessfullyApplied();
	}
	if (bApplied)
	{
		// 专属命中效果必须以伤害提交成功为前提；Sweep 接触本身不是命中结算。
		HandleSuccessfulAttackDamage(Hit, SegmentIndex);
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
	return bApplied;
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
		Context->PhysicalDamageType = Seg.Damage.PhysicalDamageType;
		if (const UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost())
		{
			if (const UWeaponCombatConfigBase* Combat = Host->GetCurrentContext().CombatConfig.Get())
			{
				Context->WeaponDamageProfile = Combat->DamageProfile;
			}
		}
		Context->bUseHitzoneDefense = Seg.Damage.bUseHitzoneDefense;
		Context->HitStaggerTag = Seg.Damage.HitStaggerTag;
		Context->HitCueTag = Seg.Damage.HitCueTag;
		Context->ElementCueTag = Seg.Damage.ElementalCueTag;
		Context->HitStopDuration = Seg.Damage.HitStopBase.GetValueAtLevel(GetAbilityLevel());
		Context->CameraShakeClass = Seg.Damage.CameraShakeClass;
		Context->CameraShakeScale = Seg.Damage.CameraShakeScale;
		if (const UMHGZMonsterHitzoneComponent* Hitzone =
			UMHGZMonsterHitzoneComponent::Resolve(Hit.GetComponent()))
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
	Spec.Data->SetSetByCallerMagnitude(
		FGameplayTag::RequestGameplayTag(TEXT("Damage.ElementMotionValue")),
		Seg.Damage.ElementMotionValue.GetValueAtLevel(GetAbilityLevel()));
	Spec.Data->SetSetByCallerMagnitude(
		FGameplayTag::RequestGameplayTag(TEXT("Damage.PhysicalMultiplier")),
		Seg.Damage.PhysicalDamageMultiplier.GetValueAtLevel(GetAbilityLevel()));
	Spec.Data->SetSetByCallerMagnitude(
		FGameplayTag::RequestGameplayTag(TEXT("Damage.ElementMultiplier")),
		Seg.Damage.ElementDamageMultiplier.GetValueAtLevel(GetAbilityLevel()));

	// 基础破坏值
	Spec.Data->SetSetByCallerMagnitude(
		FGameplayTag::RequestGameplayTag(TEXT("Damage.BaseStagger")),
		Seg.Damage.BaseStaggerValue.GetValueAtLevel(GetAbilityLevel()));

	// DynamicAssetTag 只作调试镜像；结算与反馈以 Context 为真相源。
	const UMHGZMonsterHitzoneComponent* Hitzone =
		UMHGZMonsterHitzoneComponent::Resolve(Hit.GetComponent());
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
