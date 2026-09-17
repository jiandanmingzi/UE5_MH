// Copyright MHGZ Project. All Rights Reserved.

#include "ActionSystem/MHGZBackVaultAbility.h"

#include "ActionSystem/AbilityTask_MHGZPlayMontageAndWait.h"
#include "ActionSystem/AbilityTask_MHGZWeaponMovement.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "Animation/AnimCompositeBase.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceBase.h"
#include "AttributeSystem/Res_InsectGlaive.h"
#include "Curves/CurveVector.h"
#include "Curves/RichCurve.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InsectGlaive/InsectGlaiveCombatConfig.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"
#include "MHGZ.h"

namespace
{
const FGameplayTag& WhiteExtractTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("WeaponResource.IG.Extract.White"));
	return Tag;
}

float RequiredSegmentPlayRate(const UAnimSequenceBase& Sequence,
	const float DesiredDuration)
{
	if (!FMath::IsFinite(DesiredDuration) || DesiredDuration <= KINDA_SMALL_NUMBER
		|| !FMath::IsFinite(Sequence.RateScale)
		|| FMath::IsNearlyZero(Sequence.RateScale))
	{
		return 0.0f;
	}
	return Sequence.GetPlayLength() / (DesiredDuration * FMath::Abs(Sequence.RateScale));
}

void AddLinearKey(FRichCurve& Curve, const float Time, const float Value)
{
	const FKeyHandle Handle = Curve.AddKey(Time, Value);
	Curve.SetKeyInterpMode(Handle, RCIM_Linear);
}
}

