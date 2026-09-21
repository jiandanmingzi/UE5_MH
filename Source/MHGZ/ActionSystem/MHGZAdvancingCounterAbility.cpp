// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZAdvancingCounterAbility.h"

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "ActionSystem/AbilityTask_MHGZWeaponMovement.h"
#include "ActionSystem/MHGZIncomingHitResolverComponent.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "AttributeSystem/Res_InsectGlaive.h"
#include "InsectGlaive/InsectGlaiveCombatConfig.h"
#include "MHGZCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"
#include "MHGZ.h"

namespace
{
const FGameplayTag& AdvancingCounterOpenTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.IG.AdvancingCounterOpen"));
	return Tag;
}
}

UMHGZAdvancingCounterAbility::UMHGZAdvancingCounterAbility()
{
	// This CDO-level soft reference is why the Content Browser rename dialog warns
	// "Source code, config INI, and text files may need Find/Replace":
	// FAssetRenameManager::FindCDOReferences walks every CDO's soft references and
	// refuses the rename (OKCancel defaulting to Cancel) when it finds one.  The
	// cooker never sees a CDO's soft paths either, so this asset only reaches a
	// cooked build through DirectoriesToAlwaysCook in DefaultGame.ini -- renaming
	// the asset without updating this string silently drops the montage and makes
	// StartAdvancingCounterVaultVisual() return false with no log.
	DanceVaultSequence = TSoftObjectPtr<UAnimSequenceBase>(FSoftObjectPath(
		TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_TuJinHuiXuanWuTa.AS_Unsh_TuJinHuiXuanWuTa")));
	// Authored by UMHGZWuTaMontageSetupCommandlet, which bakes DanceVaultDuration
	// into the segment's AnimPlayRate.  A CDO-level override replaces it.
	DanceVaultMontage = TSoftObjectPtr<UAnimMontage>(FSoftObjectPath(
		TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_WuTa.AM_IG_WuTa")));
}

void UMHGZAdvancingCounterAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	bCounterSucceeded = false;
	bBeginFreeFallAfterEnd = false;
	bPlayLandedPresentation = false;
	bIsEndingCounterAbility = false;
	AdvancingCounterVaultTask = nullptr;
	AdvancingCounterVaultMontageTask = nullptr;
	CounterWindows.Reset();
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}

void UMHGZAdvancingCounterAbility::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	if (bIsEndingCounterAbility)
	{
		return;
	}
	bIsEndingCounterAbility = true;
	CloseAllAdvancingCounterWindows();
	// Combat.State.Aerial.Actionable 不在这里释放：基类 EndAbility 的
	// CloseAerialHandoff() 负责（本函数末尾会走 Super::EndAbility）。
	if (AdvancingCounterVaultMontageTask)
	{
		AdvancingCounterVaultMontageTask->EndTask();
		AdvancingCounterVaultMontageTask = nullptr;
	}
	AdvancingCounterVaultTask = nullptr;
	ACharacter* EndingCharacter = ActorInfo && ActorInfo->AvatarActor.IsValid()
		? Cast<ACharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	const bool bStartFreeFall = bBeginFreeFallAfterEnd && !bWasCancelled
		&& EndingCharacter && EndingCharacter->GetCharacterMovement()
		&& EndingCharacter->GetCharacterMovement()->IsFalling();
	// 触地类收尾：JumpForce 的抛物线在 f=1 回到起跳高度，所以本动作没有、
	// 也不该有自由落体阶段（Rise 实录里舞踏是单一动作 id 覆盖升+降+触地，
	// 动画与移动同长同终）。落地姿势直接认领，不经过 AerialFalling。
	const bool bPlayLanding = bPlayLandedPresentation && !bWasCancelled;
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility,
		bWasCancelled);
	if (bPlayLanding)
	{
		if (UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost())
		{
			Host->PlayAerialLandingPresentation();
		}
	}
	if (bStartFreeFall)
	{
		if (UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost())
		{
			Host->BeginAerialFalling(true,
				FGameplayTag::RequestGameplayTag(TEXT("Combat.State.Aerial.Falling.IG_DanceVault")));
		}
	}
}

bool UMHGZAdvancingCounterAbility::ValidateActionDependencies() const
{
	if (!Super::ValidateActionDependencies() || !GetIGResourceComponent())
	{
		return false;
	}

	const AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Avatar || !Avatar->FindComponentByClass<UMHGZIncomingHitResolverComponent>())
	{
		return false;
	}

	// A montage assigned but missing on disk must not silently fall through to the
	// runtime montage: that path has different timing semantics (it cannot retime
	// the clip), so the substitution would be invisible until the pose desynced.
	if (!DanceVaultMontage.IsNull())
	{
		const UAnimMontage* Montage = DanceVaultMontage.LoadSynchronous();
		if (!Montage || !Montage->GetSkeleton())
		{
			UE_LOG(LogMHGZ, Warning,
				TEXT("[AdvancingCounter] DanceVaultMontage %s is missing or has no skeleton; refusing activation"),
				*DanceVaultMontage.ToSoftObjectPath().ToString());
			return false;
		}
	}
	return true;
}

