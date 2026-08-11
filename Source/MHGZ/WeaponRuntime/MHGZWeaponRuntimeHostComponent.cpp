// Copyright MHGZ Project. All Rights Reserved.

#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "ActionSystem/MHGZGameplayAbility.h"
#include "AttributeSystem/MHGZWeaponResourceComponent.h"
#include "Equipment/MHGZEquipmentComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "MHGZCharacter.h"

UMHGZWeaponRuntimeHostComponent::UMHGZWeaponRuntimeHostComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

// ── 生命周期 ────────────────────────────────────────────────────────────────

void UMHGZWeaponRuntimeHostComponent::InitializePawnRuntime(
	ACharacter* InCharacter,
	APlayerController* InController,
	UMHGZAbilitySystemComponent* InASC,
	UMHGZEquipmentComponent* InEquipment)
{
	if (!InCharacter)
	{
		return;
	}

	if (bInitialized)
	{
		const bool bSamePawn = CurrentContext.Character.Get() == InCharacter
			&& CurrentContext.Controller.Get() == InController
			&& CurrentContext.ASC.Get() == InASC
			&& CurrentContext.Equipment.Get() == InEquipment;
		if (bSamePawn)
		{
			// 幂等：相同输入不重建，Generation 不变。
			return;
		}

		// 不同 Pawn：清理旧状态（不单独递增 Generation，统一在下方 +1）。
		ClearRuntimeState();
	}

	++Generation;
	if (Generation == 0)
	{
		++Generation; // Token.IsValid() 要求 Generation != 0。
	}

	bInitialized = true;
	NextActivationSequenceID = 1;

	CurrentContext = FWeaponRuntimeContext();
	CurrentContext.Character = InCharacter;
	CurrentContext.Controller = InController;
	CurrentContext.ASC = InASC;
	CurrentContext.Equipment = InEquipment;

	CurrentToken.Host = this;
	CurrentToken.Generation = Generation;

	TagLedger.Initialize(CurrentToken, InASC);

	if (InASC)
	{
		InASC->SetRuntimeHost(this);
	}

	InitializePoseState(InCharacter);
}

void UMHGZWeaponRuntimeHostComponent::ShutdownRuntime(EWeaponRuntimeEndReason Reason)
{
	(void)Reason; // 结束原因当前仅用于日志/扩展；清理逻辑与原因无关。
	if (!bInitialized)
	{
		return;
	}

	++Generation;
	ClearRuntimeState();
	ResourceProvider = nullptr;
}

// ── Token / Generation ──────────────────────────────────────────────────────

bool UMHGZWeaponRuntimeHostComponent::IsTokenCurrent(const FWeaponRuntimeToken& Token) const
{
	return bInitialized && Token == CurrentToken;
}

uint32 UMHGZWeaponRuntimeHostComponent::AllocateActivationSequenceID()
{
	if (!bInitialized)
	{
		return 0;
	}

	const uint32 SequenceID = NextActivationSequenceID++;
	if (NextActivationSequenceID == 0)
	{
		NextActivationSequenceID = 1; // 永不发出 0。
	}
	return SequenceID;
}

// ── Loose Tag Ledger ────────────────────────────────────────────────────────

FWeaponTagOwnerID UMHGZWeaponRuntimeHostComponent::MakeOwnerID(
	EWeaponTagOwnerKind Kind,
	const FGameplayAbilitySpecHandle& AbilityHandle,
	uint32 ActivationSequenceID,
	FName LocalID) const
{
	FWeaponTagOwnerID OwnerID;
	OwnerID.RuntimeToken = CurrentToken;
	OwnerID.Kind = Kind;
	OwnerID.AbilityHandle = AbilityHandle;
	OwnerID.ActivationSequenceID = ActivationSequenceID;
	OwnerID.LocalID = LocalID;
	return OwnerID;
}

FGameplayTagContainer UMHGZWeaponRuntimeHostComponent::SingleTagContainer(FName TagName)
{
	FGameplayTagContainer Container;
	const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TagName);
	if (Tag.IsValid())
	{
		Container.AddTag(Tag);
	}
	return Container;
}

