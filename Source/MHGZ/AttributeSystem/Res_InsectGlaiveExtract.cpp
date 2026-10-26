// Copyright MHGZ Project. All Rights Reserved.

// 精华与舞踏资源、成本预约及资源UI快照。
#include "Res_InsectGlaive.h"
#include "MHGZGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameplayEffect.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"
#include "ActionSystem/MHGZKinsectSlashAbility.h"
#include "ActionSystem/MHGZKinsectPiercerAbility.h"
#include "ActionSystem/MHGZDescendingThrustAbility.h"
#include "GameplayEffectTypes.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "InsectGlaive/IGPowderActor.h"
#include "InsectGlaive/Kinsect/Kinsect.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

namespace
{
const FGameplayTag& ExtractGainedCueTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("GameplayCue.IG.ExtractGained"));
	return Tag;
}

const FGameplayTag& ExtractExpiredCueTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("GameplayCue.IG.ExtractExpired"));
	return Tag;
}

const FGameplayTag& TripleUpActivatedCueTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("GameplayCue.IG.TripleUpActivated"));
	return Tag;
}

const FGameplayTag& TripleUpSourceTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("WeaponResource.IG.TripleUp"));
	return Tag;
}

/** 舞踏来源的ASC镜像标签；真值仍是DanceSource字段。 */
const FGameplayTag& DanceSourceTag(const EIGDanceSource Source)
{
	static const FGameplayTag KinsectSlashTag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Aerial.Dance.Source.KinsectSlash"));
	static const FGameplayTag AdvancingCounterTag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Aerial.Dance.Source.AdvancingCounter"));
	static const FGameplayTag AwakenedKinsectTag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Aerial.Dance.Source.AwakenedKinsect"));
	switch (Source)
	{
	case EIGDanceSource::KinsectSlash: return KinsectSlashTag;
	case EIGDanceSource::AdvancingCounter: return AdvancingCounterTag;
	case EIGDanceSource::AwakenedKinsect: return AwakenedKinsectTag;
	default: return FGameplayTag::EmptyTag;
	}
}
}

void URes_InsectGlaive::RecordTripleCostTelemetryEvent(
	const uint32 ActivationSequenceID, const uint64 ReservationID,
	const FName EventName, const FName Reason) const
{
	FWeaponCombatTelemetryEvent Event;
	Event.EventName = EventName;
	Event.ActivationSequenceID = ActivationSequenceID;
	Event.ReservationID = ReservationID;
	Event.ResourceTag = FName(TEXT("Cost.IG.TripleUp"));
	Event.Reason = Reason;
	RecordCombatTelemetryEvent(MoveTemp(Event));
}

float URes_InsectGlaive::GetKinsectStaminaDrainMultiplier() const
{
	const bool bTriple = IsTripleUpActive();
	if (!bTriple && !HasExtract(MHGZGameplayTags::IGExtractRed())) return 1.0f;
	const float Default = bTriple ? 0.5f : 0.75f;
	const float Configured = CombatConfig ? (bTriple
		? CombatConfig->TripleKinsectStaminaDrainMultiplier
		: CombatConfig->RedKinsectStaminaDrainMultiplier) : Default;
	return FMath::IsFinite(Configured) ? FMath::Clamp(Configured, 0.0f, 1.0f) : Default;
}

bool URes_InsectGlaive::IsLeafExtractTag(const FGameplayTag& ExtractColor) const
{
	return ExtractColor.MatchesTagExact(MHGZGameplayTags::IGExtractWhite())
		|| ExtractColor.MatchesTagExact(MHGZGameplayTags::IGExtractOrange())
		|| ExtractColor.MatchesTagExact(MHGZGameplayTags::IGExtractRed());
}

bool URes_InsectGlaive::IsHandleActive(const FActiveGameplayEffectHandle& Handle) const
{
	const UAbilitySystemComponent* ASC = GetPlayerASC();
	return ASC && Handle.IsValid() && ASC->GetActiveGameplayEffect(Handle) != nullptr;
}

bool URes_InsectGlaive::ApplyExtract(FGameplayTag ExtractColor)
{
	return ApplyExtractInternal(ExtractColor, nullptr, FGuid(), FGuid(), 0);
}

bool URes_InsectGlaive::ApplyExtractFromKinsectHit(
	const FGameplayTag ExtractColor, const FHitResult& Hit,
	const FGuid& SourceAttackID, const FGuid& HitID, const uint64 CommandSerial)
{
	return ApplyExtractInternal(ExtractColor, &Hit, SourceAttackID, HitID, CommandSerial);
}

