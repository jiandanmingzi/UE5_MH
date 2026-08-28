// Copyright MHGZ Project. All Rights Reserved.

#include "WeaponRuntime/MHGZWeaponRuntimeHostComponent.h"

#include "Abilities/GameplayAbility.h"
#include "ActionSystem/MHGZAbilitySystemComponent.h"
#include "ActionSystem/MHGZComboCoordinatorAbility.h"
#include "ActionSystem/MHGZGameplayAbility.h"
#include "ActionSystem/MHGZHitStopControllerComponent.h"
#include "ActionSystem/MHGZWeaponComboData.h"
#include "AttributeSystem/MHGZWeaponResourceComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Equipment/MHGZEquipmentDefinition.h"
#include "InputSystem/MHGZWeaponInputRouterComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "MHGZ.h"
#include "MHGZCharacter.h"
#include "WeaponRuntime/MHGZWeaponRuntimeDefinition.h"

UMHGZWeaponRuntimeHostComponent::UMHGZWeaponRuntimeHostComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

// ----------------------------------------------------------------------
// 生命周期
// ----------------------------------------------------------------------

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

		// 不同 Pawn：完整关停旧运行时（含解除 Equipment 订阅）。
		ShutdownRuntime(EWeaponRuntimeEndReason::AvatarChanged);
	}

	// 先订阅 Equipment 武器槽委托，再主动读取一次当前快照，避免漏接。
	if (InEquipment)
	{
		BindEquipmentEvents(InEquipment);
	}

	const FEquippedWeaponSnapshot InitialSnapshot =
		InEquipment ? InEquipment->GetEquippedWeaponSnapshot() : FEquippedWeaponSnapshot();

	++Generation;
	if (Generation == 0)
	{
		++Generation; // Token.IsValid() 要求 Generation != 0。
	}

	bInitialized = true;
	bShuttingDown = false;
	bHasDeferredWeaponSnapshot = false;
	DeferredWeaponSnapshot = FEquippedWeaponSnapshot();
	NextActivationSequenceID = 1;

	CurrentContext = FWeaponRuntimeContext();
	CurrentContext.Character = InCharacter;
	CurrentContext.Controller = InController;
	CurrentContext.ASC = InASC;
	CurrentContext.Equipment = InEquipment;

	CurrentToken.Host = this;
	CurrentToken.Generation = Generation;
	CurrentContext.RuntimeToken = CurrentToken;

	TagLedger.Initialize(CurrentToken, InASC);

	if (InASC)
	{
		InASC->SetRuntimeHost(this);
	}

	InitializePoseState(InCharacter);
	ApplyWeaponSnapshot(InitialSnapshot);
}

void UMHGZWeaponRuntimeHostComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ShutdownRuntime(EWeaponRuntimeEndReason::EndPlay);
	Super::EndPlay(EndPlayReason);
}

void UMHGZWeaponRuntimeHostComponent::ShutdownRuntime(EWeaponRuntimeEndReason Reason)
{
	if (!bInitialized)
	{
		return; // UnPossessed / EndPlay 双重调用幂等。
	}

	TeardownRuntime(Reason);
	bHasDeferredWeaponSnapshot = false;
	DeferredWeaponSnapshot = FEquippedWeaponSnapshot();
	UnbindEquipmentEvents();

	if (UMHGZAbilitySystemComponent* ASC = CurrentContext.ASC.Get())
	{
		if (ASC->GetRuntimeHost() == this)
		{
			ASC->SetRuntimeHost(nullptr);
		}
	}

	++Generation;
	if (Generation == 0)
	{
		++Generation;
	}

	CurrentContext = FWeaponRuntimeContext();
	CurrentToken = FWeaponRuntimeToken();
	bInitialized = false;
}

// ----------------------------------------------------------------------
// M2 装备差分生命周期
// ----------------------------------------------------------------------

void UMHGZWeaponRuntimeHostComponent::BindEquipmentEvents(UMHGZEquipmentComponent* InEquipment)
{
	if (!InEquipment)
	{
		return;
	}

	if (BoundEquipment.Get() == InEquipment && EquipmentWeaponChangedHandle.IsValid())
	{
		return; // 已订阅同一组件，幂等。
	}

	UnbindEquipmentEvents();

	BoundEquipment = InEquipment;
	EquipmentWeaponChangedHandle = InEquipment->OnEquippedWeaponChanged.AddUObject(
		this, &UMHGZWeaponRuntimeHostComponent::HandleEquippedWeaponChanged);
}

