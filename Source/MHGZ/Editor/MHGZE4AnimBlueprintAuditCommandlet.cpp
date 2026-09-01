// Copyright MHGZ Project. All Rights Reserved.

#include "Editor/MHGZE4AnimBlueprintAuditCommandlet.h"

#if WITH_EDITOR

#include "Animation/AnimBlueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node_Composite.h"
#include "K2Node_MacroInstance.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace UE::MHGZ::E4AnimBlueprintAudit
{
constexpr TCHAR AnimBlueprintPath[] =
	TEXT("/Game/Blueprints/Characters/Demo/Animation/ABP_MH_Character.ABP_MH_Character");
constexpr TCHAR AuditDirectoryName[] = TEXT("ActionExitAudit");
constexpr TCHAR AuditFilename[] = TEXT("E4_2_AnimBlueprintGraphAudit.md");

FString Sanitize(const FString& Value)
{
	FString Result = Value;
	Result.ReplaceInline(TEXT("\r"), TEXT(" "));
	Result.ReplaceInline(TEXT("\n"), TEXT(" "));
	Result.ReplaceInline(TEXT("|"), TEXT("\\|"));
	return Result;
}

bool IsRelevantFunctionGraph(const UEdGraph& Graph)
{
	const FString GraphName = Graph.GetName();
	return GraphName.Contains(TEXT("MmUpdate"), ESearchCase::IgnoreCase)
		|| GraphName.Contains(TEXT("MotionMatching"), ESearchCase::IgnoreCase)
		|| GraphName.Contains(TEXT("Exit"), ESearchCase::IgnoreCase);
}

FString DescribeNode(const UEdGraphNode& Node)
{
	return FString::Printf(TEXT("%s :: %s"), *Node.GetClass()->GetName(),
		*Sanitize(Node.GetNodeTitle(ENodeTitleType::ListView).ToString()));
}

FString DescribePinDefault(const UEdGraphPin& Pin)
{
	TArray<FString> Parts;
	if (Pin.DefaultObject)
	{
		Parts.Add(FString::Printf(TEXT("DefaultObject=%s"), *Pin.DefaultObject->GetPathName()));
	}
	if (!Pin.DefaultValue.IsEmpty())
	{
		Parts.Add(FString::Printf(TEXT("DefaultValue=%s"), *Sanitize(Pin.DefaultValue)));
	}

	return FString::Join(Parts, TEXT(", "));
}

void AppendGraphAudit(TArray<FString>& Lines, const UEdGraph& Graph)
{
	Lines.Add(FString::Printf(TEXT("## %s"), *Graph.GetName()));
	Lines.Add(FString::Printf(TEXT("- Nodes: %d"), Graph.Nodes.Num()));
	Lines.Add(TEXT(""));

	for (int32 NodeIndex = 0; NodeIndex < Graph.Nodes.Num(); ++NodeIndex)
	{
		const UEdGraphNode* Node = Graph.Nodes[NodeIndex];
		if (!Node)
		{
			continue;
		}

		Lines.Add(FString::Printf(TEXT("- Node %02d | %s | Position=(%d, %d)"), NodeIndex,
			*DescribeNode(*Node), Node->NodePosX, Node->NodePosY));
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin)
			{
				continue;
			}

			const TCHAR* Direction = Pin->Direction == EGPD_Input ? TEXT("In") : TEXT("Out");
			const FString DefaultDescription = DescribePinDefault(*Pin);
			if (Pin->LinkedTo.IsEmpty())
			{
				if (!DefaultDescription.IsEmpty())
				{
					Lines.Add(FString::Printf(TEXT("  - %s `%s` [%s] | %s"), Direction,
						*Pin->PinName.ToString(), *Pin->PinType.PinCategory.ToString(), *DefaultDescription));
				}
				continue;
			}

			for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				const UEdGraphNode* LinkedNode = LinkedPin ? LinkedPin->GetOwningNode() : nullptr;
				const FString DefaultSuffix = DefaultDescription.IsEmpty()
					? FString()
					: FString::Printf(TEXT(" | %s"), *DefaultDescription);
				Lines.Add(FString::Printf(TEXT("  - %s `%s` [%s] -> %s . `%s`%s"), Direction,
					*Pin->PinName.ToString(), *Pin->PinType.PinCategory.ToString(),
					LinkedNode ? *DescribeNode(*LinkedNode) : TEXT("<missing node>"),
					LinkedPin ? *LinkedPin->PinName.ToString() : TEXT("<missing pin>"), *DefaultSuffix));
			}
		}
		Lines.Add(TEXT(""));
	}
}

