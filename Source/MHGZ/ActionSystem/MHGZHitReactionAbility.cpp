// Copyright MHGZ Project. All Rights Reserved.

#include "MHGZHitReactionAbility.h"

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "ActionSystem/MHGZComboCoordinatorAbility.h"
#include "ActionSystem/MHGZGameplayAbility.h"
#include "ActionSystem/MHGZGameplayEffectContext.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/Actor.h"

namespace
{
FGameplayTag Tag(const TCHAR* Name)
{
	return FGameplayTag::RequestGameplayTag(Name);
}

FGameplayTag LightStaggerTag()
{
	return Tag(TEXT("Combat.Stagger.Light"));
}

FGameplayTag MediumStaggerTag()
{
	return Tag(TEXT("Combat.Stagger.Medium"));
}

FGameplayTag HeavyStaggerTag()
{
	return Tag(TEXT("Combat.Stagger.Heavy"));
}
}

UMHGZHitReactionAbility::UMHGZHitReactionAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerExecution;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalOnly;

	FAbilityTriggerData HitTrigger;
	HitTrigger.TriggerTag = Tag(TEXT("Combat.Event.HitStagger"));
	HitTrigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	AbilityTriggers.Add(HitTrigger);

	// Activation-owned tags are reference counted per execution.  A second hit
	// can interrupt the first dynamic Montage while retaining Hitstun until the
	// replacement reaction ends.
	ActivationOwnedTags.AddTag(Tag(TEXT("Combat.State.Hitstun")));
	ActivationOwnedTags.AddTag(Tag(TEXT("Combat.State.BlockMovement")));

	LightForwardSequence = TSoftObjectPtr<UAnimSequenceBase>(FSoftObjectPath(
		TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_Hited_Little_Forward.AS_Unsh_Hited_Little_Forward")));
	LightBackSequence = TSoftObjectPtr<UAnimSequenceBase>(FSoftObjectPath(
		TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_Hitted_Little_Back.AS_Unsh_Hitted_Little_Back")));
	LightLeftSequence = TSoftObjectPtr<UAnimSequenceBase>(FSoftObjectPath(
		TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_Hitted_Little_Left.AS_Unsh_Hitted_Little_Left")));
	LightRightSequence = TSoftObjectPtr<UAnimSequenceBase>(FSoftObjectPath(
		TEXT("/Game/Weapons/InsectGlaive/Anims/Sequences/Imported/AS_Unsh_Hitted_LIttle_Right.AS_Unsh_Hitted_LIttle_Right")));
}

EMHGZLightHitDirection UMHGZHitReactionAbility::ResolveDirection(
	const FVector& SourceLocation, const FVector& TargetLocation,
	const FRotator& TargetRotation)
{
	FVector LocalSource = TargetRotation.UnrotateVector(SourceLocation - TargetLocation);
	LocalSource.Z = 0.0f;
	if (LocalSource.IsNearlyZero())
	{
		return EMHGZLightHitDirection::Forward;
	}
	LocalSource.Normalize();
	if (FMath::Abs(LocalSource.X) >= FMath::Abs(LocalSource.Y))
	{
		return LocalSource.X >= 0.0f
			? EMHGZLightHitDirection::Forward
			: EMHGZLightHitDirection::Back;
	}
	return LocalSource.Y >= 0.0f
		? EMHGZLightHitDirection::Right
		: EMHGZLightHitDirection::Left;
}

int32 UMHGZHitReactionAbility::GetStaggerPriority(const FGameplayTag& StaggerTag)
{
	if (StaggerTag == LightStaggerTag())
	{
		return 1;
	}
	if (StaggerTag == MediumStaggerTag())
	{
		return 2;
	}
	if (StaggerTag == HeavyStaggerTag())
	{
		return 3;
	}
	return 0;
}

bool UMHGZHitReactionAbility::CanIncomingStaggerInterrupt(
	int32 IncomingPriority, int32 ActivePriority)
{
	if (IncomingPriority <= 0 || ActivePriority <= 0)
	{
		return false;
	}
	// Heavy is deliberately self-interruptible; Light and Medium are not.
	return IncomingPriority > ActivePriority
		|| (IncomingPriority == 3 && ActivePriority == 3);
}

bool UMHGZHitReactionAbility::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags,
		OptionalRelevantTags))
	{
		return false;
	}

	const UAbilitySystemComponent* ASC = ActorInfo
		? ActorInfo->AbilitySystemComponent.Get()
		: nullptr;
	return ASC
		&& !ASC->HasMatchingGameplayTag(Tag(TEXT("Combat.State.Sheathed")))
		&& !ASC->HasMatchingGameplayTag(Tag(TEXT("Combat.State.Dead")));
}

void UMHGZHitReactionAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	bEndingReaction = false;
	MontageTask = nullptr;
	ActiveDynamicMontage = nullptr;
	ActiveStaggerPriority = 0;

	const FMHGZGameplayEffectContext* Context = TriggerEventData
		? FMHGZGameplayEffectContext::ExtractEffectContext(TriggerEventData->ContextHandle)
		: nullptr;
	const int32 IncomingPriority = Context
		? GetStaggerPriority(Context->HitStaggerTag)
		: 0;
	if (IncomingPriority <= 0 || IsSuppressedByActiveReaction(IncomingPriority))
	{
		FinishReaction(true);
		return;
	}
	// Medium and Heavy already participate in priority arbitration, but do not
	// yet own substitute visuals: neither may fall back to a misleading Light
	// flinch until their real hit animations are imported and assigned.
	if (Context->HitStaggerTag != LightStaggerTag())
	{
		FinishReaction(true);
		return;
	}

	AActor* TargetActor = GetAvatarActorFromActorInfo();
	const AActor* SourceActor = TriggerEventData ? TriggerEventData->Instigator.Get() : nullptr;
	if (!SourceActor)
	{
		SourceActor = Context->GetInstigator();
	}
	if (!TargetActor || !SourceActor)
	{
		FinishReaction(true);
		return;
	}

	const EMHGZLightHitDirection Direction = ResolveDirection(
		SourceActor->GetActorLocation(), TargetActor->GetActorLocation(),
		TargetActor->GetActorRotation());
	UAnimSequenceBase* Sequence = GetLightSequence(Direction).LoadSynchronous();
	if (!Sequence)
	{
		FinishReaction(true);
		return;
	}

	ActiveStaggerPriority = IncomingPriority;
	CancelActivePlayerActions();
	ActiveDynamicMontage = UAnimMontage::CreateSlotAnimationAsDynamicMontage(
		Sequence, MontageSlotName, BlendInTime, BlendOutTime, 1.0f, 1);
	if (!ActiveDynamicMontage)
	{
		FinishReaction(true);
		return;
	}

	MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, FName(TEXT("HitReaction")), ActiveDynamicMontage, 1.0f, NAME_None,
		true, 1.0f, 0.0f, true);
	if (!MontageTask)
	{
		FinishReaction(true);
		return;
	}

	MontageTask->OnCompleted.AddDynamic(this, &UMHGZHitReactionAbility::OnMontageCompleted);
	MontageTask->OnInterrupted.AddDynamic(this, &UMHGZHitReactionAbility::OnMontageInterrupted);
	MontageTask->OnCancelled.AddDynamic(this, &UMHGZHitReactionAbility::OnMontageCancelled);
	MontageTask->ReadyForActivation();
}