bool URes_InsectGlaive::CaptureExtractFromKinsectHit(
	const FKinsectFlightRequest& Request, const FHitResult& Hit, const FGuid& HitID)
{
	const UMHGZMonsterHitzoneComponent* Hitzone =
		UMHGZMonsterHitzoneComponent::Resolve(Hit.GetComponent());
	if (!Hitzone || !KinsectActor || !HitID.IsValid()
		|| Request.ExtractMode != EKinsectExtractMode::CollectPerValidHit
		|| !IsKinsectCommandCurrent(Request)
		|| KinsectActor->ActiveRequest.FlightInstanceID != Request.FlightInstanceID
		|| !KinsectActor->CapturePendingExtractColor(Hitzone->ExtractColorTag, true))
	{
		return false;
	}
	FWeaponCombatTelemetryEvent Event;
	Event.EventName = TEXT("KinsectCargoCaptured");
	Event.ResourceTag = Hitzone->ExtractColorTag.GetTagName();
	Event.SourceAttackID = Request.SourceAttackInstanceID;
	Event.HitID = HitID;
	Event.CommandSerial = Request.CommandSerial;
	Event.ActivationSequenceID = Request.ActionActivationSequenceID;
	Event.TargetActor = IsValid(Hit.GetActor()) ? Hit.GetActor()->GetFName() : NAME_None;
	Event.Hitzone = Hitzone->GetFName();
	Event.ImpactPoint = Hit.ImpactPoint;
	Event.Reason = TEXT("PiercingPendingReturn");
	RecordCombatTelemetryEvent(MoveTemp(Event));
	BroadcastResourceUIState(true);
	return true;
}

void URes_InsectGlaive::PlayExtractCollectedSound(const uint64 CommandSerial)
{
	if (!CombatConfig || !CombatConfig->ExtractCollectedSound
		|| (CommandSerial != 0 && CommandSerial == LastExtractSoundCommandSerial)) return;
	// 多色携带在同一次回手逐色结算，但同一交付提示只播一次。
	// Serial 0 是旧的直接授予入口，不用全局“本帧去重”吞掉独立授予。
	LastExtractSoundCommandSerial = CommandSerial;
	// This is physical delivery at the insect's return location, not a 2D UI cue.
	// Other resource/UI sounds retain the original 2D route.
	ACharacter* Character = RuntimeContext.Character.Get();
	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	if (Mesh && Mesh->DoesSocketExist(CombatConfig->KinsectAttachSocket)
		&& !CombatConfig->ExtractCollectedSound->IsLooping())
	{
		UGameplayStatics::SpawnSoundAttached(CombatConfig->ExtractCollectedSound,
			Mesh, CombatConfig->KinsectAttachSocket, FVector::ZeroVector,
			EAttachLocation::KeepRelativeOffset, true);
	}
}

bool URes_InsectGlaive::ApplyExtractInternal(
	const FGameplayTag ExtractColor, const FHitResult* SourceHit,
	const FGuid& SourceAttackID, const FGuid& HitID, const uint64 CommandSerial)
{
	const auto RecordAttempt = [this, ExtractColor, SourceHit, &SourceAttackID,
		&HitID, CommandSerial](const FName Reason)
	{
		FWeaponCombatTelemetryEvent Event;
		Event.EventName = TEXT("ExtractAttempt");
		Event.ResourceTag = ExtractColor.IsValid() ? ExtractColor.GetTagName() : NAME_None;
		Event.SourceAttackID = SourceAttackID;
		Event.HitID = HitID;
		Event.CommandSerial = CommandSerial;
		Event.Reason = Reason;
		if (SourceHit)
		{
			Event.TargetActor = IsValid(SourceHit->GetActor())
				? SourceHit->GetActor()->GetFName() : NAME_None;
			Event.Hitzone = SourceHit->GetComponent()
				? SourceHit->GetComponent()->GetFName() : NAME_None;
			Event.ImpactPoint = SourceHit->ImpactPoint;
		}
		RecordCombatTelemetryEvent(MoveTemp(Event));
	};

	if (!IsLeafExtractTag(ExtractColor) || !CombatConfig || !GetPlayerASC())
	{
		RecordAttempt(!IsLeafExtractTag(ExtractColor)
			? FName(TEXT("InvalidExtractColor"))
			: FName(TEXT("MissingResourceContext")));
		return false;
	}

	// 三灯期间所有吸收入口都统一吞灯，绝不生成/缓存/刷新任何 GE。
	if (IsTripleUpActive())
	{
		PlayExtractCollectedSound(CommandSerial);
		RecordAttempt(TEXT("IgnoredDuringTripleUp"));
		return true;
	}

	const bool bApplied = ApplySingleExtractEffect(ExtractColor);
	RecordAttempt(bApplied ? FName(TEXT("Granted")) : FName(TEXT("GameplayEffectRejected")));
	if (bApplied)
	{
		PlayExtractCollectedSound(CommandSerial);
		CheckAndActivateTripleUp();
		BroadcastExtractState();
		ExecuteResourceFeedbackCue(ExtractGainedCueTag(), ExtractColor);
	}
	return bApplied;
}

bool URes_InsectGlaive::ApplyExtractFromHitzone(
	const UMHGZMonsterHitzoneComponent* Hitzone)
{
	return Hitzone && ApplyExtract(Hitzone->ExtractColorTag);
}

