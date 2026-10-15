// Copyright MHGZ Project. All Rights Reserved.

#include "Commandlets/MHGZKinsectActionLayerCommandlet.h"

#if WITH_EDITOR

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimMontage.h"
#include "Animation/MHGZMotionMatchingAnimInstance.h"
#include "AnimationGraphSchema.h"
#include "AnimGraphNode_BlendListByBool.h"
#include "AnimGraphNode_LayeredBoneBlend.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_SaveCachedPose.h"
#include "AnimGraphNode_Slot.h"
#include "AnimGraphNode_UseCachedPose.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/Parse.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"

namespace UE::MHGZ::KinsectActionLayer
{
constexpr TCHAR AnimBlueprintPath[] =
	TEXT("/Game/Blueprints/Characters/Demo/Animation/ABP_MH_Character.ABP_MH_Character");
constexpr TCHAR SendKinsectMontagePath[] =
	TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_SendKinsect.AM_IG_SendKinsect");
constexpr TCHAR RecallKinsectMontagePath[] =
	TEXT("/Game/Weapons/InsectGlaive/Anims/Montage/AM_IG_RecallKinsect.AM_IG_RecallKinsect");
constexpr TCHAR BaseCacheName[] = TEXT("KinsectActionBasePose");
constexpr TCHAR ActionPoseCacheName[] = TEXT("KinsectActionMontagePose");
constexpr TCHAR BaseCacheComment[] = TEXT("MHGZ.KinsectLayer.BaseCache");
constexpr TCHAR FullBodyBaseComment[] = TEXT("MHGZ.KinsectLayer.FullBodyBase");
constexpr TCHAR LayerBaseComment[] = TEXT("MHGZ.KinsectLayer.UpperBodyBase");
constexpr TCHAR SlotComment[] = TEXT("MHGZ.KinsectLayer.UpperBodySlot");
constexpr TCHAR ActionPoseCacheComment[] = TEXT("MHGZ.KinsectLayer.ActionPoseCache");
constexpr TCHAR FullBodyActionPoseComment[] = TEXT("MHGZ.KinsectLayer.FullBodyActionPose");
constexpr TCHAR LayerActionPoseComment[] = TEXT("MHGZ.KinsectLayer.LayerActionPose");
constexpr TCHAR BoneBlendComment[] = TEXT("MHGZ.KinsectLayer.SpineBlend");
constexpr TCHAR SelectorComment[] = TEXT("MHGZ.KinsectLayer.InputSelector");
constexpr TCHAR InputGetterComment[] = TEXT("MHGZ.KinsectLayer.RawInputGetter");
const FName DefaultSlotName(TEXT("DefaultSlot"));
const FName KinsectSlotName(TEXT("UpperBody_IGAction"));
const FName SpineBoneName(TEXT("Spine_00"));

UEdGraphPin* FindPin(UEdGraphNode& Node, const FName Name,
	const EEdGraphPinDirection Direction)
{
	return Node.FindPin(Name, Direction);
}

UEdGraphPin* FindPosePin(UEdGraphNode& Node, const EEdGraphPinDirection Direction)
{
	for (UEdGraphPin* Pin : Node.Pins)
	{
		if (Pin && Pin->Direction == Direction
			&& Pin->PinType.PinCategory == UAnimationGraphSchema::PC_Struct
			&& Pin->PinType.PinSubCategoryObject.Get() == FPoseLink::StaticStruct())
		{
			return Pin;
		}
	}
	return nullptr;
}

UEdGraphPin* FindBoolInput(UEdGraphNode& Node)
{
	for (UEdGraphPin* Pin : Node.Pins)
	{
		if (Pin && Pin->Direction == EGPD_Input
			&& Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean)
		{
			return Pin;
		}
	}
	return nullptr;
}

bool IsLinked(const UEdGraphPin* A, const UEdGraphPin* B)
{
	return A && B && A->LinkedTo.Contains(const_cast<UEdGraphPin*>(B));
}

template<typename TNode>
TNode* FindCommentedNode(UEdGraph& Graph, const TCHAR* Comment)
{
	for (UEdGraphNode* Node : Graph.Nodes)
	{
		if (Node && Node->NodeComment == Comment)
		{
			return Cast<TNode>(Node);
		}
	}
	return nullptr;
}

template<typename TNode>
TNode* AddAnimNode(UEdGraph& Graph, const int32 X, const int32 Y,
	const TCHAR* Comment)
{
	TNode* Node = NewObject<TNode>(&Graph, NAME_None, RF_Transactional);
	if (!Node)
	{
		return nullptr;
	}
	Graph.AddNode(Node, true, false);
	Node->CreateNewGuid();
	Node->PostPlacedNewNode();
	Node->NodePosX = X;
	Node->NodePosY = Y;
	Node->NodeComment = Comment;
	Node->AllocateDefaultPins();
	return Node;
}

int32 CountInstalledNodes(UEdGraph& Graph)
{
	const TCHAR* Comments[] = { BaseCacheComment, FullBodyBaseComment, LayerBaseComment,
		SlotComment, ActionPoseCacheComment, FullBodyActionPoseComment, LayerActionPoseComment,
		BoneBlendComment, SelectorComment, InputGetterComment };
	int32 Count = 0;
	for (const TCHAR* Comment : Comments)
	{
		for (const UEdGraphNode* Node : Graph.Nodes)
		{
			if (Node && Node->NodeComment == Comment)
			{
				++Count;
				break;
			}
		}
	}
	return Count;
}

bool AuditInstalledLayer(UEdGraph& Graph, UAnimGraphNode_Slot& DefaultSlot,
	const bool bLogDetails)
{
	UAnimGraphNode_SaveCachedPose* Cache =
		FindCommentedNode<UAnimGraphNode_SaveCachedPose>(Graph, BaseCacheComment);
	UAnimGraphNode_UseCachedPose* FullBodyBase =
		FindCommentedNode<UAnimGraphNode_UseCachedPose>(Graph, FullBodyBaseComment);
	UAnimGraphNode_UseCachedPose* LayerBase =
		FindCommentedNode<UAnimGraphNode_UseCachedPose>(Graph, LayerBaseComment);
	UAnimGraphNode_Slot* Slot = FindCommentedNode<UAnimGraphNode_Slot>(Graph, SlotComment);
	UAnimGraphNode_SaveCachedPose* ActionPoseCache =
		FindCommentedNode<UAnimGraphNode_SaveCachedPose>(Graph, ActionPoseCacheComment);
	UAnimGraphNode_UseCachedPose* FullBodyActionPose =
		FindCommentedNode<UAnimGraphNode_UseCachedPose>(Graph, FullBodyActionPoseComment);
	UAnimGraphNode_UseCachedPose* LayerActionPose =
		FindCommentedNode<UAnimGraphNode_UseCachedPose>(Graph, LayerActionPoseComment);
	UAnimGraphNode_LayeredBoneBlend* Layer =
		FindCommentedNode<UAnimGraphNode_LayeredBoneBlend>(Graph, BoneBlendComment);
	UAnimGraphNode_BlendListByBool* Selector =
		FindCommentedNode<UAnimGraphNode_BlendListByBool>(Graph, SelectorComment);
	UK2Node_VariableGet* Getter = FindCommentedNode<UK2Node_VariableGet>(Graph, InputGetterComment);
	if (!Cache || !FullBodyBase || !LayerBase || !Slot || !ActionPoseCache
		|| !FullBodyActionPose || !LayerActionPose || !Layer || !Selector || !Getter)
	{
		UE_LOG(LogTemp, Error, TEXT("[KinsectActionLayer] Expected all 10 marked nodes."));
		return false;
	}

	UEdGraphPin* CacheInput = FindPosePin(*Cache, EGPD_Input);
	UEdGraphPin* FullBaseOutput = FindPosePin(*FullBodyBase, EGPD_Output);
	UEdGraphPin* LayerBaseOutput = FindPosePin(*LayerBase, EGPD_Output);
	UEdGraphPin* SlotInput = FindPosePin(*Slot, EGPD_Input);
	UEdGraphPin* SlotOutput = FindPosePin(*Slot, EGPD_Output);
	UEdGraphPin* ActionPoseCacheInput = FindPosePin(*ActionPoseCache, EGPD_Input);
	UEdGraphPin* FullBodyActionOutput = FindPosePin(*FullBodyActionPose, EGPD_Output);
	UEdGraphPin* LayerActionOutput = FindPosePin(*LayerActionPose, EGPD_Output);
	UEdGraphPin* LayerBaseInput = FindPin(*Layer, TEXT("BasePose"), EGPD_Input);
	UEdGraphPin* LayerMontageInput = FindPin(*Layer, TEXT("BlendPoses_0"), EGPD_Input);
	UEdGraphPin* LayerOutput = FindPosePin(*Layer, EGPD_Output);
	UEdGraphPin* TruePose = FindPin(*Selector, TEXT("BlendPose_0"), EGPD_Input);
	UEdGraphPin* FalsePose = FindPin(*Selector, TEXT("BlendPose_1"), EGPD_Input);
	UEdGraphPin* SelectorOutput = FindPosePin(*Selector, EGPD_Output);
	UEdGraphPin* SelectorBool = FindBoolInput(*Selector);
	UEdGraphPin* GetterOutput = FindPin(*Getter, GET_MEMBER_NAME_CHECKED(
		UMHGZMotionMatchingAnimInstance, bKinsectUpperBodyOnly), EGPD_Output);
	UEdGraphPin* DefaultSlotInput = FindPosePin(DefaultSlot, EGPD_Input);

	const bool bCacheBindingsValid = FullBodyBase->SaveCachedPoseNode == Cache
		&& LayerBase->SaveCachedPoseNode == Cache
		&& FullBodyActionPose->SaveCachedPoseNode == ActionPoseCache
		&& LayerActionPose->SaveCachedPoseNode == ActionPoseCache
		&& ActionPoseCache->CacheName == ActionPoseCacheName;
	const bool bPoseWiringValid = CacheInput && CacheInput->LinkedTo.Num() == 1
		&& IsLinked(FullBaseOutput, SlotInput)
		&& IsLinked(LayerBaseOutput, LayerBaseInput)
		&& IsLinked(SlotOutput, ActionPoseCacheInput)
		&& IsLinked(FullBodyActionOutput, FalsePose)
		&& IsLinked(LayerActionOutput, LayerMontageInput)
		&& IsLinked(LayerOutput, TruePose)
		&& IsLinked(SelectorOutput, DefaultSlotInput)
		&& IsLinked(GetterOutput, SelectorBool);
	const bool bNodeSettingsValid = Slot->Node.SlotName == KinsectSlotName
		&& Slot->Node.bAlwaysUpdateSourcePose
		&& Layer->Node.BlendMode == ELayeredBoneBlendMode::BranchFilter
		&& Layer->Node.LayerSetup.Num() == 1
		&& Layer->Node.LayerSetup[0].BranchFilters.Num() == 1
		&& Layer->Node.LayerSetup[0].BranchFilters[0].BoneName == SpineBoneName
		&& Layer->Node.LayerSetup[0].BranchFilters[0].BlendDepth == 0;

	UE_LOG(LogTemp, Display,
		TEXT("[KinsectActionLayer] Cache=%s slot=%s wiring=%s settings=%s"),
		bCacheBindingsValid ? TEXT("OK") : TEXT("INVALID"),
		Slot->Node.SlotName == KinsectSlotName ? TEXT("UpperBody_IGAction") : TEXT("INVALID"),
		bPoseWiringValid ? TEXT("OK") : TEXT("INVALID"),
		bNodeSettingsValid ? TEXT("OK") : TEXT("INVALID"));

	if (bLogDetails)
	{
		for (const UEdGraphNode* Node : Graph.Nodes)
		{
			if (!Node) continue;
			UE_LOG(LogTemp, Display, TEXT("[KinsectActionLayer][Node] %s (%s) [%s] at %d,%d"),
				*Node->GetNodeTitle(ENodeTitleType::ListView).ToString(),
				*Node->GetClass()->GetName(), *Node->NodeComment, Node->NodePosX, Node->NodePosY);
			for (const UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin || Pin->LinkedTo.IsEmpty()) continue;
				for (const UEdGraphPin* Linked : Pin->LinkedTo)
				{
					const UEdGraphNode* LinkedNode = Linked ? Linked->GetOwningNode() : nullptr;
					UE_LOG(LogTemp, Display, TEXT("[KinsectActionLayer][Link] %s.%s -> %s.%s"),
						*Node->GetName(), *Pin->PinName.ToString(),
						LinkedNode ? *LinkedNode->GetName() : TEXT("<missing>"),
						Linked ? *Linked->PinName.ToString() : TEXT("<missing>"));
				}
			}
		}
		UE_LOG(LogTemp, Display,
			TEXT("[KinsectActionLayer][Cache] Base=%s ActionPose=%s"),
			*Cache->CacheName, *ActionPoseCache->CacheName);
	}
	return bCacheBindingsValid && bPoseWiringValid && bNodeSettingsValid;
}

