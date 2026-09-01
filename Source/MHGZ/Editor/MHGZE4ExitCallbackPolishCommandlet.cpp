// Copyright MHGZ Project. All Rights Reserved.

#include "Editor/MHGZE4ExitCallbackPolishCommandlet.h"

#if WITH_EDITOR

#include "Animation/AnimBlueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_MakeArray.h"
#include "K2Node_PromotableOperator.h"
#include "K2Node_SwitchEnum.h"
#include "K2Node_Tunnel.h"
#include "K2Node_Variable.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace UE::MHGZ::E4ExitCallbackPolish
{
constexpr TCHAR AnimBlueprintPath[] =
	TEXT("/Game/Blueprints/Characters/Demo/Animation/ABP_MH_Character.ABP_MH_Character");
constexpr TCHAR UnsheathedExitDatabasePath[] =
	TEXT("/Game/Blueprints/Characters/Demo/Animation/MotionMatching/PSD_MH_UnSh_DodgeExit.PSD_MH_UnSh_DodgeExit");

const FName SheathedCallbackName(TEXT("OnShthMmUpdate"));
const FName UnsheathedCallbackName(TEXT("OnUnShMmUpdate"));
const FName SetDatabaseToSearchName(TEXT("SetDatabaseToSearch"));
const FName SetDatabasesToSearchName(TEXT("SetDatabasesToSearch"));
const FName QueueHandoffConsumptionName(TEXT("QueueMotionMatchingHandoffConsumption"));
const FName HandoffSerialName(TEXT("MMHandoffSerial"));
const FName UnsheathedExitDatabaseName(TEXT("MMUnShExitDatabase"));
const FName UnsheathedExitDurationName(TEXT("MMUnShExitDuration"));

struct FCallbackWorkingVariableSpec
{
	FName MemberName;
	FName LocalName;
};

const FCallbackWorkingVariableSpec CallbackWorkingVariables[] =
{
	{ TEXT("MotionMatchingNode"), TEXT("MotionMatchingNode") },
	{ TEXT("ResultSelectedTime"), TEXT("ResultSelectedTime") },
	{ TEXT("ResultSelectedDatabase"), TEXT("ResultSelectedDatabase") },
	{ TEXT("TailStartTime "), TEXT("TailStartTime") }
};

struct FMacroInputSpec
{
	const TCHAR* MacroName;
	const TCHAR* InputName;
	const TCHAR* LegacyMemberName;
	const TCHAR* LocalVariableName;
};

const FMacroInputSpec MacroInputSpecs[] =
{
	{ TEXT("ShouldOpenSheathedExitTail"), TEXT("SelectedTime"), TEXT("ResultSelectedTime"), TEXT("ResultSelectedTime") },
	{ TEXT("ShouldOpenSheathedExitTail"), TEXT("TailStartTime"), TEXT("TailStartTime "), TEXT("TailStartTime") },
	{ TEXT("HasSheathedExitHandedOffToMove"), TEXT("SelectedDatabase"), TEXT("ResultSelectedDatabase"), TEXT("ResultSelectedDatabase") },
	{ TEXT("ShouldForceFinishSheathedExit"), TEXT("SelectedTime"), TEXT("ResultSelectedTime"), TEXT("ResultSelectedTime") },
	{ TEXT("ShouldForceFinishSheathedExit"), TEXT("SelectedDatabase"), TEXT("ResultSelectedDatabase"), TEXT("ResultSelectedDatabase") },
	{ TEXT("ShouldOpenUnsheathedExitTail"), TEXT("SelectedTime"), TEXT("ResultSelectedTime"), TEXT("ResultSelectedTime") },
	{ TEXT("ShouldOpenUnsheathedExitTail"), TEXT("TailStartTime"), TEXT("TailStartTime "), TEXT("TailStartTime") },
	{ TEXT("HasUnsheathedExitHandedOffToMove"), TEXT("SelectedDatabase"), TEXT("ResultSelectedDatabase"), TEXT("ResultSelectedDatabase") },
	{ TEXT("ShouldForceFinishUnsheathedExit"), TEXT("SelectedTime"), TEXT("ResultSelectedTime"), TEXT("ResultSelectedTime") },
	{ TEXT("ShouldForceFinishUnsheathedExit"), TEXT("SelectedDatabase"), TEXT("ResultSelectedDatabase"), TEXT("ResultSelectedDatabase") }
};

struct FCommentSpec
{
	const TCHAR* Text;
	int32 X;
	int32 Y;
	int32 Width;
	int32 Height;
	FLinearColor Color;
};

const FCommentSpec UnsheathedCommentSpecs[] =
{
	{ TEXT("1. Snapshot current MM result and identify a new Handoff"), 832, -160, 1320, 300, FLinearColor(0.08f, 0.24f, 0.36f, 0.35f) },
	{ TEXT("2. Accept DodgeMoveExit: install Exit PSD and consume Handoff"), 2176, -160, 2080, 300, FLinearColor(0.08f, 0.24f, 0.36f, 0.35f) },
	{ TEXT("3. Exit tail: open normal Unsheathed Move candidates"), 2112, 160, 2240, 264, FLinearColor(0.12f, 0.30f, 0.18f, 0.35f) },
	{ TEXT("4. Natural Move takeover: restore the default database"), 2784, 432, 1664, 176, FLinearColor(0.20f, 0.28f, 0.12f, 0.35f) },
	{ TEXT("5. Final fallback: finish the Exit database at its last frame"), 2784, 608, 1664, 176, FLinearColor(0.34f, 0.16f, 0.08f, 0.35f) }
};

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

bool LinkPins(const UEdGraphSchema_K2& Schema, UEdGraphPin* From, UEdGraphPin* To)
{
	return From && To && (From->LinkedTo.Contains(To) || Schema.TryCreateConnection(From, To));
}

UEdGraph* FindFunctionGraph(UBlueprint& Blueprint, const FName Name)
{
	for (UEdGraph* Graph : Blueprint.FunctionGraphs)
	{
		if (Graph && Graph->GetFName() == Name)
		{
			return Graph;
		}
	}
	return nullptr;
}

UEdGraph* FindMacroGraph(UBlueprint& Blueprint, const FName Name)
{
	for (UEdGraph* Graph : Blueprint.MacroGraphs)
	{
		if (Graph && Graph->GetFName() == Name)
		{
			return Graph;
		}
	}
	return nullptr;
}

template <typename TNode>
TNode* FindVariableNode(UEdGraph& Graph, const FName VariableName)
{
	for (UEdGraphNode* Node : Graph.Nodes)
	{
		TNode* VariableNode = Cast<TNode>(Node);
		if (VariableNode && VariableNode->VariableReference.GetMemberName() == VariableName)
		{
			return VariableNode;
		}
	}
	return nullptr;
}

UK2Node_CallFunction* FindCall(UEdGraph& Graph, const FName FunctionName)
{
	for (UEdGraphNode* Node : Graph.Nodes)
	{
		UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node);
		if (Call && Call->FunctionReference.GetMemberName() == FunctionName)
		{
			return Call;
		}
	}
	return nullptr;
}

