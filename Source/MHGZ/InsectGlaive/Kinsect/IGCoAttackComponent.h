// Copyright MHGZ Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "IGCoAttackTypes.h"
#include "WeaponRuntime/MHGZWeaponRuntimeTypes.h"
#include "IGCoAttackComponent.generated.h"

class URes_InsectGlaive;
class USkeletalMeshComponent;

/** Resource-owned, post-pose sampler and sole cooperative movement writer. */
UCLASS()
class UIGCoAttackComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UIGCoAttackComponent();
	bool Start(URes_InsectGlaive* Resource, const FWeaponActionToken& Action,
		uint64 Serial, USkeletalMeshComponent* Weapon, const FIGCoAttackSource& Source,
		const FIGCoAttackTuning& Tuning, uint64 DescendingThrustHoverSerial = 0);
	void Stop();
	bool Join(const FWeaponActionToken& Action, USkeletalMeshComponent* Weapon,
		const FIGCoAttackSource& Source);
	bool Seal(const FWeaponActionToken& Action);
	void FinishAttackPath(const FWeaponActionToken& Action, bool bReturnAfterReplay);
	// A dead source weak pointer still needs one cleanup tick.
	bool IsRunning() const { return CommandSerial != 0; }
	const FWeaponActionToken& GetActionToken() const { return ActionToken; }
	uint64 GetCommandSerial() const { return CommandSerial; }
	bool IsTracking() const { return bTracking; }
	bool OpenWindow(const FWeaponActionToken& Action, const FIGCoAttackWindowSpec& Spec);
	void CloseWindow(const FWeaponActionToken& Action, uint64 Occurrence);
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
protected:
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	void HandleActionRegistered(const FWeaponActionToken& Action);
	void HandleDodgeAcceptWindowOpened(const FWeaponActionToken& Action);
	void HandleComboWindowsClosed(const FWeaponActionToken& Action);
	bool IsSessionCurrent(uint64 Serial) const;
	bool ReplayWindows(double From, double To, uint64 Serial);
	bool SweepWindow(uint64 Occurrence, const FVector& From, const FVector& To,
		const FVector& Forward, double BeginTime, double EndTime, uint64 Serial);
	struct FTargetBudget { int32 Hits = 0; double LastHit = -DBL_MAX; };
	struct FWindow
	{
		uint64 ReceiptID = 0; // stream-local identity, not the per-GA occurrence
		FWeaponActionToken SourceAction; // comparison only; never called during replay
		FIGCoAttackWindowSpec Spec;
		TOptional<double> CloseTime;
		bool bStarted = false;
		bool bSkipped = false;
		bool bQueriedOpen = false;
		TMap<TWeakObjectPtr<AActor>, FTargetBudget> Targets;
	};
	TArray<FWindow> Windows;
	uint64 NextReceiptID = 0;
	TOptional<double> SourceEndTime;
	FDelegateHandle ActionRegisteredHandle;
	FDelegateHandle DodgeAcceptOpenedHandle;
	FDelegateHandle ComboClosedHandle;
	TOptional<double> AttackPathEndTime;
	TOptional<FTransform> FrozenAttackPose;
	bool bReturnAfterAttackReplay = false;
	bool bRecoveryPathFrozen = false;
	TOptional<double> ReplayCursor;
	TOptional<double> TrackingSince;
	FVector ReplayForward = FVector::ForwardVector;
	TWeakObjectPtr<URes_InsectGlaive> Resource;
	TWeakObjectPtr<USkeletalMeshComponent> Weapon;
	TWeakObjectPtr<USkeletalMeshComponent> CharacterMesh;
	FWeaponActionToken ActionToken;
	uint64 CommandSerial = 0;
	FIGCoAttackSource Source;
	FIGCoAttackTuning Tuning;
	FIGCoAttackHistory History;
	bool bTracking = false;
	bool bEntryAtFirstSample = false;
};