bool URes_InsectGlaive::ApplySingleExtractEffect(FGameplayTag ExtractColor)
{
	UAbilitySystemComponent* ASC = GetPlayerASC();
	if (!ASC || !CombatConfig)
	{
		return false;
	}

	TSubclassOf<UGameplayEffect> EffectClass;
	float Duration = 0.f;
	FGameplayTag MultiplierTag;
	float Multiplier = 1.f;
	if (ExtractColor.MatchesTagExact(MHGZGameplayTags::IGExtractWhite()))
	{
		EffectClass = CombatConfig->WhiteEffectClass;
		Duration = CombatConfig->WhiteExtractDuration;
		MultiplierTag = FGameplayTag::RequestGameplayTag(
			TEXT("Data.IG.Buff.MoveSpeedMultiplier"));
		Multiplier = CombatConfig->WhiteMoveSpeedMultiplier;
	}
	else if (ExtractColor.MatchesTagExact(MHGZGameplayTags::IGExtractOrange()))
	{
		EffectClass = CombatConfig->OrangeEffectClass;
		Duration = CombatConfig->OrangeExtractDuration;
		MultiplierTag = FGameplayTag::RequestGameplayTag(
			TEXT("Data.IG.Buff.DefenseMultiplier"));
		Multiplier = CombatConfig->OrangeDefenseMultiplier;
	}
	else if (ExtractColor.MatchesTagExact(MHGZGameplayTags::IGExtractRed()))
	{
		EffectClass = CombatConfig->RedEffectClass;
		Duration = CombatConfig->RedExtractDuration;
		MultiplierTag = FGameplayTag::RequestGameplayTag(
			TEXT("Data.IG.Buff.AttackMultiplier"));
		Multiplier = CombatConfig->RedAttackMultiplier;
	}

	if (!EffectClass || Duration <= 0.f)
	{
		return false;
	}

	FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(
		EffectClass, 1.f, ASC->MakeEffectContext());
	if (!Spec.IsValid())
	{
		return false;
	}
	Spec.Data->SetDuration(Duration, true);
	Spec.Data->SetSetByCallerMagnitude(MultiplierTag, Multiplier);
	const FActiveGameplayEffectHandle NewHandle = ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
	if (!IsHandleActive(NewHandle))
	{
		return false;
	}

	const FActiveGameplayEffectHandle OldHandle = ActiveExtractHandles.FindRef(ExtractColor);
	ActiveExtractHandles.Add(ExtractColor, NewHandle);
	if (FOnActiveGameplayEffectRemoved_Info* Delegate =
		ASC->OnGameplayEffectRemoved_InfoDelegate(NewHandle))
	{
		Delegate->AddUObject(this, &URes_InsectGlaive::HandleSingleExtractRemoved,
			ExtractColor, NewHandle);
	}
	// 新 Handle 确实 Active 后才删旧 Handle；Apply 失败会保留旧灯与剩余时间。
	if (IsHandleActive(OldHandle))
	{
		ASC->RemoveActiveGameplayEffect(OldHandle);
	}
	return true;
}

void URes_InsectGlaive::CheckAndActivateTripleUp()
{
	if (IsTripleUpActive() || !CombatConfig)
	{
		return;
	}
	if (!IsHandleActive(ActiveExtractHandles.FindRef(MHGZGameplayTags::IGExtractWhite()))
		|| !IsHandleActive(ActiveExtractHandles.FindRef(MHGZGameplayTags::IGExtractOrange()))
		|| !IsHandleActive(ActiveExtractHandles.FindRef(MHGZGameplayTags::IGExtractRed())))
	{
		return;
	}

	UAbilitySystemComponent* ASC = GetPlayerASC();
	if (!ASC || !CombatConfig->TripleUpEffectClass)
	{
		return;
	}
	FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(
		CombatConfig->TripleUpEffectClass, 1.f, ASC->MakeEffectContext());
	if (!Spec.IsValid())
	{
		return;
	}
	Spec.Data->SetDuration(CombatConfig->TripleUpDuration, true);
	Spec.Data->SetSetByCallerMagnitude(
		FGameplayTag::RequestGameplayTag(TEXT("Data.IG.Buff.AttackMultiplier")),
		CombatConfig->TripleAttackMultiplier);
	Spec.Data->SetSetByCallerMagnitude(
		FGameplayTag::RequestGameplayTag(TEXT("Data.IG.Buff.MoveSpeedMultiplier")),
		CombatConfig->TripleMoveSpeedMultiplier);
	Spec.Data->SetSetByCallerMagnitude(
		FGameplayTag::RequestGameplayTag(TEXT("Data.IG.Buff.DefenseMultiplier")),
		CombatConfig->TripleDefenseMultiplier);

	const FActiveGameplayEffectHandle NewTriple = ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
	if (!IsHandleActive(NewTriple))
	{
		return; // 三灯 Apply 失败：三个单灯完整保留。
	}

	bExtractTransitionGuard = true;
	TripleUpHandle = NewTriple;
	if (FOnActiveGameplayEffectRemoved_Info* Delegate =
		ASC->OnGameplayEffectRemoved_InfoDelegate(NewTriple))
	{
		Delegate->AddUObject(this, &URes_InsectGlaive::HandleTripleUpRemoved, NewTriple);
	}
	const TArray<FActiveGameplayEffectHandle> OldSingleHandles = {
		ActiveExtractHandles.FindRef(MHGZGameplayTags::IGExtractWhite()),
		ActiveExtractHandles.FindRef(MHGZGameplayTags::IGExtractOrange()),
		ActiveExtractHandles.FindRef(MHGZGameplayTags::IGExtractRed())
	};
	ActiveExtractHandles.Reset();
	for (const FActiveGameplayEffectHandle& Handle : OldSingleHandles)
	{
		if (IsHandleActive(Handle))
		{
			ASC->RemoveActiveGameplayEffect(Handle);
		}
	}
	bExtractTransitionGuard = false;
	PlayResourceSound(CombatConfig->TripleUpActivatedSound);
	OnTripleUpChanged.Broadcast();
	BroadcastExtractState();
	ExecuteResourceFeedbackCue(TripleUpActivatedCueTag(), TripleUpSourceTag());
}

