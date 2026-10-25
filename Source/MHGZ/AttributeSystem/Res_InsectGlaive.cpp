// Copyright MHGZ Project. All Rights Reserved.

#include "Res_InsectGlaive.h"
#include "ActionSystem/MHGZDamageMath.h"
#include "InsectGlaive/Kinsect/IGCoAttackComponent.h"

#include "AbilitySystemComponent.h"
#include "ActionSystem/MHGZKinsectSlashAbility.h"
#include "ActionSystem/MHGZKinsectPiercerAbility.h"
#include "ActionSystem/MHGZDescendingThrustAbility.h"
#include "AbilitySystemGlobals.h"
#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "ActionSystem/MHGZDamageGameplayEffect.h"
#include "ActionSystem/MHGZGameplayEffectContext.h"
#include "ActionSystem/MHGZGameplayAbility.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/PrimitiveComponent.h"
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Engine/OverlapResult.h"
#include "Particles/ParticleSystemComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstance.h"
#include "GameplayEffect.h"
#include "GameplayEffectTypes.h"
#include "InsectGlaive/InsectGlaiveCombatConfig.h"
#include "InsectGlaive/IGPowderActor.h"
#include "InsectGlaive/Kinsect/IGMarkProjectile.h"
#include "InsectGlaive/Kinsect/InsectGlaiveKinsectData.h"
#include "InsectGlaive/Kinsect/KinsectCollisionComponent.h"
#include "InsectGlaive/Kinsect/Kinsect.h"
#include "Monster/MHGZMonsterBase.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"
#include "Math/RotationMatrix.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

#include "Engine/EngineTypes.h"

namespace
{
constexpr float PowderWorldLifetimeSeconds = 180.0f;
constexpr double PowderMaintenanceIntervalSeconds = 0.25;

FName PowderSpawnPolicyName(const EIGPowderSpawnPolicy Policy)
{
	switch (Policy)
	{
	case EIGPowderSpawnPolicy::MarkedContact: return TEXT("MarkedContact");
	case EIGPowderSpawnPolicy::AwakenedPiercing: return TEXT("AwakenedPiercing");
	case EIGPowderSpawnPolicy::AwakenedEachHit: return TEXT("AwakenedEachHit");
	case EIGPowderSpawnPolicy::AutoMarkContact: return TEXT("AutoMarkContact");
	default: return TEXT("None");
	}
}

FName KinsectCommandSourceName(const EKinsectCommandSource Source)
{
	switch (Source)
	{
	case EKinsectCommandSource::Player: return TEXT("Player");
	case EKinsectCommandSource::Action: return TEXT("Action");
	case EKinsectCommandSource::AutoMark: return TEXT("AutoMark");
	case EKinsectCommandSource::PowderVortex: return TEXT("PowderVortex");
	case EKinsectCommandSource::System: return TEXT("System");
	case EKinsectCommandSource::CoAttack: return TEXT("CoAttack");
	case EKinsectCommandSource::DescendingThrust: return TEXT("DescendingThrust");
	default: return TEXT("None");
	}
}

FName KinsectFlightEndReasonName(const EKinsectFlightEndReason Reason)
{
	switch (Reason)
	{
	case EKinsectFlightEndReason::MaxDistance: return TEXT("MaxDistance");
	case EKinsectFlightEndReason::Arrival: return TEXT("Arrival");
	case EKinsectFlightEndReason::WorldHit: return TEXT("WorldHit");
	case EKinsectFlightEndReason::HitzoneHit: return TEXT("HitzoneHit");
	case EKinsectFlightEndReason::Interrupted: return TEXT("Interrupted");
	default: return TEXT("None");
	}
}

bool IsFiniteVector(const FVector& Value)
{
	return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y)
		&& FMath::IsFinite(Value.Z);
}

const FGameplayTag& WhiteExtractTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("WeaponResource.IG.Extract.White"));
	return Tag;
}

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

const FGameplayTag& OrangeExtractTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("WeaponResource.IG.Extract.Orange"));
	return Tag;
}

const FGameplayTag& RedExtractTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("WeaponResource.IG.Extract.Red"));
	return Tag;
}

const FGameplayTag& TripleCostTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Cost.IG.TripleUp"));
	return Tag;
}

const FGameplayTag& KinsectActiveTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("WeaponResource.IG.Kinsect.Active"));
	return Tag;
}

const FGameplayTag& MarkActiveTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("WeaponResource.IG.Mark.Active"));
	return Tag;
}

/** 舞踏来源的 ASC 镜像标签。真值仍是 `DanceSource` 字段，这里只是它的对外广播。 */
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

const FGameplayTag& PowderVortexDeadTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("Combat.State.Dead"));
	return Tag;
}

bool IsDamageablePowderTarget(AActor* Target, UAbilitySystemComponent* TargetASC)
{
	if (!IsValid(Target) || !TargetASC
		|| TargetASC->HasMatchingGameplayTag(PowderVortexDeadTag()))
	{
		return false;
	}
	return !TargetASC->HasAttributeSetForAttribute(
		UMHGZAttributeSet::GetHealthAttribute())
		|| TargetASC->GetNumericAttribute(
			UMHGZAttributeSet::GetHealthAttribute()) > 0.0f;
}
}

URes_InsectGlaive::URes_InsectGlaive()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	WeaponTypeTag = FGameplayTag::RequestGameplayTag(TEXT("Weapon.InsectGlaive"));
}

void URes_InsectGlaive::RecordCombatTelemetryEvent(
	FWeaponCombatTelemetryEvent Event) const
{
	if (UMHGZWeaponRuntimeHostComponent* Host = RuntimeContext.RuntimeToken.Host.Get())
	{
		Host->RecordCombatTelemetryEvent(Event);
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

void URes_InsectGlaive::InitializeRuntime(const FWeaponRuntimeContext& Context)
{
	GuaranteedKinsectHitReceipts.Reset();
	ClearOwnedPowders(EWeaponRuntimeEndReason::RuntimeShutdown);
	Super::InitializeRuntime(Context);
	bRuntimeShuttingDown = false;
	LastExtractSoundCommandSerial = 0;
	bHasPublishedUISnapshot = false;
	KinsectCommandSerial = 0;
	KinsectCommandSource = EKinsectCommandSource::None;
	KinsectPlayerCommandLeaseToken = FWeaponActionToken();
	AutoMarkCommandSerial = 0;
	AutoMarkCommandMarkSerial = 0;
	bAutoMarkApproachComplete = false;
	bAutoMarkDeferredByPriority = false;
	NextAutoMarkActionWorldTime = 0.0;
	EarliestAutoMarkUpdateFrame = 0;
	LastMarkedContactPowderWorldTime = -1.0;
	NextPowderMaintenanceWorldTime = 0.0;
	// 初始化也要直接同步一次，不能只靠 OnDanceStateChanged：关闭路径会抑制广播，
	// 重建 Runtime 时残留的镜像必须在这里对齐（正常情况下它本就是空操作）。
	RefreshDanceStateTags();
	CombatConfig = Cast<UInsectGlaiveCombatConfig>(Context.CombatConfig.Get());
	if (!CombatConfig || !Context.Character.IsValid() || !Context.ASC.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[IG Resource] Invalid runtime context; resource remains inert."));
		return;
	}

	SetComponentTickEnabled(true);
	KinsectData = CombatConfig->KinsectData;
	if (!KinsectData)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[IG Resource] CombatConfig has no KinsectData; resource remains inert."));
		return;
	}

	USceneComponent* AttachComponent = Context.Character->GetMesh();
	OnWeaponEquipped(KinsectData, AttachComponent, CombatConfig->KinsectAttachSocket);
}

void URes_InsectGlaive::ShutdownRuntime(EWeaponRuntimeEndReason Reason)
{
	if (bRuntimeShuttingDown)
	{
		return;
	}
	bRuntimeShuttingDown = true;
	GuaranteedKinsectHitReceipts.Reset();
	SetComponentTickEnabled(false);
	CancelAutomaticMarkBehavior(true);
	KinsectPlayerCommandLeaseToken = FWeaponActionToken();
	KinsectCommandSource = EKinsectCommandSource::None;
	++KinsectCommandSerial;
	ClearOwnedPowders(Reason);
	ClearDanceStacks(EIGDanceClearReason::RuntimeShutdown);

	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(MarkExpiryTimer);
	}
	ClearKinsectMark(Reason == EWeaponRuntimeEndReason::WeaponChanged
		? EIGMarkClearReason::WeaponChanged : EIGMarkClearReason::RuntimeShutdown);
	OnWeaponUnequipped();
	ClearAllResourceGameplayEffects();
	TripleReservations.Reset();
	CombatConfig = nullptr;

	Super::ShutdownRuntime(Reason);
}

float URes_InsectGlaive::GetKinsectStaminaDrainMultiplier() const
{
	const bool bTriple = IsTripleUpActive();
	if (!bTriple && !HasExtract(RedExtractTag())) return 1.0f;
	const float Default = bTriple ? 0.5f : 0.75f;
	const float Configured = CombatConfig ? (bTriple
		? CombatConfig->TripleKinsectStaminaDrainMultiplier
		: CombatConfig->RedKinsectStaminaDrainMultiplier) : Default;
	return FMath::IsFinite(Configured) ? FMath::Clamp(Configured, 0.0f, 1.0f) : Default;
}

void URes_InsectGlaive::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	TickPowderMaintenance();
	TickGuaranteedKinsectHits();
	if (bRuntimeShuttingDown || !KinsectActor || !KinsectData)
	{
		BroadcastResourceUIState();
		return;
	}

	const EKinsectState State = KinsectActor->GetState();
	const bool bSlashAwayFromArm = KinsectActor->IsSlashActionControlled()
		&& State != EKinsectState::Attached;
	const float PreviousStamina = KinsectStamina;
	const float DrainMultiplier = GetKinsectStaminaDrainMultiplier();
	if (State == EKinsectState::Flying || State == EKinsectState::CoAttack || bSlashAwayFromArm)
	{
		KinsectStamina = FMath::Max(0.f,
			KinsectStamina - KinsectData->FlightDrainRate * DrainMultiplier * DeltaTime);
	}
	else if (State == EKinsectState::Hovering)
	{
		KinsectStamina = FMath::Max(0.f,
			KinsectStamina - KinsectData->HoverDrainRate * DrainMultiplier * DeltaTime);
	}
	else if (State == EKinsectState::Attached)
	{
		KinsectStamina = FMath::Min(MaxKinsectStamina,
			KinsectStamina + KinsectData->StaminaRegenRate * DeltaTime);
	}
	else if (State == EKinsectState::ActionAttached)
	{
		// 非操虫斩的临时附着保持旧规则；操虫斩离臂耗耐已在上方处理。
		KinsectStamina = FMath::Min(MaxKinsectStamina,
			KinsectStamina + KinsectData->StaminaRegenRate * DeltaTime);
	}

	if ((PreviousStamina > 0.f || DescendingThrustKinsect.IsSet()) && KinsectStamina <= 0.f
		&& (State == EKinsectState::Flying || State == EKinsectState::Hovering
			|| State == EKinsectState::CoAttack || bSlashAwayFromArm))
	{
		bDepletionEdgeTriggered = true;
		PlayResourceSound(CombatConfig ? CombatConfig->KinsectDepletedSound : nullptr);
		const uint64 CommandSerial = AdvanceKinsectCommand(EKinsectCommandSource::System);
		KinsectPlayerCommandLeaseToken = FWeaponActionToken();
		KinsectActor->ForceRecall(CommandSerial, EKinsectCommandSource::System);
	}
	else if (KinsectStamina > 0.f)
	{
		bDepletionEdgeTriggered = false;
	}
	TickDescendingThrustKinsect();

	if (!FMath::IsNearlyEqual(PreviousStamina, KinsectStamina))
	{
		OnKinsectStaminaChanged.Broadcast(KinsectStamina, MaxKinsectStamina);
	}

	const bool bHasTrackedMark = ActiveMarkHitzone.IsValid()
		|| ActiveMarkProjectile.IsValid() || MarkActiveTagToken.IsValid();
	if (bHasTrackedMark
		&& (!ActiveMarkHitzone.IsValid()
			|| !IsValid(ActiveMarkHitzone->GetOwner())
			|| !ActiveMarkHitzone->HasKinsectMarkDebuffFrom(this, ActiveMarkID)
			|| (bActiveMarkUsesProjectile && !ActiveMarkProjectile.IsValid())))
	{
		ClearKinsectMark(EIGMarkClearReason::TargetInvalid);
	}

	UpdateAutomaticMarkBehavior();
	BroadcastResourceUIState();
}

bool URes_InsectGlaive::OnWeaponEquipped(UInsectGlaiveKinsectData* Data,
	USceneComponent* AttachComponent, FName AttachSocket)
{
	if (!Data || !AttachComponent
		|| (!AttachSocket.IsNone() && !AttachComponent->DoesSocketExist(AttachSocket))
		|| !GetWorld() || KinsectActor)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[IG Resource] Cannot spawn kinsect: attach component/socket '%s' is invalid."),
			*AttachSocket.ToString());
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = GetOwner();
	SpawnParams.Instigator = Cast<APawn>(GetOwner());
	KinsectActor = GetWorld()->SpawnActor<AKinsect>(AKinsect::StaticClass(), SpawnParams);
	if (!KinsectActor)
	{
		return false;
	}

	KinsectData = Data;
	KinsectAttachComponent = AttachComponent;
	KinsectAttachSocket = AttachSocket;
	KinsectActionAttachmentToken = FWeaponActionToken();
	KinsectPlayerCommandLeaseToken = FWeaponActionToken();
	KinsectCommandSource = EKinsectCommandSource::None;
	++KinsectCommandSerial;
	KinsectActor->KinsectData = Data;
	KinsectActor->ResourceComponent = this;
	if (USkeletalMesh* Mesh = Data->KinsectMesh.LoadSynchronous())
	{
		KinsectActor->Mesh->SetSkeletalMesh(Mesh);
	}
	if (Data->KinsectAnimClass)
	{
		KinsectActor->Mesh->SetAnimInstanceClass(Data->KinsectAnimClass);
	}
	for (const TPair<FName, TSoftObjectPtr<UMaterialInstance>>& Pair : Data->MaterialOverrides)
	{
		if (UMaterialInstance* Material = Pair.Value.LoadSynchronous())
		{
			const int32 Index = KinsectActor->Mesh->GetMaterialIndex(Pair.Key);
			if (Index != INDEX_NONE)
			{
				KinsectActor->Mesh->SetMaterial(Index, Material);
			}
		}
	}
	KinsectActor->AttachToPlayer(AttachComponent, AttachSocket);
	MaxKinsectStamina = FMath::Max(0.f, Data->StaminaPool);
	KinsectStamina = MaxKinsectStamina;
	OnKinsectStaminaChanged.Broadcast(KinsectStamina, MaxKinsectStamina);
	BroadcastResourceUIState(true);
	return true;
}

void URes_InsectGlaive::OnWeaponUnequipped()
{
	ResetDescendingThrustKinsect();
	if (CoAttackComponent)
	{
		CoAttackComponent->Stop();
		CoAttackComponent->DestroyComponent();
		CoAttackComponent = nullptr;
	}
	CancelPowderVortexTransaction(0, false);
	CancelAutomaticMarkBehavior(true);
	KinsectPlayerCommandLeaseToken = FWeaponActionToken();
	KinsectCommandSource = EKinsectCommandSource::None;
	++KinsectCommandSerial;
	ClearDanceStacks(EIGDanceClearReason::Unequipped);
	SetKinsectActiveTag(false);
	// Kinsect.Active is Resource-exclusive. Normalize any pre-TagLedger count left
	// by an older runtime so a weapon swap cannot carry that stale state forward.
	if (UAbilitySystemComponent* ASC = GetPlayerASC())
	{
		ASC->SetLooseGameplayTagCount(KinsectActiveTag(), 0);
	}
	if (KinsectActor)
	{
		KinsectActor->Destroy();
		KinsectActor = nullptr;
	}
	KinsectData = nullptr;
	KinsectAttachComponent.Reset();
	KinsectAttachSocket = NAME_None;
	KinsectActionAttachmentToken = FWeaponActionToken();
	KinsectStamina = 0.f;
	MaxKinsectStamina = 0.f;
	bDepletionEdgeTriggered = false;
}

bool URes_InsectGlaive::CanDeployKinsect(const FKinsectFlightRequest& Request) const
{
	return !bRuntimeShuttingDown && KinsectActor && KinsectData
		&& KinsectActor->GetState() != EKinsectState::ActionAttached
		&& !KinsectActionAttachmentToken.IsValid()
		&& KinsectStamina > 0.f && IsRuntimeRequestCurrent(Request.RuntimeToken)
		&& KinsectActor->CanBeginFlight(Request);
}

bool URes_InsectGlaive::DeployKinsect(const FKinsectFlightRequest& Request)
{
	if (!CanDeployKinsect(Request))
	{
		return false;
	}
	FKinsectFlightRequest PlayerRequest = Request;
	PlayerRequest.CommandSource = EKinsectCommandSource::Player;
	PlayerRequest.CommandSerial = GetNextKinsectCommandSerial();
	PlayerRequest.MarkSerial = 0;
	if (!KinsectActor->BeginFlight(PlayerRequest))
	{
		return false;
	}
	AdvanceKinsectCommand(EKinsectCommandSource::Player);
	SetKinsectActiveTag(true);
	return true;
}

bool URes_InsectGlaive::BeginKinsectPlayerCommandLease(
	const FWeaponActionToken& ActionToken)
{
	UMHGZWeaponRuntimeHostComponent* Host = ActionToken.RuntimeToken.Host.Get();
	if (bRuntimeShuttingDown || !ActionToken.IsValid()
		|| !IsRuntimeRequestCurrent(ActionToken.RuntimeToken)
		|| !Host || !Host->IsActionRegistered(ActionToken)
		|| !KinsectActor || !KinsectData
		|| KinsectActor->GetState() == EKinsectState::ActionAttached
		|| (KinsectPlayerCommandLeaseToken.IsValid()
			&& KinsectPlayerCommandLeaseToken != ActionToken))
	{
		return false;
	}
	if (KinsectPlayerCommandLeaseToken == ActionToken)
	{
		return true;
	}

	// An already-running return cannot be commandeered by mark seeking and should
	// remain live while the recall Montage plays. Other requests become stale
	// before the automatic flight is interrupted.
	const bool bReturnAlreadyInProgress =
		KinsectActor->GetState() == EKinsectState::Returning;
	if (!bReturnAlreadyInProgress)
	{
		AdvanceKinsectCommand(EKinsectCommandSource::Player);
	}
	KinsectPlayerCommandLeaseToken = ActionToken;
	if (!bReturnAlreadyInProgress
		&& KinsectActor->GetState() == EKinsectState::Flying
		&& KinsectActor->ActiveRequest.CommandSource == EKinsectCommandSource::AutoMark)
	{
		KinsectActor->Interrupt();
	}
	bAutoMarkApproachComplete = false;
	NextAutoMarkActionWorldTime = 0.0;
	return true;
}

void URes_InsectGlaive::EndKinsectPlayerCommandLease(
	const FWeaponActionToken& ActionToken)
{
	if (!ActionToken.IsValid() || KinsectPlayerCommandLeaseToken != ActionToken)
	{
		return;
	}
	KinsectPlayerCommandLeaseToken = FWeaponActionToken();
	if (KinsectCommandSource == EKinsectCommandSource::Player && KinsectActor
		&& KinsectActor->GetState() != EKinsectState::Flying
		&& KinsectActor->GetState() != EKinsectState::Returning
		&& KinsectActor->GetState() != EKinsectState::ActionAttached)
	{
		AdvanceKinsectCommand(EKinsectCommandSource::None);
		EarliestAutoMarkUpdateFrame = GFrameCounter + 1;
	}
}