void CollectGraphAndChildren(const UEdGraph& Graph, TSet<const UEdGraph*>& VisitedGraphs,
	TArray<const UEdGraph*>& OutGraphs)
{
	if (VisitedGraphs.Contains(&Graph))
	{
		return;
	}

	VisitedGraphs.Add(&Graph);
	OutGraphs.Add(&Graph);
	for (const UEdGraph* SubGraph : Graph.SubGraphs)
	{
		if (SubGraph)
		{
			CollectGraphAndChildren(*SubGraph, VisitedGraphs, OutGraphs);
		}
	}

	for (const UEdGraphNode* Node : Graph.Nodes)
	{
		if (const UK2Node_MacroInstance* MacroInstance = Cast<UK2Node_MacroInstance>(Node))
		{
			if (const UEdGraph* MacroGraph = MacroInstance->GetMacroGraph())
			{
				CollectGraphAndChildren(*MacroGraph, VisitedGraphs, OutGraphs);
			}
		}
		else if (const UK2Node_Composite* Composite = Cast<UK2Node_Composite>(Node))
		{
			if (const UEdGraph* BoundGraph = Composite->BoundGraph)
			{
				CollectGraphAndChildren(*BoundGraph, VisitedGraphs, OutGraphs);
			}
		}
	}
}

bool WriteAudit(const TArray<FString>& Lines, FString& OutPath)
{
	const FString Directory = FPaths::ProjectSavedDir() / AuditDirectoryName;
	IFileManager::Get().MakeDirectory(*Directory, true);
	OutPath = Directory / AuditFilename;
	return FFileHelper::SaveStringToFile(FString::Join(Lines, TEXT("\n")), *OutPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
}

#endif

UMHGZE4AnimBlueprintAuditCommandlet::UMHGZE4AnimBlueprintAuditCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMHGZE4AnimBlueprintAuditCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	using namespace UE::MHGZ::E4AnimBlueprintAudit;

	UAnimBlueprint* AnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		UE_LOG(LogTemp, Error, TEXT("[E4AnimBlueprintAudit] Could not load %s."), AnimBlueprintPath);
		return 1;
	}

	TArray<FString> Lines;
	Lines.Add(TEXT("# E4.2 AnimBlueprint State Updated Callback Audit"));
	Lines.Add(TEXT(""));
	Lines.Add(TEXT("Read-only diagnostic. It does not compile, modify, or save the Blueprint asset."));
	Lines.Add(FString::Printf(TEXT("- Blueprint: %s"), *AnimBlueprint->GetPathName()));
	Lines.Add(TEXT("- Included function graphs: names containing MmUpdate, MotionMatching, or Exit."));
	Lines.Add(TEXT(""));

	TSet<const UEdGraph*> VisitedGraphs;
	TArray<const UEdGraph*> GraphsToAudit;
	for (const UEdGraph* Graph : AnimBlueprint->FunctionGraphs)
	{
		if (!Graph || !IsRelevantFunctionGraph(*Graph))
		{
			continue;
		}

		CollectGraphAndChildren(*Graph, VisitedGraphs, GraphsToAudit);
	}

	if (GraphsToAudit.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("[E4AnimBlueprintAudit] No relevant function graphs found in %s."),
			*AnimBlueprint->GetPathName());
		return 1;
	}

	for (const UEdGraph* Graph : GraphsToAudit)
	{
		AppendGraphAudit(Lines, *Graph);
	}

	FString OutputPath;
	if (!WriteAudit(Lines, OutputPath))
	{
		UE_LOG(LogTemp, Error, TEXT("[E4AnimBlueprintAudit] Failed to write audit report."));
		return 1;
	}

	UE_LOG(LogTemp, Display, TEXT("[E4AnimBlueprintAudit] Wrote %s (%d graph(s), including referenced macro/collapsed graphs)."),
		*OutputPath, GraphsToAudit.Num());
	return 0;
#else
	UE_LOG(LogTemp, Error, TEXT("[E4AnimBlueprintAudit] This commandlet requires an editor build."));
	return 1;
#endif
}