bool URes_InsectGlaive::HasExtract(FGameplayTag ExtractType) const
{
	return IsHandleActive(ActiveExtractHandles.FindRef(ExtractType));
}

bool URes_InsectGlaive::IsTripleUpActive() const
{
	return IsHandleActive(TripleUpHandle);
}

bool URes_InsectGlaive::TryConsumeTripleUpAtomic()
{
	UAbilitySystemComponent* ASC = GetPlayerASC();
	const FActiveGameplayEffectHandle Handle = TripleUpHandle;
	return ASC && IsHandleActive(Handle) && ASC->RemoveActiveGameplayEffect(Handle);
}

bool URes_InsectGlaive::CaptureActionKinsectExtract(const FWeaponActionToken& Action,
	const UMHGZMonsterHitzoneComponent* Hitzone)
{
	if (!Hitzone || !IsKinsectActionAttachmentCurrent(Action) || !KinsectActor
		|| !KinsectActor->CapturePendingExtractColor(Hitzone->ExtractColorTag))
	{
		return false;
	}
	BroadcastResourceUIState(true);
	FWeaponCombatTelemetryEvent Event;
	Event.EventName = TEXT("KinsectCargoCaptured");
	Event.ActivationSequenceID = Action.ActivationSequenceID;
	Event.ResourceTag = FName(*Hitzone->ExtractColorTag.ToString());
	Event.Reason = TEXT("ActionAttachedPendingReturn");
	RecordCombatTelemetryEvent(MoveTemp(Event));
	return true;
}

bool URes_InsectGlaive::ConsumeSingleExtractsForAction(const FWeaponActionToken& Action,
	FGameplayTagContainer* OutConsumedExtracts)
{
	if (OutConsumedExtracts) { OutConsumedExtracts->Reset(); }
	UAbilitySystemComponent* ASC = GetPlayerASC();
	const UMHGZGameplayAbility* Ability = Cast<UMHGZGameplayAbility>(Action.AbilityInstance.Get());
	if (!Action.IsValid() || !IsRuntimeRequestCurrent(Action.RuntimeToken) || !ASC
		|| !Ability || !Ability->IsActive() || !Ability->IsActionActivationCommitted()
		|| Ability->GetActionToken() != Action
		|| IsTripleUpActive() || bExtractTransitionGuard)
	{
		return false;
	}
	bExtractTransitionGuard = true;
	const TMap<FGameplayTag, FActiveGameplayEffectHandle> Handles = ActiveExtractHandles;
	bool bAllRemoved = true;
	int32 RemovedCount = 0;
	FGameplayTagContainer ConsumedExtracts;
	for (const auto& Pair : Handles)
	{
		if (IsHandleActive(Pair.Value))
		{
			if (ASC->RemoveActiveGameplayEffect(Pair.Value))
			{
				++RemovedCount;
				ConsumedExtracts.AddTag(Pair.Key);
			}
			else { bAllRemoved = false; }
		}
		else { ActiveExtractHandles.Remove(Pair.Key); }
	}
	bExtractTransitionGuard = false;
	BroadcastExtractState();
	FWeaponCombatTelemetryEvent Event;
	Event.EventName = TEXT("SingleExtractsConsumed");
	Event.ActivationSequenceID = Action.ActivationSequenceID;
	Event.MemberCount = RemovedCount;
	Event.Reason = bAllRemoved ? TEXT("AwakenedActivation") : TEXT("GameplayEffectRemovalFailed");
	RecordCombatTelemetryEvent(MoveTemp(Event));
	if (bAllRemoved && OutConsumedExtracts) { *OutConsumedExtracts = ConsumedExtracts; }
	return bAllRemoved;
}

bool URes_InsectGlaive::ConsumeExtract(FGameplayTag ExtractType)
{
	if (IsTripleUpActive())
	{
		return false;
	}
	UAbilitySystemComponent* ASC = GetPlayerASC();
	const FActiveGameplayEffectHandle Handle = ActiveExtractHandles.FindRef(ExtractType);
	return ASC && IsHandleActive(Handle) && ASC->RemoveActiveGameplayEffect(Handle);
}

void URes_InsectGlaive::HandleSingleExtractRemoved(
	const FGameplayEffectRemovalInfo& RemovalInfo, FGameplayTag Color,
	FActiveGameplayEffectHandle ExpectedHandle)
{
	if (ActiveExtractHandles.FindRef(Color) == ExpectedHandle)
	{
		ActiveExtractHandles.Remove(Color);
		if (!bExtractTransitionGuard)
		{
			if (!bRuntimeShuttingDown && !RemovalInfo.bPrematureRemoval)
			{
				ExecuteResourceFeedbackCue(ExtractExpiredCueTag(), Color);
			}
			BroadcastExtractState();
		}
	}
}