void UMHGZWeaponRuntimeHostComponent::UnbindEquipmentEvents()
{
	if (UMHGZEquipmentComponent* Equipment = BoundEquipment.Get())
	{
		Equipment->OnEquippedWeaponChanged.Remove(EquipmentWeaponChangedHandle);
	}
	BoundEquipment = nullptr;
	EquipmentWeaponChangedHandle.Reset();
}

void UMHGZWeaponRuntimeHostComponent::HandleEquippedWeaponChanged(
	const FEquippedWeaponSnapshot& Snapshot)
{
	if (!bInitialized)
	{
		return;
	}
	// Resource/Ability 的同步关停回调可能再次改装备。只保留最后快照，
	// 当前重建完成后重放，避免 Equipment 与 Host 长期分歧。
	if (bShuttingDown)
	{
		DeferredWeaponSnapshot = Snapshot;
		bHasDeferredWeaponSnapshot = true;
		return;
	}

	if (HasSameWeaponIdentity(Snapshot))
	{
		return; // 同一武器重复广播：护甲/饰品/镶嵌变化，no-op。
	}

	// 真实武器身份变化：TearDown → Generation+1 → Rebuild。
	TeardownRuntime(EWeaponRuntimeEndReason::WeaponChanged);
	RebuildRuntime(Snapshot);

	if (bHasDeferredWeaponSnapshot)
	{
		const FEquippedWeaponSnapshot PendingSnapshot = DeferredWeaponSnapshot;
		bHasDeferredWeaponSnapshot = false;
		DeferredWeaponSnapshot = FEquippedWeaponSnapshot();
		if (!HasSameWeaponIdentity(PendingSnapshot))
		{
			HandleEquippedWeaponChanged(PendingSnapshot);
		}
	}
}

bool UMHGZWeaponRuntimeHostComponent::HasSameWeaponIdentity(
	const FEquippedWeaponSnapshot& Snapshot) const
{
	return CurrentWeapon.EquipmentInstance.Get() == Snapshot.EquipmentInstance.Get()
		&& CurrentWeapon.RuntimeDefinition.Get() == Snapshot.RuntimeDefinition.Get();
}

void UMHGZWeaponRuntimeHostComponent::ApplyWeaponSnapshot(
	const FEquippedWeaponSnapshot& Snapshot)
{
	CurrentWeapon = Snapshot;
	CurrentContext.WeaponDefinition = Snapshot.WeaponDefinition.Get();
	CurrentContext.CombatConfig = Snapshot.RuntimeDefinition
		? Snapshot.RuntimeDefinition->CombatConfig.Get()
		: nullptr;
	if (APlayerController* Controller = CurrentContext.Controller.Get())
	{
		if (UMHGZWeaponInputRouterComponent* Router =
			Controller->FindComponentByClass<UMHGZWeaponInputRouterComponent>())
		{
			Router->SetInputProfile(Snapshot.RuntimeDefinition
				? Snapshot.RuntimeDefinition->InputProfile.Get() : nullptr);
		}
	}

	// InitializePoseState runs before the first equipment snapshot. Re-apply the
	// known pose after the definition is available so PIE starts on the back socket.
	ApplyWeaponVisualAttachment(bSheathed);
	BuildWeaponRuntime(Snapshot);
}