bool HasMemberVariable(const UBlueprint& Blueprint, const FName Name)
{
	return Blueprint.NewVariables.ContainsByPredicate([Name](const FBPVariableDescription& Variable)
	{
		return Variable.VarName == Name;
	});
}

UK2Node_VariableGet* CreateSelfGetter(UEdGraph& Graph, const FName Name, const int32 X, const int32 Y)
{
	UK2Node_VariableGet* Getter = NewObject<UK2Node_VariableGet>(&Graph);
	Graph.AddNode(Getter, true, false);
	Getter->CreateNewGuid();
	Getter->VariableReference.SetSelfMember(Name);
	Getter->NodePosX = X;
	Getter->NodePosY = Y;
	Getter->AllocateDefaultPins();
	return Getter;
}

UK2Node_VariableSet* CreateSelfSetter(UEdGraph& Graph, const FName Name, const int32 X, const int32 Y)
{
	UK2Node_VariableSet* Setter = NewObject<UK2Node_VariableSet>(&Graph);
	Graph.AddNode(Setter, true, false);
	Setter->CreateNewGuid();
	Setter->VariableReference.SetSelfMember(Name);
	Setter->NodePosX = X;
	Setter->NodePosY = Y;
	Setter->AllocateDefaultPins();
	return Setter;
}