void URes_InsectGlaive::HandleTripleUpRemoved(
	const FGameplayEffectRemovalInfo& RemovalInfo,
	FActiveGameplayEffectHandle ExpectedHandle)
{
	if (TripleUpHandle != ExpectedHandle)
	{
		return; // 旧 Handle 的迟到回调不能清后来建立的三灯。
	}
	TripleUpHandle = FActiveGameplayEffectHandle();
	TripleReservations.Reset();
	if (!bRuntimeShuttingDown && CombatConfig && !RemovalInfo.bPrematureRemoval)
	{
		PlayResourceSound(CombatConfig->TripleUpExpiredSound);
		ExecuteResourceFeedbackCue(ExtractExpiredCueTag(), TripleUpSourceTag());
	}
	if (!bRuntimeShuttingDown)
	{
		OnTripleUpChanged.Broadcast();
		BroadcastExtractState();
	}
}

void URes_InsectGlaive::ExecuteResourceFeedbackCue(const FGameplayTag& CueTag,
	const FGameplayTag& SourceTag) const
{
	UAbilitySystemComponent* ASC = GetPlayerASC();
	AActor* OwnerActor = GetOwner();
	if (!ASC || !OwnerActor || !CueTag.IsValid())
	{
		return;
	}

	FGameplayCueParameters Parameters;
	Parameters.Instigator = OwnerActor;
	Parameters.EffectCauser = OwnerActor;
	Parameters.Location = KinsectActor
		? KinsectActor->GetActorLocation() : OwnerActor->GetActorLocation();
	Parameters.EffectContext = ASC->MakeEffectContext();
	if (SourceTag.IsValid())
	{
		// Cue只读本次参数，不改ASC真实Tag集合。
		Parameters.AggregatedSourceTags.AddTag(SourceTag);
	}
	ASC->ExecuteGameplayCue(CueTag, Parameters);
}

void URes_InsectGlaive::BroadcastExtractState()
{
	for (const FGameplayTag& Color : { MHGZGameplayTags::IGExtractWhite(), MHGZGameplayTags::IGExtractOrange(), MHGZGameplayTags::IGExtractRed() })
	{
		float Ratio = 0.f;
		const FActiveGameplayEffectHandle Handle = ActiveExtractHandles.FindRef(Color);
		if (UAbilitySystemComponent* ASC = GetPlayerASC(); IsHandleActive(Handle) && ASC)
		{
			const FActiveGameplayEffect* ActiveEffect = ASC->GetActiveGameplayEffect(Handle);
			const float Duration = ActiveEffect ? ActiveEffect->GetDuration() : 0.f;
			const float Remaining = ActiveEffect && GetWorld()
				? ActiveEffect->GetTimeRemaining(GetWorld()->GetTimeSeconds()) : 0.f;
			Ratio = Duration > 0.f ? FMath::Clamp(Remaining / Duration, 0.f, 1.f) : 1.f;
		}
		OnExtractTimeUpdated.Broadcast(Color, Ratio);
	}
	BroadcastResourceUIState(true);
}

FIGResourceUISnapshot URes_InsectGlaive::GetUIState() const
{
	FIGResourceUISnapshot State;
	State.RuntimeToken = RuntimeContext.RuntimeToken;
	State.KinsectStamina = KinsectStamina;
	State.MaxKinsectStamina = MaxKinsectStamina;
	State.bHasKinsect = IsValid(KinsectActor);
	if (State.bHasKinsect)
	{
		State.KinsectState = KinsectActor->GetState();
		State.KinsectPendingExtractColor = KinsectActor->GetPendingExtractColor();
		State.KinsectPendingExtractColors = KinsectActor->GetPendingExtractColors();
	}
	State.DanceStacks = DanceStacks;
	State.DanceSource = DanceSource;

	const auto FillExtract = [this, &State](const FGameplayTag& Color,
		bool& bActive, float& RemainingSeconds, float& Ratio)
	{
		const FActiveGameplayEffectHandle Handle = ActiveExtractHandles.FindRef(Color);
		bActive = IsHandleActive(Handle);
		if (!bActive)
		{
			return;
		}
		UAbilitySystemComponent* ASC = GetPlayerASC();
		const FActiveGameplayEffect* Effect = ASC
			? ASC->GetActiveGameplayEffect(Handle) : nullptr;
		if (!Effect)
		{
			bActive = false;
			return;
		}
		const float Duration = Effect->GetDuration();
		RemainingSeconds = GetWorld()
			? FMath::Max(0.0f, Effect->GetTimeRemaining(GetWorld()->GetTimeSeconds()))
			: 0.0f;
		Ratio = Duration > 0.0f
			? FMath::Clamp(RemainingSeconds / Duration, 0.0f, 1.0f) : 1.0f;
	};
	FillExtract(MHGZGameplayTags::IGExtractRed(), State.bRed,
		State.RedRemainingSeconds, State.RedRatio);
	FillExtract(MHGZGameplayTags::IGExtractWhite(), State.bWhite,
		State.WhiteRemainingSeconds, State.WhiteRatio);
	FillExtract(MHGZGameplayTags::IGExtractOrange(), State.bOrange,
		State.OrangeRemainingSeconds, State.OrangeRatio);

	State.bTripleUp = IsHandleActive(TripleUpHandle);
	if (State.bTripleUp)
	{
		if (UAbilitySystemComponent* ASC = GetPlayerASC())
		{
			if (const FActiveGameplayEffect* Effect =
				ASC->GetActiveGameplayEffect(TripleUpHandle))
			{
				const float Duration = Effect->GetDuration();
				State.TripleRemainingSeconds = GetWorld()
					? FMath::Max(0.0f,
						Effect->GetTimeRemaining(GetWorld()->GetTimeSeconds())) : 0.0f;
				State.TripleRatio = Duration > 0.0f
					? FMath::Clamp(State.TripleRemainingSeconds / Duration, 0.0f, 1.0f)
					: 1.0f;
			}
		}
	}

	// UI只使用数量，不为每帧统计分配包含位置、伤害及来源的完整快照数组。
	// 仍按实时寿命筛选，避免计时器尚未执行时把到期粉尘计入UI。
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	for (const TWeakObjectPtr<AIGPowderActor>& WeakPowder : OwnedPowders)
	{
		const AIGPowderActor* Powder = WeakPowder.Get();
		if (!World || !Powder || Powder->GetRuntimeToken() != RuntimeContext.RuntimeToken
			|| Powder->GetOwnerResource() != this || !(Powder->GetExpireWorldTime() > Now))
		{
			continue;
		}
		if (Powder->GetPowderState() == EIGPowderState::Reserved)
		{
			++State.ReservedPowderCount;
		}
		else if (Powder->GetPowderState() == EIGPowderState::Available)
		{
			++State.AvailablePowderCount;
		}
	}
	return State;
}

