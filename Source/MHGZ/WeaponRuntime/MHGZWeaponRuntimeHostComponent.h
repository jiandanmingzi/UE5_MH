// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Equipment/MHGZEquipmentComponent.h"
#include "WeaponRuntime/MHGZWeaponRuntimeTypes.h"
#include "MHGZWeaponRuntimeHostComponent.generated.h"

class ACharacter;
class APlayerController;
class UMHGZAbilitySystemComponent;
class UMHGZWeaponResourceComponent;
class USkeletalMeshComponent;

/** 旧 Token 失效通知：M2 仅广播；M3 HUD/AimComponent 据此解绑。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnWeaponRuntimeInvalidated, const FWeaponRuntimeToken&);

/**
 * 当前 Pawn 的武器运行时所有者。
 * M1：完整生命周期 + Generation Token、TagLedger 所有权、Active Action 注册表、
 * 精确 Montage 注册表、Pawn 姿态（Grounded/Aerial、Sheathed/Unsheathed）、资源预留透传。
 * M2：订阅 Equipment 武器槽快照；仅 EquipmentInstance / RuntimeDefinition 身份真实变化时
 * 执行 TearDown → Generation+1 → Rebuild；创建/销毁 Resource、授予/移除武器 Ability、
 * 启停唯一 ComboCoordinator 并注入 ComboData。
 */