bool EnsureUnsheathedExitDatabase(UAnimBlueprint& Blueprint, UEdGraph& Graph,
	const UEdGraphSchema_K2& Schema)
{
	UK2Node_CallFunction* SetDatabase = FindCall(Graph, SetDatabaseToSearchName);
	UK2Node_VariableSet* SetDuration = FindVariableNode<UK2Node_VariableSet>(Graph, UnsheathedExitDurationName);
	UK2Node_VariableGet* HandoffSerial = FindVariableNode<UK2Node_VariableGet>(Graph, HandoffSerialName);
	if (!SetDatabase || !SetDuration || !HandoffSerial)
	{
		return false;
	}

	UEdGraphPin* DatabasePin = SetDatabase->FindPin(TEXT("Database"), EGPD_Input);
	if (!DatabasePin)
	{
		return false;
	}

	if (!HasMemberVariable(Blueprint, UnsheathedExitDatabaseName))
	{
		if (!FBlueprintEditorUtils::AddMemberVariable(&Blueprint, UnsheathedExitDatabaseName,
			DatabasePin->PinType))
		{
			return false;
		}
	}

	UK2Node_VariableSet* SetExitDatabase = FindVariableNode<UK2Node_VariableSet>(Graph, UnsheathedExitDatabaseName);
	if (!SetExitDatabase)
	{
		SetExitDatabase = CreateSelfSetter(Graph, UnsheathedExitDatabaseName,
			SetDuration->NodePosX - 272, SetDuration->NodePosY);
	}
	if (!SetExitDatabase)
	{
		return false;
	}

	UEdGraphPin* ExitDatabaseValue = SetExitDatabase->FindPin(UnsheathedExitDatabaseName, EGPD_Input);
	UEdGraphPin* SetExitExecute = SetExitDatabase->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
	UEdGraphPin* SetExitThen = SetExitDatabase->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
	UEdGraphPin* DurationExecute = SetDuration->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
	if (!ExitDatabaseValue || !SetExitExecute || !SetExitThen || !DurationExecute)
	{
		return false;
	}
	UObject* ExitDatabase = LoadObject<UObject>(nullptr, UnsheathedExitDatabasePath);
	if (!ExitDatabase)
	{
		return false;
	}
	ExitDatabaseValue->DefaultObject = ExitDatabase;

	for (UEdGraphPin* Linked : TArray<UEdGraphPin*>(DurationExecute->LinkedTo))
	{
		if (Linked && Linked->GetOwningNode()->IsA<UK2Node_SwitchEnum>())
		{
			Linked->BreakLinkTo(DurationExecute);
			if (!LinkPins(Schema, Linked, SetExitExecute))
			{
				return false;
			}
		}
	}
	if (!LinkPins(Schema, SetExitThen, DurationExecute))
	{
		return false;
	}

	UK2Node_VariableGet* ExitDatabaseGetter = FindVariableNode<UK2Node_VariableGet>(Graph, UnsheathedExitDatabaseName);
	if (!ExitDatabaseGetter)
	{
		ExitDatabaseGetter = CreateSelfGetter(Graph, UnsheathedExitDatabaseName,
			SetDatabase->NodePosX - 208, SetDatabase->NodePosY + 128);
	}
	if (!ExitDatabaseGetter)
	{
		return false;
	}
	if (!LinkPins(Schema, ExitDatabaseGetter->FindPin(UnsheathedExitDatabaseName, EGPD_Output), DatabasePin))
	{
		return false;
	}

	UK2Node_CallFunction* SetDatabases = FindCall(Graph, SetDatabasesToSearchName);
	if (!SetDatabases)
	{
		return false;
	}
	UEdGraphPin* DatabasesPin = SetDatabases->FindPin(TEXT("Databases"), EGPD_Input);
	UK2Node_MakeArray* DatabasesArray = DatabasesPin
		? Cast<UK2Node_MakeArray>(DatabasesPin->LinkedTo.Num() > 0 ? DatabasesPin->LinkedTo[0]->GetOwningNode() : nullptr)
		: nullptr;
	if (!DatabasesArray)
	{
		return false;
	}
	if (!LinkPins(Schema, ExitDatabaseGetter->FindPin(UnsheathedExitDatabaseName, EGPD_Output),
		DatabasesArray->FindPin(TEXT("[0]"), EGPD_Input)))
	{
		return false;
	}

	UEdGraph* ForceFinishMacro = FindMacroGraph(Blueprint, TEXT("ShouldForceFinishUnsheathedExit"));
	if (!ForceFinishMacro)
	{
		return false;
	}
	UK2Node_PromotableOperator* DatabaseEquals = nullptr;
	for (UEdGraphNode* Node : ForceFinishMacro->Nodes)
	{
		UK2Node_PromotableOperator* Candidate = Cast<UK2Node_PromotableOperator>(Node);
		UEdGraphPin* B = Candidate ? Candidate->FindPin(TEXT("B"), EGPD_Input) : nullptr;
		if (B && B->DefaultObject == ExitDatabase)
		{
			DatabaseEquals = Candidate;
			break;
		}
	}
	if (!DatabaseEquals)
	{
		return false;
	}
	UK2Node_VariableGet* MacroExitDatabaseGetter = FindVariableNode<UK2Node_VariableGet>(*ForceFinishMacro,
		UnsheathedExitDatabaseName);
	if (!MacroExitDatabaseGetter)
	{
		MacroExitDatabaseGetter = CreateSelfGetter(*ForceFinishMacro, UnsheathedExitDatabaseName,
			DatabaseEquals->NodePosX - 224, DatabaseEquals->NodePosY + 80);
	}
	if (!LinkPins(Schema, MacroExitDatabaseGetter->FindPin(UnsheathedExitDatabaseName, EGPD_Output),
		DatabaseEquals->FindPin(TEXT("B"), EGPD_Input)))
	{
		return false;
	}

	UEdGraph* SheathedGraph = FindFunctionGraph(Blueprint, SheathedCallbackName);
	UK2Node_CallFunction* TemplateQueue = SheathedGraph
		? FindCall(*SheathedGraph, QueueHandoffConsumptionName)
		: nullptr;
	UK2Node_CallFunction* Queue = FindCall(Graph, QueueHandoffConsumptionName);
	if (!TemplateQueue)
	{
		return false;
	}
	if (!Queue)
	{
		Queue = NewObject<UK2Node_CallFunction>(&Graph);
		Graph.AddNode(Queue, true, false);
		Queue->CreateNewGuid();
		Queue->FunctionReference = TemplateQueue->FunctionReference;
		Queue->NodePosX = SetDatabase->NodePosX + 320;
		Queue->NodePosY = SetDatabase->NodePosY;
		Queue->AllocateDefaultPins();
	}
	UEdGraphPin* SetDatabaseThen = SetDatabase->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
	UEdGraphPin* QueueExecute = Queue->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
	UEdGraphPin* QueueThen = Queue->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
	UEdGraphPin* QueueSerial = Queue->FindPin(TEXT("HandoffSerial"), EGPD_Input);
	UK2Node_VariableSet* LastSerialSetter = FindVariableNode<UK2Node_VariableSet>(Graph,
		TEXT("MMUnShLastHandoffSerial"));
	UEdGraphPin* LastSerialExecute = LastSerialSetter
		? LastSerialSetter->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input)
		: nullptr;
	if (!SetDatabaseThen || !QueueExecute || !QueueThen || !QueueSerial || !LastSerialExecute)
	{
		return false;
	}
	for (UEdGraphPin* Linked : TArray<UEdGraphPin*>(SetDatabaseThen->LinkedTo))
	{
		if (Linked == LastSerialExecute)
		{
			SetDatabaseThen->BreakLinkTo(LastSerialExecute);
		}
	}
	if (!LinkPins(Schema, SetDatabaseThen, QueueExecute)
		|| !LinkPins(Schema, QueueThen, LastSerialExecute)
		|| !LinkPins(Schema, HandoffSerial->FindPin(HandoffSerialName, EGPD_Output), QueueSerial))
	{
		return false;
	}

	return true;
}