void URes_InsectGlaive::BroadcastResourceUIState(const bool bForce)
{
	if (bRuntimeShuttingDown)
	{
		return;
	}
	const FIGResourceUISnapshot State = GetUIState();
	const bool bChanged = !bHasPublishedUISnapshot
		|| State.RuntimeToken != LastPublishedUISnapshot.RuntimeToken
		|| !FMath::IsNearlyEqual(State.KinsectStamina,
			LastPublishedUISnapshot.KinsectStamina, 0.001f)
		|| !FMath::IsNearlyEqual(State.MaxKinsectStamina,
			LastPublishedUISnapshot.MaxKinsectStamina, 0.001f)
		|| State.KinsectState != LastPublishedUISnapshot.KinsectState
		|| State.KinsectPendingExtractColor
			!= LastPublishedUISnapshot.KinsectPendingExtractColor
		|| State.KinsectPendingExtractColors
			!= LastPublishedUISnapshot.KinsectPendingExtractColors
		|| State.bHasKinsect != LastPublishedUISnapshot.bHasKinsect
		|| State.bRed != LastPublishedUISnapshot.bRed
		|| State.bWhite != LastPublishedUISnapshot.bWhite
		|| State.bOrange != LastPublishedUISnapshot.bOrange
		|| !FMath::IsNearlyEqual(State.RedRemainingSeconds,
			LastPublishedUISnapshot.RedRemainingSeconds, 0.005f)
		|| !FMath::IsNearlyEqual(State.WhiteRemainingSeconds,
			LastPublishedUISnapshot.WhiteRemainingSeconds, 0.005f)
		|| !FMath::IsNearlyEqual(State.OrangeRemainingSeconds,
			LastPublishedUISnapshot.OrangeRemainingSeconds, 0.005f)
		|| !FMath::IsNearlyEqual(State.RedRatio, LastPublishedUISnapshot.RedRatio, 0.0001f)
		|| !FMath::IsNearlyEqual(State.WhiteRatio, LastPublishedUISnapshot.WhiteRatio, 0.0001f)
		|| !FMath::IsNearlyEqual(State.OrangeRatio, LastPublishedUISnapshot.OrangeRatio, 0.0001f)
		|| State.bTripleUp != LastPublishedUISnapshot.bTripleUp
		|| !FMath::IsNearlyEqual(State.TripleRemainingSeconds,
			LastPublishedUISnapshot.TripleRemainingSeconds, 0.005f)
		|| !FMath::IsNearlyEqual(State.TripleRatio,
			LastPublishedUISnapshot.TripleRatio, 0.0001f)
		|| State.DanceStacks != LastPublishedUISnapshot.DanceStacks
		|| State.DanceSource != LastPublishedUISnapshot.DanceSource
		|| State.AvailablePowderCount != LastPublishedUISnapshot.AvailablePowderCount
		|| State.ReservedPowderCount != LastPublishedUISnapshot.ReservedPowderCount;
	if (!bForce && !bChanged)
	{
		return;
	}
	LastPublishedUISnapshot = State;
	bHasPublishedUISnapshot = true;
	IGResourceUIChanged.Broadcast(LastPublishedUISnapshot);
}

bool URes_InsectGlaive::AddDanceStack(EIGDanceSource Source)
{
	if (bRuntimeShuttingDown || !CombatConfig || Source == EIGDanceSource::None)
	{
		return false;
	}

	const int32 PreviousStacks = DanceStacks;
	const EIGDanceSource PreviousSource = DanceSource;
	DanceStacks = FMath::Clamp(DanceStacks + 1, 0, CombatConfig->MaxDanceStacks);
	DanceSource = Source;
	// 镜像必须早于广播：观察者在回调里读到的标签要与刚写的数值一致。
	// 层数到顶也要同步 —— 来源与层数是两个字段，上限不改来源。
	RefreshDanceStateTags();
	if (DanceStacks != PreviousStacks || DanceSource != PreviousSource)
	{
		OnDanceStateChanged.Broadcast(DanceStacks, DanceSource);
		BroadcastResourceUIState(true);
	}
	return true;
}

