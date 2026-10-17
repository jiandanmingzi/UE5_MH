// Copyright MHGZ Project. All Rights Reserved.
#include "MHGZAnimationSeamLibrary.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/MHGZMotionMatchingAnimInstance.h"
#include "AnimationGraphSchema.h"
#include "AnimGraphNode_Inertialization.h"
#include "AnimGraphNode_LocalToComponentSpace.h"
#include "AnimGraphNode_ComponentToLocalSpace.h"
#include "AnimGraphNode_CopyBone.h"
#include "Animation/Skeleton.h"
#include "EdGraphNode_Comment.h"
#include "K2Node_VariableGet.h"
#include "AnimGraphNode_Slot.h"
#include "AnimGraphNode_MotionMatching.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_PoseSearchHistoryCollector.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "Animation/MHGZPoseSearchFeatureChannel_LoopPhase.h"
#include "Animation/AnimSequence.h"
#include "Misc/PackageName.h"
#include "UObject/UnrealType.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphUtilities.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "K2Node.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

namespace
{
UEdGraphPin* PosePin(UEdGraphNode& Node, EEdGraphPinDirection Direction,
	UScriptStruct* PoseType = FPoseLink::StaticStruct())
{
	for (UEdGraphPin* Pin : Node.Pins)
	{
		if (Pin && Pin->Direction == Direction
			&& Pin->PinType.PinSubCategoryObject.Get() == PoseType) { return Pin; }
	}
	return nullptr;
}

FString Marker(FName SlotName) { return TEXT("MHGZ.MontageSegmentBlend.") + SlotName.ToString(); }
}

FString UMHGZAnimationSeamLibrary::AuditAnimBlueprintGraph(UAnimBlueprint* Blueprint, bool bSimulateEditorOpen)
{
	if (!Blueprint) { return TEXT("{}"); }
	TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
	Report->SetStringField(TEXT("asset"), Blueprint->GetPathName());
	Report->SetNumberField(TEXT("status"), static_cast<int32>(Blueprint->Status));
	Report->SetBoolField(TEXT("package_dirty"), Blueprint->GetOutermost()->IsDirty());
	Report->SetBoolField(TEXT("recompile_on_load"), Blueprint->bRecompileOnLoad);
	if (bSimulateEditorOpen)
	{
		TArray<UK2Node*> AllNodes;
		FBlueprintEditorUtils::GetAllNodesOfClass(Blueprint, AllNodes);
		TArray<TSharedPtr<FJsonValue>> MissingFlags;
		for (UK2Node* Node : AllNodes)
		{
			if (Node && !Node->HasAnyFlags(RF_Transactional))
			{
				MissingFlags.Add(MakeShared<FJsonValueString>(Node->GetPathName()));
			}
		}
		Report->SetArrayField(TEXT("non_transactional_nodes"), MissingFlags);
		// Mirror the non-UI part of FBlueprintEditor::EnsureBlueprintIsUpToDate.
		// The caller must discard this process without saving the loaded package.
		TArray<TSharedPtr<FJsonValue>> Steps;
		auto Step = [&](const TCHAR* Name, TFunctionRef<void()> Fixup)
		{
			Fixup();
			TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("step"), Name);
			Row->SetNumberField(TEXT("status"), static_cast<int32>(Blueprint->Status));
			Row->SetBoolField(TEXT("package_dirty"), Blueprint->GetOutermost()->IsDirty());
			Steps.Add(MakeShared<FJsonValueObject>(Row));
		};
		Step(TEXT("PurgeNullGraphs"), [&] { FBlueprintEditorUtils::PurgeNullGraphs(Blueprint); });
		Step(TEXT("UpgradeCosmeticallyStaleBlueprint"), [&] { FKismetEditorUtilities::UpgradeCosmeticallyStaleBlueprint(Blueprint); });
		Step(TEXT("ConformCallsToParentFunctions"), [&] { FBlueprintEditorUtils::ConformCallsToParentFunctions(Blueprint); });
		Step(TEXT("ConformImplementedEvents"), [&] { FBlueprintEditorUtils::ConformImplementedEvents(Blueprint); });
		Step(TEXT("ConformImplementedInterfaces"), [&] { FBlueprintEditorUtils::ConformImplementedInterfaces(Blueprint); });
		Step(TEXT("UpdateOutOfDateCompositeNodes"), [&] { FBlueprintEditorUtils::UpdateOutOfDateCompositeNodes(Blueprint); });
		Step(TEXT("UpdateTransactionalFlags"), [&] { FBlueprintEditorUtils::UpdateTransactionalFlags(Blueprint); });
		Report->SetArrayField(TEXT("open_fixups"), Steps);
	}
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	TArray<TSharedPtr<FJsonValue>> Exports;
	for (UEdGraph* Graph : Graphs)
	{
		if (!Graph) { continue; }
		TSet<UObject*> Nodes;
		for (UEdGraphNode* Node : Graph->Nodes) { if (Node) { Nodes.Add(Node); } }
		FString Text;
		FEdGraphUtilities::ExportNodesToText(Nodes, Text);
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Graph->GetName());
		Entry->SetStringField(TEXT("text"), Text);
		Exports.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Report->SetArrayField(TEXT("graphs"), Exports);
	FString Json;
	FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
	return Json;
}