bool EnsureFunctionLocal(UAnimBlueprint& Blueprint, UEdGraph& Graph,
	const FName MemberVariableName, const FName LocalVariableName)
{
	FGuid LocalGuid = FBlueprintEditorUtils::FindLocalVariableGuidByName(&Blueprint, &Graph, LocalVariableName);
	UK2Node_Variable* ExistingVariableNode = nullptr;
	for (UEdGraphNode* Node : Graph.Nodes)
	{
		UK2Node_Variable* Candidate = Cast<UK2Node_Variable>(Node);
		if (Candidate && Candidate->VariableReference.GetMemberName() == MemberVariableName)
		{
			ExistingVariableNode = Candidate;
			break;
		}
	}
	if (!ExistingVariableNode)
	{
		if (LocalGuid.IsValid())
		{
			return true;
		}
		UE_LOG(LogTemp, Error, TEXT("[E4ExitCallbackPolish] No '%s' node or local '%s' in callback '%s'."),
			*MemberVariableName.ToString(), *LocalVariableName.ToString(), *Graph.GetName());
		return false;
	}

	UEdGraphPin* ValuePin = ExistingVariableNode->FindPin(MemberVariableName, EGPD_Output);
	if (!ValuePin)
	{
		ValuePin = ExistingVariableNode->FindPin(TEXT("Output_Get"), EGPD_Output);
	}
	if (!ValuePin)
	{
		ValuePin = ExistingVariableNode->FindPin(MemberVariableName, EGPD_Input);
	}
	if (!ValuePin)
	{
		UE_LOG(LogTemp, Error, TEXT("[E4ExitCallbackPolish] No value pin for '%s' in callback '%s'."),
			*MemberVariableName.ToString(), *Graph.GetName());
		return false;
	}

	if (!LocalGuid.IsValid())
	{
		if (!FBlueprintEditorUtils::AddLocalVariable(&Blueprint, &Graph, LocalVariableName, ValuePin->PinType))
		{
			return false;
		}
		LocalGuid = FBlueprintEditorUtils::FindLocalVariableGuidByName(&Blueprint, &Graph, LocalVariableName);
	}
	if (!LocalGuid.IsValid())
	{
		return false;
	}

	for (UEdGraphNode* Node : Graph.Nodes)
	{
		UK2Node_Variable* VariableNode = Cast<UK2Node_Variable>(Node);
		if (VariableNode && VariableNode->VariableReference.GetMemberName() == MemberVariableName)
		{
			VariableNode->Modify();
			VariableNode->VariableReference.SetLocalMember(LocalVariableName, Graph.GetName(), LocalGuid);
		}
	}
	return true;
}
bool AddMacroInputAndReplaceMemberGet(UEdGraph& MacroGraph, const FName InputName,
	const FName LegacyMemberName, const UEdGraphSchema_K2& Schema)
{
	UK2Node_Tunnel* Entry = nullptr;
	UK2Node_Tunnel* Exit = nullptr;
	bool bIsPure = false;
	FKismetEditorUtilities::GetInformationOnMacro(&MacroGraph, Entry, Exit, bIsPure);
	if (!Entry || !Exit)
	{
		return false;
	}

	TArray<UK2Node_VariableGet*> LegacyGetters;
	for (UEdGraphNode* Node : MacroGraph.Nodes)
	{
		UK2Node_VariableGet* Getter = Cast<UK2Node_VariableGet>(Node);
		if (Getter && Getter->VariableReference.GetMemberName() == LegacyMemberName)
		{
			LegacyGetters.Add(Getter);
		}
	}

	UEdGraphPin* EntryOutput = Entry->FindPin(InputName, EGPD_Output);
	if (!EntryOutput)
	{
		if (LegacyGetters.IsEmpty())
		{
			return false;
		}
		UEdGraphPin* LegacyValue = LegacyGetters[0]->FindPin(LegacyMemberName, EGPD_Output);
		if (!LegacyValue)
		{
			return false;
		}
		Entry->Modify();
		EntryOutput = Entry->CreateUserDefinedPin(InputName, LegacyValue->PinType, EGPD_Output, false);
	}
	if (!EntryOutput)
	{
		return false;
	}

	for (UK2Node_VariableGet* Getter : LegacyGetters)
	{
		UEdGraphPin* LegacyValue = Getter->FindPin(LegacyMemberName, EGPD_Output);
		if (!LegacyValue)
		{
			return false;
		}
		const TArray<UEdGraphPin*> LinkedPins = LegacyValue->LinkedTo;
		for (UEdGraphPin* LinkedPin : LinkedPins)
		{
			LegacyValue->BreakLinkTo(LinkedPin);
			if (!LinkPins(Schema, EntryOutput, LinkedPin))
			{
				return false;
			}
		}
		Getter->DestroyNode();
	}

	return true;
}