void UMHGZWeaponRuntimeHostComponent::BuildWeaponRuntime(
	const FEquippedWeaponSnapshot& Snapshot)
{
	const UWeaponRuntimeDefinition* RuntimeDef = Snapshot.RuntimeDefinition.Get();
	if (!RuntimeDef)
	{
		return; // 空 RuntimeDefinition：安全结束，不产生任何运行时对象。
	}
	if (!RuntimeDef->CombatConfig || !RuntimeDef->CombatConfig->ComboData)
	{
		return; // 空 CombatConfig/ComboData：安全结束，不留半初始化状态。
	}

	UMHGZWeaponComboData* ComboData = RuntimeDef->CombatConfig->ComboData;
	AActor* OwnerActor = CurrentContext.Character.Get();
	UMHGZAbilitySystemComponent* ASC = CurrentContext.ASC.Get();
	if (!OwnerActor || !ASC)
	{
		return; // 缺少 Pawn/ASC 时无法承载运行时。
	}

	// 1) 创建 Resource（Owner=Character/Pawn）。
	if (RuntimeDef->ResourceComponentClass)
	{
		UMHGZWeaponResourceComponent* Resource = NewObject<UMHGZWeaponResourceComponent>(
			OwnerActor, RuntimeDef->ResourceComponentClass);
		if (Resource)
		{
			OwnerActor->AddInstanceComponent(Resource);
			Resource->RegisterComponent();
			Resource->InitializeRuntime(CurrentContext);
			OwnedResource = Resource;
			ResourceProvider = Resource;
		}
	}

	// 2) 唯一收集 Transition.AbilityClass 授予。
	TArray<TSubclassOf<UGameplayAbility>> WeaponAbilities;
	for (const FComboTransition& Transition : ComboData->Transitions)
	{
		if (Transition.AbilityClass)
		{
			WeaponAbilities.AddUnique(Transition.AbilityClass);
		}
	}
	ASC->GrantWeaponAbilities(WeaponAbilities);

	// 3) 激活唯一 ComboCoordinator 并注入 ComboData；失败则回滚，不留半初始化状态。
	CoordinatorAbilityHandle = ASC->GiveAbility(
		FGameplayAbilitySpec(UGA_WeaponComboCoordinator::StaticClass(), 1, INDEX_NONE, ASC));
	if (!CoordinatorAbilityHandle.IsValid() || !ASC->TryActivateAbility(CoordinatorAbilityHandle))
	{
		if (CoordinatorAbilityHandle.IsValid())
		{
			ASC->ClearAbility(CoordinatorAbilityHandle);
			CoordinatorAbilityHandle = FGameplayAbilitySpecHandle();
		}
		ASC->RemoveWeaponAbilities();
		if (OwnedResource.Get())
		{
			OwnedResource->ShutdownRuntime(EWeaponRuntimeEndReason::RuntimeShutdown);
			if (OwnedResource->IsRegistered())
			{
				OwnedResource->DestroyComponent();
			}
			OwnedResource = nullptr;
			ResourceProvider = nullptr;
		}
		return;
	}

	if (UGA_WeaponComboCoordinator* ActiveCoordinator = ASC->GetActiveComboCoordinator())
	{
		ActiveCoordinator->InjectComboData(ComboData);
	}
}

void UMHGZWeaponRuntimeHostComponent::TeardownRuntime(EWeaponRuntimeEndReason Reason)
{
	if (!bInitialized)
	{
		return;
	}

	// 1) 拒绝新请求：关停/重建窗口内不接受新的输入、注册与资源预留。
	bShuttingDown = true;

	// 2) 取消当前武器 Ability 与 ComboCoordinator。
	if (UMHGZAbilitySystemComponent* ASC = CurrentContext.ASC.Get())
	{
		ASC->RemoveWeaponAbilities();
		if (CoordinatorAbilityHandle.IsValid())
		{
			ASC->ClearAbility(CoordinatorAbilityHandle);
			CoordinatorAbilityHandle = FGameplayAbilitySpecHandle();
		}
	}

	// 3) 先广播旧 Token 失效，让所有订阅者在 Resource 发出关停回调前解绑。
	OnWeaponRuntimeInvalidated.Broadcast(CurrentToken);

	// 4) Resource Shutdown（清 modifiers；IG 走 OnWeaponUnequipped 清理猎虫/萃取 GE）。
	if (UMHGZWeaponResourceComponent* Provider = ResourceProvider.Get())
	{
		Provider->ShutdownRuntime(Reason);
	}
	if (APlayerController* Controller = CurrentContext.Controller.Get())
	{
		if (UMHGZWeaponInputRouterComponent* Router =
			Controller->FindComponentByClass<UMHGZWeaponInputRouterComponent>())
		{
			Router->SetInputProfile(nullptr);
		}
	}

	// 5) 命中停顿请求不能跨换武器、死亡或 EndPlay 泄漏。
	if (AActor* Character = CurrentContext.Character.Get())
	{
		if (UMHGZHitStopControllerComponent* HitStop =
			Character->FindComponentByClass<UMHGZHitStopControllerComponent>())
		{
			HitStop->ClearAll();
		}
	}

	// 6) Ledger ReleaseAll + 注册表清空。
	TagLedger.ReleaseAll(CurrentToken);
	ActiveActions.Reset();
	MontageRegistrations.Reset();
	MontageRootMotionOwner = FWeaponActionToken();
	PoseTokens = FPoseTokens();
	bGrounded = true;
	bSheathed = true;

	// 7) DestroyComponent（Host 自建 Resource 的最后一步）。
	if (OwnedResource.Get())
	{
		if (OwnedResource->IsRegistered())
		{
			OwnedResource->DestroyComponent();
		}
		OwnedResource = nullptr;
	}
	ResourceProvider = nullptr;

	CurrentWeapon = FEquippedWeaponSnapshot();
	CurrentContext.WeaponDefinition = nullptr;
	CurrentContext.CombatConfig = nullptr;
	bShuttingDown = false;
}