void UMHGZHitReactionAbility::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	MontageTask = nullptr;
	ActiveDynamicMontage = nullptr;
	ActiveStaggerPriority = 0;
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility,
		bWasCancelled);
}

bool UMHGZHitReactionAbility::IsSuppressedByActiveReaction(
	int32 IncomingPriority) const
{
	const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC)
	{
		return false;
	}

	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		for (UGameplayAbility* Instance : Spec.GetAbilityInstances())
		{
			const UMHGZHitReactionAbility* Existing =
				Cast<UMHGZHitReactionAbility>(Instance);
			if (!Existing || Existing == this || !Existing->IsActive()
				|| Existing->ActiveStaggerPriority <= 0)
			{
				continue;
			}
			if (!CanIncomingStaggerInterrupt(IncomingPriority,
				Existing->ActiveStaggerPriority))
			{
				return true;
			}
		}
	}
	return false;
}

const TSoftObjectPtr<UAnimSequenceBase>& UMHGZHitReactionAbility::GetLightSequence(
	EMHGZLightHitDirection Direction) const
{
	switch (Direction)
	{
	case EMHGZLightHitDirection::Back:
		return LightBackSequence;
	case EMHGZLightHitDirection::Left:
		return LightLeftSequence;
	case EMHGZLightHitDirection::Right:
		return LightRightSequence;
	case EMHGZLightHitDirection::Forward:
	default:
		return LightForwardSequence;
	}
}

void UMHGZHitReactionAbility::CancelActivePlayerActions()
{
	UMHGZAbilitySystemComponent* ASC = Cast<UMHGZAbilitySystemComponent>(
		GetAbilitySystemComponentFromActorInfo());
	if (!ASC)
	{
		return;
	}

	if (UGA_WeaponComboCoordinator* Coordinator = ASC->GetActiveComboCoordinator())
	{
		Coordinator->ResetCombo(EWeaponActionEndReason::Interrupted);
	}

	// ResetCombo owns the active Combo transition.  Direct actions (Dodge,
	// Sheathe and kinsect actions) are separate from it, so finish them here.
	TArray<TObjectPtr<UMHGZGameplayAbility>> ActionsToEnd;
	for (FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		for (UGameplayAbility* Instance : Spec.GetAbilityInstances())
		{
			UMHGZGameplayAbility* Action = Cast<UMHGZGameplayAbility>(Instance);
			if (Action && Action->IsActive()
				&& !Action->IsA<UGA_WeaponComboCoordinator>())
			{
				ActionsToEnd.Add(Action);
			}
		}
	}
	for (UMHGZGameplayAbility* Action : ActionsToEnd)
	{
		if (Action && Action->IsActive())
		{
			Action->RequestEndAction(EWeaponActionEndReason::Interrupted);
		}
	}
}

void UMHGZHitReactionAbility::FinishReaction(bool bWasCancelled)
{
	if (bEndingReaction || !IsActive())
	{
		return;
	}
	bEndingReaction = true;
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, false,
		bWasCancelled);
}

void UMHGZHitReactionAbility::OnMontageCompleted()
{
	FinishReaction(false);
}

void UMHGZHitReactionAbility::OnMontageInterrupted()
{
	FinishReaction(true);
}

void UMHGZHitReactionAbility::OnMontageCancelled()
{
	FinishReaction(true);
}