FWeaponOwnedTagToken UMHGZWeaponRuntimeHostComponent::AcquireTags(
	EWeaponTagOwnerKind Kind,
	const FGameplayAbilitySpecHandle& AbilityHandle,
	uint32 ActivationSequenceID,
	FName LocalID,
	const FGameplayTagContainer& Tags)
{
	if (!bInitialized)
	{
		return FWeaponOwnedTagToken();
	}

	return TagLedger.Acquire(
		MakeOwnerID(Kind, AbilityHandle, ActivationSequenceID, LocalID),
		Tags);
}

bool UMHGZWeaponRuntimeHostComponent::ReleaseTags(const FWeaponOwnedTagToken& Token)
{
	return TagLedger.Release(Token);
}

int32 UMHGZWeaponRuntimeHostComponent::ReleaseTagsForOwner(
	EWeaponTagOwnerKind Kind,
	const FGameplayAbilitySpecHandle& AbilityHandle,
	uint32 ActivationSequenceID,
	FName LocalID)
{
	if (!bInitialized)
	{
		return 0;
	}

	return TagLedger.ReleaseOwner(
		MakeOwnerID(Kind, AbilityHandle, ActivationSequenceID, LocalID));
}

// ── Active Action 注册表 ────────────────────────────────────────────────────

bool UMHGZWeaponRuntimeHostComponent::RegisterAction(const FWeaponActionToken& ActionToken)
{
	if (!bInitialized || !IsTokenCurrent(ActionToken.RuntimeToken) || !ActionToken.IsValid())
	{
		return false;
	}

	if (!ActiveActions.Contains(ActionToken))
	{
		ActiveActions.Add(ActionToken);
	}
	return true;
}

bool UMHGZWeaponRuntimeHostComponent::UnregisterAction(const FWeaponActionToken& ActionToken)
{
	if (!bInitialized || !IsTokenCurrent(ActionToken.RuntimeToken))
	{
		return false;
	}

	return ActiveActions.Remove(ActionToken) > 0;
}

void UMHGZWeaponRuntimeHostComponent::DispatchInputRelease(const FWeaponInputSnapshot& Snapshot)
{
	if (!bInitialized)
	{
		return;
	}

	// 释放身份不变量：仅分发给当前 Token、实例有效、且激活输入快照的
	// SourceControlTag + SequenceID 与本次 Release 完全一致的 Active Action。
	for (FWeaponActionToken Action : ActiveActions)
	{
		if (!IsTokenCurrent(Action.RuntimeToken) || !Action.IsValid())
		{
			continue;
		}
		UMHGZGameplayAbility* Ability =
			Cast<UMHGZGameplayAbility>(Action.AbilityInstance.Get());
		if (!Ability)
		{
			continue;
		}
		const FWeaponInputSnapshot& ActivationInput =
			Ability->GetWeaponActivationContext().Input;
		if (ActivationInput.SourceControlTag != Snapshot.SourceControlTag
			|| ActivationInput.SequenceID != Snapshot.SequenceID)
		{
			continue;
		}
		Ability->HandleInputReleased(Snapshot);
	}
}

// ── 精确 Montage 注册表 ─────────────────────────────────────────────────────

bool UMHGZWeaponRuntimeHostComponent::RegisterMontage(
	const FWeaponActionToken& ActionToken,
	USkeletalMeshComponent* Mesh,
	int32 MontageInstanceID)
{
	if (!bInitialized || !IsTokenCurrent(ActionToken.RuntimeToken)
		|| !ActionToken.IsValid() || !Mesh || MontageInstanceID == INDEX_NONE)
	{
		return false;
	}

	MontageRegistrations.RemoveAll(
		[Mesh, MontageInstanceID](const FWeaponMontageRegistration& Reg)
		{
			return Reg.Mesh.Get() == Mesh && Reg.MontageInstanceID == MontageInstanceID;
		});

	FWeaponMontageRegistration Registration;
	Registration.ActionToken = ActionToken;
	Registration.Mesh = Mesh;
	Registration.MontageInstanceID = MontageInstanceID;
	MontageRegistrations.Add(Registration);
	return true;
}

