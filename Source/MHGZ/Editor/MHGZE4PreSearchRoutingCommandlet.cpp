// Copyright MHGZ Project. All Rights Reserved.

#include "Editor/MHGZE4PreSearchRoutingCommandlet.h"

#if WITH_EDITOR

#include "Animation/AnimBlueprint.h"
#include "AnimGraphNode_MotionMatching.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

namespace UE::MHGZ::E4PreSearchRouting
{
constexpr TCHAR AnimBlueprintPath[] =
	TEXT("/Game/Blueprints/Characters/Demo/Animation/ABP_MH_Character.ABP_MH_Character");
const FName LegacySheathedStateUpdatedName(TEXT("OnShthMmUpdate"));
const FName LegacyUnsheathedStateUpdatedName(TEXT("OnUnShMmUpdate"));
const FName SheathedPreUpdateName(TEXT("OnSheathedMotionMatchingPreUpdate"));
const FName UnsheathedPreUpdateName(TEXT("OnUnsheathedMotionMatchingPreUpdate"));
const FName SheathedStateUpdatedName(TEXT("OnSheathedMotionMatchingStateUpdated"));
const FName UnsheathedStateUpdatedName(TEXT("OnUnsheathedMotionMatchingStateUpdated"));

bool SaveAsset(UObject& Asset)
{
	UPackage* Package = Asset.GetOutermost();
	FString Filename;
	if (!Package || !FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), Filename,
		FPackageName::GetAssetPackageExtension()))
	{
		return false;
	}

	Package->MarkPackageDirty();
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	return UPackage::SavePackage(Package, &Asset, *Filename, SaveArgs);
}

FMemberReference* GetMemberReference(UAnimGraphNode_MotionMatching& Node, const FName PropertyName)
{
	FStructProperty* Property = FindFProperty<FStructProperty>(Node.GetClass(), PropertyName);
	if (!Property)
	{
		return nullptr;
	}
	return Property->ContainerPtrToValuePtr<FMemberReference>(&Node);
}

const FMemberReference* GetMemberReference(const UAnimGraphNode_MotionMatching& Node,
	const FName PropertyName)
{
	const FStructProperty* Property = FindFProperty<FStructProperty>(Node.GetClass(), PropertyName);
	if (!Property)
	{
		return nullptr;
	}
	return Property->ContainerPtrToValuePtr<FMemberReference>(&Node);
}

bool IsSheathedNode(const UAnimGraphNode_MotionMatching& Node)
{
	const FMemberReference* StateUpdated = GetMemberReference(Node,
		TEXT("OnMotionMatchingStateUpdatedFunction"));
	const FName StateUpdatedName = StateUpdated ? StateUpdated->GetMemberName() : NAME_None;
	const FName UpdateName = Node.UpdateFunction.GetMemberName();
	return StateUpdatedName == LegacySheathedStateUpdatedName
		|| StateUpdatedName == SheathedStateUpdatedName
		|| UpdateName == SheathedPreUpdateName;
}

bool IsUnsheathedNode(const UAnimGraphNode_MotionMatching& Node)
{
	const FMemberReference* StateUpdated = GetMemberReference(Node,
		TEXT("OnMotionMatchingStateUpdatedFunction"));
	const FName StateUpdatedName = StateUpdated ? StateUpdated->GetMemberName() : NAME_None;
	const FName UpdateName = Node.UpdateFunction.GetMemberName();
	return StateUpdatedName == LegacyUnsheathedStateUpdatedName
		|| StateUpdatedName == UnsheathedStateUpdatedName
		|| UpdateName == UnsheathedPreUpdateName;
}
}

#endif

