// Copyright MHGZ Project. All Rights Reserved.

#include "Commandlets/MHGZActionVisualPitchGraphCommandlet.h"

#if WITH_EDITOR

#include "Animation/AnimBlueprint.h"
#include "BoneControllers/AnimNode_ModifyBone.h"
#include "Animation/AnimNodeSpaceConversions.h"
#include "Animation/MHGZMotionMatchingAnimInstance.h"
#include "AnimationGraphSchema.h"
#include "AnimGraphNode_ComponentToLocalSpace.h"
#include "AnimGraphNode_LocalToComponentSpace.h"
#include "AnimGraphNode_ModifyBone.h"
#include "AnimGraphNode_Root.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphNode_Comment.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "Animation/Skeleton.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/Parse.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"

namespace UE::MHGZ::ActionVisualPitchGraph
{
constexpr TCHAR AnimBlueprintPath[] =
	TEXT("/Game/Blueprints/Characters/Demo/Animation/ABP_MH_Character.ABP_MH_Character");
constexpr TCHAR LocalToComponentComment[] = TEXT("M4.KinsectSlash.ActionVisualPitch.LocalToComponent");
constexpr TCHAR ModifyBoneComment[] = TEXT("M4.KinsectSlash.ActionVisualPitch.ModifyBone");
constexpr TCHAR ComponentToLocalComment[] = TEXT("M4.KinsectSlash.ActionVisualPitch.ComponentToLocal");
constexpr TCHAR RotationGetterComment[] = TEXT("M4.KinsectSlash.ActionVisualPitch.RotationGetter");
constexpr TCHAR CommentFrameText[] =
	TEXT("Kinsect Slash | full-body pitch (presentation only; capsule and Root Motion unchanged)");

UEdGraphPin* FindPosePin(UEdGraphNode& Node, const EEdGraphPinDirection Direction,
	const UScriptStruct* PoseLinkType)
{
	for (UEdGraphPin* Pin : Node.Pins)
	{
		if (Pin && Pin->Direction == Direction
			&& Pin->PinType.PinCategory == UAnimationGraphSchema::PC_Struct
			&& Pin->PinType.PinSubCategoryObject.Get() == PoseLinkType)
		{
			return Pin;
		}
	}
	return nullptr;
}

UEdGraphPin* FindPinByName(UEdGraphNode& Node, const FName Name,
	const EEdGraphPinDirection Direction)
{
	return Node.FindPin(Name, Direction);
}

bool IsLinked(const UEdGraphPin* A, const UEdGraphPin* B)
{
	return A && B && A->LinkedTo.Contains(const_cast<UEdGraphPin*>(B));
}

UEdGraphNode* FindCommentedNode(UEdGraph& Graph, const TCHAR* Comment)
{
	for (UEdGraphNode* Node : Graph.Nodes)
	{
		if (Node && Node->NodeComment == Comment)
		{
			return Node;
		}
	}
	return nullptr;
}

bool SaveAsset(UObject& Asset)
{
	UPackage* Package = Asset.GetOutermost();
	FString Filename;
	if (!Package || !FPackageName::TryConvertLongPackageNameToFilename(
		Package->GetName(), Filename, FPackageName::GetAssetPackageExtension()))
	{
		return false;
	}
	Package->MarkPackageDirty();
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	return UPackage::SavePackage(Package, &Asset, *Filename, SaveArgs);
}

template<typename TNode>
TNode* AddAnimNode(UEdGraph& Graph, const int32 X, const int32 Y,
	const TCHAR* StableComment)
{
	TNode* Node = NewObject<TNode>(&Graph);
	if (!Node)
	{
		return nullptr;
	}
	Graph.AddNode(Node, true, false);
	Node->CreateNewGuid();
	Node->PostPlacedNewNode();
	Node->NodePosX = X;
	Node->NodePosY = Y;
	Node->NodeComment = StableComment;
	Node->AllocateDefaultPins();
	return Node;
}

bool AuditInstalledGraph(UAnimBlueprint& Blueprint, UEdGraph& Graph,
	UAnimGraphNode_Root& Root, const bool bLogConnections)
{
	UEdGraphNode* LocalToComponent = FindCommentedNode(Graph, LocalToComponentComment);
	UEdGraphNode* ModifyBone = FindCommentedNode(Graph, ModifyBoneComment);
	UEdGraphNode* ComponentToLocal = FindCommentedNode(Graph, ComponentToLocalComment);
	UEdGraphNode* RotationGetter = FindCommentedNode(Graph, RotationGetterComment);
	const int32 MarkedNodeCount = static_cast<int32>(LocalToComponent != nullptr)
		+ static_cast<int32>(ModifyBone != nullptr)
		+ static_cast<int32>(ComponentToLocal != nullptr)
		+ static_cast<int32>(RotationGetter != nullptr);
	if (MarkedNodeCount == 0)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[ActionVisualPitchGraph] AnimGraph has no action-pitch layer yet."));
		return false;
	}
	if (MarkedNodeCount != 4)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ActionVisualPitchGraph] Partial action-pitch layer found (%d/4); refusing to guess or duplicate nodes."),
			MarkedNodeCount);
		return false;
	}

	UEdGraphPin* RootInput = FindPinByName(Root, TEXT("Result"), EGPD_Input);
	const UAnimGraphNode_ModifyBone* ModifyBoneNode = Cast<UAnimGraphNode_ModifyBone>(ModifyBone);
	if (!RootInput || !ModifyBoneNode)
	{
		UE_LOG(LogTemp, Error, TEXT("[ActionVisualPitchGraph] Required root/ModifyBone node is invalid."));
		return false;
	}

	UEdGraphPin* LocalInput = FindPosePin(*LocalToComponent, EGPD_Input, FPoseLink::StaticStruct());
	UEdGraphPin* LocalOutput = FindPosePin(*LocalToComponent, EGPD_Output,
		FComponentSpacePoseLink::StaticStruct());
	UEdGraphPin* ModifyInput = FindPosePin(*ModifyBone, EGPD_Input,
		FComponentSpacePoseLink::StaticStruct());
	UEdGraphPin* ModifyOutput = FindPosePin(*ModifyBone, EGPD_Output,
		FComponentSpacePoseLink::StaticStruct());
	UEdGraphPin* ComponentInput = FindPosePin(*ComponentToLocal, EGPD_Input,
		FComponentSpacePoseLink::StaticStruct());
	UEdGraphPin* ComponentOutput = FindPosePin(*ComponentToLocal, EGPD_Output,
		FPoseLink::StaticStruct());
	UEdGraphPin* RotationInput = FindPinByName(*ModifyBone, TEXT("Rotation"), EGPD_Input);
	UEdGraphPin* RotationOutput = FindPinByName(*RotationGetter,
		GET_MEMBER_NAME_CHECKED(UMHGZMotionMatchingAnimInstance,
			ActionVisualPitchComponentRotation), EGPD_Output);
	UEdGraphPin* SourcePoseOutput = nullptr;
	if (LocalInput && LocalInput->LinkedTo.Num() == 1 && LocalInput->LinkedTo[0])
	{
		UEdGraphNode* SourceNode = LocalInput->LinkedTo[0]->GetOwningNode();
		if (SourceNode)
		{
			SourcePoseOutput = FindPosePin(*SourceNode, EGPD_Output, FPoseLink::StaticStruct());
		}
	}
	const bool bConnected = RootInput->LinkedTo.Num() == 1
		&& IsLinked(RootInput, ComponentOutput)
		&& IsLinked(ModifyOutput, ComponentInput)
		&& IsLinked(ModifyInput, LocalOutput)
		&& IsLinked(LocalInput, SourcePoseOutput)
		&& IsLinked(RotationInput, RotationOutput);

	const USkeleton* Skeleton = Blueprint.TargetSkeleton;
	const FName RootBoneName = Skeleton && Skeleton->GetReferenceSkeleton().GetNum() > 0
		? Skeleton->GetReferenceSkeleton().GetBoneName(0) : NAME_None;
	const bool bSettingsValid = ModifyBoneNode
		&& ModifyBoneNode->Node.BoneToModify.BoneName == RootBoneName
		&& ModifyBoneNode->Node.TranslationMode == BMM_Ignore
		&& ModifyBoneNode->Node.RotationMode == BMM_Additive
		&& ModifyBoneNode->Node.RotationSpace == BCS_ComponentSpace
		&& ModifyBoneNode->Node.ScaleMode == BMM_Ignore;

	UE_LOG(LogTemp, Display,
		TEXT("[ActionVisualPitchGraph] Skeleton=%s rootBone=%s layerNodes=%d/4 connections=%s settings=%s"),
		Skeleton ? *Skeleton->GetPathName() : TEXT("<none>"), *RootBoneName.ToString(),
		MarkedNodeCount, bConnected ? TEXT("OK") : TEXT("INVALID"),
		bSettingsValid ? TEXT("OK") : TEXT("INVALID"));
	if (bLogConnections && RootInput && RootInput->LinkedTo.Num() == 1)
	{
		UEdGraphNode* Source = RootInput->LinkedTo[0]->GetOwningNode();
		UE_LOG(LogTemp, Display, TEXT("[ActionVisualPitchGraph] Output Pose source after layer: %s (%s)"),
			Source ? *Source->GetClass()->GetName() : TEXT("<none>"),
			Source ? *Source->NodeComment : TEXT(""));
	}
	return bConnected && bSettingsValid;
}

