// Copyright MHGZ Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "WeaponRuntime/MHGZWeaponRuntimeTypes.h"
#include "MHGZWeaponInputRouterComponent.generated.h"

class AMHGZCharacter;
class APawn;
class APlayerController;
class UMHGZAbilitySystemComponent;
class UWeaponInputProfile;
struct FInputActionInstance;
struct FWeaponChordDefinition;

/**
 * UMHGZWeaponInputRouterComponent - raw physical input -> immutable input facts.
 *
 * Mounted on the local PlayerController. It resolves raw physical tags into
 * FWeaponInputSnapshot events (single fallback or chord output), applies the
 * chord grace rules, freezes direction/pose/held-modifier/aim context at the
 * moment of resolution, owns release identities (SourceControlTag + SequenceID),
 * and forwards every resolved snapshot to the ASC. It never selects abilities.
 */
UCLASS(ClassGroup = (MHGZ), BlueprintType, meta = (BlueprintSpawnableComponent))
class UMHGZWeaponInputRouterComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMHGZWeaponInputRouterComponent();

	/** Native observable: every emitted snapshot (Started, chord, Completed release). */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnWeaponInputSnapshotResolved, const FWeaponInputSnapshot&);
	DECLARE_MULTICAST_DELEGATE(FOnWeaponInputProfileChanged);

	void SetInputProfile(UWeaponInputProfile* InProfile);
	UWeaponInputProfile* GetInputProfile() const { return CurrentProfile; }

	/** Re-points the router at the possessed pawn (idempotent; clears state on change). */
	void AttachToPawn(APawn* InPawn);

	/** Idempotent teardown: releases aim tokens, drops held/pending/release state. */
	void ShutdownRouter();

	/** Enhanced Input entry points (bound by UMHGZInputComponent). */
	void HandleRawInputStarted(const FInputActionInstance& Instance);
	void HandleRawInputTriggered(const FInputActionInstance& Instance);
	void HandleRawInputCompleted(const FInputActionInstance& Instance);

	/** Testable core: raw physical tag callbacks with an explicit "now". */
	void HandlePhysicalStarted(FGameplayTag PhysicalTag, double Now);
	void HandlePhysicalCompleted(FGameplayTag PhysicalTag, double Now);
	void FlushExpiredInputs(double Now);

	/** Observable snapshot stream (used by tests and tooling). */
	FOnWeaponInputSnapshotResolved OnInputSnapshotResolved;
	FOnWeaponInputProfileChanged OnInputProfileChanged;

	/** Direction classifier relative to the character's forward/right (pure, testable). */
	static EDirectionalInput ClassifyDirection(
		const FVector& WorldDirection,
		const FVector& CharacterForward,
		const FVector& CharacterRight,
		float InputMagnitude,
		float Threshold,
		float ConeHalfAngleDegrees);

	/** Bounded capture of every emitted snapshot, most recent last. */
	const TArray<FWeaponInputSnapshot>& GetCapturedSnapshots() const { return CapturedSnapshots; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** Per physical key press state; one entry per held control. */
	struct FPhysicalInputState
	{
		double StartedTime = 0.0;
		double LastTriggeredTime = 0.0;
		uint32 SequenceID = 0;
		bool bSingleEmitted = false;   // fallback single already emitted
		bool bSingleConsumed = false;  // fallback single suppressed by a chord
		FWeaponInputSnapshot FrozenSingleSnapshot;
	};

	/** A complete chord whose emission is postponed until its window closes or a bigger chord wins. */
	struct FDeferredChord
	{
		int32 ChordIndex = INDEX_NONE;
		double CompletionTime = 0.0;   // moment the last required member made it complete
		double FirstTriggerTime = 0.0; // earliest started trigger; window = FirstTriggerTime + Grace
		FWeaponInputSnapshot FrozenSnapshot;
	};

	/** Release identity registered under a physical control tag. */
	struct FReleaseRegistration
	{
		FGameplayTag ResolvedInputTag;
		FGameplayTag SourceControlTag;
		uint32 SequenceID = 0;
		FWeaponInputSnapshot Snapshot;
	};

	/** Precomputed chord facts for the current profile. */
	struct FChordInfo
	{
		const FWeaponChordDefinition* Definition = nullptr;
		TArray<FGameplayTag> Members; // triggers followed by modifiers
		TSet<FGameplayTag> TriggerSet;
		TSet<FGameplayTag> ModifierSet;
		int32 TriggerCount = 0;
		int32 ModifierCount = 0;
		bool bMultiMember = false;
	};

	static bool IsBetterChord(const FChordInfo& A, const FChordInfo& B);
	static bool IsTriggerSubset(const FChordInfo& Sub, const FChordInfo& Super);

	void RebuildChordCache();
	void EvaluateChords(double Now);
	bool IsChordComplete(int32 ChordIndex) const;
	bool IsPossiblyCompletable(int32 ChordIndex, double Now) const;
	double GetFirstTriggerTime(int32 ChordIndex) const;
	FGameplayTag GetLastMemberTag(int32 ChordIndex) const;
	FWeaponInputSnapshot BuildSnapshot(
		const FGameplayTag& ResolvedTag,
		const FGameplayTag& SourceTag,
		uint32 SequenceID,
		EWeaponInputPhase Phase,
		double Timestamp,
		EWeaponAimSnapshotContext AimContext = EWeaponAimSnapshotContext::None) const;
	FWeaponInputSnapshot BuildChordSnapshot(int32 ChordIndex, double Now);
	void EmitChord(int32 ChordIndex, double Now);
	void RegisterRelease(const FWeaponInputSnapshot& Snapshot, const FGameplayTag& ControlTag);
	void EmitSnapshot(const FWeaponInputSnapshot& Snapshot);
	uint32 AllocateSequenceID();

	void RecomputeAimChildTags();
	void ReleaseAllAimChildTokens();
	void SubscribePoseTags();
	void UnsubscribePoseTags();
	void OnPoseTagChanged(const FGameplayTag Tag, int32 NewCount);

	UPROPERTY()
	TObjectPtr<UWeaponInputProfile> CurrentProfile;

	UPROPERTY()
	TWeakObjectPtr<APlayerController> OwnerPC;

	UPROPERTY()
	TWeakObjectPtr<AMHGZCharacter> CachedCharacter;

	UPROPERTY()
	TWeakObjectPtr<UMHGZAbilitySystemComponent> CachedASC;

	TMap<FGameplayTag, FPhysicalInputState> HeldControls;
	TArray<FChordInfo> Chords;
	TArray<bool> ResolvedChords;
	TMap<int32, FDeferredChord> DeferredChords;
	TSet<FGameplayTag> ConfiguredModifierTags;
	TSet<FGameplayTag> DelayedTags;
	TMap<FGameplayTag, TArray<FReleaseRegistration>> ReleaseRegistry;
	TArray<FWeaponInputSnapshot> CapturedSnapshots;
	TArray<FGameplayTag> AimChildTags;
	TArray<FWeaponOwnedTagToken> AimChildTokens;
	TMap<FGameplayTag, FDelegateHandle> PoseTagEventHandles;
	uint32 NextSequenceID = 1;
	bool bAcceptingInput = true;
};