int32 UMHGZAnimationSeamLibrary::RepairAnimBlueprintTransactionalFlags(UAnimBlueprint* Blueprint)
{
	if (!Blueprint) { return INDEX_NONE; }
	TArray<UK2Node*> Nodes;
	FBlueprintEditorUtils::GetAllNodesOfClass(Blueprint, Nodes);
	int32 Repaired = 0;
	for (UK2Node* Node : Nodes)
	{
		if (Node && !Node->HasAnyFlags(RF_Transactional))
		{
			Node->SetFlags(RF_Transactional);
			++Repaired;
		}
	}
	if (Repaired > 0)
	{
		// UpdateTransactionalFlags only dirties Blueprint::Status. Explicitly dirty
		// the package so the repaired flags survive the next editor session.
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		Blueprint->MarkPackageDirty();
	}
	return Repaired;
}

UPoseSearchDatabase* UMHGZAnimationSeamLibrary::CreateLoopOnlyDatabase(
	UPoseSearchDatabase* Template, UAnimSequence* Sequence, const FString& OutputPackage)
{
	if (!Template || !Template->Schema || !Sequence || !Sequence->bLoop
		|| !OutputPackage.StartsWith(TEXT("/Game/"))
		|| !FPackageName::IsValidLongPackageName(OutputPackage)
		|| FPackageName::DoesPackageExist(OutputPackage) || FindPackage(nullptr, *OutputPackage))
	{ return nullptr; }
	UPackage* Package = CreatePackage(*OutputPackage);
	UPoseSearchDatabase* Database = DuplicateObject<UPoseSearchDatabase>(Template, Package,
		*FPackageName::GetLongPackageAssetName(OutputPackage));
	Database->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
	while (Database->GetNumAnimationAssets() > 0)
	{ Database->RemoveAnimationAssetAt(Database->GetNumAnimationAssets() - 1); }
	FPoseSearchDatabaseSequence Entry;
	Entry.Sequence = Sequence;
	Entry.SetDisableReselection(true);
	Entry.MirrorOption = EPoseSearchMirrorOption::UnmirroredOnly;
	Database->AddAnimationAsset(FInstancedStruct::Make(Entry));
	Database->PostEditChange();
	Database->MarkPackageDirty();
	return Database;
}

UPoseSearchSchema* UMHGZAnimationSeamLibrary::CreateLoopPhaseSchema(
	UPoseSearchSchema* Template, const FString& OutputPackage)
{
	if (!Template || !OutputPackage.StartsWith(TEXT("/Game/"))
		|| !FPackageName::IsValidLongPackageName(OutputPackage)
		|| FPackageName::DoesPackageExist(OutputPackage) || FindPackage(nullptr, *OutputPackage))
	{ return nullptr; }
	UPackage* Package = CreatePackage(*OutputPackage);
	UPoseSearchSchema* Schema = DuplicateObject<UPoseSearchSchema>(Template, Package,
		*FPackageName::GetLongPackageAssetName(OutputPackage));
	Schema->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
	Schema->AddChannel(NewObject<UMHGZPoseSearchFeatureChannel_LoopPhase>(Schema, NAME_None, RF_Transactional));
	Schema->PostEditChange();
	Schema->MarkPackageDirty();
	return Schema;
}