bool ApplyLayer(UAnimBlueprint& Blueprint, UEdGraph& Graph, UAnimGraphNode_Root& Root)
{
	const USkeleton* Skeleton = Blueprint.TargetSkeleton;
	if (!Skeleton || Skeleton->GetReferenceSkeleton().GetNum() < 1)
	{
		UE_LOG(LogTemp, Error, TEXT("[ActionVisualPitchGraph] ABP has no valid target skeleton/root bone."));
		return false;
	}
	UEdGraphPin* RootInput = FindPinByName(Root, TEXT("Result"), EGPD_Input);
	if (!RootInput || RootInput->LinkedTo.Num() != 1)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ActionVisualPitchGraph] Output Pose must have exactly one source before insertion."));
		return false;
	}
	UEdGraphPin* ExistingOutput = RootInput->LinkedTo[0];
	UEdGraphNode* ExistingSource = ExistingOutput ? ExistingOutput->GetOwningNode() : nullptr;
	if (!ExistingSource || ExistingOutput->Direction != EGPD_Output)
	{
		UE_LOG(LogTemp, Error, TEXT("[ActionVisualPitchGraph] Output Pose source is invalid."));
		return false;
	}
	UEdGraphPin* SourcePoseOutput = FindPosePin(*ExistingSource, EGPD_Output,
		FPoseLink::StaticStruct());
	if (SourcePoseOutput != ExistingOutput)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ActionVisualPitchGraph] Current final output is not a Local Pose pin; refusing insertion."));
		return false;
	}

	Blueprint.Modify();
	Graph.Modify();
	const int32 BaseX = ExistingSource->NodePosX + 160;
	const int32 BaseY = ExistingSource->NodePosY;
	UEdGraphNode_Comment* Comment = NewObject<UEdGraphNode_Comment>(&Graph);
	Graph.AddNode(Comment, true, false);
	Comment->CreateNewGuid();
	Comment->NodePosX = BaseX - 24;
	Comment->NodePosY = BaseY - 112;
	Comment->NodeWidth = 1040;
	Comment->NodeHeight = 320;
	Comment->CommentColor = FLinearColor(0.12f, 0.28f, 0.38f, 1.0f);
	Comment->NodeComment = CommentFrameText;
	Comment->bCommentBubbleVisible = false;

	UAnimGraphNode_LocalToComponentSpace* LocalToComponent =
		AddAnimNode<UAnimGraphNode_LocalToComponentSpace>(Graph, BaseX, BaseY,
			LocalToComponentComment);
	UAnimGraphNode_ModifyBone* ModifyBone =
		AddAnimNode<UAnimGraphNode_ModifyBone>(Graph, BaseX + 300, BaseY,
			ModifyBoneComment);
	UAnimGraphNode_ComponentToLocalSpace* ComponentToLocal =
		AddAnimNode<UAnimGraphNode_ComponentToLocalSpace>(Graph, BaseX + 600, BaseY,
			ComponentToLocalComment);
	UK2Node_VariableGet* RotationGetter = NewObject<UK2Node_VariableGet>(&Graph);
	Graph.AddNode(RotationGetter, true, false);
	RotationGetter->CreateNewGuid();
	RotationGetter->PostPlacedNewNode();
	RotationGetter->NodePosX = BaseX + 360;
	RotationGetter->NodePosY = BaseY + 180;
	RotationGetter->NodeComment = RotationGetterComment;
	// The graph belongs to an AnimBlueprint derived from this AnimInstance.
	// Use SelfMember so the variable reference resolves through the generated
	// AnimBP class instead of creating an external native-target pin.
	RotationGetter->VariableReference.SetSelfMember(
		GET_MEMBER_NAME_CHECKED(UMHGZMotionMatchingAnimInstance,
			ActionVisualPitchComponentRotation));
	RotationGetter->AllocateDefaultPins();

	if (!LocalToComponent || !ModifyBone || !ComponentToLocal)
	{
		return false;
	}
	ModifyBone->Node.BoneToModify.BoneName = Skeleton->GetReferenceSkeleton().GetBoneName(0);
	ModifyBone->Node.TranslationMode = BMM_Ignore;
	ModifyBone->Node.RotationMode = BMM_Additive;
	ModifyBone->Node.RotationSpace = BCS_ComponentSpace;
	ModifyBone->Node.ScaleMode = BMM_Ignore;
	ModifyBone->Node.Rotation = FRotator::ZeroRotator;

	const UAnimationGraphSchema* Schema = GetDefault<UAnimationGraphSchema>();
	UEdGraphPin* LocalInput = FindPosePin(*LocalToComponent, EGPD_Input, FPoseLink::StaticStruct());
	UEdGraphPin* LocalOutput = FindPosePin(*LocalToComponent, EGPD_Output,
		FComponentSpacePoseLink::StaticStruct());
	UEdGraphPin* ModifyInput = FindPosePin(*ModifyBone, EGPD_Input,
		FComponentSpacePoseLink::StaticStruct());
	UEdGraphPin* ModifyOutput = FindPosePin(*ModifyBone, EGPD_Output,
		FComponentSpacePoseLink::StaticStruct());
	UEdGraphPin* ComponentInput = FindPosePin(*ComponentToLocal, EGPD_Input,
		FComponentSpacePoseLink::StaticStruct());
	UEdGraphPin* ComponentOutput = FindPosePin(*ComponentToLocal, EGPD_Output,
		FPoseLink::StaticStruct());
	UEdGraphPin* RotationInput = FindPinByName(*ModifyBone, TEXT("Rotation"), EGPD_Input);
	UEdGraphPin* RotationOutput = FindPinByName(*RotationGetter,
		GET_MEMBER_NAME_CHECKED(UMHGZMotionMatchingAnimInstance,
			ActionVisualPitchComponentRotation), EGPD_Output);
	if (!Schema || !LocalInput || !LocalOutput || !ModifyInput || !ModifyOutput
		|| !ComponentInput || !ComponentOutput || !RotationInput || !RotationOutput)
	{
		UE_LOG(LogTemp, Error, TEXT("[ActionVisualPitchGraph] Expected typed pins were not generated."));
		return false;
	}

	RootInput->BreakLinkTo(ExistingOutput);
	if (!Schema->TryCreateConnection(ExistingOutput, LocalInput)
		|| !Schema->TryCreateConnection(LocalOutput, ModifyInput)
		|| !Schema->TryCreateConnection(ModifyOutput, ComponentInput)
		|| !Schema->TryCreateConnection(ComponentOutput, RootInput)
		|| !Schema->TryCreateConnection(RotationOutput, RotationInput))
	{
		UE_LOG(LogTemp, Error, TEXT("[ActionVisualPitchGraph] Could not connect the complete pose/rotation chain."));
		return false;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(&Blueprint);
	FKismetEditorUtilities::CompileBlueprint(&Blueprint);
	if (Blueprint.Status == BS_Error)
	{
		UE_LOG(LogTemp, Error, TEXT("[ActionVisualPitchGraph] AnimBlueprint compilation failed."));
		return false;
	}
	if (!AuditInstalledGraph(Blueprint, Graph, Root, true))
	{
		UE_LOG(LogTemp, Error, TEXT("[ActionVisualPitchGraph] Post-compile graph audit failed."));
		return false;
	}
	return SaveAsset(Blueprint);
}
} // namespace UE::MHGZ::ActionVisualPitchGraph