bool URes_InsectGlaive::DeployKinsectFromActionAttachment(
	const FWeaponActionToken& ActionToken,
	const FKinsectFlightRequest& Request)
{
	if (bRuntimeShuttingDown || !IsKinsectActionAttachmentCurrent(ActionToken)
		|| !KinsectData || KinsectStamina <= 0.0f
		|| Request.RuntimeToken != ActionToken.RuntimeToken)
	{
		return false;
	}
	FKinsectFlightRequest ActionRequest = Request;
	ActionRequest.CommandSource = EKinsectCommandSource::Action;
	ActionRequest.CommandSerial = GetNextKinsectCommandSerial();
	ActionRequest.MarkSerial = 0;
	if (!KinsectActor->BeginFlightFromActionAttachment(ActionToken, ActionRequest))
	{
		return false;
	}

	KinsectActionAttachmentToken = FWeaponActionToken();
	AdvanceKinsectCommand(EKinsectCommandSource::Action);
	SetKinsectActiveTag(true);
	return true;
}

bool URes_InsectGlaive::RecallKinsect(const EKinsectCommandSource Source)
{
	if (bRuntimeShuttingDown || !KinsectActor
		|| Source == EKinsectCommandSource::None
		|| Source == EKinsectCommandSource::AutoMark)
	{
		return false;
	}
	if (KinsectActionAttachmentToken.IsValid())
	{
		return EndKinsectActionAttachmentAndReturn(KinsectActionAttachmentToken, Source);
	}
	if (KinsectActor->GetState() == EKinsectState::Attached)
	{
		return false;
	}
	if (KinsectActor->GetState() == EKinsectState::Returning)
	{
		return true;
	}
	const uint64 CommandSerial = AdvanceKinsectCommand(Source);
	if (KinsectActor->StartReturn(CommandSerial, Source))
	{
		return true;
	}
	if (KinsectCommandSerial == CommandSerial)
	{
		AdvanceKinsectCommand(EKinsectCommandSource::None);
		EarliestAutoMarkUpdateFrame = GFrameCounter + 1;
	}
	return false;
}

bool URes_InsectGlaive::BeginKinsectActionAttachment(
	const FWeaponActionToken& ActionToken,
	USkeletalMeshComponent* WeaponMesh, const FName SocketName)
{
	if (bRuntimeShuttingDown || !ActionToken.IsValid()
		|| !IsRuntimeRequestCurrent(ActionToken.RuntimeToken)
		|| !KinsectActor || !KinsectActor->Mesh || !KinsectData
		|| !KinsectAttachComponent.IsValid()
		|| (!KinsectAttachSocket.IsNone()
			&& !KinsectAttachComponent->DoesSocketExist(KinsectAttachSocket))
		|| KinsectActionAttachmentToken.IsValid()
		|| !WeaponMesh || SocketName.IsNone()
		|| !WeaponMesh->DoesSocketExist(SocketName))
	{
		return false;
	}

	if (!KinsectActor->BeginActionAttachment(ActionToken, WeaponMesh, SocketName))
	{
		return false;
	}

	AdvanceKinsectCommand(EKinsectCommandSource::Action);
	KinsectActionAttachmentToken = ActionToken;
	SetKinsectActiveTag(true);
	return true;
}

bool URes_InsectGlaive::IsKinsectActionAttachmentCurrent(
	const FWeaponActionToken& ActionToken) const
{
	return !bRuntimeShuttingDown && ActionToken.IsValid()
		&& ActionToken == KinsectActionAttachmentToken
		&& IsRuntimeRequestCurrent(ActionToken.RuntimeToken)
		&& KinsectActor
		&& KinsectActor->IsActionAttachmentOwnedBy(ActionToken);
}

bool URes_InsectGlaive::BeginKinsectSlashPreparation(
	const FWeaponActionToken& ActionToken, USkeletalMeshComponent* WeaponMesh,
	const FName SocketName, const float AvailableReturnTime)
{
	if (bRuntimeShuttingDown || !ActionToken.IsValid()
		|| !IsRuntimeRequestCurrent(ActionToken.RuntimeToken)
		|| !KinsectActor || !KinsectData || !KinsectAttachComponent.IsValid()
		|| KinsectActionAttachmentToken.IsValid())
	{
		return false;
	}
	const bool bHasRequiredStamina = FMath::IsFinite(KinsectStamina)
		&& FMath::IsFinite(MaxKinsectStamina) && MaxKinsectStamina > 0.f
		&& KinsectStamina >= MaxKinsectStamina * 0.2f;
	if (!KinsectActor->BeginSlashPreparation(ActionToken, WeaponMesh, SocketName,
		AvailableReturnTime, bHasRequiredStamina))
	{
		return false;
	}
	AdvanceKinsectCommand(EKinsectCommandSource::Action);
	KinsectActionAttachmentToken = ActionToken;
	SetKinsectActiveTag(KinsectActor->IsDeployed());
	return true;
}

bool URes_InsectGlaive::IsKinsectActionControlCurrent(const FWeaponActionToken& ActionToken) const
{
	return !bRuntimeShuttingDown && ActionToken.IsValid()
		&& ActionToken == KinsectActionAttachmentToken
		&& IsRuntimeRequestCurrent(ActionToken.RuntimeToken)
		&& KinsectActor && KinsectActor->IsActionControlOwnedBy(ActionToken);
}

bool URes_InsectGlaive::EndKinsectActionAttachmentAndReturn(
	const FWeaponActionToken& ActionToken, const EKinsectCommandSource Source)
{
	if (!IsKinsectActionControlCurrent(ActionToken) || !CombatConfig
		|| !KinsectAttachComponent.IsValid()
		|| (Source != EKinsectCommandSource::Action
			&& Source != EKinsectCommandSource::Player
			&& Source != EKinsectCommandSource::System))
	{
		return false;
	}

	const float ArrivalRadius = CombatConfig->KinsectArrivalRadius;
	const uint64 CommandSerial = AdvanceKinsectCommand(Source);
	const bool bReturnStarted = KinsectActor->EndActionAttachmentAndReturn(
		ActionToken, RuntimeContext.RuntimeToken, ArrivalRadius,
		CommandSerial, Source);
	KinsectActionAttachmentToken = FWeaponActionToken();
	SetKinsectActiveTag(KinsectActor->IsDeployed());
	if (bReturnStarted && KinsectActor->GetState() == EKinsectState::Hovering
		&& KinsectStamina <= 0.f)
	{
		// A slash can freeze an already exhausted Returning insect. Once its
		// hold ends, restore the system recall instead of stranding a zero pool.
		RecallKinsect(EKinsectCommandSource::System);
	}
	if (!bReturnStarted)
	{
		// Defensive recovery for invalid return tuning/target: restore the original
		// arm socket immediately and deliver an already pending extract once.
		KinsectActor->AttachToPlayer(KinsectAttachComponent.Get(), KinsectAttachSocket);
		OnKinsectReachedPlayer(KinsectActor->TakePendingExtractColors(),
			CommandSerial, Source);
		return true;
	}
	return true;
}

void URes_InsectGlaive::OnKinsectReachedPlayer(const FGameplayTagContainer& ExtractColors,
	const uint64 CommandSerial, const EKinsectCommandSource Source)
{
	if (CommandSerial != 0
		&& !IsKinsectCommandCurrent(CommandSerial, Source, 0,
			RuntimeContext.RuntimeToken))
	{
		return;
	}
	SetKinsectActiveTag(false);
	bDepletionEdgeTriggered = false;
	for (const FGameplayTag& ExtractColor : ExtractColors)
	{
		ApplyExtractInternal(ExtractColor, nullptr, FGuid(), FGuid(), CommandSerial);
	}
	if (CommandSerial != 0 && KinsectCommandSerial == CommandSerial)
	{
		AdvanceKinsectCommand(EKinsectCommandSource::None);
		EarliestAutoMarkUpdateFrame = GFrameCounter + 1;
	}
	BroadcastResourceUIState(true);
}

void URes_InsectGlaive::OnKinsectFlightEnded(
	const FKinsectFlightRequest& Request, const EKinsectFlightEndReason Reason)
{
	if (Request.CommandSerial == 0 || !IsKinsectCommandCurrent(Request)
		|| bRuntimeShuttingDown)
	{
		return;
	}

	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	FWeaponCombatTelemetryEvent FlightEndEvent;
	FlightEndEvent.EventName = Request.CommandSource == EKinsectCommandSource::PowderVortex
		? FName(TEXT("VortexFlightEnded"))
		: (Request.CommandSource == EKinsectCommandSource::AutoMark
			? FName(TEXT("AutoMarkApproachEnded")) : FName(TEXT("CommandCompleted")));
	FlightEndEvent.CommandSerial = Request.CommandSerial;
	FlightEndEvent.FlightID = Request.FlightInstanceID;
	FlightEndEvent.SourceAttackID = Request.SourceAttackInstanceID;
	FlightEndEvent.MarkID = ActiveMarkID;
	FlightEndEvent.Reason = KinsectFlightEndReasonName(Reason);
	FlightEndEvent.ActivationSequenceID = Request.ActionActivationSequenceID;
	RecordCombatTelemetryEvent(MoveTemp(FlightEndEvent));
	if (HandleDescendingThrustKinsectFlightEnd(Request, Reason)) return;
	if (Request.CommandSource == EKinsectCommandSource::PowderVortex)
	{
		if (Request.CommandSerial != ActivePowderVortexCommandSerial)
		{
			return;
		}
		if (Reason == EKinsectFlightEndReason::Arrival
			&& BeginPowderVortexGathering(Request, Request.TargetPointSnapshot))
		{
			return;
		}
		CancelPowderVortexTransaction(Request.CommandSerial, true,
			KinsectFlightEndReasonName(Reason));
		return;
	}
	if (Request.CommandSource == EKinsectCommandSource::AutoMark)
	{
		bAutoMarkApproachComplete = Reason == EKinsectFlightEndReason::Arrival;
		NextAutoMarkActionWorldTime = Now
			+ (CombatConfig ? FMath::Max(0.0f,
				CombatConfig->KinsectAutoMarkAttackInterval) : 0.0f);
		EarliestAutoMarkUpdateFrame = GFrameCounter + 1;
		return;
	}

	if (Request.PostFlightPolicy == EKinsectPostFlightPolicy::Hover
		&& (Request.CommandSource == EKinsectCommandSource::Player
			|| Request.CommandSource == EKinsectCommandSource::Action))
	{
		AdvanceKinsectCommand(EKinsectCommandSource::None);
		bAutoMarkApproachComplete = false;
		NextAutoMarkActionWorldTime = Now;
		EarliestAutoMarkUpdateFrame = GFrameCounter + 1;
	}
	else if (Request.PostFlightPolicy == EKinsectPostFlightPolicy::ReturnToOwner
		&& KinsectActor && KinsectActor->GetState() == EKinsectState::Hovering)
	{
		// A failed return target/speed must not leave a permanent Player/Action
		// command owner on a detached hovering Kinsect.
		AdvanceKinsectCommand(EKinsectCommandSource::None);
		EarliestAutoMarkUpdateFrame = GFrameCounter + 1;
	}
}

bool URes_InsectGlaive::IsKinsectCommandCurrent(
	const FKinsectFlightRequest& Request) const
{
	return Request.CommandSerial == 0
		? IsRuntimeRequestCurrent(Request.RuntimeToken)
		: IsKinsectCommandCurrent(Request.CommandSerial,
			Request.CommandSource, Request.MarkSerial, Request.RuntimeToken);
}

bool URes_InsectGlaive::IsKinsectCommandCurrent(
	const uint64 CommandSerial, const EKinsectCommandSource Source,
	const uint64 ExpectedMarkSerial, const FWeaponRuntimeToken& RuntimeToken) const
{
	if (!IsRuntimeRequestCurrent(RuntimeToken))
	{
		return false;
	}
	if (CommandSerial == 0)
	{
		return true;
	}
	if (CommandSerial != KinsectCommandSerial || Source == EKinsectCommandSource::None
		|| Source != KinsectCommandSource)
	{
		return false;
	}
	return Source != EKinsectCommandSource::AutoMark
		|| (ExpectedMarkSerial != 0 && ExpectedMarkSerial == MarkSerial
			&& HasValidKinsectMark());
}

bool URes_InsectGlaive::IsRuntimeRequestCurrent(const FWeaponRuntimeToken& Token) const
{
	return !bRuntimeShuttingDown && Token.IsValid()
		&& RuntimeContext.RuntimeToken == Token
		&& Token.Host.IsValid() && Token.Host->IsTokenCurrent(Token);
}

uint64 URes_InsectGlaive::GetNextKinsectCommandSerial() const
{
	uint64 NextSerial = KinsectCommandSerial + 1;
	if (NextSerial == 0)
	{
		NextSerial = 1;
	}
	return NextSerial;
}

uint64 URes_InsectGlaive::AdvanceKinsectCommand(
	const EKinsectCommandSource Source)
{
	ResetDescendingThrustKinsect(); // retire bookkeeping only, never overwrite a newly committed flight
	const uint64 PreviousSerial = KinsectCommandSerial;
	const EKinsectCommandSource PreviousSource = KinsectCommandSource;
	const bool bCancelPowderVortex =
		ActivePowderVortexCommandSerial != 0
		&& ActivePowderVortexCommandSerial == PreviousSerial
		&& Source != EKinsectCommandSource::PowderVortex
		&& Source != EKinsectCommandSource::AutoMark;
	const bool bInterruptPowderVortexFlight = bCancelPowderVortex
		&& KinsectActor
		&& KinsectActor->GetState() == EKinsectState::Flying
		&& KinsectActor->ActiveRequest.CommandSource
			== EKinsectCommandSource::PowderVortex
		&& KinsectActor->ActiveRequest.CommandSerial == PreviousSerial;
	if (PreviousSerial != 0 && PreviousSource != EKinsectCommandSource::None
		&& Source != PreviousSource && Source != EKinsectCommandSource::None)
	{
		FWeaponCombatTelemetryEvent Event;
		Event.EventName = TEXT("CommandPreempted");
		Event.CommandSerial = PreviousSerial;
		Event.Reason = KinsectCommandSourceName(PreviousSource);
		RecordCombatTelemetryEvent(MoveTemp(Event));
	}
	KinsectCommandSerial = GetNextKinsectCommandSerial();
	KinsectCommandSource = Source;
	if (CoAttackComponent && CoAttackComponent->IsRunning())
	{
		CoAttackComponent->Stop();
		// A new flight/attachment may already have been committed atomically.
		// Never interrupt that new state while retiring the old sampler.
		if (KinsectActor && KinsectActor->GetState() == EKinsectState::CoAttack)
		{
			KinsectActor->Interrupt();
		}
	}
	if (Source != EKinsectCommandSource::None)
	{
		FWeaponCombatTelemetryEvent Event;
		Event.EventName = TEXT("CommandAccepted");
		Event.CommandSerial = KinsectCommandSerial;
		Event.Reason = KinsectCommandSourceName(Source);
		RecordCombatTelemetryEvent(MoveTemp(Event));
	}
	if (bCancelPowderVortex)
	{
		CancelPowderVortexTransaction(PreviousSerial, false);
		if (bInterruptPowderVortexFlight)
		{
			KinsectActor->Interrupt();
		}
	}
	if (Source != EKinsectCommandSource::AutoMark)
	{
		AutoMarkCommandSerial = 0;
		AutoMarkCommandMarkSerial = 0;
		bAutoMarkApproachComplete = false;
		NextAutoMarkActionWorldTime = 0.0;
	}
	return KinsectCommandSerial;
}

bool URes_InsectGlaive::IsAutomaticMarkBehaviorEligible() const
{
	if (bRuntimeShuttingDown || !KinsectActor || !KinsectData || !CombatConfig
		|| KinsectStamina <= 0.0f || !HasValidKinsectMark()
		|| !KinsectAttachComponent.IsValid()
		|| KinsectActionAttachmentToken.IsValid()
		|| KinsectPlayerCommandLeaseToken.IsValid()
		|| (KinsectCommandSource != EKinsectCommandSource::None
			&& KinsectCommandSource != EKinsectCommandSource::AutoMark)
		|| !FMath::IsFinite(CombatConfig->KinsectAutoMarkSearchRadius)
		|| !FMath::IsFinite(CombatConfig->KinsectAutoMarkAttackInterval)
		|| CombatConfig->KinsectAutoMarkSearchRadius <= 0.0f
		|| CombatConfig->KinsectAutoMarkAttackInterval <= 0.0f)
	{
		return false;
	}
	const EKinsectState KinsectState = KinsectActor->GetState();
	const bool bCanStartFromHover = KinsectState == EKinsectState::Hovering;
	const bool bCanContinueAutoFlight = KinsectState == EKinsectState::Flying
		&& KinsectCommandSource == EKinsectCommandSource::AutoMark
		&& KinsectActor->ActiveRequest.CommandSerial == KinsectCommandSerial
		&& KinsectActor->ActiveRequest.MarkSerial == MarkSerial;
	if (!bCanStartFromHover && !bCanContinueAutoFlight)
	{
		return false;
	}
	const UMHGZWeaponRuntimeHostComponent* Host = RuntimeContext.RuntimeToken.Host.Get();
	// Hunter attacks can coexist with a detached Kinsect's automatic command.
	// The leases, attachment and command owner above arbitrate actual Kinsect use.
	return IsRuntimeRequestCurrent(RuntimeContext.RuntimeToken) && Host;
}

void URes_InsectGlaive::CancelAutomaticMarkBehavior(
	const bool bInterruptFlight)
{
	if (KinsectCommandSource != EKinsectCommandSource::AutoMark)
	{
		return;
	}
	const bool bInterruptingAutoFlight = KinsectActor && bInterruptFlight
		&& KinsectActor->GetState() == EKinsectState::Flying
		&& KinsectActor->ActiveRequest.CommandSource == EKinsectCommandSource::AutoMark
		&& KinsectActor->ActiveRequest.CommandSerial == KinsectCommandSerial;
	AdvanceKinsectCommand(EKinsectCommandSource::None);
	if (bInterruptingAutoFlight)
	{
		KinsectActor->Interrupt();
	}
}

void URes_InsectGlaive::RecordAutomaticMarkWorldBlock(
	const FKinsectFlightRequest& Request, const FHitResult& Hit) const
{
	if (Request.CommandSource != EKinsectCommandSource::AutoMark
		|| !IsKinsectCommandCurrent(Request))
	{
		return;
	}
	FWeaponCombatTelemetryEvent Event;
	Event.EventName = TEXT("AutoMarkWorldBlocked");
	Event.MarkID = ActiveMarkID;
	Event.CommandSerial = Request.CommandSerial;
	Event.FlightID = Request.FlightInstanceID;
	Event.TargetActor = Hit.GetActor() ? Hit.GetActor()->GetFName() : NAME_None;
	Event.Hitzone = Hit.GetComponent() ? Hit.GetComponent()->GetFName() : NAME_None;
	Event.ImpactPoint = Hit.ImpactPoint;
	Event.Reason = Hit.bStartPenetrating ? TEXT("StartPenetrating") : TEXT("BlockingSweep");
	RecordCombatTelemetryEvent(MoveTemp(Event));
}

