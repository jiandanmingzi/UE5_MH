// Copyright MHGZ Project. All Rights Reserved.

#include "Editor/MHGZE4ActionIdleRoutingCommandlet.h"

#if WITH_EDITOR

#include "Animation/AnimBlueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "PoseSearch/MotionMatchingAnimNodeLibrary.h"
#include "Misc/PackageName.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace UE::MHGZ::E4ActionIdleRouting
{
constexpr TCHAR AnimBlueprintPath[] =
	TEXT("/Game/Blueprints/Characters/Demo/Animation/ABP_MH_Character.ABP_MH_Character");
constexpr TCHAR SheathedIdleDatabasePath[] =
	TEXT("/Game/Blueprints/Characters/Demo/Animation/MotionMatching/PSD_MH_Shth_ActionIdle.PSD_MH_Shth_ActionIdle");
constexpr TCHAR UnsheathedIdleDatabasePath[] =
	TEXT("/Game/Blueprints/Characters/Demo/Animation/MotionMatching/PSD_MH_UnSh_ActionIdle.PSD_MH_UnSh_ActionIdle");

const FName SetDatabaseToSearchName(TEXT("SetDatabaseToSearch"));
const FName SetDatabasesToSearchName(TEXT("SetDatabasesToSearch"));
const FName ResetDatabasesToSearchName(TEXT("ResetDatabasesToSearch"));
const FName QueueHandoffConsumptionName(TEXT("QueueMotionMatchingHandoffConsumption"));
const FName QueueActionIdleConsumptionName(TEXT("QueueMotionMatchingActionIdleContextConsumption"));
const FName MotionMatchingNodeLocalName(TEXT("MotionMatchingNode"));
const FName ActionIdleContextSerialName(TEXT("MMActionIdleContextSerial"));
const FName HasLocomotionInputName(TEXT("bMMHasLocomotionInput"));
const FName ExitWantsMoveName(TEXT("bMMExitWantsMoveDatabase"));
const FName ResultSelectedDatabaseName(TEXT("ResultSelectedDatabase"));
const TCHAR ActionIdleReassertComment[] = TEXT("E4.2.ActionIdle.Reassert");
const TCHAR ExitTailGateComment[] = TEXT("E4.2.Exit.TailGate");
const TCHAR ExitDatabaseGateComment[] = TEXT("E4.2.Exit.DatabaseGate");
const TCHAR ExitDatabaseReassertComment[] = TEXT("E4.2.Exit.Reassert");

struct FRouteSpec
{
	const TCHAR* CallbackName;
	const TCHAR* HandoffMacroName;
	const TCHAR* TailMacroName;
	const TCHAR* ForceFinishMacroName;
	const TCHAR* ActionIdleRouteFlagName;
	const TCHAR* ActionIdleActiveName;
	const TCHAR* ExitActiveName;
	const TCHAR* ExitTailEnabledName;
	const TCHAR* IdleDatabasePath;
	const TCHAR* ExitDatabaseName;
	const TCHAR* Label;
};

constexpr FRouteSpec Routes[] =
{
	{
		TEXT("OnShthMmUpdate"), TEXT("IsNewSheathedExitHandoff"),
		TEXT("ShouldOpenSheathedExitTail"), TEXT("ShouldForceFinishSheathedExit"),
		TEXT("bMMRouteSheathedActionIdle"), TEXT("MMShthActionIdleActive"),
		TEXT("MMShthExitActive"), TEXT("MMShthExitTailEnabled"),
		SheathedIdleDatabasePath, TEXT("MMShthExitDatabase"), TEXT("Sheathed")
	},
	{
		TEXT("OnUnShMmUpdate"), TEXT("IsNewUnsheathedExitHandoff"),
		TEXT("ShouldOpenUnsheathedExitTail"), TEXT("ShouldForceFinishUnsheathedExit"),
		TEXT("bMMRouteUnsheathedActionIdle"), TEXT("MMUnShActionIdleActive"),
		TEXT("MMUnShExitActive"), TEXT("MMUnShExitTailEnabled"),
		UnsheathedIdleDatabasePath, TEXT("MMUnShExitDatabase"), TEXT("Unsheathed")
	}
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

bool SetInterruptAndInvalidateContinuingPose(UK2Node_CallFunction& Call)
{
    UEdGraphPin* InterruptMode = Call.FindPin(TEXT("InterruptMode"), EGPD_Input);
    if (!InterruptMode)
    {
        return false;
    }
    Call.Modify();
    InterruptMode->DefaultValue = TEXT("InterruptOnDatabaseChangeAndInvalidateContinuingPose");
    return true;
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

UK2Node_MacroInstance* FindMacroInstance(UEdGraph& Graph, const FName MacroName)
{
	for (UEdGraphNode* Node : Graph.Nodes)
	{
		UK2Node_MacroInstance* Instance = Cast<UK2Node_MacroInstance>(Node);
		const UEdGraph* MacroGraph = Instance ? Instance->GetMacroGraph() : nullptr;
		if (MacroGraph && MacroGraph->GetFName() == MacroName)
		{
			return Instance;
		}
	}
	return nullptr;
}

UEdGraphPin* FindExecInput(UEdGraphNode& Node)
{
	return Node.FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
}

UEdGraphPin* FindThenOutput(UEdGraphNode& Node)
{
	return Node.FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
}

UEdGraphPin* FindBoolBranchOutput(UK2Node_IfThenElse& Branch, const bool bTrue)
{
	return Branch.FindPin(bTrue ? UEdGraphSchema_K2::PN_Then : UEdGraphSchema_K2::PN_Else,
		EGPD_Output);
}

UEdGraphNode* GetFirstExecTarget(UEdGraphPin* Output)
{
	if (!Output)
	{
		return nullptr;
	}
	for (UEdGraphPin* Target : Output->LinkedTo)
	{
		if (Target && Target->Direction == EGPD_Input && Target->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
		{
			return Target->GetOwningNode();
		}
	}
	return nullptr;
}

UEdGraphNode* GetFirstDataTarget(UEdGraphPin* Output)
{
    if (!Output)
    {
        return nullptr;
    }
    for (UEdGraphPin* Target : Output->LinkedTo)
    {
        if (Target && Target->Direction == EGPD_Input
            && Target->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
        {
            return Target->GetOwningNode();
        }
    }
    return nullptr;
}
UK2Node_IfThenElse* FindBranchFromMacro(UK2Node_MacroInstance& Instance)
{
	UEdGraphPin* Result = Instance.FindPin(TEXT("ReturnValue"), EGPD_Output);
	return Cast<UK2Node_IfThenElse>(GetFirstDataTarget(Result));
}

UK2Node_FunctionResult* FindReturnNode(UEdGraph& Graph)
{
	for (UEdGraphNode* Node : Graph.Nodes)
	{
		if (UK2Node_FunctionResult* Result = Cast<UK2Node_FunctionResult>(Node))
		{
			return Result;
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

UK2Node_VariableSet* FindBooleanSetterWithDefault(UEdGraph& Graph, const FName VariableName,
    const bool bExpectedValue)
{
    const FString ExpectedDefault = bExpectedValue ? TEXT("true") : TEXT("false");
    for (UEdGraphNode* Node : Graph.Nodes)
    {
        UK2Node_VariableSet* Setter = Cast<UK2Node_VariableSet>(Node);
        if (!Setter || Setter->VariableReference.GetMemberName() != VariableName)
        {
            continue;
        }

        const UEdGraphPin* ValuePin = Setter->FindPin(VariableName, EGPD_Input);
        if (ValuePin && ValuePin->PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean
            && ValuePin->DefaultValue.Equals(ExpectedDefault, ESearchCase::IgnoreCase))
        {
            return Setter;
        }
    }
    return nullptr;
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

UK2Node_VariableSet* CreateSelfSetter(UEdGraph& Graph, const FName Name, const bool bValue,
	const int32 X, const int32 Y)
{
	UK2Node_VariableSet* Setter = NewObject<UK2Node_VariableSet>(&Graph);
	Graph.AddNode(Setter, true, false);
	Setter->CreateNewGuid();
	Setter->VariableReference.SetSelfMember(Name);
	Setter->NodePosX = X;
	Setter->NodePosY = Y;
	Setter->AllocateDefaultPins();
	if (UEdGraphPin* ValuePin = Setter->FindPin(Name, EGPD_Input))
	{
		ValuePin->DefaultValue = bValue ? TEXT("true") : TEXT("false");
	}
	return Setter;
}

UK2Node_IfThenElse* CreateBranch(UEdGraph& Graph, const int32 X, const int32 Y)
{
	UK2Node_IfThenElse* Branch = NewObject<UK2Node_IfThenElse>(&Graph);
	Graph.AddNode(Branch, true, false);
	Branch->CreateNewGuid();
	Branch->NodePosX = X;
	Branch->NodePosY = Y;
	Branch->AllocateDefaultPins();
	return Branch;
}

UK2Node_CallFunction* CreateCallFromTemplate(UEdGraph& Graph,
	const UK2Node_CallFunction& Template, const int32 X, const int32 Y)
{
	UK2Node_CallFunction* Call = NewObject<UK2Node_CallFunction>(&Graph);
	Graph.AddNode(Call, true, false);
	Call->CreateNewGuid();
	Call->FunctionReference = Template.FunctionReference;
	Call->NodePosX = X;
	Call->NodePosY = Y;
	Call->AllocateDefaultPins();
	return Call;
}

UK2Node_CallFunction* CreateSetDatabaseToSearch(UEdGraph& Graph, const int32 X, const int32 Y)
{
	UK2Node_CallFunction* Call = NewObject<UK2Node_CallFunction>(&Graph);
	Graph.AddNode(Call, true, false);
	Call->CreateNewGuid();
	Call->FunctionReference.SetExternalMember(
		GET_FUNCTION_NAME_CHECKED(UMotionMatchingAnimNodeLibrary, SetDatabaseToSearch),
		UMotionMatchingAnimNodeLibrary::StaticClass());
	Call->NodePosX = X;
	Call->NodePosY = Y;
	Call->AllocateDefaultPins();
	return Call;
}


UK2Node_CallFunction* CreateObjectEquals(UEdGraph& Graph, const int32 X, const int32 Y)
{
	UK2Node_CallFunction* Call = NewObject<UK2Node_CallFunction>(&Graph);
	Graph.AddNode(Call, true, false);
	Call->CreateNewGuid();
	Call->FunctionReference.SetExternalMember(GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary,
		EqualEqual_ObjectObject), UKismetMathLibrary::StaticClass());
	Call->NodePosX = X;
	Call->NodePosY = Y;
	Call->AllocateDefaultPins();
	return Call;
}

UK2Node_CallFunction* CreateBooleanOr(UEdGraph& Graph, const int32 X, const int32 Y)
{
	UK2Node_CallFunction* Call = NewObject<UK2Node_CallFunction>(&Graph);
	Graph.AddNode(Call, true, false);
	Call->CreateNewGuid();
	Call->FunctionReference.SetExternalMember(GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary,
		BooleanOR), UKismetMathLibrary::StaticClass());
	Call->NodePosX = X;
	Call->NodePosY = Y;
	Call->AllocateDefaultPins();
	return Call;
}
UK2Node_VariableGet* CreateGetterFromTemplate(UEdGraph& Graph, const UK2Node_VariableGet& Template,
	const int32 X, const int32 Y)
{
	UK2Node_VariableGet* Getter = NewObject<UK2Node_VariableGet>(&Graph);
	Graph.AddNode(Getter, true, false);
	Getter->CreateNewGuid();
	Getter->VariableReference = Template.VariableReference;
	Getter->NodePosX = X;
	Getter->NodePosY = Y;
	Getter->AllocateDefaultPins();
	return Getter;
}
UK2Node_IfThenElse* FindBranchWithConditionVariable(UEdGraph& Graph, const FName VariableName)
{
	for (UEdGraphNode* Node : Graph.Nodes)
	{
		UK2Node_IfThenElse* Branch = Cast<UK2Node_IfThenElse>(Node);
		UEdGraphPin* Condition = Branch ? Branch->FindPin(UEdGraphSchema_K2::PN_Condition, EGPD_Input) : nullptr;
		if (!Condition)
		{
			continue;
		}
		for (UEdGraphPin* LinkedPin : Condition->LinkedTo)
		{
			UK2Node_VariableGet* Getter = LinkedPin ? Cast<UK2Node_VariableGet>(LinkedPin->GetOwningNode()) : nullptr;
			if (Getter && Getter->VariableReference.GetMemberName() == VariableName)
			{
				return Branch;
			}
		}
	}
	return nullptr;
}

bool HasNodeComment(const UEdGraph& Graph, const TCHAR* Comment)
{
	return Graph.Nodes.ContainsByPredicate([Comment](const UEdGraphNode* Node)
	{
		return Node && Node->NodeComment == Comment;
	});
}

bool EnsureActionIdleMember(UAnimBlueprint& Blueprint, const FName Name,
	const FEdGraphPinType& BoolPinType)
{
	return HasMemberVariable(Blueprint, Name)
		|| FBlueprintEditorUtils::AddMemberVariable(&Blueprint, Name, BoolPinType);
}

void AddOrUpdateComment(UEdGraph& Graph, const FString& Text, const int32 X, const int32 Y,
	const int32 Width, const int32 Height, const FLinearColor& Color)
{
	for (UEdGraphNode* Node : Graph.Nodes)
	{
		if (UEdGraphNode_Comment* Comment = Cast<UEdGraphNode_Comment>(Node);
			Comment && Comment->NodeComment == Text)
		{
			Comment->Modify();
			Comment->NodePosX = X;
			Comment->NodePosY = Y;
			Comment->NodeWidth = Width;
			Comment->NodeHeight = Height;
			Comment->CommentColor = Color;
			Comment->bCommentBubbleVisible = false;
			return;
		}
	}

	UEdGraphNode_Comment* Comment = NewObject<UEdGraphNode_Comment>(&Graph);
	Graph.AddNode(Comment, true, false);
	Comment->CreateNewGuid();
	Comment->NodeComment = Text;
	Comment->NodePosX = X;
	Comment->NodePosY = Y;
	Comment->NodeWidth = Width;
	Comment->NodeHeight = Height;
	Comment->CommentColor = Color;
	Comment->bCommentBubbleVisible = false;
}

UK2Node_CallFunction* FindSetDatabaseWithDefault(UEdGraph& Graph, UObject* Database)
{
	for (UEdGraphNode* Node : Graph.Nodes)
	{
		UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node);
		if (!Call || Call->FunctionReference.GetMemberName() != SetDatabaseToSearchName)
		{
			continue;
		}
		const UEdGraphPin* DatabasePin = Call->FindPin(TEXT("Database"), EGPD_Input);
		if (DatabasePin && DatabasePin->DefaultObject == Database)
		{
			return Call;
		}
	}
	return nullptr;
}

bool InstallPersistentCandidateRouting(UAnimBlueprint& Blueprint, const FRouteSpec& Route)
{
	UEdGraph* Graph = FindFunctionGraph(Blueprint, Route.CallbackName);
	UPoseSearchDatabase* IdleDatabase = LoadObject<UPoseSearchDatabase>(nullptr, Route.IdleDatabasePath);
	if (!Graph || !IdleDatabase)
	{
		return false;
	}
	if (HasNodeComment(*Graph, ActionIdleReassertComment) && HasNodeComment(*Graph, ExitTailGateComment)
		&& HasNodeComment(*Graph, ExitDatabaseGateComment) && HasNodeComment(*Graph, ExitDatabaseReassertComment))
	{
		return true;
	}

	UK2Node_IfThenElse* ActionIdleInputBranch = FindBranchWithConditionVariable(*Graph, HasLocomotionInputName);
	UK2Node_IfThenElse* ExitActiveBranch = FindBranchWithConditionVariable(*Graph, Route.ExitActiveName);
	UK2Node_VariableGet* MotionNodeGetter = FindVariableNode<UK2Node_VariableGet>(*Graph, MotionMatchingNodeLocalName);
	UK2Node_VariableGet* ExistingResultDatabaseGetter = FindVariableNode<UK2Node_VariableGet>(*Graph, ResultSelectedDatabaseName);
	UK2Node_VariableGet* ExistingExitDatabaseGetter = FindVariableNode<UK2Node_VariableGet>(*Graph, FName(Route.ExitDatabaseName));
	UK2Node_FunctionResult* ReturnNode = FindReturnNode(*Graph);
	if (!ActionIdleInputBranch || !ExitActiveBranch || !MotionNodeGetter || !ExistingResultDatabaseGetter || !ExistingExitDatabaseGetter || !ReturnNode)
	{
		return false;
	}
	UEdGraphPin* MotionNodeOutput = MotionNodeGetter->FindPin(MotionMatchingNodeLocalName, EGPD_Output);
	UEdGraphPin* ActionIdleFalse = FindBoolBranchOutput(*ActionIdleInputBranch, false);
	UEdGraphNode* ActionIdleFalseTarget = GetFirstExecTarget(ActionIdleFalse);
	UEdGraphPin* ExitActiveTrue = FindBoolBranchOutput(*ExitActiveBranch, true);
	UEdGraphNode* ExistingExitFlow = GetFirstExecTarget(ExitActiveTrue);
	if (!MotionNodeOutput || !ActionIdleFalse || !ActionIdleFalseTarget || !ExitActiveTrue || !ExistingExitFlow)
	{
		return false;
	}
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	if (!Schema)
	{
		return false;
	}
	Graph->Modify();
	Blueprint.Modify();

	// No-input ActionIdle owns its candidate context until fresh input resets it.
	UK2Node_CallFunction* ReassertActionIdle = CreateSetDatabaseToSearch(*Graph,
		ActionIdleInputBranch->NodePosX + 304, ActionIdleInputBranch->NodePosY + 112);
	ReassertActionIdle->NodeComment = ActionIdleReassertComment;
	UEdGraphPin* ReassertActionIdleDatabase = ReassertActionIdle->FindPin(TEXT("Database"), EGPD_Input);
	UEdGraphPin* ReassertActionIdleNode = ReassertActionIdle->FindPin(TEXT("MotionMatchingNode"), EGPD_Input);
	if (!ReassertActionIdleDatabase || !ReassertActionIdleNode || !SetInterruptAndInvalidateContinuingPose(*ReassertActionIdle))
	{
		return false;
	}
	ReassertActionIdleDatabase->DefaultObject = IdleDatabase;
	UEdGraphPin* ActionIdleTargetExec = FindExecInput(*ActionIdleFalseTarget);
	ActionIdleFalse->BreakLinkTo(ActionIdleTargetExec);
	if (!LinkPins(*Schema, ActionIdleFalse, FindExecInput(*ReassertActionIdle))
		|| !LinkPins(*Schema, MotionNodeOutput, ReassertActionIdleNode)
		|| !LinkPins(*Schema, FindThenOutput(*ReassertActionIdle), ActionIdleTargetExec))
	{
		return false;
	}

	// The old Move/Idle result is not allowed to open an Exit tail. The only
	// admissible paths are: the Exit PSD has won a result, or a previously
	// opened tail is now handing that result to the normal Move PSD.
	UK2Node_IfThenElse* ExitCanAdvanceGate = CreateBranch(*Graph,
		ExitActiveBranch->NodePosX + 304, ExitActiveBranch->NodePosY - 72);
	ExitCanAdvanceGate->NodeComment = ExitTailGateComment;
	UK2Node_VariableGet* TailEnabledGetter = CreateSelfGetter(*Graph, Route.ExitTailEnabledName,
		ExitCanAdvanceGate->NodePosX - 224, ExitCanAdvanceGate->NodePosY + 64);
	UK2Node_CallFunction* ExitDatabaseEquals = CreateObjectEquals(*Graph,
		ExitCanAdvanceGate->NodePosX - 224, ExitCanAdvanceGate->NodePosY + 120);
	ExitDatabaseEquals->NodeComment = ExitDatabaseGateComment;
	UK2Node_VariableGet* ResultDatabaseGetter = CreateGetterFromTemplate(*Graph, *ExistingResultDatabaseGetter,
		ExitDatabaseEquals->NodePosX - 224, ExitDatabaseEquals->NodePosY + 80);
	UK2Node_VariableGet* ExitDatabaseGetter = CreateGetterFromTemplate(*Graph, *ExistingExitDatabaseGetter,
		ExitDatabaseEquals->NodePosX - 224, ExitDatabaseEquals->NodePosY + 136);
	UK2Node_CallFunction* CanAdvanceExit = CreateBooleanOr(*Graph,
		ExitCanAdvanceGate->NodePosX - 16, ExitCanAdvanceGate->NodePosY + 112);
	UK2Node_CallFunction* ReassertExitDatabase = CreateSetDatabaseToSearch(*Graph,
		ExitCanAdvanceGate->NodePosX + 320, ExitCanAdvanceGate->NodePosY + 80);
	ReassertExitDatabase->NodeComment = ExitDatabaseReassertComment;
	UEdGraphPin* EqualsA = ExitDatabaseEquals->FindPin(TEXT("A"), EGPD_Input);
	UEdGraphPin* EqualsB = ExitDatabaseEquals->FindPin(TEXT("B"), EGPD_Input);
	UEdGraphPin* EqualsResult = ExitDatabaseEquals->FindPin(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output);
	UEdGraphPin* CanAdvanceA = CanAdvanceExit->FindPin(TEXT("A"), EGPD_Input);
	UEdGraphPin* CanAdvanceB = CanAdvanceExit->FindPin(TEXT("B"), EGPD_Input);
	UEdGraphPin* CanAdvanceResult = CanAdvanceExit->FindPin(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output);
	UEdGraphPin* ReassertExitDatabasePin = ReassertExitDatabase->FindPin(TEXT("Database"), EGPD_Input);
	UEdGraphPin* ReassertExitNode = ReassertExitDatabase->FindPin(TEXT("MotionMatchingNode"), EGPD_Input);
	if (!EqualsA || !EqualsB || !EqualsResult || !CanAdvanceA || !CanAdvanceB || !CanAdvanceResult
		|| !ReassertExitDatabasePin || !ReassertExitNode || !SetInterruptAndInvalidateContinuingPose(*ReassertExitDatabase))
	{
		return false;
	}
	UEdGraphPin* ExitFlowExec = FindExecInput(*ExistingExitFlow);
	ExitActiveTrue->BreakLinkTo(ExitFlowExec);
	if (!LinkPins(*Schema, ExitActiveTrue, FindExecInput(*ExitCanAdvanceGate))
		|| !LinkPins(*Schema, TailEnabledGetter->FindPin(Route.ExitTailEnabledName, EGPD_Output), CanAdvanceA)
		|| !LinkPins(*Schema, ResultDatabaseGetter->FindPin(ResultSelectedDatabaseName, EGPD_Output), EqualsA)
		|| !LinkPins(*Schema, ExitDatabaseGetter->FindPin(Route.ExitDatabaseName, EGPD_Output), EqualsB)
		|| !LinkPins(*Schema, EqualsResult, CanAdvanceB)
		|| !LinkPins(*Schema, CanAdvanceResult, ExitCanAdvanceGate->FindPin(UEdGraphSchema_K2::PN_Condition, EGPD_Input))
		|| !LinkPins(*Schema, FindBoolBranchOutput(*ExitCanAdvanceGate, true), ExitFlowExec)
		|| !LinkPins(*Schema, FindBoolBranchOutput(*ExitCanAdvanceGate, false), FindExecInput(*ReassertExitDatabase))
		|| !LinkPins(*Schema, MotionNodeOutput, ReassertExitNode)
		|| !LinkPins(*Schema, ExitDatabaseGetter->FindPin(Route.ExitDatabaseName, EGPD_Output), ReassertExitDatabasePin)
		|| !LinkPins(*Schema, FindThenOutput(*ReassertExitDatabase), FindExecInput(*ReturnNode)))
	{
		return false;
	}
	AddOrUpdateComment(*Graph, FString::Printf(TEXT("E4.2 %s: ActionIdle and Exit databases are re-asserted until the intended candidate context owns the result."), Route.Label),
		ActionIdleInputBranch->NodePosX - 128, ActionIdleInputBranch->NodePosY - 48, 2304, 720,
		FLinearColor(0.20f, 0.18f, 0.38f, 0.35f));
	return true;
}
bool RepairInstalledRoute(UAnimBlueprint& Blueprint, const FRouteSpec& Route)
{
    UEdGraph* Graph = FindFunctionGraph(Blueprint, Route.CallbackName);
    UPoseSearchDatabase* IdleDatabase = LoadObject<UPoseSearchDatabase>(nullptr, Route.IdleDatabasePath);
    if (!Graph || !IdleDatabase)
    {
        return false;
    }

    bool bFoundActionIdleState = false;
    int32 IdleDatabaseCalls = 0;
    bool bFoundInputReset = false;
    Graph->Modify();
    Blueprint.Modify();
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        UK2Node_VariableSet* Setter = Cast<UK2Node_VariableSet>(Node);
        if (Setter && Setter->VariableReference.GetMemberName() == Route.ActionIdleActiveName)
        {
            bFoundActionIdleState = true;
        }

        UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node);
        if (!Call)
        {
            continue;
        }

        if (Call->FunctionReference.GetMemberName() == SetDatabaseToSearchName)
        {
            const UEdGraphPin* DatabasePin = Call->FindPin(TEXT("Database"), EGPD_Input);
            if (DatabasePin && DatabasePin->DefaultObject == IdleDatabase)
            {
                ++IdleDatabaseCalls;
                if (!SetInterruptAndInvalidateContinuingPose(*Call))
                {
                    return false;
                }
            }
        }
        else if (Call->FunctionReference.GetMemberName() == ResetDatabasesToSearchName)
        {
            UEdGraphNode* Next = GetFirstExecTarget(FindThenOutput(*Call));
            UK2Node_VariableSet* NextSetter = Cast<UK2Node_VariableSet>(Next);
            const UEdGraphPin* ValuePin = NextSetter
                ? NextSetter->FindPin(Route.ActionIdleActiveName, EGPD_Input) : nullptr;
            if (NextSetter && NextSetter->VariableReference.GetMemberName() == Route.ActionIdleActiveName
                && ValuePin && ValuePin->DefaultValue.Equals(TEXT("false"), ESearchCase::IgnoreCase))
            {
                bFoundInputReset = SetInterruptAndInvalidateContinuingPose(*Call);
            }
        }
    }
    if (!bFoundActionIdleState || IdleDatabaseCalls != 3 || !bFoundInputReset
        || !InstallPersistentCandidateRouting(Blueprint, Route))
    {
        UE_LOG(LogTemp, Error, TEXT("[E4ActionIdleRouting] %s does not match the installed ActionIdle routing contract."),
            Route.CallbackName);
        return false;
    }
    return true;
}
bool InstallRoute(UAnimBlueprint& Blueprint, const FRouteSpec& Route,
	const UEdGraphSchema_K2& Schema)
{
	UEdGraph* Graph = FindFunctionGraph(Blueprint, Route.CallbackName);
	if (!Graph)
	{
		UE_LOG(LogTemp, Error, TEXT("[E4ActionIdleRouting] %s is missing."), Route.CallbackName);
		return false;
	}
	if (FindVariableNode<UK2Node_VariableSet>(*Graph, Route.ActionIdleActiveName))
	{
		return RepairInstalledRoute(Blueprint, Route);
	}

	UK2Node_MacroInstance* HandoffMacro = FindMacroInstance(*Graph, Route.HandoffMacroName);
	UK2Node_MacroInstance* TailMacro = FindMacroInstance(*Graph, Route.TailMacroName);
	UK2Node_MacroInstance* FinishMacro = FindMacroInstance(*Graph, Route.ForceFinishMacroName);
	UK2Node_IfThenElse* HandoffBranch = HandoffMacro ? FindBranchFromMacro(*HandoffMacro) : nullptr;
	UK2Node_IfThenElse* TailBranch = TailMacro ? FindBranchFromMacro(*TailMacro) : nullptr;
	UK2Node_IfThenElse* FinishBranch = FinishMacro ? FindBranchFromMacro(*FinishMacro) : nullptr;
	UK2Node_CallFunction* SetExitDatabase = FindCall(*Graph, SetDatabaseToSearchName);
	UK2Node_CallFunction* SetExitAndMoveDatabases = FindCall(*Graph, SetDatabasesToSearchName);
	UK2Node_CallFunction* QueueHandoff = FindCall(*Graph, QueueHandoffConsumptionName);
	UK2Node_CallFunction* ResetDatabases = FindCall(*Graph, ResetDatabasesToSearchName);
	UK2Node_VariableSet* ExitActiveTrue = FindBooleanSetterWithDefault(*Graph,
		Route.ExitActiveName, true);
	UK2Node_VariableSet* ExitTailEnabledTrue = FindBooleanSetterWithDefault(*Graph,
		Route.ExitTailEnabledName, true);
	UK2Node_VariableGet* MotionNodeGetter = FindVariableNode<UK2Node_VariableGet>(*Graph,
		MotionMatchingNodeLocalName);
	UK2Node_FunctionResult* ReturnNode = FindReturnNode(*Graph);
	if (!HandoffBranch || !TailBranch || !FinishBranch || !SetExitDatabase || !SetExitAndMoveDatabases
		|| !QueueHandoff || !ResetDatabases || !ExitActiveTrue || !ExitTailEnabledTrue
		|| !MotionNodeGetter || !ReturnNode)
	{
		UE_LOG(LogTemp, Error, TEXT("[E4ActionIdleRouting] %s does not match the audited E4.2 callback contract."),
			Route.CallbackName);
		return false;
	}

	UEdGraphPin* ExitActiveTrueValue = ExitActiveTrue->FindPin(Route.ExitActiveName, EGPD_Input);
	if (!ExitActiveTrueValue || ExitActiveTrueValue->PinType.PinCategory != UEdGraphSchema_K2::PC_Boolean
		|| !EnsureActionIdleMember(Blueprint, Route.ActionIdleActiveName, ExitActiveTrueValue->PinType))
	{
		return false;
	}

	UPoseSearchDatabase* IdleDatabase = LoadObject<UPoseSearchDatabase>(nullptr, Route.IdleDatabasePath);
	UEdGraphNode* ExistingExitBranch = GetFirstExecTarget(FindBoolBranchOutput(*HandoffBranch, false));
	UEdGraphNode* ExistingTailOpen = GetFirstExecTarget(FindBoolBranchOutput(*TailBranch, true));
	UEdGraphNode* ExistingTailNext = GetFirstExecTarget(FindBoolBranchOutput(*TailBranch, false));
	UEdGraphNode* ExistingFinishReset = GetFirstExecTarget(FindBoolBranchOutput(*FinishBranch, true));
	if (!IdleDatabase || !ExistingExitBranch || !ExistingTailOpen || !ExistingTailNext || !ExistingFinishReset)
	{
		return false;
	}

	Graph->Modify();
	Blueprint.Modify();
	HandoffBranch->Modify();
	TailBranch->Modify();
	FinishBranch->Modify();

	// A new non-handoff ActionIdle event installs an Idle-only PSD and is then
	// acknowledged on the next AnimInstance update. The Blueprint-owned state
	// keeps that candidate context until fresh locomotion input arrives.
	UK2Node_IfThenElse* ActionIdleEventBranch = CreateBranch(*Graph, 1840, -592);
	UK2Node_VariableGet* ActionIdleRouteGetter = CreateSelfGetter(*Graph,
		Route.ActionIdleRouteFlagName, 1584, -544);
	UK2Node_CallFunction* SetActionIdleDatabase = CreateCallFromTemplate(*Graph,
		*SetExitDatabase, 2112, -704);
	UK2Node_CallFunction* QueueActionIdle = CreateCallFromTemplate(*Graph,
		*QueueHandoff, 2464, -704);
	QueueActionIdle->FunctionReference.SetSelfMember(QueueActionIdleConsumptionName);
	QueueActionIdle->ReconstructNode();
	UK2Node_VariableSet* SetActionIdleActive = CreateSelfSetter(*Graph,
		Route.ActionIdleActiveName, true, 2816, -704);
	UK2Node_IfThenElse* ActionIdleActiveBranch = CreateBranch(*Graph, 2112, -432);
	UK2Node_VariableGet* ActionIdleActiveGetter = CreateSelfGetter(*Graph,
		Route.ActionIdleActiveName, 1856, -384);
	UK2Node_IfThenElse* ActionIdleInputBranch = CreateBranch(*Graph, 2464, -400);
	UK2Node_VariableGet* HasInputGetter = CreateSelfGetter(*Graph, HasLocomotionInputName,
		2224, -352);
	UK2Node_CallFunction* ResetAfterActionIdle = CreateCallFromTemplate(*Graph,
		*ResetDatabases, 2816, -480);
	UK2Node_VariableSet* ClearActionIdleForInput = CreateSelfSetter(*Graph,
		Route.ActionIdleActiveName, false, 3168, -480);
	UK2Node_VariableSet* ClearActionIdleForHandoff = CreateSelfSetter(*Graph,
		Route.ActionIdleActiveName, false, ExitActiveTrue->NodePosX - 256, ExitActiveTrue->NodePosY - 112);

	UEdGraphPin* SetActionIdleDatabaseValue = SetActionIdleDatabase->FindPin(TEXT("Database"), EGPD_Input);
	UEdGraphPin* SetActionIdleDatabaseNode = SetActionIdleDatabase->FindPin(TEXT("MotionMatchingNode"), EGPD_Input);
	UEdGraphPin* QueueContextSerial = QueueActionIdle->FindPin(TEXT("ContextSerial"), EGPD_Input);
	UEdGraphPin* ContextSerialGetter = CreateSelfGetter(*Graph, ActionIdleContextSerialName, 2416, -592)
		->FindPin(ActionIdleContextSerialName, EGPD_Output);
	UEdGraphPin* MotionNodeOutput = MotionNodeGetter->FindPin(MotionMatchingNodeLocalName, EGPD_Output);
	UEdGraphPin* ResetAfterActionIdleNode = ResetAfterActionIdle->FindPin(TEXT("MotionMatchingNode"), EGPD_Input);
	if (!SetActionIdleDatabaseValue || !SetActionIdleDatabaseNode || !QueueContextSerial || !ContextSerialGetter
		|| !MotionNodeOutput || !ResetAfterActionIdleNode)
	{
		return false;
	}
	SetActionIdleDatabaseValue->DefaultObject = IdleDatabase;

	if (!SetInterruptAndInvalidateContinuingPose(*SetActionIdleDatabase)
		|| !SetInterruptAndInvalidateContinuingPose(*ResetAfterActionIdle))
	{
		return false;
	}

	UEdGraphPin* HandoffElse = FindBoolBranchOutput(*HandoffBranch, false);
	UEdGraphPin* ExistingExitExecute = FindExecInput(*ExistingExitBranch);
	HandoffElse->BreakLinkTo(ExistingExitExecute);
	if (!LinkPins(Schema, HandoffElse, FindExecInput(*ActionIdleEventBranch))
		|| !LinkPins(Schema, ActionIdleRouteGetter->FindPin(Route.ActionIdleRouteFlagName, EGPD_Output),
			ActionIdleEventBranch->FindPin(UEdGraphSchema_K2::PN_Condition, EGPD_Input))
		|| !LinkPins(Schema, FindBoolBranchOutput(*ActionIdleEventBranch, true), FindExecInput(*SetActionIdleDatabase))
		|| !LinkPins(Schema, MotionNodeOutput, SetActionIdleDatabaseNode)
		|| !LinkPins(Schema, FindThenOutput(*SetActionIdleDatabase), FindExecInput(*QueueActionIdle))
		|| !LinkPins(Schema, ContextSerialGetter, QueueContextSerial)
		|| !LinkPins(Schema, FindThenOutput(*QueueActionIdle), FindExecInput(*SetActionIdleActive))
		|| !LinkPins(Schema, FindThenOutput(*SetActionIdleActive), FindExecInput(*ReturnNode))
		|| !LinkPins(Schema, FindBoolBranchOutput(*ActionIdleEventBranch, false), FindExecInput(*ActionIdleActiveBranch))
		|| !LinkPins(Schema, ActionIdleActiveGetter->FindPin(Route.ActionIdleActiveName, EGPD_Output),
			ActionIdleActiveBranch->FindPin(UEdGraphSchema_K2::PN_Condition, EGPD_Input))
		|| !LinkPins(Schema, FindBoolBranchOutput(*ActionIdleActiveBranch, false), ExistingExitExecute)
		|| !LinkPins(Schema, FindBoolBranchOutput(*ActionIdleActiveBranch, true), FindExecInput(*ActionIdleInputBranch))
		|| !LinkPins(Schema, HasInputGetter->FindPin(HasLocomotionInputName, EGPD_Output),
			ActionIdleInputBranch->FindPin(UEdGraphSchema_K2::PN_Condition, EGPD_Input))
		|| !LinkPins(Schema, FindBoolBranchOutput(*ActionIdleInputBranch, true), FindExecInput(*ResetAfterActionIdle))
		|| !LinkPins(Schema, MotionNodeOutput, ResetAfterActionIdleNode)
		|| !LinkPins(Schema, FindThenOutput(*ResetAfterActionIdle), FindExecInput(*ClearActionIdleForInput))
		|| !LinkPins(Schema, FindThenOutput(*ClearActionIdleForInput), FindExecInput(*ReturnNode))
		|| !LinkPins(Schema, FindBoolBranchOutput(*ActionIdleInputBranch, false), FindExecInput(*ReturnNode)))
	{
		return false;
	}

	// A new Handoff always supersedes a previous Idle-only context. Insert the
	// clear immediately before the existing ExitActive=true setter.
	UEdGraphPin* ExitActiveExecute = FindExecInput(*ExitActiveTrue);
	UEdGraphPin* PreviousHandoffThen = ExitActiveExecute && ExitActiveExecute->LinkedTo.Num() == 1
		? ExitActiveExecute->LinkedTo[0] : nullptr;
	if (!PreviousHandoffThen || !PreviousHandoffThen->GetOwningNode()->IsA<UK2Node_VariableSet>())
	{
		return false;
	}
	PreviousHandoffThen->BreakLinkTo(ExitActiveExecute);
	if (!LinkPins(Schema, PreviousHandoffThen, FindExecInput(*ClearActionIdleForHandoff))
		|| !LinkPins(Schema, FindThenOutput(*ClearActionIdleForHandoff), ExitActiveExecute))
	{
		return false;
	}

	// The tail opens normal Move candidates only for a held locomotion request or
	// a genuine Stop request. With neither, mark the tail as consumed but keep the
	// isolated Exit PSD until the final no-input Idle route below.
	UK2Node_IfThenElse* TailMoveBranch = CreateBranch(*Graph, TailBranch->NodePosX + 256,
		TailBranch->NodePosY - 144);
	UK2Node_VariableGet* ExitWantsMoveGetter = CreateSelfGetter(*Graph, ExitWantsMoveName,
		TailMoveBranch->NodePosX - 224, TailMoveBranch->NodePosY + 80);
	UK2Node_VariableSet* MarkNoMoveTailEnabled = CreateSelfSetter(*Graph,
		Route.ExitTailEnabledName, true, TailMoveBranch->NodePosX + 288, TailMoveBranch->NodePosY - 96);
	UEdGraphPin* TailTrue = FindBoolBranchOutput(*TailBranch, true);
	UEdGraphPin* ExistingTailOpenExecute = FindExecInput(*ExistingTailOpen);
	UEdGraphPin* ExistingTailNextExecute = FindExecInput(*ExistingTailNext);
	TailTrue->BreakLinkTo(ExistingTailOpenExecute);
	if (!LinkPins(Schema, TailTrue, FindExecInput(*TailMoveBranch))
		|| !LinkPins(Schema, ExitWantsMoveGetter->FindPin(ExitWantsMoveName, EGPD_Output),
			TailMoveBranch->FindPin(UEdGraphSchema_K2::PN_Condition, EGPD_Input))
		|| !LinkPins(Schema, FindBoolBranchOutput(*TailMoveBranch, true), ExistingTailOpenExecute)
		|| !LinkPins(Schema, FindBoolBranchOutput(*TailMoveBranch, false), FindExecInput(*MarkNoMoveTailEnabled))
		|| !LinkPins(Schema, FindThenOutput(*MarkNoMoveTailEnabled), ExistingTailNextExecute))
	{
		return false;
	}

	// At the final Exit frame, a moving/releasing player restores the normal Move
	// PSD through the existing Reset call. No input instead enters the stance's
	// Idle-only PSD, so a Loop can no longer win by foot-pose cost alone.
	UK2Node_IfThenElse* FinishMoveBranch = CreateBranch(*Graph, FinishBranch->NodePosX + 256,
		FinishBranch->NodePosY - 144);
	UK2Node_VariableGet* FinishExitWantsMove = CreateSelfGetter(*Graph, ExitWantsMoveName,
		FinishMoveBranch->NodePosX - 224, FinishMoveBranch->NodePosY + 80);
	UK2Node_CallFunction* SetIdleAtFinish = CreateCallFromTemplate(*Graph, *SetExitDatabase,
		FinishMoveBranch->NodePosX + 352, FinishMoveBranch->NodePosY + 96);
	UK2Node_VariableSet* ClearExitActiveAfterIdle = CreateSelfSetter(*Graph,
		Route.ExitActiveName, false, SetIdleAtFinish->NodePosX + 336, SetIdleAtFinish->NodePosY);
	UK2Node_VariableSet* ClearExitTailAfterIdle = CreateSelfSetter(*Graph,
		Route.ExitTailEnabledName, false, ClearExitActiveAfterIdle->NodePosX + 256,
		ClearExitActiveAfterIdle->NodePosY);
	UEdGraphPin* FinishTrue = FindBoolBranchOutput(*FinishBranch, true);
	UEdGraphPin* ExistingFinishResetExecute = FindExecInput(*ExistingFinishReset);
	UEdGraphPin* SetIdleAtFinishValue = SetIdleAtFinish->FindPin(TEXT("Database"), EGPD_Input);
	UEdGraphPin* SetIdleAtFinishNode = SetIdleAtFinish->FindPin(TEXT("MotionMatchingNode"), EGPD_Input);
	if (!SetIdleAtFinishValue || !SetIdleAtFinishNode)
	{
		return false;
	}
	SetIdleAtFinishValue->DefaultObject = IdleDatabase;

	if (!SetInterruptAndInvalidateContinuingPose(*SetIdleAtFinish))
	{
		return false;
	}
	FinishTrue->BreakLinkTo(ExistingFinishResetExecute);
	if (!LinkPins(Schema, FinishTrue, FindExecInput(*FinishMoveBranch))
		|| !LinkPins(Schema, FinishExitWantsMove->FindPin(ExitWantsMoveName, EGPD_Output),
			FinishMoveBranch->FindPin(UEdGraphSchema_K2::PN_Condition, EGPD_Input))
		|| !LinkPins(Schema, FindBoolBranchOutput(*FinishMoveBranch, true), ExistingFinishResetExecute)
		|| !LinkPins(Schema, FindBoolBranchOutput(*FinishMoveBranch, false), FindExecInput(*SetIdleAtFinish))
		|| !LinkPins(Schema, MotionNodeOutput, SetIdleAtFinishNode)
		|| !LinkPins(Schema, FindThenOutput(*SetIdleAtFinish), FindExecInput(*ClearExitActiveAfterIdle))
		|| !LinkPins(Schema, FindThenOutput(*ClearExitActiveAfterIdle), FindExecInput(*ClearExitTailAfterIdle))
		|| !LinkPins(Schema, FindThenOutput(*ClearExitTailAfterIdle), FindExecInput(*ReturnNode)))
	{
		return false;
	}

	AddOrUpdateComment(*Graph,
		FString::Printf(TEXT("E4.2 %s: non-Handoff no-input exit stays in ActionIdle PSD; new input restores normal Move PSD."), Route.Label),
		1488, -816, 2200, 336, FLinearColor(0.12f, 0.24f, 0.38f, 0.35f));
if (!InstallPersistentCandidateRouting(Blueprint, Route))
{
	return false;
}

	AddOrUpdateComment(*Graph,
		FString::Printf(TEXT("E4.2 %s: Exit tail opens Move candidates only for held input or one legal Stop; no input ends in ActionIdle PSD."), Route.Label),
		TailBranch->NodePosX - 160, TailBranch->NodePosY - 224, 2000, 176,
		FLinearColor(0.16f, 0.30f, 0.16f, 0.35f));
	return true;
}

bool ApplyRouting(UAnimBlueprint& Blueprint)
{
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	if (!Schema)
	{
		return false;
	}

	for (const FRouteSpec& Route : Routes)
	{
		if (!InstallRoute(Blueprint, Route, *Schema))
		{
			return false;
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(&Blueprint);
	FKismetEditorUtilities::CompileBlueprint(&Blueprint);
	if (Blueprint.Status == BS_Error)
	{
		UE_LOG(LogTemp, Error, TEXT("[E4ActionIdleRouting] AnimBlueprint compilation failed."));
		return false;
	}
	return SaveAsset(Blueprint);
}
}

#endif // WITH_EDITOR

UMHGZE4ActionIdleRoutingCommandlet::UMHGZE4ActionIdleRoutingCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMHGZE4ActionIdleRoutingCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	using namespace UE::MHGZ::E4ActionIdleRouting;
	(void)Params;
	UAnimBlueprint* AnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, AnimBlueprintPath);
	if (!AnimBlueprint || !ApplyRouting(*AnimBlueprint))
	{
		UE_LOG(LogTemp, Error, TEXT("[E4ActionIdleRouting] Failed to update %s."), AnimBlueprintPath);
		return 1;
	}
	UE_LOG(LogTemp, Display, TEXT("[E4ActionIdleRouting] Updated and saved %s."), AnimBlueprintPath);
	return 0;
#else
	return 1;
#endif
}