int32 UMHGZWeaponRuntimeHostComponent::UnregisterMontages(const FWeaponActionToken& ActionToken)
{
	if (!bInitialized || !IsTokenCurrent(ActionToken.RuntimeToken))
	{
		return 0;
	}

	const int32 Before = MontageRegistrations.Num();
	MontageRegistrations.RemoveAll(
		[&ActionToken](const FWeaponMontageRegistration& Reg)
		{
			return Reg.ActionToken == ActionToken;
		});
	return Before - MontageRegistrations.Num();
}

bool UMHGZWeaponRuntimeHostComponent::ResolveMontage(
	USkeletalMeshComponent* Mesh,
	int32 MontageInstanceID,
	FWeaponActionToken& OutActionToken) const
{
	OutActionToken = FWeaponActionToken();
	if (!bInitialized || !Mesh || MontageInstanceID == INDEX_NONE)
	{
		return false;
	}

	const FWeaponMontageRegistration* Found = MontageRegistrations.FindByPredicate(
		[Mesh, MontageInstanceID](const FWeaponMontageRegistration& Reg)
		{
			return Reg.Mesh.Get() == Mesh && Reg.MontageInstanceID == MontageInstanceID;
		});
	if (!Found || !Found->ActionToken.IsValid()
		|| !IsTokenCurrent(Found->ActionToken.RuntimeToken))
	{
		return false;
	}

	OutActionToken = Found->ActionToken;
	return true;
}

// ── Pawn 姿态 ───────────────────────────────────────────────────────────────

bool UMHGZWeaponRuntimeHostComponent::SetGrounded(bool bInGrounded)
{
	if (!bInitialized || bGrounded == bInGrounded)
	{
		return false;
	}

	return ApplyGroundedPose(bInGrounded);
}

bool UMHGZWeaponRuntimeHostComponent::SetSheathed(bool bInSheathed)
{
	if (!bInitialized || bSheathed == bInSheathed)
	{
		return false;
	}

	return ApplySheathedPose(bInSheathed);
}

void UMHGZWeaponRuntimeHostComponent::HandleLanded()
{
	if (!bInitialized)
	{
		return;
	}

	// 仅释放 Host 自身持有的空中姿态 Token；禁止按 Tag 扫描其他所有者。
	ReleasePoseToken(PoseTokens.AerialFalling);
	ReleasePoseToken(PoseTokens.AerialCantDodge);
	ReleasePoseToken(PoseTokens.AerialCantAttack);
	if (!bGrounded)
	{
		ReleasePoseToken(PoseTokens.GroundedOrAerial);
		ApplyGroundedPose(true);
	}
}

void UMHGZWeaponRuntimeHostComponent::InitializePoseState(ACharacter* InCharacter)
{
	PoseTokens = FPoseTokens();

	const bool bInGrounded = InCharacter && InCharacter->GetCharacterMovement()
		? InCharacter->GetCharacterMovement()->IsMovingOnGround()
		: true;
	ApplyGroundedPose(bInGrounded);

	bool bInSheathed = true;
	if (const AMHGZCharacter* MHGZCharacter = Cast<AMHGZCharacter>(InCharacter))
	{
		bInSheathed = !MHGZCharacter->bUnsheathed;
	}
	ApplySheathedPose(bInSheathed);
}

bool UMHGZWeaponRuntimeHostComponent::ApplyGroundedPose(bool bInGrounded)
{
	ReleasePoseToken(PoseTokens.GroundedOrAerial);

	PoseTokens.GroundedOrAerial = bInGrounded
		? AcquireTags(
			EWeaponTagOwnerKind::Pose,
			FGameplayAbilitySpecHandle(),
			0,
			TEXT("Pose.Grounded"),
			SingleTagContainer(TEXT("Combat.State.Grounded")))
		: AcquireTags(
			EWeaponTagOwnerKind::Pose,
			FGameplayAbilitySpecHandle(),
			0,
			TEXT("Pose.Aerial"),
			SingleTagContainer(TEXT("Combat.State.Aerial")));

	bGrounded = bInGrounded;
	return true;
}