bool URes_InsectGlaive::ResolveAutomaticMarkApproachPoint(FVector& OutPoint) const
{
	FVector MarkLocation;
	FVector MarkNormal;
	if (!KinsectActor || !GetWorld()
		|| !GetKinsectMarkSurfaceFrame(MarkSerial, MarkLocation, MarkNormal))
	{
		return false;
	}
	// A sphere enclosing the rotating flight capsule keeps the approach reachable
	// even for a mark near a floor or on a downward-facing surface.
	const float Clearance = KinsectActor->Collision
		? KinsectActor->Collision->GetScaledCapsuleHalfHeight() + 2.0f : 2.0f;
	OutPoint = MarkLocation + MarkNormal * Clearance;
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(KinsectMarkApproach), false);
	Params.AddIgnoredActor(KinsectActor);
	Params.AddIgnoredActor(GetOwner());
	Params.AddIgnoredActor(ActiveMarkTargetActor.Get());
	FHitResult FloorHit;
	if (GetWorld()->SweepSingleByObjectType(FloorHit,
		OutPoint + FVector::UpVector * (Clearance * 4.0f), OutPoint,
		FQuat::Identity, Objects, FCollisionShape::MakeSphere(Clearance), Params))
	{
		if (FloorHit.bStartPenetrating)
		{
			return false;
		}
		OutPoint = FloorHit.Location + FloorHit.ImpactNormal * 2.0f;
	}
	return !OutPoint.ContainsNaN();
}

bool URes_InsectGlaive::StartAutomaticMarkSeek()
{
	if (!IsAutomaticMarkBehaviorEligible() || !KinsectActor || !GetWorld()
		|| KinsectActor->GetState() != EKinsectState::Hovering)
	{
		return false;
	}

	FVector MarkLocation;
	FVector MarkNormal;
	if (!GetKinsectMarkSurfaceFrame(MarkSerial, MarkLocation, MarkNormal)
		|| FVector::Dist(KinsectActor->GetActorLocation(), MarkLocation)
			> CombatConfig->KinsectAutoMarkSearchRadius)
	{
		return false;
	}

	FVector ApproachPoint;
	if (!ResolveAutomaticMarkApproachPoint(ApproachPoint))
	{
		return false;
	}
	const float ApproachOffset = FVector::Distance(MarkLocation, ApproachPoint);
	const double Now = GetWorld()->GetTimeSeconds();
	FKinsectFlightRequest Request;
	Request.RuntimeToken = RuntimeContext.RuntimeToken;
	Request.TrajectoryMode = EKinsectTrajectoryMode::ToPoint;
	Request.TargetPointSnapshot = ApproachPoint;
	Request.MaxDistance = FMath::Max(KinsectData->MaxFlightRange,
		CombatConfig->KinsectAutoMarkSearchRadius + ApproachOffset);
	Request.FlightSpeed = KinsectData->FlightSpeed;
	Request.ArrivalRadius = 1.0f;
	Request.DamageMode = EKinsectDamageMode::None;
	Request.ExtractMode = EKinsectExtractMode::None;
	Request.PostFlightPolicy = EKinsectPostFlightPolicy::Hover;
	Request.MotionValue = 0.0f;
	Request.RehitInterval = 0.0f;
	Request.FlightInstanceID = FGuid::NewGuid();
	Request.CommandSource = EKinsectCommandSource::AutoMark;
	Request.CommandSerial = GetNextKinsectCommandSerial();
	Request.MarkSerial = MarkSerial;
	if (FVector::DistSquared(KinsectActor->GetActorLocation(), ApproachPoint) <= 1.0f)
	{
		AdvanceKinsectCommand(EKinsectCommandSource::AutoMark);
		Request.CommandSerial = KinsectCommandSerial;
		if (!KinsectActor->BeginAutomaticMarkContact(Request))
		{
			AdvanceKinsectCommand(EKinsectCommandSource::None);
			return false;
		}
		AutoMarkCommandSerial = KinsectCommandSerial;
		AutoMarkCommandMarkSerial = MarkSerial;
		bAutoMarkApproachComplete = true;
		NextAutoMarkActionWorldTime = Now
			+ CombatConfig->KinsectAutoMarkAttackInterval;
		return true;
	}
	if (!KinsectActor->CanBeginFlight(Request)
		|| !KinsectActor->BeginFlight(Request))
	{
		return false;
	}
	AdvanceKinsectCommand(EKinsectCommandSource::AutoMark);
	AutoMarkCommandSerial = KinsectCommandSerial;
	AutoMarkCommandMarkSerial = MarkSerial;
	bAutoMarkApproachComplete = false;
	NextAutoMarkActionWorldTime = 0.0;
	return true;
}

bool URes_InsectGlaive::PerformAutomaticMarkContactAttack()
{
	if (!IsAutomaticMarkBehaviorEligible() || !KinsectActor
		|| KinsectActor->GetState() != EKinsectState::Hovering
		|| KinsectCommandSerial != AutoMarkCommandSerial
		|| MarkSerial != AutoMarkCommandMarkSerial)
	{
		return false;
	}

	FHitResult Hit;
	if (!KinsectActor->SweepAutomaticMarkContact(KinsectCommandSerial,
		MarkSerial, RuntimeContext.RuntimeToken, Hit))
	{
		FWeaponCombatTelemetryEvent Event;
		Event.EventName = TEXT("AutoMarkContactMiss");
		Event.MarkID = ActiveMarkID;
		Event.CommandSerial = KinsectCommandSerial;
		Event.TargetActor = ActiveMarkTargetActor->GetFName();
		Event.Hitzone = ActiveMarkHitzone->GetFName();
		Event.ImpactPoint = KinsectActor->GetActorLocation();
		Event.Reason = TEXT("NoMarkedPartContact");
		RecordCombatTelemetryEvent(MoveTemp(Event));
		return false;
	}
	const FGuid HitInstanceID = FGuid::NewGuid();
	const FGuid AttackInstanceID = FGuid::NewGuid();
	const FKinsectFlightRequest ContactRequest = KinsectActor->ActiveRequest;
	const TWeakObjectPtr<AKinsect> ContactKinsect = KinsectActor;
	const TWeakObjectPtr<UMHGZMonsterHitzoneComponent> ContactPart =
		UMHGZMonsterHitzoneComponent::Resolve(Hit.GetComponent());
	const auto IsContactCurrent = [&]()
	{
		return ContactKinsect.IsValid() && KinsectActor == ContactKinsect.Get()
			&& KinsectActor->GetState() == EKinsectState::Hovering
			&& KinsectActor->ActiveRequest.FlightInstanceID == ContactRequest.FlightInstanceID
			&& IsKinsectCommandCurrent(ContactRequest);
	};
	const bool bDamageApplied = ApplyKinsectDamage(Hit,
		CombatConfig->SendKinsectMotionValue, HitInstanceID, AttackInstanceID);
	if (bDamageApplied && IsContactCurrent())
	{
		// Seeking stays non-extracting. Only this successful real contact may
		// collect, using the ordinary single-color cargo and return-delivery path.
		if (ContactPart.IsValid()
			&& KinsectActor->CapturePendingExtractColor(ContactPart->ExtractColorTag))
		{
			FWeaponCombatTelemetryEvent Event;
			Event.EventName = TEXT("KinsectCargoCaptured");
			Event.ResourceTag = ContactPart->ExtractColorTag.GetTagName();
			Event.SourceAttackID = AttackInstanceID;
			Event.HitID = HitInstanceID;
			Event.CommandSerial = ContactRequest.CommandSerial;
			Event.MarkID = ActiveMarkID;
			Event.TargetActor = Hit.GetActor()->GetFName();
			Event.Hitzone = ContactPart->GetFName();
			Event.ImpactPoint = Hit.ImpactPoint;
			Event.Reason = TEXT("AutoMarkPendingReturn");
			RecordCombatTelemetryEvent(MoveTemp(Event));
			BroadcastResourceUIState(true);
		}
		// Damage/UI callbacks may issue a newer player command.
		if (!IsContactCurrent()) return bDamageApplied;
		HandleSuccessfulKinsectDamage(RuntimeContext.RuntimeToken,
			AttackInstanceID, HitInstanceID, Hit,
			EIGPowderSpawnPolicy::AutoMarkContact);
	}
	return bDamageApplied;
}

void URes_InsectGlaive::UpdateAutomaticMarkBehavior()
{
	if (!KinsectActor || bRuntimeShuttingDown
		|| GFrameCounter < EarliestAutoMarkUpdateFrame)
	{
		return;
	}

	if (KinsectCommandSource == EKinsectCommandSource::AutoMark
		&& (!IsAutomaticMarkBehaviorEligible()
			|| MarkSerial != AutoMarkCommandMarkSerial
			|| KinsectCommandSerial != AutoMarkCommandSerial))
	{
		CancelAutomaticMarkBehavior(true);
		return;
	}
	if (!IsAutomaticMarkBehaviorEligible())
	{
		const bool bDeferredByPriority = HasValidKinsectMark()
			&& KinsectActor->GetState() != EKinsectState::Attached
			&& (KinsectPlayerCommandLeaseToken.IsValid()
				|| KinsectActionAttachmentToken.IsValid()
				|| (KinsectCommandSource != EKinsectCommandSource::None
					&& KinsectCommandSource != EKinsectCommandSource::AutoMark));
		if (bDeferredByPriority && !bAutoMarkDeferredByPriority)
		{
			FWeaponCombatTelemetryEvent Event;
			Event.EventName = TEXT("AutoDeferred");
			Event.MarkID = ActiveMarkID;
			Event.CommandSerial = KinsectCommandSerial;
			Event.Reason = TEXT("HigherPriorityPlayerOrActionCommand");
			RecordCombatTelemetryEvent(MoveTemp(Event));
		}
		bAutoMarkDeferredByPriority = bDeferredByPriority;
		return;
	}
	bAutoMarkDeferredByPriority = false;

	const EKinsectState State = KinsectActor->GetState();
	if (State == EKinsectState::Attached || State == EKinsectState::Returning
		|| State == EKinsectState::ActionAttached)
	{
		CancelAutomaticMarkBehavior(false);
		return;
	}
	FVector MarkLocation;
	FVector MarkNormal;
	if (!GetKinsectMarkSurfaceFrame(MarkSerial, MarkLocation, MarkNormal))
	{
		CancelAutomaticMarkBehavior(true);
		return;
	}
	if (FVector::Dist(KinsectActor->GetActorLocation(), MarkLocation)
		> CombatConfig->KinsectAutoMarkSearchRadius)
	{
		CancelAutomaticMarkBehavior(true);
		return;
	}

	FVector ApproachPoint;
	if (!ResolveAutomaticMarkApproachPoint(ApproachPoint))
	{
		CancelAutomaticMarkBehavior(true);
		return;
	}
	if (State == EKinsectState::Flying)
	{
		KinsectActor->UpdateAutomaticMarkTarget(ApproachPoint,
			AutoMarkCommandSerial, AutoMarkCommandMarkSerial);
		return;
	}
	if (State != EKinsectState::Hovering)
	{
		return;
	}

	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (KinsectCommandSource != EKinsectCommandSource::AutoMark)
	{
		StartAutomaticMarkSeek();
		return;
	}
	if (!bAutoMarkApproachComplete)
	{
		if (Now >= NextAutoMarkActionWorldTime)
		{
			StartAutomaticMarkSeek();
		}
		return;
	}

	if (FVector::DistSquared(KinsectActor->GetActorLocation(), ApproachPoint) > 25.0f)
	{
		bAutoMarkApproachComplete = false;
		NextAutoMarkActionWorldTime = Now;
		StartAutomaticMarkSeek();
		return;
	}
	// Keep facing the local mark between attacks as its component/bone moves.
	// Only the visual mesh rotates: the physical capsule must stay floor-safe.
	if (!KinsectActor->FaceAutomaticMark(AutoMarkCommandSerial, AutoMarkCommandMarkSerial))
	{
		return;
	}
	if (Now >= NextAutoMarkActionWorldTime)
	{
		const uint64 ContactSerial = AutoMarkCommandSerial;
		const uint64 ContactMarkSerial = AutoMarkCommandMarkSerial;
		PerformAutomaticMarkContactAttack();
		if (KinsectCommandSerial == ContactSerial && MarkSerial == ContactMarkSerial
			&& !bRuntimeShuttingDown && CombatConfig)
		{
			NextAutoMarkActionWorldTime = Now + CombatConfig->KinsectAutoMarkAttackInterval;
		}
	}
}