void URes_InsectGlaive::ClearDanceStacks(EIGDanceClearReason Reason)
{
	(void)Reason;
	const bool bHadState = DanceStacks != 0 || DanceSource != EIGDanceSource::None;
	DanceStacks = 0;
	DanceSource = EIGDanceSource::None;
	// 释放镜像**不**看 bHadState：旧的早退（数值已是 0/None 就直接返回）会留下一个
	// 没人认领的来源标签，而它正是下游动作的入招判据。广播才维持原样，只在真有变化时发。
	RefreshDanceStateTags();
	if (bHadState && !bRuntimeShuttingDown)
	{
		OnDanceStateChanged.Broadcast(DanceStacks, DanceSource);
		BroadcastResourceUIState(true);
	}
}

float URes_InsectGlaive::GetDanceDamageMultiplier(const EIGDanceDamageAction Action) const
{
	// Preserve the Blueprint query, but resolve its defaults from the granted GA.
	// Executing GAs freeze their own instance values directly at commit.
	const UAbilitySystemComponent* ASC = GetPlayerASC();
	if (!ASC || !CombatConfig) return 1.0f;
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		const TArray<float>* Values = nullptr;
		if (Action == EIGDanceDamageAction::KinsectSlash)
		{
			if (const auto* GA = Cast<UMHGZKinsectSlashAbility>(Spec.Ability)) Values = &GA->KinsectSlashDanceDamageMultipliers;
		}
		else if (Action == EIGDanceDamageAction::KinsectPiercer)
		{
			if (const auto* GA = Cast<UMHGZKinsectPiercerAbility>(Spec.Ability)) Values = &GA->KinsectPiercerDanceDamageMultipliers;
		}
		else if (Action == EIGDanceDamageAction::DescendingThrust)
		{
			if (const auto* GA = Cast<UMHGZDescendingThrustAbility>(Spec.Ability)) Values = &GA->DescendingThrustDanceDamageMultipliers;
		}
		if (Values) return MHGZ::IGActionConfig::DanceDamageMultiplier(*Values, DanceStacks, CombatConfig->MaxDanceStacks);
	}
	return 1.0f;
}

bool URes_InsectGlaive::AreTripleCostSpecs(
	const TArray<FWeaponResourceCostSpec>& Specs) const
{
	if (Specs.Num() != 1 || !Specs[0].CostType.MatchesTagExact(MHGZGameplayTags::IGTripleUpCost()))
	{
		return false;
	}
	return FMath::IsNearlyEqual(Specs[0].Amount.GetValueAtLevel(1.f), 1.f);
}

bool URes_InsectGlaive::CanReserveCosts(
	const TArray<FWeaponResourceCostSpec>& Specs) const
{
	if (Specs.IsEmpty())
	{
		return true;
	}
	if (!AreTripleCostSpecs(Specs) || !IsTripleUpActive())
	{
		return false;
	}
	for (const TPair<uint64, FTripleCostReservationEntry>& Pair : TripleReservations)
	{
		if (Pair.Value.TripleHandle == TripleUpHandle)
		{
			return false;
		}
	}
	return true;
}

bool URes_InsectGlaive::TryReserveCosts(const FWeaponActionToken& ActionToken,
	const TArray<FWeaponResourceCostSpec>& Specs,
	FWeaponResourceCostReservation& OutReservation)
{
	if (Specs.IsEmpty())
	{
		return Super::TryReserveCosts(ActionToken, Specs, OutReservation);
	}
	OutReservation = FWeaponResourceCostReservation();
	if (!ActionToken.IsValid() || !IsRuntimeRequestCurrent(ActionToken.RuntimeToken))
	{
		RecordTripleCostTelemetryEvent(ActionToken.ActivationSequenceID, 0,
			TEXT("TripleCostRejected"), TEXT("InvalidActionOrRuntime"));
		return false;
	}
	if (!AreTripleCostSpecs(Specs))
	{
		RecordTripleCostTelemetryEvent(ActionToken.ActivationSequenceID, 0,
			TEXT("TripleCostRejected"), TEXT("InvalidCostSpec"));
		return false;
	}
	if (!IsTripleUpActive())
	{
		RecordTripleCostTelemetryEvent(ActionToken.ActivationSequenceID, 0,
			TEXT("TripleCostRejected"), TEXT("TripleUpUnavailable"));
		return false;
	}
	// One live TripleUp handle can back exactly one unconsumed reservation.
	for (const TPair<uint64, FTripleCostReservationEntry>& Pair : TripleReservations)
	{
		if (Pair.Value.TripleHandle == TripleUpHandle)
		{
			RecordTripleCostTelemetryEvent(ActionToken.ActivationSequenceID, 0,
				TEXT("TripleCostRejected"), TEXT("AlreadyReserved"));
			return false;
		}
	}
	uint64 ID = NextReservationID++;
	if (ID == 0)
	{
		ID = NextReservationID++;
	}
	OutReservation.RuntimeToken = ActionToken.RuntimeToken;
	OutReservation.ActivationSequenceID = ActionToken.ActivationSequenceID;
	OutReservation.ReservationID = ID;
	FTripleCostReservationEntry& Entry = TripleReservations.Add(ID);
	Entry.Reservation = OutReservation;
	Entry.TripleHandle = TripleUpHandle;
	RecordTripleCostTelemetryEvent(OutReservation.ActivationSequenceID,
		OutReservation.ReservationID, TEXT("TripleCostReserved"), TEXT("Reserved"));
	return true;
}

