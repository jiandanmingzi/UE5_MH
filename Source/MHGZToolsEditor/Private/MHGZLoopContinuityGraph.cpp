// Copyright MHGZ Project. All Rights Reserved.
#include "MHGZAnimationSeamLibrary.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/MHGZMotionMatchingAnimInstance.h"
#include "Animation/MHGZPoseSearchFeatureChannel_LoopPhase.h"
#include "AnimGraphNode_Inertialization.h"
#include "AnimGraphNode_MotionMatching.h"
#include "EdGraph/EdGraph.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "PoseSearch/PoseSearchSchema.h"

namespace
{
UEdGraphPin* LoopPosePin(UEdGraphNode& Node, EEdGraphPinDirection Direction)
{
	for (auto* Pin : Node.Pins)
	{
		if (Pin && Pin->Direction == Direction
			&& Pin->PinType.PinSubCategoryObject.Get() == FPoseLink::StaticStruct()) { return Pin; }
	}
	return nullptr;
}
}

bool UMHGZAnimationSeamLibrary::ConfigureLoopContinuitySchema(UPoseSearchSchema* Schema, bool bUnsheathed)
{
	if (!Schema || !Schema->GetName().StartsWith(TEXT("PSS_MH_"))
		|| (!Schema->GetName().Contains(TEXT("LoopPhase")) && !Schema->GetName().Contains(TEXT("LoopEntry"))))
	{ return false; }
	UMHGZPoseSearchFeatureChannel_LoopPhase* Phase = nullptr;
	for (const auto& Channel : Schema->GetChannels())
	{
		if (auto* Found = Cast<UMHGZPoseSearchFeatureChannel_LoopPhase>(Channel))
		{ if (Phase) { return false; } Phase = Found; }
	}
	if (!Phase) { return false; }
	Schema->Modify();
	Phase->Modify();
	Phase->MaxCycleError = 0.001f;
	Phase->bUnsheathed = bUnsheathed;
	Phase->EntryRootVelocityHorizon = 0.05f;
	Schema->PostEditChange();
	Schema->MarkPackageDirty();
	return Phase->GetChannelCardinality() == 17;
}

bool UMHGZAnimationSeamLibrary::InstallLocomotionInertialization(UAnimBlueprint* Blueprint, bool bApply)
{
	if (!Blueprint || !Blueprint->ParentClass
		|| !Blueprint->ParentClass->IsChildOf(UMHGZMotionMatchingAnimInstance::StaticClass())) { return false; }
	TArray<UAnimGraphNode_MotionMatching*> Nodes;
	FBlueprintEditorUtils::GetAllNodesOfClass(Blueprint, Nodes);
	if (Nodes.Num() != 2) { return false; }
	TArray<TPair<UAnimGraphNode_MotionMatching*, UAnimGraphNode_Inertialization*>> Paths;
	for (auto* MM : Nodes)
	{
		auto* Output = LoopPosePin(*MM, EGPD_Output);
		if (!Output || Output->LinkedTo.Num() != 1) { return false; }
		const FString Comment = TEXT("MHGZ.LocomotionInertialization.") + MM->GetName();
		auto* Existing = Cast<UAnimGraphNode_Inertialization>(Output->LinkedTo[0]->GetOwningNode());
		if (Existing && Existing->NodeComment != Comment) { return false; }
		if (Existing)
		{
			auto* Blended = LoopPosePin(*Existing, EGPD_Output);
			if (!Blended || Blended->LinkedTo.Num() != 1) { return false; }
		}
		Paths.Emplace(MM, Existing);
	}
	if (!bApply || (Paths[0].Value && Paths[1].Value)) { return Paths[0].Value && Paths[1].Value; }
	if (Paths[0].Value || Paths[1].Value) { return false; }
	Blueprint->Modify();
	for (const auto& Path : Paths)
	{
		auto* MM = Path.Key;
		auto* Graph = MM->GetGraph();
		Graph->Modify();
		auto* Output = LoopPosePin(*MM, EGPD_Output);
		auto* Consumer = Output->LinkedTo[0];
		auto* Blend = NewObject<UAnimGraphNode_Inertialization>(Graph, NAME_None, RF_Transactional);
		Graph->AddNode(Blend, true, false);
		Blend->CreateNewGuid();
		Blend->PostPlacedNewNode();
		Blend->NodePosX = MM->NodePosX + 210;
		Blend->NodePosY = MM->NodePosY + 80;
		Blend->NodeComment = TEXT("MHGZ.LocomotionInertialization.") + MM->GetName();
		Blend->bCommentBubbleVisible = true;
		Blend->AllocateDefaultPins();
		Output->BreakLinkTo(Consumer);
		if (!Graph->GetSchema()->TryCreateConnection(Output, LoopPosePin(*Blend, EGPD_Input))
			|| !Graph->GetSchema()->TryCreateConnection(LoopPosePin(*Blend, EGPD_Output), Consumer)) { return false; }
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	return Blueprint->Status != BS_Error && InstallLocomotionInertialization(Blueprint, false);
}