bool URes_InsectGlaive::IsLeafExtractTag(const FGameplayTag& ExtractColor) const
{
	return ExtractColor.MatchesTagExact(WhiteExtractTag())
		|| ExtractColor.MatchesTagExact(OrangeExtractTag())
		|| ExtractColor.MatchesTagExact(RedExtractTag());
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
	if (ExtractColor.MatchesTagExact(WhiteExtractTag()))
	{
		EffectClass = CombatConfig->WhiteEffectClass;
		Duration = CombatConfig->WhiteExtractDuration;
		MultiplierTag = FGameplayTag::RequestGameplayTag(
			TEXT("Data.IG.Buff.MoveSpeedMultiplier"));
		Multiplier = CombatConfig->WhiteMoveSpeedMultiplier;
	}
	else if (ExtractColor.MatchesTagExact(OrangeExtractTag()))
	{
		EffectClass = CombatConfig->OrangeEffectClass;
		Duration = CombatConfig->OrangeExtractDuration;
		MultiplierTag = FGameplayTag::RequestGameplayTag(
			TEXT("Data.IG.Buff.DefenseMultiplier"));
		Multiplier = CombatConfig->OrangeDefenseMultiplier;
	}
	else if (ExtractColor.MatchesTagExact(RedExtractTag()))
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
	if (!IsHandleActive(ActiveExtractHandles.FindRef(WhiteExtractTag()))
		|| !IsHandleActive(ActiveExtractHandles.FindRef(OrangeExtractTag()))
		|| !IsHandleActive(ActiveExtractHandles.FindRef(RedExtractTag())))
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
		ActiveExtractHandles.FindRef(WhiteExtractTag()),
		ActiveExtractHandles.FindRef(OrangeExtractTag()),
		ActiveExtractHandles.FindRef(RedExtractTag())
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
	for (const FGameplayTag& Color : { WhiteExtractTag(), OrangeExtractTag(), RedExtractTag() })
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
	FillExtract(RedExtractTag(), State.bRed,
		State.RedRemainingSeconds, State.RedRatio);
	FillExtract(WhiteExtractTag(), State.bWhite,
		State.WhiteRemainingSeconds, State.WhiteRatio);
	FillExtract(OrangeExtractTag(), State.bOrange,
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

	for (const FIGPowderRuntimeSnapshot& Powder : GetPowderSnapshots())
	{
		if (Powder.State == EIGPowderState::Reserved)
		{
			++State.ReservedPowderCount;
		}
		else if (Powder.State == EIGPowderState::Available)
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

bool URes_InsectGlaive::ApplyKinsectDamage(
	const FHitResult& Hit, const float MotionValue, const FGuid& HitInstanceID,
	const FGuid& SourceAttackID)
{
	return ApplyKinsectDamageWithSnapshot(Hit, MotionValue, GetModifiedKinsectAttackPower(),
		HitInstanceID, SourceAttackID);
}

bool URes_InsectGlaive::ApplyKinsectDamageWithSnapshot(
	const FHitResult& Hit, const float MotionValue, const float AttackPower,
	const FGuid& HitInstanceID, const FGuid& SourceAttackID)
{
	const auto RecordDamageResult = [this, &Hit, &HitInstanceID, &SourceAttackID](
		const bool bApplied, const FName Reason)
	{
		FWeaponCombatTelemetryEvent Event;
		Event.EventName = bApplied
			? FName(TEXT("KinsectDamageSubmitted"))
			: FName(TEXT("KinsectDamageRejected"));
		Event.SourceAttackID = SourceAttackID;
		Event.HitID = HitInstanceID;
		Event.TargetActor = IsValid(Hit.GetActor()) ? Hit.GetActor()->GetFName() : NAME_None;
		Event.Hitzone = Hit.GetComponent() ? Hit.GetComponent()->GetFName() : NAME_None;
		Event.ImpactPoint = Hit.ImpactPoint;
		Event.Reason = Reason;
		RecordCombatTelemetryEvent(MoveTemp(Event));
	};

	UMHGZMonsterHitzoneComponent* Hitzone =
		UMHGZMonsterHitzoneComponent::Resolve(Hit.GetComponent());
	AActor* Target = Hit.GetActor();
	UAbilitySystemComponent* SourceASC = GetPlayerASC();
	UAbilitySystemComponent* TargetASC = Target
		? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target) : nullptr;
	if (!Hitzone || !SourceASC || !TargetASC || !HitInstanceID.IsValid()
		|| !FMath::IsFinite(AttackPower) || AttackPower < 0.f
		|| !FMath::IsFinite(MotionValue) || MotionValue < 0.f)
	{
		RecordDamageResult(false, TEXT("InvalidHitzoneASCOrHitID"));
		return false;
	}

	FGameplayEffectContextHandle ContextHandle = SourceASC->MakeEffectContext();
	FMHGZGameplayEffectContext* Context =
		FMHGZGameplayEffectContext::ExtractEffectContext(ContextHandle);
	if (!Context)
	{
		RecordDamageResult(false, TEXT("MissingGameplayEffectContext"));
		return false;
	}
	Context->AddHitResult(Hit, true);
	// IncomingHitResolver 按 AttackInstanceID 全局去重；贯通同一 Flight 的每次
	// 有效伤害必须有独立 HitInstanceID，FlightInstanceID 只留在请求/调试域。
	Context->AttackInstanceID = HitInstanceID;
	Context->DamageSourceType = EMHGZDamageSourceType::Kinsect;
	Context->bUseHitzoneDefense = true;
	Context->HitzoneTag = Hitzone->HitzoneTag;
	Context->HitCueTag = FGameplayTag::RequestGameplayTag(TEXT("GameplayCue.Hit.Kinsect"));
	if (AActor* Source = RuntimeContext.Character.Get())
	{
		Context->AddInstigator(Source, Source);
	}

	FGameplayEffectSpecHandle Spec = SourceASC->MakeOutgoingSpec(
		UMHGZDamageGameplayEffect::StaticClass(), 1.f, ContextHandle);
	if (!Spec.IsValid())
	{
		RecordDamageResult(false, TEXT("DamageEffectSpecRejected"));
		return false;
	}
	Spec.Data->SetSetByCallerMagnitude(
		FGameplayTag::RequestGameplayTag(TEXT("Damage.MotionValue")), MotionValue);
	Spec.Data->SetSetByCallerMagnitude(
		FGameplayTag::RequestGameplayTag(TEXT("Damage.BaseStagger")), 0.0f);
	Spec.Data->SetSetByCallerMagnitude(
		FGameplayTag::RequestGameplayTag(TEXT("Damage.AttackPower")),
		AttackPower);
	const bool bApplied = SourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data, TargetASC)
		.WasSuccessfullyApplied();
	RecordDamageResult(bApplied, bApplied
		? FName(TEXT("Applied")) : FName(TEXT("GameplayEffectRejected")));
	return bApplied;
}

float URes_InsectGlaive::GetModifiedKinsectAttackPower() const
{
	return KinsectData ? FMath::Max(0.f, KinsectData->KinsectAttackPower) : 0.f;
}

bool URes_InsectGlaive::LaunchKinsectMark(const FWeaponAimSnapshot& AimSnapshot)
{
	if (bRuntimeShuttingDown || !CombatConfig || !GetWorld()
		|| AimSnapshot.Context != EWeaponAimSnapshotContext::Kinsect
		|| AimSnapshot.Direction.ContainsNaN()
		|| AimSnapshot.Direction.GetSafeNormal().IsNearlyZero())
	{
		return false;
	}

	TSubclassOf<AIGMarkProjectile> ProjectileClass = CombatConfig->KinsectMarkProjectileClass;
	if (!ProjectileClass)
	{
		ProjectileClass = AIGMarkProjectile::StaticClass();
	}

	FVector LaunchOrigin = AimSnapshot.Origin;
	FVector LaunchDirection = AimSnapshot.Direction.GetSafeNormal();
	if (!CombatConfig->KinsectMarkLaunchSocket.IsNone())
	{
		ACharacter* Character = RuntimeContext.Character.Get();
		TArray<USkeletalMeshComponent*> Meshes;
		if (Character)
		{
			Character->GetComponents<USkeletalMeshComponent>(Meshes);
		}
		USkeletalMeshComponent* LaunchMesh = nullptr;
		for (USkeletalMeshComponent* Mesh : Meshes)
		{
			if (Mesh && Mesh->ComponentHasTag(TEXT("WeaponTrace"))
				&& Mesh->DoesSocketExist(CombatConfig->KinsectMarkLaunchSocket))
			{
				LaunchMesh = Mesh;
				break;
			}
		}
		if (!LaunchMesh)
		{
			return false;
		}
		LaunchOrigin = LaunchMesh->GetSocketLocation(CombatConfig->KinsectMarkLaunchSocket);
		LaunchDirection = (AimSnapshot.TargetPoint - LaunchOrigin).GetSafeNormal();
		if (LaunchDirection.IsNearlyZero())
		{
			return false;
		}
	}
	FWeaponAimSnapshot LaunchSnapshot = AimSnapshot;
	LaunchSnapshot.Origin = LaunchOrigin;
	LaunchSnapshot.Direction = LaunchDirection;
	AIGMarkProjectile* Projectile = GetWorld()->SpawnActorDeferred<AIGMarkProjectile>(
		ProjectileClass,
		FTransform(LaunchDirection.Rotation(), LaunchOrigin),
		GetOwner(), Cast<APawn>(GetOwner()), ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Projectile)
	{
		return false;
	}
	Projectile->Initialize(this, RuntimeContext.RuntimeToken, LaunchSnapshot,
		CombatConfig->KinsectMarkProjectileSpeed,
		CombatConfig->KinsectMarkProjectileRadius,
		CombatConfig->KinsectMarkMaxDistance,
		CombatConfig->KinsectMarkProjectileLifetime);
	Projectile->FinishSpawning(FTransform(LaunchDirection.Rotation(), LaunchOrigin));
	return true;
}

bool URes_InsectGlaive::SetKinsectMarkFromMeleeHit(const FHitResult& Hit)
{
	UMHGZMonsterHitzoneComponent* Hitzone =
		UMHGZMonsterHitzoneComponent::Resolve(Hit.GetComponent());
	return SetKinsectMarkInternal(Hitzone, Hit.ImpactPoint,
		Hit.ImpactNormal, Hit.TraceStart - Hit.ImpactPoint, nullptr);
}

bool URes_InsectGlaive::SetKinsectMark(UMHGZMonsterHitzoneComponent* Hitzone,
	const FVector& ImpactPoint, AIGMarkProjectile* Projectile,
	const FVector& PreferredOutwardDirection)
{
	return SetKinsectMarkInternal(Hitzone, ImpactPoint,
		FVector::ZeroVector, PreferredOutwardDirection, Projectile);
}

bool URes_InsectGlaive::SetKinsectMarkInternal(
	UMHGZMonsterHitzoneComponent* Hitzone, const FVector& ImpactPoint,
	const FVector& ImpactNormal, const FVector& PreferredOutwardDirection,
	AIGMarkProjectile* Projectile)
{
	if (bRuntimeShuttingDown || !CombatConfig || !GetWorld() || !Hitzone
		|| Hitzone->GetWorld() != GetWorld()
		|| !IsValid(Hitzone->GetOwner())
		|| !FMath::IsFinite(ImpactPoint.X) || !FMath::IsFinite(ImpactPoint.Y)
		|| !FMath::IsFinite(ImpactPoint.Z)
		|| !FMath::IsFinite(CombatConfig->KinsectMarkDuration)
		|| CombatConfig->KinsectMarkDuration <= 0.0f
		|| (Projectile && Projectile->GetOwner() != GetOwner()))
	{
		return false;
	}

	AActor* TargetActor = Hitzone->GetOwner();
	UAbilitySystemComponent* TargetASC =
		UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(TargetActor);
	const UMHGZAttributeSet* TargetAttributes = TargetASC
		? TargetASC->GetSet<UMHGZAttributeSet>() : nullptr;
	if (TargetAttributes && TargetAttributes->GetHealth() <= 0.0f)
	{
		return false;
	}

	const FQuat HitzoneRotation = Hitzone->GetComponentQuat();
	FVector SurfaceNormal = ImpactNormal.GetSafeNormal();
	if (SurfaceNormal.IsNearlyZero())
	{
		SurfaceNormal = (ImpactPoint - Hitzone->GetComponentLocation()).GetSafeNormal();
	}
	if (SurfaceNormal.IsNearlyZero())
	{
		SurfaceNormal = HitzoneRotation.GetUpVector();
	}
	FVector VisualSurfacePoint = ImpactPoint;
	FVector VisualSurfaceNormal = SurfaceNormal;
	if (const AMHGZMonsterBase* Monster = Cast<AMHGZMonsterBase>(TargetActor))
	{
		if (!Monster->ResolveHitEffectVisualSurface(
			Hitzone, ImpactPoint, SurfaceNormal, PreferredOutwardDirection,
			VisualSurfacePoint, VisualSurfaceNormal))
		{
			VisualSurfacePoint = ImpactPoint;
			VisualSurfaceNormal = SurfaceNormal;
		}
	}
	VisualSurfaceNormal = VisualSurfaceNormal.GetSafeNormal();
	if (VisualSurfacePoint.ContainsNaN() || VisualSurfaceNormal.IsNearlyZero())
	{
		VisualSurfacePoint = ImpactPoint;
		VisualSurfaceNormal = SurfaceNormal;
	}
	if (VisualSurfaceNormal.IsNearlyZero())
	{
		return false;
	}
	FVector TangentX = FVector::VectorPlaneProject(
		HitzoneRotation.GetForwardVector(), VisualSurfaceNormal).GetSafeNormal();
	if (TangentX.IsNearlyZero())
	{
		TangentX = FVector::VectorPlaneProject(
			HitzoneRotation.GetRightVector(), VisualSurfaceNormal).GetSafeNormal();
	}
	if (TangentX.IsNearlyZero())
	{
		return false;
	}
	const FVector TangentY = FVector::CrossProduct(
		VisualSurfaceNormal, TangentX).GetSafeNormal();
	if (TangentY.IsNearlyZero())
	{
		return false;
	}
	const double CreatedAt = GetWorld()->GetTimeSeconds();
	const double ExpireAt = CreatedAt + CombatConfig->KinsectMarkDuration;
	if (!FMath::IsFinite(ExpireAt))
	{
		return false;
	}

	ClearKinsectMark(EIGMarkClearReason::Replaced);
	ActiveMarkHitzone = Hitzone;
	ActiveMarkProjectile = Projectile;
	ActiveMarkTargetActor = TargetActor;
	ActiveMarkTargetASC = TargetAttributes ? TargetASC : nullptr;
	ActiveMarkBoneName = Hitzone->BoneName;
	bActiveMarkUsesProjectile = Projectile != nullptr;
	const FTransform HitzoneTransform = Hitzone->GetComponentTransform();
	// The surface point is only the visual/automatic-seek anchor; the Debuff
	// applies to the whole hitzone, independent of effect size or surface location.
	ActiveMarkLocalPoint = HitzoneTransform.InverseTransformPosition(VisualSurfacePoint);
	ActiveMarkLocalNormal = HitzoneRotation.UnrotateVector(
		VisualSurfaceNormal).GetSafeNormal();
	ActiveMarkLocalTangentX = HitzoneRotation.UnrotateVector(TangentX).GetSafeNormal();
	ActiveMarkLocalTangentY = HitzoneRotation.UnrotateVector(TangentY).GetSafeNormal();
	ActiveMarkVisualLocalPoint = ActiveMarkLocalPoint;
	ActiveMarkVisualLocalNormal = ActiveMarkLocalNormal;
	ActiveMarkCreatedAt = CreatedAt;
	ActiveMarkExpireTime = ExpireAt;
	ActiveMarkID = FGuid::NewGuid();
	Hitzone->ApplyKinsectMarkDebuff(this, ActiveMarkID, ExpireAt);
	if (TargetAttributes && TargetASC)
	{
		MarkTargetHealthChangedDelegateHandle =
			TargetASC->GetGameplayAttributeValueChangeDelegate(
				UMHGZAttributeSet::GetHealthAttribute()).AddUObject(this,
					&URes_InsectGlaive::HandleMarkTargetHealthChanged);
	}
	SetMarkActiveTag(true);

	const uint64 ThisMarkSerial = ++MarkSerial;
	GetWorld()->GetTimerManager().SetTimer(MarkExpiryTimer,
		FTimerDelegate::CreateWeakLambda(this, [this, ThisMarkSerial]()
		{
			ExpireKinsectMark(ThisMarkSerial);
		}), CombatConfig->KinsectMarkDuration, false);
	FWeaponCombatTelemetryEvent MarkEvent;
	MarkEvent.EventName = TEXT("MarkCreated");
	MarkEvent.MarkID = ActiveMarkID;
	MarkEvent.TargetActor = TargetActor->GetFName();
	MarkEvent.Hitzone = Hitzone->GetFName();
	MarkEvent.ImpactPoint = ImpactPoint;
	MarkEvent.ExpireAt = ExpireAt;
	RecordCombatTelemetryEvent(MoveTemp(MarkEvent));
	SpawnKinsectMarkEffect();
	return true;
}

void URes_InsectGlaive::ExpireKinsectMark(const uint64 ExpectedMarkSerial)
{
	if (ExpectedMarkSerial == MarkSerial)
	{
		ClearKinsectMark(EIGMarkClearReason::Expired);
	}
}

void URes_InsectGlaive::ClearKinsectMark(EIGMarkClearReason Reason)
{
	if (UMHGZMonsterHitzoneComponent* Part = ActiveMarkHitzone.Get())
	{
		Part->RemoveKinsectMarkDebuff(this, ActiveMarkID);
	}
	if (ActiveMarkID.IsValid())
	{
		FWeaponCombatTelemetryEvent MarkEvent;
		MarkEvent.EventName = (Reason == EIGMarkClearReason::TargetInvalid
			|| Reason == EIGMarkClearReason::TargetDead)
			? FName(TEXT("MarkTargetDeadOrInvalid")) : FName(TEXT("MarkCleared"));
		MarkEvent.MarkID = ActiveMarkID;
		MarkEvent.TargetActor = ActiveMarkTargetActor.IsValid()
			? ActiveMarkTargetActor->GetFName() : NAME_None;
		MarkEvent.Hitzone = ActiveMarkBoneName;
		MarkEvent.ImpactPoint = ActiveMarkHitzone.IsValid()
			? ActiveMarkHitzone->GetComponentTransform().TransformPosition(ActiveMarkLocalPoint)
			: FVector::ZeroVector;
		MarkEvent.ExpireAt = ActiveMarkExpireTime;
		switch (Reason)
		{
		case EIGMarkClearReason::Replaced: MarkEvent.Reason = TEXT("Replaced"); break;
		case EIGMarkClearReason::Expired: MarkEvent.Reason = TEXT("Expired"); break;
		case EIGMarkClearReason::TargetInvalid: MarkEvent.Reason = TEXT("TargetInvalid"); break;
		case EIGMarkClearReason::TargetDead: MarkEvent.Reason = TEXT("TargetDead"); break;
		case EIGMarkClearReason::PowderVortex: MarkEvent.Reason = TEXT("PowderVortex"); break;
		case EIGMarkClearReason::WeaponChanged: MarkEvent.Reason = TEXT("WeaponChanged"); break;
		case EIGMarkClearReason::RuntimeShutdown: MarkEvent.Reason = TEXT("RuntimeShutdown"); break;
		default: MarkEvent.Reason = TEXT("Unknown"); break;
		}
		RecordCombatTelemetryEvent(MoveTemp(MarkEvent));
	}
	++MarkSerial;
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(MarkExpiryTimer);
	}
	UnbindMarkTargetHealthDelegate();
	if (AIGMarkProjectile* Projectile = ActiveMarkProjectile.Get())
	{
		Projectile->Destroy();
	}
	ClearKinsectMarkEffect();
	ActiveMarkProjectile.Reset();
	ActiveMarkHitzone.Reset();
	ActiveMarkTargetActor.Reset();
	ActiveMarkTargetASC.Reset();
	ActiveMarkBoneName = NAME_None;
	ActiveMarkID.Invalidate();
	ActiveMarkLocalPoint = FVector::ZeroVector;
	ActiveMarkLocalNormal = FVector::UpVector;
	ActiveMarkLocalTangentX = FVector::ForwardVector;
	ActiveMarkLocalTangentY = FVector::RightVector;
	ActiveMarkVisualLocalPoint = FVector::ZeroVector;
	ActiveMarkVisualLocalNormal = FVector::UpVector;
	ActiveMarkCreatedAt = 0.0;
	ActiveMarkExpireTime = 0.0;
	bActiveMarkUsesProjectile = false;
	SetMarkActiveTag(false);
}

void URes_InsectGlaive::HandleMarkTargetHealthChanged(
	const FOnAttributeChangeData& ChangeData)
{
	if (ChangeData.NewValue <= 0.0f && ActiveMarkTargetASC.IsValid()
		&& ActiveMarkHitzone.IsValid())
	{
		ClearKinsectMark(EIGMarkClearReason::TargetDead);
	}
}

void URes_InsectGlaive::UnbindMarkTargetHealthDelegate()
{
	if (UAbilitySystemComponent* TargetASC = ActiveMarkTargetASC.Get();
		TargetASC && MarkTargetHealthChangedDelegateHandle.IsValid())
	{
		TargetASC->GetGameplayAttributeValueChangeDelegate(
			UMHGZAttributeSet::GetHealthAttribute()).Remove(
				MarkTargetHealthChangedDelegateHandle);
	}
	MarkTargetHealthChangedDelegateHandle.Reset();
}

void URes_InsectGlaive::SpawnKinsectMarkEffect()
{
	ClearKinsectMarkEffect();
	UMHGZMonsterHitzoneComponent* Hitzone = ActiveMarkHitzone.Get();
	UParticleSystem* Template = CombatConfig
		? CombatConfig->KinsectMarkEstablishedEffect : nullptr;
	auto RecordEffectEvent = [this, Hitzone](const FName EventName, const FName Reason)
	{
		FWeaponCombatTelemetryEvent Event;
		Event.EventName = EventName;
		Event.MarkID = ActiveMarkID;
		Event.TargetActor = ActiveMarkTargetActor.IsValid()
			? ActiveMarkTargetActor->GetFName() : NAME_None;
		Event.Hitzone = Hitzone ? Hitzone->GetFName() : ActiveMarkBoneName;
		Event.ImpactPoint = Hitzone && IsValid(Hitzone->GetOwner())
			? Hitzone->GetComponentTransform().TransformPosition(ActiveMarkVisualLocalPoint)
			: FVector::ZeroVector;
		Event.ExpireAt = ActiveMarkExpireTime;
		Event.Reason = Reason;
		RecordCombatTelemetryEvent(MoveTemp(Event));
	};
	if (!Hitzone || !Template || !IsValid(Hitzone->GetOwner()))
	{
		RecordEffectEvent(FName(TEXT("MarkEffectSpawnFailed")),
			!Hitzone ? FName(TEXT("MissingHitzone"))
			: !Template ? FName(TEXT("MissingTemplate")) : FName(TEXT("InvalidTarget")));
		return;
	}

	UParticleSystemComponent* Effect = NewObject<UParticleSystemComponent>(
		Hitzone->GetOwner(), TEXT("IG_MarkYellowPowder"));
	if (!Effect)
	{
		RecordEffectEvent(FName(TEXT("MarkEffectSpawnFailed")),
			FName(TEXT("ComponentCreationFailed")));
		return;
	}
	Effect->bAutoDestroy = false;
	// Do not let RegisterComponent auto-activate the burst at the component's
	// default transform. The effect is attached and positioned below first.
	Effect->bAutoActivate = false;
	Effect->SetTemplate(Template);
	if (!Effect->AttachToComponent(Hitzone, FAttachmentTransformRules::KeepWorldTransform))
	{
		Effect->DestroyComponent();
		RecordEffectEvent(FName(TEXT("MarkEffectSpawnFailed")),
			FName(TEXT("AttachFailed")));
		return;
	}
	Effect->RegisterComponent();
	if (!Effect->IsRegistered())
	{
		Effect->DestroyComponent();
		RecordEffectEvent(FName(TEXT("MarkEffectSpawnFailed")),
			FName(TEXT("RegisterFailed")));
		return;
	}
	// The mark remains attached to the moving/scaled hitzone, but its visible patch
	// is authored in world centimeters and must not inherit a monster or dummy scale.
	Effect->SetAbsolute(false, false, true);
	Effect->SetWorldScale3D(FVector::OneVector);
	const FQuat HitzoneRotation = Hitzone->GetComponentQuat();
	const FVector WorldNormal = HitzoneRotation.RotateVector(
		ActiveMarkVisualLocalNormal).GetSafeNormal();
	const FVector WorldTangentX = HitzoneRotation.RotateVector(
		ActiveMarkLocalTangentX).GetSafeNormal();
	if (!WorldNormal.IsNearlyZero() && !WorldTangentX.IsNearlyZero())
	{
		Effect->SetWorldRotation(FRotationMatrix::MakeFromXZ(
			WorldTangentX, WorldNormal).ToQuat());
	}
	Effect->SetWorldLocation(Hitzone->GetComponentTransform()
		.TransformPosition(ActiveMarkVisualLocalPoint) + WorldNormal * 0.5f);
	// Hitzones are query-only collision components and some target setups hide
	// their helper components. Explicitly make the child particle component
	// renderable instead of inheriting an accidental hidden/visibility state.
	Effect->SetHiddenInGame(false, true);
	Effect->SetVisibility(true, true);
	Effect->ActivateSystem(true);
	ActiveMarkEffect = Effect;
	RecordEffectEvent(FName(TEXT("MarkEffectSpawned")), FName(TEXT("Activated")));

	// "Activated" alone cannot distinguish a visible emitter from an empty or
	// culled Cascade system. Sample after the particle component has had time to
	// tick; SetTimerForNextTick can still run before its first particle update.
	const FGuid SpawnedMarkID = ActiveMarkID;
	TWeakObjectPtr<UParticleSystemComponent> WeakEffect = Effect;
	if (UWorld* World = GetWorld())
	{
		FTimerHandle DiagnosticTimer;
		World->GetTimerManager().SetTimer(DiagnosticTimer,
			FTimerDelegate::CreateWeakLambda(this, [this, WeakEffect, SpawnedMarkID]()
			{
				UParticleSystemComponent* CurrentEffect = WeakEffect.Get();
				if (!CurrentEffect || ActiveMarkID != SpawnedMarkID
					|| ActiveMarkEffect != CurrentEffect)
				{
					return;
				}

				FWeaponCombatTelemetryEvent StateEvent;
				StateEvent.EventName = FName(TEXT("MarkEffectRuntimeState"));
				StateEvent.MarkID = SpawnedMarkID;
				StateEvent.TargetActor = ActiveMarkTargetActor.IsValid()
					? ActiveMarkTargetActor->GetFName() : NAME_None;
				StateEvent.Hitzone = ActiveMarkHitzone.IsValid()
					? ActiveMarkHitzone->GetFName() : ActiveMarkBoneName;
				StateEvent.ImpactPoint = CurrentEffect->GetComponentLocation();
				StateEvent.Reason = FName(*FString::Printf(
					TEXT("particles=%d;active=%d;visible=%d;registered=%d;template=%s"),
					CurrentEffect->GetNumActiveParticles(),
					CurrentEffect->IsActive() ? 1 : 0,
					CurrentEffect->IsVisible() ? 1 : 0,
					CurrentEffect->IsRegistered() ? 1 : 0,
					*GetPathNameSafe(CurrentEffect->Template)));
				RecordCombatTelemetryEvent(MoveTemp(StateEvent));
			}), 0.1f, false);
	}
}

