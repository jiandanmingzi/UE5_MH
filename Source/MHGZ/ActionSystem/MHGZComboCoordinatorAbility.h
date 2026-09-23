// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MHGZGameplayAbility.h"
#include "MHGZAirDodgeAbility.h"
#include "MHGZWeaponComboData.h"
#include "MHGZComboCoordinatorAbility.generated.h"

class UMHGZWeaponComboData;
class UMHGZWeaponRuntimeHostComponent;

/** Maintained, no-cost weapon FSM. Physical input never enters this class. */
UCLASS(BlueprintType, Blueprintable)
class UGA_WeaponComboCoordinator : public UMHGZGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_WeaponComboCoordinator();

	UPROPERTY(BlueprintReadOnly, Category = "MHGZ|Combo")
	FName CurrentState = FName(TEXT("Idle"));

	void InjectComboData(UMHGZWeaponComboData* Data);
	void HandleWeaponInput(const FWeaponInputSnapshot& Input);

	bool ConfirmTransitionActivation(const FWeaponActionToken& ActionToken);
	void RejectTransitionActivation(const FWeaponActionToken& ActionToken);
	void OnAttackHit(const FWeaponActionToken& ActionToken);
	void OnActionFinished(const FWeaponActionToken& ActionToken, EWeaponActionEndReason Reason);
	bool OnAutoTransition(FName TransitionID, const FWeaponActionToken& SourceAction);
	void OnLanded(const FHitResult& Hit);
	void ResetCombo(EWeaponActionEndReason Reason);

	bool OpenComboWindow(const FWeaponActionToken& ActionToken, FName NotifyEventID);
	void CloseComboWindow(const FWeaponActionToken& ActionToken, FName NotifyEventID);

	/** Attack-side DodgeAccept Notify calls this after its exact gate is live. */
	void OnDodgeAcceptWindowOpened(const FWeaponActionToken& ActionToken);

	/** A non-combo Action finished Commit; its new action boundary discards preinput. */
	void OnDirectActionConfirmed();

	/**
	 * Direct (non-Input.Weapon) actions may opt into the shared one-slot preinput
	 * buffer after their normal activation attempt failed solely because their
	 * action-side accept window is not open.  Dodge is the current direct user.
	 */
	bool TryBufferDirectInput(const FWeaponInputSnapshot& Input);

	/** Exact active attack gate used by the direct/core Dodge ability. */
	bool CanDodgeSupersedeActiveAction() const;
	bool PrepareActiveActionForDodge(const FWeaponActionToken& DodgeActionToken);
	bool CommitActiveActionDodgeSupersede(const FWeaponActionToken& DodgeActionToken);
	void CancelActiveActionDodgeSupersede(const FWeaponActionToken& DodgeActionToken);

	// ── M5 空中回避：可操作帧闸门 + 让位 ─────────────────────────────────────
	//
	// 与上面地面 DodgeAccept 那套**并列**、互不借用：空中走点通知的 latch
	// （UAnimNotify_IG_AerialHandoff → NotifyAerialHandoff），地面走 NotifyState 的窗口。
	// **空中回避没有预输入**（2026-09-22 用户拍板取消）：锁定期里的按下直接作废。

	/**
	 * 让位：把**其它**已 Commit 的 IG 动作以 `Superseded` 结束。返回结束个数。
	 * `RequestEndAction` 同步摘源、释放位移所有权（UnregisterAction + TaskOwnerEnded），
	 * 所以调用方随后 AcquireActionMovement 必然成功。
	 */
	int32 SupersedeOtherIGAerialActions(const FWeaponActionToken& Superseder);

	UFUNCTION(BlueprintCallable, Category = "MHGZ|Combo")
	FName GetCurrentState() const { return CurrentState; }

	const TOptional<FActiveComboTransition>& GetActiveTransition() const { return ActiveTransition; }

	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

private:
	struct FComboWindowEntry
	{
		FWeaponActionToken ActionToken;
		FWeaponOwnedTagToken TagToken;
	};

	struct FBufferedCombatInput
	{
		FWeaponInputSnapshot Snapshot;
		double ExpireAt = 0.0;
	};

	UPROPERTY()
	TObjectPtr<UMHGZWeaponComboData> ComboData;

	TMap<FName, TArray<int32>> StateIndex;
	TArray<int32> AnyStateIndices;
	TMap<FName, int32> TransitionIndex;
	TOptional<FPendingComboTransition> PendingTransition;
	TOptional<FActiveComboTransition> ActiveTransition;
	FWeaponOwnedTagToken ActiveTransitionTagToken;
	TMap<FName, FComboWindowEntry> ComboWindows;
	TOptional<FBufferedCombatInput> BufferedCombatInput;
	FTimerHandle ComboTimeoutTimer;

	void BuildIndices();
	const FComboTransition* FindTransition(FName TransitionID) const;
	const FComboTransition* FindBestMatch(const FWeaponInputSnapshot& Input,
		bool bIgnoreAcceptWindows = false) const;
	bool TransitionRequirementsPass(const FComboTransition& Transition,
		const FWeaponInputSnapshot& Input, bool bIgnoreAcceptWindows = false) const;
	bool ExecuteTransition(const FComboTransition& Transition,
		const FWeaponInputSnapshot& Input, const FWeaponActionToken* SourceAction = nullptr);
	bool ExecuteStateOnlyTransition(const FComboTransition& Transition,
		const FWeaponActionToken& SourceAction);
	bool DoesTransitionMatchState(const FComboTransition& Transition) const;
	bool IsSnapshotPostureCompatible(const FComboTransition& Transition,
		const FWeaponInputSnapshot& Input) const;
	bool HasOpenWindowFor(const FWeaponActionToken& ActionToken) const;
	bool HasOpenDodgeAcceptWindowFor(const FWeaponActionToken& ActionToken) const;
	void GrantActiveTransitionTags(const FComboTransition& Transition);
	void ReleaseActiveTransitionTags();
	void CloseWindowsFor(const FWeaponActionToken& ActionToken);
	bool CacheCombatInput(const FWeaponInputSnapshot& Input);
	bool HasLiveBufferedCombatInput();
	void ClearBufferedCombatInput();
	void TryConsumeBufferedCombatInput(bool bAllowDirectInput);
	void ResetComboTimeout();
	void OnComboTimeout();
	UMHGZWeaponRuntimeHostComponent* GetRuntimeHost() const;
	static FName MakeWindowKey(const FWeaponActionToken& ActionToken, FName NotifyEventID);

protected:
	/** The coordinator is persistent infrastructure; its transitions still obey locks. */
	virtual bool ShouldIgnorePlayerActionLocks() const override { return true; }
};