UK2Node_VariableGet* CreateLocalGetter(UEdGraph& Graph, const FName VariableName,
	const int32 X, const int32 Y, UAnimBlueprint& Blueprint)
{
	const FGuid LocalGuid = FBlueprintEditorUtils::FindLocalVariableGuidByName(&Blueprint, &Graph, VariableName);
	if (!LocalGuid.IsValid())
	{
		return nullptr;
	}
	UK2Node_VariableGet* Getter = NewObject<UK2Node_VariableGet>(&Graph);
	Graph.AddNode(Getter, true, false);
	Getter->CreateNewGuid();
	Getter->VariableReference.SetLocalMember(VariableName, Graph.GetName(), LocalGuid);
	Getter->NodePosX = X;
	Getter->NodePosY = Y;
	Getter->AllocateDefaultPins();
	return Getter;
}

bool LinkMacroInputToLocal(UAnimBlueprint& Blueprint, UEdGraph& CallbackGraph, UEdGraph& MacroGraph,
	const FName InputName, const FName LocalVariableName, const UEdGraphSchema_K2& Schema)
{
	TArray<UEdGraphNode*> CallbackNodes;
	CallbackNodes.Reserve(CallbackGraph.Nodes.Num());
	for (UEdGraphNode* Node : CallbackGraph.Nodes)
	{
		CallbackNodes.Add(Node);
	}

	int32 InstanceIndex = 0;
	for (UEdGraphNode* Node : CallbackNodes)
	{
		UK2Node_MacroInstance* Instance = Cast<UK2Node_MacroInstance>(Node);
		if (!Instance || Instance->GetMacroGraph() != &MacroGraph)
		{
			continue;
		}
		Instance->Modify();
		Instance->ReconstructNode();
		UEdGraphPin* Input = Instance->FindPin(InputName, EGPD_Input);
		if (!Input)
		{
			return false;
		}
		if (Input->LinkedTo.Num() == 0)
		{
			UK2Node_VariableGet* Getter = CreateLocalGetter(CallbackGraph, LocalVariableName,
				Instance->NodePosX - 256, Instance->NodePosY + (InstanceIndex++ * 96), Blueprint);
			if (!Getter || !LinkPins(Schema, Getter->FindPin(LocalVariableName, EGPD_Output), Input))
			{
				return false;
			}
		}
	}
	return true;
}
bool ConvertCallbackScratchToLocals(UAnimBlueprint& Blueprint, UEdGraph& CallbackGraph,
	const UEdGraphSchema_K2& Schema)
{
	for (const FCallbackWorkingVariableSpec& WorkingVariable : CallbackWorkingVariables)
	{
		if (!EnsureFunctionLocal(Blueprint, CallbackGraph,
			WorkingVariable.MemberName, WorkingVariable.LocalName))
		{
			return false;
		}
	}

	for (const FMacroInputSpec& Spec : MacroInputSpecs)
	{
		UEdGraph* MacroGraph = FindMacroGraph(Blueprint, Spec.MacroName);
		if (!MacroGraph)
		{
			UE_LOG(LogTemp, Error, TEXT("[E4ExitCallbackPolish] Macro '%s' was not found."), Spec.MacroName);
			return false;
		}
		if (!AddMacroInputAndReplaceMemberGet(*MacroGraph, Spec.InputName, Spec.LegacyMemberName, Schema))
		{
			UE_LOG(LogTemp, Error, TEXT("[E4ExitCallbackPolish] Macro input '%s' could not be installed in '%s'."),
				Spec.InputName, Spec.MacroName);
			return false;
		}
		if (!LinkMacroInputToLocal(Blueprint, CallbackGraph, *MacroGraph, Spec.InputName,
			Spec.LocalVariableName, Schema))
		{
			UE_LOG(LogTemp, Error, TEXT("[E4ExitCallbackPolish] Macro input '%s' could not be wired to local '%s' in '%s'."),
				Spec.InputName, Spec.LocalVariableName, *CallbackGraph.GetName());
			return false;
		}
	}
	return true;
}