void UMHGZWeaponRuntimeHostComponent::RebuildRuntime(
	const FEquippedWeaponSnapshot& Snapshot)
{
	// TeardownRuntime 已保留 CurrentContext（Character/Controller/ASC/Equipment）。
	++Generation;
	if (Generation == 0)
	{
		++Generation;
	}

	bInitialized = true;
	bShuttingDown = false;
	NextActivationSequenceID = 1;

	CurrentToken.Host = this;
	CurrentToken.Generation = Generation;
	CurrentContext.RuntimeToken = CurrentToken;

	TagLedger.Initialize(CurrentToken, CurrentContext.ASC.Get());

	if (UMHGZAbilitySystemComponent* ASC = CurrentContext.ASC.Get())
	{
		ASC->SetRuntimeHost(this);
	}

	InitializePoseState(CurrentContext.Character.Get());
	ApplyWeaponSnapshot(Snapshot);
}

// ----------------------------------------------------------------------
// Token / Generation
// ----------------------------------------------------------------------

bool UMHGZWeaponRuntimeHostComponent::IsTokenCurrent(const FWeaponRuntimeToken& Token) const
{
	return bInitialized && Token == CurrentToken;
}

uint32 UMHGZWeaponRuntimeHostComponent::AllocateActivationSequenceID()
{
	if (!bInitialized || bShuttingDown)
	{
		return 0;
	}

	const uint32 SequenceID = NextActivationSequenceID++;
	if (NextActivationSequenceID == 0)
	{
		NextActivationSequenceID = 1; // 永不出 0。
	}
	return SequenceID;
}

// ----------------------------------------------------------------------
// Loose Tag Ledger
// ----------------------------------------------------------------------

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
	if (!bInitialized || bShuttingDown)
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

// ----------------------------------------------------------------------
// Active Action 注册表
// ----------------------------------------------------------------------

bool UMHGZWeaponRuntimeHostComponent::RegisterAction(const FWeaponActionToken& ActionToken)
{
	if (!bInitialized || bShuttingDown || !IsTokenCurrent(ActionToken.RuntimeToken)
		|| !ActionToken.IsValid())
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

	if (MontageRootMotionOwner == ActionToken)
	{
		MontageRootMotionOwner = FWeaponActionToken();
	}
	return ActiveActions.Remove(ActionToken) > 0;
}