bool SaveAsset(UAnimBlueprint& Blueprint)
{
	UPackage* Package = Blueprint.GetOutermost();
	FString Filename;
	if (!Package || !FPackageName::TryConvertLongPackageNameToFilename(
		Package->GetName(), Filename, FPackageName::GetAssetPackageExtension()))
	{
		return false;
	}
	Package->MarkPackageDirty();
	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	return UPackage::SavePackage(Package, &Blueprint, *Filename, Args);
}

bool MontageUsesKinsectSlot(const TCHAR* AssetPath)
{
	UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, AssetPath);
	if (!Montage)
	{
		UE_LOG(LogTemp, Error, TEXT("[KinsectActionLayer] Could not load Montage %s."), AssetPath);
		return false;
	}
	for (const FSlotAnimationTrack& Track : Montage->SlotAnimTracks)
	{
		if (Track.SlotName == KinsectSlotName)
		{
			UE_LOG(LogTemp, Display, TEXT("[KinsectActionLayer] Montage %s uses slot %s."),
				*Montage->GetName(), *Track.SlotName.ToString());
			return true;
		}
	}
	UE_LOG(LogTemp, Error,
		TEXT("[KinsectActionLayer] Montage %s has no %s slot track."),
		*Montage->GetName(), *KinsectSlotName.ToString());
	return false;
}

