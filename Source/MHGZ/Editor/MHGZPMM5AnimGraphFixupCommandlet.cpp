// Copyright MHGZ Project. All Rights Reserved.

#include "Editor/MHGZPMM5AnimGraphFixupCommandlet.h"

#if WITH_EDITOR

#include "Animation/AnimBlueprint.h"
#include "AnimGraphNode_MotionMatching.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/Parse.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

namespace
{
constexpr TCHAR AnimBlueprintPath[] = TEXT("/Game/Blueprints/Characters/Demo/Animation/ABP_MH_Character.ABP_MH_Character");
constexpr float DefaultSearchThrottle = 0.12f;
constexpr float DefaultPoseReselectHistory = 0.30f;

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

bool SetNodeFloat(UAnimGraphNode_MotionMatching& MotionMatchingNode, const FName PropertyName,
	const float Value)
{
	FStructProperty* NodeProperty = FindFProperty<FStructProperty>(MotionMatchingNode.GetClass(),
		TEXT("Node"));
	if (!NodeProperty)
	{
		return false;
	}

	FFloatProperty* Property = FindFProperty<FFloatProperty>(NodeProperty->Struct, PropertyName);
	if (!Property)
	{
		return false;
	}

	Property->SetPropertyValue_InContainer(NodeProperty->ContainerPtrToValuePtr<void>(&MotionMatchingNode),
		Value);
	return true;
}

bool SetNodeBool(UAnimGraphNode_MotionMatching& MotionMatchingNode, const FName PropertyName,
	const bool Value)
{
	FStructProperty* NodeProperty = FindFProperty<FStructProperty>(MotionMatchingNode.GetClass(),
		TEXT("Node"));
	if (!NodeProperty)
	{
		return false;
	}

	FBoolProperty* Property = FindFProperty<FBoolProperty>(NodeProperty->Struct, PropertyName);
	if (!Property)
	{
		return false;
	}

	Property->SetPropertyValue_InContainer(NodeProperty->ContainerPtrToValuePtr<void>(&MotionMatchingNode),
		Value);
	return true;
}

bool SetNodeInt(UAnimGraphNode_MotionMatching& MotionMatchingNode, const FName PropertyName,
	const int32 Value)
{
	FStructProperty* NodeProperty = FindFProperty<FStructProperty>(MotionMatchingNode.GetClass(),
		TEXT("Node"));
	if (!NodeProperty)
	{
		return false;
	}

	FIntProperty* Property = FindFProperty<FIntProperty>(NodeProperty->Struct, PropertyName);
	if (!Property)
	{
		return false;
	}

	Property->SetPropertyValue_InContainer(NodeProperty->ContainerPtrToValuePtr<void>(&MotionMatchingNode),
		Value);
	return true;
}
}

#endif

UMHGZPMM5AnimGraphFixupCommandlet::UMHGZPMM5AnimGraphFixupCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMHGZPMM5AnimGraphFixupCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	float SearchThrottle = DefaultSearchThrottle;
	float PoseReselectHistory = DefaultPoseReselectHistory;
	FParse::Value(*Params, TEXT("SearchThrottle="), SearchThrottle);
	FParse::Value(*Params, TEXT("PoseReselectHistory="), PoseReselectHistory);
	if (SearchThrottle < 0.05f || SearchThrottle > 0.25f || PoseReselectHistory < 0.0f
		|| PoseReselectHistory > 1.0f)
	{
		UE_LOG(LogTemp, Error, TEXT("[PMM5AnimGraphFixup] SearchThrottle must be [0.05, 0.25] and PoseReselectHistory must be [0, 1]."));
		return 1;
	}

	UAnimBlueprint* AnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		UE_LOG(LogTemp, Error, TEXT("[PMM5AnimGraphFixup] Could not load %s."), AnimBlueprintPath);
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
			if (UAnimGraphNode_MotionMatching* MotionMatchingNode = Cast<UAnimGraphNode_MotionMatching>(GraphNode))
			{
				MotionMatchingNodes.Add(MotionMatchingNode);
			}
		}
	}

	if (MotionMatchingNodes.Num() != 2)
	{
		UE_LOG(LogTemp, Error, TEXT("[PMM5AnimGraphFixup] Expected exactly two Motion Matching nodes in ABP_MH_Character; found %d."),
			MotionMatchingNodes.Num());
		return 1;
	}

	AnimBlueprint->Modify();
	for (UAnimGraphNode_MotionMatching* MotionMatchingNode : MotionMatchingNodes)
	{
		MotionMatchingNode->Modify();
		const bool bConfigured =
			SetNodeFloat(*MotionMatchingNode, TEXT("SearchThrottleTime"), SearchThrottle)
			&& SetNodeFloat(*MotionMatchingNode, TEXT("PoseReselectHistory"), PoseReselectHistory)
			&& SetNodeBool(*MotionMatchingNode, TEXT("bResetOnBecomingRelevant"), true)
			&& SetNodeFloat(*MotionMatchingNode, TEXT("BlendTime"), 0.0f)
			&& SetNodeBool(*MotionMatchingNode, TEXT("bUseInertialBlend"), false)
			&& SetNodeInt(*MotionMatchingNode, TEXT("MaxActiveBlends"), 0);
		if (!bConfigured)
		{
			UE_LOG(LogTemp, Error, TEXT("[PMM5AnimGraphFixup] Could not reflect the serialized MM settings for node %s."),
				*MotionMatchingNode->GetName());
			return 1;
		}
		UE_LOG(LogTemp, Display, TEXT("[PMM5AnimGraphFixup] Updated node %s: Throttle=%.3f ReselectHistory=%.3f ResetOnRelevant=1 Blend=0."),
			*MotionMatchingNode->GetName(), SearchThrottle, PoseReselectHistory);
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(AnimBlueprint);
	FKismetEditorUtilities::CompileBlueprint(AnimBlueprint);
	if (AnimBlueprint->Status == BS_Error)
	{
		UE_LOG(LogTemp, Error, TEXT("[PMM5AnimGraphFixup] Blueprint compilation failed for %s."), *AnimBlueprint->GetPathName());
		return 1;
	}
	if (!SaveAsset(*AnimBlueprint))
	{
		UE_LOG(LogTemp, Error, TEXT("[PMM5AnimGraphFixup] Failed to save %s."), *AnimBlueprint->GetPathName());
		return 1;
	}

	UE_LOG(LogTemp, Display, TEXT("[PMM5AnimGraphFixup] Saved %s."), *AnimBlueprint->GetPathName());
	return 0;
#else
	UE_LOG(LogTemp, Error, TEXT("[PMM5AnimGraphFixup] This commandlet requires an editor build."));
	return 1;
#endif
}
