// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WeaponRuntime/MHGZWeaponRuntimeTypes.h"
#include "MHGZWeaponRuntimeHostComponent.generated.h"

class ACharacter;
class APlayerController;
class UMHGZAbilitySystemComponent;
class UMHGZEquipmentComponent;
class UMHGZWeaponResourceComponent;
class USkeletalMeshComponent;

/**
 * 当前 Pawn 的武器运行时所有者。
 * M1：完整生命周期 + Generation Token、TagLedger 所有权、Active Action 注册表、
 * 精确 Montage 注册表、Pawn 姿态（Grounded/Aerial、Sheathed/Unsheathed）、
 * 武器资源预留透传。
 */
UCLASS(ClassGroup = (MHGZ), BlueprintType, meta = (BlueprintSpawnableComponent))
class MHGZ_API UMHGZWeaponRuntimeHostComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMHGZWeaponRuntimeHostComponent();

	// ═══════════════════════════════════════════
	// 生命周期
	// ═══════════════════════════════════════════

	/**
	 * 初始化 Pawn 运行时。
	 * 相同输入幂等（不重建）；不同输入触发重建（Generation +1，旧 Token 全部失效）。
	 * InCharacter 必填；InController / InASC / InEquipment 可为空。
	 */
	void InitializePawnRuntime(
		ACharacter* InCharacter,
		APlayerController* InController,
		UMHGZAbilitySystemComponent* InASC,
		UMHGZEquipmentComponent* InEquipment);

	/** 关闭运行时（Generation +1，清理 Ledger 与全部注册表）。幂等。 */
	void ShutdownRuntime(EWeaponRuntimeEndReason Reason);

	bool IsRuntimeInitialized() const { return bInitialized; }

	// ═══════════════════════════════════════════
	// Token / Generation
	// ═══════════════════════════════════════════

	const FWeaponRuntimeContext& GetCurrentContext() const { return CurrentContext; }

	FWeaponRuntimeToken GetCurrentToken() const { return CurrentToken; }

	/** Token 必须同时匹配 Host 指针与当前 Generation。 */
	bool IsTokenCurrent(const FWeaponRuntimeToken& Token) const;

	/** 分配本 Runtime 内单调递增的激活序列号；未初始化时返回 0。 */
	uint32 AllocateActivationSequenceID();

	// ═══════════════════════════════════════════
	// Loose Tag Ledger
	// ═══════════════════════════════════════════

	FWeaponOwnedTagToken AcquireTags(
		EWeaponTagOwnerKind Kind,
		const FGameplayAbilitySpecHandle& AbilityHandle,
		uint32 ActivationSequenceID,
		FName LocalID,
		const FGameplayTagContainer& Tags);

	bool ReleaseTags(const FWeaponOwnedTagToken& Token);

	int32 ReleaseTagsForOwner(
		EWeaponTagOwnerKind Kind,
		const FGameplayAbilitySpecHandle& AbilityHandle,
		uint32 ActivationSequenceID,
		FName LocalID);

	// ═══════════════════════════════════════════
	// Active Action 注册表
	// ═══════════════════════════════════════════

	bool RegisterAction(const FWeaponActionToken& ActionToken);

	bool UnregisterAction(const FWeaponActionToken& ActionToken);

	/** 将 Release 快照分发给 InputTag 精确匹配的 Active Action。 */
	void DispatchInputRelease(const FWeaponInputSnapshot& Snapshot);

	// ═══════════════════════════════════════════
	// 精确 Montage 注册表
	// ═══════════════════════════════════════════

	bool RegisterMontage(
		const FWeaponActionToken& ActionToken,
		USkeletalMeshComponent* Mesh,
		int32 MontageInstanceID);

	int32 UnregisterMontages(const FWeaponActionToken& ActionToken);

	bool ResolveMontage(
		USkeletalMeshComponent* Mesh,
		int32 MontageInstanceID,
		FWeaponActionToken& OutActionToken) const;

	// ═══════════════════════════════════════════
	// Pawn 姿态（经 Ledger 拥有）
	// ═══════════════════════════════════════════

	bool SetGrounded(bool bInGrounded);

	bool SetSheathed(bool bInSheathed);

	/** 清除全部空中 Cant/Falling 拥有状态并落回 Grounded。 */
	void HandleLanded();

	bool IsGrounded() const { return bGrounded; }

	bool IsSheathed() const { return bSheathed; }

	// ═══════════════════════════════════════════
	// 武器资源预留透传
	// ═══════════════════════════════════════════

	void SetResourceProvider(UMHGZWeaponResourceComponent* InProvider);

	UMHGZWeaponResourceComponent* GetResourceProvider() const { return ResourceProvider.Get(); }

	bool CanReserveCosts(const TArray<FWeaponResourceCostSpec>& Specs) const;

	bool TryReserveCosts(
		const FWeaponActionToken& ActionToken,
		const TArray<FWeaponResourceCostSpec>& Specs,
		FWeaponResourceCostReservation& OutReservation);

	void ReleaseReservation(const FWeaponResourceCostReservation& Reservation);

	void ConsumeReservedCosts(const FWeaponResourceCostReservation& Reservation);

private:
	FWeaponTagOwnerID MakeOwnerID(
		EWeaponTagOwnerKind Kind,
		const FGameplayAbilitySpecHandle& AbilityHandle,
		uint32 ActivationSequenceID,
		FName LocalID) const;

	static FGameplayTagContainer SingleTagContainer(FName TagName);

	void ClearRuntimeState();

	void InitializePoseState(ACharacter* InCharacter);

	bool ApplyGroundedPose(bool bInGrounded);

	bool ApplySheathedPose(bool bInSheathed);

	void ReleasePoseToken(FWeaponOwnedTagToken& Token);

	struct FPoseTokens
	{
		FWeaponOwnedTagToken GroundedOrAerial;
		FWeaponOwnedTagToken SheathedOrUnsheathed;
		FWeaponOwnedTagToken AerialFalling;
		FWeaponOwnedTagToken AerialCantDodge;
		FWeaponOwnedTagToken AerialCantAttack;
	};

	FWeaponRuntimeContext CurrentContext;
	FWeaponRuntimeToken CurrentToken;
	uint64 Generation = 0;
	uint32 NextActivationSequenceID = 1;
	bool bInitialized = false;
	bool bGrounded = true;
	bool bSheathed = true;

	FWeaponRuntimeTagLedger TagLedger;
	TArray<FWeaponActionToken> ActiveActions;
	TArray<FWeaponMontageRegistration> MontageRegistrations;
	FPoseTokens PoseTokens;
	TObjectPtr<UMHGZWeaponResourceComponent> ResourceProvider;
};