void UMHGZWeaponRuntimeHostComponent::DispatchInputRelease(const FWeaponInputSnapshot& Snapshot)
{
	if (!bInitialized || bShuttingDown)
	{
		return;
	}

	// 释放身份不变式：仅分发给当前 Token、实例有效、且激活输入快照的
	// SourceControlTag + SequenceID 与本次 Release 完全一致的 Active Action。
	const TArray<FWeaponActionToken> ActionsSnapshot = ActiveActions;
	for (const FWeaponActionToken& Action : ActionsSnapshot)
	{
		if (!IsTokenCurrent(Action.RuntimeToken) || !Action.IsValid()
			|| !ActiveActions.Contains(Action))
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

FString UMHGZWeaponRuntimeHostComponent::GetActiveActionsDebugString() const
{
	TArray<FString> Actions;
	Actions.Reserve(ActiveActions.Num());
	for (const FWeaponActionToken& Action : ActiveActions)
	{
		if (!Action.IsValid() || !IsTokenCurrent(Action.RuntimeToken))
		{
			continue;
		}
		const UGameplayAbility* Ability = Action.AbilityInstance.Get();
		const FString AbilityName = Ability ? Ability->GetClass()->GetName() : TEXT("InvalidAbility");
		Actions.Add(FString::Printf(TEXT("%s#%u"), *AbilityName, Action.ActivationSequenceID));
	}
	Actions.Sort();
	return FString::Join(Actions, TEXT("|"));
}

// ----------------------------------------------------------------------
// Montage Root Motion 单一所有者
// ----------------------------------------------------------------------

bool UMHGZWeaponRuntimeHostComponent::AcquireMontageRootMotion(
	const FWeaponActionToken& ActionToken)
{
	if (!bInitialized || bShuttingDown || !ActionToken.IsValid()
		|| !IsTokenCurrent(ActionToken.RuntimeToken)
		|| !ActiveActions.Contains(ActionToken))
	{
		return false;
	}

	if (MontageRootMotionOwner == ActionToken)
	{
		return true;
	}
	if (IsMontageRootMotionOwned())
	{
		return false;
	}

	MontageRootMotionOwner = ActionToken;
	return true;
}

bool UMHGZWeaponRuntimeHostComponent::ReleaseMontageRootMotion(
	const FWeaponActionToken& ActionToken)
{
	if (!bInitialized || !ActionToken.IsValid()
		|| !IsTokenCurrent(ActionToken.RuntimeToken)
		|| MontageRootMotionOwner != ActionToken)
	{
		return false;
	}

	MontageRootMotionOwner = FWeaponActionToken();
	return true;
}

bool UMHGZWeaponRuntimeHostComponent::IsMontageRootMotionOwned() const
{
	return bInitialized && !bShuttingDown && MontageRootMotionOwner.IsValid()
		&& IsTokenCurrent(MontageRootMotionOwner.RuntimeToken)
		&& ActiveActions.Contains(MontageRootMotionOwner);
}

bool UMHGZWeaponRuntimeHostComponent::IsMontageRootMotionOwnedBy(
	const FWeaponActionToken& ActionToken) const
{
	return IsMontageRootMotionOwned() && MontageRootMotionOwner == ActionToken;
}

FString UMHGZWeaponRuntimeHostComponent::GetMontageRootMotionOwnerDebugString() const
{
	if (!IsMontageRootMotionOwned())
	{
		return FString();
	}
	const UGameplayAbility* Ability = MontageRootMotionOwner.AbilityInstance.Get();
	const FString AbilityName = Ability ? Ability->GetClass()->GetName() : TEXT("InvalidAbility");
	return FString::Printf(TEXT("%s#%u"), *AbilityName,
		MontageRootMotionOwner.ActivationSequenceID);
}

// ----------------------------------------------------------------------
// 精确 Montage 注册表
// ----------------------------------------------------------------------

bool UMHGZWeaponRuntimeHostComponent::RegisterMontage(
	const FWeaponActionToken& ActionToken,
	USkeletalMeshComponent* Mesh,
	int32 MontageInstanceID)
{
	if (!bInitialized || bShuttingDown || !IsTokenCurrent(ActionToken.RuntimeToken)
		|| !ActionToken.IsValid() || !ActiveActions.Contains(ActionToken)
		|| !Mesh || MontageInstanceID == INDEX_NONE)
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
	if (!bInitialized || bShuttingDown || !Mesh || MontageInstanceID == INDEX_NONE)
	{
		return false;
	}

	const FWeaponMontageRegistration* Found = MontageRegistrations.FindByPredicate(
		[Mesh, MontageInstanceID](const FWeaponMontageRegistration& Reg)
		{
			return Reg.Mesh.Get() == Mesh && Reg.MontageInstanceID == MontageInstanceID;
		});
	if (!Found || !Found->ActionToken.IsValid()
		|| !ActiveActions.Contains(Found->ActionToken)
		|| !IsTokenCurrent(Found->ActionToken.RuntimeToken))
	{
		return false;
	}

	OutActionToken = Found->ActionToken;
	return true;
}

// ----------------------------------------------------------------------
// Pawn 姿态
// ----------------------------------------------------------------------

bool UMHGZWeaponRuntimeHostComponent::SetGrounded(bool bInGrounded)
{
	if (!bInitialized || bShuttingDown || bGrounded == bInGrounded)
	{
		return false;
	}

	return ApplyGroundedPose(bInGrounded);
}

bool UMHGZWeaponRuntimeHostComponent::SetSheathed(bool bInSheathed)
{
	if (!bInitialized || bShuttingDown || bSheathed == bInSheathed)
	{
		return false;
	}

	return ApplySheathedPose(bInSheathed);
}

void UMHGZWeaponRuntimeHostComponent::HandleLanded()
{
	if (!bInitialized || bShuttingDown)
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
	bWeaponVisualAttachmentWarningIssued = false;
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
	// Keep visual reattachment in the same exact path as Draw/Sheathe Commit.
	// Missing visual configuration must not leave GAS in the previous posture.
	ApplyWeaponVisualAttachment(bInSheathed);

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

USkeletalMeshComponent* UMHGZWeaponRuntimeHostComponent::FindWeaponVisualComponent() const
{
	const ACharacter* Character = CurrentContext.Character.Get();
	const UMHGZWeaponDefinition* Definition = CurrentContext.WeaponDefinition.Get();
	if (!Character || !Definition || Definition->VisualComponentTag.IsNone())
	{
		return nullptr;
	}

	TArray<USkeletalMeshComponent*> MeshComponents;
	Character->GetComponents<USkeletalMeshComponent>(MeshComponents);
	for (USkeletalMeshComponent* MeshComponent : MeshComponents)
	{
		if (MeshComponent && MeshComponent != Character->GetMesh()
			&& MeshComponent->ComponentHasTag(Definition->VisualComponentTag))
		{
			return MeshComponent;
		}
	}
	return nullptr;
}

bool UMHGZWeaponRuntimeHostComponent::ApplyWeaponVisualAttachment(bool bInSheathed)
{
	ACharacter* Character = CurrentContext.Character.Get();
	const UMHGZWeaponDefinition* Definition = CurrentContext.WeaponDefinition.Get();
	USkeletalMeshComponent* CharacterMesh = Character ? Character->GetMesh() : nullptr;
	if (!Character || !Definition || !CharacterMesh)
	{
		return false;
	}

	USkeletalMeshComponent* WeaponMesh = FindWeaponVisualComponent();
	const FName AttachSocket = bInSheathed
		? Definition->SheathedAttachSocket
		: Definition->AttachSocket;
	if (!WeaponMesh || AttachSocket.IsNone() || !CharacterMesh->DoesSocketExist(AttachSocket))
	{
		if (!bWeaponVisualAttachmentWarningIssued)
		{
			UE_LOG(LogMHGZ, Warning,
				TEXT("Weapon visual attachment skipped for '%s': visual component tag '%s' or character socket '%s' is missing."),
				*Definition->GetName(), *Definition->VisualComponentTag.ToString(), *AttachSocket.ToString());
			bWeaponVisualAttachmentWarningIssued = true;
		}
		return false;
	}

	WeaponMesh->AttachToComponent(CharacterMesh,
		FAttachmentTransformRules::SnapToTargetNotIncludingScale, AttachSocket);
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

// ----------------------------------------------------------------------
// 武器资源预留透传
// ----------------------------------------------------------------------

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
	if (!bInitialized || bShuttingDown || !ActionToken.IsValid()
		|| !IsTokenCurrent(ActionToken.RuntimeToken))
	{
		return false;
	}

	OutReservation.RuntimeToken = CurrentToken;
	OutReservation.ActivationSequenceID = ActionToken.ActivationSequenceID;

	if (Specs.IsEmpty())
	{
		// 空 Specs：预留“成功”，ReservationID 保持 0 —— Reservation.IsValid() 为 false，
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