void URes_InsectGlaive::ClearKinsectMarkEffect()
{
	if (ActiveMarkEffect)
	{
		ActiveMarkEffect->DestroyComponent();
		ActiveMarkEffect = nullptr;
	}
}

bool URes_InsectGlaive::HasValidKinsectMark() const
{
	if (!ActiveMarkHitzone.IsValid()
		|| !ActiveMarkHitzone->HasKinsectMarkDebuffFrom(this, ActiveMarkID)
		|| !IsValid(ActiveMarkHitzone->GetOwner())
		|| !ActiveMarkTargetActor.IsValid()
		|| ActiveMarkTargetActor.Get() != ActiveMarkHitzone->GetOwner()
		|| (bActiveMarkUsesProjectile && !ActiveMarkProjectile.IsValid()))
	{
		return false;
	}
	const UAbilitySystemComponent* TargetASC = ActiveMarkTargetASC.Get();
	const UMHGZAttributeSet* TargetAttributes = TargetASC
		? TargetASC->GetSet<UMHGZAttributeSet>() : nullptr;
	return !TargetAttributes || TargetAttributes->GetHealth() > 0.0f;
}

bool URes_InsectGlaive::GetKinsectMarkWorldLocation(FVector& OutLocation) const
{
	if (!HasValidKinsectMark())
	{
		return false;
	}
	OutLocation = ActiveMarkHitzone->GetComponentTransform()
		.TransformPosition(ActiveMarkLocalPoint);
	return true;
}

bool URes_InsectGlaive::GetKinsectMarkSurfaceFrame(
	const uint64 ExpectedMarkSerial, FVector& OutLocation, FVector& OutNormal) const
{
	if (ExpectedMarkSerial == 0 || ExpectedMarkSerial != MarkSerial
		|| !HasValidKinsectMark() || !GetWorld()
		|| GetWorld()->GetTimeSeconds() >= ActiveMarkExpireTime
		|| !ActiveMarkHitzone.IsValid())
	{
		return false;
	}

	const FTransform HitzoneTransform = ActiveMarkHitzone->GetComponentTransform();
	OutLocation = HitzoneTransform.TransformPosition(ActiveMarkLocalPoint);
	OutNormal = ActiveMarkHitzone->GetComponentQuat()
		.RotateVector(ActiveMarkLocalNormal).GetSafeNormal();
	return !OutLocation.ContainsNaN() && !OutNormal.ContainsNaN()
		&& !OutNormal.IsNearlyZero();
}

bool URes_InsectGlaive::IsHitOnKinsectMarkedPart(const FHitResult& Hit) const
{
	const UMHGZMonsterHitzoneComponent* HitHitzone =
		UMHGZMonsterHitzoneComponent::Resolve(Hit.GetComponent());
	if (!HasValidKinsectMark() || !CombatConfig || !GetWorld()
		|| GetWorld()->GetTimeSeconds() >= ActiveMarkExpireTime
		|| !Hit.bBlockingHit
		|| Hit.GetActor() != ActiveMarkTargetActor.Get()
		|| HitHitzone != ActiveMarkHitzone.Get()
		|| !FMath::IsFinite(Hit.ImpactPoint.X)
		|| !FMath::IsFinite(Hit.ImpactPoint.Y)
		|| !FMath::IsFinite(Hit.ImpactPoint.Z))
	{
		return false;
	}

	return HitHitzone->HasKinsectMarkDebuffFrom(this, ActiveMarkID);
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

bool URes_InsectGlaive::SpawnPowderForSuccessfulHit(
	const FWeaponRuntimeToken& Runtime,
	const FGuid& SourceAttackID,
	const FGuid& HitID,
	const FHitResult& Hit, const float KinsectAttackPower, const float MotionValue)
{
	if (!FMath::IsFinite(KinsectAttackPower) || KinsectAttackPower < 0.0f
        || !FMath::IsFinite(MotionValue) || MotionValue < 0.0f) return false;
    return SpawnPowderInternal(Runtime, SourceAttackID, HitID, Hit,
        MHGZDamageMath::PowderDamage(KinsectAttackPower, MotionValue), EIGPowderSpawnPolicy::None,
        KinsectAttackPower, MotionValue);
}

bool URes_InsectGlaive::HandleSuccessfulKinsectDamage(
	const FWeaponRuntimeToken& Runtime, const FGuid& SourceAttackID,
	const FGuid& HitID, const FHitResult& Hit,
	const EIGPowderSpawnPolicy SpawnPolicy)
{
	const auto RecordRejectedSpawn = [this, &SourceAttackID, &HitID, &Hit, SpawnPolicy](
		const FName Reason)
	{
		FWeaponCombatTelemetryEvent Event;
		Event.EventName = TEXT("PowderRejected");
		Event.SourceAttackID = SourceAttackID;
		Event.HitID = HitID;
		Event.TargetActor = IsValid(Hit.GetActor()) ? Hit.GetActor()->GetFName() : NAME_None;
		Event.Hitzone = Hit.GetComponent() ? Hit.GetComponent()->GetFName() : NAME_None;
		Event.ImpactPoint = Hit.ImpactPoint;
		Event.SpawnPolicy = PowderSpawnPolicyName(SpawnPolicy);
		Event.Reason = Reason;
		RecordCombatTelemetryEvent(MoveTemp(Event));
	};
	if (SpawnPolicy == EIGPowderSpawnPolicy::None || bRuntimeShuttingDown
		|| !CombatConfig || !GetWorld() || !IsRuntimeRequestCurrent(Runtime)
		|| Runtime != RuntimeContext.RuntimeToken || !SourceAttackID.IsValid()
		|| !HitID.IsValid() || SpawnedPowderHitIDs.Contains(HitID)
		|| !Hit.bBlockingHit || !IsValid(Hit.GetActor()))
	{
		RecordRejectedSpawn(SpawnedPowderHitIDs.Contains(HitID)
			? FName(TEXT("DuplicateHit")) : FName(TEXT("InvalidRequest")));
		return false;
	}

	const double Now = GetWorld()->GetTimeSeconds();
	if (SpawnPolicy == EIGPowderSpawnPolicy::MarkedContact
		|| SpawnPolicy == EIGPowderSpawnPolicy::AutoMarkContact)
	{
		if (!HasValidKinsectMark())
		{
			RecordRejectedSpawn(TEXT("NoActiveMark"));
			return false;
		}
		if (Now >= ActiveMarkExpireTime)
		{
			RecordRejectedSpawn(TEXT("MarkExpired"));
			return false;
		}
		if (Hit.GetActor() != ActiveMarkTargetActor.Get())
		{
			RecordRejectedSpawn(TEXT("WrongMarkTarget"));
			return false;
		}
		if (!UMHGZMonsterHitzoneComponent::Resolve(Hit.GetComponent()))
		{
			RecordRejectedSpawn(TEXT("NotMonsterHitzone"));
			return false;
		}
		if (!IsHitOnKinsectMarkedPart(Hit))
		{
			RecordRejectedSpawn(TEXT("UnmarkedPart"));
			return false;
		}
		if (SpawnPolicy == EIGPowderSpawnPolicy::MarkedContact
			&& LastMarkedContactPowderWorldTime >= 0.0
			&& Now - LastMarkedContactPowderWorldTime < CombatConfig->MarkedHitPowderCooldown)
		{
			RecordRejectedSpawn(TEXT("SharedCooldown"));
			return false;
		}
	}
	else if (SpawnPolicy != EIGPowderSpawnPolicy::AwakenedPiercing
		&& SpawnPolicy != EIGPowderSpawnPolicy::AwakenedEachHit)
	{
		RecordRejectedSpawn(TEXT("UnsupportedPolicy"));
		return false;
	}

	if (!FMath::IsFinite(CombatConfig->PowderMotionValue)
		|| CombatConfig->PowderMotionValue <= 0.0f)
	{
		RecordRejectedSpawn(TEXT("DamageConfigDisabled"));
		return false;
	}
	const float AttackPowerSnapshot = GetModifiedKinsectAttackPower();
	const float MotionValueSnapshot = CombatConfig->PowderMotionValue;
	const float DamageSnapshot = MHGZDamageMath::PowderDamage(AttackPowerSnapshot, MotionValueSnapshot);
	if (!FMath::IsFinite(AttackPowerSnapshot) || AttackPowerSnapshot <= 0.0f
		|| !FMath::IsFinite(DamageSnapshot) || DamageSnapshot <= 0.0f)
	{
		RecordRejectedSpawn(TEXT("InvalidDamageSnapshot"));
		return false;
	}

	if (!SpawnPowderInternal(Runtime, SourceAttackID, HitID, Hit, DamageSnapshot,
		SpawnPolicy, AttackPowerSnapshot, MotionValueSnapshot))
	{
		RecordRejectedSpawn(TEXT("SpawnFailedOrAtCapacity"));
		return false;
	}
	if (SpawnPolicy == EIGPowderSpawnPolicy::MarkedContact)
	{
		LastMarkedContactPowderWorldTime = Now;
	}
	return true;
}

bool URes_InsectGlaive::SpawnPowderInternal(
	const FWeaponRuntimeToken& Runtime, const FGuid& SourceAttackID,
	const FGuid& HitID, const FHitResult& Hit, const float DamageContribution,
	const EIGPowderSpawnPolicy SpawnPolicy,
	const float KinsectAttackPowerSnapshot,
	const float MotionValueSnapshot)
{
	UWorld* World = GetWorld();
	if (bRuntimeShuttingDown || !CombatConfig || !World
		|| !IsRuntimeRequestCurrent(Runtime) || Runtime != RuntimeContext.RuntimeToken
		|| !SourceAttackID.IsValid() || !HitID.IsValid()
		|| SpawnedPowderHitIDs.Contains(HitID)
		|| !Hit.bBlockingHit || !IsValid(Hit.GetActor())
		|| !FMath::IsFinite(Hit.ImpactPoint.X) || !FMath::IsFinite(Hit.ImpactPoint.Y)
		|| !FMath::IsFinite(Hit.ImpactPoint.Z)
		|| !FMath::IsFinite(DamageContribution) || DamageContribution < 0.0f
		|| !FMath::IsFinite(KinsectAttackPowerSnapshot)
		|| KinsectAttackPowerSnapshot < 0.0f
		|| !FMath::IsFinite(MotionValueSnapshot)
		|| MotionValueSnapshot < 0.0f)
	{
		return false;
	}
	int32 LivePowderCount = 0;
	const double Now = World->GetTimeSeconds();
	const TArray<TWeakObjectPtr<AIGPowderActor>> ExistingPowders = OwnedPowders;
	for (const TWeakObjectPtr<AIGPowderActor>& WeakPowder : ExistingPowders)
	{
		if (AIGPowderActor* Powder = WeakPowder.Get();
			Powder && Powder->GetExpireWorldTime() <= Now)
		{
			HandlePowderLifetimeExpired(Powder, Powder->GetLifetimeRevision());
		}
	}
	for (const TWeakObjectPtr<AIGPowderActor>& WeakPowder : OwnedPowders)
	{
		const AIGPowderActor* Powder = WeakPowder.Get();
		if (Powder && Powder->GetRuntimeToken() == Runtime
			&& Powder->GetPowderState() != EIGPowderState::Consumed
			&& Powder->GetExpireWorldTime() > Now)
		{
			++LivePowderCount;
		}
	}
	if (LivePowderCount >= CombatConfig->PowderMaxCount)
	{
		FWeaponCombatTelemetryEvent Event;
		Event.EventName = TEXT("PowderRejected");
		Event.SourceAttackID = SourceAttackID;
		Event.HitID = HitID;
		Event.TargetActor = IsValid(Hit.GetActor()) ? Hit.GetActor()->GetFName() : NAME_None;
		Event.Hitzone = Hit.GetComponent() ? Hit.GetComponent()->GetFName() : NAME_None;
		Event.ImpactPoint = Hit.ImpactPoint;
		Event.SpawnPolicy = PowderSpawnPolicyName(SpawnPolicy);
		Event.Reason = FName(*FString::Printf(TEXT("RuntimeCap%d"), CombatConfig->PowderMaxCount));
		Event.MemberCount = LivePowderCount;
		RecordCombatTelemetryEvent(MoveTemp(Event));
		UE_LOG(LogTemp, Warning,
			TEXT("[IG Powder] CapRejected: Runtime owns %d live powders (configured cap %d)."),
			LivePowderCount, CombatConfig->PowderMaxCount);
		return false;
	}

	FVector VisualSurfacePoint = Hit.ImpactPoint;
	FVector VisualSurfaceNormal = Hit.ImpactNormal.GetSafeNormal();
	const UMHGZMonsterHitzoneComponent* HitHitzone =
		UMHGZMonsterHitzoneComponent::Resolve(Hit.GetComponent());
	if (const AMHGZMonsterBase* Monster = Cast<AMHGZMonsterBase>(Hit.GetActor());
		Monster && HitHitzone)
	{
		FVector ResolvedPoint = VisualSurfacePoint;
		FVector ResolvedNormal = VisualSurfaceNormal;
		if (Monster->ResolveHitEffectVisualSurface(HitHitzone,
			Hit.ImpactPoint, Hit.ImpactNormal,
			Hit.TraceStart - Hit.ImpactPoint, ResolvedPoint, ResolvedNormal)
			&& !ResolvedPoint.ContainsNaN() && !ResolvedNormal.IsNearlyZero())
		{
			VisualSurfacePoint = ResolvedPoint;
			VisualSurfaceNormal = ResolvedNormal.GetSafeNormal();
		}
	}
	if (VisualSurfaceNormal.IsNearlyZero() && HitHitzone)
	{
		VisualSurfaceNormal = (Hit.ImpactPoint - HitHitzone->GetComponentLocation()).GetSafeNormal();
	}
	if (VisualSurfaceNormal.IsNearlyZero())
	{
		VisualSurfaceNormal = FVector::UpVector;
	}
	// The rendered powder cloud is about 15 cm across. Put its center one radius
	// plus a small margin outside the resolved visible surface so it cannot spawn
	// inside large proxy hitzones or the monster's opaque collision mesh.
	constexpr float PowderVisualSurfaceClearanceCm = 9.0f;
	const FVector PresentationLocation = VisualSurfacePoint
		+ VisualSurfaceNormal * PowderVisualSurfaceClearanceCm;
	const FTransform SpawnTransform(FRotator::ZeroRotator, PresentationLocation);
	const double SpawnTime = Now;
	uint64 SpawnID = NextPowderSpawnSequenceID++;
	if (SpawnID == 0)
	{
		SpawnID = NextPowderSpawnSequenceID++;
	}
	if (SpawnID == 0)
	{
		return false;
	}

	TSubclassOf<AIGPowderActor> ActorClass = CombatConfig->PowderActorClass;
	if (!ActorClass)
	{
		ActorClass = AIGPowderActor::StaticClass();
	}
	const float Lifetime = FMath::IsFinite(CombatConfig->PowderLifetime)
		&& CombatConfig->PowderLifetime > 0.0f
		? CombatConfig->PowderLifetime : PowderWorldLifetimeSeconds;
	AIGPowderActor* Powder = World->SpawnActorDeferred<AIGPowderActor>(
		ActorClass, SpawnTransform, GetOwner(),
		Cast<APawn>(GetOwner()), ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Powder)
	{
		UE_LOG(LogTemp, Error, TEXT("[IG Powder] SpawnActorDeferred failed for %s."),
			*GetNameSafe(*ActorClass));
		return false;
	}

	const bool bInitialized = Powder->InitializePowder(this,
		RuntimeContext.Controller.Get(), Runtime, FGuid::NewGuid(),
		SourceAttackID, HitID, Hit, SpawnPolicy, SpawnTime, Lifetime,
		KinsectAttackPowerSnapshot, MotionValueSnapshot,
		DamageContribution, SpawnID);
	if (!bInitialized)
	{
		Powder->Destroy();
		return false;
	}
	Powder->FinishSpawning(SpawnTransform);
	if (!IsValid(Powder) || Powder->GetOwnerResource() != this
		|| Powder->GetRuntimeToken() != Runtime)
	{
		if (IsValid(Powder))
		{
			Powder->Destroy();
		}
		return false;
	}

	OwnedPowders.AddUnique(Powder);
	SpawnedPowderHitIDs.Add(HitID);
	FWeaponCombatTelemetryEvent Event;
	Event.EventName = TEXT("PowderSpawn");
	Event.SourceAttackID = SourceAttackID;
	Event.HitID = HitID;
	Event.PowderID = Powder->GetPowderID();
	Event.TargetActor = IsValid(Hit.GetActor()) ? Hit.GetActor()->GetFName() : NAME_None;
	Event.Hitzone = Hit.GetComponent() ? Hit.GetComponent()->GetFName() : NAME_None;
	Event.ImpactPoint = Hit.ImpactPoint;
	Event.SpawnPolicy = PowderSpawnPolicyName(SpawnPolicy);
	Event.ExpireAt = Powder->GetExpireWorldTime();
	Event.LifetimeRevision = Powder->GetLifetimeRevision();
	Event.Damage = DamageContribution;
	RecordCombatTelemetryEvent(MoveTemp(Event));
	return true;
}

void URes_InsectGlaive::RemovePowderFromReservations(AIGPowderActor* Powder)
{
	if (!Powder)
	{
		return;
	}
	for (TPair<uint64, FReservedPowderSet>& Pair : PowderReservations)
	{
		Pair.Value.Members.RemoveAll([Powder](const FReservedPowderMember& Member)
		{
			return !Member.Actor.IsValid() || Member.Actor.Get() == Powder;
		});
	}
}

void URes_InsectGlaive::HandlePowderLifetimeExpired(
	AIGPowderActor* Powder, const int32 ExpectedLifetimeRevision)
{
	UWorld* World = GetWorld();
	if (!Powder || bRuntimeShuttingDown || !World
		|| Powder->GetOwnerResource() != this
		|| Powder->GetRuntimeToken() != RuntimeContext.RuntimeToken
		|| !IsRuntimeRequestCurrent(Powder->GetRuntimeToken())
		|| Powder->GetLifetimeRevision() != ExpectedLifetimeRevision
		|| Powder->GetPowderState() == EIGPowderState::Consumed)
	{
		return;
	}
	if (World->GetTimeSeconds() + UE_SMALL_NUMBER < Powder->GetExpireWorldTime())
	{
		Powder->ScheduleLifetimeTimer();
		return;
	}

	// Claim the terminal outcome and unregister before damage/callbacks. Destroy/EndPlay
	// can therefore never race a second expiry, distance removal, or gather commit.
	const FGuid ExplosionID = FGuid::NewGuid();
	OwnedPowders.RemoveAll([Powder](const TWeakObjectPtr<AIGPowderActor>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == Powder;
	});
	RemovePowderFromReservations(Powder);
	Powder->MarkTerminal(EIGPowderTerminalReason::NaturalExplosion);
	FWeaponCombatTelemetryEvent Event;
	Event.EventName = TEXT("PowderNaturalDetonate");
	Event.ExplosionID = ExplosionID;
	Event.PowderID = Powder->GetPowderID();
	Event.ImpactPoint = Powder->GetActorLocation();
	Event.Damage = Powder->GetDamageContribution();
	Event.ExpireAt = Powder->GetExpireWorldTime();
	Event.LifetimeRevision = Powder->GetLifetimeRevision();
	RecordCombatTelemetryEvent(MoveTemp(Event));
	ApplyNaturalPowderExplosion(Powder, ExplosionID);
	Powder->Destroy();
}

void URes_InsectGlaive::TickPowderMaintenance()
{
	if (bRuntimeShuttingDown || !GetWorld())
	{
		return;
	}
	TickPowderVortexPresentation();
	if (OwnedPowders.IsEmpty())
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now < NextPowderMaintenanceWorldTime)
	{
		return;
	}
	NextPowderMaintenanceWorldTime = Now + PowderMaintenanceIntervalSeconds;

	const TArray<TWeakObjectPtr<AIGPowderActor>> PowderSnapshot = OwnedPowders;
	AActor* PlayerActor = RuntimeContext.Character.Get();
	const float DespawnDistance = CombatConfig
		? CombatConfig->PowderDespawnDistance : 0.0f;
	const bool bDistanceRemovalEnabled = FMath::IsFinite(DespawnDistance)
		&& DespawnDistance > 0.0f && IsValid(PlayerActor);
	const double DespawnDistanceSquared = bDistanceRemovalEnabled
		? FMath::Square(static_cast<double>(DespawnDistance)) : 0.0;

	for (const TWeakObjectPtr<AIGPowderActor>& WeakPowder : PowderSnapshot)
	{
		AIGPowderActor* Powder = WeakPowder.Get();
		if (!Powder)
		{
			OwnedPowders.Remove(WeakPowder);
			continue;
		}
		if (Powder->GetRuntimeToken() != RuntimeContext.RuntimeToken
			|| Powder->GetOwnerResource() != this)
		{
			RemovePowderFromReservations(Powder);
			OwnedPowders.Remove(Powder);
			Powder->MarkTerminal(EIGPowderTerminalReason::RuntimeShutdown);
			Powder->Destroy();
			continue;
		}
		if (Powder->GetExpireWorldTime() <= Now)
		{
			HandlePowderLifetimeExpired(Powder, Powder->GetLifetimeRevision());
			continue;
		}
		if (bDistanceRemovalEnabled
			&& FVector::DistSquared(PlayerActor->GetActorLocation(),
				Powder->GetActorLocation()) > DespawnDistanceSquared)
		{
			FWeaponCombatTelemetryEvent Event;
			Event.EventName = TEXT("PowderDistanceCulled");
			Event.PowderID = Powder->GetPowderID();
			Event.ImpactPoint = Powder->GetActorLocation();
			Event.ExpireAt = Powder->GetExpireWorldTime();
			Event.LifetimeRevision = Powder->GetLifetimeRevision();
			Event.Reason = TEXT("BeyondDespawnDistance");
			RecordCombatTelemetryEvent(MoveTemp(Event));
			RemovePowderFromReservations(Powder);
			OwnedPowders.Remove(Powder);
			Powder->MarkTerminal(EIGPowderTerminalReason::DistanceDespawn);
			Powder->Destroy();
		}
	}
}