bool ApplyLayer(UAnimBlueprint& Blueprint, UEdGraph& Graph, UAnimGraphNode_Slot& DefaultSlot)
{
	const USkeleton* Skeleton = Blueprint.TargetSkeleton;
	if (!Skeleton || Skeleton->GetReferenceSkeleton().FindBoneIndex(SpineBoneName) == INDEX_NONE)
	{
		UE_LOG(LogTemp, Error, TEXT("[KinsectActionLayer] Target skeleton is missing Spine_00."));
		return false;
	}
	if (Blueprint.ParentClass && !Blueprint.ParentClass->IsChildOf(UMHGZMotionMatchingAnimInstance::StaticClass()))
	{
		UE_LOG(LogTemp, Error, TEXT("[KinsectActionLayer] AnimBP parent is not UMHGZMotionMatchingAnimInstance."));
		return false;
	}
	if (CountInstalledNodes(Graph) != 0)
	{
		UE_LOG(LogTemp, Error, TEXT("[KinsectActionLayer] Partial or existing marked layer found; refusing to duplicate."));
		return false;
	}
	for (const UEdGraphNode* Node : Graph.Nodes)
	{
		if (const UAnimGraphNode_Slot* ExistingSlot = Cast<UAnimGraphNode_Slot>(Node))
		{
			if (ExistingSlot->Node.SlotName == KinsectSlotName)
			{
				UE_LOG(LogTemp, Error, TEXT("[KinsectActionLayer] An unmarked UpperBody_IGAction Slot already exists; refusing to overwrite."));
				return false;
			}
		}
		if (const UAnimGraphNode_SaveCachedPose* ExistingCache = Cast<UAnimGraphNode_SaveCachedPose>(Node))
		{
			if (ExistingCache->CacheName == BaseCacheName
				|| ExistingCache->CacheName == ActionPoseCacheName)
			{
				UE_LOG(LogTemp, Error, TEXT("[KinsectActionLayer] Cache name %s already exists."),
					*ExistingCache->CacheName);
				return false;
			}
		}
	}

	UEdGraphPin* DefaultSlotSource = FindPosePin(DefaultSlot, EGPD_Input);
	if (!DefaultSlotSource || DefaultSlotSource->LinkedTo.Num() != 1)
	{
		UE_LOG(LogTemp, Error, TEXT("[KinsectActionLayer] DefaultSlot must have exactly one existing Source pose."));
		return false;
	}
	UEdGraphPin* ExistingBaseOutput = DefaultSlotSource->LinkedTo[0];
	if (!ExistingBaseOutput || ExistingBaseOutput->Direction != EGPD_Output
		|| ExistingBaseOutput->PinType.PinSubCategoryObject.Get() != FPoseLink::StaticStruct())
	{
		UE_LOG(LogTemp, Error, TEXT("[KinsectActionLayer] Existing DefaultSlot Source is not a local pose output."));
		return false;
	}
	UEdGraphNode* ExistingBaseNode = ExistingBaseOutput->GetOwningNode();
	if (!ExistingBaseNode)
	{
		return false;
	}

	Blueprint.Modify();
	Graph.Modify();
	const int32 BaseX = ExistingBaseNode->NodePosX + 220;
	int32 MaxY = ExistingBaseNode->NodePosY;
	for (const UEdGraphNode* Node : Graph.Nodes)
	{
		if (Node) MaxY = FMath::Max(MaxY, Node->NodePosY);
	}
	const int32 BaseY = MaxY + 260;

	UAnimGraphNode_SaveCachedPose* BaseCache =
		AddAnimNode<UAnimGraphNode_SaveCachedPose>(Graph, BaseX, BaseY, BaseCacheComment);
	if (BaseCache) BaseCache->CacheName = BaseCacheName;
	UAnimGraphNode_UseCachedPose* FullBodyBase =
		AddAnimNode<UAnimGraphNode_UseCachedPose>(Graph, BaseX + 260, BaseY, FullBodyBaseComment);
	UAnimGraphNode_UseCachedPose* LayerBase =
		AddAnimNode<UAnimGraphNode_UseCachedPose>(Graph, BaseX + 260, BaseY + 170, LayerBaseComment);
	if (FullBodyBase) FullBodyBase->SaveCachedPoseNode = BaseCache;
	if (LayerBase) LayerBase->SaveCachedPoseNode = BaseCache;
	UAnimGraphNode_Slot* KinsectSlot =
		AddAnimNode<UAnimGraphNode_Slot>(Graph, BaseX + 540, BaseY, SlotComment);
	if (KinsectSlot)
	{
		KinsectSlot->Node.SlotName = KinsectSlotName;
		KinsectSlot->Node.bAlwaysUpdateSourcePose = true;
	}
	UAnimGraphNode_SaveCachedPose* ActionPoseCache =
		AddAnimNode<UAnimGraphNode_SaveCachedPose>(Graph, BaseX + 800, BaseY, ActionPoseCacheComment);
	if (ActionPoseCache)
	{
		ActionPoseCache->CacheName = ActionPoseCacheName;
		ActionPoseCache->GetNodeTitle(ENodeTitleType::ListView);
	}
	UAnimGraphNode_UseCachedPose* FullBodyActionPose =
		AddAnimNode<UAnimGraphNode_UseCachedPose>(Graph, BaseX + 1080, BaseY, FullBodyActionPoseComment);
	UAnimGraphNode_UseCachedPose* LayerActionPose =
		AddAnimNode<UAnimGraphNode_UseCachedPose>(Graph, BaseX + 1080, BaseY + 170, LayerActionPoseComment);
	if (FullBodyActionPose) FullBodyActionPose->SaveCachedPoseNode = ActionPoseCache;
	if (LayerActionPose) LayerActionPose->SaveCachedPoseNode = ActionPoseCache;
	UAnimGraphNode_LayeredBoneBlend* Layer =
		AddAnimNode<UAnimGraphNode_LayeredBoneBlend>(Graph, BaseX + 1360, BaseY + 120, BoneBlendComment);
	if (Layer && Layer->Node.LayerSetup.IsValidIndex(0))
	{
		Layer->Node.BlendMode = ELayeredBoneBlendMode::BranchFilter;
		FBranchFilter& Filter = Layer->Node.LayerSetup[0].BranchFilters.AddDefaulted_GetRef();
		Filter.BoneName = SpineBoneName;
		Filter.BlendDepth = 0; // 0 applies full weight to Spine_00 and every descendant.
	}
	UAnimGraphNode_BlendListByBool* Selector =
		AddAnimNode<UAnimGraphNode_BlendListByBool>(Graph, BaseX + 1690, BaseY + 120, SelectorComment);
	UK2Node_VariableGet* InputGetter = NewObject<UK2Node_VariableGet>(
		&Graph, NAME_None, RF_Transactional);
	if (InputGetter)
	{
		Graph.AddNode(InputGetter, true, false);
		InputGetter->CreateNewGuid();
		InputGetter->PostPlacedNewNode();
		InputGetter->NodePosX = BaseX + 1690;
		InputGetter->NodePosY = BaseY + 300;
		InputGetter->NodeComment = InputGetterComment;
		InputGetter->VariableReference.SetSelfMember(GET_MEMBER_NAME_CHECKED(
			UMHGZMotionMatchingAnimInstance, bKinsectUpperBodyOnly));
		InputGetter->AllocateDefaultPins();
	}
	if (!BaseCache || !FullBodyBase || !LayerBase || !KinsectSlot || !ActionPoseCache
		|| !FullBodyActionPose || !LayerActionPose || !Layer || !Selector || !InputGetter)
	{
		UE_LOG(LogTemp, Error, TEXT("[KinsectActionLayer] Failed to create one or more AnimGraph nodes."));
		return false;
	}

	UEdGraphPin* CacheInput = FindPosePin(*BaseCache, EGPD_Input);
	UEdGraphPin* FullBaseOutput = FindPosePin(*FullBodyBase, EGPD_Output);
	UEdGraphPin* LayerBaseOutput = FindPosePin(*LayerBase, EGPD_Output);
	UEdGraphPin* KinsectSlotInput = FindPosePin(*KinsectSlot, EGPD_Input);
	UEdGraphPin* KinsectSlotOutput = FindPosePin(*KinsectSlot, EGPD_Output);
	UEdGraphPin* ActionPoseCacheInput = FindPosePin(*ActionPoseCache, EGPD_Input);
	UEdGraphPin* FullBodyActionOutput = FindPosePin(*FullBodyActionPose, EGPD_Output);
	UEdGraphPin* LayerActionOutput = FindPosePin(*LayerActionPose, EGPD_Output);
	UEdGraphPin* LayerBaseInput = FindPin(*Layer, TEXT("BasePose"), EGPD_Input);
	UEdGraphPin* LayerMontageInput = FindPin(*Layer, TEXT("BlendPoses_0"), EGPD_Input);
	UEdGraphPin* LayerOutput = FindPosePin(*Layer, EGPD_Output);
	UEdGraphPin* TruePose = FindPin(*Selector, TEXT("BlendPose_0"), EGPD_Input);
	UEdGraphPin* FalsePose = FindPin(*Selector, TEXT("BlendPose_1"), EGPD_Input);
	UEdGraphPin* SelectorOutput = FindPosePin(*Selector, EGPD_Output);
	UEdGraphPin* SelectorBool = FindBoolInput(*Selector);
	UEdGraphPin* GetterOutput = FindPin(*InputGetter, GET_MEMBER_NAME_CHECKED(
		UMHGZMotionMatchingAnimInstance, bKinsectUpperBodyOnly), EGPD_Output);
	const UAnimationGraphSchema* Schema = GetDefault<UAnimationGraphSchema>();
	if (!Schema || !CacheInput || !FullBaseOutput || !LayerBaseOutput || !KinsectSlotInput
		|| !KinsectSlotOutput || !LayerBaseInput || !LayerMontageInput || !LayerOutput
		|| !TruePose || !FalsePose || !SelectorOutput || !SelectorBool || !GetterOutput)
	{
		UE_LOG(LogTemp, Error, TEXT("[KinsectActionLayer] Required AnimGraph pins were not generated."));
		for (const UEdGraphNode* Node : Graph.Nodes)
		{
			if (!Node || (Node->NodeComment != BaseCacheComment
				&& Node->NodeComment != FullBodyBaseComment && Node->NodeComment != LayerBaseComment
				&& Node->NodeComment != SlotComment && Node->NodeComment != ActionPoseCacheComment
				&& Node->NodeComment != FullBodyActionPoseComment
				&& Node->NodeComment != LayerActionPoseComment && Node->NodeComment != BoneBlendComment
				&& Node->NodeComment != SelectorComment && Node->NodeComment != InputGetterComment))
			{
				continue;
			}
			for (const UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin)
				{
					UE_LOG(LogTemp, Error, TEXT("[KinsectActionLayer][Pin] %s.%s dir=%d category=%s subcategory=%s"),
						*Node->NodeComment, *Pin->PinName.ToString(), static_cast<int32>(Pin->Direction),
						*Pin->PinType.PinCategory.ToString(),
						*GetNameSafe(Pin->PinType.PinSubCategoryObject.Get()));
				}
			}
		}
		return false;
	}

	if (!Schema->TryCreateConnection(ExistingBaseOutput, CacheInput)
		|| !Schema->TryCreateConnection(FullBaseOutput, KinsectSlotInput)
		|| !Schema->TryCreateConnection(LayerBaseOutput, LayerBaseInput)
		|| !Schema->TryCreateConnection(KinsectSlotOutput, ActionPoseCacheInput)
		|| !Schema->TryCreateConnection(FullBodyActionOutput, FalsePose)
		|| !Schema->TryCreateConnection(LayerActionOutput, LayerMontageInput)
		|| !Schema->TryCreateConnection(LayerOutput, TruePose)
		|| !Schema->TryCreateConnection(GetterOutput, SelectorBool))
	{
		UE_LOG(LogTemp, Error, TEXT("[KinsectActionLayer] Could not connect the kinsect layer branches."));
		return false;
	}

	DefaultSlotSource->BreakAllPinLinks();
	if (!Schema->TryCreateConnection(SelectorOutput, DefaultSlotSource))
	{
		UE_LOG(LogTemp, Error, TEXT("[KinsectActionLayer] Could not reconnect the preserved DefaultSlot."));
		return false;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(&Blueprint);
	FKismetEditorUtilities::CompileBlueprint(&Blueprint);
	if (Blueprint.Status == BS_Error)
	{
		UE_LOG(LogTemp, Error, TEXT("[KinsectActionLayer] ABP_MH_Character compilation failed."));
		return false;
	}
	if (!AuditInstalledLayer(Graph, DefaultSlot, true))
	{
		UE_LOG(LogTemp, Error, TEXT("[KinsectActionLayer] Post-compile graph audit failed."));
		return false;
	}
	return SaveAsset(Blueprint);
}
}