UMHGZE4PreSearchRoutingCommandlet::UMHGZE4PreSearchRoutingCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMHGZE4PreSearchRoutingCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	using namespace UE::MHGZ::E4PreSearchRouting;
	UAnimBlueprint* AnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		UE_LOG(LogTemp, Error, TEXT("[E4PreSearchRouting] Could not load %s."), AnimBlueprintPath);
		return 1;
	}

	TArray<UEdGraph*> Graphs = AnimBlueprint->UbergraphPages;
	Graphs.Append(AnimBlueprint->FunctionGraphs);
	TArray<UAnimGraphNode_MotionMatching*> MotionMatchingNodes;
	for (UEdGraph* Graph : Graphs)
	{
		if (!Graph)
		{
			continue;
		}

		for (UEdGraphNode* GraphNode : Graph->Nodes)
		{
			if (UAnimGraphNode_MotionMatching* MotionMatchingNode =
				Cast<UAnimGraphNode_MotionMatching>(GraphNode))
			{
				MotionMatchingNodes.Add(MotionMatchingNode);
			}
		}
	}

	if (MotionMatchingNodes.Num() != 2)
	{
		UE_LOG(LogTemp, Error, TEXT("[E4PreSearchRouting] Expected two Motion Matching nodes; found %d."),
			MotionMatchingNodes.Num());
		return 1;
	}

	int32 SheathedCount = 0;
	int32 UnsheathedCount = 0;
	AnimBlueprint->Modify();
	for (UAnimGraphNode_MotionMatching* MotionMatchingNode : MotionMatchingNodes)
	{
		if (!MotionMatchingNode)
		{
			continue;
		}

		const bool bSheathed = IsSheathedNode(*MotionMatchingNode);
		const bool bUnsheathed = IsUnsheathedNode(*MotionMatchingNode);
		if (bSheathed == bUnsheathed)
		{
			const FMemberReference* StateUpdated = GetMemberReference(*MotionMatchingNode,
				TEXT("OnMotionMatchingStateUpdatedFunction"));
			UE_LOG(LogTemp, Error,
				TEXT("[E4PreSearchRouting] Could not identify stance for MM node %s (Update=%s StateUpdated=%s)."),
				*MotionMatchingNode->GetName(), *MotionMatchingNode->UpdateFunction.GetMemberName().ToString(),
				StateUpdated ? *StateUpdated->GetMemberName().ToString() : TEXT("<missing>"));
			return 1;
		}

		MotionMatchingNode->Modify();
		FMemberReference* StateUpdated = GetMemberReference(*MotionMatchingNode,
			TEXT("OnMotionMatchingStateUpdatedFunction"));
		if (!StateUpdated)
		{
			UE_LOG(LogTemp, Error, TEXT("[E4PreSearchRouting] %s has no serialized state-updated binding."),
				*MotionMatchingNode->GetName());
			return 1;
		}
		MotionMatchingNode->UpdateFunction.SetSelfMember(
			bSheathed ? SheathedPreUpdateName : UnsheathedPreUpdateName);
		StateUpdated->SetSelfMember(bSheathed ? SheathedStateUpdatedName : UnsheathedStateUpdatedName);
		if (bSheathed)
		{
			++SheathedCount;
		}
		else
		{
			++UnsheathedCount;
		}
		UE_LOG(LogTemp, Display,
			TEXT("[E4PreSearchRouting] %s: Update=%s; StateUpdated=%s."),
			*MotionMatchingNode->GetName(),
			bSheathed ? *SheathedPreUpdateName.ToString() : *UnsheathedPreUpdateName.ToString(),
			bSheathed ? *SheathedStateUpdatedName.ToString() : *UnsheathedStateUpdatedName.ToString());
	}

	if (SheathedCount != 1 || UnsheathedCount != 1)
	{
		UE_LOG(LogTemp, Error, TEXT("[E4PreSearchRouting] Expected one node for each stance; got Sheathed=%d Unsheathed=%d."),
			SheathedCount, UnsheathedCount);
		return 1;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
	FKismetEditorUtilities::CompileBlueprint(AnimBlueprint);
	if (AnimBlueprint->Status == BS_Error)
	{
		UE_LOG(LogTemp, Error, TEXT("[E4PreSearchRouting] AnimBlueprint compilation failed."));
		return 1;
	}
	if (!SaveAsset(*AnimBlueprint))
	{
		UE_LOG(LogTemp, Error, TEXT("[E4PreSearchRouting] Failed to save %s."),
			*AnimBlueprint->GetPathName());
		return 1;
	}

	UE_LOG(LogTemp, Display, TEXT("[E4PreSearchRouting] Updated and saved %s."),
		*AnimBlueprint->GetPathName());
	return 0;
#else
	UE_LOG(LogTemp, Error, TEXT("[E4PreSearchRouting] This commandlet requires an editor build."));
	return 1;
#endif
}