void CollectGraphAndChildren(UEdGraph& Graph, TSet<const UEdGraph*>& Visited, TArray<UEdGraph*>& OutGraphs)
{
	if (Visited.Contains(&Graph))
	{
		return;
	}
	Visited.Add(&Graph);
	OutGraphs.Add(&Graph);
	for (UEdGraph* Child : Graph.SubGraphs)
	{
		if (Child)
		{
			CollectGraphAndChildren(*Child, Visited, OutGraphs);
		}
	}
}

bool HasRemainingWorkingMemberReferences(UAnimBlueprint& Blueprint)
{
	TSet<const UEdGraph*> Visited;
	TArray<UEdGraph*> Graphs;
	for (UEdGraph* Graph : Blueprint.FunctionGraphs)
	{
		if (Graph)
		{
			CollectGraphAndChildren(*Graph, Visited, Graphs);
		}
	}
	for (UEdGraph* Graph : Blueprint.MacroGraphs)
	{
		if (Graph)
		{
			CollectGraphAndChildren(*Graph, Visited, Graphs);
		}
	}

	for (UEdGraph* Graph : Graphs)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			const UK2Node_Variable* VariableNode = Cast<UK2Node_Variable>(Node);
			if (!VariableNode || VariableNode->VariableReference.IsLocalScope())
			{
				continue;
			}
			for (const FCallbackWorkingVariableSpec& WorkingVariable : CallbackWorkingVariables)
			{
				if (VariableNode->VariableReference.GetMemberName() == WorkingVariable.MemberName)
				{
					UE_LOG(LogTemp, Error, TEXT("[E4ExitCallbackPolish] Remaining member reference '%s' in graph '%s'."),
						*WorkingVariable.MemberName.ToString(), *Graph->GetName());
					return true;
				}
			}
		}
	}
	return false;
}