bool UMHGZAdvancingCounterAbility::BeginAdvancingCounterWindow(
	FName NotifyEventID, float TotalDuration)
{
	if (!IsActive() || bIsEndingCounterAbility || bCounterSucceeded
		|| !IsActionActivationCommitted() || NotifyEventID.IsNone()
		|| !FMath::IsFinite(TotalDuration) || TotalDuration <= 0.f)
	{
		return false;
	}
	if (CounterWindows.Contains(NotifyEventID))
	{
		return true;
	}

	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	const FWeaponActionToken& ActionToken = GetActionToken();
	AActor* Avatar = GetAvatarActorFromActorInfo();
	UMHGZIncomingHitResolverComponent* Resolver = Avatar
		? Avatar->FindComponentByClass<UMHGZIncomingHitResolverComponent>() : nullptr;
	if (!Host || !Resolver || !ActionToken.IsValid()
		|| !Host->IsTokenCurrent(ActionToken.RuntimeToken))
	{
		return false;
	}

	FGameplayTagContainer Tags;
	Tags.AddTag(AdvancingCounterOpenTag());
	FCounterWindowState State;
	State.TagToken = Host->AcquireTags(EWeaponTagOwnerKind::NotifyWindow,
		ActionToken.AbilityHandle, ActionToken.ActivationSequenceID, NotifyEventID, Tags);
	if (!State.TagToken.IsValid())
	{
		return false;
	}

	const TWeakObjectPtr<UMHGZAdvancingCounterAbility> WeakThis(this);
	const FWeaponActionToken ExpectedAction = ActionToken;
	State.ResolverTokenID = Resolver->RegisterInterceptorNative(ExpectedAction,
		CounterInterceptorPriority, TotalDuration + KINDA_SMALL_NUMBER,
		[WeakThis, ExpectedAction](const FIncomingHitContext& Context)
		{
			if (UMHGZAdvancingCounterAbility* Ability = WeakThis.Get())
			{
				return Ability->HandleIncomingHit(ExpectedAction, Context);
			}
			return EIncomingHitInterceptResult::Pass;
		});
	if (State.ResolverTokenID == 0)
	{
		Host->ReleaseTags(State.TagToken);
		return false;
	}

	CounterWindows.Add(NotifyEventID, MoveTemp(State));
	return true;
}

void UMHGZAdvancingCounterAbility::EndAdvancingCounterWindow(FName NotifyEventID)
{
	FCounterWindowState* State = CounterWindows.Find(NotifyEventID);
	if (!State)
	{
		return;
	}

	AActor* Avatar = GetAvatarActorFromActorInfo();
	if (UMHGZIncomingHitResolverComponent* Resolver = Avatar
		? Avatar->FindComponentByClass<UMHGZIncomingHitResolverComponent>() : nullptr)
	{
		Resolver->UnregisterInterceptor(State->ResolverTokenID);
	}
	if (UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost())
	{
		Host->ReleaseTags(State->TagToken);
	}
	CounterWindows.Remove(NotifyEventID);
}

bool UMHGZAdvancingCounterAbility::AddAdvancingCounterDanceStack()
{
	if (URes_InsectGlaive* Resource = GetIGResourceComponent())
	{
		return Resource->AddDanceStack(EIGDanceSource::AdvancingCounter);
	}
	return false;
}