UCLASS(ClassGroup = (MHGZ), BlueprintType, meta = (BlueprintSpawnableComponent))
class MHGZ_API UMHGZWeaponRuntimeHostComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMHGZWeaponRuntimeHostComponent();

	// ----------------------------------------------------------------------
	// 生命周期
	// ----------------------------------------------------------------------
	/**
	 * 初始化 Pawn 运行时。
	 * 相同输入幂等（不重建）；不同输入触发重建（Generation +1，旧 Token 全部失效）。
	 * 先订阅 Equipment 武器槽委托，再主动读取一次当前快照，避免漏接。
	 * InCharacter 必填；InController / InASC / InEquipment 可为空。
	 */
	void InitializePawnRuntime(
		ACharacter* InCharacter,
		APlayerController* InController,
		UMHGZAbilitySystemComponent* InASC,
		UMHGZEquipmentComponent* InEquipment);

	/** 关闭运行时（Generation +1，清理 Ledger 与全部注册表、Resource、武器 Ability）。幂等。 */
	void ShutdownRuntime(EWeaponRuntimeEndReason Reason);

	/** EndPlay 兜底：与 UnPossessed 双重调用保持幂等。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	bool IsRuntimeInitialized() const { return bInitialized; }

	/** 旧 Token 失效通知（携带被废弃的 RuntimeToken）。 */
	FOnWeaponRuntimeInvalidated OnWeaponRuntimeInvalidated;

	// ----------------------------------------------------------------------
	// Token / Generation
	// ----------------------------------------------------------------------
	const FWeaponRuntimeContext& GetCurrentContext() const { return CurrentContext; }

	FWeaponRuntimeToken GetCurrentToken() const { return CurrentToken; }

	/** Token 必须同时匹配 Host 指针与当前 Generation。 */
	bool IsTokenCurrent(const FWeaponRuntimeToken& Token) const;

	/** 分配本 Runtime 内单调递增的激活序列号；未初始化或关停中返回 0。 */
	uint32 AllocateActivationSequenceID();

	// ----------------------------------------------------------------------
	// Loose Tag Ledger
	// ----------------------------------------------------------------------
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

	// ----------------------------------------------------------------------
	// Active Action 注册表
	// ----------------------------------------------------------------------
	bool RegisterAction(const FWeaponActionToken& ActionToken);

	bool UnregisterAction(const FWeaponActionToken& ActionToken);

	/** 将 Release 快照分发给 InputTag 精确匹配的 Active Action。 */
	void DispatchInputRelease(const FWeaponInputSnapshot& Snapshot);

	// ----------------------------------------------------------------------
	// 精确 Montage 注册表
	// ----------------------------------------------------------------------
	bool RegisterMontage(
		const FWeaponActionToken& ActionToken,
		USkeletalMeshComponent* Mesh,
		int32 MontageInstanceID);

	int32 UnregisterMontages(const FWeaponActionToken& ActionToken);

	bool ResolveMontage(
		USkeletalMeshComponent* Mesh,
		int32 MontageInstanceID,
		FWeaponActionToken& OutActionToken) const;

	// ----------------------------------------------------------------------
	// Pawn 姿态（经 Ledger 拥有）
	// ----------------------------------------------------------------------
	bool SetGrounded(bool bInGrounded);

	bool SetSheathed(bool bInSheathed);

	/** 清除全部空中 Cant/Falling 拥有状态并落回 Grounded。 */
	void HandleLanded();

	bool IsGrounded() const { return bGrounded; }

	bool IsSheathed() const { return bSheathed; }

	// ----------------------------------------------------------------------
	// 武器资源预留透传
	// ----------------------------------------------------------------------
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

	void InitializePoseState(ACharacter* InCharacter);

	bool ApplyGroundedPose(bool bInGrounded);

	bool ApplySheathedPose(bool bInSheathed);

	void ReleasePoseToken(FWeaponOwnedTagToken& Token);

	// ----------------------------------------------------------------------
	// M2 装备差分生命周期
	// ----------------------------------------------------------------------
	/** 订阅 Equipment 的 OnEquippedWeaponChanged（重复订阅幂等）。 */
	void BindEquipmentEvents(UMHGZEquipmentComponent* InEquipment);

	/** 解除订阅；任何路径只解除自己持有的绑定。 */
	void UnbindEquipmentEvents();

	/** Equipment 武器槽广播回调：身份未变 no-op；真实变化则完整重建。 */
	void HandleEquippedWeaponChanged(const FEquippedWeaponSnapshot& Snapshot);

	bool HasSameWeaponIdentity(const FEquippedWeaponSnapshot& Snapshot) const;

	/** 应用快照：写入 CurrentWeapon 与 CurrentContext.WeaponDefinition 后构建运行时。 */
	void ApplyWeaponSnapshot(const FEquippedWeaponSnapshot& Snapshot);

	/** 从 RuntimeDefinition 构建 Resource / 武器 Ability / ComboCoordinator；空配置安全结束。 */
	void BuildWeaponRuntime(const FEquippedWeaponSnapshot& Snapshot);

	/** 冻结清理顺序：拒绝新请求→RemoveWeaponAbilities→Resource Shutdown→广播失效→Ledger/注册表清空→DestroyComponent。 */
	void TeardownRuntime(EWeaponRuntimeEndReason Reason);

	/** 重建：Generation+1、Token/Ledger/姿态重初始化后应用新快照。 */
	void RebuildRuntime(const FEquippedWeaponSnapshot& Snapshot);

	struct FPoseTokens
	{
		FWeaponOwnedTagToken GroundedOrAerial;
		FWeaponOwnedTagToken SheathedOrUnsheathed;
		FWeaponOwnedTagToken AerialFalling;
		FWeaponOwnedTagToken AerialCantDodge;
		FWeaponOwnedTagToken AerialCantAttack;
	};

	/** 当前运行上下文（含 RuntimeToken 与 WeaponDefinition；UPROPERTY 保证强引用被 GC 追踪）。 */
	UPROPERTY()
	FWeaponRuntimeContext CurrentContext;

	FWeaponRuntimeToken CurrentToken;
	uint64 Generation = 0;
	uint32 NextActivationSequenceID = 1;
	bool bInitialized = false;
	/** 关停/重建窗口：拒绝新的输入、注册与资源预留请求。 */
	bool bShuttingDown = false;
	bool bGrounded = true;
	bool bSheathed = true;

	FWeaponRuntimeTagLedger TagLedger;
	TArray<FWeaponActionToken> ActiveActions;
	TArray<FWeaponMontageRegistration> MontageRegistrations;
	FPoseTokens PoseTokens;

	/** 资源预留透传目标（可由 SetResourceProvider 测试入口注入）。 */
	TObjectPtr<UMHGZWeaponResourceComponent> ResourceProvider;

	/** Host 自建并拥有生命周期的 Resource；换武器/关停时销毁。 */
	UPROPERTY()
	TObjectPtr<UMHGZWeaponResourceComponent> OwnedResource;

	/** Host 单独授予的 ComboCoordinator；不属于 WeaponAbilityHandles，必须显式回收。 */
	FGameplayAbilitySpecHandle CoordinatorAbilityHandle;

	/** 当前已应用快照；用于身份 no-op 判定。 */
	UPROPERTY()
	FEquippedWeaponSnapshot CurrentWeapon;

	/** Teardown 同步回调中到达的最后一个装备快照；当前重建完成后立即重放。 */
	UPROPERTY()
	FEquippedWeaponSnapshot DeferredWeaponSnapshot;
	bool bHasDeferredWeaponSnapshot = false;

	/** 当前订阅的 Equipment 组件与其委托句柄。 */
	TWeakObjectPtr<UMHGZEquipmentComponent> BoundEquipment;
	FDelegateHandle EquipmentWeaponChangedHandle;
};