void URes_InsectGlaive::ApplyNaturalPowderExplosion(
	AIGPowderActor* Powder, const FGuid& ExplosionID)
{
	UWorld* World = GetWorld();
	UAbilitySystemComponent* SourceASC = GetPlayerASC();
	if (!Powder || !World || !SourceASC || !CombatConfig || !ExplosionID.IsValid()
		|| !FMath::IsFinite(CombatConfig->PowderNaturalExplosionRadius)
		|| CombatConfig->PowderNaturalExplosionRadius <= 0.0f)
	{
		return;
	}

	struct FPowderTargetHit
	{
		AActor* Actor = nullptr;
		UMHGZMonsterHitzoneComponent* Hitzone = nullptr;
		FHitResult Hit;
		double DistanceSquared = TNumericLimits<double>::Max();
	};

	TArray<FOverlapResult> Overlaps;
	FCollisionObjectQueryParams ObjectQuery;
	ObjectQuery.AddObjectTypesToQuery(ECC_GameTraceChannel3);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(IGPowderNaturalExplosion), false,
		GetOwner());
	const FVector Center = Powder->GetActorLocation();
	// Presentation belongs to the committed explosion, including an empty blast.
	// Never attach it to Powder: that actor is destroyed immediately afterwards.
	if (CombatConfig->PowderExplosionSound)
	{
		UGameplayStatics::SpawnSoundAtLocation(this,
			CombatConfig->PowderExplosionSound, Center);
	}
	const bool bFoundOverlap = World->OverlapMultiByObjectType(Overlaps, Center,
		FQuat::Identity, ObjectQuery,
		FCollisionShape::MakeSphere(CombatConfig->PowderNaturalExplosionRadius), QueryParams);
	if (!bFoundOverlap)
	{
		return;
	}

	TMap<AActor*, FPowderTargetHit> BestHitByActor;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		UMHGZMonsterHitzoneComponent* Hitzone =
			UMHGZMonsterHitzoneComponent::Resolve(Overlap.GetComponent());
		AActor* TargetActor = Hitzone ? Hitzone->GetOwner() : nullptr;
		if (!IsValid(Hitzone) || !IsValid(TargetActor) || TargetActor == GetOwner()
			|| !UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(TargetActor))
		{
			continue;
		}

		FVector ClosestPoint = Hitzone->GetComponentLocation();
		const float ClosestDistance = Hitzone->GetClosestPointOnQueryCollision(Center,
			ClosestPoint);
		if (ClosestDistance < 0.0f || ClosestPoint.ContainsNaN())
		{
			ClosestPoint = Hitzone->GetComponentLocation();
		}
		const double DistanceSquared = FVector::DistSquared(Center, ClosestPoint);
		FPowderTargetHit* Existing = BestHitByActor.Find(TargetActor);
		if (Existing && (Existing->DistanceSquared < DistanceSquared
			|| (FMath::IsNearlyEqual(Existing->DistanceSquared, DistanceSquared)
				&& Existing->Hitzone->GetName().Compare(Hitzone->GetName(),
					ESearchCase::CaseSensitive) <= 0)))
		{
			continue;
		}

		FPowderTargetHit Candidate;
		Candidate.Actor = TargetActor;
		Candidate.Hitzone = Hitzone;
		Candidate.DistanceSquared = DistanceSquared;
		Candidate.Hit = FHitResult(TargetActor, Hitzone->GetQueryCollisionComponent(), ClosestPoint,
			(Center - ClosestPoint).GetSafeNormal());
		Candidate.Hit.bBlockingHit = true;
		Candidate.Hit.TraceStart = Center;
		Candidate.Hit.TraceEnd = ClosestPoint;
		Candidate.Hit.Location = ClosestPoint;
		Candidate.Hit.ImpactPoint = ClosestPoint;
		Candidate.Hit.Normal = (Center - ClosestPoint).GetSafeNormal();
		Candidate.Hit.ImpactNormal = Candidate.Hit.Normal;
		BestHitByActor.Add(TargetActor, MoveTemp(Candidate));
	}

	TArray<FPowderTargetHit> Targets;
	BestHitByActor.GenerateValueArray(Targets);
	Targets.Sort([](const FPowderTargetHit& A, const FPowderTargetHit& B)
	{
		return GetNameSafe(A.Actor) < GetNameSafe(B.Actor);
	});

	static const FGameplayTag AttackPowerTag = FGameplayTag::RequestGameplayTag(
		TEXT("Damage.AttackPower"));
	static const FGameplayTag MotionValueTag = FGameplayTag::RequestGameplayTag(
		TEXT("Damage.MotionValue"));
	static const FGameplayTag BaseStaggerTag = FGameplayTag::RequestGameplayTag(
		TEXT("Damage.BaseStagger"));
	static const FGameplayTag CritOverrideTag = FGameplayTag::RequestGameplayTag(
		TEXT("Damage.CritOverride"));
	static const FGameplayTag DanceMultiplierTag = FGameplayTag::RequestGameplayTag(
		TEXT("Damage.DanceMultiplier"));
	const float AttackPowerSnapshot = Powder->GetKinsectAttackPowerSnapshot();
	const float MotionValueSnapshot = Powder->GetMotionValueSnapshot();
	for (const FPowderTargetHit& Target : Targets)
	{
		UAbilitySystemComponent* TargetASC =
			UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target.Actor);
		if (!TargetASC)
		{
			continue;
		}

		FGameplayEffectContextHandle ContextHandle = SourceASC->MakeEffectContext();
		FMHGZGameplayEffectContext* Context =
			FMHGZGameplayEffectContext::ExtractEffectContext(ContextHandle);
		if (!Context)
		{
			continue;
		}
		Context->AddHitResult(Target.Hit, true);
		Context->AttackInstanceID = ExplosionID;
		Context->DamageSourceType = EMHGZDamageSourceType::Powder;
		Context->bUseHitzoneDefense = false;
		Context->HitStopDuration = 0.0f;
		Context->HitzoneTag = Target.Hitzone->HitzoneTag;
		if (AActor* Source = RuntimeContext.Character.Get())
		{
			Context->AddInstigator(Source, Source);
		}

		FGameplayEffectSpecHandle Spec = SourceASC->MakeOutgoingSpec(
			UMHGZDamageGameplayEffect::StaticClass(), 1.0f, ContextHandle);
		if (!Spec.IsValid())
		{
			continue;
		}
		Spec.Data->SetSetByCallerMagnitude(AttackPowerTag, AttackPowerSnapshot);
		Spec.Data->SetSetByCallerMagnitude(MotionValueTag, MotionValueSnapshot);
		Spec.Data->SetSetByCallerMagnitude(BaseStaggerTag, 0.0f);
		Spec.Data->SetSetByCallerMagnitude(CritOverrideTag, 0.0f);
		Spec.Data->SetSetByCallerMagnitude(DanceMultiplierTag, 1.0f);
	SourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data, TargetASC);
	}
}

bool URes_InsectGlaive::StartPowderVortex(const FWeaponActionToken& Action,
	const FWeaponAimSnapshot& AimSnapshot, const FIGPowderVortexSettings& Settings)
{
	UMHGZWeaponRuntimeHostComponent* Host = RuntimeContext.RuntimeToken.Host.Get();
	if (bRuntimeShuttingDown || ActivePowderVortexCommandSerial != 0
		|| !Action.IsValid() || Action.RuntimeToken != RuntimeContext.RuntimeToken
		|| !IsRuntimeRequestCurrent(Action.RuntimeToken)
		|| !Host || !Host->IsActionRegistered(Action)
		|| !CombatConfig || !KinsectActor || !KinsectData
		|| KinsectActionAttachmentToken.IsValid()
		|| AimSnapshot.Context != EWeaponAimSnapshotContext::Kinsect
		|| !IsFiniteVector(AimSnapshot.TargetPoint)
		|| !Settings.IsValid()
		|| !FMath::IsFinite(KinsectData->FlightSpeed)
		|| KinsectData->FlightSpeed <= 0.0f
		|| !FMath::IsFinite(KinsectData->MaxFlightRange)
		|| KinsectData->MaxFlightRange <= 0.0f
		|| !FMath::IsFinite(CombatConfig->KinsectArrivalRadius)
		|| CombatConfig->KinsectArrivalRadius <= 0.0f)
	{
		return false;
	}

	FVector TargetPoint = AimSnapshot.TargetPoint;
	const FHitResult* AimHit = AimSnapshot.bHasHitResult
		? &AimSnapshot.HitResult : nullptr;
	if (AimHit && AimHit->bBlockingHit && IsFiniteVector(AimHit->ImpactPoint))
	{
		TargetPoint = AimHit->ImpactPoint;
	}
	else if (AimSnapshot.bHasBlockingHit && AimSnapshot.BlockingHit.bBlockingHit
		&& IsFiniteVector(AimSnapshot.BlockingHit.ImpactPoint))
	{
		TargetPoint = AimSnapshot.BlockingHit.ImpactPoint;
	}
	const FVector Start = KinsectActor->GetActorLocation();
	const float TargetDistance = FVector::Distance(Start, TargetPoint);
	if (!IsFiniteVector(TargetPoint) || TargetDistance <= KINDA_SMALL_NUMBER
		|| TargetDistance > KinsectData->MaxFlightRange + KINDA_SMALL_NUMBER)
	{
		return false;
	}

	FKinsectFlightRequest Request;
	Request.RuntimeToken = Action.RuntimeToken;
	Request.TrajectoryMode = EKinsectTrajectoryMode::ToPoint;
	Request.TargetPointSnapshot = TargetPoint;
	Request.MaxDistance = KinsectData->MaxFlightRange
		+ CombatConfig->KinsectArrivalRadius + 1.0f;
	Request.FlightSpeed = KinsectData->FlightSpeed;
	Request.ArrivalRadius = CombatConfig->KinsectArrivalRadius;
	Request.DamageMode = EKinsectDamageMode::None;
	Request.ExtractMode = EKinsectExtractMode::None;
	Request.PostFlightPolicy = EKinsectPostFlightPolicy::Hover;
	Request.FlightInstanceID = FGuid::NewGuid();
	Request.CommandSource = EKinsectCommandSource::PowderVortex;
	Request.ActionActivationSequenceID = Action.ActivationSequenceID;

	const FHitResult* FollowHit = AimHit;
	if (FollowHit && FollowHit->bBlockingHit)
	{
		if (UPrimitiveComponent* Component = FollowHit->GetComponent())
		{
			Request.TargetComponent = Component;
			if (USkeletalMeshComponent* SkeletalMesh =
				Cast<USkeletalMeshComponent>(Component);
				SkeletalMesh && !FollowHit->BoneName.IsNone())
			{
				Request.TargetBoneName = FollowHit->BoneName;
				Request.TargetLocalPoint = SkeletalMesh->GetSocketTransform(
					Request.TargetBoneName, RTS_World).InverseTransformPosition(TargetPoint);
			}
			else
			{
				Request.TargetLocalPoint = Component->GetComponentTransform()
					.InverseTransformPosition(TargetPoint);
			}
			Request.bFollowTargetComponent = true;
		}
	}
	// Any blocking point captured by the activation-time aim is a legitimate
	// A floor/wall remains an aim point, not a physical Kinsect hit. Arrival is
	// resolved only from the saved/moving point, with no terrain-stop exception.
	Request.bAllowWorldHitAtTarget = false; // legacy reflected field

	const uint64 CommandSerial = AdvanceKinsectCommand(
		EKinsectCommandSource::PowderVortex);
	Request.CommandSerial = CommandSerial;
	if (KinsectActor->GetState() == EKinsectState::Flying
		|| KinsectActor->GetState() == EKinsectState::Returning)
	{
		KinsectActor->Interrupt();
	}
	if (!KinsectActor->BeginFlight(Request))
	{
		if (KinsectCommandSerial == CommandSerial)
		{
			AdvanceKinsectCommand(EKinsectCommandSource::None);
		}
		return false;
	}

	ActivePowderVortexCommandSerial = CommandSerial;
	ActivePowderVortexReservationID = 0;
	ActivePowderVortexSettings = Settings;
	ActivePowderVortexAttackPower = GetModifiedKinsectAttackPower();
	SetKinsectActiveTag(true);
	return true;
}

