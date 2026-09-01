// Copyright MHGZ Project. All Rights Reserved.

#include "Editor/MHGZE4SheathedExitGraphFixupCommandlet.h"

#if WITH_EDITOR

#include "Animation/AnimBlueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"
#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "EdGraphSchema_K2.h"

namespace UE::MHGZ::E4SheathedExitGraphFixup
{
constexpr TCHAR AnimBlueprintPath[] =
	TEXT("/Game/Blueprints/Characters/Demo/Animation/ABP_MH_Character.ABP_MH_Character");
const FName CallbackGraphName(TEXT("OnShthMmUpdate"));
const FName ExitDatabaseVariableName(TEXT("MMShthExitDatabase"));
const FName ExitDurationVariableName(TEXT("MMShthExitDuration"));
const FName SetDatabaseToSearchFunctionName(TEXT("SetDatabaseToSearch"));

struct FRouteDuration
{
	const TCHAR* DatabasePath;
	float DurationSeconds;
};

constexpr FRouteDuration RouteDurations[] =
{
	{ TEXT("/Game/Blueprints/Characters/Demo/Animation/MotionMatching/PSD_MH_Shth_SheatheExit.PSD_MH_Shth_SheatheExit"), 0.3500f },
	{ TEXT("/Game/Blueprints/Characters/Demo/Animation/MotionMatching/PSD_MH_Shth_DodgeExit.PSD_MH_Shth_DodgeExit"), 0.2667f }
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

const FCommentSpec CommentSpecs[] =
{
	{ TEXT("1. 接收一次新的收刀 Handoff"), 1180, -144, 2560, 352, FLinearColor(0.08f, 0.24f, 0.36f, 0.35f) },
	{ TEXT("2. 已在 Exit 中：读取当前 MM 搜索结果"), 960, 176, 960, 432, FLinearColor(0.20f, 0.20f, 0.12f, 0.35f) },
	{ TEXT("3. Exit 尾段：开放普通移动候选库"), 1920, 176, 1360, 336, FLinearColor(0.12f, 0.30f, 0.18f, 0.35f) },
	{ TEXT("4. 普通移动自然接手：恢复默认候选库"), 1680, 528, 1840, 288, FLinearColor(0.20f, 0.28f, 0.12f, 0.35f) },
	{ TEXT("5. 末帧兜底：退出 Exit 候选库"), 1680, 736, 2000, 480, FLinearColor(0.34f, 0.16f, 0.08f, 0.35f) }
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

UEdGraph* FindFunctionGraph(UBlueprint& Blueprint, const FName GraphName)
{
	for (UEdGraph* Graph : Blueprint.FunctionGraphs)
	{
		if (Graph && Graph->GetFName() == GraphName)
		{
			return Graph;
		}
	}
	return nullptr;
}

UK2Node_CallFunction* FindSetDatabaseToSearch(UEdGraph& Graph)
{
	for (UEdGraphNode* Node : Graph.Nodes)
	{
		if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node);
			Call && Call->FunctionReference.GetMemberName() == SetDatabaseToSearchFunctionName)
		{
			return Call;
		}
	}
	return nullptr;
}

UK2Node_VariableSet* FindExitDatabaseSetter(UEdGraph& Graph, const TCHAR* DatabasePath)
{
	for (UEdGraphNode* Node : Graph.Nodes)
	{
		UK2Node_VariableSet* Setter = Cast<UK2Node_VariableSet>(Node);
		if (!Setter || Setter->VariableReference.GetMemberName() != ExitDatabaseVariableName)
		{
			continue;
		}

		const UEdGraphPin* ValuePin = Setter->FindPin(ExitDatabaseVariableName, EGPD_Input);
		if (ValuePin && ValuePin->DefaultObject && ValuePin->DefaultObject->GetPathName() == DatabasePath)
		{
			return Setter;
		}
	}
	return nullptr;
}

UK2Node_VariableSet* FindExistingDurationSetter(const UK2Node_VariableSet& DatabaseSetter,
	const float DurationSeconds)
{
	const UEdGraphPin* ThenPin = DatabaseSetter.FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
	if (!ThenPin)
	{
		return nullptr;
	}

	const FString ExpectedValue = FString::SanitizeFloat(DurationSeconds);
	for (const UEdGraphPin* LinkedPin : ThenPin->LinkedTo)
	{
		const UK2Node_VariableSet* Candidate = LinkedPin
			? Cast<UK2Node_VariableSet>(LinkedPin->GetOwningNode())
			: nullptr;
		if (!Candidate || Candidate->VariableReference.GetMemberName() != ExitDurationVariableName)
		{
			continue;
		}

		const UEdGraphPin* ValuePin = Candidate->FindPin(ExitDurationVariableName, EGPD_Input);
		if (ValuePin && ValuePin->DefaultValue == ExpectedValue)
		{
			return const_cast<UK2Node_VariableSet*>(Candidate);
		}
	}
	return nullptr;
}

bool LinkPins(const UEdGraphSchema_K2& Schema, UEdGraphPin* From, UEdGraphPin* To)
{
	return From && To && (From->LinkedTo.Contains(To) || Schema.TryCreateConnection(From, To));
}

bool InsertDurationSetter(UEdGraph& Graph, const UEdGraphSchema_K2& Schema,
	UK2Node_VariableSet& DatabaseSetter, UK2Node_CallFunction& SetDatabaseToSearch,
	const float DurationSeconds)
{
	if (UK2Node_VariableSet* ExistingDurationSetter = FindExistingDurationSetter(DatabaseSetter, DurationSeconds))
	{
		if (!ExistingDurationSetter->NodeGuid.IsValid())
		{
			ExistingDurationSetter->Modify();
			ExistingDurationSetter->CreateNewGuid();
		}
		return true;
	}

	UEdGraphPin* DatabaseThenPin = DatabaseSetter.FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
	UEdGraphPin* SearchExecutePin = SetDatabaseToSearch.FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
	if (!DatabaseThenPin || !SearchExecutePin || !DatabaseThenPin->LinkedTo.Contains(SearchExecutePin))
	{
		return false;
	}

	UK2Node_VariableSet* DurationSetter = NewObject<UK2Node_VariableSet>(&Graph);
	Graph.AddNode(DurationSetter, true, false);
	DurationSetter->CreateNewGuid();
	DurationSetter->VariableReference.SetSelfMember(ExitDurationVariableName);
	DurationSetter->NodePosX = DatabaseSetter.NodePosX + 272;
	DurationSetter->NodePosY = DatabaseSetter.NodePosY;
	DurationSetter->AllocateDefaultPins();

	UEdGraphPin* DurationValuePin = DurationSetter->FindPin(ExitDurationVariableName, EGPD_Input);
	UEdGraphPin* DurationExecutePin = DurationSetter->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
	UEdGraphPin* DurationThenPin = DurationSetter->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
	if (!DurationValuePin || !DurationExecutePin || !DurationThenPin)
	{
		DurationSetter->DestroyNode();
		return false;
	}
	DurationValuePin->DefaultValue = FString::SanitizeFloat(DurationSeconds);

	DatabaseThenPin->BreakLinkTo(SearchExecutePin);
	if (!LinkPins(Schema, DatabaseThenPin, DurationExecutePin)
		|| !LinkPins(Schema, DurationThenPin, SearchExecutePin))
	{
		return false;
	}
	return true;
}

void AddOrUpdateComment(UEdGraph& Graph, const FCommentSpec& Spec)
{
	for (UEdGraphNode* Node : Graph.Nodes)
	{
		if (UEdGraphNode_Comment* Existing = Cast<UEdGraphNode_Comment>(Node);
			Existing && Existing->NodeComment == Spec.Text)
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

bool ApplyFixup(UAnimBlueprint& Blueprint)
{
	UEdGraph* Graph = FindFunctionGraph(Blueprint, CallbackGraphName);
	UK2Node_CallFunction* SetDatabaseToSearch = Graph ? FindSetDatabaseToSearch(*Graph) : nullptr;
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	if (!Graph || !SetDatabaseToSearch || !Schema)
	{
		return false;
	}

	Blueprint.Modify();
	Graph->Modify();
	SetDatabaseToSearch->Modify();
	bool bSucceeded = true;
	for (const FRouteDuration& Route : RouteDurations)
	{
		UK2Node_VariableSet* DatabaseSetter = FindExitDatabaseSetter(*Graph, Route.DatabasePath);
		if (!DatabaseSetter)
		{
			bSucceeded = false;
			continue;
		}
		DatabaseSetter->Modify();
		bSucceeded &= InsertDurationSetter(*Graph, *Schema, *DatabaseSetter, *SetDatabaseToSearch,
			Route.DurationSeconds);
	}

	for (const FCommentSpec& Spec : CommentSpecs)
	{
		AddOrUpdateComment(*Graph, Spec);
	}

	if (!bSucceeded)
	{
		return false;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(&Blueprint);
	FKismetEditorUtilities::CompileBlueprint(&Blueprint);
	return Blueprint.Status != BS_Error && SaveAsset(Blueprint);
}
}

#endif

UMHGZE4SheathedExitGraphFixupCommandlet::UMHGZE4SheathedExitGraphFixupCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMHGZE4SheathedExitGraphFixupCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	using namespace UE::MHGZ::E4SheathedExitGraphFixup;
	(void)Params;

	UAnimBlueprint* AnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, AnimBlueprintPath);
	if (!AnimBlueprint || !ApplyFixup(*AnimBlueprint))
	{
		UE_LOG(LogTemp, Error, TEXT("[E4SheathedExitGraphFixup] Failed to repair %s."), AnimBlueprintPath);
		return 1;
	}

	UE_LOG(LogTemp, Display, TEXT("[E4SheathedExitGraphFixup] Updated and saved %s."), AnimBlueprintPath);
	return 0;
#else
	return 1;
#endif
}
