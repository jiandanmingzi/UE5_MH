// Copyright MHGZ Project. All Rights Reserved.

#include "Editor/MHGZE4TailStartTimeRepairCommandlet.h"

#if WITH_EDITOR

#include "Animation/AnimBlueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace UE::MHGZ::E4TailStartTimeRepair
{
constexpr TCHAR AnimBlueprintPath[] =
	TEXT("/Game/Blueprints/Characters/Demo/Animation/ABP_MH_Character.ABP_MH_Character");
const FName TailStartTimeName(TEXT("TailStartTime"));

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

UEdGraph* FindFunctionGraph(UAnimBlueprint& Blueprint, const FName GraphName)
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

bool RepairGraph(UAnimBlueprint& Blueprint, UEdGraph& Graph, const UEdGraphSchema_K2& Schema)
{
	TArray<UK2Node_VariableSet*> Setters;
	for (UEdGraphNode* Node : Graph.Nodes)
	{
		UK2Node_VariableSet* Setter = Cast<UK2Node_VariableSet>(Node);
		if (Setter && Setter->VariableReference.IsLocalScope()
			&& Setter->VariableReference.GetMemberName() == TailStartTimeName)
		{
			Setters.Add(Setter);
		}
	}

	if (Setters.Num() != 1)
	{
		UE_LOG(LogTemp, Error, TEXT("[E4TailStartTimeRepair] Expected one local TailStartTime setter in '%s'; found %d."),
			*Graph.GetName(), Setters.Num());
		return false;
	}

	UK2Node_VariableSet* Setter = Setters[0];
	UEdGraphPin* StaleValueInput = nullptr;
	UEdGraphPin* SourceOutput = nullptr;
	for (UEdGraphPin* Pin : Setter->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Input && Pin->PinName != TailStartTimeName
			&& Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec
			&& Pin->LinkedTo.Num() > 0)
		{
			StaleValueInput = Pin;
			SourceOutput = Pin->LinkedTo[0];
			break;
		}
	}
	if (!StaleValueInput || !SourceOutput)
	{
		UE_LOG(LogTemp, Error, TEXT("[E4TailStartTimeRepair] Could not find the saved duration-minus-tail source in '%s'."),
			*Graph.GetName());
		return false;
	}

	Setter->Modify();
	StaleValueInput->BreakAllPinLinks();
	Setter->RemovePin(StaleValueInput);
	UEdGraphPin* FreshValueInput = Setter->FindPin(TailStartTimeName, EGPD_Input);
	if (!FreshValueInput || !Schema.TryCreateConnection(SourceOutput, FreshValueInput))
	{
		UE_LOG(LogTemp, Error, TEXT("[E4TailStartTimeRepair] Failed to reconnect TailStartTime in '%s'."),
			*Graph.GetName());
		return false;
	}

	return true;
}

bool RepairBlueprint(UAnimBlueprint& Blueprint)
{
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	UEdGraph* Sheathed = FindFunctionGraph(Blueprint, TEXT("OnShthMmUpdate"));
	UEdGraph* Unsheathed = FindFunctionGraph(Blueprint, TEXT("OnUnShMmUpdate"));
	if (!Schema || !Sheathed || !Unsheathed)
	{
		return false;
	}

	Blueprint.Modify();
	Sheathed->Modify();
	Unsheathed->Modify();
	if (!RepairGraph(Blueprint, *Sheathed, *Schema) || !RepairGraph(Blueprint, *Unsheathed, *Schema))
	{
		return false;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(&Blueprint);
	FKismetEditorUtilities::CompileBlueprint(&Blueprint);
	return Blueprint.Status != BS_Error && SaveAsset(Blueprint);
}
}

#endif

UMHGZE4TailStartTimeRepairCommandlet::UMHGZE4TailStartTimeRepairCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMHGZE4TailStartTimeRepairCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	using namespace UE::MHGZ::E4TailStartTimeRepair;
	(void)Params;

	UAnimBlueprint* AnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, AnimBlueprintPath);
	if (!AnimBlueprint || !RepairBlueprint(*AnimBlueprint))
	{
		UE_LOG(LogTemp, Error, TEXT("[E4TailStartTimeRepair] Failed to repair %s."), AnimBlueprintPath);
		return 1;
	}

	UE_LOG(LogTemp, Display, TEXT("[E4TailStartTimeRepair] Repaired and saved %s."), AnimBlueprintPath);
	return 0;
#else
	return 1;
#endif
}