EIncomingHitInterceptResult UMHGZAdvancingCounterAbility::HandleIncomingHit(
	const FWeaponActionToken& ExpectedAction, const FIncomingHitContext& Context)
{
	if (!Context.bCounterable || bCounterSucceeded || bIsEndingCounterAbility
		|| !IsActive() || !IsActionActivationCommitted()
		|| ExpectedAction != GetActionToken() || CounterWindows.IsEmpty())
	{
		return EIncomingHitInterceptResult::Pass;
	}

	// Only a successful Resource operation is allowed to consume the real incoming hit.
	// This keeps a broken/rebuilt runtime from accidentally turning into invulnerability.
	if (!AddAdvancingCounterDanceStack())
	{
		return EIncomingHitInterceptResult::Pass;
	}

	bCounterSucceeded = true;
	DisableCollision(); // Stop all later ground-attack segments before the montage is cancelled.
	CloseAllAdvancingCounterWindows();
	if (!SuspendAttackMontageForFollowup() || !StartAdvancingCounterVault())
	{
		// The hit has already been accepted and the dance stack was successfully
		// committed. A broken movement setup must end this exact action rather
		// than leaving a cancelled ground montage with a live ActionToken.
		RequestEndAction(EWeaponActionEndReason::Interrupted);
		return EIncomingHitInterceptResult::Consume;
	}
	OnAdvancingCounterSucceeded(Context);
	return EIncomingHitInterceptResult::Consume;
}

bool UMHGZAdvancingCounterAbility::StartAdvancingCounterVault()
{
	if (!IsActive() || bIsEndingCounterAbility || AdvancingCounterVaultTask
		|| !IsActionActivationCommitted())
	{
		return false;
	}

	UMHGZWeaponRuntimeHostComponent* Host = GetRuntimeHost();
	const UInsectGlaiveCombatConfig* CombatConfig = Host
		? Cast<UInsectGlaiveCombatConfig>(Host->GetCurrentContext().CombatConfig) : nullptr;
	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!Host || !CombatConfig || !Character
		|| Host->IsMontageRootMotionOwnedBy(GetActionToken()))
	{
		return false;
	}

	FWeaponMovementRequest Request;
	Request.OwnerAction = GetActionToken();
	Request.Mode = EWeaponMovementMode::BallisticVault;
	Request.DirectionSnapshot = Character->GetActorForwardVector();
	Request.BallisticMode = CombatConfig->DanceVaultBallisticMode;
	Request.ApexHeight = CombatConfig->DanceVaultApexHeight;
	Request.Duration = CombatConfig->DanceVaultDuration;
	// ApexHeightAndDuration 模式不会从别的字段推导水平位移；不设这一项，
	// JumpForce 的 Distance 为 0，落点会比 Rise 更贴脚下。
	Request.MaxDistance = CombatConfig->DanceVaultDistance;
	Request.LaunchVelocity = CombatConfig->DanceVaultLaunchVelocity;
	Request.RotationPolicy = EActionRotationPolicy::Locked;
	Request.CancelVelocityPolicy = EMovementCancelVelocityPolicy::PreserveVelocity;

	if (!Request.HasValidBallisticParameters())
	{
		return false;
	}

	UAbilityTask_MHGZWeaponMovement* Task =
		UAbilityTask_MHGZWeaponMovement::StartWeaponMovement(this,
			TEXT("AdvancingCounterVault"), Request);
	if (!Task)
	{
		return false;
	}

	AdvancingCounterVaultTask = Task;
	Task->OnFinished.AddDynamic(this,
		&UMHGZAdvancingCounterAbility::HandleAdvancingCounterVaultFinished);
	Task->ReadyForActivation();
	if (!Task->DidStartMovement())
	{
		AdvancingCounterVaultTask = nullptr;
		return false;
	}

	// AS_Unsh_TuJinHuiXuanWuTa is intentionally in-place.  Its visual playback may end
	// before or after the editable ballistic duration, but never owns CMC
	// movement or this Action's completion; the vault task/landing does.
	StartAdvancingCounterVaultVisual();
	return true;
}