bool URes_InsectGlaive::BeginPowderVortexGathering(
	const FKinsectFlightRequest& Request, const FVector& Center)
{
	if (!GetWorld() || !CombatConfig || !IsFiniteVector(Center)
		|| Request.CommandSerial != ActivePowderVortexCommandSerial
		|| Request.ActionActivationSequenceID == 0
		|| !IsKinsectCommandCurrent(Request)
		|| ActivePowderVortexReservationID != 0)
	{
		return false;
	}

	uint64 ReservationID = NextPowderReservationID++;
	if (ReservationID == 0)
	{
		ReservationID = NextPowderReservationID++;
	}
	if (ReservationID == 0)
	{
		return false;
	}

	FIGPowderReservationToken Token;
	Token.RuntimeToken = Request.RuntimeToken;
	Token.ActivationSequenceID = Request.ActionActivationSequenceID;
	Token.ReservationID = ReservationID;
	Token.CommandSerial = Request.CommandSerial;

	const double Now = GetWorld()->GetTimeSeconds();
	const double RadiusSquared = FMath::Square(
		static_cast<double>(ActivePowderVortexSettings.GatherRadius));
	TArray<AIGPowderActor*> Candidates;
	// The attached mark is not a powder member and is never moved by the gather
	// presentation. A mark in the attraction radius is consumed directly when
	// gathering starts below; a separate MarkedContact powder can coexist there.
	for (const TWeakObjectPtr<AIGPowderActor>& WeakPowder : OwnedPowders)
	{
		AIGPowderActor* Powder = WeakPowder.Get();
		if (Powder && Powder->IsAvailableFor(this, Request.RuntimeToken, Now)
			&& FVector::DistSquared(Center, Powder->GetActorLocation()) <= RadiusSquared)
		{
			Candidates.Add(Powder);
		}
	}
	Candidates.Sort([](const AIGPowderActor& A, const AIGPowderActor& B)
	{
		return A.GetSpawnSequenceID() < B.GetSpawnSequenceID();
	});

	FReservedPowderSet Pending;
	Pending.Token = Token;
	Pending.Center = Center;
	Pending.CreatedWorldTime = Now;
	Pending.GatherDeadlineWorldTime = Now + ActivePowderVortexSettings.GatherDelay;
	Pending.bPowderVortexTransaction = true;
	Pending.bGathering = true;
	for (AIGPowderActor* Powder : Candidates)
	{
		if (!IsValid(Powder) || !Powder->TryReserve(Token))
		{
			for (const FReservedPowderMember& Member : Pending.Members)
			{
				if (AIGPowderActor* Reserved = Member.Actor.Get())
				{
					Reserved->ReleaseReservationAtCurrentLocation(Token, Now);
				}
			}
			return false;
		}
		FReservedPowderMember& Member = Pending.Members.AddDefaulted_GetRef();
		Member.Actor = Powder;
		Member.OriginalTransform = Powder->GetActorTransform();
		FWeaponCombatTelemetryEvent ReserveEvent;
		ReserveEvent.EventName = TEXT("PowderReserved");
		ReserveEvent.FlightID = Request.FlightInstanceID;
		ReserveEvent.CommandSerial = Request.CommandSerial;
		ReserveEvent.ActivationSequenceID = Request.ActionActivationSequenceID;
		ReserveEvent.PowderID = Powder->GetPowderID();
		ReserveEvent.ReservationID = ReservationID;
		ReserveEvent.ImpactPoint = Center;
		RecordCombatTelemetryEvent(MoveTemp(ReserveEvent));
		if (!Powder->RefreshLifetime(Token, Now, PowderWorldLifetimeSeconds))
		{
			for (const FReservedPowderMember& ReservedMember : Pending.Members)
			{
				if (AIGPowderActor* Reserved = ReservedMember.Actor.Get())
				{
					Reserved->ReleaseReservationAtCurrentLocation(Token, Now);
				}
			}
			return false;
		}
		FWeaponCombatTelemetryEvent RefreshEvent;
		RefreshEvent.EventName = TEXT("PowderLifetimeRefreshed");
		RefreshEvent.FlightID = Request.FlightInstanceID;
		RefreshEvent.CommandSerial = Request.CommandSerial;
		RefreshEvent.ActivationSequenceID = Request.ActionActivationSequenceID;
		RefreshEvent.PowderID = Powder->GetPowderID();
		RefreshEvent.ReservationID = ReservationID;
		RefreshEvent.ExpireAt = Powder->GetExpireWorldTime();
		RefreshEvent.LifetimeRevision = Powder->GetLifetimeRevision();
		RecordCombatTelemetryEvent(MoveTemp(RefreshEvent));
	}

	const int32 ReservedMemberCount = Pending.Members.Num();
	PowderReservations.Add(ReservationID, MoveTemp(Pending));
	ActivePowderVortexReservationID = ReservationID;
	FWeaponCombatTelemetryEvent ArrivedEvent;
	ArrivedEvent.EventName = TEXT("VortexArrived");
	ArrivedEvent.FlightID = Request.FlightInstanceID;
	ArrivedEvent.CommandSerial = Request.CommandSerial;
	ArrivedEvent.ActivationSequenceID = Request.ActionActivationSequenceID;
	ArrivedEvent.ReservationID = ReservationID;
	ArrivedEvent.ImpactPoint = Center;
	ArrivedEvent.MemberCount = ReservedMemberCount;
	ArrivedEvent.Reason = TEXT("TargetReached");
	RecordCombatTelemetryEvent(MoveTemp(ArrivedEvent));
	if (!UpdatePowderGathering(Token, 0.0f))
	{
		CancelPowderVortexTransaction(Request.CommandSerial, true);
		return false;
	}
	FVector MarkLocation = FVector::ZeroVector;
	FVector MarkNormal = FVector::UpVector;
	if (ActiveMarkID.IsValid()
		&& GetKinsectMarkSurfaceFrame(MarkSerial, MarkLocation, MarkNormal)
		&& FVector::DistSquared(Center, MarkLocation) <= RadiusSquared)
	{
		ClearKinsectMark(EIGMarkClearReason::PowderVortex);
	}
	GetWorld()->GetTimerManager().ClearTimer(PowderVortexCommitTimer);
	FTimerDelegate CommitDelegate = FTimerDelegate::CreateWeakLambda(this,
		[this, ExpectedCommandSerial = Request.CommandSerial]()
		{
			CommitPowderVortexTransaction(ExpectedCommandSerial);
		});
	GetWorld()->GetTimerManager().SetTimer(PowderVortexCommitTimer,
		CommitDelegate, ActivePowderVortexSettings.GatherDelay, false);
	StartPowderVortexGatherSounds(Center);
	return true;
}

void URes_InsectGlaive::StartPowderVortexGatherSounds(const FVector& Center)
{
	StopPowderVortexGatherSounds();
	if (!CombatConfig || !GetWorld() || !IsFiniteVector(Center))
	{
		return;
	}
	for (USoundBase* Sound : ActivePowderVortexSettings.GatherSounds)
	{
		if (Sound)
		{
			if (UAudioComponent* Audio = UGameplayStatics::SpawnSoundAtLocation(this, Sound, Center))
			{
				PowderVortexGatherAudio.Add(Audio);
			}
		}
	}
}

void URes_InsectGlaive::StopPowderVortexGatherSounds()
{
	for (UAudioComponent* Audio : PowderVortexGatherAudio)
	{
		if (IsValid(Audio))
		{
			Audio->Stop();
		}
	}
	PowderVortexGatherAudio.Reset();
}

void URes_InsectGlaive::TickPowderVortexPresentation()
{
	if (ActivePowderVortexReservationID == 0 || !GetWorld())
	{
		return;
	}
	FReservedPowderSet* Reservation = PowderReservations.Find(
		ActivePowderVortexReservationID);
	if (!Reservation || !Reservation->bPowderVortexTransaction
		|| Reservation->Token.CommandSerial != ActivePowderVortexCommandSerial
		|| !IsKinsectCommandCurrent(Reservation->Token.CommandSerial,
			EKinsectCommandSource::PowderVortex, 0, Reservation->Token.RuntimeToken))
	{
		CancelPowderVortexTransaction(ActivePowderVortexCommandSerial, true);
		return;
	}
	const double Duration = FMath::Max(
		static_cast<double>(KINDA_SMALL_NUMBER),
		Reservation->GatherDeadlineWorldTime - Reservation->CreatedWorldTime);
	const float Progress = static_cast<float>(FMath::Clamp(
		(GetWorld()->GetTimeSeconds() - Reservation->CreatedWorldTime) / Duration,
		0.0, 1.0));
	if (!UpdatePowderGathering(Reservation->Token, Progress))
	{
		CancelPowderVortexTransaction(ActivePowderVortexCommandSerial, true);
	}
}

void URes_InsectGlaive::CommitPowderVortexTransaction(
	const uint64 ExpectedCommandSerial)
{
	if (bRuntimeShuttingDown || !GetWorld() || !CombatConfig
		|| ExpectedCommandSerial == 0
		|| ExpectedCommandSerial != ActivePowderVortexCommandSerial
		|| !IsKinsectCommandCurrent(ExpectedCommandSerial,
			EKinsectCommandSource::PowderVortex, 0, RuntimeContext.RuntimeToken))
	{
		CancelPowderVortexTransaction(ExpectedCommandSerial, true);
		return;
	}
	FReservedPowderSet* Found = PowderReservations.Find(
		ActivePowderVortexReservationID);
	if (!Found || !Found->bPowderVortexTransaction
		|| Found->Token.CommandSerial != ExpectedCommandSerial)
	{
		CancelPowderVortexTransaction(ExpectedCommandSerial, true);
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now + UE_SMALL_NUMBER < Found->GatherDeadlineWorldTime)
	{
		const float Remaining = static_cast<float>(
			Found->GatherDeadlineWorldTime - Now);
		FTimerDelegate CommitDelegate = FTimerDelegate::CreateWeakLambda(this,
			[this, ExpectedCommandSerial]()
			{
				CommitPowderVortexTransaction(ExpectedCommandSerial);
			});
		GetWorld()->GetTimerManager().SetTimer(PowderVortexCommitTimer,
			CommitDelegate, FMath::Max(Remaining, KINDA_SMALL_NUMBER), false);
		return;
	}
	GetWorld()->GetTimerManager().ClearTimer(PowderVortexCommitTimer);

	const FReservedPowderSet Reservation = *Found;
	StopPowderVortexGatherSounds();
	TArray<AIGPowderActor*> LiveMembers;
	double Stage2Damage = 0.0;
	for (const FReservedPowderMember& Member : Reservation.Members)
	{
		AIGPowderActor* Powder = Member.Actor.Get();
		if (Powder && Powder->CanCommitReservation(Reservation.Token, Now))
		{
			LiveMembers.Add(Powder);
			// Sum the damage of individually exploding powders, never previously
			// rounded float ratios. Every member keeps its own generation-time AP/MV.
			Stage2Damage += MHGZDamageMath::PowderDamage(
				Powder->GetKinsectAttackPowerSnapshot(), Powder->GetMotionValueSnapshot());
		}
		else if (Powder)
		{
			Powder->ReleaseReservationAtCurrentLocation(Reservation.Token, Now);
		}
	}

	PowderReservations.Remove(Reservation.Token.ReservationID);
	ActivePowderVortexReservationID = 0;
	FIGPowderExplosionSnapshot Explosion;
	Explosion.Center = Reservation.Center;
	Explosion.ConsumedCount = LiveMembers.Num();
	Explosion.Radius = ActivePowderVortexSettings.ExplosionRadius;
	Explosion.BaseStagger = 0.0f;
	if (!LiveMembers.IsEmpty())
	{
		const int32 Count = LiveMembers.Num();
		Explosion.Stage1AttackPower = ActivePowderVortexAttackPower;
		Explosion.Stage1MotionValue = Count < 5 ? ActivePowderVortexSettings.MotionValueAt1
			: (Count < 10 ? ActivePowderVortexSettings.MotionValueAt5 : ActivePowderVortexSettings.MotionValueAt10);
		Explosion.Stage1Damage = MHGZDamageMath::PowderDamage(Explosion.Stage1AttackPower, Explosion.Stage1MotionValue);
		Explosion.Stage2Damage = static_cast<float>(Stage2Damage);
		Explosion.Stage1AttackID = FGuid::NewGuid();
		Explosion.Stage2AttackID = FGuid::NewGuid();
		Explosion.ExplosionID = Explosion.Stage1AttackID;
		Explosion.TotalDamage = Explosion.Stage1Damage + Explosion.Stage2Damage;
	}

	for (AIGPowderActor* Powder : LiveMembers)
	{
		Powder->MarkConsumed(Reservation.Token);
		FWeaponCombatTelemetryEvent ConsumeEvent;
		ConsumeEvent.EventName = TEXT("PowderConsumed");
		ConsumeEvent.CommandSerial = ExpectedCommandSerial;
		ConsumeEvent.ActivationSequenceID = Reservation.Token.ActivationSequenceID;
		ConsumeEvent.PowderID = Powder->GetPowderID();
		ConsumeEvent.ReservationID = Reservation.Token.ReservationID;
		ConsumeEvent.ExplosionID = Explosion.ExplosionID;
		ConsumeEvent.StageAttackID = Explosion.Stage2AttackID;
		ConsumeEvent.ImpactPoint = Explosion.Center;
		ConsumeEvent.MemberCount = LiveMembers.Num();
		ConsumeEvent.Damage = Powder->GetDamageContribution();
		RecordCombatTelemetryEvent(MoveTemp(ConsumeEvent));
		Powder->Destroy();
	}
	FWeaponCombatTelemetryEvent CommitEvent;
	CommitEvent.EventName = TEXT("VortexCommitted");
	CommitEvent.CommandSerial = ExpectedCommandSerial;
	CommitEvent.ActivationSequenceID = Reservation.Token.ActivationSequenceID;
	CommitEvent.ReservationID = Reservation.Token.ReservationID;
	CommitEvent.ExplosionID = Explosion.ExplosionID;
	CommitEvent.StageAttackID = Explosion.Stage1AttackID;
	CommitEvent.ImpactPoint = Explosion.Center;
	CommitEvent.MemberCount = Explosion.ConsumedCount;
	CommitEvent.Damage = Explosion.TotalDamage;
	RecordCombatTelemetryEvent(MoveTemp(CommitEvent));
	const FIGPowderVortexSettings ExplosionSettings = ActivePowderVortexSettings;
	ApplyPowderVortexExplosion(Explosion, ExplosionSettings);

	if (ActivePowderVortexCommandSerial == ExpectedCommandSerial)
	{
		ActivePowderVortexCommandSerial = 0;
		ActivePowderVortexSettings = FIGPowderVortexSettings();
		ActivePowderVortexAttackPower = 0.f;
	}
	if (KinsectCommandSerial == ExpectedCommandSerial
		&& KinsectCommandSource == EKinsectCommandSource::PowderVortex)
	{
		AdvanceKinsectCommand(EKinsectCommandSource::None);
		EarliestAutoMarkUpdateFrame = GFrameCounter + 1;
	}
}

void URes_InsectGlaive::CancelPowderVortexTransaction(
	const uint64 ExpectedCommandSerial, const bool bReleaseCommand,
	const FName CancellationReason)
{
	if (ExpectedCommandSerial != 0 && ActivePowderVortexCommandSerial != 0
		&& ExpectedCommandSerial != ActivePowderVortexCommandSerial)
	{
		return;
	}
	const uint64 CancelledSerial = ActivePowderVortexCommandSerial;
	StopPowderVortexGatherSounds();
	const uint64 CancelledReservationID = ActivePowderVortexReservationID;
	if (CancelledSerial != 0 || CancelledReservationID != 0)
	{
		FWeaponCombatTelemetryEvent Event;
		Event.EventName = TEXT("VortexCancelled");
		Event.CommandSerial = CancelledSerial;
		Event.ReservationID = CancelledReservationID;
		Event.Reason = !CancellationReason.IsNone() ? CancellationReason
			: (bReleaseCommand ? FName(TEXT("InputOrCommandSuperseded"))
				: FName(TEXT("RuntimeTransition")));
		if (const FReservedPowderSet* Reservation = PowderReservations.Find(
			CancelledReservationID))
		{
			Event.ActivationSequenceID = Reservation->Token.ActivationSequenceID;
			Event.MemberCount = Reservation->Members.Num();
			Event.ImpactPoint = Reservation->Center;
		}
		RecordCombatTelemetryEvent(MoveTemp(Event));
	}
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(PowderVortexCommitTimer);
	}
	if (ActivePowderVortexReservationID != 0)
	{
		if (FReservedPowderSet* Found = PowderReservations.Find(
			ActivePowderVortexReservationID))
		{
			const FReservedPowderSet Reservation = *Found;
			PowderReservations.Remove(Reservation.Token.ReservationID);
			const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
			for (const FReservedPowderMember& Member : Reservation.Members)
			{
				if (AIGPowderActor* Powder = Member.Actor.Get())
				{
					FWeaponCombatTelemetryEvent ReleaseEvent;
					ReleaseEvent.EventName = TEXT("PowderReleased");
					ReleaseEvent.CommandSerial = Reservation.Token.CommandSerial;
					ReleaseEvent.ActivationSequenceID = Reservation.Token.ActivationSequenceID;
					ReleaseEvent.PowderID = Powder->GetPowderID();
					ReleaseEvent.ReservationID = Reservation.Token.ReservationID;
					ReleaseEvent.LifetimeRevision = Powder->GetLifetimeRevision();
					RecordCombatTelemetryEvent(MoveTemp(ReleaseEvent));
					Powder->ReleaseReservationAtCurrentLocation(Reservation.Token, Now);
				}
			}
		}
	}
	ActivePowderVortexReservationID = 0;
	ActivePowderVortexCommandSerial = 0;
	ActivePowderVortexSettings = FIGPowderVortexSettings();
	ActivePowderVortexAttackPower = 0.f;
	if (bReleaseCommand && CancelledSerial != 0
		&& KinsectCommandSerial == CancelledSerial
		&& KinsectCommandSource == EKinsectCommandSource::PowderVortex)
	{
		AdvanceKinsectCommand(EKinsectCommandSource::None);
		EarliestAutoMarkUpdateFrame = GFrameCounter + 1;
	}
}

void URes_InsectGlaive::ApplyPowderVortexExplosion(
	const FIGPowderExplosionSnapshot& Explosion, const FIGPowderVortexSettings& Settings)
{
	if (!IsFiniteVector(Explosion.Center))
	{
		return;
	}
	UAbilitySystemComponent* SourceASC = GetPlayerASC();
	AActor* SourceActor = GetOwner();
	if (!SourceASC || !SourceActor || !CombatConfig)
	{
		return;
	}
	FGameplayCueParameters CueParameters;
	CueParameters.Location = Explosion.Center;
	CueParameters.RawMagnitude = Explosion.TotalDamage;
	CueParameters.Instigator = SourceActor;
	CueParameters.EffectCauser = SourceActor;
	CueParameters.EffectContext = SourceASC->MakeEffectContext();
	SourceASC->ExecuteGameplayCue(Settings.ExplosionCueTag,
		CueParameters);
	if (Explosion.ConsumedCount <= 0)
	{
		return;
	}
	// A single spatial burst per transaction, not one per powder, target or stage.
	for (USoundBase* Sound : { CombatConfig->PowderExplosionSound.Get(),
		Settings.ExplosionSound.Get() })
	{
		if (Sound)
		{
			UGameplayStatics::SpawnSoundAtLocation(this, Sound, Explosion.Center);
		}
	}
	ApplyPowderVortexDamageStage(Explosion, Explosion.Stage1AttackID,
		Explosion.Stage1Damage, Settings.HitCueTag);
	ApplyPowderVortexDamageStage(Explosion, Explosion.Stage2AttackID,
		Explosion.Stage2Damage, Settings.HitCueTag);
}

