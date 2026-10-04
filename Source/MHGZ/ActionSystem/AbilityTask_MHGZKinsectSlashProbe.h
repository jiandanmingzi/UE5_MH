// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/Tasks/AbilityTask.h"
#include "Engine/HitResult.h"
#include "GameplayTagContainer.h"
#include "WeaponRuntime/MHGZWeaponRuntimeTypes.h"
#include "AbilityTask_MHGZKinsectSlashProbe.generated.h"

class ACharacter;
class USkeletalMeshComponent;
class UMHGZGameplayAbility;
class UMHGZWeaponRuntimeHostComponent;
class URes_InsectGlaive;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMHGZKinsectSlashProbeHitzoneContact,
	FWeaponActionToken, ActionToken, FHitResult, Hit);

/**
 * Action-owned, read-only Kinsect appearance probe for the Insect Glaive slash.
 * The live Kinsect keeps its runtime state and movement; this task owns only a
 * transient visual clone and a swept sphere query along the Hunter's path.
 */
UCLASS()
class MHGZ_API UAbilityTask_MHGZKinsectSlashProbe : public UAbilityTask
{
	GENERATED_BODY()

public:
	UAbilityTask_MHGZKinsectSlashProbe(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(BlueprintAssignable)
	FMHGZKinsectSlashProbeHitzoneContact OnHitzoneContact;

	static UAbilityTask_MHGZKinsectSlashProbe* StartProbe(
		UMHGZGameplayAbility* OwningAbility,
		URes_InsectGlaive* Resource,
		const FWeaponActionToken& ActionToken,
		FVector LaunchDirection,
		float ForwardOffset,
		float Radius,
		float MaxSampleStep,
		FGameplayTag HitzoneQueryTag,
		float LateHitWindowSeconds,
		bool bDrawDebug);

	virtual void Activate() override;
	virtual void TickTask(float DeltaTime) override;
	virtual void OnDestroy(bool AbilityIsEnding) override;

	bool BeginDash(const FWeaponActionToken& Token);
	bool NotifyDashMovementFinished(const FWeaponActionToken& Token,
		EWeaponMovementEndReason Reason, FVector AnalyticTerminalVelocity);
	bool OpenLateHitWindow(const FWeaponActionToken& Token);
	bool CloseLateHitWindow(const FWeaponActionToken& Token);
	bool DisableAttackSweep(const FWeaponActionToken& Token);
	bool AcknowledgeSuccessfulContact(const FWeaponActionToken& Token);
	bool EndVisual(const FWeaponActionToken& Token);
	bool DidStartProbe() const;

private:
	#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
	friend class FMHGZM5KinsectSlashProbeSweepTest;
	#endif

	enum class ESweepSegmentResult : uint8
	{
		Continue,
		AcceptedContact,
		WorldBlocked,
		OwnerLost
	};

	bool IsCurrentOwnerContext() const;
	bool ValidateCurrentOwner();
	bool ValidateMethodToken(const FWeaponActionToken& Token);
	void AbortForOwnershipLoss();
	void WarnOnce(const TCHAR* Message);
	bool CreateVisualProxy(ACharacter* Character);
	void UpdateDashProbePosition();
	bool SweepProbePath(const FVector& Destination, FVector& OutResolvedLocation);
	ESweepSegmentResult SweepProbeSegment(const FVector& Start, const FVector& End,
		FVector& OutResolvedLocation);
	void TryEnterLateHitMode();
	void DisableSweepPermanently();
	void DestroyVisualProxyAndRestoreKinsect();
	void Cleanup();

	TWeakObjectPtr<UMHGZGameplayAbility> OwnerAbility;
	TWeakObjectPtr<URes_InsectGlaive> Resource;
	TWeakObjectPtr<UMHGZWeaponRuntimeHostComponent> RuntimeHost;
	TWeakObjectPtr<ACharacter> Character;
	TWeakObjectPtr<USkeletalMeshComponent> LiveKinsectMesh;
	TWeakObjectPtr<USkeletalMeshComponent> VisualProxy;

	UPROPERTY()
	FWeaponActionToken ActionToken;

	FVector LaunchDirection = FVector::ForwardVector;
	FVector LastProbeLocation = FVector::ZeroVector;
	FVector LateTerminalVelocity = FVector::ZeroVector;
	float ForwardOffset = 0.0f;
	float Radius = 0.0f;
	float MaxSampleStep = 0.0f;
	float LateHitWindowSeconds = 0.0f;
	float LateHitElapsedSeconds = 0.0f;
	FGameplayTag HitzoneQueryTag;
	bool bDrawDebug = false;
	bool bStarted = false;
	bool bDashActive = false;
	bool bDashMovementFinished = false;
	bool bLateWindowRequested = false;
	bool bLateWindowClosed = false;
	bool bLateHitModeActive = false;
	bool bSweepEnabled = false;
	bool bSweepPermanentlyDisabled = false;
	bool bVisualEnded = false;
	bool bHasOriginalKinsectVisibility = false;
	bool bOriginalKinsectVisible = true;
	bool bCleanupDone = false;
	bool bWarningIssued = false;
	bool bDispatchingContact = false;
	bool bContactAccepted = false;
};