bool UMHGZAdvancingCounterAbility::StartAdvancingCounterVaultVisual()
{
	if (!IsActive() || bIsEndingCounterAbility || AdvancingCounterVaultMontageTask)
	{
		return false;
	}

	// Prefer the authored asset.  The runtime fallback is kept because it needs no
	// asset to exist, but it cannot retime the clip (see DanceVaultMontage), so a
	// missing asset is the only case where DanceVaultAnimationPlayRate applies.
	UAnimMontage* Montage = DanceVaultMontage.LoadSynchronous();
	if (!Montage)
	{
		if (DanceVaultSequence.IsNull())
		{
			return false;
		}
		UAnimSequenceBase* Sequence = DanceVaultSequence.LoadSynchronous();
		if (!Sequence)
		{
			return false;
		}
		Montage = UAnimMontage::CreateSlotAnimationAsDynamicMontage(
			Sequence, DanceVaultMontageSlot, DanceVaultBlendInTime, DanceVaultBlendOutTime,
			DanceVaultAnimationPlayRate, 1);
	}
	if (!Montage)
	{
		return false;
	}
	DetectAerialHandoffNotify(Montage);

	AdvancingCounterVaultMontageTask =
		UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this, TEXT("AdvancingCounterVaultVisual"), Montage, 1.0f,
			NAME_None, true, 1.0f, 0.0f, true);
	if (!AdvancingCounterVaultMontageTask)
	{
		return false;
	}

	// Intentionally no completion delegates: a presentation-only sequence must
	// not end the live Action while BallisticVault is still airborne.
	AdvancingCounterVaultMontageTask->ReadyForActivation();

	// Register the instance with the Host, exactly as every other montage-playing
	// ability does (UMHGZPoleVaultAbility::BeginBackVaultInitialFlight, MHGZAttackAbility.cpp:415,
	// MHGZDodgeAbility.cpp:262, ...).  MHGZ::AnimNotify::ResolveAction finds the
	// active ActionToken through Host->ResolveMontage(Mesh, MontageInstanceID), so
	// without this the dance vault's montage can never resolve a notify: the
	// notifies fire, find no token, and silently do nothing.
	//
	// Measured before this line existed: the aerial-action window's tag appeared
	// for every back vault and for none of the nine dance vaults, which made the
	// landing gate read "window never opened" and divert every dance-vault
	// touchdown into a one-frame free fall.
	ACharacter* VisualCharacter = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	UAnimInstance* AnimInstance = VisualCharacter && VisualCharacter->GetMesh()
		? VisualCharacter->GetMesh()->GetAnimInstance() : nullptr;
	FAnimMontageInstance* MontageInstance = AnimInstance
		? AnimInstance->GetActiveInstanceForMontage(Montage) : nullptr;
	if (!MontageInstance || !AnimInstance
		|| !RegisterMontageInstance(VisualCharacter->GetMesh(), MontageInstance->GetInstanceID()))
	{
		AdvancingCounterVaultMontageTask->EndTask();
		AdvancingCounterVaultMontageTask = nullptr;
		return false;
	}
	return true;
}

void UMHGZAdvancingCounterAbility::HandleAdvancingCounterVaultFinished(
	const FWeaponMovementResult& MovementResult)
{
	AdvancingCounterVaultTask = nullptr;
	if (!IsActive() || bIsEndingCounterAbility)
	{
		return;
	}

	const EWeaponActionEndReason EndReason =
		(MovementResult.EndReason == EWeaponMovementEndReason::Completed
			|| MovementResult.EndReason == EWeaponMovementEndReason::Landed)
		? EWeaponActionEndReason::Normal
		: EWeaponActionEndReason::Interrupted;
	const ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	const UCharacterMovementComponent* Movement = Character
		? Character->GetCharacterMovement() : nullptr;
	// 只吃结束原因 —— 见 UMHGZInsectGlaiveAbility::ResolveVaultExit 的注释。
	// 这里原来还有一道「挂了窗口但没到过窗口就把落地翻成自由落体」的闸门，
	// 它是在用「动作到没到最早可操作帧」去代理「胶囊在半空吗」这个问题。
	// 现在弧全程由 MOVE_Flying 独占，触地只可能发生在通知交出胶囊之后，
	// 所以那次误判（木桩圆顶上离地 200 cm 就认领落地）在构造上不可能再发生，
	// 闸门连同它的两个查询一起消失。
	const EVaultExit Exit = ResolveVaultExit(MovementResult.EndReason);
	bBeginFreeFallAfterEnd = Exit == EVaultExit::FreeFall;
	bPlayLandedPresentation = Exit == EVaultExit::LandedPresentation;
	RequestEndAction(EndReason);
}

void UMHGZAdvancingCounterAbility::CloseAllAdvancingCounterWindows()
{
	TArray<FName> WindowIDs;
	CounterWindows.GetKeys(WindowIDs);
	for (const FName WindowID : WindowIDs)
	{
		EndAdvancingCounterWindow(WindowID);
	}
}