#endif // WITH_EDITOR

UMHGZActionVisualPitchGraphCommandlet::UMHGZActionVisualPitchGraphCommandlet()
{
	IsClient = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UMHGZActionVisualPitchGraphCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	using namespace UE::MHGZ::ActionVisualPitchGraph;
	const bool bApply = FParse::Param(*Params, TEXT("Apply"));
	UAnimBlueprint* Blueprint = LoadObject<UAnimBlueprint>(nullptr, AnimBlueprintPath);
	if (!Blueprint)
	{
		UE_LOG(LogTemp, Error, TEXT("[ActionVisualPitchGraph] Could not load %s."), AnimBlueprintPath);
		return 1;
	}
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	UEdGraph* AnimGraph = nullptr;
	for (UEdGraph* Candidate : Graphs)
	{
		if (Candidate && Candidate->GetFName() == TEXT("AnimGraph"))
		{
			AnimGraph = Candidate;
			break;
		}
	}
	if (!AnimGraph)
	{
		UE_LOG(LogTemp, Error, TEXT("[ActionVisualPitchGraph] AnimGraph was not found."));
		return 1;
	}
	TArray<UAnimGraphNode_Root*> Roots;
	AnimGraph->GetNodesOfClass<UAnimGraphNode_Root>(Roots);
	if (Roots.Num() != 1)
	{
		UE_LOG(LogTemp, Error, TEXT("[ActionVisualPitchGraph] Expected one AnimGraph root, found %d."), Roots.Num());
		return 1;
	}

	if (bApply)
	{
		const int32 MarkedNodes = static_cast<int32>(FindCommentedNode(*AnimGraph, LocalToComponentComment) != nullptr)
			+ static_cast<int32>(FindCommentedNode(*AnimGraph, ModifyBoneComment) != nullptr)
			+ static_cast<int32>(FindCommentedNode(*AnimGraph, ComponentToLocalComment) != nullptr)
			+ static_cast<int32>(FindCommentedNode(*AnimGraph, RotationGetterComment) != nullptr);
		if (MarkedNodes == 4)
		{
			return AuditInstalledGraph(*Blueprint, *AnimGraph, *Roots[0], true) ? 0 : 1;
		}
		if (MarkedNodes != 0)
		{
			UE_LOG(LogTemp, Error, TEXT("[ActionVisualPitchGraph] Partial layer found; use AuditOnly and resolve manually."));
			return 1;
		}
		if (!ApplyLayer(*Blueprint, *AnimGraph, *Roots[0]))
		{
			return 1;
		}
		UE_LOG(LogTemp, Display, TEXT("[ActionVisualPitchGraph] Updated and saved %s."), AnimBlueprintPath);
		return 0;
	}

	UEdGraphPin* RootInput = FindPinByName(*Roots[0], TEXT("Result"), EGPD_Input);
	if (RootInput && RootInput->LinkedTo.Num() == 1)
	{
		UEdGraphNode* Source = RootInput->LinkedTo[0]->GetOwningNode();
		UE_LOG(LogTemp, Display,
			TEXT("[ActionVisualPitchGraph] Final local-pose source: %s; use -Apply for idempotent insertion."),
			Source ? *Source->GetClass()->GetName() : TEXT("<invalid>"));
	}
	const bool bInstalled = AuditInstalledGraph(*Blueprint, *AnimGraph, *Roots[0], true);
	return bInstalled ? 0 : 2;
#else
	return 1;
#endif
}