bool UMHGZAnimationSeamLibrary::ConfigureLocomotionSeams(UAnimBlueprint* Blueprint, bool bCollectFinalPose)
{
	if (!Blueprint || !Blueprint->ParentClass
		|| !Blueprint->ParentClass->IsChildOf(UMHGZMotionMatchingAnimInstance::StaticClass())) { return false; }
	TArray<UAnimGraphNode_MotionMatching*> MMNodes;
	FBlueprintEditorUtils::GetAllNodesOfClass(Blueprint, MMNodes);
	TArray<UAnimGraphNode_PoseSearchHistoryCollector*> Histories;
	FBlueprintEditorUtils::GetAllNodesOfClass(Blueprint, Histories);
	TArray<UAnimGraphNode_Root*> Roots;
	FBlueprintEditorUtils::GetAllNodesOfClass(Blueprint, Roots);
	// BlendStackResult derives from Root too; its per-sample graphs are not
	// the final AnimGraph output and must never be rewired as such.
	Roots.RemoveAll([](const UAnimGraphNode_Root* Root)
	{ return !Root->GetGraph() || Root->GetGraph()->GetFName() != TEXT("AnimGraph"); });
	if (MMNodes.Num() != 2 || Histories.Num() != 1 || Roots.Num() != 1) { return false; }
	UEdGraphPin* Source = PosePin(*Histories[0], EGPD_Input);
	UEdGraphPin* Output = PosePin(*Histories[0], EGPD_Output);
	UEdGraphPin* RootInput = PosePin(*Roots[0], EGPD_Input);
	if (!Source || !Output || !RootInput || Source->LinkedTo.Num() != 1
		|| Output->LinkedTo.Num() != 1 || RootInput->LinkedTo.Num() != 1) { return false; }
	const UEdGraphSchema* Schema = Histories[0]->GetGraph()->GetSchema();
	if (!Schema) { return false; }
	Blueprint->Modify();
	for (UAnimGraphNode_MotionMatching* Node : MMNodes)
	{
		Node->Modify();
		const FStructProperty* Property = FindFProperty<FStructProperty>(Node->GetClass(), TEXT("Node"));
		if (!Property || Property->Struct != FAnimNode_MotionMatching::StaticStruct()) { return false; }
		Property->ContainerPtrToValuePtr<FAnimNode_MotionMatching>(Node)->SetMaxActiveBlends(2);
	}
	if (bCollectFinalPose && RootInput->LinkedTo[0] != Output)
	{
		UEdGraphPin* OldSource = Source->LinkedTo[0];
		UEdGraphPin* OldConsumer = Output->LinkedTo[0];
		UEdGraphPin* FinalPose = RootInput->LinkedTo[0];
		Source->BreakLinkTo(OldSource);
		Output->BreakLinkTo(OldConsumer);
		RootInput->BreakLinkTo(FinalPose);
		if (!Schema->TryCreateConnection(OldSource, OldConsumer)
			|| !Schema->TryCreateConnection(FinalPose, Source)
			|| !Schema->TryCreateConnection(Output, RootInput)) { return false; }
		Histories[0]->NodePosX = Roots[0]->NodePosX - 250;
		Histories[0]->NodePosY = Roots[0]->NodePosY;
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	return Blueprint->Status != BS_Error;
}

bool UMHGZAnimationSeamLibrary::InstallMontageSegmentInertialization(UAnimBlueprint* Blueprint, bool bApply)
{
	if (!Blueprint || !Blueprint->ParentClass
		|| !Blueprint->ParentClass->IsChildOf(UMHGZMotionMatchingAnimInstance::StaticClass())) { return false; }
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	UEdGraph** Found = Graphs.FindByPredicate([](UEdGraph* Graph)
		{ return Graph && Graph->GetFName() == TEXT("AnimGraph"); });
	if (!Found) { return false; }
	UEdGraph& Graph = **Found;
	TArray<UAnimGraphNode_Slot*> Slots;
	Graph.GetNodesOfClass(Slots);
	// Validate both action paths before editing. Partial/foreign wiring is not repaired by guessing.
	TArray<TPair<UAnimGraphNode_Slot*, UAnimGraphNode_Inertialization*>> Paths;
	for (FName SlotName : { FName(TEXT("DefaultSlot")), FName(TEXT("UpperBody_IGAction")) })
	{
		TArray<UAnimGraphNode_Slot*> Matches = Slots.FilterByPredicate([SlotName](const UAnimGraphNode_Slot* Slot)
			{ return Slot && Slot->Node.SlotName == SlotName; });
		if (Matches.Num() != 1) { return false; }
		UAnimGraphNode_Slot* Slot = Matches[0];
		UEdGraphPin* Output = PosePin(*Slot, EGPD_Output);
		if (!Output || Output->LinkedTo.Num() != 1) { return false; }
		UAnimGraphNode_Inertialization* Existing = nullptr;
		for (UEdGraphNode* Node : Graph.Nodes)
		{
			if (Node && Node->NodeComment == Marker(SlotName))
			{
				if (Existing) { return false; }
				Existing = Cast<UAnimGraphNode_Inertialization>(Node);
				if (!Existing) { return false; }
			}
		}
		if (Existing)
		{
			UEdGraphPin* Input = PosePin(*Existing, EGPD_Input);
			UEdGraphPin* Blended = PosePin(*Existing, EGPD_Output);
			if (!Input || !Blended || Input->LinkedTo.Num() != 1 || Blended->LinkedTo.Num() != 1
				|| !Input->LinkedTo.Contains(Output) || !Output->LinkedTo.Contains(Input)) { return false; }
		}
		else if (Cast<UAnimGraphNode_Inertialization>(Output->LinkedTo[0]->GetOwningNode()))
		{
			return false; // An unowned node is a deliberate user edit, not a duplicate insertion target.
		}
		Paths.Emplace(Slot, Existing);
	}
	if (!bApply) { return Paths[0].Value && Paths[1].Value; }
	if (Paths[0].Value && Paths[1].Value) { return true; }
	if (Paths[0].Value || Paths[1].Value) { return false; }
	const UEdGraphSchema* Schema = Graph.GetSchema();
	if (!Schema) { return false; }
	Blueprint->Modify();
	Graph.Modify();
	for (const auto& Path : Paths)
	{
		UAnimGraphNode_Slot* Slot = Path.Key;
		UEdGraphPin* Output = PosePin(*Slot, EGPD_Output);
		UEdGraphPin* Consumer = Output->LinkedTo[0];
		UAnimGraphNode_Inertialization* Blend = NewObject<UAnimGraphNode_Inertialization>(
			&Graph, NAME_None, RF_Transactional);
		Graph.AddNode(Blend, true, false);
		Blend->CreateNewGuid();
		Blend->PostPlacedNewNode();
		Blend->NodePosX = Slot->NodePosX + 190;
		Blend->NodePosY = Slot->NodePosY + 100;
		Blend->NodeComment = Marker(Slot->Node.SlotName);
		Blend->bCommentBubbleVisible = true;
		Blend->AllocateDefaultPins();
		UEdGraphPin* Input = PosePin(*Blend, EGPD_Input);
		UEdGraphPin* Blended = PosePin(*Blend, EGPD_Output);
		if (!Input || !Blended) { return false; }
		Output->BreakLinkTo(Consumer);
		if (!Schema->TryCreateConnection(Output, Input) || !Schema->TryCreateConnection(Blended, Consumer))
		{ return false; } // Caller must not save a failed authoring operation.
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	return Blueprint->Status != BS_Error && InstallMontageSegmentInertialization(Blueprint, false);
}

bool UMHGZAnimationSeamLibrary::InstallWeaponGripConstraint(UAnimBlueprint* Blueprint, bool bApply)
{
	if (!Blueprint || !Blueprint->ParentClass || !Blueprint->TargetSkeleton
		|| !Blueprint->ParentClass->IsChildOf(UMHGZMotionMatchingAnimInstance::StaticClass())
		|| Blueprint->TargetSkeleton->GetReferenceSkeleton().FindBoneIndex(TEXT("L_Weapon_00")) == INDEX_NONE
		|| Blueprint->TargetSkeleton->GetReferenceSkeleton().FindBoneIndex(TEXT("R_Weapon_00")) == INDEX_NONE) { return false; }
	TArray<UEdGraph*> Graphs; Blueprint->GetAllGraphs(Graphs);
	UEdGraph** Found = Graphs.FindByPredicate([](UEdGraph* G) { return G && G->GetFName() == TEXT("AnimGraph"); });
	if (!Found) { return false; }
	UEdGraph& Graph = **Found;
	const auto FindMarked = [&Graph](const FString& Label) -> UEdGraphNode*
	{
		UEdGraphNode* Result = nullptr;
		for (UEdGraphNode* Node : Graph.Nodes)
		{
			if (Node && Node->NodeComment == Label)
			{ if (Result) { return nullptr; } Result = Node; }
		}
		return Result;
	};
	auto* Inertia = Cast<UAnimGraphNode_Inertialization>(FindMarked(Marker(TEXT("DefaultSlot"))));
	UEdGraphPin* Source = Inertia ? PosePin(*Inertia, EGPD_Output) : nullptr;
	if (!Source || Source->LinkedTo.Num() != 1) { return false; }
	const FString Prefix(TEXT("MHGZ.WeaponGrip."));
	const FName AlphaName = GET_MEMBER_NAME_CHECKED(UMHGZMotionMatchingAnimInstance, WeaponGripConstraintAlpha);
	auto* ToComponent = Cast<UAnimGraphNode_LocalToComponentSpace>(FindMarked(Prefix + TEXT("Component")));
	auto* Copy = Cast<UAnimGraphNode_CopyBone>(FindMarked(Prefix + TEXT("Copy")));
	auto* ToLocal = Cast<UAnimGraphNode_ComponentToLocalSpace>(FindMarked(Prefix + TEXT("Local")));
	auto* Getter = Cast<UK2Node_VariableGet>(FindMarked(Prefix + TEXT("Alpha")));
	int32 MarkedCount = 0;
	for (UEdGraphNode* Node : Graph.Nodes)
	{ if (Node && Node->NodeComment.StartsWith(Prefix)) { ++MarkedCount; } }
	const auto Linked = [](UEdGraphPin* A, UEdGraphPin* B)
	{ return A && B && A->LinkedTo.Num() == 1 && B->LinkedTo.Num() == 1 && A->LinkedTo.Contains(B) && B->LinkedTo.Contains(A); };
	if (MarkedCount)
	{
		if (MarkedCount != 4 || !ToComponent || !Copy || !ToLocal || !Getter) { return false; }
		return Copy->Node.SourceBone.BoneName == TEXT("R_Weapon_00") && Copy->Node.TargetBone.BoneName == TEXT("L_Weapon_00")
			&& Copy->Node.ControlSpace == BCS_ComponentSpace && Copy->Node.bCopyTranslation && Copy->Node.bCopyRotation
			&& !Copy->Node.bCopyScale && Copy->Node.AlphaInputType == EAnimAlphaInputType::Float
			&& Getter->VariableReference.GetMemberName() == AlphaName
			&& Linked(Source, PosePin(*ToComponent, EGPD_Input))
			&& Linked(PosePin(*ToComponent, EGPD_Output, FComponentSpacePoseLink::StaticStruct()),
				PosePin(*Copy, EGPD_Input, FComponentSpacePoseLink::StaticStruct()))
			&& Linked(PosePin(*Copy, EGPD_Output, FComponentSpacePoseLink::StaticStruct()),
				PosePin(*ToLocal, EGPD_Input, FComponentSpacePoseLink::StaticStruct()))
			&& Linked(Getter->FindPin(AlphaName), Copy->FindPin(TEXT("Alpha")))
			&& PosePin(*ToLocal, EGPD_Output) && PosePin(*ToLocal, EGPD_Output)->LinkedTo.Num() == 1;
	}
	if (!bApply) { return false; }
	const UEdGraphSchema* Schema = Graph.GetSchema();
	UEdGraphPin* Consumer = Source->LinkedTo[0];
	if (!Schema || Consumer->PinType.PinSubCategoryObject.Get() != FPoseLink::StaticStruct()) { return false; }
	Blueprint->Modify(); Graph.Modify();
	const auto Add = [&Graph, Inertia, &Prefix](UEdGraphNode* Node, const TCHAR* Suffix, int32 X, int32 Y)
	{
		Graph.AddNode(Node, true, false); Node->CreateNewGuid(); Node->PostPlacedNewNode();
		Node->NodeComment = Prefix + Suffix; Node->bCommentBubbleVisible = true;
		Node->NodePosX = Inertia->NodePosX + X; Node->NodePosY = Inertia->NodePosY + Y;
		Node->AllocateDefaultPins();
	};
	ToComponent = NewObject<UAnimGraphNode_LocalToComponentSpace>(&Graph, NAME_None, RF_Transactional);
	Copy = NewObject<UAnimGraphNode_CopyBone>(&Graph, NAME_None, RF_Transactional);
	Copy->Node.SourceBone.BoneName = TEXT("R_Weapon_00"); Copy->Node.TargetBone.BoneName = TEXT("L_Weapon_00");
	Copy->Node.bCopyTranslation = true; Copy->Node.bCopyRotation = true; Copy->Node.bCopyScale = false;
	Copy->Node.ControlSpace = BCS_ComponentSpace; Copy->Node.Alpha = 0.f;
	Copy->Node.AlphaInputType = EAnimAlphaInputType::Float;
	ToLocal = NewObject<UAnimGraphNode_ComponentToLocalSpace>(&Graph, NAME_None, RF_Transactional);
	Getter = NewObject<UK2Node_VariableGet>(&Graph, NAME_None, RF_Transactional);
	Getter->VariableReference.SetSelfMember(AlphaName);
	Add(ToComponent, TEXT("Component"), 190, 220); Add(Copy, TEXT("Copy"), 420, 220);
	Add(ToLocal, TEXT("Local"), 720, 220); Add(Getter, TEXT("Alpha"), 370, 440);
	Source->BreakLinkTo(Consumer);
	if (!Schema->TryCreateConnection(Source, PosePin(*ToComponent, EGPD_Input))
		|| !Schema->TryCreateConnection(PosePin(*ToComponent, EGPD_Output, FComponentSpacePoseLink::StaticStruct()),
			PosePin(*Copy, EGPD_Input, FComponentSpacePoseLink::StaticStruct()))
		|| !Schema->TryCreateConnection(PosePin(*Copy, EGPD_Output, FComponentSpacePoseLink::StaticStruct()),
			PosePin(*ToLocal, EGPD_Input, FComponentSpacePoseLink::StaticStruct()))
		|| !Schema->TryCreateConnection(Getter->FindPin(AlphaName), Copy->FindPin(TEXT("Alpha")))
		|| !Schema->TryCreateConnection(PosePin(*ToLocal, EGPD_Output), Consumer)) { return false; }
	UEdGraphNode_Comment* Comment = NewObject<UEdGraphNode_Comment>(&Graph, NAME_None, RF_Transactional);
	Graph.AddNode(Comment, true, false); Comment->CreateNewGuid();
	Comment->NodeComment = TEXT("飞圆斩310→117：惯性混合后持械约束（仅显式接缝启用）");
	Comment->NodePosX = Inertia->NodePosX + 160; Comment->NodePosY = Inertia->NodePosY + 140;
	Comment->NodeWidth = 850; Comment->NodeHeight = 430;
	Comment->CommentColor = FLinearColor(.12f,.32f,.25f,1.f);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	return Blueprint->Status != BS_Error && InstallWeaponGripConstraint(Blueprint, false);
}