UMHGZBackVaultAbility::UMHGZBackVaultAbility()
{
	InputTag = FGameplayTag::RequestGameplayTag(TEXT("Input.Weapon.RTA"));
	// RT+A is a backwards vault, not a facing correction request.
	MaxCorrectionAngle = 0.0f;

	// Forward lead recovered from the JumpOver clip's root track, as a fraction
	// of BackVaultDistance.  Measured from the non-white capture: the root bone
	// reaches (along 23.34 cm, lateral 13.01 cm) by the handoff; only the along
	// component is taken here, because the recorded path's own lateral is
	// already the out-and-back the reference shows and its net displacement is
	// zero.  The shape is the measured ramp -- front-loaded, flat from ~0.32.
	// The terminal keys must stay equal: a non-zero slope here would leave the
	// capsule's exit velocity different from the analytic handoff tangent.
	auto AddDriftKey = [](TArray<FBackVaultClipDriftKey>& Table, const float Time,
		const float Fraction)
	{
		FBackVaultClipDriftKey& Key = Table.AddDefaulted_GetRef();
		Key.CurveTime = Time;
		Key.ForwardFraction = Fraction;
	};
	constexpr float NormalLeadFraction = 23.34f / 583.87f;
	AddDriftKey(BackVaultClipDrift, 0.00f, 0.0f);
	AddDriftKey(BackVaultClipDrift, 0.09f, NormalLeadFraction * 0.55f);
	AddDriftKey(BackVaultClipDrift, 0.23f, NormalLeadFraction * 0.95f);
	AddDriftKey(BackVaultClipDrift, 0.32f, NormalLeadFraction);
	AddDriftKey(BackVaultClipDrift, 1.00f, NormalLeadFraction);
	// White is a different clip (3.1667 s vs 2.1667 s) and has not been measured
	// on its own yet.  Its apex and distance are both ~1.2x the normal variant,
	// so the same *fraction* is the best available starting point -- replace it
	// with values baked from AS_Unsh_W_Jump_Over_Back's own root track.
	constexpr float WhiteLeadFraction = 0.0400f;
	AddDriftKey(WhiteBackVaultClipDrift, 0.00f, 0.0f);
	AddDriftKey(WhiteBackVaultClipDrift, 0.09f, WhiteLeadFraction * 0.55f);
	AddDriftKey(WhiteBackVaultClipDrift, 0.23f, WhiteLeadFraction * 0.95f);
	AddDriftKey(WhiteBackVaultClipDrift, 0.32f, WhiteLeadFraction);
	AddDriftKey(WhiteBackVaultClipDrift, 1.00f, WhiteLeadFraction);

	BackJumpSequence = TSoftObjectPtr<UAnimSequenceBase>(FSoftObjectPath(
		TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_Jump_Back.AS_Unsh_Jump_Back")));
	BackJumpOverSequence = TSoftObjectPtr<UAnimSequenceBase>(FSoftObjectPath(
		TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_Jump_Over_Back.AS_Unsh_Jump_Over_Back")));
	WhiteBackJumpSequence = TSoftObjectPtr<UAnimSequenceBase>(FSoftObjectPath(
		TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_W_Jump_Back.AS_Unsh_W_Jump_Back")));
	WhiteBackJumpOverSequence = TSoftObjectPtr<UAnimSequenceBase>(FSoftObjectPath(
		TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_W_Jump_Over_Back.AS_Unsh_W_Jump_Over_Back")));

	// Capture mhrise_20260913_001506_01: full ground-to-ground trace,
	// 5.8387 m distance and 5.797 m apex.  We use only its action-owned prefix;
	// the final free-fall portion is deliberately left to CMC.
	BackVaultTrajectory = {
		{ 0.000f, FVector(0.000f, 0.000f, 0.000f) },
		{ 0.050f, FVector(-0.004f, 0.009f, 0.000f) },
		{ 0.101f, FVector(0.018f, -0.003f, 0.000f) },
		{ 0.151f, FVector(0.052f, -0.013f, 0.000f) },
		{ 0.202f, FVector(0.159f, -0.019f, 0.043f) },
		{ 0.252f, FVector(0.249f, -0.021f, 0.131f) },
		{ 0.303f, FVector(0.266f, -0.023f, 0.141f) },
		{ 0.328f, FVector(0.272f, -0.023f, 0.177f) },
		{ 0.353f, FVector(0.301f, -0.019f, 0.309f) },
		{ 0.404f, FVector(0.356f, -0.007f, 0.527f) },
		{ 0.454f, FVector(0.413f, -0.002f, 0.703f) },
		{ 0.504f, FVector(0.472f, -0.001f, 0.839f) },
		{ 0.555f, FVector(0.530f, -0.001f, 0.934f) },
		{ 0.605f, FVector(0.589f, -0.001f, 0.987f) },
		{ 0.655f, FVector(0.646f, -0.001f, 1.000f) },
		{ 0.706f, FVector(0.700f, -0.001f, 0.972f) },
		{ 0.756f, FVector(0.751f, -0.001f, 0.902f) },
		{ 0.807f, FVector(0.803f, -0.001f, 0.792f) },
		{ 0.857f, FVector(0.854f, -0.001f, 0.640f) },
		{ 0.908f, FVector(0.906f, -0.001f, 0.447f) },
		{ 0.958f, FVector(0.957f, 0.000f, 0.214f) },
		{ 1.000f, FVector(1.000f, 0.000f, 0.000f) }
	};

	// Capture mhrise_20260913_001535_02: 2.1179 s, 7.0605 m, 7.489 m apex.
	WhiteBackVaultTrajectory = {
		{ 0.000f, FVector(0.000f, 0.000f, 0.000f) },
		{ 0.047f, FVector(-0.003f, 0.007f, 0.001f) },
		{ 0.094f, FVector(0.015f, -0.004f, 0.001f) },
		{ 0.142f, FVector(0.058f, -0.017f, 0.001f) },
		{ 0.189f, FVector(0.136f, -0.026f, 0.001f) },
		{ 0.236f, FVector(0.235f, -0.031f, 0.097f) },
		{ 0.283f, FVector(0.350f, -0.035f, 0.149f) },
		{ 0.307f, FVector(0.394f, -0.037f, 0.145f) },
		{ 0.331f, FVector(0.422f, -0.038f, 0.260f) },
		{ 0.378f, FVector(0.464f, -0.040f, 0.467f) },
		{ 0.425f, FVector(0.507f, -0.041f, 0.640f) },
		{ 0.473f, FVector(0.549f, -0.041f, 0.779f) },
		{ 0.520f, FVector(0.590f, -0.037f, 0.885f) },
		{ 0.567f, FVector(0.632f, -0.030f, 0.957f) },
		{ 0.614f, FVector(0.672f, -0.023f, 0.995f) },
		{ 0.662f, FVector(0.712f, -0.017f, 1.000f) },
		{ 0.709f, FVector(0.752f, -0.011f, 0.970f) },
		{ 0.756f, FVector(0.791f, -0.006f, 0.908f) },
		{ 0.803f, FVector(0.829f, -0.003f, 0.812f) },
		{ 0.851f, FVector(0.868f, -0.001f, 0.682f) },
		{ 0.898f, FVector(0.909f, 0.000f, 0.518f) },
		{ 0.945f, FVector(0.951f, 0.000f, 0.321f) },
		{ 0.992f, FVector(0.993f, 0.000f, 0.090f) },
		{ 1.000f, FVector(1.000f, 0.000f, 0.000f) }
	};
}

void UMHGZBackVaultAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	bUseWhiteBackVault = CheckExtractRequirement(WhiteExtractTag());
	bBackVaultVisualRootMotionDisabled = false;
	bVisualFinished = false;
	bMovementFinished = false;
	bBeginFreeFallAfterEnd = false;
	bBackVaultInitialFlightOwned = false;
	PreBackVaultMovementMode = 0;
	PreBackVaultCustomMovementMode = 0;
	bIsEndingBackVault = false;
	BackVaultVisualTask = nullptr;
	JumpOverHandoffTask = nullptr;
	BackVaultMovementTask = nullptr;
	ActiveTrajectoryCurve = nullptr;

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!IsActionActivationCommitted() || !IsActive())
	{
		return;
	}
	if (!ScheduleJumpOverMovementHandoff())
	{
		RequestEndAction(EWeaponActionEndReason::Interrupted);
	}
}

void UMHGZBackVaultAbility::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	if (bIsEndingBackVault)
	{
		return;
	}
	bIsEndingBackVault = true;
	if (JumpOverHandoffTask)
	{
		JumpOverHandoffTask->EndTask();
		JumpOverHandoffTask = nullptr;
	}
	// The Jump segment owns Montage Root Motion, then CurvedVault owns the
	// JumpOver path.  If either phase is interrupted, stop this exact visual
	// first; otherwise an already-extracted source-sequence root track can keep
	// CMC in HasAnimRootMotion and swallow subsequent player input.
	ACharacter* VisualCharacter = ActorInfo && ActorInfo->AvatarActor.IsValid()
		? Cast<ACharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	UAnimInstance* AnimInstance = VisualCharacter && VisualCharacter->GetMesh()
		? VisualCharacter->GetMesh()->GetAnimInstance() : nullptr;
	// Balance the JumpOver Push unconditionally.  Whether bVisualFinished is set
	// by now is a race between the movement task and the montage's own length,
	// and the losing branch would leave IsRootMotionDisabled() standing on a
	// montage that keeps playing at weight 1 for another frame.  Popping a
	// matching push cannot double-pop: the flag is cleared as we go.
	if (bBackVaultVisualRootMotionDisabled && AnimInstance)
	{
		if (FAnimMontageInstance* MontageInstance =
			AnimInstance->GetActiveInstanceForMontage(AttackMontage))
		{
			MontageInstance->PopDisableRootMotion();
		}
		bBackVaultVisualRootMotionDisabled = false;
	}
	// The visual itself is only stopped when it did not finish on its own.
	if (!bVisualFinished && AnimInstance)
	{
		AnimInstance->Montage_Stop(0.05f, AttackMontage);
	}
	if (BackVaultVisualTask)
	{
		BackVaultVisualTask->EndTask();
		BackVaultVisualTask = nullptr;
	}
	BackVaultMovementTask = nullptr;
	ActiveTrajectoryCurve = nullptr;
	ACharacter* EndingCharacter = ActorInfo && ActorInfo->AvatarActor.IsValid()
		? Cast<ACharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	const bool bStartFreeFall = bBeginFreeFallAfterEnd && !bWasCancelled
		&& EndingCharacter && EndingCharacter->GetCharacterMovement()
		&& EndingCharacter->GetCharacterMovement()->IsFalling();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
	// CurvedVault has already switched to Falling on its normal completion.  Any
	// other exit can happen before CurvedVault exists, or while its source is
	// being torn down; in both cases this GA must return the CMC to a valid
	// locomotion mode instead of leaving it in Flying.
	if (EndingCharacter && !bStartFreeFall)
	{
		RestoreBackVaultInitialFlight(*EndingCharacter);
	}
	else
	{
		bBackVaultInitialFlightOwned = false;
	}
	if (bStartFreeFall)
	{
		if (UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost())
		{
			Host->BeginAerialFalling(bUseWhiteBackVault,
				FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aerial.Falling.IG_BackVault")));
		}
	}
}

bool UMHGZBackVaultAbility::ValidateActionDependencies() const
{
	const ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	const UInsectGlaiveCombatConfig* CombatConfig = nullptr;
	if (const UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost())
	{
		CombatConfig = Cast<UInsectGlaiveCombatConfig>(Host->GetCurrentContext().CombatConfig);
	}
	if (!Character || !Character->GetMesh() || !Character->GetMesh()->GetAnimInstance()
		|| !GetIGResourceComponent() || !CombatConfig)
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[BackVault] Dependency rejected: Character=%s Mesh=%s AnimInstance=%s Resource=%s CombatConfig=%s"),
			Character ? TEXT("true") : TEXT("false"),
			Character && Character->GetMesh() ? TEXT("true") : TEXT("false"),
			Character && Character->GetMesh() && Character->GetMesh()->GetAnimInstance() ? TEXT("true") : TEXT("false"),
			GetIGResourceComponent() ? TEXT("true") : TEXT("false"),
			CombatConfig ? TEXT("true") : TEXT("false"));
		return false;
	}

	const bool bWhite = CheckExtractRequirement(WhiteExtractTag());
	const TSoftObjectPtr<UAnimSequenceBase>& Jump = bWhite
		? WhiteBackJumpSequence : BackJumpSequence;
	const TSoftObjectPtr<UAnimSequenceBase>& JumpOver = bWhite
		? WhiteBackJumpOverSequence : BackJumpOverSequence;
	const TArray<FBackVaultTrajectoryKey>& Path = bWhite
		? WhiteBackVaultTrajectory : BackVaultTrajectory;
	if (!Jump.LoadSynchronous() || !JumpOver.LoadSynchronous()
		|| Path.Num() < 2)
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[BackVault] Dependency rejected: White=%s Jump=%s JumpOver=%s PathKeys=%d"),
			bWhite ? TEXT("true") : TEXT("false"),
			*Jump.ToSoftObjectPath().ToString(), *JumpOver.ToSoftObjectPath().ToString(),
			Path.Num());
		return false;
	}

	// The JumpOver clip's root track must be locked to the ref pose and NOT
	// extracted.  Both other states are wrong, in different ways: with neither
	// flag set its forward lead lives on in the pose, survives until the fall
	// clip replaces it, and then snaps back in two frames (A9); with both set,
	// anim root motion pre-empts the CurvedVault source outright and the whole
	// analytic path dies.  Jump is the opposite case -- it owns montage root
	// motion and no source runs during it -- so it is deliberately not checked.
	if (const UAnimSequence* JumpOverSequence = Cast<UAnimSequence>(JumpOver.Get()))
	{
		const EBackVaultRootTrackPolicy Policy = ResolveRootTrackPolicy(
			JumpOverSequence->bEnableRootMotion, JumpOverSequence->bForceRootLock);
		if (Policy != EBackVaultRootTrackPolicy::LockedNotExtracted)
		{
			UE_LOG(LogMHGZ, Warning,
				TEXT("[BackVault] Dependency rejected: JumpOver %s policy=%d, need LockedNotExtracted (EnableRootMotion=%d ForceRootLock=%d)"),
				*JumpOver.ToSoftObjectPath().ToString(), static_cast<int32>(Policy),
				JumpOverSequence->bEnableRootMotion ? 1 : 0,
				JumpOverSequence->bForceRootLock ? 1 : 0);
			return false;
		}
	}

	const float Duration = bWhite ? CombatConfig->WhiteBackVaultDuration
		: CombatConfig->BackVaultDuration;
	const float Distance = bWhite ? CombatConfig->WhiteBackVaultDistance
		: CombatConfig->BackVaultDistance;
	const float Apex = bWhite ? CombatConfig->WhiteBackVaultApexHeight
		: CombatConfig->BackVaultApexHeight;
	const bool bValidMetrics = FMath::IsFinite(Duration) && Duration > 0.f
		&& FMath::IsFinite(Distance) && Distance > 0.f
		&& FMath::IsFinite(Apex) && Apex > 0.f;
	if (!bValidMetrics)
	{
		UE_LOG(LogMHGZ, Warning,
			TEXT("[BackVault] Dependency rejected: White=%s Duration=%.3f Distance=%.3f Apex=%.3f"),
			bWhite ? TEXT("true") : TEXT("false"), Duration, Distance, Apex);
	}
	return bValidMetrics;
}

bool UMHGZBackVaultAbility::PrepareAttackMontage()
{
	if (bUseWhiteBackVault && WhiteAttackMontage)
	{
		AttackMontage = WhiteAttackMontage;
		return true;
	}
	if (!bUseWhiteBackVault && AttackMontage)
	{
		return true;
	}

	return BuildBackVaultMontage();
}

bool UMHGZBackVaultAbility::StartAttackMontage(ACharacter& Character,
	UAnimMontage* Montage, FName StartSection)
{
	UAnimInstance* AnimInstance = Character.GetMesh()
		? Character.GetMesh()->GetAnimInstance() : nullptr;
	if (!AnimInstance || !Montage)
	{
		return false;
	}
	// This must happen before the montage task is activated.  In Walking mode,
	// CMC constrains an animation Root Motion delta to the floor, so the Jump
	// sequence can pose a pole-vault without ever raising the capsule.
	if (!BeginBackVaultInitialFlight(Character))
	{
		return false;
	}

	BackVaultVisualTask = UAbilityTask_MHGZPlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, TEXT("BackVaultVisual"), Montage, 1.0f, StartSection,
		ResolveAttackMontageBlendInTime(GetWeaponActivationContext()),
		true, 1.0f);
	if (!BackVaultVisualTask)
	{
		RestoreBackVaultInitialFlight(Character);
		return false;
	}
	BackVaultVisualTask->OnCompleted.AddDynamic(this,
		&UMHGZBackVaultAbility::HandleBackVaultVisualCompleted);
	BackVaultVisualTask->OnInterrupted.AddDynamic(this,
		&UMHGZBackVaultAbility::HandleBackVaultVisualInterrupted);
	BackVaultVisualTask->OnCancelled.AddDynamic(this,
		&UMHGZBackVaultAbility::HandleBackVaultVisualInterrupted);
	BackVaultVisualTask->ReadyForActivation();

	FAnimMontageInstance* MontageInstance = AnimInstance->GetActiveInstanceForMontage(Montage);
	if (!MontageInstance
		|| !RegisterMontageInstance(Character.GetMesh(), MontageInstance->GetInstanceID()))
	{
		AnimInstance->Montage_Stop(0.0f, Montage);
		RestoreBackVaultInitialFlight(Character);
		return false;
	}

	// Jump owns its imported Montage Root Motion.  The JumpOver boundary task
	// disables only the remaining visual root track immediately before CurvedVault
	// starts; do not use Character-wide root-translation scaling here.
	// The physical curve can outlive the visual chain by a few frames, so hold
	// the final pose until its exact free-fall handoff.
	MontageInstance->bEnableAutoBlendOut = false;
	return true;
}

bool UMHGZBackVaultAbility::BeginBackVaultInitialFlight(ACharacter& Character)
{
	UCharacterMovementComponent* CMC = Character.GetCharacterMovement();
	if (!CMC)
	{
		return false;
	}

	if (bBackVaultInitialFlightOwned)
	{
		return CMC->MovementMode == MOVE_Flying;
	}

	PreBackVaultMovementMode = static_cast<uint8>(CMC->MovementMode);
	PreBackVaultCustomMovementMode = CMC->CustomMovementMode;
	CMC->SetMovementMode(MOVE_Flying);
	bBackVaultInitialFlightOwned = CMC->MovementMode == MOVE_Flying;
	return bBackVaultInitialFlightOwned;
}

void UMHGZBackVaultAbility::RestoreBackVaultInitialFlight(ACharacter& Character)
{
	if (!bBackVaultInitialFlightOwned)
	{
		return;
	}
	bBackVaultInitialFlightOwned = false;

	UCharacterMovementComponent* CMC = Character.GetCharacterMovement();
	if (!CMC || CMC->MovementMode != MOVE_Flying)
	{
		return;
	}

	const EMovementMode PreviousMode = static_cast<EMovementMode>(PreBackVaultMovementMode);
	if (PreviousMode == MOVE_Walking || PreviousMode == MOVE_NavWalking)
	{
		// Re-checking the floor avoids incorrectly pinning an interrupted vault to
		// Walking when its capsule is already above the ground.
		CMC->SetDefaultMovementMode();
	}
	else
	{
		CMC->SetMovementMode(PreviousMode, PreBackVaultCustomMovementMode);
	}
}

bool UMHGZBackVaultAbility::BuildBackVaultMontage()
{
	const TSoftObjectPtr<UAnimSequenceBase>& JumpRef = bUseWhiteBackVault
		? WhiteBackJumpSequence : BackJumpSequence;
	const TSoftObjectPtr<UAnimSequenceBase>& JumpOverRef = bUseWhiteBackVault
		? WhiteBackJumpOverSequence : BackJumpOverSequence;
	UAnimSequenceBase* Jump = JumpRef.LoadSynchronous();
	UAnimSequenceBase* JumpOver = JumpOverRef.LoadSynchronous();
	if (!Jump || !JumpOver || !Jump->GetSkeleton()
		|| Jump->GetSkeleton() != JumpOver->GetSkeleton())
	{
		return false;
	}

	const float JumpDuration = bUseWhiteBackVault ? WhiteBackJumpDuration : BackJumpDuration;
	const float JumpOverDuration = bUseWhiteBackVault
		? WhiteBackJumpOverDuration : BackJumpOverDuration;
	if (RequiredSegmentPlayRate(*Jump, JumpDuration) <= 0.f
		|| RequiredSegmentPlayRate(*JumpOver, JumpOverDuration) <= 0.f)
	{
		return false;
	}

	UAnimMontage* Montage = NewObject<UAnimMontage>(this, NAME_None, RF_Transient);
	if (!Montage)
	{
		return false;
	}
	Montage->SetSkeleton(Jump->GetSkeleton());
	Montage->BlendIn.SetBlendTime(3.0f / 60.0f);
	Montage->BlendOut.SetBlendTime(0.05f);
	FSlotAnimationTrack& SlotTrack = Montage->SlotAnimTracks.AddDefaulted_GetRef();
	SlotTrack.SlotName = BackVaultMontageSlot;

	float StartPos = 0.0f;
	auto AddSegment = [&SlotTrack, &StartPos](UAnimSequenceBase& Sequence,
		const float DesiredDuration)
	{
		FAnimSegment& Segment = SlotTrack.AnimTrack.AnimSegments.AddDefaulted_GetRef();
		Segment.SetAnimReference(&Sequence, true);
		Segment.StartPos = StartPos;
		Segment.AnimStartTime = 0.0f;
		Segment.AnimEndTime = Sequence.GetPlayLength();
		Segment.AnimPlayRate = RequiredSegmentPlayRate(Sequence, DesiredDuration);
		Segment.LoopingCount = 1;
		StartPos += DesiredDuration;
	};
	AddSegment(*Jump, JumpDuration);
	AddSegment(*JumpOver, JumpOverDuration);
	FCompositeSection& JumpSection = Montage->CompositeSections.AddDefaulted_GetRef();
	JumpSection.SectionName = TEXT("Jump");
	JumpSection.Link(Montage, 0.0f, 0);
	JumpSection.NextSectionName = TEXT("JumpOver");
	FCompositeSection& JumpOverSection = Montage->CompositeSections.AddDefaulted_GetRef();
	JumpOverSection.SectionName = TEXT("JumpOver");
	JumpOverSection.Link(Montage, JumpDuration, 0);
	Montage->UpdateLinkableElements();
	Montage->RefreshCacheData();
	if (bUseWhiteBackVault)
	{
		WhiteAttackMontage = Montage;
		AttackMontage = Montage;
	}
	else
	{
		AttackMontage = Montage;
	}
	return FMath::IsNearlyEqual(Montage->GetPlayLength(), StartPos, 0.01f);
}

bool UMHGZBackVaultAbility::ScheduleJumpOverMovementHandoff()
{
	if (!IsActive() || bIsEndingBackVault || JumpOverHandoffTask
		|| !IsActionActivationCommitted())
	{
		return false;
	}

	const float JumpDuration = bUseWhiteBackVault ? WhiteBackJumpDuration : BackJumpDuration;
	if (!FMath::IsFinite(JumpDuration) || JumpDuration <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	JumpOverHandoffTask = UAbilityTask_WaitDelay::WaitDelay(this, JumpDuration);
	if (!JumpOverHandoffTask)
	{
		return false;
	}
	JumpOverHandoffTask->OnFinish.AddDynamic(this,
		&UMHGZBackVaultAbility::HandleJumpOverMovementHandoff);
	JumpOverHandoffTask->ReadyForActivation();
	return true;
}

void UMHGZBackVaultAbility::HandleJumpOverMovementHandoff()
{
	JumpOverHandoffTask = nullptr;
	if (!IsActive() || bIsEndingBackVault || !IsActionActivationCommitted())
	{
		return;
	}
	if (!StartBackVaultMovement())
	{
		RequestEndAction(EWeaponActionEndReason::Interrupted);
	}
}

bool UMHGZBackVaultAbility::StartBackVaultMovement()
{
	if (!IsActive() || bIsEndingBackVault || BackVaultMovementTask
		|| !IsActionActivationCommitted())
	{
		return false;
	}

	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	const UInsectGlaiveCombatConfig* CombatConfig = Host
		? Cast<UInsectGlaiveCombatConfig>(Host->GetCurrentContext().CombatConfig) : nullptr;
	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!Host || !CombatConfig || !Character)
	{
		return false;
	}

	UAnimInstance* AnimInstance = Character->GetMesh()
		? Character->GetMesh()->GetAnimInstance() : nullptr;
	FAnimMontageInstance* MontageInstance = AnimInstance && AttackMontage
		? AnimInstance->GetActiveInstanceForMontage(AttackMontage) : nullptr;
	if (!MontageInstance)
	{
		return false;
	}

	// Jump has completed.  Disable only the remaining JumpOver root track before
	// CurvedVault acquires the capsule so animation Root Motion cannot compete
	// with the procedural path or rotate the character during the handoff.
	MontageInstance->PushDisableRootMotion();
	bBackVaultVisualRootMotionDisabled = true;

	// An ActionRootMotionPhase placed on the imported Jump sequence may still
	// own the Host at this exact boundary.  Release that exact owner before the
	// CurvedVault task takes ActionMovement; later notify end events are stale
	// and harmless.
	if (Host->IsMontageRootMotionOwnedBy(GetActionToken()))
	{
		Host->ReleaseMontageRootMotion(GetActionToken());
	}

	const float Duration = bUseWhiteBackVault ? CombatConfig->WhiteBackVaultDuration
		: CombatConfig->BackVaultDuration;
	const float Distance = bUseWhiteBackVault ? CombatConfig->WhiteBackVaultDistance
		: CombatConfig->BackVaultDistance;
	const float ApexHeight = bUseWhiteBackVault ? CombatConfig->WhiteBackVaultApexHeight
		: CombatConfig->BackVaultApexHeight;
	const TArray<FBackVaultTrajectoryKey>& Keys = bUseWhiteBackVault
		? WhiteBackVaultTrajectory : BackVaultTrajectory;
	const TArray<FBackVaultClipDriftKey>& DriftKeys = bUseWhiteBackVault
		? WhiteBackVaultClipDrift : BackVaultClipDrift;
	const float JumpDuration = bUseWhiteBackVault ? WhiteBackJumpDuration : BackJumpDuration;
	const float JumpOverDuration = Duration - JumpDuration;
	// The recorded path is normalised over the whole airborne arc -- Jump,
	// JumpOver and the descent -- so every progress here is a fraction of that.
	// It used to be a fraction of Jump + JumpOver only, which put the Jump
	// boundary at 0.375 instead of 0.330 and handed the Jump ~30 cm of travel
	// that belongs to JumpOver.  The CurvedVault window keeps its own duration;
	// the two quantities are no longer derived from one number.
	const float ArcDuration = bUseWhiteBackVault
		? CombatConfig->WhiteBackVaultArcDuration : CombatConfig->BackVaultArcDuration;
	if (!FMath::IsFinite(ArcDuration) || ArcDuration <= KINDA_SMALL_NUMBER)
	{
		return false;
	}
	const float StartProgress = JumpDuration / ArcDuration;
	const float HandoffProgress = (JumpDuration + JumpOverDuration) / ArcDuration;
	const float CurveDuration = JumpOverDuration;
	if (!FMath::IsFinite(StartProgress) || StartProgress <= 0.0f
		|| !FMath::IsFinite(HandoffProgress) || StartProgress >= HandoffProgress
		|| HandoffProgress >= 1.0f || !FMath::IsFinite(CurveDuration)
		|| CurveDuration <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	float CurveDistance = 0.0f;
	FVector HandoffVelocityLocal = FVector::ZeroVector;
	ActiveTrajectoryCurve = BuildTrajectoryCurve(Keys, DriftKeys, StartProgress,
		HandoffProgress, Distance, ApexHeight, CurveDuration, CurveDistance,
		HandoffVelocityLocal);
	if (!ActiveTrajectoryCurve || HandoffVelocityLocal.IsNearlyZero())
	{
		return false;
	}

	FWeaponMovementRequest Request;
	Request.OwnerAction = GetActionToken();
	Request.Mode = EWeaponMovementMode::CurvedVault;
	Request.DirectionSnapshot = -Character->GetActorForwardVector();
	Request.MaxDistance = CurveDistance;
	Request.Duration = CurveDuration;
	Request.PathOffsetCurve = ActiveTrajectoryCurve;
	Request.CurvedVaultHandoffVelocityLocal = HandoffVelocityLocal;
	Request.RotationPolicy = EActionRotationPolicy::Locked;
	Request.CollisionPolicy = EMovementCollisionPolicy::StopOnBlockingHit;
	// Preserve the CurvedVault's tangent at the handoff.  Once the source ends,
	// CMC alone integrates gravity and collision for the free-fall phase.
	Request.CancelVelocityPolicy = EMovementCancelVelocityPolicy::PreserveVelocity;
	if (!Request.HasValidCurvedVaultParameters())
	{
		return false;
	}

	UAbilityTask_MHGZWeaponMovement* Task =
		UAbilityTask_MHGZWeaponMovement::StartWeaponMovement(this, TEXT("BackVaultPath"), Request);
	if (!Task)
	{
		return false;
	}
	BackVaultMovementTask = Task;
	Task->OnFinished.AddDynamic(this,
		&UMHGZBackVaultAbility::HandleBackVaultMovementFinished);
	Task->ReadyForActivation();
	if (!Task->DidStartMovement())
	{
		BackVaultMovementTask = nullptr;
		return false;
	}
	return true;
}

namespace
{
/** Linear interpolation of the recorded local trajectory at one normalised time. */
bool SampleTrajectoryPosition(const TArray<FBackVaultTrajectoryKey>& Keys,
	const float Progress, FVector& OutPosition)
{
	for (int32 Index = 1; Index < Keys.Num(); ++Index)
	{
		const FBackVaultTrajectoryKey& Previous = Keys[Index - 1];
		const FBackVaultTrajectoryKey& Next = Keys[Index];
		if (Progress <= Next.Time)
		{
			const float Span = Next.Time - Previous.Time;
			const float Alpha = Span > KINDA_SMALL_NUMBER
				? (Progress - Previous.Time) / Span : 0.0f;
			OutPosition = FMath::Lerp(Previous.NormalizedPosition, Next.NormalizedPosition,
				FMath::Clamp(Alpha, 0.0f, 1.0f));
			return true;
		}
	}
	return false;
}
}

bool UMHGZBackVaultAbility::ComputeTrajectoryTangent(
	const TArray<FBackVaultTrajectoryKey>& Keys, const float StartProgress,
	const float HandoffProgress, const float TotalDistance, const float ApexHeight,
	const float CurveDuration, FVector& OutVelocityLocal)
{
	OutVelocityLocal = FVector::ZeroVector;
	const float ProgressSpan = HandoffProgress - StartProgress;
	if (Keys.Num() < 2 || TotalDistance <= 0.f || ApexHeight <= 0.f
		|| !FMath::IsFinite(CurveDuration) || CurveDuration <= KINDA_SMALL_NUMBER
		|| !FMath::IsFinite(ProgressSpan) || ProgressSpan <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	FVector HandoffPosition;
	if (!SampleTrajectoryPosition(Keys, HandoffProgress, HandoffPosition))
	{
		return false;
	}

	// Taking a backward difference over the recorded prefix keeps the sample
	// inside the action-owned segment and yields the same vector the curve is
	// about to hand to the CMC, so the hand-off no longer has to be read back
	// from a live root-motion source after the fact.
	float LastKeyBeforeHandoff = StartProgress;
	for (const FBackVaultTrajectoryKey& Key : Keys)
	{
		if (Key.Time <= StartProgress)
		{
			continue;
		}
		if (Key.Time >= HandoffProgress)
		{
			break;
		}
		LastKeyBeforeHandoff = Key.Time;
	}
	// A prefix with no interior key has no measured shape to differentiate, so
	// fall back to the average over the whole span.
	const float AvailableSpan = HandoffProgress - LastKeyBeforeHandoff;
	const float DeltaProgress = AvailableSpan > KINDA_SMALL_NUMBER
		? FMath::Min(ProgressSpan * 0.02f, AvailableSpan * 0.5f) : ProgressSpan;
	FVector BeforeHandoffPosition;
	if (DeltaProgress <= KINDA_SMALL_NUMBER
		|| !SampleTrajectoryPosition(Keys, HandoffProgress - DeltaProgress,
			BeforeHandoffPosition))
	{
		return false;
	}

	const FVector PositionRatePerProgress =
		(HandoffPosition - BeforeHandoffPosition) / DeltaProgress;
	// The curve's normalised time spans ProgressSpan of recorded progress, and the
	// source advances it once over CurveDuration.
	OutVelocityLocal = FVector(
		PositionRatePerProgress.X * TotalDistance,
		PositionRatePerProgress.Y * TotalDistance,
		PositionRatePerProgress.Z * ApexHeight) * (ProgressSpan / CurveDuration);
	return !OutVelocityLocal.ContainsNaN() && !OutVelocityLocal.IsNearlyZero();
}

EBackVaultRootTrackPolicy
UMHGZBackVaultAbility::ResolveRootTrackPolicy(
	const bool bEnableRootMotion, const bool bForceRootLock)
{
	if (bForceRootLock)
	{
		return bEnableRootMotion
			? EBackVaultRootTrackPolicy::Conflicting
			: EBackVaultRootTrackPolicy::LockedNotExtracted;
	}
	return bEnableRootMotion
		? EBackVaultRootTrackPolicy::Extracted
		: EBackVaultRootTrackPolicy::Unaccounted;
}

bool UMHGZBackVaultAbility::SampleClipDriftForwardFraction(
	const TArray<FBackVaultClipDriftKey>& DriftKeys, const float CurveTime,
	float& OutFraction)
{
	OutFraction = 0.0f;
	if (DriftKeys.Num() == 0)
	{
		// No drift to recover is a legitimate configuration.
		return true;
	}
	if (DriftKeys.Num() < 2 || !FMath::IsFinite(CurveTime))
	{
		return false;
	}
	// A first key with a head start, or a table that does not reach the end of
	// the window, both mean the capsule would take a discontinuity at a boundary.
	if (!FMath::IsNearlyZero(DriftKeys[0].CurveTime)
		|| !FMath::IsNearlyEqual(DriftKeys.Last().CurveTime, 1.0f))
	{
		return false;
	}
	// Validate the WHOLE table before sampling anything.  Interpolating in the
	// same pass would only inspect keys up to the sample point, so a malformed
	// tail would slip through on every frame that samples before reaching it.
	for (int32 Index = 0; Index < DriftKeys.Num(); ++Index)
	{
		const FBackVaultClipDriftKey& Key = DriftKeys[Index];
		if (!FMath::IsFinite(Key.CurveTime) || !FMath::IsFinite(Key.ForwardFraction))
		{
			return false;
		}
		if (Index == 0)
		{
			continue;
		}
		const FBackVaultClipDriftKey& Previous = DriftKeys[Index - 1];
		if (Key.CurveTime <= Previous.CurveTime
			|| Key.ForwardFraction < Previous.ForwardFraction)
		{
			return false;
		}
		if (Key.CurveTime >= 1.0f
			&& !FMath::IsNearlyEqual(Key.ForwardFraction, Previous.ForwardFraction))
		{
			// A non-zero terminal slope would make the capsule's real exit
			// velocity differ from the analytic tangent the free fall launches
			// with -- the exact class of bug ComputeTrajectoryTangent exists to
			// prevent.
			return false;
		}
	}

	for (int32 Index = 1; Index < DriftKeys.Num(); ++Index)
	{
		const FBackVaultClipDriftKey& Previous = DriftKeys[Index - 1];
		const FBackVaultClipDriftKey& Next = DriftKeys[Index];
		if (CurveTime <= Next.CurveTime)
		{
			const float Span = Next.CurveTime - Previous.CurveTime;
			const float Alpha = Span > KINDA_SMALL_NUMBER
				? (CurveTime - Previous.CurveTime) / Span : 0.0f;
			OutFraction = FMath::Lerp(Previous.ForwardFraction, Next.ForwardFraction,
				FMath::Clamp(Alpha, 0.0f, 1.0f));
			return true;
		}
	}
	return false;
}

UCurveVector* UMHGZBackVaultAbility::BuildTrajectoryCurve(
	const TArray<FBackVaultTrajectoryKey>& Keys,
	const TArray<FBackVaultClipDriftKey>& DriftKeys, const float StartProgress,
	const float FreeFallHandoffProgress, const float TotalDistance,
	const float ApexHeight, const float CurveDuration, float& OutHandoffDistance,
	FVector& OutHandoffVelocityLocal) const
{
	OutHandoffDistance = 0.0f;
	OutHandoffVelocityLocal = FVector::ZeroVector;
	if (Keys.Num() < 2 || TotalDistance <= 0.f || ApexHeight <= 0.f
		|| StartProgress < 0.f || StartProgress >= FreeFallHandoffProgress
		|| FreeFallHandoffProgress <= 0.f || FreeFallHandoffProgress >= 1.f
		|| !FMath::IsFinite(CurveDuration) || CurveDuration <= KINDA_SMALL_NUMBER)
	{
		return nullptr;
	}

	UCurveVector* Curve = NewObject<UCurveVector>(const_cast<UMHGZBackVaultAbility*>(this));
	if (!Curve)
	{
		return nullptr;
	}
	FVector StartPosition;
	FVector HandoffPosition;
	if (!SampleTrajectoryPosition(Keys, StartProgress, StartPosition)
		|| !SampleTrajectoryPosition(Keys, FreeFallHandoffProgress, HandoffPosition))
	{
		return nullptr;
	}
	const FVector CurveDisplacement = HandoffPosition - StartPosition;
	OutHandoffDistance = CurveDisplacement.X * TotalDistance;
	if (OutHandoffDistance <= KINDA_SMALL_NUMBER)
	{
		return nullptr;
	}

	// The JumpOver clip's root track carries the rest of the forward travel in
	// its pose.  With the sequence locked to the ref pose that lead no longer
	// exists, so the capsule has to take the same distance here or the move
	// lands short by exactly that amount.
	float RecoveredFraction = 0.0f;
	if (!SampleClipDriftForwardFraction(DriftKeys, 1.0f, RecoveredFraction))
	{
		return nullptr;
	}
	OutHandoffDistance += RecoveredFraction * TotalDistance;
	if (OutHandoffDistance <= KINDA_SMALL_NUMBER)
	{
		return nullptr;
	}

	if (!ComputeTrajectoryTangent(Keys, StartProgress, FreeFallHandoffProgress,
		TotalDistance, ApexHeight, CurveDuration, OutHandoffVelocityLocal))
	{
		return nullptr;
	}
	float PreviousTime = -1.0f;
	auto AddTrajectoryKey = [&Curve, &PreviousTime, TotalDistance, ApexHeight,
		&OutHandoffDistance, &StartPosition, &DriftKeys](const float NormalizedTime,
		const FVector& Position)
	{
		if (!FMath::IsFinite(NormalizedTime) || NormalizedTime < 0.f || NormalizedTime > 1.f
			|| NormalizedTime <= PreviousTime || Position.ContainsNaN())
		{
			return false;
		}
		float DriftFraction = 0.0f;
		if (!SampleClipDriftForwardFraction(DriftKeys, NormalizedTime, DriftFraction))
		{
			return false;
		}
		// MoveToForce contributes NormalizedTime * endpoint X.  The curve supplies
		// the exact sampled lateral, vertical and non-linear X remainder, plus the
		// forward distance recovered from the clip's now-locked root track.
		const FVector RelativePosition = Position - StartPosition;
		const float OffsetX = RelativePosition.X * TotalDistance
			+ DriftFraction * TotalDistance
			- NormalizedTime * OutHandoffDistance;
		AddLinearKey(Curve->FloatCurves[0], NormalizedTime, OffsetX);
		AddLinearKey(Curve->FloatCurves[1], NormalizedTime, RelativePosition.Y * TotalDistance);
		AddLinearKey(Curve->FloatCurves[2], NormalizedTime, RelativePosition.Z * ApexHeight);
		PreviousTime = NormalizedTime;
		return true;
	};

	if (!AddTrajectoryKey(0.0f, StartPosition))
	{
		return nullptr;
	}
	for (const FBackVaultTrajectoryKey& Key : Keys)
	{
		if (Key.Time <= StartProgress)
		{
			continue;
		}
		if (Key.Time >= FreeFallHandoffProgress)
		{
			break;
		}
		const float CurveTime = (Key.Time - StartProgress)
			/ (FreeFallHandoffProgress - StartProgress);
		if (!AddTrajectoryKey(CurveTime, Key.NormalizedPosition))
		{
			return nullptr;
		}
	}
	if (!AddTrajectoryKey(1.0f, HandoffPosition))
	{
		return nullptr;
	}
	return FMath::IsNearlyZero(Keys[0].Time)
		&& FMath::IsNearlyEqual(PreviousTime, 1.0f)
		? Curve : nullptr;
}

void UMHGZBackVaultAbility::HandleBackVaultVisualCompleted()
{
	BackVaultVisualTask = nullptr;
	if (!IsActive() || bIsEndingBackVault)
	{
		return;
	}
	bVisualFinished = true;
	TryFinishBackVault();
}

void UMHGZBackVaultAbility::HandleBackVaultVisualInterrupted()
{
	BackVaultVisualTask = nullptr;
	if (IsActive() && !bIsEndingBackVault)
	{
		RequestEndAction(EWeaponActionEndReason::Interrupted);
	}
}

void UMHGZBackVaultAbility::HandleBackVaultMovementFinished(
	const FWeaponMovementResult& MovementResult)
{
	BackVaultMovementTask = nullptr;
	ActiveTrajectoryCurve = nullptr;
	if (!IsActive() || bIsEndingBackVault)
	{
		return;
	}
	if (MovementResult.EndReason != EWeaponMovementEndReason::Completed)
	{
		RequestEndAction(EWeaponActionEndReason::Interrupted);
		return;
	}
	bMovementFinished = true;
	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	bBeginFreeFallAfterEnd = Character && Character->GetCharacterMovement()
		&& Character->GetCharacterMovement()->IsFalling();
	// CurvedVault has just switched CMC from Flying to Falling.  End on this
	// deterministic hand-off, not on the visual montage's natural completion:
	// the visual instance intentionally holds its terminal pose until EndAbility
	// starts the system-owned falling montage.
	RequestEndAction(bBeginFreeFallAfterEnd
		? EWeaponActionEndReason::Normal : EWeaponActionEndReason::Interrupted);
}

void UMHGZBackVaultAbility::TryFinishBackVault()
{
	if (bVisualFinished && bMovementFinished && IsActive() && !bIsEndingBackVault)
	{
		RequestEndAction(EWeaponActionEndReason::Normal);
	}
}
