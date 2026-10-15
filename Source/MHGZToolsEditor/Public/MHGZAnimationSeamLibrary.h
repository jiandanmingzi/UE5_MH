// Copyright MHGZ Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MHGZAnimationSeamLibrary.generated.h"

class UAnimSequence;
class UAnimMontage;
class UAnimBlueprint;
class UPoseSearchDatabase;
class UPoseSearchSchema;

/** Editor asset authoring only; no runtime Montage or gameplay dependencies. */
UCLASS()
class MHGZTOOLSEDITOR_API UMHGZAnimationSeamLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** One 60Hz display frame of midpoint pose; root advances by adjacent mean velocity. Never overwrites. */
	UFUNCTION(BlueprintCallable, Category="MHGZ|Editor|Animation")
	static UAnimSequence* CreateOneFrameBridge(UAnimSequence* Before, float BeforeTime,
		UAnimSequence* After, float AfterTime, float BeforeRate, float AfterRate, const FString& OutputPackage);

	/** Insert after an exact segment, optionally replacing its next segment. Relinks existing windows/sections. */
	UFUNCTION(BlueprintCallable, Category="MHGZ|Editor|Animation")
	static bool InsertBridge(UAnimMontage* Montage, int32 BeforeSegmentIndex,
		UAnimSequence* Bridge, bool bReplaceNextSegment);

	/** Trim only the start of one segment; maintain source-relative notify timing. */
	UFUNCTION(BlueprintCallable, Category="MHGZ|Editor|Animation")
	static bool TrimSegmentStart(UAnimMontage* Montage, int32 SegmentIndex, float NewSourceStartTime);

	/** Repair cached composite length from existing segments; never inserts or trims. */
	UFUNCTION(BlueprintCallable, Category="MHGZ|Editor|Animation")
	static bool RefreshMontageLength(UAnimMontage* Montage);

	/** Audit or idempotently insert Inertialization after both action Slots. Does not save. */
	UFUNCTION(BlueprintCallable, Category="MHGZ|Editor|Animation")
	static bool InstallMontageSegmentInertialization(UAnimBlueprint* Blueprint, bool bApply);

	/** Graph/state export. Optional editor-open fixups are memory-only; use an isolated commandlet, never save. */
	UFUNCTION(BlueprintCallable, Category="MHGZ|Editor|Animation")
	static FString AuditAnimBlueprintGraph(UAnimBlueprint* Blueprint, bool bSimulateEditorOpen = false);

	/** Persistable repair of missing node transaction flags only; caller compiles/saves explicitly. */
	UFUNCTION(BlueprintCallable, Category="MHGZ|Editor|Animation")
	static int32 RepairAnimBlueprintTransactionalFlags(UAnimBlueprint* Blueprint);

	/** Existing two-node locomotion graph only. Keeps both live players; optionally moves its collector to final pose. No save. */
	UFUNCTION(BlueprintCallable, Category="MHGZ|Editor|Animation")
	static bool ConfigureLocomotionSeams(UAnimBlueprint* Blueprint, bool bCollectFinalPose);

	/** Persistent Loop-only candidate bank copied from an explicit template. Never overwrites or saves. */
	UFUNCTION(BlueprintCallable, Category="MHGZ|Editor|Animation")
	static UPoseSearchDatabase* CreateLoopOnlyDatabase(UPoseSearchDatabase* Template,
		UAnimSequence* Sequence, const FString& OutputPackage);

	/** Isolated schema copy for Loop banks; ordinary Start/Stop schema stays unchanged. Never saves. */
	UFUNCTION(BlueprintCallable, Category="MHGZ|Editor|Animation")
	static UPoseSearchSchema* CreateLoopPhaseSchema(UPoseSearchSchema* Template, const FString& OutputPackage);

	/** Narrow indexed phase and cooked feet on a dedicated Loop schema. No save. */
	UFUNCTION(BlueprintCallable, Category="MHGZ|Editor|Animation")
	static bool ConfigureLoopContinuitySchema(UPoseSearchSchema* Schema, bool bUnsheathed);

	/** Close only the boundary windows of an explicit locomotion Loop in place; preserve length/root displacement. No save. */
	UFUNCTION(BlueprintCallable, Category="MHGZ|Editor|Animation")
	static bool CloseLocomotionLoop(UAnimSequence* Sequence, float WindowSeconds = 0.1f);

	/** Audit/idempotently insert two MM-local inertial nodes. No unrelated graph changes or save. */
	UFUNCTION(BlueprintCallable, Category="MHGZ|Editor|Animation")
	static bool InstallLocomotionInertialization(UAnimBlueprint* Blueprint, bool bApply);

	/** Explicit single-slot recipe, independent of locomotion; relinks or clones notify events. No save. */
	UFUNCTION(BlueprintCallable, Category="MHGZ|Editor|Animation")
	static bool RebuildSingleSlotMontage(UAnimMontage* Montage, const FString& RecipeJson);

	/** Opt in to cooked segment-blend metadata; does not alter clips or save. */
	UFUNCTION(BlueprintCallable, Category="MHGZ|Editor|Animation")
	static bool SetMontageSegmentBlend(UAnimMontage* Montage, float Duration);

	/** Preserve all animation tracks/rate; enable Root Motion on a new persistent copy, never overwrite. */
	UFUNCTION(BlueprintCallable, Category="MHGZ|Editor|Animation")
	static UAnimSequence* CreateRootMotionCopy(UAnimSequence* Source, const FString& OutputPackage);
};