void AddOrUpdateComment(UEdGraph& Graph, const FCommentSpec& Spec)
{
	for (UEdGraphNode* Node : Graph.Nodes)
	{
		UEdGraphNode_Comment* Existing = Cast<UEdGraphNode_Comment>(Node);
		if (Existing && Existing->NodeComment == Spec.Text)
		{
			Existing->Modify();
			if (!Existing->NodeGuid.IsValid())
			{
				Existing->CreateNewGuid();
			}
			Existing->NodePosX = Spec.X;
			Existing->NodePosY = Spec.Y;
			Existing->NodeWidth = Spec.Width;
			Existing->NodeHeight = Spec.Height;
			Existing->CommentColor = Spec.Color;
			Existing->bCommentBubbleVisible = false;
			return;
		}
	}

	UEdGraphNode_Comment* Comment = NewObject<UEdGraphNode_Comment>(&Graph);
	Graph.AddNode(Comment, true, false);
	Comment->CreateNewGuid();
	Comment->NodeComment = Spec.Text;
	Comment->NodePosX = Spec.X;
	Comment->NodePosY = Spec.Y;
	Comment->NodeWidth = Spec.Width;
	Comment->NodeHeight = Spec.Height;
	Comment->CommentColor = Spec.Color;
	Comment->bCommentBubbleVisible = false;
}