#endif // WITH_EDITOR

UMHGZKinsectActionLayerCommandlet::UMHGZKinsectActionLayerCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMHGZKinsectActionLayerCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	using namespace UE::MHGZ::KinsectActionLayer;
	const bool bApply = FParse::Param(*Params, TEXT("Apply"));
	UAnimBlueprint* Blueprint = LoadObject<UAnimBlueprint>(nullptr, AnimBlueprintPath);
	if (!Blueprint)
	{
		UE_LOG(LogTemp, Error, TEXT("[KinsectActionLayer] Could not load %s."), AnimBlueprintPath);
		return 1;
	}
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	UEdGraph* AnimGraph = nullptr;
	for (UEdGraph* Graph : Graphs)
	{
		if (Graph && Graph->GetFName() == TEXT("AnimGraph"))
		{
			AnimGraph = Graph;
			break;
		}
	}
	if (!AnimGraph)
	{
		UE_LOG(LogTemp, Error, TEXT("[KinsectActionLayer] AnimGraph was not found."));
		return 1;
	}
	if (!Blueprint->TargetSkeleton || !Blueprint->TargetSkeleton->ContainsSlotName(KinsectSlotName))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[KinsectActionLayer] Target Skeleton does not register slot %s."),
			*KinsectSlotName.ToString());
		return 1;
	}
	if (!MontageUsesKinsectSlot(SendKinsectMontagePath)
		|| !MontageUsesKinsectSlot(RecallKinsectMontagePath))
	{
		return 1;
	}
	TArray<UAnimGraphNode_Slot*> DefaultSlots;
	AnimGraph->GetNodesOfClass<UAnimGraphNode_Slot>(DefaultSlots);
	DefaultSlots.RemoveAll([](const UAnimGraphNode_Slot* Slot)
	{
		return !Slot || Slot->Node.SlotName != DefaultSlotName;
	});
	if (DefaultSlots.Num() != 1)
	{
		UE_LOG(LogTemp, Error, TEXT("[KinsectActionLayer] Expected exactly one DefaultSlot; found %d."), DefaultSlots.Num());
		return 1;
	}

	const int32 MarkedNodes = CountInstalledNodes(*AnimGraph);
	if (bApply && MarkedNodes == 0)
	{
		if (!ApplyLayer(*Blueprint, *AnimGraph, *DefaultSlots[0])) return 1;
		UE_LOG(LogTemp, Display, TEXT("[KinsectActionLayer] Updated and saved %s."), AnimBlueprintPath);
		return 0;
	}
	if (MarkedNodes != 10)
	{
		UE_LOG(LogTemp, Error, TEXT("[KinsectActionLayer] Layer is absent or partial (%d/10); run with -Apply only for an absent layer."), MarkedNodes);
		return 2;
	}
	return AuditInstalledLayer(*AnimGraph, *DefaultSlots[0], true) ? 0 : 1;
#else
	return 1;
#endif
}
