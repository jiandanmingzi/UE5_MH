// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MHGZGameplayAbility.h"
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

	UPROPERTY()
	TObjectPtr<UMHGZWeaponComboData> ComboData;

	TMap<FName, TArray<int32>> StateIndex;
	TArray<int32> AnyStateIndices;
	TMap<FName, int32> TransitionIndex;
	TOptional<FPendingComboTransition> PendingTransition;
	TOptional<FActiveComboTransition> ActiveTransition;
	FWeaponOwnedTagToken ActiveTransitionTagToken;
	TMap<FName, FComboWindowEntry> ComboWindows;
	FTimerHandle ComboTimeoutTimer;

	void BuildIndices();
	const FComboTransition* FindTransition(FName TransitionID) const;
	const FComboTransition* FindBestMatch(const FWeaponInputSnapshot& Input) const;
	bool TransitionRequirementsPass(const FComboTransition& Transition,
		const FWeaponInputSnapshot& Input) const;
	bool ExecuteTransition(const FComboTransition& Transition,
		const FWeaponInputSnapshot& Input, const FWeaponActionToken* SourceAction = nullptr);
	bool ExecuteStateOnlyTransition(const FComboTransition& Transition,
		const FWeaponActionToken& SourceAction);
	bool DoesTransitionMatchState(const FComboTransition& Transition) const;
	bool IsSnapshotPostureCompatible(const FComboTransition& Transition,
		const FWeaponInputSnapshot& Input) const;
	bool HasOpenWindowFor(const FWeaponActionToken& ActionToken) const;
	void GrantActiveTransitionTags(const FComboTransition& Transition);
	void ReleaseActiveTransitionTags();
	void CloseWindowsFor(const FWeaponActionToken& ActionToken);
	void ResetComboTimeout();
	void OnComboTimeout();
	UMHGZWeaponRuntimeHostComponent* GetRuntimeHost() const;
	static FName MakeWindowKey(const FWeaponActionToken& ActionToken, FName NotifyEventID);
};