bool ApplyPolish(UAnimBlueprint& Blueprint)
{
	UEdGraph* SheathedGraph = FindFunctionGraph(Blueprint, SheathedCallbackName);
	UEdGraph* UnsheathedGraph = FindFunctionGraph(Blueprint, UnsheathedCallbackName);
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	if (!SheathedGraph || !UnsheathedGraph || !Schema)
	{
		UE_LOG(LogTemp, Error, TEXT("[E4ExitCallbackPolish] Required callback graph or K2 schema was not found."));
		return false;
	}

	Blueprint.Modify();
	SheathedGraph->Modify();
	UnsheathedGraph->Modify();
	if (!EnsureUnsheathedExitDatabase(Blueprint, *UnsheathedGraph, *Schema))
	{
		UE_LOG(LogTemp, Error, TEXT("[E4ExitCallbackPolish] Unsheathed handoff repair failed."));
		return false;
	}
	if (!ConvertCallbackScratchToLocals(Blueprint, *SheathedGraph, *Schema))
	{
		UE_LOG(LogTemp, Error, TEXT("[E4ExitCallbackPolish] Sheathed callback-local conversion failed."));
		return false;
	}
	if (!ConvertCallbackScratchToLocals(Blueprint, *UnsheathedGraph, *Schema))
	{
		UE_LOG(LogTemp, Error, TEXT("[E4ExitCallbackPolish] Unsheathed callback-local conversion failed."));
		return false;
	}

	for (const FCommentSpec& Spec : UnsheathedCommentSpecs)
	{
		AddOrUpdateComment(*UnsheathedGraph, Spec);
	}

	if (HasRemainingWorkingMemberReferences(Blueprint))
	{
		return false;
	}
	for (const FCallbackWorkingVariableSpec& WorkingVariable : CallbackWorkingVariables)
	{
		if (HasMemberVariable(Blueprint, WorkingVariable.MemberName))
		{
			FBlueprintEditorUtils::RemoveMemberVariable(&Blueprint, WorkingVariable.MemberName);
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(&Blueprint);
	FKismetEditorUtilities::CompileBlueprint(&Blueprint);
	if (Blueprint.Status == BS_Error)
	{
		UE_LOG(LogTemp, Error, TEXT("[E4ExitCallbackPolish] Blueprint compilation failed after the migration."));
		return false;
	}
	if (!SaveAsset(Blueprint))
	{
		UE_LOG(LogTemp, Error, TEXT("[E4ExitCallbackPolish] Blueprint compilation succeeded but package save failed."));
		return false;
	}
	return true;
}
}

#endif

UMHGZE4ExitCallbackPolishCommandlet::UMHGZE4ExitCallbackPolishCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMHGZE4ExitCallbackPolishCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	using namespace UE::MHGZ::E4ExitCallbackPolish;
	(void)Params;

	UAnimBlueprint* AnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, AnimBlueprintPath);
	if (!AnimBlueprint || !ApplyPolish(*AnimBlueprint))
	{
		UE_LOG(LogTemp, Error, TEXT("[E4ExitCallbackPolish] Failed to update %s."), AnimBlueprintPath);
		return 1;
	}

	UE_LOG(LogTemp, Display, TEXT("[E4ExitCallbackPolish] Updated and saved %s."), AnimBlueprintPath);
	return 0;
#else
	return 1;
#endif
}