void URes_InsectGlaive::ApplyPowderVortexDamageStage(
	const FIGPowderExplosionSnapshot& Explosion, const FGuid& StageAttackID,
	const float Damage, const FGameplayTag HitCueTag)
{
	if (!StageAttackID.IsValid() || !FMath::IsFinite(Damage) || Damage <= 0.0f
		|| !GetWorld() || !CombatConfig)
	{
		return;
	}
	UAbilitySystemComponent* SourceASC = GetPlayerASC();
	AActor* SourceActor = GetOwner();
	if (!SourceASC || !SourceActor)
	{
		return;
	}
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(IGPowderVortexStage), false,
		SourceActor);
	QueryParams.AddIgnoredActor(SourceActor);
	TArray<FOverlapResult> Overlaps;
	GetWorld()->OverlapMultiByChannel(Overlaps, Explosion.Center,
		FQuat::Identity, ECC_GameTraceChannel1,
		FCollisionShape::MakeSphere(Explosion.Radius), QueryParams);

	TMap<TWeakObjectPtr<AActor>, FHitResult> NearestHitByActor;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		UMHGZMonsterHitzoneComponent* Hitzone =
			UMHGZMonsterHitzoneComponent::Resolve(Overlap.GetComponent());
		AActor* Target = Hitzone ? Hitzone->GetOwner() : nullptr;
		if (!IsValid(Target) || !IsValid(Hitzone) || !Hitzone->IsRegistered()
			|| Target == SourceActor)
		{
			continue;
		}

		FVector ClosestPoint = Hitzone->GetComponentLocation();
		const float ClosestDistance = Hitzone->GetClosestPointOnQueryCollision(
			Explosion.Center, ClosestPoint);
		if (ClosestDistance < 0.0f || !IsFiniteVector(ClosestPoint))
		{
			ClosestPoint = Hitzone->GetComponentLocation();
		}
		const double DistanceSquared = FVector::DistSquared(
			Explosion.Center, ClosestPoint);
		const TWeakObjectPtr<AActor> Key(Target);
		FHitResult* Existing = NearestHitByActor.Find(Key);
		if (!Existing || DistanceSquared < FVector::DistSquared(
			Explosion.Center, Existing->ImpactPoint))
		{
			FHitResult Hit(Target, Hitzone->GetQueryCollisionComponent(), ClosestPoint,
				(Explosion.Center - ClosestPoint).GetSafeNormal());
			Hit.bBlockingHit = true;
			Hit.TraceStart = Explosion.Center;
			Hit.TraceEnd = ClosestPoint;
			Hit.Location = ClosestPoint;
			Hit.ImpactPoint = ClosestPoint;
			Hit.ImpactNormal = Hit.Normal;
			NearestHitByActor.Add(Key, MoveTemp(Hit));
		}
	}

	TArray<FHitResult> UniqueHits;
	NearestHitByActor.GenerateValueArray(UniqueHits);
	for (const FHitResult& Hit : UniqueHits)
	{
		AActor* Target = Hit.GetActor();
		UAbilitySystemComponent* TargetASC = Target
			? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target) : nullptr;
		UMHGZMonsterHitzoneComponent* Hitzone =
			UMHGZMonsterHitzoneComponent::Resolve(Hit.GetComponent());
		if (!Hitzone || !IsDamageablePowderTarget(Target, TargetASC))
		{
			continue;
		}

		FGameplayEffectContextHandle ContextHandle = SourceASC->MakeEffectContext();
		FMHGZGameplayEffectContext* Context =
			FMHGZGameplayEffectContext::ExtractEffectContext(ContextHandle);
		if (!Context)
		{
			continue;
		}
		// The ASC is owned by PlayerState, while the Character is the physical causer.
		// Keep those identities separate so GAS can resolve the source ASC from context.
		Context->AddInstigator(SourceASC->GetOwnerActor(), SourceActor);
		Context->AddHitResult(Hit, true);
		Context->AttackInstanceID = StageAttackID;
		Context->SourceActionTag = FGameplayTag::RequestGameplayTag(
			TEXT("Input.Weapon.LTYB"));
		Context->HitzoneTag = Hitzone->HitzoneTag;
		Context->DamageSourceType = EMHGZDamageSourceType::Powder;
		Context->bUseHitzoneDefense = false;
		Context->HitCueTag = HitCueTag;

		FGameplayEffectSpecHandle Spec = SourceASC->MakeOutgoingSpec(
			UMHGZDamageGameplayEffect::StaticClass(), 1.0f, ContextHandle);
		if (!Spec.IsValid())
		{
			continue;
		}
		Spec.Data->SetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag(
			TEXT("Damage.AttackPower")), StageAttackID == Explosion.Stage1AttackID
				? Explosion.Stage1AttackPower : Damage);
		// Stage 1 retains its actual GA motion value and AP snapshot. Stage 2
		// adapts the sum of individually rounded powder damage with MV=100.
		Spec.Data->SetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag(
			TEXT("Damage.MotionValue")), StageAttackID == Explosion.Stage1AttackID
				? Explosion.Stage1MotionValue : 100.0f);
		Spec.Data->SetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag(
			TEXT("Damage.BaseStagger")), 0.0f);
		Spec.Data->SetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag(
			TEXT("Damage.CritOverride")), 0.0f);
		Spec.Data->SetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag(
			TEXT("Damage.DanceMultiplier")), 1.0f);
		if (Hitzone->HitzoneTag.IsValid())
		{
			Spec.Data->AddDynamicAssetTag(Hitzone->HitzoneTag);
		}
		Spec.Data->AddDynamicAssetTag(FGameplayTag::RequestGameplayTag(
			TEXT("GameplayCue.Hit.DamageNumber")));
		const FActiveGameplayEffectHandle AppliedHandle =
			SourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data, TargetASC);
		if (AppliedHandle.IsValid())
		{
			FWeaponCombatTelemetryEvent Event;
			Event.EventName = TEXT("StageDamageSubmitted");
			Event.ExplosionID = Explosion.ExplosionID;
			Event.StageIndex = StageAttackID == Explosion.Stage1AttackID ? 1 : 2;
			Event.StageAttackID = StageAttackID;
			Event.TargetActor = Target->GetFName();
			Event.Hitzone = Hitzone->GetFName();
			Event.ImpactPoint = Hit.ImpactPoint;
			Event.MemberCount = Explosion.ConsumedCount;
			Event.Damage = Damage;
			RecordCombatTelemetryEvent(MoveTemp(Event));
		}
	}
}

bool URes_InsectGlaive::CanGatherPowders(const FVector& Center, const float GatherRadius) const
{
	if (bRuntimeShuttingDown || !CombatConfig || !GetWorld()
		|| !RuntimeContext.RuntimeToken.IsValid()
		|| !IsRuntimeRequestCurrent(RuntimeContext.RuntimeToken)
		|| !FMath::IsFinite(Center.X) || !FMath::IsFinite(Center.Y)
		|| !FMath::IsFinite(Center.Z)
		|| !FMath::IsFinite(GatherRadius)
		|| GatherRadius <= 0.0f)
	{
		return false;
	}

	const double Now = GetWorld()->GetTimeSeconds();
	const double RadiusSquared = FMath::Square(
		static_cast<double>(GatherRadius));
	int32 AvailableCount = 0;
	for (const TWeakObjectPtr<AIGPowderActor>& WeakPowder : OwnedPowders)
	{
		const AIGPowderActor* Powder = WeakPowder.Get();
		if (Powder && Powder->IsAvailableFor(this, RuntimeContext.RuntimeToken, Now)
			&& FVector::DistSquared(Center, Powder->GetActorLocation()) <= RadiusSquared
			&& ++AvailableCount > 0)
		{
			return true;
		}
	}
	return AvailableCount > 0;
}

bool URes_InsectGlaive::TryReservePowders(const FWeaponActionToken& Action,
	const FVector& Center, FIGPowderReservationToken& OutToken, const float GatherRadius, const int32 MaxCount)
{
	OutToken = FIGPowderReservationToken();
	UMHGZWeaponRuntimeHostComponent* Host = RuntimeContext.RuntimeToken.Host.Get();
	if (!Action.IsValid() || !IsRuntimeRequestCurrent(Action.RuntimeToken)
		|| Action.RuntimeToken != RuntimeContext.RuntimeToken || !GetWorld()
		|| !Host || !Host->IsActionRegistered(Action)
		|| !CombatConfig || !FMath::IsFinite(Center.X)
		|| !FMath::IsFinite(Center.Y) || !FMath::IsFinite(Center.Z)
		|| MaxCount <= 0 || !CanGatherPowders(Center, GatherRadius))
	{
		return false;
	}
	for (const TPair<uint64, FReservedPowderSet>& Pair : PowderReservations)
	{
		if (Pair.Value.Token.RuntimeToken == Action.RuntimeToken
			&& Pair.Value.Token.ActivationSequenceID == Action.ActivationSequenceID)
		{
			return false; // repeated GatherBegin cannot acquire a second set
		}
	}

	struct FCandidate
	{
		AIGPowderActor* Actor = nullptr;
		double DistanceSquared = 0.0;
	};
	const double Now = GetWorld()->GetTimeSeconds();
	const double RadiusSquared = FMath::Square(
		static_cast<double>(GatherRadius));
	TArray<FCandidate> Candidates;
	for (const TWeakObjectPtr<AIGPowderActor>& WeakPowder : OwnedPowders)
	{
		AIGPowderActor* Powder = WeakPowder.Get();
		if (!Powder || !Powder->IsAvailableFor(this, Action.RuntimeToken, Now))
		{
			continue;
		}
		const double DistanceSquared = FVector::DistSquared(Center, Powder->GetActorLocation());
		if (DistanceSquared <= RadiusSquared)
		{
			Candidates.Add({ Powder, DistanceSquared });
		}
	}
	Candidates.Sort([](const FCandidate& A, const FCandidate& B)
	{
		if (A.DistanceSquared != B.DistanceSquared)
		{
			return A.DistanceSquared < B.DistanceSquared;
		}
		return A.Actor->GetSpawnSequenceID() < B.Actor->GetSpawnSequenceID();
	});
	Candidates.SetNum(FMath::Min(Candidates.Num(), MaxCount));
	if (Candidates.IsEmpty())
	{
		return false;
	}

	uint64 ReservationID = NextPowderReservationID++;
	if (ReservationID == 0)
	{
		ReservationID = NextPowderReservationID++;
	}
	if (ReservationID == 0)
	{
		return false;
	}
	FIGPowderReservationToken Token;
	Token.RuntimeToken = Action.RuntimeToken;
	Token.ActivationSequenceID = Action.ActivationSequenceID;
	Token.ReservationID = ReservationID;

	FReservedPowderSet Pending;
	Pending.Token = Token;
	Pending.Center = Center;
	Pending.CreatedWorldTime = Now;
	for (const FCandidate& Candidate : Candidates)
	{
		AIGPowderActor* Powder = Candidate.Actor;
		if (!IsValid(Powder) || !Powder->TryReserve(Token))
		{
			for (const FReservedPowderMember& Member : Pending.Members)
			{
				if (AIGPowderActor* Reserved = Member.Actor.Get())
				{
					Reserved->ReleaseReservation(Token, Member.OriginalTransform, Now);
				}
			}
			return false;
		}
		FReservedPowderMember& Member = Pending.Members.AddDefaulted_GetRef();
		Member.Actor = Powder;
		Member.OriginalTransform = Powder->GetActorTransform();
	}

	PowderReservations.Add(Token.ReservationID, MoveTemp(Pending));
	OutToken = Token;
	return true;
}

bool URes_InsectGlaive::BeginPowderGathering(
	const FIGPowderReservationToken& Token)
{
	UMHGZWeaponRuntimeHostComponent* Host = RuntimeContext.RuntimeToken.Host.Get();
	if (!Token.IsValid() || bRuntimeShuttingDown || !Host
		|| Token.RuntimeToken != RuntimeContext.RuntimeToken
		|| !IsRuntimeRequestCurrent(Token.RuntimeToken)
		|| !Host->IsActionActivationRegistered(Token.RuntimeToken,
			Token.ActivationSequenceID))
	{
		return false;
	}

	FReservedPowderSet* Reservation = PowderReservations.Find(Token.ReservationID);
	if (!Reservation || Reservation->Token != Token || Reservation->bGathering)
	{
		return false;
	}

	Reservation->bGathering = true;
	return true;
}

bool URes_InsectGlaive::UpdatePowderGathering(
	const FIGPowderReservationToken& Token, const float Progress)
{
	if (!Token.IsValid() || bRuntimeShuttingDown || !FMath::IsFinite(Progress)
		|| Token.RuntimeToken != RuntimeContext.RuntimeToken
		|| !IsRuntimeRequestCurrent(Token.RuntimeToken))
	{
		return false;
	}

	FReservedPowderSet* Reservation = PowderReservations.Find(Token.ReservationID);
	if (!Reservation || Reservation->Token != Token || !Reservation->bGathering)
	{
		return false;
	}

	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const float Alpha = FMath::Clamp(Progress, 0.0f, 1.0f);
	for (const FReservedPowderMember& Member : Reservation->Members)
	{
		AIGPowderActor* Powder = Member.Actor.Get();
		if (!Powder)
		{
			continue;
		}
		if (!Powder->CanCommitReservation(Token, Now))
		{
			// An expired or otherwise invalid member must not remain reserved if it
			// survives until its timer callback is processed.
			Powder->ReleaseReservationAtCurrentLocation(Token, Now);
			continue;
		}

		const FVector GatherLocation = FMath::Lerp(
			Member.OriginalTransform.GetLocation(), Reservation->Center, Alpha);
		Powder->SetGatherPresentationLocation(Token, GatherLocation);
	}
	return true;
}

bool URes_InsectGlaive::CommitPowderReservation(
	const FIGPowderReservationToken& Token,
	FIGPowderExplosionSnapshot& OutExplosion, const float ExplosionRadius)
{
	OutExplosion = FIGPowderExplosionSnapshot();
	UMHGZWeaponRuntimeHostComponent* Host = RuntimeContext.RuntimeToken.Host.Get();
	if (!Token.IsValid() || !IsRuntimeRequestCurrent(Token.RuntimeToken)
		|| Token.RuntimeToken != RuntimeContext.RuntimeToken || !GetWorld()
		|| !Host || !Host->IsActionActivationRegistered(
			Token.RuntimeToken, Token.ActivationSequenceID)
		|| !CombatConfig)
	{
		return false;
	}
	FReservedPowderSet* Found = PowderReservations.Find(Token.ReservationID);
	if (!Found || Found->Token != Token || !Found->bGathering)
	{
		return false;
	}
	if (!FMath::IsFinite(ExplosionRadius) || ExplosionRadius <= 0.0f)
    { ReleasePowderReservation(Token); return false; }

	// Copy and remove the transaction before changing actors; EndPlay callbacks may
	// update the registry synchronously when the consumed actors are destroyed.
	const FReservedPowderSet Reservation = *Found;
	PowderReservations.Remove(Token.ReservationID);
	const double Now = GetWorld()->GetTimeSeconds();
	TArray<AIGPowderActor*> LiveMembers;
	double TotalDamage = 0.0;
	for (const FReservedPowderMember& Member : Reservation.Members)
	{
		AIGPowderActor* Powder = Member.Actor.Get();
		if (Powder && Powder->CanCommitReservation(Token, Now))
		{
			LiveMembers.Add(Powder);
			TotalDamage += Powder->GetDamageContribution();
		}
		else if (Powder)
		{
			Powder->ReleaseReservation(Token, Member.OriginalTransform, Now);
		}
	}
	if (LiveMembers.IsEmpty() || !FMath::IsFinite(TotalDamage) || TotalDamage > static_cast<double>(MAX_flt))
	{
		// If all reserved members expired/died, fail and release any surviving stale
		// members; a partial live set is valid and is committed below at actual count.
		for (const FReservedPowderMember& Member : Reservation.Members)
		{
			if (AIGPowderActor* Powder = Member.Actor.Get())
			{
				Powder->ReleaseReservation(Token, Member.OriginalTransform, Now);
			}
		}
		return false;
	}

	for (AIGPowderActor* Powder : LiveMembers)
	{
		Powder->MarkConsumed(Token);
	}
	FIGPowderExplosionSnapshot Snapshot;
	Snapshot.ExplosionID = FGuid::NewGuid();
	Snapshot.Center = Reservation.Center;
	Snapshot.ConsumedCount = LiveMembers.Num();
	Snapshot.TotalDamage = TotalDamage;
	Snapshot.Radius = ExplosionRadius;
	Snapshot.BaseStagger = 0.0f;
	OutExplosion = Snapshot;

	for (AIGPowderActor* Powder : LiveMembers)
	{
		Powder->Destroy();
	}
	return true;
}

void URes_InsectGlaive::ReleasePowderReservation(
	const FIGPowderReservationToken& Token)
{
	if (!Token.IsValid())
	{
		return;
	}
	FReservedPowderSet* Found = PowderReservations.Find(Token.ReservationID);
	if (!Found || Found->Token != Token)
	{
		return;
	}
	const FReservedPowderSet Reservation = *Found;
	PowderReservations.Remove(Token.ReservationID);
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	for (const FReservedPowderMember& Member : Reservation.Members)
	{
		if (AIGPowderActor* Powder = Member.Actor.Get())
		{
			Powder->ReleaseReservation(Token, Member.OriginalTransform, Now);
		}
	}
}

void URes_InsectGlaive::ClearOwnedPowders(const EWeaponRuntimeEndReason Reason)
{
	ActivePowderVortexSettings = FIGPowderVortexSettings();
	ActivePowderVortexAttackPower = 0.f;
	StopPowderVortexGatherSounds();
	const int32 Count = OwnedPowders.Num();
	const TArray<TWeakObjectPtr<AIGPowderActor>> Powders = OwnedPowders;
	OwnedPowders.Reset();
	PowderReservations.Reset();
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(PowderVortexCommitTimer);
	}
	ActivePowderVortexCommandSerial = 0;
	ActivePowderVortexReservationID = 0;
	SpawnedPowderHitIDs.Reset();
	NextPowderSpawnSequenceID = 1;
	NextPowderReservationID = 1;
	LastMarkedContactPowderWorldTime = -1.0;
	NextPowderMaintenanceWorldTime = 0.0;
	for (const TWeakObjectPtr<AIGPowderActor>& WeakPowder : Powders)
	{
		if (AIGPowderActor* Powder = WeakPowder.Get())
		{
			Powder->MarkTerminal(EIGPowderTerminalReason::RuntimeShutdown);
			Powder->Destroy();
		}
	}
	if (Count > 0)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[IG Powder] Cleared %d powder actors for runtime end reason %d."),
			Count, static_cast<int32>(Reason));
	}
}

TArray<FIGPowderRuntimeSnapshot> URes_InsectGlaive::GetPowderSnapshots() const
{
	TArray<FIGPowderRuntimeSnapshot> Snapshots;
	if (!GetWorld())
	{
		return Snapshots;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	for (const TWeakObjectPtr<AIGPowderActor>& WeakPowder : OwnedPowders)
	{
		const AIGPowderActor* Powder = WeakPowder.Get();
		if (Powder && Powder->GetRuntimeToken() == RuntimeContext.RuntimeToken
			&& Powder->GetOwnerResource() == this
			&& Powder->GetPowderState() != EIGPowderState::Consumed
			&& Powder->MakeSnapshot(Now).RemainingLifetime > 0.0f)
		{
			Snapshots.Add(Powder->MakeSnapshot(Now));
		}
	}
	return Snapshots;
}

void URes_InsectGlaive::HandlePowderActorEndPlay(AIGPowderActor* Powder)
{
	if (!Powder)
	{
		return;
	}
	OwnedPowders.RemoveAll([Powder](const TWeakObjectPtr<AIGPowderActor>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == Powder;
	});
	RemovePowderFromReservations(Powder);
}

bool URes_InsectGlaive::AreTripleCostSpecs(
	const TArray<FWeaponResourceCostSpec>& Specs) const
{
	if (Specs.Num() != 1 || !Specs[0].CostType.MatchesTagExact(TripleCostTag()))
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

void URes_InsectGlaive::SetKinsectActiveTag(bool bActive)
{
	UMHGZWeaponRuntimeHostComponent* Host = RuntimeContext.RuntimeToken.Host.Get();
	if (!Host)
	{
		return;
	}
	if (bActive && !KinsectActiveTagToken.IsValid())
	{
		FGameplayTagContainer Tags;
		Tags.AddTag(KinsectActiveTag());
		KinsectActiveTagToken = Host->AcquireTags(EWeaponTagOwnerKind::Resource,
			FGameplayAbilitySpecHandle(), 0, TEXT("IG.Kinsect.Active"), Tags);
	}
	else if (!bActive && KinsectActiveTagToken.IsValid())
	{
		Host->ReleaseTags(KinsectActiveTagToken);
		KinsectActiveTagToken = FWeaponOwnedTagToken();
	}
}

void URes_InsectGlaive::SetMarkActiveTag(bool bActive)
{
	UMHGZWeaponRuntimeHostComponent* Host = RuntimeContext.RuntimeToken.Host.Get();
	if (!Host)
	{
		return;
	}
	if (bActive && !MarkActiveTagToken.IsValid())
	{
		FGameplayTagContainer Tags;
		Tags.AddTag(MarkActiveTag());
		MarkActiveTagToken = Host->AcquireTags(EWeaponTagOwnerKind::Resource,
			FGameplayAbilitySpecHandle(), 0, TEXT("IG.Mark.Active"), Tags);
	}
	else if (!bActive && MarkActiveTagToken.IsValid())
	{
		Host->ReleaseTags(MarkActiveTagToken);
		MarkActiveTagToken = FWeaponOwnedTagToken();
	}
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

void URes_InsectGlaive::ApplyEntryModifier(FGameplayTag AttributeTag, float Value,
	TEnumAsByte<EGameplayModOp::Type> Op)
{
	(void)AttributeTag;
	(void)Value;
	(void)Op;
	// Demo 冻结：该路径语义有缺陷，明确禁用，不允许悄悄修改猎虫数值。
}

void URes_InsectGlaive::ClearAllEntryModifiers()
{
	Super::ClearAllEntryModifiers();
}