bool UMHGZWeaponRuntimeHostComponent::ApplySheathedPose(bool bInSheathed)
{
	ReleasePoseToken(PoseTokens.SheathedOrUnsheathed);

	PoseTokens.SheathedOrUnsheathed = bInSheathed
		? AcquireTags(
			EWeaponTagOwnerKind::Pose,
			FGameplayAbilitySpecHandle(),
			0,
			TEXT("Pose.Sheathed"),
			SingleTagContainer(TEXT("Combat.State.Sheathed")))
		: AcquireTags(
			EWeaponTagOwnerKind::Pose,
			FGameplayAbilitySpecHandle(),
			0,
			TEXT("Pose.Unsheathed"),
			SingleTagContainer(TEXT("Combat.State.Unsheathed")));

	bSheathed = bInSheathed;
	return true;
}

void UMHGZWeaponRuntimeHostComponent::ReleasePoseToken(FWeaponOwnedTagToken& Token)
{
	if (Token.IsValid())
	{
		TagLedger.Release(Token);
	}
	Token = FWeaponOwnedTagToken();
}

// ── 武器资源预留透传 ────────────────────────────────────────────────────────

void UMHGZWeaponRuntimeHostComponent::SetResourceProvider(UMHGZWeaponResourceComponent* InProvider)
{
	ResourceProvider = InProvider;
}

bool UMHGZWeaponRuntimeHostComponent::CanReserveCosts(const TArray<FWeaponResourceCostSpec>& Specs) const
{
	if (Specs.IsEmpty())
	{
		return true;
	}
	return ResourceProvider.Get() ? ResourceProvider->CanReserveCosts(Specs) : false;
}

bool UMHGZWeaponRuntimeHostComponent::TryReserveCosts(
	const FWeaponActionToken& ActionToken,
	const TArray<FWeaponResourceCostSpec>& Specs,
	FWeaponResourceCostReservation& OutReservation)
{
	OutReservation = FWeaponResourceCostReservation();
	if (!bInitialized || !ActionToken.IsValid()
		|| !IsTokenCurrent(ActionToken.RuntimeToken))
	{
		return false;
	}

	OutReservation.RuntimeToken = CurrentToken;
	OutReservation.ActivationSequenceID = ActionToken.ActivationSequenceID;

	if (Specs.IsEmpty())
	{
		// 空 Specs：预留“成功”，ReservationID 保持 0 —— IsValid() 为 false，
		// Release/Consume 对无效预留均为无操作。
		return true;
	}

	return ResourceProvider.Get()
		? ResourceProvider->TryReserveCosts(ActionToken, Specs, OutReservation)
		: false;
}

void UMHGZWeaponRuntimeHostComponent::ReleaseReservation(const FWeaponResourceCostReservation& Reservation)
{
	if (!Reservation.IsValid() || !IsTokenCurrent(Reservation.RuntimeToken))
	{
		return;
	}

	if (UMHGZWeaponResourceComponent* Provider = ResourceProvider.Get())
	{
		Provider->ReleaseReservation(Reservation);
	}
}

void UMHGZWeaponRuntimeHostComponent::ConsumeReservedCosts(const FWeaponResourceCostReservation& Reservation)
{
	if (!Reservation.IsValid() || !IsTokenCurrent(Reservation.RuntimeToken))
	{
		return;
	}

	if (UMHGZWeaponResourceComponent* Provider = ResourceProvider.Get())
	{
		Provider->ConsumeReservedCosts(Reservation);
	}
}

// ── 内部清理 ────────────────────────────────────────────────────────────────

void UMHGZWeaponRuntimeHostComponent::ClearRuntimeState()
{
	TagLedger.ReleaseAll(CurrentToken);
	ResourceProvider = nullptr;

	ActiveActions.Reset();
	MontageRegistrations.Reset();
	PoseTokens = FPoseTokens();
	bGrounded = true;
	bSheathed = true;

	if (UMHGZAbilitySystemComponent* ASC = CurrentContext.ASC.Get())
	{
		if (ASC->GetRuntimeHost() == this)
		{
			ASC->SetRuntimeHost(nullptr);
		}
	}

	CurrentContext = FWeaponRuntimeContext();
	CurrentToken = FWeaponRuntimeToken();
	bInitialized = false;
}
