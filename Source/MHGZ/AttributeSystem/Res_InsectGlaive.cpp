// Copyright MHGZ Project. All Rights Reserved.

// 运行期生命周期、猎虫指令仲裁与通用猎虫伤害。
#include "Res_InsectGlaive.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameplayEffect.h"
#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"
#include "InsectGlaive/Kinsect/IGCoAttackComponent.h"
#include "AbilitySystemGlobals.h"
#include "ActionSystem/MHGZDamageGameplayEffect.h"
#include "ActionSystem/MHGZGameplayEffectContext.h"
#include "AttributeSystem/MHGZAttributeSet.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstance.h"
#include "InsectGlaive/Kinsect/InsectGlaiveKinsectData.h"
#include "InsectGlaive/Kinsect/Kinsect.h"
#include "Monster/MHGZMonsterHitzoneComponent.h"

namespace
{
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

const FGameplayTag& KinsectActiveTag()
{
	static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
		TEXT("WeaponResource.IG.Kinsect.Active"));
	return Tag;
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

bool URes_InsectGlaive::CanDeployKinsect(const FKinsectFlightRequest& Request, const bool bAfterStaminaRefill) const
{
	return !bRuntimeShuttingDown && KinsectActor && KinsectData
		&& KinsectActor->GetState() != EKinsectState::ActionAttached
		&& !KinsectActionAttachmentToken.IsValid()
		&& (KinsectStamina > 0.f || (bAfterStaminaRefill && FMath::IsFinite(MaxKinsectStamina) && MaxKinsectStamina > 0.f))
		&& IsRuntimeRequestCurrent(Request.RuntimeToken)
		&& KinsectActor->CanBeginFlight(Request);
}

bool URes_InsectGlaive::RestoreKinsectStaminaForAction(const FWeaponActionToken& Action)
{
	const UMHGZWeaponRuntimeHostComponent* Host = Action.RuntimeToken.Host.Get();
	if (!Action.IsValid() || !IsRuntimeRequestCurrent(Action.RuntimeToken)
		|| !Host || !Host->IsActionRegistered(Action) || !IsValid(KinsectActor)
		|| !FMath::IsFinite(MaxKinsectStamina) || MaxKinsectStamina <= 0.f) return false;
	const float Previous = KinsectStamina;
	KinsectStamina = MaxKinsectStamina;
	bDepletionEdgeTriggered = false;
	if (!FMath::IsNearlyEqual(Previous, KinsectStamina))
		OnKinsectStaminaChanged.Broadcast(KinsectStamina, MaxKinsectStamina);
	if (IsRuntimeRequestCurrent(Action.RuntimeToken)) BroadcastResourceUIState(true);
	return true;
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
