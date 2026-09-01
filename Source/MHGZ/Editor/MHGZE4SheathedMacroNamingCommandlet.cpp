// Copyright MHGZ Project. All Rights Reserved.

#include "Editor/MHGZE4SheathedMacroNamingCommandlet.h"

#if WITH_EDITOR

#include "Animation/AnimBlueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/Blueprint.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace UE::MHGZ::E4SheathedMacroNaming
{
constexpr TCHAR AnimBlueprintPath[] =
	TEXT("/Game/Blueprints/Characters/Demo/Animation/ABP_MH_Character.ABP_MH_Character");

const FName HandoffActiveVariable(TEXT("bMMHandoffActive"));
const FName HandoffSerialVariable(TEXT("MMHandoffSerial"));
const FName LastHandoffSerialVariable(TEXT("MMShthLastHandoffSerial"));
const FName ExitTailEnabledVariable(TEXT("MMShthExitTailEnabled"));
const FName ExitDatabaseVariable(TEXT("MMShthExitDatabase"));
const FName ExitDurationVariable(TEXT("MMShthExitDuration"));
const FName SelectedDatabaseVariable(TEXT("ResultSelectedDatabase"));
const FName SelectedTimeVariable(TEXT("ResultSelectedTime"));
const FName TailStartTimeVariable(TEXT("TailStartTime"));

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

bool HasVariableGet(const UEdGraph& Graph, const FName VariableName)
{
	for (const UEdGraphNode* Node : Graph.Nodes)
	{
		const UK2Node_VariableGet* Getter = Cast<UK2Node_VariableGet>(Node);
		if (Getter && Getter->VariableReference.GetMemberName() == VariableName)
		{
			return true;
		}
	}
	return false;
}

FString GetVerifiedMacroName(const UEdGraph& Graph)
{
	const bool bChecksNewHandoff =
		HasVariableGet(Graph, HandoffActiveVariable)
		&& HasVariableGet(Graph, HandoffSerialVariable)
		&& HasVariableGet(Graph, LastHandoffSerialVariable);
	if (bChecksNewHandoff)
	{
		return TEXT("IsNewSheathedExitHandoff");
	}

	const bool bChecksEndFallback =
		HasVariableGet(Graph, ExitTailEnabledVariable)
		&& HasVariableGet(Graph, ExitDatabaseVariable)
		&& HasVariableGet(Graph, ExitDurationVariable)
		&& HasVariableGet(Graph, SelectedDatabaseVariable)
		&& HasVariableGet(Graph, SelectedTimeVariable);
	if (bChecksEndFallback)
	{
		return TEXT("ShouldForceFinishSheathedExit");
	}

	const bool bChecksOpenTail =
		HasVariableGet(Graph, ExitTailEnabledVariable)
		&& HasVariableGet(Graph, SelectedTimeVariable);
	if (bChecksOpenTail)
	{
		return TEXT("ShouldOpenSheathedExitTail");
	}

	const bool bChecksMoveTakeover =
		HasVariableGet(Graph, ExitTailEnabledVariable)
		&& HasVariableGet(Graph, SelectedDatabaseVariable);
	if (bChecksMoveTakeover)
	{
		return TEXT("HasSheathedExitHandedOffToMove");
	}

	return FString();
}

bool RenameVerifiedMacros(UAnimBlueprint& Blueprint)
{
	bool bChanged = false;
	int32 RecognizedMacroCount = 0;
	TSet<FString> AssignedNames;

	for (UEdGraph* MacroGraph : Blueprint.MacroGraphs)
	{
		if (!MacroGraph)
		{
			continue;
		}

		const FString VerifiedName = GetVerifiedMacroName(*MacroGraph);
		if (VerifiedName.IsEmpty())
		{
			continue;
		}

		++RecognizedMacroCount;
		if (AssignedNames.Contains(VerifiedName))
		{
			UE_LOG(LogTemp, Error,
				TEXT("[E4SheathedMacroNaming] More than one macro matches '%s'."), *VerifiedName);
			return false;
		}
		AssignedNames.Add(VerifiedName);

		if (MacroGraph->GetName() != VerifiedName)
		{
			MacroGraph->Modify();
			FBlueprintEditorUtils::RenameGraph(MacroGraph, VerifiedName);
			bChanged = true;
		}
	}

	constexpr int32 ExpectedMacroCount = 4;
	if (RecognizedMacroCount != ExpectedMacroCount || AssignedNames.Num() != ExpectedMacroCount)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[E4SheathedMacroNaming] Expected %d verified local macros, found %d."),
			ExpectedMacroCount, RecognizedMacroCount);
		return false;
	}

	if (!bChanged)
	{
		UE_LOG(LogTemp, Display, TEXT("[E4SheathedMacroNaming] Macro names are already up to date."));
		return true;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(&Blueprint);
	FKismetEditorUtilities::CompileBlueprint(&Blueprint);
	if (Blueprint.Status == BS_Error)
	{
		UE_LOG(LogTemp, Error, TEXT("[E4SheathedMacroNaming] Blueprint compile failed."));
		return false;
	}

	return SaveAsset(Blueprint);
}
}

#endif

UMHGZE4SheathedMacroNamingCommandlet::UMHGZE4SheathedMacroNamingCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMHGZE4SheathedMacroNamingCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	using namespace UE::MHGZ::E4SheathedMacroNaming;
	(void)Params;

	UAnimBlueprint* AnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, AnimBlueprintPath);
	if (!AnimBlueprint || !RenameVerifiedMacros(*AnimBlueprint))
	{
		UE_LOG(LogTemp, Error, TEXT("[E4SheathedMacroNaming] Failed to rename local macros in %s."),
			AnimBlueprintPath);
		return 1;
	}

	UE_LOG(LogTemp, Display, TEXT("[E4SheathedMacroNaming] Verified macro names in %s."),
		AnimBlueprintPath);
	return 0;
#else
	return 1;
#endif
}
