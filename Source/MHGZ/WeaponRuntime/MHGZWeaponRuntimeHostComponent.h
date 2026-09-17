// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Equipment/MHGZEquipmentComponent.h"
#include "WeaponRuntime/MHGZWeaponRuntimeTypes.h"
#include "MHGZWeaponRuntimeHostComponent.generated.h"

class ACharacter;
class APlayerController;
class UAnimMontage;
class UMHGZAbilitySystemComponent;
class UMHGZWeaponResourceComponent;
class USkeletalMeshComponent;

/** Read-only state of the system-owned aerial presentation montages for runtime telemetry. */
struct FMHGZAerialPresentationRootMotionTelemetry
{
	FString FallingMontage;
	bool bFallingMontageReferenced = false;
	bool bFallingRootMotionDisabledByHost = false;
	bool bFallingMontageInstanceFound = false;
	bool bFallingInstanceRootMotionDisabled = false;

	FString LandingMontage;
	bool bLandingMontageReferenced = false;
	bool bLandingRootMotionDisabledByHost = false;
	bool bLandingMontageInstanceFound = false;
	bool bLandingInstanceRootMotionDisabled = false;
};

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

	/** Compact active GA names and activation sequences for opt-in runtime capture. */
	FString GetActiveActionsDebugString() const;

	// ----------------------------------------------------------------------
	// Montage Root Motion 单一所有者
	// ----------------------------------------------------------------------
	/** 当前已注册 Action 获取 Montage Root Motion 独占权；同一 Action 重复获取幂等。 */
	bool AcquireMontageRootMotion(const FWeaponActionToken& ActionToken);

	/** 仅精确所有者可释放；重复、旧 Runtime 或其他 Action 释放均失败。 */
	bool ReleaseMontageRootMotion(const FWeaponActionToken& ActionToken);

	/** 当前 Runtime 是否存在有效的 Montage Root Motion 所有者。 */
	bool IsMontageRootMotionOwned() const;

	/** 指定 ActionToken 是否为当前 Montage Root Motion 所有者。 */
	bool IsMontageRootMotionOwnedBy(const FWeaponActionToken& ActionToken) const;

	/** Active GA that owns Montage Root Motion, or an empty string. */
	FString GetMontageRootMotionOwnerDebugString() const;

	// ----------------------------------------------------------------------
	// Action Movement 单一所有者（M5）
	// ----------------------------------------------------------------------
	/**
	 * 获得 CMC 动作位移的唯一执行权，并为本次 Action 分配一次性的 WarpTarget 名称。
	 * Montage Root Motion 与动作 MovementTask 不能同时拥有 CMC 位移；调用者必须先完成
	 * 旧 Montage 的 Root Motion 交接或停止。
	 */
	bool AcquireActionMovement(const FWeaponActionToken& ActionToken,
		FName& OutWarpTargetName);

	/** 仅精确拥有者可释放；同时移除该动作唯一的 MotionWarping Target。 */
	bool ReleaseActionMovement(const FWeaponActionToken& ActionToken);

	/** 当前 Runtime 是否有有效的 MovementTask 所有者。 */
	bool IsActionMovementOwned() const;

	/** 指定 Action 是否是当前 MovementTask 的唯一所有者。 */
	bool IsActionMovementOwnedBy(const FWeaponActionToken& ActionToken) const;

	// ----------------------------------------------------------------------
	// Motion Matching Handoff (M4.4)
	// ----------------------------------------------------------------------
	/**
	 * Publish one exact Action-owned exit payload for the AnimBP/Chooser.  The
	 * Host accepts it only while the action is still registered in this runtime.
	 * Publication itself never changes movement, tags, or root-motion ownership.
	 */
	bool PublishMotionMatchingHandoff(const FWeaponActionToken& ActionToken,
		FWeaponMotionMatchingHandoff Handoff);

	/** Read the current pending payload. It may outlive the source GA cleanup. */
	UFUNCTION(BlueprintPure, Category = "Movement|MM|Handoff")
	bool GetPendingMotionMatchingHandoff(FWeaponMotionMatchingHandoff& OutHandoff) const;

	/**
	 * Clear exactly the serial that the AnimBP/Chooser consumed.  A stale graph
	 * update cannot clear a newer handoff published in the same runtime.
	 */
	UFUNCTION(BlueprintCallable, Category = "Movement|MM|Handoff")
	bool ClearPendingMotionMatchingHandoff(int64 ExpectedSerial);

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

	/**
	 * Starts an in-place, CMC-owned free-fall visual and publishes the exact
	 * Falling tags.  The visual survives the source action; only HandleLanded
	 * is allowed to clear it.
	 */
	bool BeginAerialFalling(bool bEnhancedVariant, FGameplayTag StyleTag = FGameplayTag());

	/** 清除全部空中 Cant/Falling 拥有状态并落回 Grounded。 */
	void HandleLanded();

	/**
	 * 为「以自己的弧线抵达地面、因而从未有过系统托管自由落体」的动作播放落地表现。
	 *
	 * 与 HandleLanded 的分工：HandleLanded 走的是「系统托管过下落 → 落地收尾」这条
	 * 路，闸门是 AerialFalling 令牌；本函数供 BallisticVault 这类弧线自己落地的动作
	 * 直接认领落地姿势，不要求先经过 AerialFalling。所有权仍归 Host，调用方只断言
	 * 「这次触地值得一个落地姿势」。幂等：最终落到 PlayAerialLandingVisual，它会先
	 * 停掉上一个落地表现。
	 *
	 * 刻意不检查 bGrounded —— 唯一调用点上它合法地为 false。
	 */
	bool PlayAerialLandingPresentation();

	bool IsGrounded() const { return bGrounded; }

	/** True only while the Host owns the CMC free-fall presentation/state. */
	bool IsAerialFalling() const { return PoseTokens.AerialFalling.IsValid(); }

	/** True while the system-owned landing presentation locks locomotion input. */
	bool IsAerialLanding() const { return PoseTokens.AerialLanding.IsValid(); }

	/** Samples the Host-owned falling/landing visual instances without changing their state. */
	void GetAerialPresentationRootMotionTelemetry(
		FMHGZAerialPresentationRootMotionTelemetry& OutTelemetry) const;

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

	/** 在收刀/拔刀姿态切换时更新带 VisualComponentTag 的武器视觉组件。 */
	bool ApplyWeaponVisualAttachment(bool bInSheathed);

	USkeletalMeshComponent* FindWeaponVisualComponent() const;

	void ReleasePoseToken(FWeaponOwnedTagToken& Token);

	/** Stops only the system-owned fall montage, never an active action montage. */
	void StopAerialFallingVisual(float BlendOutTime);

	/** Stops only the system-owned landing montage and releases its pose lock. */
	void StopAerialLandingVisual(float BlendOutTime);

	/** Applies/restores the optional current-weapon CMC profile for free fall. */
	void ApplyAerialFallingPhysics(bool bEnhancedVariant);
	void RestoreAerialFallingPhysics();

	/** Plays the configured landing presentation with root motion explicitly disabled. */
	bool PlayAerialLandingVisual();

	void HandleAerialLandingMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	/** 清理当前 MovementTask 所有权；调用者已完成其精确身份校验。 */
	void ClearActionMovementOwner();

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
		FWeaponOwnedTagToken AerialLanding;
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
	bool bWeaponVisualAttachmentWarningIssued = false;

	FWeaponRuntimeTagLedger TagLedger;
	TArray<FWeaponActionToken> ActiveActions;
	TArray<FWeaponMontageRegistration> MontageRegistrations;
	FWeaponActionToken MontageRootMotionOwner;
	FWeaponActionToken ActionMovementOwner;
	FName ActionMovementWarpTargetName;
	uint32 NextActionMovementSerial = 1;
	FWeaponMotionMatchingHandoff PendingMotionMatchingHandoff;
	int64 NextMotionMatchingHandoffSerial = 1;
	FPoseTokens PoseTokens;

	/** Presentation owned by BeginAerialFalling and stopped only on landing/runtime teardown. */
	TWeakObjectPtr<UAnimMontage> ActiveAerialFallingMontage;
	bool bAerialFallingRootMotionDisabledByHost = false;

	/** Presentation owned by HandleLanded and released exactly when it ends. */
	TWeakObjectPtr<UAnimMontage> ActiveAerialLandingMontage;
	bool bAerialLandingRootMotionDisabledByHost = false;

	/** Saved only while the current weapon opted into ResolveAerialFallingPhysics. */
	bool bAerialFallingPhysicsOverridden = false;
	float SavedGravityScale = 1.0f;
	float SavedBrakingDecelerationFalling = 0.0f;

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