void URes_InsectGlaive::ReleaseReservation(
	const FWeaponResourceCostReservation& Reservation)
{
	if (const FTripleCostReservationEntry* Entry = TripleReservations.Find(Reservation.ReservationID);
		Entry && Entry->Reservation.RuntimeToken == Reservation.RuntimeToken
		&& Entry->Reservation.ActivationSequenceID == Reservation.ActivationSequenceID)
	{
		TripleReservations.Remove(Reservation.ReservationID);
		RecordTripleCostTelemetryEvent(Reservation.ActivationSequenceID,
			Reservation.ReservationID, TEXT("TripleCostReleased"), TEXT("Released"));
	}
}

bool URes_InsectGlaive::ConsumeReservedCosts(
	const FWeaponResourceCostReservation& Reservation)
{
	if (!Reservation.IsValid() || !IsRuntimeRequestCurrent(Reservation.RuntimeToken))
	{
		RecordTripleCostTelemetryEvent(Reservation.ActivationSequenceID,
			Reservation.ReservationID, TEXT("TripleCostRejected"),
			TEXT("InvalidReservationOrRuntime"));
		return false;
	}
	const FTripleCostReservationEntry* Entry = TripleReservations.Find(Reservation.ReservationID);
	if (!Entry || Entry->Reservation.RuntimeToken != Reservation.RuntimeToken
		|| Entry->Reservation.ActivationSequenceID != Reservation.ActivationSequenceID)
	{
		RecordTripleCostTelemetryEvent(Reservation.ActivationSequenceID,
			Reservation.ReservationID, TEXT("TripleCostRejected"),
			TEXT("ReservationIdentityMismatch"));
		return false;
	}

	const FActiveGameplayEffectHandle ReservedHandle = Entry->TripleHandle;
	TripleReservations.Remove(Reservation.ReservationID);
	UAbilitySystemComponent* ASC = GetPlayerASC();
	if (!ReservedHandle.IsValid() || ReservedHandle != TripleUpHandle
		|| !IsHandleActive(ReservedHandle) || !ASC)
	{
		RecordTripleCostTelemetryEvent(Reservation.ActivationSequenceID,
			Reservation.ReservationID, TEXT("TripleCostRejected"),
			TEXT("ReservedTripleExpiredOrRemoved"));
		return false;
	}
	const bool bConsumed = ASC->RemoveActiveGameplayEffect(ReservedHandle);
	RecordTripleCostTelemetryEvent(Reservation.ActivationSequenceID,
		Reservation.ReservationID,
		bConsumed ? FName(TEXT("TripleCostConsumed")) : FName(TEXT("TripleCostRejected")),
		bConsumed ? FName(TEXT("Consumed")) : FName(TEXT("GameplayEffectRemovalFailed")));
	return bConsumed;
}

void URes_InsectGlaive::RefreshDanceStateTags()
{
	const FGameplayTag DesiredTag = DanceSourceTag(DanceSource);

	UMHGZWeaponRuntimeHostComponent* Host = RuntimeContext.RuntimeToken.Host.Get();
	if (!Host && !DanceSourceTagToken.IsValid())
	{
		ActiveDanceSourceTag = FGameplayTag();
		return;
	}

	// 来源没变就不动 Token：反复释放/重新 Acquire 会让依赖标签计数的观察者看到抖动。
	if (DanceSourceTagToken.IsValid() && DesiredTag.IsValid()
		&& ActiveDanceSourceTag == DesiredTag)
	{
		return;
	}

	if (DanceSourceTagToken.IsValid())
	{
		if (Host)
		{
			Host->ReleaseTags(DanceSourceTagToken);
		}
		DanceSourceTagToken = FWeaponOwnedTagToken();
		ActiveDanceSourceTag = FGameplayTag();
	}

	if (!Host || !DesiredTag.IsValid())
	{
		return;
	}

	FGameplayTagContainer Tags;
	Tags.AddTag(DesiredTag);
	DanceSourceTagToken = Host->AcquireTags(EWeaponTagOwnerKind::Resource,
		FGameplayAbilitySpecHandle(), 0, TEXT("IG.Dance.Source"), Tags);
	ActiveDanceSourceTag = DanceSourceTagToken.IsValid() ? DesiredTag : FGameplayTag();
}

void URes_InsectGlaive::ClearAllResourceGameplayEffects()
{
	UAbilitySystemComponent* ASC = GetPlayerASC();
	if (!ASC)
	{
		ActiveExtractHandles.Reset();
		TripleUpHandle = FActiveGameplayEffectHandle();
		return;
	}
	bExtractTransitionGuard = true;
	TArray<FActiveGameplayEffectHandle> SingleHandles;
	ActiveExtractHandles.GenerateValueArray(SingleHandles);
	ActiveExtractHandles.Reset();
	for (const FActiveGameplayEffectHandle& Handle : SingleHandles)
	{
		if (IsHandleActive(Handle))
		{
			ASC->RemoveActiveGameplayEffect(Handle);
		}
	}
	const FActiveGameplayEffectHandle TripleHandle = TripleUpHandle;
	TripleUpHandle = FActiveGameplayEffectHandle();
	if (IsHandleActive(TripleHandle))
	{
		ASC->RemoveActiveGameplayEffect(TripleHandle);
	}
	bExtractTransitionGuard = false;
}
